#!/usr/bin/env python3
"""Diagnose focused direct-window adapter probe packets."""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import Counter
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PROBES = ROOT / "analysis" / "direct_window_adapter_probes.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_window_adapter_probe_diagnostics.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_window_adapter_probe_diagnostics.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_window_adapter_probe_diagnostics.md"
COND_SUFFIXES = {
    "eq",
    "ne",
    "cs",
    "hs",
    "cc",
    "lo",
    "mi",
    "pl",
    "vs",
    "vc",
    "hi",
    "ls",
    "ge",
    "lt",
    "gt",
    "le",
}
CORE_MNEMONICS = {
    "add",
    "and",
    "bic",
    "cmp",
    "cmn",
    "eor",
    "ldr",
    "ldrb",
    "ldrh",
    "ldrsh",
    "ldrsb",
    "mov",
    "mvn",
    "orr",
    "rsb",
    "sbc",
    "str",
    "strb",
    "strh",
    "sub",
    "tst",
}


def read_json(path: Path, default: Any) -> Any:
    if not path.is_file():
        return default
    return json.loads(path.read_text(encoding="utf-8-sig"))


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


def int_value(value: Any) -> int:
    try:
        return int(value or 0)
    except (TypeError, ValueError):
        return 0


def float_value(value: Any) -> float:
    try:
        return float(value or 0.0)
    except (TypeError, ValueError):
        return 0.0


def list_value(value: Any) -> list[Any]:
    return value if isinstance(value, list) else []


def repo_path(value: Any) -> Path:
    path = Path(str(value or ""))
    return path if path.is_absolute() else ROOT / path


def read_ops(path: Path) -> list[str]:
    if not path.is_file():
        return []
    ops: list[str] = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if ":" not in line:
            continue
        ops.append(line.split(":", 1)[1].strip())
    return ops


def mnemonic(op: str) -> str:
    return op.split(" ", 1)[0].strip().lower()


def base_mnemonic(op: str) -> str:
    mnem = mnemonic(op)
    if mnem.startswith("v"):
        return mnem
    for suffix in COND_SUFFIXES:
        if mnem.endswith(suffix) and len(mnem) > len(suffix) + 1:
            base = mnem[: -len(suffix)]
            if base.endswith("s") and base[:-1] in CORE_MNEMONICS:
                return base[:-1]
            if base in CORE_MNEMONICS or base.startswith("ldr") or base.startswith("str"):
                return base
    return mnem


def is_unusual_conditional(op: str) -> bool:
    mnem = mnemonic(op)
    if mnem.startswith("b") or mnem.startswith("bl"):
        return False
    if mnem in {"moveq", "movne"}:
        return False
    for suffix in COND_SUFFIXES:
        if mnem.endswith(suffix) and len(mnem) > len(suffix) + 1:
            base = mnem[: -len(suffix)]
            if base.endswith("s") and base[:-1] in CORE_MNEMONICS:
                return True
            return base in CORE_MNEMONICS or base.startswith("ldr") or base.startswith("str")
    return False


def count_leading_unusual(ops: list[str]) -> int:
    count = 0
    for op in ops[:12]:
        if is_unusual_conditional(op):
            count += 1
            continue
        break
    return count


def op_class_counts(ops: list[str]) -> dict[str, int]:
    counts: Counter[str] = Counter()
    for op in ops:
        mnem = mnemonic(op)
        base = base_mnemonic(op)
        if mnem.startswith("bl"):
            counts["call"] += 1
        elif mnem.startswith("b"):
            counts["branch"] += 1
        elif base.startswith("ldr"):
            counts["load"] += 1
        elif base.startswith("str"):
            counts["store"] += 1
        elif base.startswith("v"):
            counts["vfp"] += 1
        elif base in {"cmp", "cmn", "tst"}:
            counts["compare"] += 1
        elif base in {"mov", "mvn"}:
            counts["move"] += 1
        elif base in {"add", "sub", "rsb", "and", "orr", "eor", "bic"}:
            counts["alu"] += 1
        else:
            counts["other"] += 1
    return dict(sorted(counts.items()))


def mismatch_sample(target_ops: list[str], compiled_ops: list[str], limit: int) -> list[str]:
    rows: list[str] = []
    for index, (target, compiled) in enumerate(zip(target_ops, compiled_ops)):
        if base_mnemonic(target) == base_mnemonic(compiled):
            continue
        rows.append(f"{index}: {target} <> {compiled}")
        if len(rows) >= limit:
            break
    return rows


def diagnostic_class(target_ops: list[str], compiled_ops: list[str], row: dict[str, Any]) -> str:
    if count_leading_unusual(target_ops) >= 3:
        return "target-window-starts-in-data-like-run"
    first_difference = str(row.get("first_difference", ""))
    if " vs bl <target>" in first_difference or "bl <target> vs " in first_difference:
        return "call-shape-mismatch"
    target_counts = op_class_counts(target_ops)
    compiled_counts = op_class_counts(compiled_ops)
    if abs(target_counts.get("call", 0) - compiled_counts.get("call", 0)) >= 3:
        return "call-count-delta"
    if abs(target_counts.get("load", 0) - compiled_counts.get("load", 0)) >= 6:
        return "load-store-shape-delta"
    if float_value(row.get("lcs_window_ratio")) < 0.18:
        return "low-body-signal"
    return "semantic-body-shape-gap"


def next_gate(diag: str) -> str:
    gates = {
        "target-window-starts-in-data-like-run": "Move the target boundary past decoded data/literal-like instructions before recompiling or comparing.",
        "call-shape-mismatch": "Resolve call/inlining differences or add helper-call adapters before source promotion.",
        "call-count-delta": "Map target calls and helper calls before trying source-shape promotion.",
        "load-store-shape-delta": "Audit struct offsets/register roles for this helper body.",
        "low-body-signal": "Treat as evidence only; search for a better candidate or boundary.",
        "semantic-body-shape-gap": "Mine local shape anchors and adapter rewrites for this body.",
    }
    return gates.get(diag, "Keep blocked until the focused packet reaches near/exact quality.")


def build_report(probes: dict[str, Any], sample_limit: int) -> dict[str, Any]:
    rows: list[dict[str, Any]] = []
    for source in list_value(probes.get("rows", [])):
        if not isinstance(source, dict) or source.get("status") != "materialized":
            continue
        target_ops = read_ops(repo_path(source.get("target_window_ops", "")))
        compiled_ops = read_ops(repo_path(source.get("compiled_helper_body_ops", "")))
        diag = diagnostic_class(target_ops, compiled_ops, source)
        target_counts = op_class_counts(target_ops)
        compiled_counts = op_class_counts(compiled_ops)
        rows.append(
            {
                "entry": source.get("entry", ""),
                "oot3d_name": source.get("oot3d_name", ""),
                "candidate_symbol": source.get("candidate_symbol", ""),
                "blocker_class": source.get("blocker_class", ""),
                "diagnostic_class": diag,
                "refined_window": source.get("refined_window", ""),
                "compiled_skip": int_value(source.get("compiled_skip")),
                "lcs_window_ratio": float_value(source.get("lcs_window_ratio")),
                "target_instruction_count": int_value(source.get("target_instruction_count")),
                "compiled_instruction_count": int_value(source.get("compiled_instruction_count")),
                "leading_unusual_target_ops": count_leading_unusual(target_ops),
                "target_call_count": target_counts.get("call", 0),
                "compiled_call_count": compiled_counts.get("call", 0),
                "target_branch_count": target_counts.get("branch", 0),
                "compiled_branch_count": compiled_counts.get("branch", 0),
                "target_load_count": target_counts.get("load", 0),
                "compiled_load_count": compiled_counts.get("load", 0),
                "target_store_count": target_counts.get("store", 0),
                "compiled_store_count": compiled_counts.get("store", 0),
                "target_classes": target_counts,
                "compiled_classes": compiled_counts,
                "mismatch_sample": mismatch_sample(target_ops, compiled_ops, sample_limit),
                "probe_manifest": source.get("probe_manifest", ""),
                "source_excerpt": source.get("source_excerpt", ""),
                "next_gate": next_gate(diag),
            }
        )
    rows.sort(
        key=lambda row: (
            row["diagnostic_class"] != "target-window-starts-in-data-like-run",
            -float_value(row["lcs_window_ratio"]),
            str(row["entry"]),
        )
    )
    diagnostics = Counter(str(row["diagnostic_class"]) for row in rows)
    summary = {
        "packets": len(rows),
        "diagnostic_classes": dict(sorted(diagnostics.items())),
        "data_like_start_packets": diagnostics["target-window-starts-in-data-like-run"],
        "call_shape_packets": diagnostics["call-shape-mismatch"] + diagnostics["call-count-delta"],
        "low_body_signal_packets": diagnostics["low-body-signal"],
        "top_entry": rows[0]["entry"] if rows else "",
        "top_candidate_symbol": rows[0]["candidate_symbol"] if rows else "",
        "top_diagnostic_class": rows[0]["diagnostic_class"] if rows else "",
        "next_gate": "Fix data-like target boundaries first, then resolve call/shape deltas in focused packets.",
    }
    return {
        "format": "oot3d_direct_window_adapter_probe_diagnostics_v1",
        "summary": summary,
        "rows": rows,
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct Window Adapter Probe Diagnostics",
        "",
        "This report diagnoses focused adapter probe packets before any source promotion.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Packets | {summary['packets']} |",
        f"| Data-like target starts | {summary['data_like_start_packets']} |",
        f"| Call-shape packets | {summary['call_shape_packets']} |",
        f"| Low-body-signal packets | {summary['low_body_signal_packets']} |",
        "",
        "## Diagnostics",
        "",
        "| Diagnostic | OOT3D | Candidate | LCS | Calls T/C | Loads T/C | Leading unusual | Next gate |",
        "| --- | --- | --- | ---: | ---: | ---: | ---: | --- |",
    ]
    for row in data["rows"]:
        lines.append(
            f"| `{row['diagnostic_class']}` | `{row['entry']}` `{row['oot3d_name']}` | "
            f"`{row['candidate_symbol']}` | {row['lcs_window_ratio']:.4f} | "
            f"{row['target_call_count']}/{row['compiled_call_count']} | "
            f"{row['target_load_count']}/{row['compiled_load_count']} | "
            f"{row['leading_unusual_target_ops']} | {row['next_gate']} |"
        )
    lines.extend(["", "## Mismatch Samples", ""])
    for row in data["rows"]:
        lines.append(f"### `{row['entry']}` `{row['candidate_symbol']}`")
        for sample in row["mismatch_sample"]:
            lines.append(f"- `{sample}`")
        if not row["mismatch_sample"]:
            lines.append("- _none_")
        lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probes", type=Path, default=DEFAULT_PROBES)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    parser.add_argument("--sample-limit", type=int, default=8)
    args = parser.parse_args()

    data = build_report(read_json(args.probes, {}), args.sample_limit)
    fields = [
        "entry",
        "oot3d_name",
        "candidate_symbol",
        "blocker_class",
        "diagnostic_class",
        "refined_window",
        "compiled_skip",
        "lcs_window_ratio",
        "target_instruction_count",
        "compiled_instruction_count",
        "leading_unusual_target_ops",
        "target_call_count",
        "compiled_call_count",
        "target_branch_count",
        "compiled_branch_count",
        "target_load_count",
        "compiled_load_count",
        "target_store_count",
        "compiled_store_count",
        "target_classes",
        "compiled_classes",
        "mismatch_sample",
        "probe_manifest",
        "source_excerpt",
        "next_gate",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"], fields)
    write_markdown(args.out_md, data)
    summary = data["summary"]
    print(
        "direct window adapter probe diagnostics: "
        f"{summary['packets']} packets, "
        f"{summary['data_like_start_packets']} data-like starts, "
        f"{summary['call_shape_packets']} call-shape packets"
    )
    print(str(args.out_md.relative_to(ROOT)).replace("\\", "/"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
