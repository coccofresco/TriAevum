"""Install-time, hash-bound COPY-only normalization of small title inputs.

The publisher needs source and canonical inputs to build a recipe. Installation
needs only the user's source input and offset/length operations; recipes contain
no literal replacement bytes. This does not establish semantic compatibility.
"""

from __future__ import annotations

import hashlib
from pathlib import Path

try:
    from .common import atomic_write_bytes
except ImportError:
    from common import atomic_write_bytes

FORMAT = "triaevum_input_copy_adapter_v1"
MAX_BYTES = 16 * 1024 * 1024
MAX_OPERATIONS = 500_000


def identity(data: bytes) -> dict:
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def build_program(source: bytes, canonical: bytes) -> dict:
    if not source or not canonical or max(len(source), len(canonical)) > MAX_BYTES:
        raise ValueError("normalization input size is outside limits")
    indexes = {}
    for width in (16, 4, 1):
        table = {}
        stride = 1 if width == 1 else 4
        for offset in range(0, len(source) - width + 1, stride):
            table.setdefault(source[offset:offset + width], offset)
        indexes[width] = table
    operations = []
    output = 0
    while output < len(canonical):
        for width in (16, 4, 1):
            if output + width > len(canonical):
                continue
            origin = indexes[width].get(canonical[output:output + width])
            if origin is not None:
                break
        else:
            raise ValueError("canonical bytes cannot be copied from the source")
        maximum = min(len(source) - origin, len(canonical) - output)
        length = width
        while length + 4096 <= maximum and source[origin+length:origin+length+4096] == canonical[output+length:output+length+4096]:
            length += 4096
        while length < maximum and source[origin+length] == canonical[output+length]:
            length += 1
        if operations and operations[-1][0] + operations[-1][1] == origin:
            operations[-1][1] += length
        else:
            operations.append([origin, length])
        output += length
    if len(operations) > MAX_OPERATIONS:
        raise ValueError("normalization recipe has too many operations")
    return {"format": FORMAT, "source": identity(source), "canonical": identity(canonical),
            "copies": operations}


def validate_program(program: dict) -> None:
    """Validate the complete COPY-only schema, also usable by the release audit."""
    if (not isinstance(program, dict) or set(program) != {"format", "source", "canonical", "copies"}
            or program.get("format") != FORMAT):
        raise ValueError("invalid COPY-only normalization format")
    for field in ("source", "canonical"):
        record = program[field]
        if not isinstance(record, dict) or set(record) != {"bytes", "sha256"}:
            raise ValueError("invalid normalization identity")
        size, digest = record["bytes"], record["sha256"]
        if (type(size) is not int or not 0 < size <= MAX_BYTES or not isinstance(digest, str)
                or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest)):
            raise ValueError("invalid normalization identity")
    size = program["canonical"]["bytes"]
    copies = program.get("copies")
    if not isinstance(copies, list) or not 0 < len(copies) <= MAX_OPERATIONS:
        raise ValueError("invalid normalization operation list")
    written = 0
    for operation in copies:
        if not isinstance(operation, list) or len(operation) != 2 or any(type(n) is not int for n in operation):
            raise ValueError("invalid COPY operation")
        offset, count = operation
        if offset < 0 or count <= 0 or offset + count > program["source"]["bytes"] or written + count > size:
            raise ValueError("COPY operation exceeds input or output bounds")
        written += count
    if written != size:
        raise ValueError("incomplete normalization output")


def apply_program(source: bytes, program: dict) -> bytes:
    validate_program(program)
    if program["source"] != identity(source):
        raise ValueError("normalization source identity mismatch")
    output = bytearray()
    for offset, count in program["copies"]:
        output.extend(source[offset:offset+count])
    result = bytes(output)
    if identity(result) != program["canonical"]:
        raise ValueError("canonical output identity mismatch")
    return result


def normalize_file(source: Path, destination: Path, program: dict) -> dict:
    if source.is_symlink() or not source.is_file() or source.stat().st_size > MAX_BYTES:
        raise ValueError("invalid normalization source file")
    if source.resolve() == destination.resolve() or destination.exists():
        raise ValueError("normalization requires a new output path")
    result = apply_program(source.read_bytes(), program)
    atomic_write_bytes(destination, result)
    return identity(result)
