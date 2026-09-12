"""Publisher bridge for host-independent FidelityFX Vulkan shader headers."""

import argparse
import hashlib
import json
from pathlib import Path


def digest(path, *, text=False):
    data = path.read_bytes()
    if text:
        data = data.replace(b'\r\n', b'\n')
    return hashlib.sha256(data).hexdigest()


def inputs(sdk):
    result = {}
    for directory in ('include/FidelityFX/gpu', 'src/backends/vk/shaders/sssr',
                      'src/backends/vk/shaders/denoiser'):
        paths = sorted((sdk / directory).rglob('*'))
        if not paths:
            raise ValueError('Missing FidelityFX shader source directory: ' + directory)
        for path in paths:
            if path.is_file():
                result[path.relative_to(sdk).as_posix()] = digest(path, text=True)
    return result


def export(sdk, generated, output):
    if output.exists():
        raise ValueError('Bundle output must not exist')
    headers = sorted(generated.glob('*.h'))
    if not headers or not any(p.name.endswith('_permutations.h') for p in headers):
        raise ValueError('No generated shader permutation headers')
    overlay = generated / 'source/sssr/ffx_sssr_callbacks_glsl.h'
    manifest = dict(format='triaevum_ffx_vulkan_shader_bundle_v1',
        inputs=inputs(sdk), overlay_sha256=digest(overlay, text=True),
        headers={p.name: digest(p) for p in headers})
    output.mkdir(parents=True)
    for path in headers:
        (output / path.name).write_bytes(path.read_bytes())
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


def install(sdk, bundle, overlay, output):
    manifest = json.loads((bundle / 'manifest.json').read_text())
    if (manifest.get('format') != 'triaevum_ffx_vulkan_shader_bundle_v1' or
            manifest.get('inputs') != inputs(sdk) or
            manifest.get('overlay_sha256') != digest(overlay, text=True)):
        raise ValueError('FidelityFX bundle does not match shader inputs/overlay')
    headers = manifest.get('headers')
    if not isinstance(headers, dict) or not headers:
        raise ValueError('Empty shader header inventory')
    # Validate the complete set before touching the build directory.
    for name, expected in headers.items():
        if Path(name).name != name or not name.endswith('.h') or '\\' in name:
            raise ValueError('Invalid shader header path')
        path = bundle / name
        if path.is_symlink() or digest(path) != expected:
            raise ValueError('Shader header integrity mismatch: ' + name)
    output.mkdir(parents=True, exist_ok=True)
    for name, expected in headers.items():
        target = output / name
        if not target.is_file() or digest(target) != expected:
            target.write_bytes((bundle / name).read_bytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('export', 'install'))
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--generated', type=Path)
    parser.add_argument('--bundle', type=Path, required=True)
    parser.add_argument('--overlay', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.mode == 'export':
        if args.generated is None:
            parser.error('export requires --generated')
        export(args.sdk, args.generated, args.bundle)
    else:
        if args.output is None or args.overlay is None:
            parser.error('install requires --output and --overlay')
        install(args.sdk, args.bundle, args.overlay, args.output)


if __name__ == '__main__':
    main()
