#!/usr/bin/env python3
"""Synthesize a target disassembly export for direct packet comparison.

This is a narrow fallback for environments where the full Ghidra export is not
available. It writes the same header/instruction shape consumed by
compare_runtime_objects.read_target_functions, using OOT3D function entries and
instruction counts from the local direct-conversion analysis.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import subprocess
import tempfile
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CODE_BIN = Path(r"E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin")
DEFAULT_OBJDUMP = Path(r"C:\devkitPro\devkitARM\bin\arm-none-eabi-objdump.exe")
OBJDUMP_INSN_RE = re.compile(r"^\s*(?P<addr>[0-9a-fA-F]+):\s+(?:[0-9a-fA-F]{2,8}\s+)+\t(?P<op>.+?)\s*$")


def read_json(path: Path, default: Any) -> Any:
    if not path.is_file():
        return default
    return json.loads(path.read_text(encoding="utf-8-sig"))


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


def parse_hex(value: object) -> int | None:
    text = str(value or "").strip().lower()
    if not text:
        return None
    try:
        return int(text, 16)
    except ValueError:
        return None


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def load_symbol_entries(path: Path) -> list[int]:
    entries: list[int] = []
    for row in read_csv(path):
        if row.get("kind") != "function":
            continue
        entry = parse_hex(row.get("entry"))
        if entry is not None:
            entries.append(entry)
    return sorted(set(entries))


def next_symbol_entry(entries: list[int], entry: int) -> int | None:
    for candidate in entries:
        if candidate > entry:
            return candidate
    return None


def load_port_names(path: Path) -> dict[str, str]:
    names: dict[str, str] = {}
    for row in read_csv(path):
        entry = str(row.get("oot3d_entry") or "").strip().lower()
        name = str(row.get("oot3d_name") or "").strip()
        if entry and name:
            names[entry] = name
    return names


def collect_target_rows(args: argparse.Namespace) -> list[dict[str, Any]]:
    rows_by_entry: dict[str, dict[str, Any]] = {}
    port_names = load_port_names(args.port_map)

    for source_path in [args.frontier_json, args.readiness_json]:
        data = read_json(source_path, {})
        for row in data.get("rows", []) if isinstance(data, dict) else []:
            if not isinstance(row, dict):
                continue
            entry = str(row.get("entry") or row.get("oot3d_entry") or "").strip().lower()
            if not entry:
                continue
            existing = rows_by_entry.setdefault(entry, {"entry": entry})
            existing["name"] = (
                str(row.get("oot3d_name") or "").strip()
                or port_names.get(entry)
                or existing.get("name")
                or f"FUN_{entry}"
            )
            count = row.get("target_instruction_count")
            if count not in (None, ""):
                existing["target_instruction_count"] = int(count)

    probe_data = read_json(args.probe_json, {})
    for row in probe_data.get("rows", []) if isinstance(probe_data, dict) else []:
        if not isinstance(row, dict):
            continue
        entry = str(row.get("entry") or "").strip().lower()
        if not entry:
            continue
        existing = rows_by_entry.setdefault(entry, {"entry": entry})
        existing["name"] = (
            port_names.get(entry)
            or str(row.get("name") or "").strip()
            or existing.get("name")
            or f"FUN_{entry}"
        )

    return sorted(rows_by_entry.values(), key=lambda item: int(str(item["entry"]), 16))


def disassemble_range(objdump: Path, data: bytes, entry: int, count_bytes: int) -> list[str]:
    with tempfile.TemporaryDirectory(prefix="oot3d_disasm_") as tmp_dir_name:
        tmp_path = Path(tmp_dir_name) / f"{entry:08x}.bin"
        tmp_path.write_bytes(data)
        command = [
            str(objdump),
            "-D",
            "-b",
            "binary",
            "-m",
            "arm",
            f"--adjust-vma=0x{entry:08x}",
            str(tmp_path),
        ]
        result = subprocess.run(command, check=True, text=True, capture_output=True)

    lines: list[str] = []
    end = entry + count_bytes
    for raw_line in result.stdout.splitlines():
        match = OBJDUMP_INSN_RE.match(raw_line)
        if not match:
            continue
        addr = int(match.group("addr"), 16)
        if addr >= end:
            continue
        lines.append(f"{addr:08x}: {match.group('op').strip()}")
    return lines


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--code-bin", type=Path, default=DEFAULT_CODE_BIN)
    parser.add_argument("--code-base", default="0x00100000")
    parser.add_argument("--objdump", type=Path, default=DEFAULT_OBJDUMP)
    parser.add_argument("--out", type=Path, default=ROOT / "ghidra_export" / "disassembly.txt")
    parser.add_argument("--probe-json", type=Path, default=ROOT / "analysis" / "direct_packet_compile_probe.json")
    parser.add_argument("--frontier-json", type=Path, default=ROOT / "analysis" / "c_reconstruction_frontier.json")
    parser.add_argument("--readiness-json", type=Path, default=ROOT / "analysis" / "c_conversion_readiness.json")
    parser.add_argument("--port-map", type=Path, default=ROOT / "metadata" / "n64_port_map.csv")
    parser.add_argument("--manual-symbols", type=Path, default=ROOT / "symbols" / "manual_symbols.csv")
    args = parser.parse_args()

    if not args.code_bin.is_file():
        raise SystemExit(f"code.bin not found: {args.code_bin}")
    if not args.objdump.is_file():
        raise SystemExit(f"objdump not found: {args.objdump}")

    code = args.code_bin.read_bytes()
    code_base = int(str(args.code_base), 0)
    symbol_entries = load_symbol_entries(args.manual_symbols)
    rows = collect_target_rows(args)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    emitted = 0
    with args.out.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write("// synthetic OOT3D target disassembly generated from extracted code.bin\n")
        handle.write(f"// code_bin: {args.code_bin}\n\n")
        for row in rows:
            entry = int(str(row["entry"]), 16)
            offset = entry - code_base
            if offset < 0 or offset >= len(code):
                continue
            count = int(row.get("target_instruction_count") or 0)
            if count <= 0:
                next_entry = next_symbol_entry(symbol_entries, entry)
                count_bytes = (next_entry - entry) if next_entry is not None else 0
            else:
                count_bytes = count * 4
            if count_bytes <= 0:
                continue
            count_bytes = min(count_bytes, len(code) - offset)
            lines = disassemble_range(args.objdump, code[offset : offset + count_bytes], entry, count_bytes)
            if not lines:
                continue
            handle.write(f"\n\n// {row['name']} @ {entry:08x}\n")
            for line in lines:
                handle.write(line + "\n")
            emitted += 1

    summary = {
        "output": rel(args.out),
        "target_rows": len(rows),
        "emitted_functions": emitted,
        "code_bin": str(args.code_bin),
    }
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
