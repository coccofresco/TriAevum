"""Controlled native-frame throughput comparison; no captures during timing."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--invocation', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--repeats', type=int, default=3)
    parser.add_argument('--frames', type=int, default=900)
    parser.add_argument('--warmup', type=int, default=180)
    args = parser.parse_args()
    if args.repeats < 1 or not 0 < args.warmup < args.frames:
        parser.error('require repeats > 0 and 0 < warmup < frames')
    fixture = json.loads(args.invocation.read_text(encoding='utf-8-sig'))
    current = Path(fixture['executable'])
    args.output.mkdir(parents=True, exist_ok=False)
    arms = ('historical', 'current_specialized', 'current_parametric')
    rows = []
    for repeat in range(args.repeats):
        order = arms[repeat % 3:] + arms[:repeat % 3]
        for arm in order:
            root = args.output / f'{repeat}-{arm}'
            root.mkdir()
            executable = args.baseline if arm == 'historical' else current
            command = [str(executable.resolve())]
            tokens = iter(fixture['arguments'])
            removed_values = {'--output', '--renderer-cache-directory', '--screenshot',
                '--screenshot-start-frame', '--screenshot-interval', '--input-timeline',
                '--save-state', '--save-state-frame', '--frames', '--max-seconds',
                '--benchmark-warmup-frames', '--pica-effective-shader-inventory',
                '--pica-semantic-trace'}
            removed_flags = {'--screenshot-sequence', '--extended-diagnostics',
                '--throughput-benchmark', '--pica-parametric-tev'}
            if arm == 'current_parametric':
                removed_values.add('--pica-aot-shader-pack')
                removed_flags.add('--pica-aot-shader-strict')
            copies = {'--config': 'config.json', '--topscreen-config': 'topscreen.json',
                      '--save-data': 'savedata'}
            for token in tokens:
                if token in removed_values:
                    next(tokens)
                elif token in removed_flags:
                    continue
                elif token in copies:
                    source = Path(next(tokens))
                    target = root / copies[token]
                    if source.is_dir():
                        shutil.copytree(source, target)
                    else:
                        shutil.copy2(source, target)
                    if token == '--config':
                        config = json.loads(target.read_text(encoding='utf-8-sig'))
                        config['Graphics']['Presentation']['VSync'] = False
                        target.write_text(json.dumps(config, indent=2), encoding='utf-8')
                    command.extend((token, str(target.resolve())))
                else:
                    command.append(token)
            cache = args.output / f'cache-{arm}'
            command.extend(('--throughput-benchmark', '--benchmark-warmup-frames', str(args.warmup),
                '--frames', str(args.frames), '--max-seconds', '120',
                '--output', str((root/'runtime.json').resolve()),
                '--renderer-cache-directory', str(cache.resolve())))
            if arm == 'current_parametric':
                command.append('--pica-parametric-tev')
            environment = os.environ.copy()
            for name in ('OOT3D_PICA_AOT_SHADER_PACK', 'OOT3D_PICA_AOT_SHADER_STRICT',
                'OOT3D_PICA_PIPELINE_PREWARM', 'OOT3D_PICA_PIPELINE_MANIFEST',
                'OOT3D_PICA_EFFECTIVE_SHADER_INVENTORY', 'OOT3D_VULKAN_DIAGNOSTICS_PATH',
                'OOT3D_VULKAN_DIAGNOSTICS_MAX_FRAMES'):
                environment.pop(name, None)
            (root/'invocation.json').write_text(json.dumps(command, indent=2))
            start = time.perf_counter()
            with (root/'stdout.log').open('w') as out, (root/'stderr.log').open('w') as err:
                result = subprocess.run(command, cwd=executable.parent, env=environment,
                    stdout=out, stderr=err, timeout=150, check=False)
            elapsed = time.perf_counter() - start
            if result.returncode:
                raise RuntimeError(f'{arm}: exit {result.returncode}, see {root}')
            report = json.loads((root/'runtime.json').read_text())
            window = report['benchmark_window']
            if (not window['throughput_mode'] or window['vsync'] or window['pacing_enabled']
                or window['sdl_frame_limiter_enabled'] or window['measured_frames'] != args.frames - args.warmup):
                raise RuntimeError(f'{arm}: invalid measurement window: {window}')
            timing = report['frame_rate']
            visual = report['visual_interpolation']
            if (timing['mode'] != 'native30_no_interpolation' or timing['visual_interpolation_active']
                or timing['game_state_updates_observed'] != args.frames
                or report['presentation_frames'] != args.frames
                or visual['direct_current_frames_presented'] != args.frames
                or visual['interpolated_draws'] != 0 or visual['reused_snapshot_presentations'] != 0):
                raise RuntimeError(f'{arm}: native visual/update accounting differs')
            log = (root/'stderr.log').read_text(errors='replace')
            counters = {}
            for label in ('TRIAEVUM_PASS_SHADER_CACHE', 'TRIAEVUM_SPIRV_CACHE',
                          'TRIAEVUM_NATIVE_PROGRAM_OWNERS'):
                match = re.search(label + r' ([^\n]+)', log)
                if match:
                    counters[label] = dict(re.findall(r'(\w+)=([\w.]+)', match[1]))
            row = {'arm': arm, 'repeat': repeat, 'application_cache_initially_empty': repeat == 0,
                'wall_seconds_including_load': elapsed, 'benchmark': window,
                'guest_refresh_frames': report['guest_refresh_frames'],
                'presentation_frames': report['presentation_frames'],
                'game_state_updates': timing['game_state_updates_observed'],
                'executed_draws': visual['execution_scheduler']['executed_draws'],
                'counters': counters, 'phase_timing': report.get('phase_timing'),
                'executable_sha256': hashlib.sha256(executable.read_bytes()).hexdigest()}
            rows.append(row)
            (args.output/'measurements.json').write_text(json.dumps(rows, indent=2))
            print(f'{arm} repeat={repeat}: {window["frames_per_second"]:.3f} native frames/s, '
                  f'{window["host_seconds"]:.3f}s measured, {elapsed:.3f}s incl. load', flush=True)
    summary = {}
    for arm in arms:
        samples = [r['benchmark']['frames_per_second'] for r in rows if r['arm'] == arm]
        summary[arm] = {'median_native_fps': statistics.median(samples),
                        'min_native_fps': min(samples), 'max_native_fps': max(samples)}
    summary['method'] = ('Rotating arm order; same guest DLL/assets/state/config; native30 fixed delta; '
        'no interpolation, VSync, pacing, limiter, screenshots or effective shader inventory. '
        'Warmup excluded. First application cache empty, reused per arm thereafter. '
        'Driver/OS cache not cleared. Historical executable may include unrelated differences.')
    (args.output/'summary.json').write_text(json.dumps(summary, indent=2))
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
