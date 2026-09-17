"""Select observed closed call groups; static membership is not replay coverage."""
import argparse
import hashlib
import json
from pathlib import Path

from analyze_aot_region_surface import classify, code_hazards


def select(functions, calls, closed, paths, minimum):
    if minimum < 1:
        raise ValueError('Minimum must be positive')
    closures = {}
    def members(entry):
        if entry not in closures:
            result = {entry}
            for callee in calls[entry]:
                result.update(members(callee))
            closures[entry] = result
        return closures[entry]
    observed = {e for entries, count in paths if count > 0 for e in entries} & closed
    support = {e: {i for i, (entries, count) in enumerate(paths)
                   if count > 0 and e in entries} for e in observed}
    selected, included, covered = [], set(), set()
    while len(included) < minimum:
        eligible = [e for e in observed if members(e) - included]
        if not eligible:
            raise ValueError(f'Observed closed groups cover only {len(included)} functions')
        root = max(eligible, key=lambda e: (
            sum(paths[i][1] for i in support[e] - covered),
            len(members(e) - included), -e))
        selected.append(root)
        included.update(members(root))
        covered.update(support[root])
    return {'roots': [f'{e:08X}' for e in selected],
            'functions': [f'{e:08X}' for e in sorted(included)],
            'observed_samples_under_roots': sum(paths[i][1] for i in covered),
            'groups': [{'root': f'{e:08X}', 'name': functions[e]['name'],
                        'members': len(members(e)),
                        'observations': sum(paths[i][1] for i in support[e])}
                       for e in selected]}


def main():
    parser = argparse.ArgumentParser(__doc__)
    for name in ('program', 'code', 'manifest', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--analysis', type=Path, action='append', required=True)
    parser.add_argument('--minimum', type=int, default=300)
    args = parser.parse_args()
    program = json.loads(args.program.read_text())
    manifest = json.loads(args.manifest.read_text())
    if hashlib.sha256(args.program.read_bytes()).hexdigest() != manifest['program_sha256']:
        raise ValueError('Program/manifest mismatch')
    if program['code_sha256'] != manifest['code_sha256']:
        raise ValueError('Manifest code mismatch')
    functions, calls, _, _, closed = classify(program, set(manifest['external_functions']),
                                             code_hazards(program, args.code.read_bytes(), True))
    analyses = [json.loads(p.read_text()) for p in args.analysis]
    hashes = {a['module_sha256'] for a in analyses if 'module_sha256' in a}
    if len(hashes) > 1:
        raise ValueError('Mixed profile modules')
    paths = [(tuple(int(e, 16) for e in row['entries']), row['count'])
             for a in analyses for row in a.get('paths', [])]
    report = select(functions, calls, closed, paths, args.minimum)
    report.update(developer_only=True, minimum=args.minimum,
                  scope='Static selection from observed callers; not execution or performance validation',
                  program_sha256=manifest['program_sha256'], code_sha256=program['code_sha256'],
                  inputs={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in
                          (args.program, args.code, args.manifest, *args.analysis)})
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2))
    print(json.dumps({'roots': len(report['roots']), 'functions': len(report['functions']),
                      'observations': report['observed_samples_under_roots']}))


if __name__ == '__main__':
    main()
