#!/usr/bin/env python3
"""Generate a deterministic guest-PC to native whole-AOT symbol map."""

from __future__ import annotations

import argparse
import json
import re
import struct
from pathlib import Path
from typing import Any


OUTPUT_FORMAT = "oot3d_whole_aot_guest_map_v1"
_PREFERRED_BASE_PATTERN = re.compile(
    r"Preferred load address is\s+([0-9A-Fa-f]+)"
)
_WHOLE_AOT_MAP_SYMBOL_PATTERN = re.compile(
    r"^\s*[0-9A-Fa-f]+:[0-9A-Fa-f]+\s+"
    r"\?(Execute_[^@]+)@GeneratedWholeAot@Oot3dNativeGame@@\S+\s+"
    r"([0-9A-Fa-f]+)\s+"
)


def native_symbol(name: str, entry: int) -> str:
    sanitized = re.sub(r"[^A-Za-z0-9_]", "_", name)
    if not sanitized or sanitized[0].isdigit():
        sanitized = "Function_" + sanitized
    return (
        "Oot3dNativeGame::GeneratedWholeAot::Execute_"
        f"{sanitized}_{entry:08X}"
    )


def _load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"expected a JSON object: {path}")
    return value


def _function_ranges(
    function: dict[str, Any], blocks_by_id: dict[int, dict[str, Any]]
) -> list[list[int]]:
    ranges = sorted(
        (
            int(blocks_by_id[int(block_id)]["pc"]),
            int(blocks_by_id[int(block_id)]["end_pc"]),
        )
        for block_id in function["blocks"]
    )
    merged: list[list[int]] = []
    for start, end in ranges:
        if not merged or start > merged[-1][1]:
            merged.append([start, end])
        else:
            merged[-1][1] = max(merged[-1][1], end)
    return merged


def attach_native_addresses(result: dict[str, Any], link_map: str) -> None:
    image_base: int | None = None
    addresses: dict[str, int] = {}
    for line in link_map.splitlines():
        if image_base is None:
            base_match = _PREFERRED_BASE_PATTERN.search(line)
            if base_match is not None:
                image_base = int(base_match.group(1), 16)
        symbol_match = _WHOLE_AOT_MAP_SYMBOL_PATTERN.match(line)
        if symbol_match is None:
            continue
        symbol = symbol_match.group(1)
        if symbol in addresses:
            raise ValueError(f"native link map contains duplicate symbol {symbol}")
        addresses[symbol] = int(symbol_match.group(2), 16)

    if image_base is None:
        raise ValueError("native link map has no preferred load address")
    expected = {
        str(record["native_symbol"]).rsplit("::", 1)[-1]
        for record in result["functions"]
    }
    missing = sorted(expected - addresses.keys())
    unexpected = sorted(addresses.keys() - expected)
    if missing or unexpected:
        detail = []
        if missing:
            detail.append(f"{len(missing)} missing")
        if unexpected:
            detail.append(f"{len(unexpected)} unexpected")
        raise ValueError("native whole-AOT symbols differ: " + ", ".join(detail))

    for record in result["functions"]:
        symbol = str(record["native_symbol"]).rsplit("::", 1)[-1]
        native_va = addresses[symbol]
        if native_va < image_base:
            raise ValueError(f"native symbol {symbol} precedes the image base")
        record["native_va"] = native_va
        record["native_rva"] = native_va - image_base
    result["native_image_base"] = image_base
    result["native_symbol_count"] = len(addresses)


def _pe_exception_ranges(image: bytes) -> tuple[dict[int, int], dict[str, int]]:
    if len(image) < 0x40 or image[:2] != b"MZ":
        raise ValueError("native executable is not a PE image")
    pe_offset = struct.unpack_from("<I", image, 0x3C)[0]
    if pe_offset + 24 > len(image) or image[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise ValueError("native executable has no valid PE header")
    coff_offset = pe_offset + 4
    machine, section_count, timestamp, _, _, optional_size, _ = struct.unpack_from(
        "<HHIIIHH", image, coff_offset
    )
    optional_offset = coff_offset + 20
    if optional_offset + optional_size > len(image):
        raise ValueError("native executable has a truncated optional header")
    magic = struct.unpack_from("<H", image, optional_offset)[0]
    if magic == 0x20B:
        directory_offset = optional_offset + 112
    elif magic == 0x10B:
        directory_offset = optional_offset + 96
    else:
        raise ValueError("native executable has an unsupported PE format")
    image_size = struct.unpack_from("<I", image, optional_offset + 56)[0]
    exception_rva, exception_size = struct.unpack_from(
        "<II", image, directory_offset + 3 * 8
    )
    section_offset = optional_offset + optional_size
    sections: list[tuple[int, int, int, int]] = []
    for index in range(section_count):
        offset = section_offset + index * 40
        if offset + 40 > len(image):
            raise ValueError("native executable has truncated section headers")
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
            "<IIII", image, offset + 8
        )
        sections.append((virtual_address, virtual_size, raw_size, raw_offset))

    def rva_to_offset(rva: int) -> int:
        for virtual_address, virtual_size, raw_size, raw_offset in sections:
            if virtual_address <= rva < virtual_address + max(
                virtual_size, raw_size
            ):
                return raw_offset + rva - virtual_address
        raise ValueError(f"PE RVA 0x{rva:08X} is outside all sections")

    exception_offset = rva_to_offset(exception_rva)
    if exception_offset + exception_size > len(image):
        raise ValueError("native executable has a truncated exception directory")
    ranges: dict[int, int] = {}
    for offset in range(
        exception_offset,
        exception_offset + exception_size - exception_size % 12,
        12,
    ):
        begin, end, _ = struct.unpack_from("<III", image, offset)
        if begin == 0 or end <= begin:
            continue
        previous = ranges.setdefault(begin, end)
        if previous != end:
            raise ValueError(f"conflicting PE function extent at RVA 0x{begin:08X}")
    return ranges, {
        "machine": machine,
        "timestamp": timestamp,
        "image_size": image_size,
    }


def attach_native_extents(result: dict[str, Any], executable: bytes) -> None:
    if "native_symbol_count" not in result:
        raise ValueError("native addresses must be attached before PE extents")
    ranges, image = _pe_exception_ranges(executable)
    missing: list[int] = []
    for record in result["functions"]:
        begin = int(record["native_rva"])
        end = ranges.get(begin)
        if end is None:
            missing.append(begin)
            continue
        record["native_end_rva"] = end
        record["native_size"] = end - begin
    if missing:
        raise ValueError(
            f"{len(missing)} native whole-AOT symbols have no PE extent; "
            f"first RVA 0x{missing[0]:08X}"
        )
    result["native_extent_count"] = len(result["functions"])
    result["native_machine"] = image["machine"]
    result["native_image_timestamp"] = image["timestamp"]
    result["native_image_size"] = image["image_size"]


def build_guest_map(
    program: dict[str, Any], generated: dict[str, Any]
) -> dict[str, Any]:
    if program.get("code_sha256") != generated.get("code_sha256"):
        raise ValueError("program and generated manifest code hashes differ")

    generated_functions = generated.get("functions")
    program_functions = program.get("functions")
    shards = generated.get("shards")
    if not isinstance(generated_functions, list):
        raise ValueError("generated manifest has no function list")
    if not isinstance(program_functions, list):
        raise ValueError("program has no function list")
    if not isinstance(shards, list):
        raise ValueError("generated manifest has no shard list")

    program_blocks = program.get("blocks")
    if not isinstance(program_blocks, list):
        raise ValueError("program has no block list")
    blocks_by_id = {int(item["id"]): item for item in program_blocks}
    if len(blocks_by_id) != len(program_blocks):
        raise ValueError("program contains duplicate block identifiers")
    program_by_entry = {int(item["entry"]): item for item in program_functions}
    if len(program_by_entry) != len(program_functions):
        raise ValueError("program contains duplicate function entries")

    shard_by_entry: dict[int, tuple[int, str]] = {}
    for index, shard in enumerate(shards):
        filename = str(shard["file"])
        entries = shard.get("entries")
        if not isinstance(entries, list):
            raise ValueError(f"shard {index} has no entry list")
        if int(shard.get("functions", -1)) != len(entries):
            raise ValueError(f"shard {index} function count differs from entries")
        for raw_entry in entries:
            entry = int(raw_entry)
            if entry in shard_by_entry:
                raise ValueError(f"duplicate shard owner 0x{entry:08X}")
            shard_by_entry[entry] = (index, filename)

    generated_entries = {int(function["entry"]) for function in generated_functions}
    missing_shards = sorted(generated_entries - shard_by_entry.keys())
    unexpected_shards = sorted(shard_by_entry.keys() - generated_entries)
    if missing_shards:
        raise ValueError(
            f"generated owner 0x{missing_shards[0]:08X} has no shard"
        )
    if unexpected_shards:
        raise ValueError(
            f"shard owner 0x{unexpected_shards[0]:08X} has no generated function"
        )

    entry_owners = {
        int(function["entry"]): int(function["entry"])
        for function in generated_functions
    }
    raw_dispatch_reference_count = len(entry_owners)
    for function in generated_functions:
        owner = int(function["entry"])
        for raw_pc in function["dispatch_entries"]:
            pc = int(raw_pc)
            raw_dispatch_reference_count += 1
            entry_owners.setdefault(pc, owner)
    dispatch_by_owner: dict[int, list[int]] = {
        entry: [] for entry in shard_by_entry
    }
    for pc, owner in sorted(entry_owners.items()):
        dispatch_by_owner[owner].append(pc)

    records: list[dict[str, Any]] = []
    resume_entry_count = 0
    range_count = 0
    for generated_function in sorted(
        generated_functions, key=lambda item: int(item["entry"])
    ):
        entry = int(generated_function["entry"])
        program_function = program_by_entry.get(entry)
        if program_function is None:
            raise ValueError(f"generated owner 0x{entry:08X} is absent from program")
        shard = shard_by_entry.get(entry)
        if shard is None:
            raise ValueError(f"generated owner 0x{entry:08X} has no shard")
        name = str(generated_function["name"])
        if name != str(program_function["name"]):
            raise ValueError(f"owner name differs at 0x{entry:08X}")
        dispatch_entries = dispatch_by_owner[entry]
        resume_entries = sorted(
            int(pc) for pc in generated_function["resume_entries"]
        )
        ranges = _function_ranges(program_function, blocks_by_id)
        if len(program_function["blocks"]) != int(generated_function["blocks"]):
            raise ValueError(f"owner block count differs at 0x{entry:08X}")
        resume_entry_count += len(resume_entries)
        range_count += len(ranges)
        records.append(
            {
                "entry": entry,
                "name": name,
                "native_symbol": native_symbol(name, entry),
                "shard": shard[0],
                "shard_file": shard[1],
                "blocks": int(generated_function["blocks"]),
                "instructions": int(generated_function["instructions"]),
                "ranges": ranges,
                "dispatch_entries": dispatch_entries,
                "resume_entries": resume_entries,
            }
        )

    if len(records) != len(shard_by_entry):
        raise ValueError("shard owners and generated functions differ")

    external_entries = sorted(int(entry) for entry in generated["external_functions"])
    return {
        "format": OUTPUT_FORMAT,
        "code_sha256": str(generated["code_sha256"]),
        "program_sha256": str(generated["program_sha256"]),
        "generator_sha256": str(generated["generator_sha256"]),
        "function_count": len(records),
        "dispatch_entry_count": len(entry_owners),
        "dispatcher_alias_count": raw_dispatch_reference_count - len(entry_owners),
        "resume_entry_count": resume_entry_count,
        "range_count": range_count,
        "shard_count": len(shards),
        "external_entries": external_entries,
        "functions": records,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--program", required=True, type=Path)
    parser.add_argument("--generated-manifest", required=True, type=Path)
    parser.add_argument("--link-map", type=Path)
    parser.add_argument("--executable", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    result = build_guest_map(
        _load_json(args.program), _load_json(args.generated_manifest)
    )
    if args.link_map is not None:
        attach_native_addresses(
            result, args.link_map.read_text(encoding="utf-8", errors="replace")
        )
    if args.executable is not None:
        if args.link_map is None:
            raise ValueError("--executable requires --link-map")
        attach_native_extents(result, args.executable.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_name(args.output.name + ".tmp")
    temporary.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    temporary.replace(args.output)
    print(
        json.dumps(
            {
                "output": str(args.output),
                "functions": result["function_count"],
                "dispatch_entries": result["dispatch_entry_count"],
                "shards": result["shard_count"],
                "native_symbols": result.get("native_symbol_count", 0),
                "native_extents": result.get("native_extent_count", 0),
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
