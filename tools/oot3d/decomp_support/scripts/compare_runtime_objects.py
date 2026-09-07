#!/usr/bin/env python3
"""Compare maintained object dumps against Ghidra target disassembly."""

from __future__ import annotations

import argparse
import csv
import json
import re
from pathlib import Path


TARGET_HEADER_RE = re.compile(r"^//\s+(?P<name>\S+)\s+@\s+(?P<addr>[0-9a-fA-F]+)\s*$")
TARGET_INSN_RE = re.compile(r"^[0-9a-fA-F]+:\s+(?P<op>.+?)\s*$")
OBJDUMP_HEADER_RE = re.compile(r"^[0-9a-fA-F]+\s+<(?P<name>[^>]+)>:\s*$")
OBJDUMP_INSN_RE = re.compile(r"^\s*[0-9a-fA-F]+:\s+(?:[0-9a-fA-F]{2,8}\s+)+\t(?P<op>.+?)\s*$")
COND_CODES = "eq|ne|cs|hs|cc|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le|al"
SHIFT_SET_ALIAS_RE = re.compile(
    rf"^(?P<shift>lsl|lsr|asr|ror)(?P<cond>{COND_CODES})?s\s+"
    r"(?P<dst>[^,]+),(?P<src>[^,]+),(?P<amount>.+)$"
)


def normalize_op(op: str) -> str:
    op = op.strip().lower()
    op = re.sub(r"\s+", " ", op)
    op = op.replace("\t", " ")
    op = re.sub(r"\s*,\s*", ",", op)
    op = re.sub(r"\s+\[", "[", op)
    op = re.sub(r"\]\s*,", "],", op)
    op = re.sub(r"\s+@", " @", op)
    op = re.sub(r"\s+@.*$", "", op)
    op = re.sub(r"\bsb\b", "r9", op)
    op = re.sub(r"\bsl\b", "r10", op)
    op = re.sub(r"\bfp\b", "r11", op)
    op = op.replace("ip", "r12")
    op = op.replace("apsr_nzcv", "apsr")
    op = re.sub(r"^nop(?:\s+\{0\})?$", "mov r0,r0", op)
    op = re.sub(r"\[(?:0x[0-9a-fA-F]+|pc,#-?(?:0x[0-9a-fA-F]+|\d+))\]", "[literal]", op)
    op = re.sub(r"^cpy([a-z]{0,2})\b", r"mov\1", op)
    op = re.sub(r"^vstr([a-z]{2})?\s+", r"vstr\1.32 ", op)
    op = re.sub(r"^vldr([a-z]{2})?\s+", r"vldr\1.32 ", op)
    op = re.sub(r"^vstr([a-z]{2})?\.32\s+(d\d+),", r"vstr\1.64 \2,", op)
    op = re.sub(r"^vldr([a-z]{2})?\.32\s+(d\d+),", r"vldr\1.64 \2,", op)
    op = normalize_doubleword_shorthand(op)
    op = re.sub(r"^str\s+(r\d+|r12),\[sp,#(?:-4|-0x4)\]!$", r"stmdb sp!,{\1}", op)
    op = re.sub(r"^ldr\s+(r\d+|r12),\[sp\],#(?:4|0x4)$", r"ldmia sp!,{\1}", op)
    op = re.sub(r"^push\s+(\{.*\})$", r"stmdb sp!,\1", op)
    op = re.sub(r"^pop([a-z]{0,2})\s+(\{.*\})$", r"ldmia\1 sp!,\2", op)
    op = SHIFT_SET_ALIAS_RE.sub(
        lambda match: (
            f"mov{match.group('cond') or ''}s "
            f"{match.group('dst')},{match.group('src')},{match.group('shift')} {match.group('amount')}"
        ),
        op,
    )
    op = re.sub(r"^lsl([a-z]{2})\s+([^,]+),([^,]+),(.+)$", r"mov\1 \2,\3,lsl \4", op)
    op = re.sub(r"^lsr([a-z]{2})\s+([^,]+),([^,]+),(.+)$", r"mov\1 \2,\3,lsr \4", op)
    op = re.sub(r"^asr([a-z]{2})\s+([^,]+),([^,]+),(.+)$", r"mov\1 \2,\3,asr \4", op)
    op = re.sub(r"^lsl\s+([^,]+),([^,]+),(.+)$", r"mov \1,\2,lsl \3", op)
    op = re.sub(r"^lsr\s+([^,]+),([^,]+),(.+)$", r"mov \1,\2,lsr \3", op)
    op = re.sub(r"^asr\s+([^,]+),([^,]+),(.+)$", r"mov \1,\2,asr \3", op)
    op = re.sub(r"^orrs(eq|ne|cc|cs|mi|pl|vs|vc|hi|ls|ge|lt|gt|le|al)\b", r"orr\1s", op)
    op = re.sub(r"^ldm\b", "ldmia", op)
    op = re.sub(r"^stm\b", "stmia", op)
    op = re.sub(r",#0x0\]", "]", op)
    op = re.sub(r",#0\]", "]", op)
    op = re.sub(r"#-0x([0-9a-fA-F]+)", normalize_negative_hex_immediate, op)
    op = re.sub(r"#0x([0-9a-fA-F]+)", normalize_hex_immediate, op)
    op = re.sub(r"^(b(?!x)[a-z]*)\s+.+$", r"\1 <target>", op)
    op = re.sub(r"\{([^{}]+)\}", lambda match: "{" + expand_register_ranges(match.group(1)) + "}", op)
    return op


def normalize_doubleword_shorthand(op: str) -> str:
    match = re.match(r"^(?P<mnemonic>(?:strd|ldrd)[a-z]*)\s+(?P<reg>r(?P<num>\d+)),(?P<addr>\[.+\])$", op)
    if not match:
        return op

    reg_num = int(match.group("num"))
    if reg_num >= 15:
        return op
    return f"{match.group('mnemonic')} {match.group('reg')},r{reg_num + 1},{match.group('addr')}"


def normalize_hex_immediate(match: re.Match[str]) -> str:
    value = int(match.group(1), 16)
    if value & 0x80000000:
        value -= 0x100000000
    return f"#{value}"


def normalize_negative_hex_immediate(match: re.Match[str]) -> str:
    return f"#-{int(match.group(1), 16)}"


def expand_register_ranges(registers: str) -> str:
    parts: list[str] = []
    for part in registers.split(","):
        part = part.strip()
        range_match = re.fullmatch(r"([rsd])(\d+)-\1(\d+)", part)
        if not range_match:
            parts.append(part)
            continue

        prefix = range_match.group(1)
        start = int(range_match.group(2))
        end = int(range_match.group(3))
        if start <= end:
            parts.extend(f"{prefix}{value}" for value in range(start, end + 1))
        else:
            parts.append(part)
    return ",".join(parts)


def read_target_functions(path: Path) -> dict[str, dict[str, object]]:
    functions: dict[str, dict[str, object]] = {}
    current_name: str | None = None

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        header = TARGET_HEADER_RE.match(raw_line)
        if header:
            current_name = header.group("name")
            functions[current_name] = {
                "entry": header.group("addr").lower(),
                "ops": [],
            }
            continue

        if current_name is None:
            continue

        if not raw_line.strip():
            current_name = None
            continue

        insn = TARGET_INSN_RE.match(raw_line)
        if insn:
            functions[current_name]["ops"].append(normalize_op(insn.group("op")))

    return functions


def read_manual_symbol_aliases(path: Path) -> dict[str, str]:
    if not path.is_file():
        return {}

    aliases: dict[str, str] = {}
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            if row.get("kind") == "function":
                aliases[row["entry"].lower()] = row["new_name"]
    return aliases


def apply_target_aliases(
    functions: dict[str, dict[str, object]],
    aliases_by_entry: dict[str, str],
) -> dict[str, dict[str, object]]:
    aliased = dict(functions)
    for function in functions.values():
        entry = str(function["entry"]).lower()
        alias = aliases_by_entry.get(entry)
        if alias and alias not in aliased:
            aliased[alias] = function
    return aliased


def read_objdump_functions(compiled_dir: Path) -> dict[str, dict[str, object]]:
    functions: dict[str, dict[str, object]] = {}

    for dump_path in sorted(compiled_dir.glob("*.dump")):
        current_name: str | None = None
        for raw_line in dump_path.read_text(encoding="utf-8", errors="replace").splitlines():
            header = OBJDUMP_HEADER_RE.match(raw_line)
            if header:
                current_name = header.group("name")
                functions[current_name] = {
                    "dump": str(dump_path),
                    "ops": [],
                }
                continue

            if current_name is None:
                continue

            insn = OBJDUMP_INSN_RE.match(raw_line)
            if insn:
                op = insn.group("op").strip()
                if not op.startswith(".word"):
                    functions[current_name]["ops"].append(normalize_op(op))

    return functions


def compare_ops(target_ops: list[str], compiled_ops: list[str]) -> dict[str, object]:
    prefix = 0
    for left, right in zip(target_ops, compiled_ops):
        if left != right:
            break
        prefix += 1

    suffix = 0
    max_suffix = min(len(target_ops), len(compiled_ops)) - prefix
    while suffix < max_suffix and target_ops[-1 - suffix] == compiled_ops[-1 - suffix]:
        suffix += 1

    first_difference = None
    if prefix < max(len(target_ops), len(compiled_ops)):
        first_difference = {
            "index": prefix,
            "target": target_ops[prefix] if prefix < len(target_ops) else None,
            "compiled": compiled_ops[prefix] if prefix < len(compiled_ops) else None,
        }

    lcs_count = lcs_length(target_ops, compiled_ops)
    longest_run = longest_common_run(target_ops, compiled_ops)

    return {
        "target_instruction_count": len(target_ops),
        "compiled_instruction_count": len(compiled_ops),
        "matching_prefix": prefix,
        "matching_suffix": suffix,
        "lcs_instruction_count": lcs_count,
        "lcs_target_ratio": round(lcs_count / len(target_ops), 4) if target_ops else 1.0,
        "longest_common_run": longest_run,
        "exact_match": target_ops == compiled_ops,
        "first_difference": first_difference,
    }


def lcs_length(left: list[str], right: list[str]) -> int:
    if len(right) > len(left):
        left, right = right, left

    previous = [0] * (len(right) + 1)
    for left_op in left:
        current = [0]
        northwest = 0
        for index, right_op in enumerate(right, start=1):
            north = previous[index]
            west = current[-1]
            if left_op == right_op:
                value = northwest + 1
            else:
                value = north if north >= west else west
            current.append(value)
            northwest = north
        previous = current
    return previous[-1]


def longest_common_run(left: list[str], right: list[str]) -> dict[str, object]:
    previous = [0] * (len(right) + 1)
    best_length = 0
    best_left_end = 0
    best_right_end = 0

    for left_index, left_op in enumerate(left, start=1):
        current = [0] * (len(right) + 1)
        for right_index, right_op in enumerate(right, start=1):
            if left_op != right_op:
                continue
            length = previous[right_index - 1] + 1
            current[right_index] = length
            if length > best_length:
                best_length = length
                best_left_end = left_index
                best_right_end = right_index
        previous = current

    return {
        "length": best_length,
        "target_index": best_left_end - best_length,
        "compiled_index": best_right_end - best_length,
    }


def write_markdown(path: Path, rows: list[dict[str, object]], title: str) -> None:
    lines = [
        f"# {title}",
        "",
        "This is an instruction-text comparison between maintained C object dumps and the Ghidra target export.",
        "It is a first-pass signal for reconstruction work, not a final matching verdict.",
        "",
        "| Function | Target insns | Compiled insns | Prefix | Suffix | LCS | Longest run | Exact | First difference |",
        "| --- | ---: | ---: | ---: | ---: | ---: | --- | --- | --- |",
    ]

    for row in rows:
        diff = row.get("first_difference")
        if isinstance(diff, dict):
            diff_text = f"{diff['index']}: `{diff['target']}` vs `{diff['compiled']}`"
        else:
            diff_text = ""
        run = row.get("longest_common_run")
        if isinstance(run, dict):
            run_text = f"{run['length']} @ {run['target_index']}/{run['compiled_index']}"
        else:
            run_text = ""
        lines.append(
            "| {name} | {target_instruction_count} | {compiled_instruction_count} | "
            "{matching_prefix} | {matching_suffix} | {lcs_instruction_count} | "
            "{run_text} | {exact_match} | {diff_text} |".format(
                diff_text=diff_text,
                run_text=run_text,
                **row,
            )
        )

    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target-disassembly", type=Path, default=Path("ghidra_export/disassembly.txt"))
    parser.add_argument("--compiled-dir", type=Path, default=Path("build/runtime"))
    parser.add_argument("--manual-symbols", type=Path, default=Path("symbols/manual_symbols.csv"))
    parser.add_argument("--out-json", type=Path, default=Path("build/runtime/compare_runtime_objects.json"))
    parser.add_argument("--out-md", type=Path, default=Path("build/runtime/compare_runtime_objects.md"))
    parser.add_argument("--title", default="Runtime Object Comparison")
    args = parser.parse_args()

    target_functions = apply_target_aliases(
        read_target_functions(args.target_disassembly),
        read_manual_symbol_aliases(args.manual_symbols),
    )
    compiled_functions = read_objdump_functions(args.compiled_dir)

    rows: list[dict[str, object]] = []
    for name in sorted(set(target_functions) & set(compiled_functions)):
        target = target_functions[name]
        compiled = compiled_functions[name]
        row = {
            "name": name,
            "target_entry": target["entry"],
            "compiled_dump": compiled["dump"],
            **compare_ops(target["ops"], compiled["ops"]),
        }
        rows.append(row)

    args.out_json.parent.mkdir(parents=True, exist_ok=True)
    args.out_json.write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
    write_markdown(args.out_md, rows, args.title)

    exact = sum(1 for row in rows if row["exact_match"])
    print(f"compared {len(rows)} functions; exact matches: {exact}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
