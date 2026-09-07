#!/usr/bin/env python3
"""Infer target split addresses from direct split/helper workorders.

The direct split workorders already identify N64 helper functions whose
compiled instruction stream appears inside an OOT3D target function. This
script projects each common-run anchor back onto the target disassembly so the
next batch step can review or promote OOT3D function boundaries instead of
working one imported helper at a time.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import Counter
from pathlib import Path
from typing import Any

from compare_runtime_objects import (
    TARGET_HEADER_RE,
    normalize_op,
    read_manual_symbol_aliases,
)


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_WORKORDERS = ROOT / "analysis" / "direct_split_workorders.json"
DEFAULT_TARGET_DISASSEMBLY = ROOT / "ghidra_export" / "disassembly.txt"
DEFAULT_MANUAL_SYMBOLS = ROOT / "symbols" / "manual_symbols.csv"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_target_split_suggestions.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_target_split_suggestions.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_target_split_suggestions.md"
DEFAULT_OUT_SYMBOL_CSV = ROOT / "analysis" / "direct_target_split_symbol_candidates.csv"

TARGET_INSN_ADDR_RE = re.compile(r"^(?P<addr>[0-9a-fA-F]+):\s+(?P<op>.+?)\s*$")
RUN_RE = re.compile(r"^(?P<length>\d+)@(?P<target>\d+)/(?P<compiled>\d+)$")


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


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


def read_target_functions_with_addresses(path: Path) -> dict[str, dict[str, Any]]:
    functions: dict[str, dict[str, Any]] = {}
    current_name: str | None = None

    for raw_line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        header = TARGET_HEADER_RE.match(raw_line)
        if header:
            current_name = header.group("name")
            functions[current_name] = {
                "name": current_name,
                "entry": header.group("addr").lower(),
                "addresses": [],
                "raw_ops": [],
                "ops": [],
            }
            continue

        if current_name is None:
            continue

        if not raw_line.strip():
            current_name = None
            continue

        insn = TARGET_INSN_ADDR_RE.match(raw_line)
        if insn:
            functions[current_name]["addresses"].append(insn.group("addr").lower())
            functions[current_name]["raw_ops"].append(insn.group("op").strip().lower())
            functions[current_name]["ops"].append(normalize_op(insn.group("op")))

    return functions


def apply_target_aliases(
    functions: dict[str, dict[str, Any]],
    aliases_by_entry: dict[str, str],
) -> dict[str, dict[str, Any]]:
    aliased = dict(functions)
    for function in functions.values():
        alias = aliases_by_entry.get(str(function["entry"]).lower())
        if alias and alias not in aliased:
            aliased[alias] = function
    return aliased


def index_target_functions(functions: dict[str, dict[str, Any]]) -> dict[str, dict[str, Any]]:
    by_entry: dict[str, dict[str, Any]] = {}
    for function in functions.values():
        by_entry[str(function["entry"]).lower()] = function
    return by_entry


def read_manual_symbols(path: Path) -> dict[str, dict[str, str]]:
    if not path.is_file():
        return {}

    rows: dict[str, dict[str, str]] = {}
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            entry = row.get("entry", "").lower()
            if entry:
                rows[entry] = row
    return rows


def parse_run(value: str) -> dict[str, int] | None:
    match = RUN_RE.match(value.strip())
    if not match:
        return None
    return {
        "run_length": int(match.group("length")),
        "target_index": int(match.group("target")),
        "compiled_index": int(match.group("compiled")),
    }


def snake_name(value: str) -> str:
    value = value.strip()
    value = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", value)
    value = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1_\2", value)
    value = re.sub(r"[^A-Za-z0-9]+", "_", value)
    value = re.sub(r"_+", "_", value).strip("_").lower()
    value = merge_trailing_hex_chunks(value)
    return value or "split_candidate"


def merge_trailing_hex_chunks(value: str) -> str:
    parts = value.split("_")
    suffix: list[str] = []
    while parts and re.fullmatch(r"[0-9a-f]+", parts[-1]):
        suffix.insert(0, parts.pop())
    if len(suffix) >= 2 and any(any(char.isdigit() for char in part) for part in suffix):
        parts.append("".join(suffix))
        return "_".join(parts)
    return value


def suggested_symbol(row: dict[str, Any]) -> str:
    base = snake_name(str(row.get("candidate_symbol", "")))
    if base.startswith("func_"):
        domain = snake_name(str(row.get("domain", "")))
        return f"oot3d_{domain}_{base}" if domain else f"oot3d_{base}"
    return f"oot3d_{base}"


def prologue_like(op: str) -> bool:
    normalized = normalize_op(op)
    if re.match(r"^stmdb sp!,\{[^{}]*lr[^{}]*\}$", normalized):
        return True
    if re.match(r"^push\s+\{[^{}]*lr[^{}]*\}$", op):
        return True
    if normalized.startswith("vpush ") or normalized.startswith("sub sp,sp,"):
        return True
    return False


def branch_reference_counts(raw_ops: list[str], address: str) -> tuple[int, int]:
    stripped_address = address.lower().lstrip("0") or "0"
    target_re = re.compile(rf"\b0x0*{re.escape(stripped_address)}\b", re.IGNORECASE)
    branch_refs = 0
    call_refs = 0
    for op in raw_ops:
        stripped = op.strip().lower()
        if not target_re.search(stripped):
            continue
        if stripped.startswith("bl") or stripped.startswith("blx"):
            call_refs += 1
        elif re.match(r"^b[a-z]*\s+", stripped):
            branch_refs += 1
    return branch_refs, call_refs


def boundary_evidence(
    target: dict[str, Any] | None,
    start_index: int | None,
    start_addr: str,
) -> dict[str, Any]:
    if target is None or start_index is None or not start_addr:
        return {
            "boundary_kind": "missing",
            "start_op": "",
            "branch_ref_count": 0,
            "call_ref_count": 0,
            "prologue_like": "no",
        }

    raw_ops = list(target.get("raw_ops", []))
    start_op = str(raw_ops[start_index]) if 0 <= start_index < len(raw_ops) else ""
    branch_refs, call_refs = branch_reference_counts(raw_ops, start_addr)
    is_prologue = prologue_like(start_op)

    if is_prologue:
        kind = "function-prologue"
    elif branch_refs:
        kind = "branch-target-anchor"
    else:
        kind = "mid-block-anchor"

    return {
        "boundary_kind": kind,
        "start_op": start_op,
        "branch_ref_count": branch_refs,
        "call_ref_count": call_refs,
        "prologue_like": "yes" if is_prologue else "no",
    }


def classify_row(
    workorder: dict[str, Any],
    target: dict[str, Any] | None,
    run: dict[str, int] | None,
    start_index: int | None,
    start_addr: str,
    existing_symbol: dict[str, str] | None,
    boundary: dict[str, Any],
    args: argparse.Namespace,
) -> tuple[str, str, str]:
    if target is None:
        return "target-missing", "low", "Find/apply the OOT3D target symbol before promoting this split."
    if run is None:
        return "run-unparsed", "low", "Re-run the symbol scan; the common-run anchor is not parseable."
    if start_index is None or not start_addr:
        return "anchor-out-of-range", "low", "Inspect manually; the inferred split address falls outside the parsed target."
    if existing_symbol:
        return "already-symbolized", "high", "Use the existing manual symbol and rebuild/export before comparing the helper."
    if workorder.get("source_span_status") != "complete":
        return "source-truncated", "medium", "Complete the materialized source span before promoting the target split."
    if workorder.get("reason") == "mapped-symbol-reshape":
        return "reshape-anchor", "medium", "Use this as a reshape anchor for the mapped function, not a new split yet."
    if start_index < args.min_nested_start_insns:
        return "entry-adjacent", "medium", "Treat as an entry remap/prologue mismatch before adding a new split symbol."
    if boundary.get("boundary_kind") == "function-prologue":
        return "function-split", "high", "Promote as a manual-symbol candidate, then export and compare this helper."
    if boundary.get("boundary_kind") == "branch-target-anchor":
        return "block-anchor", "medium", "Use as an internal shape anchor; do not promote as a function until a prologue/call boundary is confirmed."
    return "body-anchor", "medium", "Use as an internal shape anchor; it is not currently a safe function-symbol candidate."



def build_rows(args: argparse.Namespace) -> tuple[list[dict[str, Any]], list[dict[str, str]], dict[str, Any]]:
    data = read_json(args.workorders_json, {})
    workorders = data.get("rows", []) if isinstance(data, dict) else []

    target_functions = read_target_functions_with_addresses(args.target_disassembly)
    aliased_functions = apply_target_aliases(
        target_functions,
        read_manual_symbol_aliases(args.manual_symbols),
    )
    functions_by_entry = index_target_functions(target_functions)
    manual_symbols_by_entry = read_manual_symbols(args.manual_symbols)

    rows: list[dict[str, Any]] = []
    symbol_rows: list[dict[str, str]] = []
    used_symbol_names: Counter[str] = Counter()

    for workorder in workorders:
        entry = str(workorder.get("entry", "")).lower()
        target = aliased_functions.get(str(workorder.get("oot3d_name", ""))) or functions_by_entry.get(entry)
        run = parse_run(str(workorder.get("longest_common_run", "")))
        addresses = list(target.get("addresses", [])) if target else []

        start_index_raw: int | None = None
        start_index: int | None = None
        start_addr = ""
        end_index: int | None = None
        end_addr = ""
        run_target_addr = ""
        if target and run:
            target_index = run["target_index"]
            compiled_index = run["compiled_index"]
            if 0 <= target_index < len(addresses):
                run_target_addr = str(addresses[target_index])
            start_index_raw = target_index - compiled_index
            start_index = max(start_index_raw, 0)
            if 0 <= start_index < len(addresses):
                start_addr = str(addresses[start_index])
                candidate_insns = int_value(workorder.get("candidate_instruction_count"))
                end_index = min(start_index + max(candidate_insns - 1, 0), len(addresses) - 1)
                end_addr = str(addresses[end_index]) if 0 <= end_index < len(addresses) else ""

        existing_symbol = manual_symbols_by_entry.get(start_addr.lower()) if start_addr else None
        boundary = boundary_evidence(target, start_index, start_addr)
        kind, confidence, next_action = classify_row(
            workorder,
            target,
            run,
            start_index,
            start_addr,
            existing_symbol,
            boundary,
            args,
        )
        symbol_candidate = (
            kind == "function-split"
            and confidence == "high"
            and start_addr
            and not existing_symbol
        )

        proposed_symbol = suggested_symbol(workorder)
        used_symbol_names[proposed_symbol] += 1
        if used_symbol_names[proposed_symbol] > 1:
            proposed_symbol = f"{proposed_symbol}_{entry}"

        row = {
            "priority": int_value(workorder.get("priority")),
            "kind": kind,
            "confidence": confidence,
            "symbol_candidate": "yes" if symbol_candidate else "no",
            "entry": entry,
            "oot3d_name": workorder.get("oot3d_name", ""),
            "target_name": target.get("name", "") if target else "",
            "domain": workorder.get("domain", ""),
            "mapped_n64_name": workorder.get("mapped_n64_name", ""),
            "candidate_symbol": workorder.get("candidate_symbol", ""),
            "proposed_oot3d_symbol": proposed_symbol,
            "reason": workorder.get("reason", ""),
            "boundary_kind": boundary["boundary_kind"],
            "start_op": boundary["start_op"],
            "branch_ref_count": boundary["branch_ref_count"],
            "call_ref_count": boundary["call_ref_count"],
            "prologue_like": boundary["prologue_like"],
            "target_instruction_count": int_value(workorder.get("target_instruction_count")),
            "candidate_instruction_count": int_value(workorder.get("candidate_instruction_count")),
            "lcs_target_ratio": float_value(workorder.get("lcs_target_ratio")),
            "lcs_symbol_ratio": float_value(workorder.get("lcs_symbol_ratio")),
            "longest_common_run": workorder.get("longest_common_run", ""),
            "run_length": run["run_length"] if run else 0,
            "target_run_index": run["target_index"] if run else -1,
            "compiled_run_index": run["compiled_index"] if run else -1,
            "target_run_addr": run_target_addr,
            "inferred_start_index_raw": start_index_raw if start_index_raw is not None else "",
            "inferred_start_index": start_index if start_index is not None else "",
            "inferred_start_addr": start_addr,
            "inferred_end_index": end_index if end_index is not None else "",
            "inferred_end_addr": end_addr,
            "existing_manual_symbol": existing_symbol.get("new_name", "") if existing_symbol else "",
            "source_span_status": workorder.get("source_span_status", ""),
            "source_nonblank_lines": int_value(workorder.get("source_nonblank_lines")),
            "source": workorder.get("source", ""),
            "packet": workorder.get("packet", ""),
            "next_action": next_action,
        }
        rows.append(row)

        if symbol_candidate:
            symbol_rows.append(
                {
                    "entry": start_addr,
                    "old_name": f"FUN_{start_addr}",
                    "new_name": proposed_symbol,
                    "kind": "function",
                    "confidence": "medium",
                    "source_file": rel(args.out_md),
                    "notes": (
                        "Batch split candidate inferred from direct packet symbol scan: "
                        f"{workorder.get('candidate_symbol', '')} in {workorder.get('oot3d_name', '')}; "
                        f"run {workorder.get('longest_common_run', '')}; "
                        f"source {workorder.get('source', '')}."
                    ),
                }
            )

    rows.sort(
        key=lambda row: (
            row["symbol_candidate"] != "yes",
            row["kind"] != "function-split",
            -int_value(row["priority"]),
            str(row["entry"]),
            str(row["candidate_symbol"]),
        )
    )
    symbol_rows.sort(key=lambda row: (row["entry"], row["new_name"]))

    kinds = Counter(str(row["kind"]) for row in rows)
    boundary_kinds = Counter(str(row["boundary_kind"]) for row in rows)
    confidence = Counter(str(row["confidence"]) for row in rows)
    summary = {
        "suggestions": len(rows),
        "entries": len({row["entry"] for row in rows if row["entry"]}),
        "candidate_symbols": len({row["candidate_symbol"] for row in rows if row["candidate_symbol"]}),
        "inferred_addresses": len({row["inferred_start_addr"] for row in rows if row["inferred_start_addr"]}),
        "symbol_candidates": len(symbol_rows),
        "existing_manual_symbols": sum(1 for row in rows if row["existing_manual_symbol"]),
        "kinds": dict(sorted(kinds.items())),
        "boundary_kinds": dict(sorted(boundary_kinds.items())),
        "confidence": dict(sorted(confidence.items())),
        "min_nested_start_insns": args.min_nested_start_insns,
    }
    return rows, symbol_rows, summary


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    fieldnames = [
        "priority",
        "kind",
        "confidence",
        "symbol_candidate",
        "entry",
        "oot3d_name",
        "target_name",
        "domain",
        "mapped_n64_name",
        "candidate_symbol",
        "proposed_oot3d_symbol",
        "reason",
        "boundary_kind",
        "start_op",
        "branch_ref_count",
        "call_ref_count",
        "prologue_like",
        "target_instruction_count",
        "candidate_instruction_count",
        "lcs_target_ratio",
        "lcs_symbol_ratio",
        "longest_common_run",
        "run_length",
        "target_run_index",
        "compiled_run_index",
        "target_run_addr",
        "inferred_start_index_raw",
        "inferred_start_index",
        "inferred_start_addr",
        "inferred_end_index",
        "inferred_end_addr",
        "existing_manual_symbol",
        "source_span_status",
        "source_nonblank_lines",
        "source",
        "packet",
        "next_action",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fieldnames})


def write_symbol_csv(path: Path, rows: list[dict[str, str]]) -> None:
    fieldnames = ["entry", "old_name", "new_name", "kind", "confidence", "source_file", "notes"]
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def write_markdown(path: Path, rows: list[dict[str, Any]], summary: dict[str, Any], symbol_csv: Path) -> None:
    kind_text = ", ".join(f"{key}: {value}" for key, value in summary["kinds"].items()) or "none"
    boundary_text = ", ".join(f"{key}: {value}" for key, value in summary["boundary_kinds"].items()) or "none"
    confidence_text = ", ".join(f"{key}: {value}" for key, value in summary["confidence"].items()) or "none"
    lines = [
        "# Direct Target Split Suggestions",
        "",
        "This queue projects direct split/helper workorders onto OOT3D target addresses.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Suggestions | {summary['suggestions']} |",
        f"| OOT3D entries covered | {summary['entries']} |",
        f"| Candidate N64 symbols | {summary['candidate_symbols']} |",
        f"| Unique inferred target addresses | {summary['inferred_addresses']} |",
        f"| Manual-symbol candidates | {summary['symbol_candidates']} |",
        f"| Existing manual symbols at inferred addresses | {summary['existing_manual_symbols']} |",
        f"| Minimum nested-start instruction distance | {summary['min_nested_start_insns']} |",
        "",
        f"- Kinds: {kind_text}",
        f"- Boundary evidence: {boundary_text}",
        f"- Confidence: {confidence_text}",
        f"- Manual-symbol candidate CSV: `{rel(symbol_csv)}`",
        "",
        "## Queue",
        "",
        "| Priority | Kind | Boundary | OOT3D | Candidate | Address | Dist | Insns | LCS | Run | Span | Proposed symbol | Next action |",
        "| ---: | --- | --- | --- | --- | --- | ---: | ---: | ---: | --- | --- | --- | --- |",
    ]
    for row in rows:
        address = row["inferred_start_addr"] or "?"
        distance = row["inferred_start_index"] if row["inferred_start_index"] != "" else "?"
        symbol = row["existing_manual_symbol"] or row["proposed_oot3d_symbol"]
        lines.append(
            f"| {row['priority']} | `{row['kind']}` `{row['confidence']}` | "
            f"`{row['boundary_kind']}` | "
            f"`{row['entry']}` `{row['oot3d_name']}` | `{row['candidate_symbol']}` | "
            f"`{address}` | {distance} | "
            f"{row['target_instruction_count']}/{row['candidate_instruction_count']} | "
            f"{float_value(row['lcs_target_ratio']):.4f}/{float_value(row['lcs_symbol_ratio']):.4f} | "
            f"{row['longest_common_run']} | `{row['source_span_status']}` | `{symbol}` | "
            f"{row['next_action']} |"
        )
    if not rows:
        lines.append("|  | _none_ |  |  |  |  |  |  |  |  |  |  |  |")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workorders-json", type=Path, default=DEFAULT_WORKORDERS)
    parser.add_argument("--target-disassembly", type=Path, default=DEFAULT_TARGET_DISASSEMBLY)
    parser.add_argument("--manual-symbols", type=Path, default=DEFAULT_MANUAL_SYMBOLS)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    parser.add_argument("--out-symbol-csv", type=Path, default=DEFAULT_OUT_SYMBOL_CSV)
    parser.add_argument("--min-nested-start-insns", type=int, default=16)
    args = parser.parse_args()

    rows, symbol_rows, summary = build_rows(args)
    write_json(args.out_json, {"summary": summary, "rows": rows})
    write_csv(args.out_csv, rows)
    write_symbol_csv(args.out_symbol_csv, symbol_rows)
    write_markdown(args.out_md, rows, summary, args.out_symbol_csv)

    print(
        "direct target split suggestions: "
        f"{summary['suggestions']} suggestions across {summary['entries']} entries, "
        f"{summary['symbol_candidates']} manual-symbol candidates"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
