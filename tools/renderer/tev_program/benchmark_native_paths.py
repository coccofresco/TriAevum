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
    parser.add_argument('--baseline', type=Path, help='Optional historical executable; omitted compares current paths only')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--repeats', type=int, default=3)
    parser.add_argument('--frames', type=int, default=900)
    parser.add_argument('--warmup', type=int, default=180)
    parser.add_argument('--taa', action='store_true', help='Exercise temporal shader outputs without frame interpolation')
    parser.add_argument('--toon', action='store_true', help='Exercise material toon without outline')
    parser.add_argument('--from-start', action='store_true', help='Boot instead of loading the fixture state')
    parser.add_argument('--compare-pipeline-libraries', action='store_true',
                        help='Compare monolithic and GPL with the same parametric programs, no collected pack')
    args = parser.parse_args()
    if args.repeats < 1 or not 0 <= args.warmup < args.frames:
        parser.error('require repeats > 0 and 0 <= warmup < frames')
    if args.compare_pipeline_libraries and args.baseline:
        parser.error('pipeline comparison uses the same current executable only')
    fixture = json.loads(args.invocation.read_text(encoding='utf-8-sig'))
    current = Path(fixture['executable'])
    args.output.mkdir(parents=True, exist_ok=False)
    arms = (('historical',) if args.baseline else ()) + ('current_specialized', 'current_parametric')
    if args.compare_pipeline_libraries:
        arms = ('current_parametric', 'current_libraries')
    rows = []
    for repeat in range(args.repeats):
        order = arms[repeat % len(arms):] + arms[:repeat % len(arms)]
        for arm in order:
            parametric = arm in ('current_parametric', 'current_libraries')
            root = args.output / f'{repeat}-{arm}'
            root.mkdir()
            neutral_input = root / 'neutral-input.json'
            neutral_input.write_text(json.dumps({
                'schema': 'oot3d.native_game.input_timeline.v1', 'frame_origin': 'run',
                'segments': [{'start_frame': 0, 'end_frame_exclusive': 2147483647, 'buttons': []}]
            }), encoding='utf-8')
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
            if args.from_start:
                removed_values.add('--load-state')
            if parametric:
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
                        if args.taa or args.toon:
                            config['Graphics']['Preset'] = 'Custom'
                        if args.taa:
                            config['Graphics']['AA']['Mode'] = 'TAA'
                        if args.toon:
                            config['Graphics']['Effects']['Toon']['Mode'] = 'PicaMaterial'
                            config['Graphics']['Effects']['Toon']['OutlineEnabled'] = False
                        target.write_text(json.dumps(config, indent=2), encoding='utf-8')
                    command.extend((token, str(target.resolve())))
                else:
                    command.append(token)
            cache = args.output / f'cache-{arm}'
            command.extend(('--throughput-benchmark', '--benchmark-warmup-frames', str(args.warmup),
                '--frames', str(args.frames), '--max-seconds', '120',
                '--input-timeline', str(neutral_input.resolve()),
                '--output', str((root/'runtime.json').resolve()),
                '--renderer-cache-directory', str(cache.resolve())))
            if parametric:
                command.append('--pica-parametric-tev')
            environment = os.environ.copy()
            environment['OOT3D_VULKAN_VALIDATION'] = '0'
            environment['TRIAEVUM_NRI_PIPELINE_LIBRARIES'] = '1' if arm == 'current_libraries' else '0'
            environment['OOT3D_GRAPHICS_NRI_PICA_DRAWS'] = '1'
            environment['OOT3D_GRAPHICS_PICA_DYNAMIC_RENDERING'] = '1'
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
            distribution = window.get('frame_times')
            if arm != 'historical':
                if (not distribution or distribution['samples'] != window['measured_frames']
                    or distribution['invalid_samples'] != 0
                    or abs(distribution['mean_ms'] * distribution['samples'] / 1000.0
                           - window['host_seconds']) > 1e-6):
                    raise RuntimeError(f'{arm}: frame-time distribution disagrees with benchmark window')
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
            if args.compare_pipeline_libraries:
                if 'TRIAEVUM_RUNTIME_SHADER_COMPILE ' in log or not re.search(
                    r'TRIAEVUM_SPIRV_CACHE requests=0 hits=0 misses=0 rejected=0 compiled=0\b', log):
                    raise RuntimeError(f'{arm}: shader compilation invalidates pipeline-only comparison')
                links = [dict(zip(('count', 'ns', 'max_ns', 'rejected'), map(int, values)))
                         for values in re.findall(r'TRIAEVUM_NRI_GPL_LINK count=(\d+) ns=(\d+) max_ns=(\d+) rejected=(\d+)', log)]
                counters['pipeline_library_links'] = links
                counters['pipeline_library_parts'] = [dict(zip(('part', 'created', 'reused', 'ns'), map(int, values)))
                    for values in re.findall(r'TRIAEVUM_NRI_GPL_PART part=(\d+) created=(\d+) reused=(\d+) ns=(\d+)', log)]
                if arm == 'current_libraries' and (not links or any(item['rejected'] for item in links)
                        or not sum(item['count'] for item in links)):
                    raise RuntimeError('GPL draw execution missing or contains fallback')
                if arm == 'current_parametric' and links:
                    raise RuntimeError('monolithic reference unexpectedly used GPL')
            for label in ('TRIAEVUM_PASS_SHADER_CACHE', 'TRIAEVUM_SPIRV_CACHE',
                          'TRIAEVUM_NATIVE_PROGRAM_OWNERS'):
                match = re.search(label + r' ([^\n]+)', log)
                if match:
                    counters[label] = dict(re.findall(r'(\w+)=([\w.]+)', match[1]))
            match = re.search(r'TRIAEVUM_NRI_PIPELINE_CACHE (\{[^\n]+\})', log)
            if match:
                counters['TRIAEVUM_NRI_PIPELINE_CACHE'] = json.loads(match[1])
            match = re.search(r'TRIAEVUM_PICA_VULKAN_PIPELINES created=(\d+)', log)
            if match:
                counters['TRIAEVUM_PICA_VULKAN_PIPELINES'] = {'created': int(match[1])}
            match = re.search(r'TRIAEVUM_PICA_VULKAN_SHADER_PAIRS created=(\d+)', log)
            if match:
                counters['TRIAEVUM_PICA_VULKAN_SHADER_PAIRS'] = {'created': int(match[1])}
            if args.taa and arm != 'historical':
                if int(counters.get('TRIAEVUM_NATIVE_PROGRAM_OWNERS', {}).get('instrumented', 0)) == 0:
                    raise RuntimeError(f'{arm}: TAA instrumentation was not exercised')
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
        summary[arm]['cache_windows'] = {
            name: [r['benchmark'] for r in rows if r['arm'] == arm
                   and r['application_cache_initially_empty'] == initially_empty]
            for name, initially_empty in (('application_empty', True), ('application_reused', False))}
        distributions = [r['benchmark'].get('frame_times') for r in rows if r['arm'] == arm]
        if all(distributions):
            summary[arm]['frame_times'] = {
                'median_run_p95_upper_ms': statistics.median(d['p95_upper_ms'] for d in distributions),
                'median_run_p99_upper_ms': statistics.median(d['p99_upper_ms'] for d in distributions),
                'maximum_ms': max(d['maximum_ms'] for d in distributions),
                'over_30hz_budget': sum(d['over_30hz_budget'] for d in distributions),
                'samples': sum(d['samples'] for d in distributions)}
    summary['taa'] = args.taa
    summary['toon'] = args.toon
    summary['compare_pipeline_libraries'] = args.compare_pipeline_libraries
    summary['from_start'] = args.from_start
    summary['warmup_frames'] = args.warmup
    summary['method'] = ('Rotating arm order; same guest DLL/assets/state/config; native30 fixed delta; '
        'no interpolation, VSync, pacing, limiter, screenshots or effective shader inventory. '
        'Neutral input. Configured warmup excluded (zero includes first native frame). '
        'Pipeline/compiler counters span the full process, not just the timing window. '
        'First application cache empty, reused per arm thereafter. '
        'Driver/OS cache not cleared. Historical executable may include unrelated differences.')
    (args.output/'summary.json').write_text(json.dumps(summary, indent=2))
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
