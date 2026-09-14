"""Compare real NRI TEV modes using an existing, private invocation fixture."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--invocation', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--timeout', type=float, default=120)
    parser.add_argument('--from-start', action='store_true', help='Ignore the fixture savestate and boot the game')
    args = parser.parse_args()
    source = json.loads(args.invocation.read_text(encoding='utf-8-sig'))
    executable = Path(source['executable'])
    args.output.mkdir(parents=True, exist_ok=False)
    reports = {}
    for mode in ('specialized', 'parametric'):
        root = args.output / mode
        root.mkdir()
        original = iter(source['arguments'])
        command = [str(executable)]
        removed = {'--output', '--screenshot', '--renderer-cache-directory',
                   '--pica-effective-shader-inventory', '--save-state',
                   '--save-state-frame', '--input-timeline', '--pica-semantic-trace'}
        if args.from_start:
            removed.add('--load-state')
        copied = {'--config': 'config.json', '--topscreen-config': 'topscreen.json',
                  '--save-data': 'savedata'}
        for token in original:
            if token == '--pica-parametric-tev':
                continue
            if token in removed:
                next(original)
                continue
            if token in copied:
                path = Path(next(original))
                target = root / copied[token]
                if path.is_dir():
                    shutil.copytree(path, target)
                else:
                    shutil.copy2(path, target)
                command.extend([token, str(target.resolve())])
            else:
                command.append(token)
        command.extend(['--output', str((root/'runtime.json').resolve()),
                        '--screenshot', str((root/'framebuffer.bmp').resolve()),
                        '--renderer-cache-directory', str((root/'cache').resolve()),
                        '--pica-effective-shader-inventory', str((root/'shaders.json').resolve())])
        if mode == 'parametric':
            command.append('--pica-parametric-tev')
        (root/'invocation.json').write_text(json.dumps({'command': command}, indent=2))
        environment = os.environ.copy()
        environment['OOT3D_VULKAN_DIAGNOSTICS_PATH'] = str((root/'renderer.json').resolve())
        environment['OOT3D_VULKAN_DIAGNOSTICS_MAX_FRAMES'] = '256'
        with (root/'stdout.log').open('w') as out, (root/'stderr.log').open('w') as err:
            result = subprocess.run(command, cwd=executable.parent, stdout=out,
                                    stderr=err, timeout=args.timeout, check=False, env=environment)
        if result.returncode:
            raise RuntimeError(f'{mode}: exit {result.returncode}; see {root}')
        captures = sorted(root.glob('*.bmp'))
        if not captures or not (root/'runtime.json').exists():
            raise RuntimeError(f'{mode}: missing framebuffer/report')
        inventory = json.loads((root/'shaders.json').read_text())
        parametric_modules = sum('pica_evaluate_tev_resolved' in shader['source']
                                 for shader in inventory['shaders'])
        if (parametric_modules > 0) != (mode == 'parametric'):
            raise RuntimeError(f'{mode}: effective shader inventory contradicts requested mode')
        reports[mode] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in captures}
        print(f'{mode}: {len(captures)} framebuffer captures', flush=True)
    if reports['specialized'].keys() != reports['parametric'].keys():
        raise RuntimeError('capture frame sets differ')
    from PIL import Image, ImageChops, ImageStat
    comparison = []
    for name in reports['specialized']:
        with Image.open(args.output/'specialized'/name) as a, Image.open(args.output/'parametric'/name) as b:
            if a.size != b.size:
                raise RuntimeError('capture extents differ')
            diff = ImageChops.difference(a.convert('RGB'), b.convert('RGB'))
            stats = ImageStat.Stat(diff)
            comparison.append({'frame': name, 'identical_pixels': diff.getbbox() is None,
                               'mean_absolute_rgb': stats.mean,
                               'max_absolute_rgb': [v[1] for v in diff.getextrema()]})
    summary = {'executable_sha256': hashlib.sha256(executable.read_bytes()).hexdigest(),
               'captures': reports, 'comparison': comparison,
               'note': 'Framebuffer parity test, not a performance measurement.'}
    (args.output/'comparison.json').write_text(json.dumps(summary, indent=2))
    print(json.dumps(comparison, indent=2))
    if not all(frame['identical_pixels'] for frame in comparison):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
