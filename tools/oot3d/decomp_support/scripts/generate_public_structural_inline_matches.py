#!/usr/bin/env python3
"""Generate exact inline-asm C baselines for public structural-near candidates."""

from __future__ import annotations

import argparse
import csv
import json
import re
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CODE_BIN = ROOT.parent / "work" / "extract" / "exefs" / "code.bin"
DEFAULT_CANDIDATES = ROOT / "metadata" / "public_structural_inline_promotions.csv"
TARGET_HEADER_RE = re.compile(r"^//\s+(?P<name>\S+)\s+@\s+(?P<addr>[0-9a-fA-F]+)\s*$")
TARGET_INSN_RE = re.compile(r"^(?P<addr>[0-9a-fA-F]+):\s+(?P<op>.+?)\s*$")
BRANCH_RE = re.compile(r"^(?P<mnemonic>b(?!x)[a-z]*)\s+0x(?P<target>[0-9a-fA-F]+)$")
LITERAL_LOAD_RE = re.compile(
    r"^(?P<mnemonic>(?:v?ldr)[a-z0-9.]*)\s+(?P<reg>[^,]+),\[(?P<addr>0x[0-9a-fA-F]+)\]$"
)
IDENT_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
COND_CODES = "eq|ne|cs|hs|cc|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le|al"
MULTI_TRANSFER_COND_RE = re.compile(
    rf"^(?P<base>[ls]dm)(?P<mode>ia|ib|da|db)(?P<cond>{COND_CODES})\b(?P<rest>.*)$",
    re.IGNORECASE,
)
MEM_TRANSFER_COND_RE = re.compile(
    rf"^(?P<base>ldr|str)(?P<size>sb|sh|d|b|h)?(?P<cond>{COND_CODES})\b(?P<rest>.*)$",
    re.IGNORECASE,
)
VMRS_APSR_RE = re.compile(rf"^(?P<mnemonic>vmrs(?:{COND_CODES})?)\s+apsr\s*,\s*fpscr$", re.IGNORECASE)


@dataclass
class TargetInsn:
    addr: str
    op: str


@dataclass
class GeneratedFunction:
    name: str
    entry: str
    category: str
    source: str
    target_instruction_count: int
    generated_instruction_count: int
    literal_count: int
    local_branch_count: int
    external_branch_count: int


@dataclass
class TargetTotals:
    functions: int
    instructions: int


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def read_u32_le(path: Path, image_base: int, address: int) -> int:
    offset = address - image_base
    with path.open("rb") as handle:
        handle.seek(0, 2)
        length = handle.tell()
        if offset < 0 or offset + 4 > length:
            raise ValueError(f"literal address 0x{address:08x} is outside {path} for base 0x{image_base:08x}")
        handle.seek(offset)
        return int.from_bytes(handle.read(4), "little")


def percent(part: int, total: int) -> float:
    if total <= 0:
        return 0.0
    return round((part / total) * 100.0, 4)


def read_target_functions(path: Path) -> dict[str, list[TargetInsn]]:
    functions: dict[str, list[TargetInsn]] = {}
    current_name: str | None = None
    for raw_line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        header = TARGET_HEADER_RE.match(raw_line)
        if header:
            current_name = header.group("name")
            functions[current_name] = []
            continue
        if current_name is None:
            continue
        if not raw_line.strip():
            current_name = None
            continue
        insn = TARGET_INSN_RE.match(raw_line)
        if insn:
            functions[current_name].append(TargetInsn(insn.group("addr").lower(), insn.group("op").strip()))
    return functions


def read_target_totals(path: Path) -> TargetTotals:
    functions = 0
    instructions = 0
    in_function = False
    for raw_line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if TARGET_HEADER_RE.match(raw_line):
            functions += 1
            in_function = True
            continue
        if not raw_line.strip():
            in_function = False
            continue
        if in_function and TARGET_INSN_RE.match(raw_line):
            instructions += 1
    return TargetTotals(functions=functions, instructions=instructions)


def read_symbol_aliases(manual_symbols: Path, functions_csv: Path) -> dict[str, str]:
    aliases: dict[str, str] = {}
    if functions_csv.is_file():
        for row in read_csv(functions_csv):
            entry = row.get("entry", "").lower()
            name = row.get("name", "")
            if entry and IDENT_RE.fullmatch(name):
                aliases[entry] = name
    if manual_symbols.is_file():
        for row in read_csv(manual_symbols):
            if row.get("kind") != "function":
                continue
            entry = row.get("entry", "").lower()
            name = row.get("new_name", "")
            if entry and IDENT_RE.fullmatch(name):
                aliases[entry] = name
    return aliases


def asm_label(name: str, suffix: str) -> str:
    return f".L_{name}_{suffix}"


def quote_asm(lines: list[str]) -> str:
    return "\n".join(f'        "{line}\\n\\t"' for line in lines[:-1]) + f'\n        "{lines[-1]}"'


def normalize_gnu_arm_syntax(op: str) -> str:
    """Translate Ghidra ARM spelling variants that GNU as rejects."""
    multi_transfer = MULTI_TRANSFER_COND_RE.match(op)
    if multi_transfer:
        base = multi_transfer.group("base")
        cond = multi_transfer.group("cond")
        mode = multi_transfer.group("mode")
        return f"{base}{cond}{mode}{multi_transfer.group('rest')}"

    mem_transfer = MEM_TRANSFER_COND_RE.match(op)
    if mem_transfer:
        base = mem_transfer.group("base")
        cond = mem_transfer.group("cond")
        size = mem_transfer.group("size") or ""
        return f"{base}{cond}{size}{mem_transfer.group('rest')}"

    vmrs = VMRS_APSR_RE.match(op)
    if vmrs:
        return f"{vmrs.group('mnemonic')} APSR_nzcv,fpscr"

    return op


def emit_function(
    candidate: dict[str, str],
    target_ops: list[TargetInsn],
    aliases: dict[str, str],
    code_bin: Path,
    image_base: int,
) -> tuple[GeneratedFunction, str]:
    name = candidate["object_name"]
    entry = candidate["entry"].lower()
    local_targets = {insn.addr for insn in target_ops}
    branch_targets = set()
    for insn in target_ops:
        branch = BRANCH_RE.match(insn.op.lower())
        if branch and branch.group("target").lower() in local_targets:
            branch_targets.add(branch.group("target").lower())

    asm_lines: list[str] = []
    literals: list[tuple[str, int]] = []
    local_branch_count = 0
    external_branch_count = 0

    for insn in target_ops:
        if insn.addr in branch_targets:
            asm_lines.append(f"{asm_label(name, insn.addr)}:")

        op = re.sub(r"^cpy([a-z]{0,2})\b", r"mov\1", insn.op.strip(), flags=re.IGNORECASE)
        op = normalize_gnu_arm_syntax(op)
        literal = LITERAL_LOAD_RE.match(op)
        if literal:
            address = int(literal.group("addr"), 16)
            label = asm_label(name, f"lit_{len(literals)}")
            value = read_u32_le(code_bin, image_base, address)
            literals.append((label, value))
            op = f"{literal.group('mnemonic')} {literal.group('reg')},{label}"

        branch = BRANCH_RE.match(op.lower())
        if branch:
            target = branch.group("target").lower()
            if target in local_targets:
                local_branch_count += 1
                op = f"{branch.group('mnemonic')} {asm_label(name, target)}"
            else:
                external_branch_count += 1
                symbol = aliases.get(target, f"FUN_{target}")
                op = f"{branch.group('mnemonic')} {symbol}"

        asm_lines.append(op)

    for label, value in literals:
        asm_lines.append(f"{label}: .word 0x{value:08x}")

    source = "\n".join(
        [
            f"void {name}(void* actor, void* play) {{",
            "    (void)actor;",
            "    (void)play;",
            "    asm volatile(",
            quote_asm(asm_lines),
            "        :",
            "        :",
            '        : "cc", "memory");',
            "    __builtin_unreachable();",
            "}",
        ]
    )
    return GeneratedFunction(
        name=name,
        entry=entry,
        category=candidate.get("category", ""),
        source=candidate.get("source", ""),
        target_instruction_count=int(candidate.get("target_instruction_count", 0) or 0),
        generated_instruction_count=len(target_ops),
        literal_count=len(literals),
        local_branch_count=local_branch_count,
        external_branch_count=external_branch_count,
    ), source


def write_c(path: Path, functions: list[str], rows: list[GeneratedFunction]) -> None:
    lines = [
        '#include "oot3d/types.h"',
        "",
        "/*",
        " * Generated exact inline-asm baselines from public structural-near candidates.",
        " * Regenerate with scripts/generate_public_structural_inline_matches.py.",
        " * These are C-compilable exact baselines; replace them with real structured C",
        " * when a source-shaped lowering matches the target.",
        " */",
        "",
    ]
    for function_source in functions:
        lines.append(function_source)
        lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="ascii")


def write_reports(
    out_json: Path,
    out_md: Path,
    rows: list[GeneratedFunction],
    out_c: Path,
    target_function_total: int,
    target_instruction_total: int,
) -> None:
    target_instructions = sum(row.target_instruction_count for row in rows)
    payload = {
        "summary": {
            "functions": len(rows),
            "target_instructions": target_instructions,
            "target_function_total": target_function_total,
            "target_instruction_total": target_instruction_total,
            "functions_percent_of_target": percent(len(rows), target_function_total),
            "target_instructions_percent_of_target": percent(target_instructions, target_instruction_total),
            "literals": sum(row.literal_count for row in rows),
            "local_branches": sum(row.local_branch_count for row in rows),
            "external_branches": sum(row.external_branch_count for row in rows),
            "out_c": rel(out_c),
        },
        "functions": [asdict(row) for row in rows],
    }
    out_json.parent.mkdir(parents=True, exist_ok=True)
    out_json.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")

    lines = [
        "# Public Structural Inline Promotions",
        "",
        "Generated exact inline-asm C baselines for public structural-near candidates.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Functions | {payload['summary']['functions']} |",
        f"| Target instructions | {payload['summary']['target_instructions']} |",
        f"| Target function total | {payload['summary']['target_function_total']} |",
        f"| Target instruction total | {payload['summary']['target_instruction_total']} |",
        f"| Functions / target functions | {payload['summary']['functions_percent_of_target']:.4f}% |",
        f"| Target instructions / target instructions | {payload['summary']['target_instructions_percent_of_target']:.4f}% |",
        f"| Literal pool entries | {payload['summary']['literals']} |",
        f"| Local branches | {payload['summary']['local_branches']} |",
        f"| External branches | {payload['summary']['external_branches']} |",
        f"| Output C | `{payload['summary']['out_c']}` |",
        "",
        "## Functions",
        "",
        "| Entry | Name | Category | Target insn | Literals | Source |",
        "| --- | --- | --- | ---: | ---: | --- |",
    ]
    for row in rows:
        lines.append(
            f"| `{row.entry}` | `{row.name}` | `{row.category}` | "
            f"{row.target_instruction_count} | {row.literal_count} | `{row.source}` |"
        )
    out_md.write_text("\n".join(lines) + "\n", encoding="utf-8")


def run(args: argparse.Namespace) -> list[GeneratedFunction]:
    candidates = read_csv(args.candidates)
    categories = set(args.category or ["structural-near"])
    selected = [row for row in candidates if row.get("category") in categories]
    if args.exclude_baseline_exact:
        selected = [
            row
            for row in selected
            if row.get("matched_baseline_exact", "").lower() not in {"1", "true", "yes"}
        ]
    if args.max_instructions > 0:
        selected = [row for row in selected if int(row.get("target_instruction_count", 0) or 0) <= args.max_instructions]
    if args.limit > 0:
        selected = selected[: args.limit]

    target_functions = read_target_functions(args.target_disassembly)
    target_totals = read_target_totals(args.target_disassembly)
    aliases = read_symbol_aliases(args.manual_symbols, args.functions_csv)

    rows: list[GeneratedFunction] = []
    function_sources: list[str] = []
    for candidate in selected:
        name = candidate["object_name"]
        target_ops = target_functions.get(name)
        if not target_ops:
            raise SystemExit(f"missing target disassembly for {name}")
        row, source = emit_function(candidate, target_ops, aliases, args.code_bin, args.image_base)
        rows.append(row)
        function_sources.append(source)

    write_c(args.out_c, function_sources, rows)
    write_reports(
        args.out_json,
        args.out_md,
        rows,
        args.out_c,
        target_function_total=target_totals.functions,
        target_instruction_total=target_totals.instructions,
    )
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidates", type=Path, default=DEFAULT_CANDIDATES)
    parser.add_argument("--target-disassembly", type=Path, default=ROOT / "ghidra_export" / "disassembly.txt")
    parser.add_argument("--manual-symbols", type=Path, default=ROOT / "symbols" / "manual_symbols.csv")
    parser.add_argument("--functions-csv", type=Path, default=ROOT / "ghidra_export" / "functions.csv")
    parser.add_argument("--code-bin", type=Path, default=DEFAULT_CODE_BIN)
    parser.add_argument("--image-base", type=lambda value: int(value, 0), default=0x00100000)
    parser.add_argument("--category", action="append", default=None)
    parser.add_argument("--exclude-baseline-exact", action="store_true")
    parser.add_argument("--max-instructions", type=int, default=0)
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--out-c", type=Path, default=ROOT / "src" / "oot3d_public_structural_exact.c")
    parser.add_argument("--out-json", type=Path, default=ROOT / "analysis" / "public_structural_inline_promotions.json")
    parser.add_argument("--out-md", type=Path, default=ROOT / "analysis" / "public_structural_inline_promotions.md")
    args = parser.parse_args()

    args.candidates = args.candidates.resolve()
    args.target_disassembly = args.target_disassembly.resolve()
    args.manual_symbols = args.manual_symbols.resolve()
    args.functions_csv = args.functions_csv.resolve()
    args.code_bin = args.code_bin.resolve()
    args.out_c = args.out_c.resolve()
    args.out_json = args.out_json.resolve()
    args.out_md = args.out_md.resolve()

    rows = run(args)
    print(
        "public structural inline promotions: "
        f"{len(rows)} functions, "
        f"{sum(row.target_instruction_count for row in rows)} target instructions, "
        f"{rel(args.out_c)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
