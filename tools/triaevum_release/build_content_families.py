"""Publisher-only family qualification from exact previously audited inputs."""
import argparse
import copy
from pathlib import Path

try:
    from .common import load_json_object, atomic_write_json, sha256_file
    from .data_compatibility import FORMAT, identity, expected
    from .input_adapters import import_contract
    from .precompiled_titles import load_catalog, select_title
except ImportError:
    from common import load_json_object, atomic_write_json, sha256_file
    from data_compatibility import FORMAT, identity, expected
    from input_adapters import import_contract
    from precompiled_titles import load_catalog, select_title


def qualify(recipe, source, execution):
    result = copy.deepcopy(recipe)
    families = {}
    for phase, directory, contract in (('source', source, import_contract(recipe)),
                                        ('execution', execution, recipe['inputs'])):
        paths = {kind: directory / f'{kind}.bin' for kind in ('code', 'exheader', 'romfs')}
        for kind, path in paths.items():
            if path.is_symlink() or not path.is_file():
                raise ValueError(f'Missing regular qualification input: {kind}')
            if {'bytes': path.stat().st_size, 'sha256': sha256_file(path)} != contract[kind]:
                raise ValueError(f'Unverified publisher {phase} {kind}')
        families[phase] = identity(paths)
    result['data_compatibility'] = {'format': FORMAT, **families}
    expected(result, 'execution')
    return result


def build(package, specification, output):
    catalog = load_catalog(package)
    recipes = load_json_object(package / 'recipes/oot3d.json')
    seen = set()
    for binding in specification['families']:
        recipe_id = binding['recipe']
        if recipe_id in seen:
            raise ValueError('Duplicate family binding')
        seen.add(recipe_id)
        matches = [r for r in recipes['recipes'] if r['id'] == recipe_id]
        if len(matches) != 1:
            raise ValueError('Unknown qualification recipe')
        recipe = matches[0]
        title = select_title(package, recipe, catalog=catalog)
        result = qualify(recipe, Path(binding['source']), Path(binding['execution']))
        recipe.update(result)
        title['data_compatibility'] = copy.deepcopy(result['data_compatibility'])
    sources = [expected(r, 'source') for r in recipes['recipes'] if r.get('data_compatibility')]
    if any(sources.count(item) != 1 for item in sources):
        raise ValueError('Ambiguous source families; bind only one recipe per family')
    output.mkdir(parents=True, exist_ok=False)
    atomic_write_json(output / 'recipes/oot3d.json', recipes)
    atomic_write_json(output / 'recipes/precompiled-titles.json', catalog)
    atomic_write_json(output / 'layout-overlay.json', {'files': [
        {'source': str((output / name).resolve()), 'path': name, 'role': role}
        for name, role in (('recipes/oot3d.json', 'revision_recipe'),
                           ('recipes/precompiled-titles.json', 'precompiled_catalog'))]})
    return {'families': len(sources), 'game_rebuild_required': False,
            'forge_rebuild_required': True, 'output': str(output)}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--specification', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(build(args.package, load_json_object(args.specification), args.output))
