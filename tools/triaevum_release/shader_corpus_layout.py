"""Publisher binding for the bundled portable corpus, never a driver cache.

The same catalog is consumed automatically by frozen Forge on both desktops.
Inputs are publisher artifacts, not additional selections in the user GUI.
"""

import copy
from pathlib import Path

try:
    from .common import load_json_object
    from .merge_shader_packs import decode, MAX_BYTES
    from .precompiled_title_layout import artifact
    from .release_platform import WINDOWS, for_target
    from .shader_preparation import FORMAT as SEED_FORMAT
    from .device_pipeline_preparation import FORMAT as DEVICE_FORMAT
except ImportError:
    from common import load_json_object
    from merge_shader_packs import decode, MAX_BYTES
    from precompiled_title_layout import artifact
    from release_platform import WINDOWS, for_target
    from shader_preparation import FORMAT as SEED_FORMAT
    from device_pipeline_preparation import FORMAT as DEVICE_FORMAT


def bind_shader_corpus(catalog: dict, pack: Path, manifests: list[Path],
                       helper: Path, dependencies: list[Path] = ()) -> tuple[dict, list[dict]]:
    """Bind one validated corpus to every supported recipe without modifying inputs."""
    platform = for_target(catalog['target'])
    if not catalog.get('titles'):
        raise ValueError('Shader corpus requires title recipes')
    # Never guess a corpus from a directory or import a GPU-cache binary.
    if pack.is_symlink() or not pack.is_file() or pack.stat().st_size > MAX_BYTES:
        raise ValueError('Invalid portable corpus file')
    schema, _modules = decode(pack.read_bytes())
    if not 1 <= len(manifests) <= 64:
        raise ValueError('Shader corpus requires observed pipeline manifests')
    items = []
    destinations = set()

    def add(source, path, role):
        record = artifact(source, path)
        if path.casefold() in destinations:
            raise ValueError('Duplicate corpus artifact: ' + path)
        destinations.add(path.casefold())
        items.append(dict(source=str(source.resolve()), path=path, role=role))
        return record

    packed = add(pack, 'forge/shader-corpus/portable.o3ps', 'portable_shader_corpus')
    pipeline_records = []
    for index, source in enumerate(manifests):
        if source.is_symlink() or source.stat().st_size > 16 * 1024 * 1024:
            raise ValueError('Invalid pipeline manifest file')
        data = load_json_object(source)
        pipelines = data.get('pipelines')
        if (data.get('format') != 'oot3d_pica_pipeline_manifest_v2'
                or data.get('schema_version') != 2
                or data.get('descriptor_schema_version') != schema
                or not isinstance(pipelines, list) or not pipelines
                or data.get('pipeline_count') != len(pipelines)):
            raise ValueError('Incompatible observed pipeline manifest')
        pipeline_records.append(add(source, f'forge/shader-corpus/pipelines-{index}.json',
                                    'portable_pipeline_recipes'))
    suffix = '.exe' if platform == WINDOWS else ''
    tool = add(helper, 'forge/oot3d_native_nri_pipeline_prepare' + suffix,
               'shader_preparation_tool')
    libraries = [add(path, 'forge/' + path.name, 'forge_runtime_module') for path in dependencies]
    result = copy.deepcopy(catalog)
    for title in result['titles']:
        if 'renderer_shader_preparation' not in title:
            raise ValueError('Bind the platform renderer compiler before the corpus')
        renderer = title['renderer_shader_preparation']
        # Older distributed Forge versions obtain the pass compiler from the
        # combined seed contract. Keep the same compiler, not an evidence copy.
        title['shader_preparation'] = dict(format=SEED_FORMAT, mode='portable_pack',
            descriptor_schema_version=schema, pack=copy.deepcopy(packed),
            **{key: copy.deepcopy(renderer[key]) for key in ('compiler', 'dependencies') if key in renderer})
        title['device_pipeline_preparation'] = dict(format=DEVICE_FORMAT,
            helper=copy.deepcopy(tool), manifests=copy.deepcopy(pipeline_records),
            dependencies=copy.deepcopy(libraries))
    return result, items
