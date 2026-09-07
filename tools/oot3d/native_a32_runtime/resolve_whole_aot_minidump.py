#!/usr/bin/env python3
"""Resolve an OoT3D product minidump against native and guest symbols."""

from __future__ import annotations

import argparse
import bisect
import json
import re
import struct
import zipfile
from pathlib import Path
from typing import Any


OUTPUT_FORMAT = "oot3d_whole_aot_crash_resolution_v1"
_EXCEPTION_STREAM = 6
_MODULE_LIST_STREAM = 4
_LINK_MAP_BASE = re.compile(r"Preferred load address is\s+([0-9A-Fa-f]+)")
_LINK_MAP_SYMBOL = re.compile(
    r"^\s*[0-9A-Fa-f]+:[0-9A-Fa-f]+\s+(\S+)\s+"
    r"([0-9A-Fa-f]{16})\s+(.+)$"
)


def _checked_slice(data: bytes, offset: int, size: int) -> bytes:
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ValueError("minidump contains an out-of-range stream")
    return data[offset : offset + size]


def _read_minidump_string(data: bytes, offset: int) -> str:
    length = struct.unpack("<I", _checked_slice(data, offset, 4))[0]
    encoded = _checked_slice(data, offset + 4, length)
    return encoded.decode("utf-16-le", errors="replace")


def parse_minidump(data: bytes) -> dict[str, Any]:
    if len(data) < 32 or data[:4] != b"MDMP":
        raise ValueError("input is not a minidump")
    _, stream_count, directory_rva, _, timestamp, flags = struct.unpack_from(
        "<IIIIIQ", data, 4
    )
    streams: dict[int, tuple[int, int]] = {}
    for index in range(stream_count):
        stream_type, size, rva = struct.unpack(
            "<III", _checked_slice(data, directory_rva + index * 12, 12)
        )
        streams[stream_type] = (rva, size)

    if _EXCEPTION_STREAM not in streams or _MODULE_LIST_STREAM not in streams:
        raise ValueError("minidump lacks exception or module information")
    exception_rva, exception_size = streams[_EXCEPTION_STREAM]
    if exception_size < 160:
        raise ValueError("minidump exception stream is truncated")
    exception = {
        "thread_id": struct.unpack_from("<I", data, exception_rva)[0],
        "code": struct.unpack_from("<I", data, exception_rva + 8)[0],
        "flags": struct.unpack_from("<I", data, exception_rva + 12)[0],
        "address": struct.unpack_from("<Q", data, exception_rva + 24)[0],
    }

    module_rva, module_size = streams[_MODULE_LIST_STREAM]
    if module_size < 4:
        raise ValueError("minidump module stream is truncated")
    module_count = struct.unpack_from("<I", data, module_rva)[0]
    if module_size < 4 + module_count * 108:
        raise ValueError("minidump module records are truncated")
    modules = []
    for index in range(module_count):
        offset = module_rva + 4 + index * 108
        base = struct.unpack_from("<Q", data, offset)[0]
        size, checksum, module_timestamp, name_rva = struct.unpack_from(
            "<IIII", data, offset + 8
        )
        modules.append(
            {
                "name": _read_minidump_string(data, name_rva),
                "base": base,
                "size": size,
                "checksum": checksum,
                "timestamp": module_timestamp,
            }
        )
    return {
        "timestamp": timestamp,
        "flags": flags,
        "exception": exception,
        "modules": modules,
    }


def _nearest_link_symbol(link_map: str, rva: int) -> dict[str, Any] | None:
    image_base: int | None = None
    best: tuple[int, str, str] | None = None
    for line in link_map.splitlines():
        if image_base is None:
            base_match = _LINK_MAP_BASE.search(line)
            if base_match is not None:
                image_base = int(base_match.group(1), 16)
        symbol_match = _LINK_MAP_SYMBOL.match(line)
        if symbol_match is None or image_base is None:
            continue
        symbol_rva = int(symbol_match.group(2), 16) - image_base
        if symbol_rva <= rva and (best is None or symbol_rva > best[0]):
            best = (symbol_rva, symbol_match.group(1), symbol_match.group(3))
    if best is None:
        return None
    return {
        "symbol": best[1],
        "symbol_rva": best[0],
        "offset": rva - best[0],
        "object": best[2],
    }


def resolve_minidump(
    dump: dict[str, Any], guest_map: dict[str, Any], link_map: str | None = None
) -> dict[str, Any]:
    exception = dump["exception"]
    address = int(exception["address"])
    containing = [
        module
        for module in dump["modules"]
        if int(module["base"]) <= address < int(module["base"]) + int(module["size"])
    ]
    module = containing[0] if containing else None
    result: dict[str, Any] = {
        "format": OUTPUT_FORMAT,
        "exception": {
            "thread_id": int(exception["thread_id"]),
            "code": int(exception["code"]),
            "code_hex": f"0x{int(exception['code']):08X}",
            "flags": int(exception["flags"]),
            "address": address,
            "address_hex": f"0x{address:016X}",
        },
        "module": None,
        "image_matches_symbols": False,
        "whole_aot": {"resolved": False, "reason": "address_outside_modules"},
    }
    if module is None:
        return result

    rva = address - int(module["base"])
    result["module"] = {
        **module,
        "base_hex": f"0x{int(module['base']):016X}",
        "rva": rva,
        "rva_hex": f"0x{rva:08X}",
    }
    matches = (
        int(module["timestamp"]) == int(guest_map["native_image_timestamp"])
        and int(module["size"]) == int(guest_map["native_image_size"])
    )
    result["image_matches_symbols"] = matches
    if not matches:
        result["whole_aot"] = {
            "resolved": False,
            "reason": "module_timestamp_or_size_mismatch",
        }
        return result

    functions = sorted(guest_map["functions"], key=lambda item: int(item["native_rva"]))
    starts = [int(function["native_rva"]) for function in functions]
    index = bisect.bisect_right(starts, rva) - 1
    owner = functions[index] if index >= 0 else None
    if owner is not None and rva < int(owner["native_end_rva"]):
        result["whole_aot"] = {
            "resolved": True,
            "guest_owner_entry": int(owner["entry"]),
            "guest_owner_entry_hex": f"0x{int(owner['entry']):08X}",
            "guest_name": str(owner["name"]),
            "guest_ranges": owner["ranges"],
            "native_symbol": str(owner["native_symbol"]),
            "native_rva": int(owner["native_rva"]),
            "native_offset": rva - int(owner["native_rva"]),
            "native_size": int(owner["native_size"]),
            "shard": int(owner["shard"]),
            "shard_file": str(owner["shard_file"]),
        }
    else:
        result["whole_aot"] = {
            "resolved": False,
            "reason": "instruction_pointer_outside_whole_aot_bodies",
        }
    if link_map is not None:
        result["nearest_native_symbol"] = _nearest_link_symbol(link_map, rva)
    return result


def _read_link_map(path: Path) -> str:
    if path.suffix.lower() != ".zip":
        return path.read_text(encoding="utf-8", errors="replace")
    with zipfile.ZipFile(path) as archive:
        candidates = [name for name in archive.namelist() if name.lower().endswith(".map")]
        if len(candidates) != 1:
            raise ValueError("link MAP archive must contain exactly one .map file")
        return archive.read(candidates[0]).decode("utf-8", errors="replace")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dump", required=True, type=Path)
    parser.add_argument("--guest-map", required=True, type=Path)
    parser.add_argument("--link-map", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    dump = parse_minidump(args.dump.read_bytes())
    guest_map = json.loads(args.guest_map.read_text(encoding="utf-8"))
    if guest_map.get("format") != "oot3d_whole_aot_guest_map_v1":
        raise ValueError("unsupported whole-AOT guest map")
    link_map = _read_link_map(args.link_map) if args.link_map is not None else None
    result = resolve_minidump(dump, guest_map, link_map)
    result["dump"] = str(args.dump)
    encoded = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded, encoding="utf-8")
    print(encoded, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
