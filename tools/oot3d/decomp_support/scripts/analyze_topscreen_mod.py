#!/usr/bin/env python3
"""Classify TopScreenMod IPS records against the maintained OoT3D inventory."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import struct
import zipfile
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

from capstone import CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_LITTLE_ENDIAN, Cs


CODE_BASE = 0x00100000


@dataclass(frozen=True)
class IpsRecord:
    index: int
    offset: int
    size: int
    rle: bool
    payload: bytes


@dataclass(frozen=True)
class FunctionRange:
    entry: int
    end: int
    name: str
    module: str
    logical_file: str


def parse_hex(value: str) -> int:
    return int(value, 16)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_ips(data: bytes) -> list[IpsRecord]:
    if not data.startswith(b"PATCH"):
        raise ValueError("IPS header is missing")
    cursor = 5
    records: list[IpsRecord] = []
    while data[cursor : cursor + 3] != b"EOF":
        if cursor + 5 > len(data):
            raise ValueError("truncated IPS record")
        offset = int.from_bytes(data[cursor : cursor + 3], "big")
        encoded_size = int.from_bytes(data[cursor + 3 : cursor + 5], "big")
        cursor += 5
        if encoded_size == 0:
            if cursor + 3 > len(data):
                raise ValueError("truncated IPS RLE record")
            size = int.from_bytes(data[cursor : cursor + 2], "big")
            payload = data[cursor + 2 : cursor + 3] * size
            cursor += 3
            rle = True
        else:
            size = encoded_size
            if cursor + size > len(data):
                raise ValueError("truncated IPS payload")
            payload = data[cursor : cursor + size]
            cursor += size
            rle = False
        records.append(IpsRecord(len(records), offset, size, rle, payload))
    return records


def load_inventory(
    path: Path,
) -> tuple[list[FunctionRange], dict[int, list[FunctionRange]]]:
    functions: list[FunctionRange] = []
    references: dict[int, list[FunctionRange]] = {}
    with path.open(newline="", encoding="utf-8-sig") as source:
        for row in csv.DictReader(source):
            try:
                entry = parse_hex(row["entry"])
                end = parse_hex(row["end"])
            except (KeyError, TypeError, ValueError):
                continue
            if end < entry:
                continue
            function = FunctionRange(
                entry=entry,
                end=end,
                name=row.get("name", ""),
                module=row.get("module", ""),
                logical_file=row.get("logical_file", ""),
            )
            functions.append(function)
            for raw_reference in row.get("global_refs", "").split(";"):
                if not raw_reference:
                    continue
                try:
                    reference = parse_hex(raw_reference)
                except ValueError:
                    continue
                references.setdefault(reference, []).append(function)
    functions.sort(key=lambda function: function.entry)
    return functions, references


def owner_for(address: int, functions: list[FunctionRange]) -> FunctionRange | None:
    candidates = [
        function
        for function in functions
        if function.entry <= address <= function.end
    ]
    return max(candidates, key=lambda function: function.entry, default=None)


def disassemble(engine: Cs, address: int, payload: bytes) -> list[str]:
    if address % 4 != 0 or len(payload) % 4 != 0:
        return []
    return [
        f"0x{instruction.address:08X}: {instruction.mnemonic} {instruction.op_str}".rstrip()
        for instruction in engine.disasm(payload, address)
    ]


def scalar_values(payload: bytes) -> dict[str, list[Any]]:
    """Expose patch data without pretending every aligned word is ARM code."""
    result: dict[str, list[Any]] = {}
    if len(payload) % 2 == 0:
        result["u16_le"] = list(struct.unpack(f"<{len(payload) // 2}H", payload))
    if len(payload) % 4 == 0:
        result["u32_le"] = list(struct.unpack(f"<{len(payload) // 4}I", payload))
        result["f32_le"] = [
            value if abs(value) < 1.0e30 else None
            for value in struct.unpack(f"<{len(payload) // 4}f", payload)
        ]
    return result


def direct_hook_target(payload: bytes) -> int | None:
    # ARM absolute jump veneer: ldr pc, [pc, #-4]; .word target
    if len(payload) >= 8 and payload[:4] == bytes.fromhex("04f01fe5"):
        return struct.unpack_from("<I", payload, 4)[0]
    return None


def branch_targets(engine: Cs, address: int, payload: bytes) -> list[int]:
    targets: list[int] = []
    hook_target = direct_hook_target(payload)
    if hook_target is not None:
        targets.append(hook_target)
    for instruction in engine.disasm(payload, address):
        if instruction.mnemonic not in {"b", "bl"}:
            continue
        operand = instruction.op_str.removeprefix("#")
        if not operand.startswith("0x"):
            continue
        target = int(operand, 16)
        if target not in targets:
            targets.append(target)
    return targets


def classify(
    address: int,
    size: int,
    source_size: int,
    owner: FunctionRange | None,
    referenced_by: list[FunctionRange],
) -> str:
    if address == CODE_BASE:
        return "startup_hook"
    if address >= CODE_BASE + source_size:
        return "injected_payload"
    if owner is not None:
        return "function_patch"
    if referenced_by:
        return "referenced_data_patch"
    if address % 4 == 0 and size % 4 == 0:
        return "unowned_words"
    return "data_patch"


def contiguous_payload_regions(
    records: list[IpsRecord], source_size: int
) -> list[tuple[int, bytes]]:
    injected = sorted(
        (record for record in records if record.offset >= source_size),
        key=lambda record: record.offset,
    )
    regions: list[tuple[int, bytearray]] = []
    for record in injected:
        if regions and regions[-1][0] + len(regions[-1][1]) == record.offset:
            regions[-1][1].extend(record.payload)
        else:
            regions.append((record.offset, bytearray(record.payload)))
    return [(offset, bytes(payload)) for offset, payload in regions]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", required=True, type=Path)
    parser.add_argument("--ips-member", required=True)
    parser.add_argument("--code-bin", required=True, type=Path)
    parser.add_argument("--inventory", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--entries-output", type=Path)
    parser.add_argument("--payload-output", type=Path)
    arguments = parser.parse_args()

    source = arguments.code_bin.read_bytes()
    with zipfile.ZipFile(arguments.archive) as archive:
        ips_data = archive.read(arguments.ips_member)
    records = read_ips(ips_data)
    payload_regions = contiguous_payload_regions(records, len(source))
    payload_region_starts = {CODE_BASE + offset for offset, _ in payload_regions}
    functions, references = load_inventory(arguments.inventory)
    arm = Cs(CS_ARCH_ARM, CS_MODE_ARM | CS_MODE_LITTLE_ENDIAN)
    arm.detail = False

    analyzed: list[dict[str, object]] = []
    payload_entries: set[int] = set()
    for record in records:
        address = CODE_BASE + record.offset
        source_payload = (
            source[record.offset : record.offset + record.size]
            if record.offset < len(source)
            else b""
        )
        owner = owner_for(address, functions)
        referenced_by: list[FunctionRange] = []
        for word_address in range(address, address + record.size, 4):
            for function in references.get(word_address, []):
                if function not in referenced_by:
                    referenced_by.append(function)
        replacement_arm = disassemble(arm, address, record.payload)
        if address in payload_region_starts:
            payload_entries.add(address)
        replacement_targets = branch_targets(arm, address, record.payload)
        for target in replacement_targets:
            if target >= CODE_BASE + len(source):
                payload_entries.add(target)
        analyzed.append(
            {
                "index": record.index,
                "offset": record.offset,
                "address": f"0x{address:08X}",
                "size": record.size,
                "rle": record.rle,
                "classification": classify(
                    address, record.size, len(source), owner, referenced_by
                ),
                "owner": asdict(owner) if owner is not None else None,
                "referenced_by": [
                    asdict(function) for function in referenced_by
                ],
                "source_hex": source_payload.hex(),
                "replacement_hex": record.payload.hex(),
                "source_values": scalar_values(source_payload),
                "replacement_values": scalar_values(record.payload),
                "source_arm": disassemble(arm, address, source_payload),
                "replacement_arm": replacement_arm,
                "hook_target": (
                    f"0x{target:08X}"
                    if (target := direct_hook_target(record.payload)) is not None
                    else None
                ),
                "branch_targets": [
                    f"0x{target:08X}" for target in replacement_targets
                ],
            }
        )

    counts: dict[str, int] = {}
    for record in analyzed:
        key = str(record["classification"])
        counts[key] = counts.get(key, 0) + 1
    result = {
        "format": "oot3d_topscreen_mod_analysis_v2",
        "inputs": {
            "archive": str(arguments.archive.resolve()),
            "archive_sha256": sha256(arguments.archive.read_bytes()),
            "ips_member": arguments.ips_member,
            "ips_sha256": sha256(ips_data),
            "code_bin": str(arguments.code_bin.resolve()),
            "code_bin_sha256": sha256(source),
            "inventory": str(arguments.inventory.resolve()),
        },
        "summary": {
            "record_count": len(records),
            "replacement_bytes": sum(record.size for record in records),
            "classification_counts": counts,
        },
        "records": analyzed,
    }
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(
        json.dumps(result, indent=2) + "\n", encoding="utf-8"
    )
    if arguments.entries_output is not None:
        arguments.entries_output.parent.mkdir(parents=True, exist_ok=True)
        arguments.entries_output.write_text(
            "".join(f"0x{entry:08X}\n" for entry in sorted(payload_entries)),
            encoding="ascii",
        )
    if arguments.payload_output is not None:
        if len(payload_regions) != 1:
            raise ValueError(
                "expected exactly one contiguous injected payload region"
            )
        arguments.payload_output.parent.mkdir(parents=True, exist_ok=True)
        arguments.payload_output.write_bytes(payload_regions[0][1])
    print(json.dumps(result["summary"], indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
