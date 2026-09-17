"""Private-fixture desktop regression runs; never mutate an installed profile."""
import argparse
import copy
import json
import os
from pathlib import Path
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('executable', 'profile', 'config', 'topscreen', 'kokiri', 'output'):
        p.add_argument('--' + key, type=Path, required=True)
    p.add_argument('--sages', type=Path)
    p.add_argument('--race', type=Path)
    p.add_argument('--seconds', type=int, default=12)
    p.add_argument('--only', nargs='+')
    args = p.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    profile = json.loads(args.profile.read_text(encoding='utf-8-sig'))
    tokens = profile[1:] if isinstance(profile, list) else profile['arguments']
    resources = {'--title-plugin', '--a32-process-manifest', '--resource-root',
                 '--topscreen-texture-overrides'}
    base = []
    for index, token in enumerate(tokens):
        if token in resources:
            base += [token, tokens[index + 1].replace('${profile_dir}', str(args.profile.parent))]
    original = json.loads(args.config.read_text(encoding='utf-8-sig'))
    cases = [('kokiri-30', args.kokiri, 30, False),
             ('kokiri-60', args.kokiri, 60, False),
             ('kokiri-90', args.kokiri, 90, False),
             ('boot', None, 30, True), ('kokiri-advanced', args.kokiri, 60, True)]
    if args.sages:
        cases.append(('sages-toon', args.sages, 30, True))
    if args.race:
        cases.append(('race', args.race, 30, True))
    results = []
    for name, state, rate, capture in cases:
        if args.only and name not in args.only:
            continue
        root = args.output / name
        root.mkdir()
        config = copy.deepcopy(original)
        g = config['Graphics']
        g['FrameRate'] = {'Mode': {30: 'Original30', 60: 'Interpolated2x', 90: 'Interpolated3x'}[rate]}
        g['Output'] = {'Width': 1280, 'Height': 720, 'RefreshRate': 60}
        g['Window'] = {'Mode': 'Windowed', 'Display': 0}
        g['Presentation'] = {'VSync': capture}
        config.pop('Window', None)
        (root / 'config.json').write_text(json.dumps(config, indent=2))
        (root / 'topscreen.json').write_bytes(args.topscreen.read_bytes())
        (root / 'input.json').write_text(json.dumps({
            'schema': 'oot3d.native_game.input_timeline.v1', 'frame_origin': 'run',
            'segments': [{'start_frame': 0, 'end_frame_exclusive': 2147483647, 'buttons': []}]}))
        command = [str(args.executable), *base, '--renderer', 'nri', '--ui-profile', 'topscreen',
                   '--config', str(root / 'config.json'), '--topscreen-config', str(root / 'topscreen.json'),
                   '--save-data', str(root / 'savedata'), '--output', str(root / 'runtime.json'),
                   '--renderer-cache-directory', str(args.output / 'cache'),
                   '--gameplay-timing', 'native30_no_interpolation' if rate == 30 else 'native30_interpolated',
                   '--presentation-rate', str(rate), '--pica-parametric-tev',
                   '--input-timeline', str(root / 'input.json')]
        if state:
            command += ['--load-state', str(state)]
        if capture:
            command += ['--frames', '180', '--max-seconds', '60', '--fixed-delta-seconds', str(1 / rate),
                        '--screenshot', str(root / 'framebuffer.bmp'), '--screenshot-sequence',
                        '--screenshot-start-frame', '9' if name == 'boot' else '60', '--screenshot-interval', '60']
        else:
            command += ['--frames', '0', '--max-seconds', str(args.seconds)]
        env = os.environ.copy()
        for key in list(env):
            if key.startswith(('OOT3D_GRAPHICS_', 'OOT3D_PICA_', 'OOT3D_GRASS_', 'TRIAEVUM_AOT_')):
                env.pop(key)
        env.update(DISABLE_VULKAN_OBS_CAPTURE='1', TRIAEVUM_NRI_PIPELINE_LIBRARIES='0')
        (root / 'invocation.json').write_text(json.dumps(command, indent=2))
        with (root / 'stdout.log').open('w') as stdout, (root / 'stderr.log').open('w') as stderr:
            proc = subprocess.run(command, cwd=args.executable.parent, env=env,
                                  stdout=stdout, stderr=stderr, timeout=120)
        report = json.loads((root / 'runtime.json').read_text()) if (root / 'runtime.json').exists() else {}
        faults = report.get('compiled_functions', {}).get('whole_aot_memory_faults')
        window, clock = report.get('benchmark_window', {}), report.get('frame_rate', {})
        samples = clock.get('visual_sample_cadence', {})
        checks = {'exit': proc.returncode == 0, 'no_guest_faults': faults == 0,
                  'draws': report.get('draws_submitted', 0) > 0,
                  'no_throughput_override': window.get('throughput_mode') is False}
        if capture:
            checks['captures'] = bool(list(root.glob('*.bmp')))
            checks['frames'] = report.get('run_frames', 0) >= 180
        else:
            checks.update(real_clock=window.get('fixed_delta_seconds') is None,
                          limiter=report.get('realtime_pacing', {}).get('enabled') is True,
                          rate=clock.get('visual_presentation_rate_hz') == rate)
            if rate > 30:
                checks['intermediate_samples'] = samples.get('repeated_samples', 0) > 0
                checks['interpolated_draws'] = report.get('visual_interpolation', {}).get('interpolated_draws', 0) > 0
        results.append(dict(name=name, checks=checks, passed=all(checks.values()),
                            pacing=report.get('realtime_pacing'), clock=clock))
        (args.output / 'summary.json').write_text(json.dumps(results, indent=2))
        print(name, checks, flush=True)
    return 0 if results and all(r['passed'] for r in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
