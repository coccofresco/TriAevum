#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Local MM3D UI preparation oracle; generated text/assets never leave the user's machine.

This is NOT registration of a supported Forge recipe. It provides the atomic
preparation/launch contract for the native Forge port and local launcher.
Reviewed extra specs are supplied by the caller, never translated or inferred.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import sys
import tempfile

from prepare_message_edits import decode, encode, fnv, messages, prepare as dialogue
from prepare_romfs_layer import prepare as archives

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(ROOT / 'tools/triaevum_release'))
from romfs_mods import romfs_file_records
from romfs_overlay import build_overlay

CODE_SHA256 = '6eec27b1827eab2c87744ac38066d569ffe6fe75790b1eb3300bd91f5099038d'
LANGUAGES = {'en': ('eue', 0x10), 'de': ('eug', 0x20), 'fr': ('euf', 0x40),
             'es': ('eus', 0x80), 'it': ('eui', 0x100)}
FORMAT = 'triaevum_mm3d_ui_preparation_v1'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def file_sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def price_edits(data, spec):
    """Reviewed literal replacements + native 10-bit choice prices, allocation preserving.

    Each row owns an exact original SHA256 and metadata word. No numeric regex,
    automatic reward doubling, text shortening or language fallback is allowed.
    """
    original, language = messages(data)
    if spec.get('language_mask') != 1 << language:
        raise ValueError('Price specification language mismatch')
    metadata = {struct.unpack_from('<H', data, 16 + i * 20)[0]:
                struct.unpack_from('<I', data, 20 + i * 20)[0]
                for i in range(len(original))}
    rows, seen = [], set()
    for rule in spec['prices']:
        mid = int(rule['id'], 16)
        if mid in seen or mid not in original:
            raise ValueError('Duplicate or missing price message')
        seen.add(mid)
        raw, before = original[mid], metadata[mid]
        if sha(raw) != rule['original_sha256'] or before != rule['original_metadata']:
            raise ValueError(f'Unreviewed price message {mid:04x}')
        tokens = decode(raw)
        edited = list(tokens)
        for replacement in rule.get('replacements', []):
            old, new = replacement['old'], replacement['new']
            if not old or sum(t.count(old) for t in edited if isinstance(t, str)) != 1:
                raise ValueError('Price replacement must match exactly once')
            if '\x7f' in new or '\0' in new:
                raise ValueError('Price text cannot introduce controls')
            edited = [t.replace(old, new) if isinstance(t, str) else t for t in edited]
        after = before
        for shift in rule.get('double_fields', []):
            if type(shift) is not int or shift not in (0, 10, 20):
                raise ValueError('Invalid price field')
            value = (before >> shift) & 1023
            if value == 1023:
                continue
            if value * 2 >= 1023:
                raise ValueError('Price exceeds native field')
            after = (after & ~(1023 << shift)) | ((value * 2) << shift)
        out = encode(edited)
        if len(out) > len(raw) or decode(out) != edited:
            raise ValueError('Price text needs reviewed shortening')
        out += bytes(len(raw) - len(out))
        rows.append({'id': mid, 'language': language, 'price': True,
                     'original': raw.hex(), 'edited': out.hex(),
                     'original_hash': f'{fnv(raw):016x}', 'edited_hash': f'{fnv(out):016x}',
                     'original_metadata': before, 'edited_metadata': after})
    return rows


def compile_messages(romfs, specs):
    if not isinstance(specs, dict) or set(specs) - set(LANGUAGES):
        raise ValueError('Unknown locale in reviewed specifications')
    rows, coverage = [], {}
    for code, (name, mask) in LANGUAGES.items():
        path = f'message/eu/{name}.gmsg'
        data = romfs.read(path)
        _, language = messages(data)
        if 1 << language != mask:
            raise ValueError(f'Wrong archive language: {path}')
        spec = specs.get(code)
        coverage[code] = {'archive': path, 'sha256': sha(data), 'mask': mask,
                          'dialogue': 'not-prepared', 'prices': 'not-prepared'}
        if spec is None:
            continue
        if 'edits' in spec:
            changes, skipped = dialogue(data, spec)
            if skipped:
                raise ValueError(f'{code}: reviewed dialogue did not fit/match: {skipped}')
            rows.extend(changes)
            coverage[code]['dialogue'] = len(changes)
        if 'prices' in spec:
            changes = price_edits(data, spec)
            rows.extend(changes)
            coverage[code]['prices'] = len(changes)
    keys = [(r['id'], r['language']) for r in rows]
    if len(keys) != len(set(keys)):
        raise ValueError('Dialogue/price edits overlap; a combined variant contract is required')
    return {'format': 'triaevum_mm3d_message_edits_v1', 'edits': rows}, coverage


def publish(output, identity, build):
    """Immutable generation + one atomic pointer; failure preserves the last install.

    A corrupt generation is never modified beneath a running game. Repair publishes
    a fresh directory. Callers serialize preparation for one output directory.
    """
    output.mkdir(parents=True, exist_ok=True)
    pointer = output / 'prepared.json'
    if pointer.is_file():
        try:
            previous = json.loads(pointer.read_text(encoding='utf-8'))
            folder = output / previous['generation']
            safe = folder.parent == output and folder.name.startswith('generation-')
            expected_args = ['--romfs-overlay', str(folder / 'romfs_overlay.o3ro'),
                             '--title-message-edits', str(folder / 'message_edits.json')]
            if (safe and previous['format'] == FORMAT and previous['identity'] == identity
                    and previous['runtime_args'] == expected_args
                    and set(previous['files']) == {'romfs_overlay.o3ro', 'message_edits.json'} and all(
                        Path(name).name == name and file_sha(folder / name) == digest
                        for name, digest in previous['files'].items())):
                return previous, True
        except (OSError, ValueError, KeyError, TypeError, AttributeError):
            pass
    stage = Path(tempfile.mkdtemp(prefix='.prepare-', dir=output))
    generation = None
    try:
        details = build(stage)
        files = {p.name: file_sha(p) for p in stage.iterdir() if p.is_file()}
        if not files:
            raise ValueError('Empty preparation')
        generation = output / ('generation-' + stage.name[len('.prepare-'):])
        stage.rename(generation)
        record = {'format': FORMAT, 'identity': identity, 'generation': generation.name,
                  'files': files, **details}
        record['runtime_args'] = ['--romfs-overlay', str(generation / 'romfs_overlay.o3ro'),
                                  '--title-message-edits', str(generation / 'message_edits.json')]
        temporary = output / (generation.name + '.json.tmp')
        try:
            with temporary.open('w', encoding='utf-8', newline='\n') as stream:
                json.dump(record, stream, sort_keys=True, indent=2)
                stream.write('\n')
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, pointer)
        finally:
            temporary.unlink(missing_ok=True)
        return record, False
    except BaseException:
        if generation is not None:
            shutil.rmtree(generation, ignore_errors=True)
        raise
    finally:
        shutil.rmtree(stage, ignore_errors=True)


def prepare_install(romfs_path, restoration, output, specs, code_path, expected_code_sha256, offset=0x1000):
    # A complete digest supplied by the revision owner is mandatory. A prefix is
    # NOT a revision contract and no supported recipe is fabricated here.
    if len(expected_code_sha256) != 64 or file_sha(code_path) != expected_code_sha256:
        raise ValueError('Wrong code revision')
    if expected_code_sha256 != CODE_SHA256:
        raise ValueError('Unsupported MM3D revision')
    spec_identity = sha(json.dumps(specs, sort_keys=True).encode())
    source_files = sorted(HERE.glob('*.py')) + [HERE / 'setup-fingerprints.json',
                                               ROOT / 'tools/triaevum_release/romfs_mods.py']
    restoration_files = sorted((restoration / 'romfs').rglob('*'))
    identity = {'title': 'mm3d-eur', 'code_sha256': expected_code_sha256,
                'romfs_sha256': file_sha(romfs_path), 'offset': offset, 'specs_sha256': spec_identity,
                'generator_sha256': sha(b''.join(p.read_bytes() for p in source_files)),
                'restoration': {str(p.relative_to(restoration)): file_sha(p)
                                for p in restoration_files if p.is_file()}}
    def build(stage):
        with romfs_path.open('rb') as stream:
            header, records = romfs_file_records(stream, offset, romfs_path.stat().st_size - offset)
        class CheckedRomFs:
            def read(self, name):
                if name not in records:
                    raise ValueError(f'Missing native UI resource: {name}')
                _, at, size = records[name]
                if size > 16 * 1024 * 1024:
                    raise ValueError('UI resource exceeds preparation limit')
                with romfs_path.open('rb') as stream:
                    stream.seek(offset + header[9] + at)
                    data = stream.read(size)
                if len(data) != size:
                    raise ValueError('Truncated UI resource')
                return data
        romfs = CheckedRomFs()
        edits, coverage = compile_messages(romfs, specs)
        (stage / 'message_edits.json').write_text(json.dumps(edits, sort_keys=True) + '\n', encoding='utf-8')
        layer, sources = stage / 'layer', stage / 'sources'
        archives(romfs, restoration, layer, sources)
        replacements = {p.relative_to(layer).as_posix(): p for p in layer.rglob('*') if p.is_file()}
        overlay = build_overlay(romfs_path, service_offset=offset,
                                      service_size=romfs_path.stat().st_size - offset,
                                      replacements=replacements, output=stage / 'romfs_overlay.o3ro')
        # Re-edit sources are rebuildable, not runtime dependencies.
        shutil.rmtree(layer)
        shutil.rmtree(sources, ignore_errors=True)
        return {'languages': coverage, 'overlay': overlay,
                'limitations': ['Native Forge recipe not registered', 'HD font replacement not prepared',
                                'Unprepared language/kind rows retain native text']}
    return publish(output.resolve(), identity, build)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--romfs', type=Path, required=True)
    parser.add_argument('--restoration', type=Path, required=True)
    parser.add_argument('--code', type=Path, required=True)
    parser.add_argument('--code-sha256', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--specs', type=Path, help='Reviewed object keyed by en/de/fr/es/it; no automatic translations')
    args = parser.parse_args()
    # The historical US wording is not a complete EUR specification. Native
    # text is the honest default until the caller supplies reviewed locale rules.
    try:
        specs = json.loads(args.specs.read_text(encoding='utf-8')) if args.specs else {}
        result, reused = prepare_install(args.romfs, args.restoration, args.output, specs,
                                         args.code, args.code_sha256)
        print(json.dumps({'reused': reused, 'manifest': str(args.output / 'prepared.json'),
                          'runtime_args': result['runtime_args']}))
    except (ValueError, OSError, KeyError, struct.error) as error:
        parser.exit(1, f'prepare_install: {error}\n')
