#!/usr/bin/env python3
"""Generate a naked C inline-assembly seed from Ghidra target disassembly.

This is a bootstrap tool for object-matching work. The generated source is not
intended to be the final structured decompilation; it creates a compilable,
compareable foothold that can later be replaced function-by-function with
ordinary C.
"""

from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path


HEADER_RE = re.compile(r"^//\s+(?P<name>\S+)\s+@\s+(?P<entry>[0-9a-fA-F]+)\s*$")
INSN_RE = re.compile(r"^(?P<addr>[0-9a-fA-F]+):\s+(?P<op>.+?)\s*$")
BRANCH_RE = re.compile(r"^(?P<mnemonic>b(?:l|x)?[a-z]*)\s+0x(?P<target>[0-9a-fA-F]+)$")
ABS_LITERAL_RE = re.compile(r"^(?P<mnemonic>(?:v?ldr)(?:\.\d+)?)\s+(?P<dst>[^,]+),\[(?P<addr>0x[0-9a-fA-F]+)\]$")
PC_LITERAL_RE = re.compile(r"^(?P<mnemonic>(?:v?ldr)(?:\.\d+)?)\s+(?P<dst>[^,]+),\[pc,#(?P<offset>0x[0-9a-fA-F]+|\d+)\]$")


def normalize_entry(value: str) -> str:
    value = value.strip().lower()
    if value.startswith("0x"):
        value = value[2:]
    return value.zfill(8)


def load_functions(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def function_names_by_entry(functions: list[dict[str, str]]) -> dict[str, str]:
    return {normalize_entry(row["entry"]): row["name"] for row in functions}


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


def extract_disassembly(path: Path, function: dict[str, str]) -> list[tuple[int, str]]:
    wanted_name = function["name"]
    wanted_entry = normalize_entry(function["entry"])
    current = False
    instructions: list[tuple[int, str]] = []

    for line in path.read_text(encoding="utf-8").splitlines():
        header = HEADER_RE.match(line)
        if header:
            current = header.group("name") == wanted_name and normalize_entry(header.group("entry")) == wanted_entry
            continue

        if current and not line.strip():
            break

        if current:
            insn = INSN_RE.match(line)
            if insn:
                instructions.append((int(insn.group("addr"), 16), insn.group("op").strip()))

    if not instructions:
        raise ValueError(f"disassembly not found for {wanted_name}@{wanted_entry}")
    return instructions


def symbol_for_entry(entry: int, names_by_entry: dict[str, str]) -> str:
    normalized = normalize_entry(f"{entry:08x}")
    return names_by_entry.get(normalized, f"FUN_{normalized}")


def local_label(function_name: str, addr: int) -> str:
    safe_name = re.sub(r"[^A-Za-z0-9_]", "_", function_name)
    return f".L{safe_name}_{addr:08x}"


def literal_label(function_name: str, addr: int) -> str:
    safe_name = re.sub(r"[^A-Za-z0-9_]", "_", function_name)
    return f".L{safe_name}_lit_{addr:08x}"


def gas_op(
    function_name: str,
    addr: int,
    op: str,
    body_min: int,
    body_max: int,
    names_by_entry: dict[str, str],
    literal_addrs: set[int],
) -> str:
    op = op.strip().lower()
    op = re.sub(r"\s+", " ", op)
    op = re.sub(r"\s*,\s*", ",", op)
    op = re.sub(r"^cpy([a-z]{0,2})\b", r"mov\1", op)
    op = op.replace("vmrs apsr,fpscr", "vmrs apsr_nzcv,fpscr")

    branch = BRANCH_RE.match(op)
    if branch:
        target = int(branch.group("target"), 16)
        mnemonic = branch.group("mnemonic")
        if body_min <= target <= body_max:
            return f"{mnemonic} {local_label(function_name, target)}"
        return f"{mnemonic} {symbol_for_entry(target, names_by_entry)}"

    absolute = ABS_LITERAL_RE.match(op)
    if absolute:
        literal_addr = int(absolute.group("addr"), 16)
        literal_addrs.add(literal_addr)
        return f"{absolute.group('mnemonic')} {absolute.group('dst')}, {literal_label(function_name, literal_addr)}"

    pc_relative = PC_LITERAL_RE.match(op)
    if pc_relative:
        offset_text = pc_relative.group("offset")
        offset = int(offset_text, 16) if offset_text.startswith("0x") else int(offset_text)
        literal_addr = addr + 8 + offset
        literal_addrs.add(literal_addr)
        return f"{pc_relative.group('mnemonic')} {pc_relative.group('dst')}, {literal_label(function_name, literal_addr)}"

    return op


def quote_asm(line: str) -> str:
    escaped = line.replace("\\", "\\\\").replace('"', '\\"')
    return f'        "{escaped}\\n"'


def generate_source(function: dict[str, str], instructions: list[tuple[int, str]], names_by_entry: dict[str, str], signature: str | None) -> str:
    function_name = function["name"]
    body_min = int(normalize_entry(function["body_min"]), 16)
    body_max = int(normalize_entry(function["body_max"]), 16)

    branch_targets: set[int] = set()
    for _, op in instructions:
        match = BRANCH_RE.match(op.strip().lower())
        if match:
            target = int(match.group("target"), 16)
            if body_min <= target <= body_max:
                branch_targets.add(target)

    literal_addrs: set[int] = set()
    asm_lines: list[str] = [".syntax unified"]
    for addr, op in instructions:
        if addr in branch_targets:
            asm_lines.append(f"{local_label(function_name, addr)}:")
        asm_lines.append(gas_op(function_name, addr, op, body_min, body_max, names_by_entry, literal_addrs))

    if literal_addrs:
        asm_lines.append(".align 2")
        for literal_addr in sorted(literal_addrs):
            asm_lines.append(f"{literal_label(function_name, literal_addr)}:")
            asm_lines.append(".word 0")

    if signature is None:
        signature = f"void {function_name}(void)"

    lines = [
        '#include "oot3d/types.h"',
        "",
        "#if defined(__arm__)",
        "#define OOT3D_NAKED __attribute__((naked))",
        "#else",
        "#define OOT3D_NAKED",
        "#endif",
        "",
        f"// Ghidra: {function_name} @ {normalize_entry(function['entry'])}.",
        f"OOT3D_NAKED {signature} {{",
        "#if defined(__arm__)",
        "    __asm__ volatile(",
    ]
    lines.extend(quote_asm(line) for line in asm_lines)
    lines.extend(
        [
            "    );",
            "#endif",
            "}",
            "",
        ]
    )
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    selector = parser.add_mutually_exclusive_group(required=True)
    selector.add_argument("--name")
    selector.add_argument("--entry")
    parser.add_argument("--functions", type=Path, default=Path("ghidra_export/functions.csv"))
    parser.add_argument("--disassembly", type=Path, default=Path("ghidra_export/disassembly.txt"))
    parser.add_argument("--signature", help="C signature to use, without the trailing function body")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    functions = load_functions(args.functions)
    function = resolve_function(functions, args.name, args.entry)
    instructions = extract_disassembly(args.disassembly, function)
    source = generate_source(function, instructions, function_names_by_entry(functions), args.signature)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(source, encoding="utf-8")
    print(args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
