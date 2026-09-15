"""Measure real-time host presentation intervals, not unlimited throughput or GPU scanout."""
import argparse
import json
import os
import re
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--invocation', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=16)
    parser.add_argument('--config', type=Path, help='Use this graphics profile without modifying it')
    parser.add_argument('--load-state', type=Path, help='Override fixture state; retain runtime timing validation')
    parser.add_argument('--warmup-frames', type=int, default=0,
                        help='Exclude this many completed presentations, not elapsed seconds')
    parser.add_argument('--no-scenario', action='store_true', help='Load the checkpoint without scenario injection')
    parser.add_argument('--from-start', action='store_true', help='Boot without a savestate')
    parser.add_argument('--inventory', action='store_true', help='Diagnostic source inventory; adds CPU overhead')
    parser.add_argument('--diagnostics', action='store_true', help='Bounded renderer phase attribution; not a clean timing run')
    parser.add_argument('--rates', type=int, nargs='+', choices=(30, 60, 90), default=[30, 60, 90])
    args = parser.parse_args()
    if args.seconds < 8:
        parser.error('need at least eight seconds, including warmup')
    if args.warmup_frames < 0:
        parser.error('warmup must be nonnegative')
    if args.from_start and args.load_state:
        parser.error('choose either boot or a checkpoint')
    fixture = json.loads(args.invocation.read_text(encoding='utf-8-sig'))
    base = fixture if isinstance(fixture, list) else [fixture['executable'], *fixture['arguments']]
    args.output.mkdir(parents=True, exist_ok=False)
    results = []
    for rate in args.rates:
        root = args.output / str(rate)
        root.mkdir()
        command = [base[0]]
        tokens = iter(base[1:])
        values = {'--frames', '--max-seconds', '--output', '--renderer-cache-directory',
                  '--fixed-delta-seconds', '--gameplay-timing', '--presentation-rate',
                  '--benchmark-warmup-frames', '--screenshot', '--screenshot-start-frame',
                  '--screenshot-interval', '--pica-aot-shader-pack', '--save-state', '--save-state-frame',
                  '--pica-effective-shader-inventory', '--pica-semantic-trace'}
        flags = {'--throughput-benchmark', '--extended-diagnostics', '--screenshot-sequence',
                 '--pica-aot-shader-strict', '--pica-parametric-tev', '--disable-visual-interpolation'}
        if args.load_state or args.from_start:
            values.add('--load-state')
        if args.no_scenario:
            values.update(('--scenario', '--scenario-catalog'))
        for token in tokens:
            if token in values:
                next(tokens)
            elif token in flags:
                continue
            elif token in ('--config', '--topscreen-config', '--save-data'):
                source = Path(next(tokens))
                if token == '--config' and args.config:
                    source = args.config
                dest = root / token[2:]
                if source.is_dir():
                    shutil.copytree(source, dest)
                else:
                    shutil.copy2(source, dest)
                if token == '--config':
                    config = json.loads(dest.read_text(encoding='utf-8-sig'))
                    config['Graphics']['Preset'] = 'Custom'
                    config['Graphics']['FrameRate']['Mode'] = {30: 'Original30', 60: 'Interpolated2x', 90: 'Interpolated3x'}[rate]
                    config['Graphics']['Presentation']['VSync'] = False
                    dest.write_text(json.dumps(config, indent=2))
                command.extend((token, str(dest.resolve())))
            else:
                command.append(token)
        command.extend(('--frames', '0', '--max-seconds', str(args.seconds),
                        '--gameplay-timing', 'native30_no_interpolation' if rate == 30 else 'native30_interpolated',
                        '--presentation-rate', str(rate), '--benchmark-warmup-frames', str(args.warmup_frames),
                        '--output', str((root/'runtime.json').resolve()),
                        '--renderer-cache-directory', str((root/'cache').resolve()), '--pica-parametric-tev'))
        if args.load_state:
            command.extend(('--load-state', str(args.load_state.resolve())))
        if args.inventory:
            command.extend(('--pica-effective-shader-inventory', str((root/'shaders.json').resolve())))
        env = os.environ.copy()
        for key in ('OOT3D_VULKAN_DIAGNOSTICS_PATH', 'OOT3D_PICA_AOT_SHADER_PACK',
                    'OOT3D_PICA_EFFECTIVE_SHADER_INVENTORY', 'TRIAEVUM_FRAME_START_TIMING'):
            env.pop(key, None)
        env.update(TRIAEVUM_PACING_TRACE='1', TRIAEVUM_NRI_PIPELINE_LIBRARIES='1',
                   OOT3D_VULKAN_VALIDATION='0', OOT3D_GRAPHICS_NRI_PICA_DRAWS='1',
                   OOT3D_GRAPHICS_PICA_DYNAMIC_RENDERING='1')
        if args.diagnostics:
            command.append('--extended-diagnostics')
            env['OOT3D_VULKAN_DIAGNOSTICS_PATH'] = str((root/'renderer.json').resolve())
            env['OOT3D_VULKAN_DIAGNOSTICS_MAX_FRAMES'] = str(args.seconds * rate + 8)
        (root/'invocation.json').write_text(json.dumps(command, indent=2))
        with (root/'stdout.log').open('w') as out, (root/'stderr.log').open('w') as err:
            subprocess.run(command, cwd=Path(base[0]).parent, env=env, stdout=out, stderr=err,
                           timeout=args.seconds + 60, check=True)
        report = json.loads((root/'runtime.json').read_text())
        window = report['benchmark_window']
        if (window['throughput_mode'] or not window['pacing_enabled'] or window['vsync'] or
                window['fixed_delta_seconds'] is not None or report['frame_rate']['visual_presentation_rate_hz'] != rate):
            raise RuntimeError(f'invalid real-time measurement: {window}')
        diagnostics = (root/'stderr.log').read_text(errors='replace')
        results.append({'rate': rate, 'window': window, 'phases': report['phase_timing'],
                        'pacing': report['realtime_pacing'], 'frame_rate': report['frame_rate'],
                        'warmup_basis': 'completed_presentations_not_wall_seconds',
                        'inventory_instrumentation': args.inventory,
                        'renderer_instrumentation': args.diagnostics,
                        'grass_instrumentation': 'OOT3D_GRASS_DIAGNOSTICS' in env,
                        'shader_diagnostics': re.findall(
                            r'^TRIAEVUM_(?:SPIRV_CACHE|PASS_SHADER_CACHE|NATIVE_PROGRAM_PREPARATION_END|NRI_GPL_LINK)[^\n]*',
                            diagnostics, re.MULTILINE)})
        (args.output/'measurements.json').write_text(json.dumps(results, indent=2))
        print(rate, window['frame_times'], flush=True)


if __name__ == '__main__':
    main()
