"""Developer-only union of qualified portable PICA packs, never driver caches.

Wire format belongs to fast/oot3d/pica_aot_shader_pack.cpp (O3PSAOT v1).
No title, GPU, scene or compiler selection occurs here. Different descriptor
schemas or different binaries for the same complete source identity fail closed.
"""

import argparse
import hashlib
import struct
from pathlib import Path

try:
    from .common import atomic_write_bytes, atomic_write_json
except ImportError:
    from common import atomic_write_bytes, atomic_write_json

HEADER = struct.Struct("<8s4I")
ENTRY = struct.Struct("<IIQQQIIQQ")
MAX_BYTES = 512 * 1024 * 1024


def binary_hash(data):
    value = 1469598103934665603
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return value or 1


def decode(data):
    if not HEADER.size <= len(data) <= MAX_BYTES:
        raise ValueError("Invalid portable pack size")
    magic, version, schema, count, reserved = HEADER.unpack_from(data)
    table_end = HEADER.size + count * ENTRY.size
    if (magic != b"O3PSAOT\0" or version != 1 or not schema or not count
            or reserved or table_end > len(data)):
        raise ValueError("Invalid portable pack header")
    modules = {}
    ranges = []
    for index in range(count):
        stage, pad, primary, secondary, size, words, pad2, offset, digest = ENTRY.unpack_from(
            data, HEADER.size + index * ENTRY.size)
        end = offset + words * 4
        key = (stage, primary, secondary, size)
        if (stage not in (1, 2, 3) or pad or pad2 or not all(key[1:]) or not words
                or offset < table_end or offset % 4 or end > len(data) or key in modules):
            raise ValueError("Invalid portable pack entry")
        payload = data[offset:end]
        if payload[:4] != b"\x03\x02\x23\x07" or binary_hash(payload) != digest:
            raise ValueError("Corrupt SPIR-V payload")
        modules[key] = payload
        ranges.append((offset, end))
    previous = table_end
    for start, end in sorted(ranges):
        if start < previous:
            raise ValueError("Overlapping SPIR-V payloads")
        previous = end
    return schema, modules


def encode(schema, modules):
    if not schema or not modules:
        raise ValueError("Empty portable pack")
    offset = HEADER.size + len(modules) * ENTRY.size
    entries, payloads = [], []
    for (stage, primary, secondary, size), payload in sorted(modules.items()):
        entries.append(ENTRY.pack(stage, 0, primary, secondary, size,
                                  len(payload) // 4, 0, offset, binary_hash(payload)))
        payloads.append(payload)
        offset += len(payload)
    if offset > MAX_BYTES:
        raise ValueError("Merged pack exceeds size limit")
    result = HEADER.pack(b"O3PSAOT\0", 1, schema, len(modules), 0) + b"".join(entries + payloads)
    decode(result)
    return result


def merge(inputs, output, receipt):
    inputs = [Path(path).resolve(strict=True) for path in inputs]
    output, receipt = Path(output).resolve(), Path(receipt).resolve()
    if output in inputs or receipt in inputs or output == receipt:
        raise ValueError("Output must not overwrite evidence")
    schema, modules, sources = None, {}, []
    for path in inputs:
        if path.stat().st_size > MAX_BYTES:
            raise ValueError("Input pack exceeds size limit")
        data = path.read_bytes()
        current, incoming = decode(data)
        if schema is not None and schema != current:
            raise ValueError("Incompatible descriptor schemas")
        schema = current
        added = 0
        for key, payload in incoming.items():
            if key in modules:
                if modules[key] != payload:
                    raise ValueError("Conflicting SPIR-V for the same source identity")
            else:
                modules[key] = payload
                added += 1
        sources.append(dict(path=str(path), sha256=hashlib.sha256(data).hexdigest(),
                            modules=len(incoming), added=added))
    data = encode(schema, modules)
    result = dict(format="triaevum_private_shader_pack_union_v1", inputs=sources,
                  descriptor_schema_version=schema, modules=len(modules),
                  stages={str(stage): sum(key[0] == stage for key in modules) for stage in (1, 2, 3)},
                  pack_sha256=hashlib.sha256(data).hexdigest(),
                  public_distribution=False, new_pipeline_recipes=False)
    atomic_write_bytes(output, data)
    atomic_write_json(receipt, result)
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", action="append", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--receipt", required=True, type=Path)
    args = parser.parse_args()
    print(merge(args.input, args.output, args.receipt))
