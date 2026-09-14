"""Compare real NRI TEV modes using an existing, private invocation fixture."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--invocation', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--timeout', type=float, default=120)
    parser.add_argument('--from-start', action='store_true', help='Ignore the fixture savestate and boot the game')
    parser.add_argument('--taa', action='store_true', help='Enable TAA in the isolated copied configuration to exercise typed temporal programs')
    parser.add_argument('--no-shader-pack', action='store_true', help='Remove collected shader packs; verify built-in fragment artifact use')
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
        if args.no_shader_pack:
            removed.add('--pica-aot-shader-pack')
        copied = {'--config': 'config.json', '--topscreen-config': 'topscreen.json',
                  '--save-data': 'savedata'}
        for token in original:
            if token == '--pica-parametric-tev':
                continue
            if args.no_shader_pack and token == '--pica-aot-shader-strict':
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
                if token == '--config' and args.taa:
                    configuration = json.loads(target.read_text(encoding='utf-8-sig'))
                    configuration['Graphics']['Preset'] = 'Custom'
                    configuration['Graphics']['AA']['Mode'] = 'TAA'
                    target.write_text(json.dumps(configuration, indent=2), encoding='utf-8')
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
        if args.no_shader_pack:
            for name in ('OOT3D_PICA_AOT_SHADER_PACK', 'OOT3D_PICA_AOT_SHADER_STRICT',
                         'OOT3D_PICA_PIPELINE_PREWARM', 'OOT3D_PICA_PIPELINE_MANIFEST'):
                environment.pop(name, None)
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
        if args.no_shader_pack and mode == 'parametric':
            diagnostics = (root/'stderr.log').read_text(errors='replace')
            hits = re.search(r'TRIAEVUM_NATIVE_FRAGMENT_ARTIFACTS hits=(\d+) modules=56\b', diagnostics)
            if not hits or int(hits[1]) == 0:
                raise RuntimeError('parametric: no built-in fragment artifact was used')
            native_identities = {(shader['stage'], int(shader['source_id'], 16), shader['source_size'])
                                 for shader in inventory['shaders']}
            # The compatibility combiner also uses this resolver. Classify by
            # the actual native draw inventory, never by a material ID allowlist.
            for stage, source_id, size in re.findall(
                    r'TRIAEVUM_FRAGMENT_ARTIFACT_MISS stage=(\w+) source=(\d+) bytes=(\d+)', diagnostics):
                if (stage, int(source_id), int(size)) in native_identities:
                    raise RuntimeError('parametric: a native fragment program still required a cache/compiler fallback')
            vertex_hits = re.search(r'TRIAEVUM_NATIVE_VERTEX_ARTIFACTS hits=(\d+)', diagnostics)
            if not vertex_hits or int(vertex_hits[1]) == 0:
                raise RuntimeError('parametric: no built-in vertex artifact was used')
            pass_hits = re.search(r'TRIAEVUM_BUILTIN_PASS_ARTIFACTS hits=(\d+)', diagnostics)
            if not pass_hits or int(pass_hits[1]) == 0:
                raise RuntimeError('parametric: no built-in pass artifact was used')
            if not re.search(r'TRIAEVUM_PASS_SHADER_CACHE requests=0 hits=0 compiled=0\b', diagnostics):
                raise RuntimeError('parametric: fixed passes still requested compiler/cache resolution')
            if not re.search(r'OOT3D_PICA_AOT_SHADER_RESOLUTION .*entries=0\b', diagnostics):
                raise RuntimeError('parametric: collected shader pack was not proven absent')
            if args.taa:
                owners = re.search(r'TRIAEVUM_NATIVE_PROGRAM_OWNERS canonical=(\d+) instrumented=(\d+)', diagnostics)
                if not owners or int(owners[2]) == 0:
                    raise RuntimeError('TAA: instrumented shader program use was not proven')
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
               'captures': reports, 'comparison': comparison, 'no_shader_pack': args.no_shader_pack,
               'taa': args.taa, 'note': 'Framebuffer parity test, not a performance measurement.'}
    (args.output/'comparison.json').write_text(json.dumps(summary, indent=2))
    print(json.dumps(comparison, indent=2))
    if not all(frame['identical_pixels'] for frame in comparison):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
