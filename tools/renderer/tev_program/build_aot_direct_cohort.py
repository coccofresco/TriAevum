"""Private sparse-link experiment: direct closed families, no ABI handoff.

Only selected root shards are rebuilt. No release cache is written. A matched
control is emitted with the same generator so compiler changes are not credited
to the experiment. Generated title sources belong outside the public repo.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

from build_aot_family_probe import aot, closure, memory_observer, REPO

sys.path.insert(0, str(REPO / 'tools'))
from triaevum_release import whole_aot_object_cache as cache


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def render_family(program, root, functions):
    """Local calls share the existing frame, promoted registers and budget."""
    entries = closure(program, root, 512)
    members = [functions[e] for e in entries]
    records = {f['entry']: f for f in program['functions']}
    symbols = {f.entry: aot._symbol(f.name, f.entry) for f in members}
    lines = [f'namespace Direct_{root:08X} {{']
    lines += aot._render_function_declarations(members)
    for f in members:
        if any(memory_observer(i.raw, True) for b in f.blocks for i in b.instructions):
            raise ValueError(f'Observable memory/system instruction: {f.entry:08X}')
        tails = sorted({c['target'] for c in records[f.entry]['tail_calls']})
        lines += aot._render_function(f, symbols, frozenset(), callback_free=True,
                                     symbol_suffix='_body' if tails else '')
        if tails:
            lines += [f'Oot3dWholeAotFlow Execute_{symbols[f.entry]}(',
                      'Oot3dWholeAotFrame& f, Oot3dWholeAotContext& c,',
                      'Oot3dAotArchitecturalState& s, uint32_t pc) {',
                      f'auto flow = Execute_{symbols[f.entry]}_body(f,c,s,pc);',
                      'if (flow.Kind == Oot3dWholeAotFlowKind::Branch) { switch(flow.Pc) {']
            for tail in tails:
                lines += [f'case 0x{tail:08X}U: return Execute_{symbols[tail]}(f,c,s,flow.Pc);']
            lines += ['default: break;', '}}', 'return flow;', '}']
    lines += ['bool Eligible(const Oot3dWholeAotContext& c) {',
              'if (!c.BlockEntry) return true;',
              'if (!c.BlockEntryPcs || !c.BlockEntryPcCount) return false;',
              'const auto end = c.BlockEntryPcs + c.BlockEntryPcCount;']
    for f in members:
        lo = min(b.pc for b in f.blocks)
        hi = max(i.pc + 4 for b in f.blocks for i in b.instructions)
        lines += [f'{{ auto p = std::lower_bound(c.BlockEntryPcs,end,0x{lo:08X}U);',
                  f'if (p != end && *p < 0x{hi:08X}U) return false; }}']
    lines += ['return true;', '}', '}']
    return lines


def render_shard(program, members, functions, external, roots):
    symbols = {e: aot._symbol(f.name, e) for e, f in functions.items()}
    declarations = {f.entry for f in members} | {
        t for f in members for _, t in f.direct_calls if t in functions}
    lines = ['#include "oot3d_whole_aot_generated_internal.h"',
             '#include "oot3d_native_a32_memory.h"', '#include <atomic>',
             '#include <algorithm>', 'namespace Oot3dNativeGame::GeneratedWholeAot {',
             'namespace a32 = oot3d::recomp::a32;']
    lines += aot._render_function_declarations([functions[e] for e in sorted(declarations)])
    for f in members:
        if f.entry not in roots:
            lines += aot._render_function(f, symbols, external)
            continue
        lines += render_family(program, f.entry, functions)
        symbol = symbols[f.entry]
        lines += aot._render_function(f, symbols, external, symbol_suffix='_Observed')
        lines += [f'Oot3dWholeAotFlow Execute_{symbol}(',
                  'Oot3dWholeAotFrame& f, Oot3dWholeAotContext& c,',
                  'Oot3dAotArchitecturalState& s, uint32_t pc) {',
                  f'if (pc == 0x{f.entry:08X}U && Direct_{f.entry:08X}::Eligible(c)) {{',
                  '// Restore the callback even on a memory exception or budget exit.',
                  'struct Restore { Oot3dWholeAotContext& c; a32::BlockEntryCallback saved;',
                  '~Restore() { c.BlockEntry = saved; } } restore{c,c.BlockEntry};',
                  'c.BlockEntry = nullptr;',
                  f'return Direct_{f.entry:08X}::Execute_{symbol}(f,c,s,pc);',
                  '}', f'return Execute_{symbol}_Observed(f,c,s,pc);', '}']
    lines += ['}']
    return '\n'.join(lines)


def verify_dependencies(record, generated, include):
    deps = cache.default_dependency_files(REPO, generated, include)
    old = {d['role']: d for d in record['dependencies']}
    helper = 'repo/tools/oot3d/native_game_runtime/oot3d_native_a32_memory_region.h'
    deps = [d for d in deps if d.role != helper]
    if set(old) != {d.role for d in deps}:
        raise ValueError('Frozen common dependency set differs')
    friend = ('    // A call-free AOT region may retain views until its next observable exit.\n'
              '    // This does not change the memory object layout or the title-module ABI.\n'
              '    friend class NativeA32UnobservedMemory;\n').encode()
    for dep in deps:
        data = dep.path.read_bytes()
        if dep.role.endswith('/oot3d_native_a32_memory.h'):
            data = data.replace(friend, b'')
        if hashlib.sha256(data).hexdigest() != old[dep.role]['sha256']:
            raise ValueError(f'Unapproved common dependency change: {dep.role}')


def main():
    p = argparse.ArgumentParser(__doc__)
    for name in ('program', 'code', 'baseline-manifest', 'baseline-archive',
                 'generated-headers', 'compiler', 'archiver', 'support', 'include', 'output'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--root', type=lambda s: int(s, 16), action='append', required=True)
    p.add_argument('--max-shards', type=int, default=1)
    p.add_argument('--thinlto-cache', type=Path, required=True,
                   help='Existing populated developer cache, not a new per-run directory')
    args = p.parse_args()
    if args.output.resolve().is_relative_to(REPO.resolve()):
        raise ValueError('Generated title code must remain outside the public repository')
    if not args.thinlto_cache.is_dir() or not any(args.thinlto_cache.iterdir()):
        raise ValueError('Populate a developer ThinLTO cache before sparse experiments')
    started = time.monotonic()
    baseline = json.loads(args.baseline_manifest.read_text())
    record = json.loads(args.baseline_archive.with_suffix('.build.json').read_text())
    program = json.loads(args.program.read_text())
    if (digest(args.code) != baseline['code_sha256'] or
            digest(args.program) != baseline['program_sha256'] or
            baseline['code_sha256'] != record['generated_code_sha256'] or
            baseline['program_sha256'] != record['generated_program_sha256']):
        raise ValueError('Frozen program/code identity differs')
    roots = set(args.root)
    selected = [s for s in baseline['shards'] if roots.intersection(s['entries'])]
    if not selected or len(selected) > args.max_shards:
        raise ValueError(f'Refusing {len(selected)} shards (budget {args.max_shards})')
    if not roots.issubset({e for s in selected for e in s['entries']}):
        raise ValueError('Unknown root')
    toolchain = cache.NativeToolchain(args.compiler, args.archiver)
    sysroot = cache.compiler_sysroot(toolchain.compiler, toolchain.sysroot)
    tool_id, toolchain = cache._toolchain_identity(toolchain, sysroot)
    if tool_id != record['toolchain_identity_sha256']:
        raise ValueError('Frozen toolchain differs')
    verify_dependencies(record, args.generated_headers, args.include)
    sources = sorted(n for n in baseline['files'] if n.endswith('.cpp'))
    if len(sources) != len(record['objects']):
        raise ValueError('Frozen source/object count differs')
    object_dir = args.baseline_archive.parent.parent / 'objects'
    objects = {}
    original_flags = cache._compile_arguments(toolchain, args.generated_headers, REPO, args.include, sysroot)
    for name, key in zip(sources, record['objects']):
        expected = cache.source_object_key(baseline['files'][name],
            record['dependency_identity_sha256'], tool_id, original_flags)
        if key != expected:
            raise ValueError(f'Frozen object/source association differs: {name}')
        obj = object_dir / key[:2] / (key + '.obj')
        meta = json.loads(obj.with_suffix('.json').read_text())
        if meta['sha256'] != digest(obj):
            raise ValueError(f'Changed frozen object: {name}')
        objects[name] = obj
    functions, external = aot._load_functions(program, {
        'format': aot.SELECTION_FORMAT,
        'functions': baseline['functions'],
        'external_functions': [{'entry': e} for e in baseline['external_functions']]},
        args.code.read_bytes())
    functions = {f.entry: f for f in functions}
    all_members = sorted({e for root in roots for e in closure(program, root, 512)})
    if set(all_members).intersection(external):
        raise ValueError('Cohort crosses existing native owner')
    args.output.mkdir(parents=True, exist_ok=False)
    commands = []
    reports = {}
    for mode in ('control', 'candidate'):
        out = args.output / mode
        out.mkdir()
        generated = out / 'generated'
        generated.mkdir()
        for name in baseline['files']:
            if name.endswith('.h'):
                source = args.generated_headers / name
                if digest(source) != baseline['files'][name]:
                    raise ValueError(f'Generated header differs: {name}')
                (generated / name).write_bytes(source.read_bytes())
        flags = cache._compile_arguments(toolchain, generated, REPO, args.include, sysroot)
        linked = dict(objects)
        hashes = {}
        for shard in selected:
            path = generated / shard['file']
            path.write_text(render_shard(program, [functions[e] for e in shard['entries']],
                                        functions, external, roots if mode == 'candidate' else set()))
            obj = out / (shard['file'] + '.obj')
            command = [str(args.compiler), *flags, f'/Fo{obj}', str(path)]
            commands.append(command)
            print(f'{mode}: compile {path.name}', flush=True)
            subprocess.run(command, check=True, timeout=180,
                           env=cache.build_environment() if sysroot else None)
            linked[shard['file']] = obj
            hashes[path.name] = digest(path)
        wrapper = out / 'wrapper.obj'
        # Keep the exported ABI wrapper outside LTO, matching the frozen sparse
        # build. Importing it changes the optimization graph of the whole title.
        command = [str(args.compiler), *(f for f in flags if f != '-flto=thin'), f'/Fo{wrapper}',
                   str(REPO / 'tools/oot3d/native_game_runtime/triaevum_title_whole_aot_plugin.cpp')]
        commands.append(command)
        subprocess.run(command, check=True, timeout=120,
                       env=cache.build_environment() if sysroot else None)
        response = out / 'link.rsp'
        response.write_text('\n'.join(f'"{v}"' for v in (wrapper, *linked.values(), args.support)))
        dll = out / 'triaevum_title_aot.dll'
        command = [str(args.compiler), '/nologo', '/LD', '/MT', f'/Fe{dll}', '@'+str(response),
                   '-fuse-ld=lld', '/link', '/NOIMPLIB', '/OPT:REF', '/OPT:ICF', '/INCREMENTAL:NO',
                   '/Brepro', '/threads:2', f'/lldltocache:{args.thinlto_cache}']
        commands.append(command)
        print(f'{mode}: sparse link, {len(objects)-len(selected)} reused objects', flush=True)
        subprocess.run(command, check=True, timeout=900)
        reports[mode] = {'dll': str(dll), 'dll_sha256': digest(dll), 'sources': hashes}
    report = {'developer_only': True, 'roots': sorted(roots), 'functions': all_members,
              'compiled_shards': [s['file'] for s in selected], 'commands': commands,
              'reused_objects': len(objects)-len(selected), 'variants': reports,
              'elapsed_seconds': time.monotonic()-started,
              'hashes': {str(f): digest(f) for f in (args.program, args.code, args.support,
                  args.baseline_manifest, args.baseline_archive.with_suffix('.build.json'),
                  Path(__file__), Path(aot.__file__))}}
    (args.output / 'build.json').write_text(json.dumps(report, indent=2))
    print(json.dumps({'elapsed_seconds': report['elapsed_seconds'], 'functions': len(all_members)}))


if __name__ == '__main__':
    main()
