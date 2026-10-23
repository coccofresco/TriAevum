"""Publisher binding for the bundled portable corpus, never a driver cache.

The same catalog is consumed automatically by frozen Forge on both desktops.
Inputs are publisher artifacts, not additional selections in the user GUI.

A corpus is bound only with its qualification record (issue #76): the
publisher checks it against the checkout being released and the audit against
the corresponding source the package ships, so a corpus generated from other
sources, or from recordings of another runtime, is refused.
"""

import copy
import json
import zipfile
from pathlib import Path

try:
    from .common import load_json_object
    from .merge_shader_packs import decode, encode, MAX_BYTES
    from .precompiled_titles import checked_file
    from .precompiled_title_layout import artifact
    from .release_platform import WINDOWS, for_target
    from .shader_preparation import FORMAT as SEED_FORMAT
    from .device_pipeline_preparation import FORMAT as DEVICE_FORMAT
    from .shader_corpus_qualification import repository_reader, validate_qualification
except ImportError:
    from common import load_json_object
    from merge_shader_packs import decode, encode, MAX_BYTES
    from precompiled_titles import checked_file
    from precompiled_title_layout import artifact
    from release_platform import WINDOWS, for_target
    from shader_preparation import FORMAT as SEED_FORMAT
    from device_pipeline_preparation import FORMAT as DEVICE_FORMAT
    from shader_corpus_qualification import repository_reader, validate_qualification


QUALIFICATION_PATH = 'forge/shader-corpus/qualification.json'


def _validate_against_source_archive(root: Path, declared: dict, record: dict, **artifacts):
    sources = [path for path, item in declared.items() if item['role'] == 'corresponding_source']
    if len(sources) != 1:
        raise ValueError('Shader corpus qualification requires the corresponding source archive')
    with zipfile.ZipFile(root / sources[0]) as archive:
        names = set(archive.namelist())
        validate_qualification(record, read=lambda path: archive.read(path) if path in names else None,
                               **artifacts)


def read_portable_corpus(pack: Path):
    if pack.is_symlink() or not pack.is_file() or pack.stat().st_size > MAX_BYTES:
        raise ValueError('Invalid portable corpus file')
    data = pack.read_bytes()
    schema, modules = decode(data)
    if encode(schema, modules) != data:
        raise ValueError('Public corpus must be canonical, without unclaimed data')
    return schema, modules


def read_pipeline_manifest(source: Path, schema: int):
    if source.is_symlink() or source.stat().st_size > 16 * 1024 * 1024:
        raise ValueError('Invalid pipeline manifest file')
    data = load_json_object(source)
    pipelines = data.get('pipelines')
    if (set(data) != {'format', 'schema_version', 'descriptor_schema_version', 'pipeline_count', 'pipelines'}
            or data.get('format') != 'oot3d_pica_pipeline_manifest_v2'
            or data.get('schema_version') != 2
            or data.get('descriptor_schema_version') != schema
            or not isinstance(pipelines, list) or not pipelines
            or data.get('pipeline_count') != len(pipelines)):
        raise ValueError('Incompatible observed pipeline manifest')
    return data


def validate_bundled_corpus(root: Path, catalog: dict, declared: dict):
    roles = {'portable_shader_corpus', 'portable_pipeline_recipes', 'portable_shader_qualification'}
    actual = {path for path, record in declared.items() if record['role'] in roles}
    if not actual:
        return
    expected = set()
    qualified = set()
    for title in catalog['titles']:
        seed = title.get('shader_preparation', {})
        device = title.get('device_pipeline_preparation', {})
        if seed.get('mode') != 'portable_pack' or device.get('format') != DEVICE_FORMAT:
            raise ValueError('Every title requires the bundled corpus and GPU preparation')
        pack = seed['pack']
        if declared.get(pack['path'], {}).get('role') != 'portable_shader_corpus':
            raise ValueError('Portable corpus lacks its explicit release role')
        schema, _ = read_portable_corpus(checked_file(root, pack))
        if schema != seed['descriptor_schema_version']:
            raise ValueError('Portable corpus descriptor schema mismatch')
        expected.add(pack['path'])
        helper = device['helper']
        checked_file(root, helper)
        if declared.get(helper['path'], {}).get('role') != 'shader_preparation_tool':
            raise ValueError('Pipeline helper lacks its explicit release role')
        if not device.get('manifests'):
            raise ValueError('Bundled corpus requires observed pipeline recipes')
        for record in device['manifests']:
            if declared.get(record['path'], {}).get('role') != 'portable_pipeline_recipes':
                raise ValueError('Pipeline recipes lack their explicit release role')
            read_pipeline_manifest(checked_file(root, record), schema)
            expected.add(record['path'])
        qualification = seed.get('qualification')
        if not isinstance(qualification, dict):
            raise ValueError('Bundled corpus lacks its qualification record')
        if declared.get(qualification['path'], {}).get('role') != 'portable_shader_qualification':
            raise ValueError('Shader corpus qualification lacks its explicit release role')
        record = json.loads(checked_file(root, qualification).read_text(encoding='utf-8'))
        key = (pack['sha256'], qualification['sha256'], tuple(item['sha256'] for item in device['manifests']))
        if key not in qualified:
            _validate_against_source_archive(root, declared, record, pack_sha256=pack['sha256'],
                                             manifest_sha256=[item['sha256'] for item in device['manifests']])
            qualified.add(key)
        expected.add(qualification['path'])
    if actual != expected:
        raise ValueError('Uncatalogued portable shader artifact')


def bind_shader_corpus(catalog: dict, pack: Path, manifests: list[Path],
                       helper: Path, dependencies: list[Path] = (), *, qualification: Path,
                       source_root: Path) -> tuple[dict, list[dict]]:
    """Bind one qualified corpus to every supported recipe without modifying inputs."""
    platform = for_target(catalog['target'])
    if not catalog.get('titles'):
        raise ValueError('Shader corpus requires title recipes')
    # Never guess a corpus from a directory or import a GPU-cache binary.
    schema, _modules = read_portable_corpus(pack)
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
        read_pipeline_manifest(source, schema)
        pipeline_records.append(add(source, f'forge/shader-corpus/pipelines-{index}.json',
                                    'portable_pipeline_recipes'))
    # Refuse a corpus not generated from, and recorded with, these sources.
    validate_qualification(load_json_object(qualification), pack_sha256=packed['sha256'],
                           manifest_sha256=[record['sha256'] for record in pipeline_records],
                           read=repository_reader(source_root))
    qualified = add(qualification, QUALIFICATION_PATH, 'portable_shader_qualification')
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
            qualification=copy.deepcopy(qualified),
            **{key: copy.deepcopy(renderer[key]) for key in ('compiler', 'dependencies') if key in renderer})
        # The helper built with this runtime also prepares the renderer's
        # startup program parts, so the first launch does not wait for them.
        title['device_pipeline_preparation'] = dict(format=DEVICE_FORMAT,
            helper=copy.deepcopy(tool), manifests=copy.deepcopy(pipeline_records),
            dependencies=copy.deepcopy(libraries), startup_programs=True)
    return result, items
