#!/usr/bin/env python3
"""Compare state-gate lowering shapes across direct structured variants."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
from typing import Any

from compare_direct_packet_probe_matches import read_objdump_file
from compare_runtime_objects import compare_ops, read_target_functions
from probe_direct_packet_compilability import rel


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PROBE = ROOT / "analysis" / "direct_structured_lowering_probe.json"
DEFAULT_TARGET_DISASM = ROOT / "ghidra_export" / "disassembly.txt"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_structured_state_gate_variants.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_structured_state_gate_variants.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_structured_state_gate_variants.md"

TARGET_STATE_GATE_SPAN = (4, 12)


def read_json(path: Path, default: Any) -> Any:
    if not path.is_file():
        return default
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")


def write_csv(path: Path, rows: list[dict[str, Any]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fieldnames})


def bool_value(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    return str(value).strip().lower() in {"1", "true", "yes"}


def float_value(value: Any) -> float:
    try:
        return float(value or 0.0)
    except (TypeError, ValueError):
        return 0.0


def int_value(value: Any) -> int:
    try:
        return int(value or 0)
    except (TypeError, ValueError):
        return 0


def state_gate_end(ops: list[str]) -> int:
    for index in range(4, min(len(ops), 28)):
        op = ops[index]
        if op == "mov r1,#13":
            return index
        if op.startswith("mov r5,#0") or op.startswith("mov r3,#0"):
            return index
        if op.startswith("add r0,r4,#420") and index > 6:
            return index
    return min(len(ops), 16)


def branch_shape(ops: list[str]) -> str:
    text = " ".join(ops)
    if "cmpne" in text:
        return "cmpne-chain"
    if "sub " in text and ("bls <target>" in text or "bhi <target>" in text):
        return "subtract-range-check"
    if "ble <target>" in text or "bgt <target>" in text:
        return "range-check"
    eq_branches = sum(1 for op in ops if op.startswith("beq "))
    if eq_branches >= 2:
        return "explicit-eq-chain"
    if any(op.startswith("beq ") for op in ops):
        return "single-eq-branch"
    return "other"


def has_target_cmp_values(ops: list[str]) -> str:
    values = []
    for value in ("#10", "#11", "#12", "#13"):
        if any(value in op for op in ops):
            values.append(value[1:])
    return ";".join(values)


def first_difference(compare: dict[str, Any]) -> str:
    diff = compare.get("first_difference")
    if not isinstance(diff, dict):
        return ""
    return f"{diff.get('index')}: {diff.get('target')} != {diff.get('compiled')}"


def row_for_variant(row: dict[str, Any], target_gate_ops: list[str]) -> dict[str, Any] | None:
    if not bool_value(row.get("dumped")):
        return None
    dump = ROOT / str(row.get("dump", ""))
    if not dump.is_file():
        return None
    compiled_ops = list(read_objdump_file(dump).get(str(row.get("symbol", "")), {}).get("ops", []))
    if not compiled_ops:
        return None
    end = state_gate_end(compiled_ops)
    compiled_gate_ops = compiled_ops[4:end]
    compare = compare_ops(target_gate_ops, compiled_gate_ops)
    compiled_shape = branch_shape(compiled_gate_ops)
    target_shape = branch_shape(target_gate_ops)
    return {
        "entry": row.get("entry", ""),
        "oot3d_name": row.get("oot3d_name", ""),
        "variant": row.get("variant", ""),
        "profile": row.get("profile", ""),
        "global_category": row.get("category", ""),
        "global_lcs_target_ratio": row.get("lcs_target_ratio", 0),
        "target_state_gate_instruction_count": len(target_gate_ops),
        "compiled_state_gate_instruction_count": len(compiled_gate_ops),
        "state_gate_lcs_instruction_count": compare.get("lcs_instruction_count", 0),
        "state_gate_lcs_target_ratio": compare.get("lcs_target_ratio", 0),
        "target_branch_shape": target_shape,
        "compiled_branch_shape": compiled_shape,
        "branch_shape_matches": target_shape == compiled_shape,
        "compiled_cmp_values": has_target_cmp_values(compiled_gate_ops),
        "first_difference": first_difference(compare),
        "cmpne_chain_recovered": compiled_shape == "cmpne-chain",
        "range_check_avoided": compiled_shape not in {"range-check", "subtract-range-check"},
        "next_gate": next_gate(compiled_shape),
        "dump": row.get("dump", ""),
    }


def next_gate(compiled_shape: str) -> str:
    if compiled_shape == "cmpne-chain":
        return "Use this state gate as the next structured lowering seed."
    if compiled_shape == "explicit-eq-chain":
        return "State range fold is avoided; next gate is conditional-cmp codegen rather than C control-flow layout."
    if compiled_shape in {"range-check", "subtract-range-check"}:
        return "Range fold remains; keep this variant as negative evidence."
    return "Classify branch shape before using this variant."


def build_report(args: argparse.Namespace) -> dict[str, Any]:
    probe = read_json(args.probe, {})
    target = read_target_functions(args.target_disasm).get("oot3d_boss_va_zapper_intro", {})
    target_ops = list(target.get("ops", []))
    target_gate_ops = target_ops[TARGET_STATE_GATE_SPAN[0] : TARGET_STATE_GATE_SPAN[1]]
    rows = [
        result
        for probe_row in probe.get("rows", [])
        if isinstance(probe_row, dict)
        for result in [row_for_variant(probe_row, target_gate_ops)]
        if result is not None
    ]
    rows.sort(
        key=lambda row: (
            bool_value(row.get("cmpne_chain_recovered")),
            bool_value(row.get("range_check_avoided")),
            float_value(row.get("state_gate_lcs_target_ratio")),
            float_value(row.get("global_lcs_target_ratio")),
        ),
        reverse=True,
    )
    best = rows[0] if rows else {}
    summary = {
        "rows": len(rows),
        "range_check_avoided_rows": sum(1 for row in rows if row.get("range_check_avoided")),
        "cmpne_chain_recovered_rows": sum(1 for row in rows if row.get("cmpne_chain_recovered")),
        "best_variant": best.get("variant", ""),
        "best_profile": best.get("profile", ""),
        "best_compiled_branch_shape": best.get("compiled_branch_shape", ""),
        "best_state_gate_lcs_target_ratio": best.get("state_gate_lcs_target_ratio", 0),
        "next_gate": best.get("next_gate", "No state-gate variants compared."),
    }
    return {
        "format": "oot3d_direct_structured_state_gate_variants_v1",
        "inputs": {"probe": rel(args.probe), "target_disasm": rel(args.target_disasm)},
        "summary": summary,
        "rows": rows,
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct Structured State Gate Variants",
        "",
        "This report compares only the `00398484` state-gate lowering shape across structured variants.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Rows | {summary['rows']} |",
        f"| Range-check avoided rows | {summary['range_check_avoided_rows']} |",
        f"| cmpne-chain recovered rows | {summary['cmpne_chain_recovered_rows']} |",
        f"| Best variant | `{summary['best_variant']}` |",
        f"| Best profile | `{summary['best_profile']}` |",
        f"| Best branch shape | `{summary['best_compiled_branch_shape']}` |",
        f"| Best state-gate LCS | {float_value(summary['best_state_gate_lcs_target_ratio']):.4f} |",
        "",
        "## Next Gate",
        "",
        summary["next_gate"],
        "",
        "## Rows",
        "",
        "| Variant | Profile | Branch shape | Avoids range | cmpne | State LCS | Global LCS | Next gate |",
        "| --- | --- | --- | --- | --- | ---: | ---: | --- |",
    ]
    for row in data["rows"]:
        lines.append(
            f"| `{row['variant']}` | `{row['profile']}` | `{row['compiled_branch_shape']}` | "
            f"{row['range_check_avoided']} | {row['cmpne_chain_recovered']} | "
            f"{float_value(row['state_gate_lcs_target_ratio']):.4f} | "
            f"{float_value(row['global_lcs_target_ratio']):.4f} | {row['next_gate']} |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, default=DEFAULT_PROBE)
    parser.add_argument("--target-disasm", type=Path, default=DEFAULT_TARGET_DISASM)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    args = parser.parse_args()
    data = build_report(args)
    fields = [
        "entry",
        "oot3d_name",
        "variant",
        "profile",
        "global_category",
        "global_lcs_target_ratio",
        "target_state_gate_instruction_count",
        "compiled_state_gate_instruction_count",
        "state_gate_lcs_instruction_count",
        "state_gate_lcs_target_ratio",
        "target_branch_shape",
        "compiled_branch_shape",
        "branch_shape_matches",
        "compiled_cmp_values",
        "first_difference",
        "cmpne_chain_recovered",
        "range_check_avoided",
        "next_gate",
        "dump",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"], fields)
    write_markdown(args.out_md, data)
    summary = data["summary"]
    print(
        "direct structured state gate variants: "
        f"{summary['range_check_avoided_rows']}/{summary['rows']} avoid range-check, "
        f"{summary['cmpne_chain_recovered_rows']} recover cmpne-chain"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
