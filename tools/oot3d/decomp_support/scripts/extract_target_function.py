#!/usr/bin/env python3
"""Extract one target function from the Ghidra export for matching work."""

from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path


HEADER_RE = re.compile(r"^//\s+(?P<name>\S+)\s+@\s+(?P<entry>[0-9a-fA-F]+)\s*$")
INSN_RE = re.compile(r"^(?P<addr>[0-9a-fA-F]+):\s+(?P<op>.+?)\s*$")


def normalize_entry(value: str) -> str:
    value = value.strip().lower()
    if value.startswith("0x"):
        value = value[2:]
    return value.zfill(8)


def load_functions(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def load_manual_symbol_aliases(path: Path) -> dict[str, str]:
    if not path.is_file():
        return {}

    aliases: dict[str, str] = {}
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            if row.get("kind") == "function":
                aliases[normalize_entry(row["entry"])] = row["new_name"]
    return aliases


def resolve_function(functions: list[dict[str, str]], name: str | None, entry: str | None) -> dict[str, str]:
    if entry:
        wanted = normalize_entry(entry)
        matches = [row for row in functions if normalize_entry(row["entry"]) == wanted]
    elif name:
        matches = [row for row in functions if row["name"] == name]
    else:
        raise ValueError("provide --name or --entry")

    if not matches:
        key = entry if entry else name
        raise ValueError(f"function not found: {key}")
    if len(matches) > 1:
        names = ", ".join(f'{row["name"]}@{row["entry"]}' for row in matches[:8])
        raise ValueError(f"ambiguous function selector: {names}")
    return matches[0]


def extract_disassembly(path: Path, name: str, entry: str) -> list[tuple[str, str]]:
    wanted_entry = normalize_entry(entry)
    current = False
    instructions: list[tuple[str, str]] = []

    for line in path.read_text(encoding="utf-8").splitlines():
        header = HEADER_RE.match(line)
        if header:
            current = header.group("name") == name and normalize_entry(header.group("entry")) == wanted_entry
            continue

        if current and not line.strip():
            break

        if current:
            insn = INSN_RE.match(line)
            if insn:
                instructions.append((normalize_entry(insn.group("addr")), insn.group("op").strip()))

    if not instructions:
        raise ValueError(f"disassembly not found for {name}@{wanted_entry}")
    return instructions


def find_decompiled(decompiled_dir: Path, entry: str) -> Path | None:
    wanted = normalize_entry(entry)
    matches = sorted(decompiled_dir.glob(f"*_{wanted}_*.c"))
    return matches[0] if matches else None


def write_outputs(
    out_dir: Path,
    function: dict[str, str],
    instructions: list[tuple[str, str]],
    decompiled: Path | None,
    aliases_by_entry: dict[str, str],
) -> None:
    entry = normalize_entry(function["entry"])
    name = aliases_by_entry.get(entry, function["name"])
    entry = normalize_entry(function["entry"])
    stem = f"{entry}_{name}"
    out_dir.mkdir(parents=True, exist_ok=True)

    target_asm = out_dir / f"{stem}.target.s"
    scratch_asm = out_dir / f"{stem}.scratch.s"
    metadata = out_dir / f"{stem}.md"

    target_lines = [f"// {name} @ {entry}"]
    target_lines.extend(f"{addr}: {op}" for addr, op in instructions)
    target_asm.write_text("\n".join(target_lines) + "\n", encoding="utf-8")

    scratch_lines = [f"{name}:"]
    scratch_lines.extend(f"    {op}" for _, op in instructions)
    scratch_asm.write_text("\n".join(scratch_lines) + "\n", encoding="utf-8")

    lines = [
        f"# {name}",
        "",
        f"- Entry: `0x{entry}`",
        f"- Ghidra export name: `{function['name']}`",
        f"- Body: `0x{normalize_entry(function['body_min'])}`-`0x{normalize_entry(function['body_max'])}`",
        f"- Target assembly: `{target_asm}`",
        f"- Scratch assembly: `{scratch_asm}`",
    ]

    if decompiled:
        copied = out_dir / f"{stem}.ghidra.c"
        copied.write_text(decompiled.read_text(encoding="utf-8"), encoding="utf-8")
        lines.append(f"- Ghidra pseudocode: `{copied}`")
    else:
        lines.append("- Ghidra pseudocode: not found")

    metadata.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(metadata)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    selector = parser.add_mutually_exclusive_group(required=True)
    selector.add_argument("--name")
    selector.add_argument("--entry")
    parser.add_argument("--functions", type=Path, default=Path("ghidra_export/functions.csv"))
    parser.add_argument("--disassembly", type=Path, default=Path("ghidra_export/disassembly.txt"))
    parser.add_argument("--decompiled-dir", type=Path, default=Path("ghidra_export/decompiled"))
    parser.add_argument("--manual-symbols", type=Path, default=Path("symbols/manual_symbols.csv"))
    parser.add_argument("--out-dir", type=Path, default=Path("analysis/target_functions"))
    args = parser.parse_args()

    functions = load_functions(args.functions)
    function = resolve_function(functions, args.name, args.entry)
    instructions = extract_disassembly(args.disassembly, function["name"], function["entry"])
    decompiled = find_decompiled(args.decompiled_dir, function["entry"])
    aliases = load_manual_symbol_aliases(args.manual_symbols)
    write_outputs(args.out_dir, function, instructions, decompiled, aliases)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
