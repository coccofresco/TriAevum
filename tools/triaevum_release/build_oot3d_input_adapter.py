"""Publisher-only COPY recipe for the audited USA input; no new title build.

The output is a package-layout overlay containing metadata and COPY operations.
No ROM, canonical code, ExHeader, RomFS or translated source is copied into it.
End-user Forge needs only this overlay and the user's matching decrypted ROM.
"""

from __future__ import annotations

import argparse
import copy
from pathlib import Path

from .common import atomic_write_json, load_json_object, sha256_file
from .input_adapters import FORMAT
from .input_copy_adapter import apply_program, build_program
from .oot3d_region_assets import ALGORITHM
from .precompiled_title_layout import artifact
from .precompiled_titles import CATALOG, load_catalog, select_title

BASELINE = 'oot3d-eur-project-baseline-16a6b0aa'
CANDIDATE = 'oot3d-usa-input-canonical-eur-ef210566'
SOURCE_INPUTS = {
    'code': {'bytes': 4567040, 'sha256': 'ef210566e1d9d16879a746dfb063fcbad232f0171d860de906531ecc526cc020'},
    'exheader': {'bytes': 2048, 'sha256': 'dbe5fa0174d73bffb75d7cf0fbaa05d3e0ee080df0e257afc58b6c45ae5de3d0'},
    'romfs': {'bytes': 473526272, 'sha256': 'dd6def65af151d40fcbba7202c36bbdd8ed5b5b431373dc6a8c89bfd708af690'},
}
RESOURCE_OUTPUT = {'bytes': 473522176, 'sha256': '011c0b6933f3932c704ff1c6ab562023f407c2929661a6d54ecea1ca81072f99'}


def build_overlay(package: Path, source_inputs: Path, canonical_code: Path, output: Path) -> list[dict]:
    catalog = copy.deepcopy(load_catalog(package))
    recipes = load_json_object(package / 'recipes/oot3d.json')
    baseline = next(item for item in recipes['recipes'] if item['id'] == BASELINE)
    title = select_title(package, baseline, catalog=catalog)
    if any(item['id'] == CANDIDATE for item in recipes['recipes']):
        raise ValueError('Candidate already exists in package; use a clean baseline')
    for kind, expected in SOURCE_INPUTS.items():
        path = source_inputs / f'{kind}.bin'
        if (path.is_symlink() or not path.is_file() or path.stat().st_size != expected['bytes']
                or sha256_file(path) != expected['sha256']):
            raise ValueError(f'Publisher {kind} does not match the audited USA input')
    if (canonical_code.is_symlink() or not canonical_code.is_file()
            or canonical_code.stat().st_size != baseline['inputs']['code']['bytes']
            or sha256_file(canonical_code) != baseline['inputs']['code']['sha256']):
        raise ValueError('Canonical code does not match the packaged EUR module')
    source, canonical = (source_inputs/'code.bin').read_bytes(), canonical_code.read_bytes()
    program = build_program(source, canonical)
    if apply_program(source, program) != canonical:
        raise ValueError('Publisher COPY verification failed')
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    program_path = 'recipes/adapters/oot3d-usa-ef210566-copies.json'
    atomic_write_json(output / program_path, program)
    adapter = {'format': FORMAT, 'source_inputs': SOURCE_INPUTS,
               'code_copies': artifact(output / program_path, program_path),
               'resource_algorithm': ALGORITHM}
    inputs = {'code': baseline['inputs']['code'], 'exheader': SOURCE_INPUTS['exheader'],
              'romfs': RESOURCE_OUTPUT}
    recipe = {**copy.deepcopy(baseline), 'id': CANDIDATE,
              'region': 'USA input, canonical EUR runtime', 'inputs': inputs, 'input_adapter': adapter,
              'qualification': 'experimental_boot_title_verified',
              'dialogue_languages': ['English', 'French', 'Spanish']}
    recipes['recipes'].append(recipe)
    # Same DLL, same code identity and corresponding source; only install inputs differ.
    catalog['titles'].append({**copy.deepcopy(title), 'recipe': CANDIDATE,
                              'inputs': inputs, 'input_adapter': adapter})
    atomic_write_json(output / 'recipes/oot3d.json', recipes)
    atomic_write_json(output / CATALOG, catalog)
    layout = [{'source': str(output / relative), 'path': relative, 'role': role}
              for relative, role in (('recipes/oot3d.json', 'revision_recipe'),
                                     (CATALOG, 'precompiled_catalog'),
                                     (program_path, 'input_copy_adapter'))]
    atomic_write_json(output / 'layout-overlay.json', {'files': layout})
    return layout


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--source-inputs', type=Path, required=True)
    parser.add_argument('--canonical-code', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build_overlay(args.package, args.source_inputs, args.canonical_code, args.output)
    print(args.output.resolve() / 'layout-overlay.json')


if __name__ == '__main__':
    main()
