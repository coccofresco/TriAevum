"""Publisher-bound content families; no inference from region or filename."""
import hashlib
from pathlib import Path

try:
    from .common import sha256_file
    from .romfs_identity import fingerprint as romfs_fingerprint
except ImportError:
    from common import sha256_file
    from romfs_identity import fingerprint as romfs_fingerprint

FORMAT = 'triaevum_content_family_v1'


def identity(paths: dict[str, Path]) -> dict:
    header = bytearray(paths['exheader'].read_bytes())
    if len(header) != 2048:
        raise ValueError('Content family requires a complete ExHeader')
    # ExeFS is already decompressed. SD application storage and signature/key
    # bytes do not define process data; every other header byte stays checked.
    header[0x0d] &= ~3
    header[0x400:0x600] = bytes(0x200)
    return {'code': {'bytes': paths['code'].stat().st_size, 'sha256': sha256_file(paths['code'])},
            'exheader': hashlib.sha256(header).hexdigest(),
            'romfs': romfs_fingerprint(paths['romfs'])}


def expected(recipe: dict, phase: str) -> dict | None:
    contract = recipe.get('data_compatibility')
    if contract is None:
        return None
    if (not isinstance(contract, dict) or set(contract) != {'format', 'source', 'execution'}
            or contract.get('format') != FORMAT or phase not in ('source', 'execution')):
        raise ValueError('Unsupported content-family contract')
    for member in ('source', 'execution'):
        family = contract[member]
        if not isinstance(family, dict) or set(family) != {'code', 'exheader', 'romfs'}:
            raise ValueError('Invalid content-family identity')
        code, romfs = family['code'], family['romfs']
        if (not isinstance(code, dict) or set(code) != {'bytes', 'sha256'}
                or not isinstance(romfs, dict) or set(romfs) != {'sha256', 'files', 'directories', 'payload_bytes'}):
            raise ValueError('Invalid content-family record')
        for digest in (family['exheader'], code['sha256'], romfs['sha256']):
            if not isinstance(digest, str) or len(digest) != 64 or any(c not in '0123456789abcdef' for c in digest):
                raise ValueError('Invalid content-family digest')
        for value in (code['bytes'], romfs['files'], romfs['directories'], romfs['payload_bytes']):
            if type(value) is not int or value <= 0:
                raise ValueError('Invalid content-family size')
        inputs = (recipe.get('input_adapter', {}).get('source_inputs', recipe.get('inputs', {}))
                  if member == 'source' else recipe.get('inputs', {}))
        if code != inputs.get('code'):
            raise ValueError('Content family cannot change the compiled code identity')
    return contract[phase]


def verify(recipe: dict, paths: dict[str, Path], *, phase: str) -> dict:
    contract = expected(recipe, phase)
    if contract is None:
        raise ValueError('No content-family contract for this title')
    actual = identity(paths)
    if actual != contract:
        different = ', '.join(kind for kind in actual if actual[kind] != contract[kind])
        raise ValueError(f'Incompatible {phase} data: {different}; no title was activated')
    return actual
