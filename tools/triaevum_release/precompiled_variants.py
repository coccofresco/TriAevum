"""Publisher binding of explicitly reviewed ExHeader variants to an existing title.

This is not runtime compatibility inference. Definitions require exact input
hashes and prior process/thread equivalence verification, documented per title.
No input bytes, module, adapter or source archive are transformed here.
"""

from pathlib import Path
import argparse
import copy

try:
    from .common import atomic_write_json, load_json_object
    from .precompiled_titles import CATALOG, load_catalog, select_title
except ImportError:
    from common import atomic_write_json, load_json_object
    from precompiled_titles import CATALOG, load_catalog, select_title

DEFINITIONS = Path(__file__).with_name('supported_revisions.json')


def add_verified_variants(catalog: dict, recipes: dict, definitions: dict) -> tuple[dict, dict]:
    catalog, recipes = copy.deepcopy(catalog), copy.deepcopy(recipes)
    for variant in definitions['recipes']:
        base_id = variant.get('precompiled_base_recipe')
        if base_id is None:
            continue
        base = next((r for r in recipes['recipes'] if r['id'] == base_id), None)
        title = next((t for t in catalog['titles'] if t['recipe'] == base_id), None)
        if base is None or title is None:
            raise ValueError('Verified variant requires its packaged base recipe and title')
        if any('input_adapter' in r for r in (base, title, variant)):
            raise ValueError('ExHeader variants cannot introduce or inherit input adapters')
        if (title['inputs'] != base['inputs']
                or any(variant['inputs'][k] != base['inputs'][k] for k in ('code', 'romfs'))
                or variant.get('process') != base.get('process')
                or variant.get('optional_inputs') != base.get('optional_inputs')):
            raise ValueError('Variant changes more than the reviewed ExHeader identity')
        header = variant['inputs'].get('exheader', {})
        digest = header.get('sha256', '')
        if (set(header) != {'bytes', 'sha256'} or header['bytes'] != 2048
                or not isinstance(digest, str) or len(digest) != 64
                or any(c not in '0123456789abcdef' for c in digest)):
            raise ValueError('Variant requires an exact ExHeader identity')
        for existing in recipes['recipes']:
            if existing['id'] != variant['id'] and existing['inputs'] == variant['inputs']:
                raise ValueError('Duplicate input triplet: reuse the existing recipe')
        expected = {**copy.deepcopy(title), 'recipe': variant['id'], 'inputs': variant['inputs']}
        for items, key, value in ((recipes['recipes'], 'id', variant),
                                  (catalog['titles'], 'recipe', expected)):
            matches = [r for r in items if r[key] == variant['id']]
            if matches and (len(matches) != 1 or matches[0] != value):
                raise ValueError('Existing variant binding conflicts with its definition')
            if not matches:
                items.append(copy.deepcopy(value))
    return catalog, recipes


def build_overlay(package: Path, output: Path) -> list[dict]:
    catalog = load_catalog(package)
    recipes = load_json_object(package / 'recipes/oot3d.json')
    for recipe in recipes['recipes']:
        select_title(package, recipe, catalog=catalog)
    catalog, recipes = add_verified_variants(catalog, recipes, load_json_object(DEFINITIONS))
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    atomic_write_json(output / CATALOG, catalog)
    atomic_write_json(output / 'recipes/oot3d.json', recipes)
    files = [{'path': path, 'source': str(output / path), 'role': role}
             for path, role in ((CATALOG, 'precompiled_catalog'), ('recipes/oot3d.json', 'revision_recipe'))]
    atomic_write_json(output / 'layout-overlay.json', {'files': files})
    return files


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build_overlay(args.package, args.output)
    print(args.output.resolve() / 'layout-overlay.json')
