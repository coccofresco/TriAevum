"""Verify the NVIDIA DLSS Windows runtime (nvngx_dlss.dll) on a non-Windows build host.

Equivalent of the PowerShell check in runtime/three_ds_recomp/cmake/ValidateNvidiaDlssRuntime.cmake
(see coord note XBUILD_DLSS_SIGNATURE_NOTE):
  1. Authenticode: image digest, chain to a trusted root and timestamp countersignature, verified by
     osslsigncode against a PINNED trust bundle (sha256 checked before use). Online revocation is
     not checked (-ignore-crl), the same as Get-AuthenticodeSignature's default.
  2. The leaf signer subject contains O=NVIDIA Corporation.
  3. The VS_VERSIONINFO ProductVersion major.minor.build equals the expected version.
Standard library only; run with python -I.
"""
from __future__ import annotations

import argparse
import hashlib
import re
import struct
import subprocess
import sys
from pathlib import Path

NVIDIA_SUBJECT = re.compile(r'(^|[,/]\s*)O=NVIDIA Corporation(\s*[,/]|$)')
RT_VERSION = 16


class ValidationError(RuntimeError):
    pass


# ---------------------------------------------------------------- PE version resource

def _rva_to_offset(rva: int, sections: list[tuple[int, int, int, int]]) -> int:
    for virtual_address, virtual_size, raw_offset, raw_size in sections:
        if virtual_address <= rva < virtual_address + max(virtual_size, raw_size):
            return raw_offset + (rva - virtual_address)
    raise ValidationError(f'RVA 0x{rva:x} is outside every section')


def _first_leaf(data: bytes, resource_base: int, directory: int, depth: int) -> int:
    """Return the resource-section-relative offset of the first data entry below `directory`."""
    named, ids = struct.unpack_from('<HH', data, resource_base + directory + 12)
    if named + ids == 0:
        raise ValidationError('empty resource directory')
    _name, target = struct.unpack_from('<II', data, resource_base + directory + 16)
    if target & 0x80000000:
        if depth > 4:
            raise ValidationError('resource directory too deep')
        return _first_leaf(data, resource_base, target & 0x7FFFFFFF, depth + 1)
    return target


def _version_resource(data: bytes) -> bytes:
    if data[:2] != b'MZ':
        raise ValidationError('not a PE image (no MZ header)')
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    if data[pe:pe + 4] != b'PE\0\0':
        raise ValidationError('not a PE image (no PE signature)')
    section_count = struct.unpack_from('<H', data, pe + 6)[0]
    optional_size = struct.unpack_from('<H', data, pe + 20)[0]
    optional = pe + 24
    magic = struct.unpack_from('<H', data, optional)[0]
    directories = optional + (112 if magic == 0x20B else 96)
    resource_rva, resource_size = struct.unpack_from('<II', data, directories + 2 * 8)
    if not resource_rva or not resource_size:
        raise ValidationError('the image has no resources')
    sections = []
    table = optional + optional_size
    for index in range(section_count):
        entry = table + index * 40
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from('<IIII', data, entry + 8)
        sections.append((virtual_address, virtual_size, raw_offset, raw_size))
    base = _rva_to_offset(resource_rva, sections)
    named, ids = struct.unpack_from('<HH', data, base + 12)
    for index in range(named + ids):
        name, target = struct.unpack_from('<II', data, base + 16 + index * 8)
        if not name & 0x80000000 and name == RT_VERSION and target & 0x80000000:
            leaf = _first_leaf(data, base, target & 0x7FFFFFFF, 1)
            data_rva, size = struct.unpack_from('<II', data, base + leaf)
            start = _rva_to_offset(data_rva, sections)
            return data[start:start + size]
    raise ValidationError('the image has no VS_VERSIONINFO resource')


def _align4(value: int) -> int:
    return (value + 3) & ~3


def _read_block(blob: bytes, offset: int):
    length, value_length, value_type = struct.unpack_from('<HHH', blob, offset)
    if length < 6 or offset + length > len(blob):
        raise ValidationError('malformed version resource block')
    cursor = offset + 6
    end_key = cursor
    while blob[end_key:end_key + 2] != b'\0\0':
        end_key += 2
    key = blob[cursor:end_key].decode('utf-16-le')
    value_offset = _align4(end_key + 2)
    value_bytes = value_length * 2 if value_type == 1 else value_length
    children = _align4(value_offset + value_bytes)
    return key, value_offset, value_length, value_type, children, offset + length


def _children(blob: bytes, start: int, end: int):
    cursor = start
    while cursor + 6 <= end:
        block = _read_block(blob, cursor)
        yield block
        cursor = _align4(block[5])


def product_version_string(path: Path) -> str:
    """ProductVersion as Windows' FileVersionInfo reports it: the first string table's value,
    falling back to the fixed file info when no string is present."""
    blob = _version_resource(Path(path).read_bytes())
    key, value_offset, value_length, _type, children, end = _read_block(blob, 0)
    if key != 'VS_VERSION_INFO':
        raise ValidationError(f'unexpected version resource key {key!r}')
    for child_key, _vo, _vl, _vt, grandchildren, child_end in _children(blob, children, end):
        if child_key != 'StringFileInfo':
            continue
        for _table_key, _to, _tl, _tt, strings, table_end in _children(blob, grandchildren, child_end):
            for name, s_offset, s_length, s_type, _c, _e in _children(blob, strings, table_end):
                if name == 'ProductVersion' and s_type == 1 and s_length:
                    raw = blob[s_offset:s_offset + s_length * 2].decode('utf-16-le')
                    return raw.split('\0', 1)[0].strip()
    if value_length >= 52:
        signature, _struct_version, _fms, _fls, pms, pls = struct.unpack_from('<IIIIII', blob, value_offset)
        if signature == 0xFEEF04BD:
            return f'{pms >> 16}.{pms & 0xFFFF}.{pls >> 16}.{pls & 0xFFFF}'
    raise ValidationError('the version resource has no ProductVersion')


def parse_version(text: str) -> tuple[int, ...]:
    parts = re.split(r'[.,]', text.replace(' ', ''))
    try:
        numbers = tuple(int(part) for part in parts if part != '')
    except ValueError as error:
        raise ValidationError(f'unparseable version {text!r}') from error
    if len(numbers) < 2:
        raise ValidationError(f'unparseable version {text!r}')
    return numbers + (0,) * (4 - len(numbers))


def versions_match(actual: str, expected: str) -> bool:
    return parse_version(actual)[:3] == parse_version(expected)[:3]


# ---------------------------------------------------------------- Authenticode via osslsigncode

def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, 'rb') as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b''):
            digest.update(chunk)
    return digest.hexdigest()


def leaf_signer_subject(output: str) -> str:
    """The first Subject line of the "Signer's certificate" block of osslsigncode verify."""
    marker = output.find("Signer's certificate:")
    if marker < 0:
        raise ValidationError('osslsigncode reported no signer certificate')
    match = re.search(r'^\s*Subject:\s*(.+?)\s*$', output[marker:], re.MULTILINE)
    if not match:
        raise ValidationError('osslsigncode reported no signer subject')
    return match.group(1)


def check_osslsigncode_output(returncode: int, output: str) -> str:
    if returncode != 0 or not re.search(r'^Signature verification: ok\s*$', output, re.MULTILINE) \
            or not re.search(r'^Succeeded\s*$', output, re.MULTILINE):
        tail = '\n'.join(output.strip().splitlines()[-6:])
        raise ValidationError(f'Authenticode verification failed (osslsigncode {returncode}): {tail}')
    subject = leaf_signer_subject(output)
    if not NVIDIA_SUBJECT.search(subject):
        raise ValidationError(f'the signer is not NVIDIA Corporation: {subject}')
    return subject


def validate(runtime: Path, expected_version: str, osslsigncode: Path, trust_bundle: Path,
             trust_bundle_sha256: str) -> str:
    runtime, trust_bundle = Path(runtime), Path(trust_bundle)
    if not runtime.is_file():
        raise ValidationError(f'DLSS runtime is missing: {runtime}')
    actual_bundle = sha256_file(trust_bundle)
    if actual_bundle.lower() != trust_bundle_sha256.strip().lower():
        raise ValidationError(f'trust bundle {trust_bundle} has sha256 {actual_bundle}, '
                              f'pinned {trust_bundle_sha256}')
    result = subprocess.run(
        [str(osslsigncode), 'verify', '-ignore-crl', '-in', str(runtime),
         '-CAfile', str(trust_bundle), '-TSA-CAfile', str(trust_bundle)],
        capture_output=True, text=True, errors='replace', timeout=120)
    subject = check_osslsigncode_output(result.returncode, result.stdout + result.stderr)
    actual = product_version_string(runtime)
    if not versions_match(actual, expected_version):
        raise ValidationError(f'runtime version {actual} does not match required version {expected_version}')
    return (f'NVIDIA DLSS runtime verified: {actual} (signer {subject}; '
            f'trust bundle sha256 {actual_bundle}; osslsigncode, revocation not checked)')


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--runtime', required=True, type=Path)
    parser.add_argument('--expected-version', required=True)
    parser.add_argument('--osslsigncode', required=True, type=Path)
    parser.add_argument('--trust-bundle', required=True, type=Path)
    parser.add_argument('--trust-bundle-sha256', required=True)
    args = parser.parse_args(argv)
    try:
        print(validate(args.runtime, args.expected_version, args.osslsigncode,
                       args.trust_bundle, args.trust_bundle_sha256))
    except (ValidationError, OSError, struct.error, subprocess.SubprocessError) as error:
        print(f'Rejected DLSS runtime {args.runtime}: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
