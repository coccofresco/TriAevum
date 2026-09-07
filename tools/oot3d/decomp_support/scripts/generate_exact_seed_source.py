#!/usr/bin/env python3
"""Generate naked exact-seed C functions from the target disassembly export."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

HEADER_RE = re.compile(r"^//\s+(?P<name>\S+)\s+@\s+(?P<addr>[0-9a-fA-F]+)\s*$")
INSN_RE = re.compile(r"^(?P<addr>[0-9a-fA-F]+):\s+(?P<op>.+?)\s*$")
LITERAL_LOAD_RE = re.compile(r"^(?P<mnemonic>ldr[a-z]*|vldr(?:\.\d+)?(?:[a-z]+)?)\s+(?P<dst>[^,]+),\[(?P<addr>0x[0-9a-fA-F]+|[0-9a-fA-F]+)\]$")
BRANCH_RE = re.compile(r"^(?P<mnemonic>b(?:l|x)?[a-z]*)\s+(?P<target>0x[0-9a-fA-F]+|[0-9a-fA-F]+)$")


def parse_function_arg(value: str) -> tuple[str, str]:
    if ":" not in value:
        raise argparse.ArgumentTypeError("function must be ENTRY:NAME")
    entry, name = value.split(":", 1)
    entry = normalize_addr(entry)
    if not name:
        raise argparse.ArgumentTypeError("function name must not be empty")
    return entry, name


def parse_symbol_arg(value: str) -> tuple[str, str]:
    if "=" not in value:
        raise argparse.ArgumentTypeError("symbol must be ADDR=NAME")
    addr, name = value.split("=", 1)
    addr = normalize_addr(addr)
    if not name:
        raise argparse.ArgumentTypeError("symbol name must not be empty")
    return addr, name


def normalize_addr(value: str) -> str:
    value = value.strip().lower()
    if value.startswith("0x"):
        value = value[2:]
    return f"{int(value, 16):08x}"


def symbol_for(addr: str, symbols: dict[str, str]) -> str:
    return symbols.get(addr, f"FUN_{addr}")


def read_target_functions(path: Path) -> dict[str, dict[str, object]]:
    functions: dict[str, dict[str, object]] = {}
    current: dict[str, object] | None = None

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        header = HEADER_RE.match(raw_line)
        if header:
            entry = normalize_addr(header.group("addr"))
            current = {
                "target_name": header.group("name"),
                "entry": entry,
                "insns": [],
            }
            functions[entry] = current
            continue

        if current is None:
            continue

        if not raw_line.strip():
            current = None
            continue

        insn = INSN_RE.match(raw_line)
        if insn:
            current["insns"].append((normalize_addr(insn.group("addr")), insn.group("op").strip()))

    return functions


def asm_label(prefix: str, addr: str) -> str:
    return f".L{prefix}_{addr}"


def asm_literal(prefix: str, addr: str, index: int) -> str:
    return f".L{prefix}_lit_{index}_{addr}"


def literal_targets(insns: list[tuple[str, str]]) -> dict[str, str]:
    targets: dict[str, str] = {}
    for _, op in insns:
        literal = LITERAL_LOAD_RE.match(op)
        if not literal:
            continue
        target_addr = normalize_addr(literal.group("addr"))
        if target_addr not in targets:
            targets[target_addr] = ""
    return targets


def rewrite_op(
    op: str,
    local_addrs: set[str],
    prefix: str,
    symbols: dict[str, str],
    literal_labels: dict[str, str],
) -> str:
    if op == "nop":
        return "mov r0,r0"
    if op.startswith("vmrs"):
        return re.sub(r"\bapsr\b", "APSR_nzcv", op, count=1)

    op = op.replace("cpy", "mov", 1) if op.startswith("cpy") else op

    literal = LITERAL_LOAD_RE.match(op)
    if literal:
        target_addr = normalize_addr(literal.group("addr"))
        label = literal_labels[target_addr]
        return f"{literal.group('mnemonic')} {literal.group('dst')}, {label}"

    branch = BRANCH_RE.match(op)
    if branch:
        target_addr = normalize_addr(branch.group("target"))
        if target_addr in local_addrs:
            target = asm_label(prefix, target_addr)
        else:
            target = symbol_for(target_addr, symbols)
        return f"{branch.group('mnemonic')} {target}"

    return op


def emit_function(
    entry: str,
    function_name: str,
    target_function: dict[str, object],
    symbols: dict[str, str],
) -> list[str]:
    insns = list(target_function["insns"])
    local_addrs = {addr for addr, _ in insns}
    prefix = function_name
    literals = literal_targets(insns)
    for index, target_addr in enumerate(sorted(literals)):
        literals[target_addr] = asm_literal(prefix, target_addr, index)
    pending_literals = sorted(literals)
    asm_lines: list[str] = [".syntax unified"]
    previous_addr: str | None = None

    def flush_literals_before(addr: str) -> None:
        nonlocal pending_literals
        ready = [literal_addr for literal_addr in pending_literals if literal_addr < addr]
        if not ready:
            return
        asm_lines.append(".align 2")
        for literal_addr in ready:
            asm_lines.append(f"{literals[literal_addr]}:")
            asm_lines.append(".word 0x00000000")
        pending_literals = [literal_addr for literal_addr in pending_literals if literal_addr not in set(ready)]

    for addr, op in insns:
        if previous_addr is not None:
            flush_literals_before(addr)
        asm_lines.append(f"{asm_label(prefix, addr)}:")
        asm_lines.append(rewrite_op(op, local_addrs, prefix, symbols, literals))
        previous_addr = addr

    if pending_literals:
        asm_lines.append(".align 2")
        for target_addr in pending_literals:
            label = literals[target_addr]
            asm_lines.append(f"{label}:")
            asm_lines.append(".word 0x00000000")

    lines = [
        f"// Ghidra: {target_function['target_name']} @ {entry}.",
        f"OOT3D_NAKED void {function_name}(void) {{",
        "#if defined(__arm__)",
        "    __asm__ volatile(",
    ]
    for line in asm_lines:
        lines.append(f'        "{line}\\n"')
    lines.extend(
        [
            "    );",
            "#else",
            f"    (void){function_name};",
            "#endif",
            "}",
            "",
        ]
    )
    return lines


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target-disassembly", type=Path, default=ROOT / "ghidra_export" / "disassembly.txt")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--function", action="append", type=parse_function_arg, required=True)
    parser.add_argument("--symbol", action="append", type=parse_symbol_arg, default=[])
    parser.add_argument("--include", action="append", default=[])
    parser.add_argument("--guard", default="")
    parser.add_argument("--banner", default="Generated exact matching seeds. Do not edit by hand.")
    args = parser.parse_args()

    target_functions = read_target_functions(args.target_disassembly)
    symbols = dict(args.symbol)

    lines = [
        "/*",
        f" * {args.banner}",
        " * Source: ghidra_export/disassembly.txt",
        " */",
        "",
    ]
    if args.guard:
        lines.append(f"#ifndef {args.guard}")
        lines.append(f"#define {args.guard}")
        lines.append("")
    for include in args.include:
        lines.append(f'#include "{include}"')
    if args.include:
        lines.append("")
    lines.extend(
        [
            "#if defined(__arm__)",
            "#define OOT3D_NAKED __attribute__((naked))",
            "#else",
            "#define OOT3D_NAKED",
            "#endif",
            "",
        ]
    )

    for entry, function_name in args.function:
        target_function = target_functions.get(entry)
        if target_function is None:
            raise SystemExit(f"target function not found: {entry}")
        lines.extend(emit_function(entry, function_name, target_function, symbols))

    if args.guard:
        lines.append(f"#endif // {args.guard}")

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
