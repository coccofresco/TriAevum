"""Build one diagnostic proxy object, then replay one captured game invocation.

Requires ABI-matching developer support library/headers and trusted AOT modules.
No title compilation or release-cache update. Whole-run times are diagnostic.
"""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time


def digest(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def batch_result(rows):
    trials = [r for r in rows if r and r[0].isdigit()]
    if (len(trials) != 2 or any(len(r) < 6 or r[4:6] != ['1', '1'] for r in trials)
            or not rows or rows[-1] != ['summary', '1', 'original_memory_untouched', '1']):
        return False, set()
    if [int(r[1]) for r in rows if r and r[0] == 'candidate_fast_hits'] != [1]:
        return False, set()
    return True, {int(r[1]) for r in rows if r and r[0] == 'visited_function'}


def main():
    p = argparse.ArgumentParser(__doc__)
    for name in ('compiler', 'support', 'include', 'baseline', 'executable',
                 'invocation', 'output'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--candidate', type=Path)
    p.add_argument('--oracle', type=Path, help='Frozen fallback for closed-family control/candidate modules')
    p.add_argument('--entry', type=lambda s: int(s, 16))
    p.add_argument('--selection', type=Path, help='Batch qualification, two replicas per root; not timing')
    p.add_argument('--occurrence', type=int, default=64)
    args = p.parse_args()
    targets = []
    if args.selection:
        if args.entry:
            p.error('Use either entry or selection')
        targets = sorted(set(int(e,16) for e in json.loads(args.selection.read_text())['roots']))
        if not targets or len(targets)>1024 or any(not 0<e<=0xFFFFFFFF or e%4 for e in targets):
            p.error('Invalid batch roots')
        args.entry = targets[0]
    if args.entry is None:
        p.error('Entry or selection required')
    if not 0 < args.entry <= 0xFFFFFFFF or args.entry % 4:
        p.error('Expected aligned nonzero A32 entry')
    if not 1 <= args.occurrence <= 8192:
        p.error('Occurrence must be between 1 and 8192')
    repo = Path(__file__).resolve().parents[3]
    here = Path(__file__).resolve().parent
    runtime = repo / 'tools/oot3d/native_game_runtime'
    a32 = repo / 'tools/oot3d/native_a32_runtime'
    args.output.mkdir(parents=True, exist_ok=False)
    args.output = args.output.resolve()
    source = here / 'aot_invocation_probe.cpp'
    obj, dll = args.output / 'probe.obj', args.output / 'probe.dll'
    started = time.monotonic()
    commands = [[str(args.compiler), '/nologo', '/c', '/MT', '/O2', '/EHsc',
        '/std:c++20', '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS',
        *(f'/I{v}' for v in (runtime, a32, a32 / 'upstream', args.include)),
        str(source), f'/Fo{obj}'],
        [str(args.compiler), '/nologo', '/LD', '/MT', str(obj), str(args.support),
         f'/Fe{dll}', '-fuse-ld=lld', '/link', '/NOIMPLIB', '/threads:2']]
    for command in commands:
        subprocess.run(command, check=True, timeout=120)
    build_seconds = time.monotonic() - started
    report = args.output / 'invocation.csv'
    env = dict(os.environ, TRIAEVUM_INVOCATION_BASELINE=str(args.baseline.resolve()),
               TRIAEVUM_INVOCATION_ENTRY=f'{args.entry:08X}',
               TRIAEVUM_INVOCATION_OCCURRENCE=str(args.occurrence),
               TRIAEVUM_INVOCATION_OUTPUT=str(report))
    env.pop('TRIAEVUM_INVOCATION_CANDIDATE', None)
    env.pop('TRIAEVUM_INVOCATION_TARGETS', None)
    if targets:
        env['TRIAEVUM_INVOCATION_TARGETS'] = ','.join(f'{e:08X}' for e in targets)
    if args.candidate:
        env['TRIAEVUM_INVOCATION_CANDIDATE'] = str(args.candidate.resolve())
    env.pop('TRIAEVUM_FAMILY_ORACLE', None)
    if args.oracle:
        env['TRIAEVUM_FAMILY_ORACLE'] = str(args.oracle.resolve())
    files = [source, args.baseline, args.support, args.executable, args.invocation, dll,
             runtime / 'oot3d_native_a32_memory.h', runtime / 'triaevum_title_whole_aot_abi.h']
    if args.candidate:
        files.append(args.candidate)
    if args.oracle:
        files.append(args.oracle)
    if args.selection:
        files.append(args.selection)
    metadata = {'diagnostic_only': True, 'a_a_control': not args.candidate,
                'batch_targets': [f'{e:08X}' for e in targets],
                'diagnostic_selection_env': {k: v for k, v in env.items()
                    if k.startswith(('TRIAEVUM_SKELETON_', 'TRIAEVUM_FAMILY_'))},
                'entry': f'{args.entry:08X}', 'occurrence': args.occurrence, 'build_seconds': build_seconds,
                'build_commands': commands,
                'sha256': {str(f): digest(f) for f in files}}
    (args.output / 'provenance.json').write_text(json.dumps(metadata, indent=2))
    subprocess.run([sys.executable, str(here / 'measure_prebackend.py'),
        '--invocation', str(args.invocation), '--executable', str(args.executable),
        '--diagnostic-plugin', str(dll), '--frames', '360', '--runs', '1',
        '--output', str(args.output / 'game')], env=env, check=True, timeout=150)
    if targets:
        passed, rejected, missing, visited = [], [], [], set()
        for entry in targets:
            path=Path(str(report)+f'.{entry}.csv')
            if not path.exists():
                missing.append(f'{entry:08X}')
                continue
            rows=list(csv.reader(path.read_text().splitlines()))
            valid, covered = batch_result(rows)
            (passed if valid else rejected).append(f'{entry:08X}')
            if valid:
                visited.update(covered)
        result={'qualification_only':True,'passed_roots':passed,'rejected_roots':rejected,
                'uncaptured_roots':missing,'visited_in_matching_candidates':[f'{e:08X}' for e in sorted(visited)]}
        (args.output/'batch.json').write_text(json.dumps(result,indent=2))
        print(json.dumps({k:len(v) for k,v in result.items() if isinstance(v,list)}))
        if not passed:
            raise RuntimeError('No batch root qualified')
        return
    if not report.exists():
        raise RuntimeError('Target invocation was not captured; no successful replay')
    rows = list(csv.reader(report.read_text().splitlines()))
    trials = [r for r in rows if r and r[0].isdigit()]
    if len(trials) != 16 or any(r[4:6] != ['1', '1'] for r in trials) or (
            rows[-1] != ['summary', '1', 'original_memory_untouched', '1']):
        raise RuntimeError('Invocation replay rejected; inspect diagnostic output')
    hits = [int(r[1]) for r in rows if r and r[0] == 'candidate_fast_hits']
    if args.candidate and hits and hits != [8]:
        raise RuntimeError(f'Candidate fast path ran {hits}, expected 8; equivalence may only validate fallback')
    print('16 matching real-input replays; not a whole-game speedup measurement.')


if __name__ == '__main__':
    main()
