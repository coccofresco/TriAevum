"""Build a private TriAevum native-module container from a local native image."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path
from typing import Any, Sequence

try:
    from . import TOOL_VERSION
    from .common import sha256_file
except ImportError:
    from __init__ import TOOL_VERSION
    from common import sha256_file


MAGIC = b"TRIAEVM1"
FORMAT_MAJOR = 1
FORMAT_MINOR = 1
HEADER_SIZE = 64
SECTION_ENTRY_SIZE = 64
SECTION_ALIGNMENT = 16
RUNTIME_ABI_V1 = 1
PICA_SERVICE_V1 = 0x41434950
AUDIO_SERVICE_V1 = 0x49445541
INPUT_SERVICE_V1 = 0x33444948
FILESYSTEM_SERVICE_V1 = 0x53595346
MAXIMUM_PHYSICAL_MEMORY_REGIONS = 64
SECTION_METADATA_JSON = 1
SECTION_NATIVE_IMAGE = 2
SECTION_TITLE_AOT_IMAGE = 3
SECTION_REQUIRED = 1 << 0
SECTION_EXECUTABLE = 1 << 1
SUPPORTED_TARGET_TRIPLES = {
    "aarch64-apple-darwin",
    "aarch64-pc-windows-msvc",
    "aarch64-unknown-linux-gnu",
    "x86_64-apple-darwin",
    "x86_64-pc-windows-msvc",
    "x86_64-unknown-linux-gnu",
}
HEADER = struct.Struct("<8sHHHHIIQQ24s")
SECTION = struct.Struct("<IIQQ32sQ")


def _align(value: int) -> int:
    return (value + SECTION_ALIGNMENT - 1) & ~(SECTION_ALIGNMENT - 1)


def _identity(value: str, label: str) -> str:
    lowered = value.lower()
    if len(lowered) != 64 or any(
        character not in "0123456789abcdef" for character in lowered
    ):
        raise ValueError(f"{label} must be a lowercase or uppercase SHA-256")
    return lowered


def _physical_memory_region(value: str) -> tuple[int, int, int]:
    parts = value.split(":")
    if len(parts) != 3:
        raise argparse.ArgumentTypeError(
            "memory region must be PHYSICAL_BASE:GUEST_BASE:BYTES"
        )
    try:
        return tuple(int(part, 0) for part in parts)  # type: ignore[return-value]
    except ValueError as exc:
        raise argparse.ArgumentTypeError(
            "memory region contains an invalid integer"
        ) from exc


def _normalize_physical_memory_regions(
    regions: Sequence[tuple[int, int, int]],
) -> list[tuple[int, int, int]]:
    if len(regions) > MAXIMUM_PHYSICAL_MEMORY_REGIONS:
        raise ValueError("too many physical memory regions")
    normalized: list[tuple[int, int, int]] = []
    address_space_bytes = 1 << 32
    for region in regions:
        if len(region) != 3 or any(type(value) is not int for value in region):
            raise ValueError("physical memory regions must contain three integers")
        physical_base, guest_base, byte_count = region
        if (
            physical_base < 0
            or guest_base < 0
            or byte_count <= 0
            or physical_base > 0xFFFFFFFF
            or guest_base > 0xFFFFFFFF
            or byte_count > 0xFFFFFFFF
            or physical_base + byte_count > address_space_bytes
            or guest_base + byte_count > address_space_bytes
        ):
            raise ValueError(
                "physical memory region is outside the 32-bit address space"
            )
        normalized.append(region)

    def overlaps(
        left: tuple[int, int, int], right: tuple[int, int, int], index: int
    ) -> bool:
        left_base, right_base = left[index], right[index]
        return left_base < right_base + right[2] and right_base < left_base + left[2]

    for index, region in enumerate(normalized):
        for previous in normalized[:index]:
            if overlaps(region, previous, 0) or overlaps(region, previous, 1):
                raise ValueError("physical memory regions overlap")
    return sorted(normalized)


def build_tam(
    native_image: Path,
    *,
    title_aot_image: Path | None = None,
    recipe: str,
    target_triple: str,
    source_identity: str,
    translator_identity: str,
    required_services: Sequence[int] = (),
    physical_memory_regions: Sequence[tuple[int, int, int]] = (),
    runtime_abi: int = RUNTIME_ABI_V1,
) -> tuple[bytes, dict[str, Any]]:
    native_image = native_image.expanduser().resolve()
    if not native_image.is_file():
        raise ValueError(f"native module image does not exist: {native_image}")
    if not recipe.strip() or not target_triple.strip():
        raise ValueError("recipe and target triple must not be empty")
    if target_triple not in SUPPORTED_TARGET_TRIPLES:
        raise ValueError(f"unsupported target triple: {target_triple}")
    source_identity = _identity(source_identity, "source identity")
    translator_identity = _identity(translator_identity, "translator identity")
    if runtime_abi != RUNTIME_ABI_V1:
        raise ValueError(f"unsupported runtime ABI: {runtime_abi}")
    normalized_services = sorted(set(required_services))
    if len(normalized_services) != len(required_services) or any(
        service <= 0 or service > 0xFFFFFFFF for service in normalized_services
    ):
        raise ValueError("required service IDs must be unique nonzero uint32 values")
    normalized_regions = _normalize_physical_memory_regions(physical_memory_regions)
    if PICA_SERVICE_V1 in normalized_services and not normalized_regions:
        raise ValueError("PICA service requires at least one physical memory region")

    native_bytes = native_image.read_bytes()
    if not native_bytes:
        raise ValueError("native module image is empty")
    native_hash = hashlib.sha256(native_bytes).hexdigest()
    title_aot_bytes: bytes | None = None
    if title_aot_image is not None:
        title_aot_image = title_aot_image.expanduser().resolve()
        if not title_aot_image.is_file():
            raise ValueError(
                f"title AOT image does not exist: {title_aot_image}"
            )
        title_aot_bytes = title_aot_image.read_bytes()
        if not title_aot_bytes:
            raise ValueError("title AOT image is empty")
    metadata = {
        "format": "triaevum_module_metadata_v1",
        "private_local_artifact": True,
        "redistributable": False,
        "runtime_abi": runtime_abi,
        "query_symbol": "TriAevumQueryModuleV1",
        "recipe": recipe,
        "target_triple": target_triple,
        "source_identity_sha256": source_identity,
        "translator_identity_sha256": translator_identity,
        "native_image": {
            "bytes": len(native_bytes),
            "sha256": native_hash,
        },
        "forge_tool_version": TOOL_VERSION,
        "required_services": [
            {"id": service, "schema_version": 1} for service in normalized_services
        ],
        "physical_memory_regions": [
            {
                "physical_base": physical_base,
                "guest_base": guest_base,
                "bytes": byte_count,
            }
            for physical_base, guest_base, byte_count in normalized_regions
        ],
    }
    if title_aot_bytes is not None:
        metadata["title_aot_image"] = {
            "abi": 1,
            "bytes": len(title_aot_bytes),
            "sha256": hashlib.sha256(title_aot_bytes).hexdigest(),
        }
    metadata_bytes = (
        json.dumps(metadata, sort_keys=True, separators=(",", ":")) + "\n"
    ).encode("utf-8")
    sections = [
        (SECTION_METADATA_JSON, SECTION_REQUIRED, metadata_bytes),
        (
            SECTION_NATIVE_IMAGE,
            SECTION_REQUIRED | SECTION_EXECUTABLE,
            native_bytes,
        ),
    ]
    if title_aot_bytes is not None:
        sections.append(
            (
                SECTION_TITLE_AOT_IMAGE,
                SECTION_REQUIRED | SECTION_EXECUTABLE,
                title_aot_bytes,
            )
        )
    table_end = HEADER_SIZE + len(sections) * SECTION_ENTRY_SIZE
    cursor = _align(table_end)
    descriptors: list[tuple[int, int, int, bytes, bytes]] = []
    for section_type, flags, payload in sections:
        cursor = _align(cursor)
        descriptors.append(
            (section_type, flags, cursor, hashlib.sha256(payload).digest(), payload)
        )
        cursor += len(payload)

    output = bytearray(cursor)
    output[:HEADER_SIZE] = HEADER.pack(
        MAGIC,
        FORMAT_MAJOR,
        FORMAT_MINOR if title_aot_bytes is not None else 0,
        HEADER_SIZE,
        SECTION_ENTRY_SIZE,
        runtime_abi,
        len(sections),
        len(output),
        HEADER_SIZE,
        bytes(24),
    )
    for index, (section_type, flags, offset, digest, payload) in enumerate(descriptors):
        descriptor = HEADER_SIZE + index * SECTION_ENTRY_SIZE
        output[descriptor : descriptor + SECTION_ENTRY_SIZE] = SECTION.pack(
            section_type,
            flags,
            offset,
            len(payload),
            digest,
            0,
        )
        output[offset : offset + len(payload)] = payload
    return bytes(output), metadata


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native-image", type=Path, required=True)
    parser.add_argument("--title-aot-image", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--recipe", required=True)
    parser.add_argument("--target-triple", required=True)
    parser.add_argument("--source-identity", required=True)
    parser.add_argument("--translator-identity", required=True)
    parser.add_argument(
        "--required-service",
        action="append",
        default=[],
        type=lambda value: int(value, 0),
        help="required host service ID (decimal or 0x-prefixed; repeatable)",
    )
    parser.add_argument(
        "--physical-memory-region",
        action="append",
        default=[],
        type=_physical_memory_region,
        metavar="PHYSICAL:GUEST:BYTES",
        help="verified 32-bit physical-to-guest mapping (repeatable)",
    )
    args = parser.parse_args(argv)
    try:
        output = args.output.expanduser().resolve()
        if output.exists():
            raise ValueError(f"TAM output already exists: {output}")
        module, metadata = build_tam(
            args.native_image,
            title_aot_image=args.title_aot_image,
            recipe=args.recipe,
            target_triple=args.target_triple,
            source_identity=args.source_identity,
            translator_identity=args.translator_identity,
            required_services=args.required_service,
            physical_memory_regions=args.physical_memory_region,
        )
        output.parent.mkdir(parents=True, exist_ok=True)
        temporary = output.with_name(f".{output.name}.tmp")
        if temporary.exists():
            raise ValueError(f"stale TAM temporary output exists: {temporary}")
        temporary.write_bytes(module)
        temporary.replace(output)
    except (OSError, ValueError) as exc:
        print(json.dumps({"status": "error", "error": str(exc)}, indent=2))
        return 1
    print(
        json.dumps(
            {
                "status": "built",
                "output": str(output),
                "bytes": len(module),
                "sha256": sha256_file(output),
                "metadata": metadata,
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
