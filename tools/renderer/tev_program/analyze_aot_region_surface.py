"""Offline structural cohort sizing. Sample coverage is NOT time or removable cost."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct


def memory_observer(raw, allow_vfp_transport=False):
    # Same conservative exclusions as the existing scoped-memory prototype.
    return ((raw & 0x0F000FF0) == 0x01000F90 or
            (raw & 0x0FB00FF0) == 0x01000090 or
            ((raw & 0x0F000010) == 0x0E000010 and
             not (allow_vfp_transport and ((raw >> 8) & 15) in (10, 11))) or
            (raw & 0x0F000000) == 0x0F000000)


def code_hazards(program, code, allow_vfp_transport=False):
    if hashlib.sha256(code).hexdigest() != program['code_sha256']:
        raise ValueError('Code does not match the structural program')
    result = set()
    for block in program['blocks']:
        begin, end = block['pc'] - program['base'], block['end_pc'] - program['base']
        if begin < 0 or end > len(code) or end <= begin or (end - begin) % 4:
            raise ValueError('Invalid ARM block extent')
        if any(memory_observer(raw, allow_vfp_transport) for (raw,) in struct.iter_unpack('<I', code[begin:end])):
            result.add(block['id'])
    return result


def classify(program, external, hazardous_blocks):
    functions = {f['entry']: f for f in program['functions']}
    blocks = {b['id']: b for b in program['blocks']}
    calls, reasons = {}, {}
    for entry, function in functions.items():
        callees = {c['target'] for kind in ('direct_calls', 'tail_calls')
                   for c in function.get(kind, [])}
        calls[entry] = callees
        why = set()
        if entry in external:
            why.add('external_owner')
        if not function.get('closed_static_cfg') or function.get('unresolved_static_edges'):
            why.add('open_cfg')
        if function.get('indirect_sites'):
            why.add('indirect_control')
        if not function['blocks'] or any(b not in blocks for b in function['blocks']):
            why.add('missing_blocks')
        if set(function['blocks']) & hazardous_blocks:
            why.add('memory_or_system_observer')
        pcs = {blocks[b]['pc'] for b in function['blocks'] if b in blocks}
        for bid in function['blocks']:
            for edge in blocks.get(bid, {}).get('successors', []):
                kind = edge['kind']
                if kind not in {'return', 'branch', 'fallthrough', 'resume', 'direct_call'}:
                    why.add('other_control_boundary')
                elif kind != 'return':
                    target = edge.get('target')
                    if target not in pcs and target not in callees:
                        why.add('unaccounted_edge')
        if not callees <= functions.keys():
            why.add('missing_callee')
        reasons[entry] = why

    leaves = {entry for entry in functions if not reasons[entry] and not calls[entry]}
    closed = set(leaves)
    # A least fixed point intentionally rejects recursive call cycles.
    while True:
        additions = {entry for entry in functions if entry not in closed
                     and not reasons[entry] and calls[entry] <= closed}
        if not additions:
            break
        closed.update(additions)
    for entry in functions.keys() - closed:
        if not reasons[entry]:
            reasons[entry].add('callee_boundary_or_recursion')
    return functions, calls, reasons, leaves, closed


def summarize(program, analyses, external=(), hazardous_blocks=()):
    functions, calls, reasons, leaves, closed = classify(program, set(external), set(hazardous_blocks))
    direct = Counter()
    total = 0
    per_run = []
    paths = []
    for analysis in analyses:
        run = Counter()
        for row in analysis['direct_owners']:
            run[int(row['entry'], 16)] += row['count']
        count = analysis['aot_leaf_samples']
        if sum(run.values()) > count:
            raise ValueError('Direct owner samples are not exclusive')
        total += count
        direct.update(run)
        run_paths = [(tuple(int(e, 16) for e in row['entries']), row['count'])
                     for row in analysis.get('paths', [])]
        path_leaves = Counter()
        for entries, observations in run_paths:
            if not entries or observations < 0:
                raise ValueError('Invalid observed stack path')
            path_leaves[entries[0]] += observations
        if any(n > run[e] for e, n in path_leaves.items()):
            raise ValueError('Stack paths exceed exclusive direct owner observations')
        paths.extend(run_paths)
        per_run.append({'aot_samples': count,
                        'leaf_samples': sum(n for e, n in run.items() if e in leaves),
                        'closed_samples': sum(n for e, n in run.items() if e in closed)})

    def cohort(entries):
        active = entries & direct.keys()
        samples = sum(direct[e] for e in active)
        categories = Counter()
        for analysis in analyses:
            for row in analysis['direct_owners']:
                if int(row['entry'], 16) in entries:
                    categories[row['category']] += row['count']
        return {'static_functions': len(entries), 'sampled_functions': len(active),
                'exclusive_samples': samples, 'share_of_aot_observations': samples / total if total else 0,
                'categories': dict(categories)}

    # Do not credit a root with shared callee samples reached through other roots.
    closures = {}
    def closure(entry):
        if entry not in closures:
            value = {entry}
            for callee in calls[entry]:
                value.update(closure(callee))
            closures[entry] = value
        return closures[entry]
    ranked = []
    coverage = {}
    for entry in closed:
        members = closure(entry)
        n = sum(direct[e] for e in members)
        seen = {i for i, (entries, _) in enumerate(paths)
                if entry in entries and entries[0] in members}
        under_root = sum(paths[i][1] for i in seen)
        coverage[entry] = seen
        if under_root:
            ranked.append({'entry': f'{entry:08X}', 'name': functions[entry]['name'],
                           'members': len(members), 'direct_samples': direct[entry],
                           'closure_samples_all_callers': n,
                           'observed_samples_under_root': under_root})
    ranked.sort(key=lambda r: (-r['observed_samples_under_root'], r['members'], r['entry']))
    selected, covered = [], set()
    remaining = {int(row['entry'], 16) for row in ranked}
    while remaining and len(selected) < 16:
        entry = max(remaining, key=lambda e: (sum(paths[i][1] for i in coverage[e] - covered),
                                              -len(closure(e)), -e))
        gain = sum(paths[i][1] for i in coverage[entry] - covered)
        if not gain:
            break
        covered.update(coverage[entry])
        remaining.remove(entry)
        selected.append({'entry': f'{entry:08X}', 'name': functions[entry]['name'],
                         'members': len(closure(entry)), 'new_samples': gain,
                         'cumulative_distinct_samples': sum(paths[i][1] for i in covered)})
    rejected = []
    for entry, n in direct.most_common():
        if entry in closed:
            continue
        f = functions.get(entry, {})
        rejected.append({'entry': f'{entry:08X}', 'name': f.get('name'), 'samples': n,
                         'reasons': sorted(reasons.get(entry, {'owner_not_in_program'}))})
    return {'purpose': 'structural preselection, not correctness approval or speedup estimate',
            'limitations': ['intrusive sample observations, not exclusive CPU time',
                'actual callback PCs, aliasing, fault order, budgets and floating-point contracts still unqualified',
                'closed-family rows overlap; only greedy selection deduplicates across roots',
                'closure_samples_all_callers is not credited to the root activation'],
            'aot_observations': total, 'resolved_owner_observations': sum(direct.values()),
            'per_run': per_run, 'call_free': cohort(leaves), 'closed_direct_calls': cohort(closed),
            'added_by_direct_call_closures': cohort(closed - leaves),
            'ranked_closed_families': ranked[:30], 'greedy_observed_roots': selected,
            'rejected_sampled_owners': rejected[:30]}


def main():
    p = argparse.ArgumentParser(__doc__)
    for name in ('program', 'code', 'manifest', 'output'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--analysis', type=Path, action='append', required=True)
    p.add_argument('--allow-vfp-transport', action='store_true',
                   help='Structural sizing only: CP10/11 transport is not an external memory observer')
    args = p.parse_args()
    program = json.loads(args.program.read_text())
    manifest = json.loads(args.manifest.read_text())
    if program['code_sha256'] != manifest['code_sha256']:
        raise ValueError('Manifest/code mismatch')
    if hashlib.sha256(args.program.read_bytes()).hexdigest() != manifest['program_sha256']:
        raise ValueError('Manifest/program mismatch')
    analyses = [json.loads(path.read_text()) for path in args.analysis]
    module_hashes = {a['module_sha256'] for a in analyses if 'module_sha256' in a}
    if len(module_hashes) > 1:
        raise ValueError('Sample reports refer to different modules')
    report = summarize(program, analyses, manifest['external_functions'],
                       code_hazards(program, args.code.read_bytes(), args.allow_vfp_transport))
    report['allow_vfp_transport_for_structural_sizing'] = args.allow_vfp_transport
    report['inputs'] = {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                        for path in (args.program, args.code, args.manifest, *args.analysis)}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2))
    print(json.dumps({k: report[k] for k in ('aot_observations', 'call_free', 'closed_direct_calls',
                                          'added_by_direct_call_closures', 'per_run')}, indent=2))


if __name__ == '__main__':
    main()
