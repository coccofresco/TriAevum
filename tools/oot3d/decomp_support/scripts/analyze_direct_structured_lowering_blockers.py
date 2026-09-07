#!/usr/bin/env python3
"""Diagnose structured-C lowering blockers for current frontier rows."""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import Counter
from pathlib import Path
from typing import Any

from compare_direct_packet_probe_matches import read_objdump_file
from compare_runtime_objects import compare_ops, normalize_op
from probe_direct_source_subregions import rel
from sweep_direct_source_subregion_ranges import float_value, int_value


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_FRONTIER = ROOT / "analysis" / "c_reconstruction_frontier.csv"
DEFAULT_TARGET_DISASM = ROOT / "ghidra_export" / "disassembly.txt"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_structured_lowering_blockers.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_structured_lowering_blockers.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_structured_lowering_blockers.md"
TARGET_HEADER_RE = re.compile(r"^//\s+(?P<name>\S+)\s+@\s+(?P<addr>[0-9a-fA-F]+)\s*$")
TARGET_INSN_RE = re.compile(r"^(?P<addr>[0-9a-fA-F]{8}):\s+(?P<op>.+?)\s*$")
CALL_RE = re.compile(r"^(?P<mnemonic>bl|blx|b)\s+")
OFFSET_RE = re.compile(r"\[(?P<reg>r\d+|sp|r10|r11|r12),#(?P<offset>-?\d+)\]")


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


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


def repo_path(value: Any) -> Path:
    path = Path(str(value or ""))
    return path if path.is_absolute() else ROOT / path


def read_target_functions(path: Path) -> dict[str, dict[str, Any]]:
    functions: dict[str, dict[str, Any]] = {}
    current: dict[str, Any] | None = None
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        header = TARGET_HEADER_RE.match(raw)
        if header:
            current = {
                "name": header.group("name"),
                "entry": header.group("addr").lower(),
                "ops": [],
                "rows": [],
            }
            functions[current["entry"]] = current
            continue
        if current is None:
            continue
        if not raw.strip():
            current = None
            continue
        match = TARGET_INSN_RE.match(raw)
        if not match:
            continue
        op = match.group("op").strip()
        if op.startswith(".word"):
            continue
        normalized = normalize_op(op)
        current["ops"].append(normalized)
        current["rows"].append({"addr": match.group("addr").lower(), "op": normalized, "raw_op": op})
    return functions


def source_text_from_frontier(row: dict[str, str]) -> str:
    materialized = repo_path(row.get("materialized_output", ""))
    if materialized.is_file():
        return materialized.read_text(encoding="utf-8", errors="replace")
    source = str(row.get("best_split_source", ""))
    if ":" in source:
        path_part, _, range_part = source.rpartition(":")
        path = repo_path(path_part)
        if path.is_file():
            lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
            match = re.match(r"(?P<start>\d+)-(?P<end>\d+)", range_part)
            if match:
                start = int(match.group("start"))
                end = int(match.group("end"))
                return "\n".join(lines[start - 1 : end])
    return ""


def compiled_ops_for(row: dict[str, str]) -> list[str]:
    dump_path = repo_path(row.get("compiled_dump", ""))
    if not dump_path.is_file():
        probe_source = repo_path(row.get("probe_source", ""))
        if probe_source.name.endswith(".probe.c"):
            dump_path = probe_source.parent.parent / "dumps" / probe_source.name.replace(".probe.c", ".dump")
    if not dump_path.is_file():
        return []
    symbol = read_objdump_file(dump_path).get(str(row.get("best_symbol", "")), {})
    return list(symbol.get("ops", [])) if isinstance(symbol, dict) else []


def count_calls(ops: list[str]) -> int:
    return sum(1 for op in ops if op.startswith("bl ") or op.startswith("blx "))


def count_branches(ops: list[str]) -> int:
    return sum(1 for op in ops if CALL_RE.match(op))


def offset_set(ops: list[str]) -> set[str]:
    result: set[str] = set()
    for op in ops:
        for match in OFFSET_RE.finditer(op):
            result.add(f"{match.group('reg')}#{match.group('offset')}")
    return result


def detect_features(source: str, target_ops: list[str], compiled_ops: list[str]) -> tuple[list[str], list[str]]:
    features: list[str] = []
    gates: list[str] = []
    if "sCsState" in source and "switch" in source:
        features.append("static-state-switch-source")
        has_runtime_state_load = any("ldrsb" in op for op in target_ops) and any(
            op.startswith("cmp") for op in target_ops[:14]
        )
        compiled_has_runtime_switch = any(op.startswith("cmp") for op in compiled_ops[:24]) and any(
            op.startswith("b") for op in compiled_ops[:32]
        )
        if has_runtime_state_load:
            features.append("target-runtime-state-load")
            if compiled_has_runtime_switch:
                features.append("compiled-runtime-state-switch")
            else:
                gates.append("model-static-state-as-runtime-global")
        if not compiled_has_runtime_switch:
            features.append("compiled-switch-optimized-away")
    if any(op.startswith("v") for op in target_ops) and not any(op.startswith("v") for op in compiled_ops):
        features.append("target-vfp-envelope")
        gates.append("lower-math-helper-or-float-path")
    target_offsets = offset_set(target_ops)
    compiled_offsets = offset_set(compiled_ops)
    if target_offsets and compiled_offsets and len(target_offsets & compiled_offsets) <= 2:
        features.append("struct-offset-layout-delta")
        gates.append("apply-direct-struct-offset-lowering")
    if count_calls(target_ops) != count_calls(compiled_ops):
        features.append("call-count-delta")
        gates.append("resolve-helper-call-shape")
    if len(target_ops) > len(compiled_ops) * 1.4:
        features.append("target-envelope-wider-than-source")
        gates.append("split-wrapper-envelope-from-helper-body")
    return sorted(set(features)), sorted(set(gates))


def build_report(args: argparse.Namespace) -> dict[str, Any]:
    target_functions = read_target_functions(args.target_disasm)
    rows: list[dict[str, Any]] = []
    for row in read_csv(args.frontier):
        if row.get("frontier_status") != "structured-c-needs-lowering":
            continue
        entry = str(row.get("entry", "")).lower()
        target = target_functions.get(entry, {})
        target_ops = list(target.get("ops", []))
        compiled_ops = compiled_ops_for(row)
        source = source_text_from_frontier(row)
        compare = compare_ops(target_ops, compiled_ops) if target_ops and compiled_ops else {}
        features, gates = detect_features(source, target_ops, compiled_ops)
        target_call_count = count_calls(target_ops)
        compiled_call_count = count_calls(compiled_ops)
        blocker_class = classify_blocker(features, gates)
        rows.append(
            {
                "entry": entry,
                "oot3d_name": row.get("oot3d_name", ""),
                "n64_name": row.get("n64_name", ""),
                "target_instruction_count": len(target_ops),
                "compiled_instruction_count": len(compiled_ops),
                "target_call_count": target_call_count,
                "compiled_call_count": compiled_call_count,
                "target_branch_count": count_branches(target_ops),
                "compiled_branch_count": count_branches(compiled_ops),
                "lcs_instruction_count": int_value(compare.get("lcs_instruction_count")),
                "lcs_target_ratio": float_value(compare.get("lcs_target_ratio")),
                "matching_prefix": int_value(compare.get("matching_prefix")),
                "matching_suffix": int_value(compare.get("matching_suffix")),
                "first_difference": first_difference_text(compare),
                "features": ";".join(features),
                "required_gates": ";".join(gates),
                "blocker_class": blocker_class,
                "promotion_ready": False,
                "next_gate": next_gate_for(blocker_class, gates),
                "materialized_output": row.get("materialized_output", ""),
                "compiled_dump": row.get("compiled_dump", ""),
            }
        )
    rows.sort(
        key=lambda item: (
            blocker_rank(str(item.get("blocker_class", ""))),
            int_value(item.get("target_instruction_count")),
            str(item.get("entry", "")),
        )
    )
    blockers = Counter(str(row.get("blocker_class", "")) for row in rows)
    gates = Counter(gate for row in rows for gate in str(row.get("required_gates", "")).split(";") if gate)
    best = rows[0] if rows else {}
    summary = {
        "structured_rows": len(rows),
        "promotion_ready": sum(1 for row in rows if row.get("promotion_ready")),
        "blocker_counts": dict(sorted(blockers.items())),
        "required_gate_counts": dict(sorted(gates.items())),
        "next_entry": best.get("entry", ""),
        "next_name": best.get("oot3d_name", ""),
        "next_blocker": best.get("blocker_class", ""),
        "next_gate": best.get("next_gate", ""),
    }
    return {
        "format": "oot3d_direct_structured_lowering_blockers_v1",
        "inputs": {
            "frontier": rel(args.frontier),
            "target_disasm": rel(args.target_disasm),
        },
        "summary": summary,
        "rows": rows,
    }


def first_difference_text(compare: dict[str, Any]) -> str:
    diff = compare.get("first_difference")
    if not isinstance(diff, dict):
        return ""
    return f"{diff.get('index')}: {diff.get('target')} vs {diff.get('compiled')}"


def classify_blocker(features: list[str], gates: list[str]) -> str:
    if "model-static-state-as-runtime-global" in gates:
        return "runtime-state-switch-lowered-as-constant"
    if "apply-direct-struct-offset-lowering" in gates:
        return "struct-offset-layout-delta"
    if "split-wrapper-envelope-from-helper-body" in gates:
        return "wrapper-envelope-delta"
    if "resolve-helper-call-shape" in gates:
        return "helper-call-shape-delta"
    return "structured-lowering-needed"


def blocker_rank(value: str) -> int:
    return {
        "runtime-state-switch-lowered-as-constant": 0,
        "struct-offset-layout-delta": 1,
        "wrapper-envelope-delta": 2,
        "helper-call-shape-delta": 3,
        "structured-lowering-needed": 4,
    }.get(value, 99)


def next_gate_for(blocker: str, gates: list[str]) -> str:
    if blocker == "runtime-state-switch-lowered-as-constant":
        return "Make state globals such as sCsState compile as runtime-loaded symbols, then rerun the structured probe."
    if blocker == "struct-offset-layout-delta":
        return "Apply direct OOT3D struct offsets for BossVa fields before comparing the maintained C body."
    if blocker == "wrapper-envelope-delta":
        return "Split the OOT3D wrapper/envelope from the helper body before promotion."
    if blocker == "helper-call-shape-delta":
        return "Resolve helper-call identity/inlining differences before source promotion."
    if gates:
        return f"Apply lowering gates: {', '.join(gates)}."
    return "Inspect structured C lowering manually and rerun the probe."


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct Structured Lowering Blockers",
        "",
        "This report diagnoses why maintained/plain C frontier rows still compare as semantic windows.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Structured rows | {summary['structured_rows']} |",
        f"| Promotion-ready | {summary['promotion_ready']} |",
        "",
        "## Next Gate",
        "",
        f"- Entry: `{summary['next_entry']}` `{summary['next_name']}`",
        f"- Blocker: `{summary['next_blocker']}`",
        f"- Gate: {summary['next_gate']}",
        "",
        "## Rows",
        "",
        "| Entry | Name | Blocker | Target/compiled | Calls | LCS | Features | Next gate |",
        "| --- | --- | --- | ---: | ---: | ---: | --- | --- |",
    ]
    for row in data["rows"]:
        lines.append(
            f"| `{row['entry']}` | `{row['oot3d_name']}` | `{row['blocker_class']}` | "
            f"{row['target_instruction_count']}/{row['compiled_instruction_count']} | "
            f"{row['target_call_count']}/{row['compiled_call_count']} | "
            f"{float_value(row['lcs_target_ratio']):.4f} | `{row['features']}` | {row['next_gate']} |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frontier", type=Path, default=DEFAULT_FRONTIER)
    parser.add_argument("--target-disasm", type=Path, default=DEFAULT_TARGET_DISASM)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    args = parser.parse_args()
    data = build_report(args)
    fields = [
        "entry",
        "oot3d_name",
        "n64_name",
        "target_instruction_count",
        "compiled_instruction_count",
        "target_call_count",
        "compiled_call_count",
        "target_branch_count",
        "compiled_branch_count",
        "lcs_instruction_count",
        "lcs_target_ratio",
        "matching_prefix",
        "matching_suffix",
        "first_difference",
        "features",
        "required_gates",
        "blocker_class",
        "promotion_ready",
        "next_gate",
        "materialized_output",
        "compiled_dump",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"], fields)
    write_markdown(args.out_md, data)
    summary = data["summary"]
    print(
        "direct structured lowering blockers: "
        f"{summary['structured_rows']} rows, next {summary['next_entry']} {summary['next_blocker']}"
    )
    print(str(args.out_md.relative_to(ROOT)).replace("\\", "/"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
