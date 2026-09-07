#!/usr/bin/env python3
"""Build split/helper workorders from direct packet symbol scans.

The symbol scan tells us when a target is closer to an internal helper than to
the nominal N64 function. This script turns those scan rows into actionable
source workorders by locating candidate function spans in the materialized C
packet.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SCAN = ROOT / "analysis" / "direct_packet_symbol_scan.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_split_workorders.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_split_workorders.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_split_workorders.md"


@dataclass(frozen=True)
class FunctionSpan:
    found: bool
    start_line: int = 0
    end_line: int = 0
    nonblank_lines: int = 0
    status: str = "missing"


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def repo_path(value: str | Path) -> Path:
    path = Path(value)
    if path.is_absolute():
        return path
    return ROOT / path


def read_json(path: Path, default: Any) -> Any:
    if not path.is_file():
        return default
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


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


def line_number_at(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def find_matching_brace(text: str, brace_offset: int) -> int | None:
    depth = 0
    i = brace_offset
    state = "code"
    while i < len(text):
        char = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if state == "code":
            if char == "/" and nxt == "/":
                state = "line-comment"
                i += 2
                continue
            if char == "/" and nxt == "*":
                state = "block-comment"
                i += 2
                continue
            if char == '"':
                state = "string"
                i += 1
                continue
            if char == "'":
                state = "char"
                i += 1
                continue
            if char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0:
                    return i
        elif state == "line-comment":
            if char == "\n":
                state = "code"
        elif state == "block-comment":
            if char == "*" and nxt == "/":
                state = "code"
                i += 2
                continue
        elif state == "string":
            if char == "\\":
                i += 2
                continue
            if char == '"':
                state = "code"
        elif state == "char":
            if char == "\\":
                i += 2
                continue
            if char == "'":
                state = "code"
        i += 1
    return None


def find_function_span(path: Path, name: str) -> FunctionSpan:
    name = name.strip()
    if not path.is_file() or not name:
        return FunctionSpan(False)
    text = path.read_text(encoding="utf-8", errors="replace")
    definition_re = re.compile(rf"(?m)^[A-Za-z_][A-Za-z0-9_\s\*]*\b{re.escape(name)}\s*\([^;{{}}]*\)\s*\{{")
    match = definition_re.search(text)
    if not match:
        return FunctionSpan(False)
    brace = text.find("{", match.start(), match.end())
    if brace < 0:
        return FunctionSpan(False)
    end = find_matching_brace(text, brace)
    if end is None:
        function_text = text[match.start() :]
        return FunctionSpan(
            found=True,
            start_line=line_number_at(text, match.start()),
            end_line=text.count("\n") + 1,
            nonblank_lines=sum(1 for line in function_text.splitlines() if line.strip()),
            status="truncated",
        )
    function_text = text[match.start() : end + 1]
    return FunctionSpan(
        found=True,
        start_line=line_number_at(text, match.start()),
        end_line=line_number_at(text, end),
        nonblank_lines=sum(1 for line in function_text.splitlines() if line.strip()),
        status="complete",
    )


def source_link(path: Path, span: FunctionSpan) -> str:
    if not span.found:
        return rel(path)
    return f"{rel(path)}:{span.start_line}-{span.end_line}"


def candidate_reason(scan_row: dict[str, Any], candidate: dict[str, Any], min_symbol_ratio: float) -> str:
    if candidate.get("symbol") != scan_row.get("mapped_n64_name"):
        return "best-helper" if candidate.get("symbol") == scan_row.get("best_symbol") else "alternate-helper"
    if float_value(candidate.get("lcs_symbol_ratio")) >= min_symbol_ratio:
        return "mapped-symbol-reshape"
    return "low-signal"


def priority(scan_row: dict[str, Any], candidate: dict[str, Any], reason: str, span: FunctionSpan) -> int:
    value = 0
    value += int(float_value(candidate.get("lcs_symbol_ratio")) * 100)
    run = str(candidate.get("longest_common_run", "0@")).split("@", 1)[0]
    value += int_value(run) * 5
    if reason == "best-helper":
        value += 35
    elif reason == "alternate-helper":
        value += 20
    elif reason == "mapped-symbol-reshape":
        value += 10
    if span.found:
        value += 10
    if scan_row.get("scan_category") == "semantic-gap":
        value -= 5
    return value


def recommendation(scan_row: dict[str, Any], candidate: dict[str, Any], reason: str) -> str:
    if reason in {"best-helper", "alternate-helper"}:
        return "Treat this as a split/helper reconstruction candidate; inspect/promote the candidate span before the nominal N64 function."
    if candidate.get("symbol") == scan_row.get("mapped_n64_name"):
        return "Continue reshaping the mapped N64 function; no better helper was found in the packet."
    return "Keep as supporting evidence only; signal is below the configured threshold."


def build_rows(args: argparse.Namespace) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    scan = read_json(args.scan_json, {})
    scan_rows = scan.get("rows", []) if isinstance(scan, dict) else []
    workorders: list[dict[str, Any]] = []

    for scan_row in scan_rows:
        packet = repo_path(str(scan_row.get("packet", "")))
        candidates = scan_row.get("top_candidates", [])
        if not isinstance(candidates, list):
            continue
        selected: list[dict[str, Any]] = []
        for candidate in candidates:
            if not isinstance(candidate, dict):
                continue
            symbol_ratio = float_value(candidate.get("lcs_symbol_ratio"))
            symbol_differs = candidate.get("symbol") != scan_row.get("mapped_n64_name")
            is_best = candidate.get("symbol") == scan_row.get("best_symbol")
            enough_size = int_value(candidate.get("compiled_instruction_count")) >= args.min_insns
            if is_best and (symbol_differs or symbol_ratio >= args.min_symbol_ratio):
                selected.append(candidate)
            elif symbol_differs and enough_size and symbol_ratio >= args.min_symbol_ratio:
                selected.append(candidate)
        for candidate in selected[: args.max_candidates_per_packet]:
            span = find_function_span(packet, str(candidate.get("symbol", "")))
            reason = candidate_reason(scan_row, candidate, args.min_symbol_ratio)
            workorders.append(
                {
                    "priority": priority(scan_row, candidate, reason, span),
                    "reason": reason,
                    "entry": scan_row.get("entry", ""),
                    "oot3d_name": scan_row.get("oot3d_name", ""),
                    "domain": scan_row.get("domain", ""),
                    "mapped_n64_name": scan_row.get("mapped_n64_name", ""),
                    "candidate_symbol": candidate.get("symbol", ""),
                    "best_symbol": scan_row.get("best_symbol", ""),
                    "candidate_is_best": "yes" if candidate.get("symbol") == scan_row.get("best_symbol") else "no",
                    "target_instruction_count": int_value(scan_row.get("target_instruction_count")),
                    "candidate_instruction_count": int_value(candidate.get("compiled_instruction_count")),
                    "lcs_target_ratio": float_value(candidate.get("lcs_target_ratio")),
                    "lcs_symbol_ratio": float_value(candidate.get("lcs_symbol_ratio")),
                    "longest_common_run": candidate.get("longest_common_run", ""),
                    "matching_prefix": int_value(candidate.get("matching_prefix")),
                    "first_difference": candidate.get("first_difference", ""),
                    "source_span_found": "yes" if span.found else "no",
                    "source_span_status": span.status,
                    "source_start_line": span.start_line,
                    "source_end_line": span.end_line,
                    "source_nonblank_lines": span.nonblank_lines,
                    "source": source_link(packet, span),
                    "packet": rel(packet),
                    "dump": scan_row.get("dump", ""),
                    "recommendation": recommendation(scan_row, candidate, reason),
                }
            )

    workorders.sort(key=lambda row: (-int_value(row["priority"]), row["entry"], row["candidate_symbol"]))
    counts = Counter(str(row["reason"]) for row in workorders)
    span_statuses = Counter(str(row["source_span_status"]) for row in workorders)
    summary = {
        "workorders": len(workorders),
        "entries": len({row["entry"] for row in workorders}),
        "source_spans_found": sum(1 for row in workorders if row["source_span_found"] == "yes"),
        "span_statuses": dict(sorted(span_statuses.items())),
        "candidate_symbols": len({row["candidate_symbol"] for row in workorders}),
        "reasons": dict(sorted(counts.items())),
        "min_symbol_ratio": args.min_symbol_ratio,
        "min_insns": args.min_insns,
    }
    return workorders, summary


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    fieldnames = [
        "priority",
        "reason",
        "entry",
        "oot3d_name",
        "domain",
        "mapped_n64_name",
        "candidate_symbol",
        "candidate_is_best",
        "target_instruction_count",
        "candidate_instruction_count",
        "lcs_target_ratio",
        "lcs_symbol_ratio",
        "longest_common_run",
        "matching_prefix",
        "source_span_found",
        "source_span_status",
        "source_start_line",
        "source_end_line",
        "source_nonblank_lines",
        "source",
        "packet",
        "recommendation",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fieldnames})


def write_markdown(path: Path, rows: list[dict[str, Any]], summary: dict[str, Any]) -> None:
    reason_text = ", ".join(f"{key}: {value}" for key, value in summary["reasons"].items()) or "none"
    span_text = ", ".join(f"{key}: {value}" for key, value in summary["span_statuses"].items()) or "none"
    lines = [
        "# Direct Split Workorders",
        "",
        "This queue turns direct packet symbol-scan hits into source-level split/helper workorders.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Workorders | {summary['workorders']} |",
        f"| OOT3D entries covered | {summary['entries']} |",
        f"| Candidate symbols | {summary['candidate_symbols']} |",
        f"| Source spans found | {summary['source_spans_found']} |",
        f"| Minimum symbol LCS ratio | {summary['min_symbol_ratio']:.2f} |",
        f"| Minimum candidate instructions | {summary['min_insns']} |",
        "",
        f"- Reasons: {reason_text}",
        f"- Span status: {span_text}",
        "",
        "## Queue",
        "",
        "| Priority | Reason | OOT3D | Mapped N64 | Candidate | Insns | LCS target/symbol | Run | Span | Source | Next action |",
        "| ---: | --- | --- | --- | --- | ---: | ---: | --- | --- | --- | --- |",
    ]
    for row in rows:
        lines.append(
            f"| {row['priority']} | `{row['reason']}` | `{row['entry']}` `{row['oot3d_name']}` | "
            f"`{row['mapped_n64_name']}` | `{row['candidate_symbol']}` | "
            f"{row['target_instruction_count']}/{row['candidate_instruction_count']} | "
            f"{float_value(row['lcs_target_ratio']):.4f}/{float_value(row['lcs_symbol_ratio']):.4f} | "
            f"{row['longest_common_run']} | `{row['source_span_status']}` | `{row['source']}` | "
            f"{row['recommendation']} |"
        )
    if not rows:
        lines.append("|  | _none_ |  |  |  |  |  |  |  |  |  |")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scan-json", type=Path, default=DEFAULT_SCAN)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    parser.add_argument("--min-symbol-ratio", type=float, default=0.25)
    parser.add_argument("--min-insns", type=int, default=20)
    parser.add_argument("--max-candidates-per-packet", type=int, default=5)
    args = parser.parse_args()

    rows, summary = build_rows(args)
    write_json(args.out_json, {"summary": summary, "rows": rows})
    write_csv(args.out_csv, rows)
    write_markdown(args.out_md, rows, summary)
    print(
        "direct split workorders: "
        f"{summary['workorders']} workorders across {summary['entries']} entries, "
        f"{summary['source_spans_found']} source spans found"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
