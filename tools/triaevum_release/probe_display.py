"""Exercise the shipped renderer's F1 presentation transactions on private data."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys


def sequence(case):
    if case == 'rapid':
        return [dict(frame=240 + i * 12, mode=i % 3, width=1280, height=720)
                for i in range(24)]
    steps = []
    for frame, mode, width, height in (
        (240, 1, 1280, 720), (330, 2, 1280, 720), (420, 0, 1280, 720),
        (510, 2, 1920, 1080), (600, 1, 1920, 1080), (690, 0, 1280, 720),
        (780, 2, 1234, 777),
    ):
        steps.extend([dict(frame=frame, mode=mode, width=width, height=height),
                      dict(frame=frame + 15, confirm=True)])
    steps.append(dict(frame=900, mode=1))
    return steps


def verify(case, log, runtime, gpu):
    steps = sequence(case)
    observed_steps = [int(n) for n in re.findall(r'DISPLAY diagnostic frame=(\d+)', log)]
    if observed_steps != [s['frame'] for s in steps]:
        raise ValueError('Display sequence did not execute at every requested frame')
    frames = gpu.get('frames', [])
    if not frames or runtime.get('run_frames', 0) < steps[-1]['frame'] + 60:
        raise ValueError('No sustained rendering after the final display transition')
    if runtime.get('compiled_functions', {}).get('whole_aot_memory_faults') != 0:
        raise ValueError('Title execution fault or missing fault telemetry')
    if len(frames) < steps[-1]['frame'] + 60:
        raise ValueError('Incomplete GPU diagnostics: increase the retained frame limit')
    for prefix in ('vulkan', 'nri'):
        if any(f.get(prefix + '_validation_error_count', 0) for f in frames):
            raise ValueError(prefix + ' validation reported errors')
    counts = {k: sum(f.get(k, 0) for f in frames) for k in (
        'presentation_apply_count', 'presentation_rollback_count',
        'presentation_apply_failure_count')}
    modes = sorted({f['presentation_window_mode'] for f in frames})
    if case == 'recovery':
        if counts['presentation_apply_failure_count'] < 1 or modes != [0]:
            raise ValueError('Injected failure did not recover to the initial windowed state')
    else:
        if modes != [0, 1, 2] or counts['presentation_apply_count'] < 7:
            raise ValueError('Requested window modes were not actually exercised')
        if counts['presentation_apply_failure_count']:
            raise ValueError('A display transition failed')
        if case == 'modes' and counts['presentation_rollback_count'] < 1:
            raise ValueError('Unconfirmed display change did not roll back')
    return dict(case=case, frames=runtime['run_frames'], modes=modes, **counts)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('installation', type=Path)
    parser.add_argument('executable', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--case', choices=['modes', 'rapid', 'recovery'], default='modes')
    parser.add_argument('--seconds', type=int, default=75)
    parser.add_argument('--load-state', type=Path)
    parser.add_argument('--validation', action='store_true')
    args = parser.parse_args()
    env = os.environ.copy()
    env['TRIAEVUM_DISPLAY_DIAGNOSTIC_SEQUENCE'] = json.dumps(sequence(args.case))
    env.pop('OOT3D_GRAPHICS_TEST_FAIL_PRESENTATION_APPLY', None)
    if args.case == 'recovery':
        env['OOT3D_GRAPHICS_TEST_FAIL_PRESENTATION_APPLY'] = '1'
    if args.validation:
        env['OOT3D_VULKAN_VALIDATION'] = '1'
    command = [sys.executable, '-m', 'tools.triaevum_release.probe_renderer',
               str(args.installation), str(args.executable), str(args.output),
               '--seconds', str(args.seconds), '--capture-interval', '240']
    if args.load_state:
        command += ['--load-state', str(args.load_state)]
    result = subprocess.run(command, env=env)
    if result.returncode:
        parser.exit(1, 'Display probe crashed or timed out; inspect launch.log\n')
    try:
        report = verify(args.case, (args.output / 'launch.log').read_text(errors='replace'),
                        json.loads((args.output / 'runtime.json').read_text()),
                        json.loads((args.output / 'gpu.json').read_text()))
        if args.validation and not any(f.get('vulkan_validation_enabled') for f in
                json.loads((args.output / 'gpu.json').read_text())['frames']):
            raise ValueError('Requested Vulkan validation was not enabled')
    except (ValueError, OSError, KeyError) as exc:
        parser.exit(1, f'Display verification failed: {exc}\n')
    (args.output / 'display-verification.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
