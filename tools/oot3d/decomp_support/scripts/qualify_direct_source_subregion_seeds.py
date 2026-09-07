#!/usr/bin/env python3
"""Qualify near/exact direct source subregion sweep seeds for promotion risk."""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import Counter
from pathlib import Path
from typing import Any

from compare_direct_packet_probe_matches import read_objdump_file
from probe_direct_source_subregions import rel
from refine_direct_data_like_boundaries import read_indexed_ops
from sweep_direct_source_subregion_ranges import float_value, int_value, list_value, repo_path, semantic_index


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SWEEP = ROOT / "analysis" / "direct_source_subregion_sweep.json"
DEFAULT_SEMANTIC = ROOT / "analysis" / "direct_semantic_call_shape.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_source_subregion_seed_qualification.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_source_subregion_seed_qualification.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_source_subregion_seed_qualification.md"
NEAR_CATEGORIES = {"exact-window", "codegen-near-window", "structural-near-window"}
GENERIC_MOV_RE = re.compile(r"^mov r\d+,r\d+$")
GENERIC_IMM_RE = re.compile(r"^mov r\d+,#-?\d+$")


def read_json(path: Path, default: Any) -> Any:
    if not path.is_file():
        return default
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    return value if isinstance(value, dict) else default


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


def longest_run_length(value: Any) -> int:
    match = re.match(r"^\s*(\d+)@", str(value or ""))
    return int(match.group(1)) if match else 0


def is_branch_or_call(op: str) -> bool:
    mnemonic = op.split(" ", 1)[0]
    return mnemonic in {"b", "beq", "bne", "bcs", "bcc", "bmi", "bpl", "bvs", "bvc", "bhi", "bls", "bge", "blt", "bgt", "ble", "bl", "blx"}


def is_generic_op(op: str) -> bool:
    op = op.strip()
    if not op:
        return True
    if op in {"nop", "mov r0,r0"}:
        return True
    if op in {"b <target>", "bl <target>", "blx <target>"}:
        return True
    if GENERIC_MOV_RE.match(op) or GENERIC_IMM_RE.match(op):
        return True
    return False


def opcode_counts(ops: list[str]) -> Counter[str]:
    return Counter(op.split(" ", 1)[0] if op else "" for op in ops)


def compiled_ops_for(row: dict[str, Any]) -> list[str]:
    dump_path = repo_path(row.get("dump", ""))
    if not dump_path.is_file():
        return []
    symbol = read_objdump_file(dump_path).get(str(row.get("function_name", "")), {})
    return list(symbol.get("ops", [])) if isinstance(symbol, dict) else []


def target_rows_for(row: dict[str, Any], semantic_rows: dict[tuple[str, str], dict[str, Any]]) -> list[dict[str, Any]]:
    key = (str(row.get("entry", "")).lower(), str(row.get("candidate_symbol", "")))
    semantic_row = semantic_rows.get(key, {})
    source_excerpt = repo_path(semantic_row.get("source_excerpt", ""))
    manifest = read_json(source_excerpt.with_name("probe_manifest.json"), {})
    return read_indexed_ops(repo_path(manifest.get("target_window_ops", "")))


def classify_seed(row: dict[str, Any], target_ops: list[str], compiled_ops: list[str]) -> tuple[str, str, dict[str, Any]]:
    compare_count = int_value(row.get("compare_instruction_count"))
    lcs_ratio = float_value(row.get("lcs_window_ratio"))
    run_length = longest_run_length(row.get("longest_common_run"))
    matching_prefix = int_value(row.get("matching_prefix"))
    matching_suffix = int_value(row.get("matching_suffix"))
    category = str(row.get("category", ""))
    source_line_count = int_value(row.get("source_line_count"))
    target_generic = sum(1 for op in target_ops if is_generic_op(op))
    compiled_generic = sum(1 for op in compiled_ops if is_generic_op(op))
    generic_ratio = (target_generic + compiled_generic) / max(1, len(target_ops) + len(compiled_ops))
    branch_call_ratio = (
        sum(1 for op in target_ops + compiled_ops if is_branch_or_call(op)) / max(1, len(target_ops) + len(compiled_ops))
    )
    target_counts = opcode_counts(target_ops)
    compiled_counts = opcode_counts(compiled_ops)
    shared_specific_opcodes = sorted(
        op
        for op in set(target_counts) & set(compiled_counts)
        if op and op not in {"b", "bl", "mov", "nop"}
    )

    metrics = {
        "longest_common_run_length": run_length,
        "target_generic_ops": target_generic,
        "compiled_generic_ops": compiled_generic,
        "generic_op_ratio": round(generic_ratio, 4),
        "branch_call_ratio": round(branch_call_ratio, 4),
        "shared_specific_opcodes": ",".join(shared_specific_opcodes),
    }

    if category not in NEAR_CATEGORIES:
        return "not-near-window", "Sweep row is outside the near/exact categories.", metrics
    if compare_count <= 8 and run_length <= 1:
        return "short-generic-near-risk", "Near score is based on a very short slice with no sustained common run.", metrics
    if run_length >= 4 and compare_count >= 12 and generic_ratio <= 0.55 and len(shared_specific_opcodes) >= 2:
        return "candidate-control-flow-seed", "Slice has enough length, a sustained common run, and non-generic opcode overlap.", metrics
    if run_length >= 4 and compare_count >= 10 and branch_call_ratio >= 0.35:
        return "tail-call-pattern-seed", "The common run is usable for orientation but dominated by call/branch tail shape.", metrics
    if lcs_ratio >= 0.4 and compare_count >= 10 and (matching_prefix + matching_suffix + run_length) >= 4:
        return "structural-orientation-seed", "The row can orient a manual split, but specificity is below promotion level.", metrics
    if source_line_count >= 20 and compare_count >= 12:
        return "wide-weak-orientation-seed", "Wide source range aligns structurally but lacks a specific common run.", metrics
    return "near-window-risk", "Near score is insufficiently specific for promotion.", metrics


def qualify_rows(sweep: dict[str, Any], semantic: dict[str, Any]) -> list[dict[str, Any]]:
    semantic_rows = semantic_index(semantic)
    qualified: list[dict[str, Any]] = []
    for row in list_value(sweep.get("rows", [])):
        if not isinstance(row, dict):
            continue
        target_rows = target_rows_for(row, semantic_rows)
        start = int_value(row.get("target_window_start_offset"))
        count = int_value(row.get("compare_instruction_count"))
        target_ops = [str(item.get("op", "")) for item in target_rows[start : start + count]]
        compiled_all_ops = compiled_ops_for(row)
        compiled_skip = int_value(row.get("compiled_skip"))
        compiled_ops = compiled_all_ops[compiled_skip : compiled_skip + count]
        seed_class, seed_reason, metrics = classify_seed(row, target_ops, compiled_ops)
        qualified.append(
            {
                "seed_class": seed_class,
                "seed_reason": seed_reason,
                "entry": row.get("entry", ""),
                "oot3d_name": row.get("oot3d_name", ""),
                "candidate_symbol": row.get("candidate_symbol", ""),
                "source_start_line": row.get("source_start_line", ""),
                "source_end_line": row.get("source_end_line", ""),
                "source_range": f"{row.get('source_start_line', '')}-{row.get('source_end_line', '')}",
                "category": row.get("category", ""),
                "target_start_addr": row.get("target_start_addr", ""),
                "target_end_addr": row.get("target_end_addr", ""),
                "compiled_skip": row.get("compiled_skip", 0),
                "compare_instruction_count": row.get("compare_instruction_count", 0),
                "lcs_instruction_count": row.get("lcs_instruction_count", 0),
                "lcs_window_ratio": row.get("lcs_window_ratio", 0.0),
                "longest_common_run": row.get("longest_common_run", ""),
                "matching_prefix": row.get("matching_prefix", 0),
                "matching_suffix": row.get("matching_suffix", 0),
                "first_difference": row.get("first_difference", ""),
                "target_ops": "; ".join(target_ops),
                "compiled_ops": "; ".join(compiled_ops),
                **metrics,
                "probe_source": row.get("probe_source", ""),
                "dump": row.get("dump", ""),
            }
        )
    class_rank = {
        "candidate-control-flow-seed": 0,
        "tail-call-pattern-seed": 1,
        "structural-orientation-seed": 2,
        "wide-weak-orientation-seed": 3,
        "short-generic-near-risk": 4,
        "near-window-risk": 5,
        "not-near-window": 6,
    }
    qualified.sort(
        key=lambda item: (
            class_rank.get(str(item.get("seed_class", "")), 99),
            -float_value(item.get("lcs_window_ratio")),
            -int_value(item.get("longest_common_run_length")),
            -int_value(item.get("compare_instruction_count")),
            int_value(item.get("source_start_line")),
            int_value(item.get("source_end_line")),
        )
    )
    return qualified


def build_report(args: argparse.Namespace) -> dict[str, Any]:
    sweep = read_json(args.sweep, {})
    semantic = read_json(args.semantic, {})
    rows = qualify_rows(sweep, semantic)
    classes = Counter(str(row.get("seed_class", "")) for row in rows)
    qualified_classes = {"candidate-control-flow-seed", "tail-call-pattern-seed", "structural-orientation-seed"}
    risk_classes = {"short-generic-near-risk", "near-window-risk"}
    near_rows = [row for row in rows if str(row.get("category", "")) in NEAR_CATEGORIES]
    qualified_rows = [row for row in rows if str(row.get("seed_class", "")) in qualified_classes]
    risk_rows = [row for row in rows if str(row.get("seed_class", "")) in risk_classes]
    best = qualified_rows[0] if qualified_rows else (near_rows[0] if near_rows else (rows[0] if rows else {}))
    summary = {
        "sweep_rows": len(rows),
        "near_or_exact_rows": len(near_rows),
        "qualified_seed_rows": len(qualified_rows),
        "risk_rows": len(risk_rows),
        "classes": dict(sorted(classes.items())),
        "best_seed_entry": best.get("entry", ""),
        "best_seed_candidate_symbol": best.get("candidate_symbol", ""),
        "best_seed_source_range": best.get("source_range", ""),
        "best_seed_target_start_addr": best.get("target_start_addr", ""),
        "best_seed_compiled_skip": best.get("compiled_skip", 0),
        "best_seed_class": best.get("seed_class", ""),
        "best_seed_lcs_window_ratio": best.get("lcs_window_ratio", 0.0),
        "best_seed_longest_common_run": best.get("longest_common_run", ""),
        "next_gate": "Use qualified seeds for manual control-flow isolation; do not promote risk rows as maintained C.",
    }
    return {
        "format": "oot3d_direct_source_subregion_seed_qualification_v1",
        "inputs": {
            "sweep": rel(args.sweep),
            "semantic": rel(args.semantic),
        },
        "summary": summary,
        "rows": rows,
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct Source Subregion Seed Qualification",
        "",
        "This report qualifies near/exact subregion sweep rows before they are used as decompilation split seeds.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Sweep rows | {summary['sweep_rows']} |",
        f"| Near/exact rows | {summary['near_or_exact_rows']} |",
        f"| Qualified seed rows | {summary['qualified_seed_rows']} |",
        f"| Risk rows | {summary['risk_rows']} |",
        f"| Best seed LCS | {float_value(summary['best_seed_lcs_window_ratio']):.4f} |",
        "",
        "## Classes",
        "",
        "| Class | Rows |",
        "| --- | ---: |",
    ]
    for seed_class, count in summary["classes"].items():
        lines.append(f"| `{seed_class}` | {count} |")
    lines.extend(
        [
            "",
            "## Top Seeds",
            "",
            "| Class | OOT3D | Candidate | Source range | Target start | Skip | LCS | Run | Generic | Reason |",
            "| --- | --- | --- | ---: | --- | ---: | ---: | --- | ---: | --- |",
        ]
    )
    for row in data["rows"][:25]:
        lines.append(
            f"| `{row.get('seed_class', '')}` | `{row.get('entry', '')}` `{row.get('oot3d_name', '')}` | "
            f"`{row.get('candidate_symbol', '')}` | {row.get('source_range', '')} | "
            f"`{row.get('target_start_addr', '')}` | {row.get('compiled_skip', '')} | "
            f"{float_value(row.get('lcs_window_ratio')):.4f} | {row.get('longest_common_run', '')} | "
            f"{float_value(row.get('generic_op_ratio')):.4f} | {row.get('seed_reason', '')} |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sweep", type=Path, default=DEFAULT_SWEEP)
    parser.add_argument("--semantic", type=Path, default=DEFAULT_SEMANTIC)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    args = parser.parse_args()

    data = build_report(args)
    fields = [
        "seed_class",
        "seed_reason",
        "entry",
        "oot3d_name",
        "candidate_symbol",
        "source_range",
        "category",
        "target_start_addr",
        "target_end_addr",
        "compiled_skip",
        "compare_instruction_count",
        "lcs_instruction_count",
        "lcs_window_ratio",
        "longest_common_run",
        "longest_common_run_length",
        "matching_prefix",
        "matching_suffix",
        "generic_op_ratio",
        "branch_call_ratio",
        "shared_specific_opcodes",
        "first_difference",
        "probe_source",
        "dump",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"], fields)
    write_markdown(args.out_md, data)
    summary = data["summary"]
    print(
        "direct source subregion seed qualification: "
        f"{summary['qualified_seed_rows']}/{summary['near_or_exact_rows']} qualified, "
        f"{summary['risk_rows']} risk, "
        f"best {summary['best_seed_entry']} {summary['best_seed_candidate_symbol']} "
        f"{summary['best_seed_source_range']} {summary['best_seed_class']} "
        f"{summary['best_seed_lcs_window_ratio']}"
    )
    print(str(args.out_md.relative_to(ROOT)).replace("\\", "/"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
