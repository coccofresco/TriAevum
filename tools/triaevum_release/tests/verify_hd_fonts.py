"""Local-fixture HD font runs with deterministic framebuffer captures."""
import argparse
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess


def values(node, name):
    if isinstance(node, dict):
        if name in node:
            yield node[name]
        for value in node.values():
            yield from values(value, name)
    elif isinstance(node, list):
        for value in node:
            yield from values(value, name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('executable', 'profile', 'config', 'topscreen', 'state', 'font-pack', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--only', nargs='+')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    profile = json.loads(args.profile.read_text(encoding='utf-8-sig'))
    tokens = profile[1:] if isinstance(profile, list) else profile['arguments']
    base = []
    for index, token in enumerate(tokens):
        if token in {'--title-plugin', '--a32-process-manifest', '--resource-root'}:
            base += [token, tokens[index + 1].replace('${profile_dir}', str(args.profile.parent))]
    atlas = Path(tokens[tokens.index('--topscreen-texture-overrides') + 1].replace(
        '${profile_dir}', str(args.profile.parent)))
    original = json.loads(args.config.read_text(encoding='utf-8-sig'))
    results = []
    for name, height, active in [('native720', 720, False), ('hd720', 720, True),
                                 ('hd480', 480, True), ('native360', 360, True)]:
        if args.only and name not in args.only:
            continue
        root = args.output / name
        root.mkdir(exist_ok=False)
        shutil.copyfile(atlas, root / 'atlas_overrides.o3tu')
        if active:
            shutil.copyfile(args.font_pack, root / 'font_coverage.zip')
        config = copy.deepcopy(original)
        config['Graphics']['Output'] = {'Width': height * 16 // 9, 'Height': height, 'RefreshRate': 60}
        config['Graphics']['Window'] = {'Mode': 'Windowed', 'Display': 0}
        config['Graphics']['FrameRate'] = {'Mode': 'Original30'}
        config.pop('Window', None)
        (root / 'config.json').write_text(json.dumps(config))
        shutil.copyfile(args.topscreen, root / 'topscreen.json')
        (root / 'input.json').write_text(json.dumps({
            'schema': 'oot3d.native_game.input_timeline.v1', 'frame_origin': 'run',
            'segments': [{'start_frame': 0, 'end_frame_exclusive': 20, 'buttons': []},
                         {'start_frame': 20, 'end_frame_exclusive': 23, 'buttons': ['a']},
                         {'start_frame': 23, 'end_frame_exclusive': 1000, 'buttons': []}]}))
        command = [str(args.executable), *base, '--renderer', 'nri', '--ui-profile', 'topscreen',
                   '--config', str(root / 'config.json'), '--topscreen-config', str(root / 'topscreen.json'),
                   '--topscreen-texture-overrides', str(root / 'atlas_overrides.o3tu'),
                   '--load-state', str(args.state), '--save-data', str(root / 'savedata'),
                   '--output', str(root / 'runtime.json'), '--frames', '180', '--max-seconds', '60',
                   '--fixed-delta-seconds', str(1 / 30), '--gameplay-timing', 'native30_no_interpolation',
                   '--presentation-rate', '30', '--input-timeline', str(root / 'input.json'),
                   '--screenshot', str(root / 'framebuffer.bmp'), '--screenshot-sequence',
                   '--screenshot-start-frame', '60', '--screenshot-interval', '60',
                   '--renderer-cache-directory', str(args.output / 'cache')]
        (root / 'command.json').write_text(json.dumps(command, indent=2))
        env = {k: v for k, v in os.environ.items() if not k.startswith(
            ('OOT3D_GRAPHICS_', 'OOT3D_PICA_', 'OOT3D_GRASS_', 'TRIAEVUM_AOT_'))}
        env['DISABLE_VULKAN_OBS_CAPTURE'] = '1'
        with (root / 'stdout.log').open('w') as stdout, (root / 'stderr.log').open('w') as stderr:
            result = subprocess.run(command, cwd=args.executable.parent, env=env,
                                    stdout=stdout, stderr=stderr, timeout=120)
        report = json.loads((root / 'runtime.json').read_text()) if (root / 'runtime.json').exists() else {}
        stats = {key: next(values(report, key), None) for key in (
            'hd_font_pack_loaded', 'hd_font_native_blits', 'hd_font_atlas_rebuilds', 'hd_font_texture_replacements')}
        expected = active and height >= 480
        passed = (result.returncode == 0 and bool(list(root.glob('*.bmp'))) and
                  stats['hd_font_pack_loaded'] == active and
                  stats['hd_font_texture_replacements'] is not None and
                  (stats['hd_font_texture_replacements'] > 0) == expected)
        results.append({'case': name, 'passed': passed, **stats})
        print(results[-1], flush=True)
        (args.output / 'summary.json').write_text(json.dumps(results, indent=2))
    return 0 if results and all(result['passed'] for result in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
