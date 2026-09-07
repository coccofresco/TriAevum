#!/usr/bin/env python3
"""Search better local target-window alignments for direct helper candidates."""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from pathlib import Path
from typing import Any

from build_direct_target_split_suggestions import read_target_functions_with_addresses
from compare_direct_packet_probe_matches import read_objdump_file
from compare_direct_target_window_probes import (
    category_order,
    classify_window,
    first_difference_text,
    gap_index,
    repo_path,
    run_text,
    target_by_entry,
)
from compare_runtime_objects import compare_ops


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_WINDOW_MATCH = ROOT / "analysis" / "direct_target_window_probe_match.json"
DEFAULT_ENVELOPE_PLAN = ROOT / "analysis" / "direct_target_envelope_plan.json"
DEFAULT_GAP_REPORT = ROOT / "analysis" / "direct_split_gap_report.json"
DEFAULT_TARGET_DISASSEMBLY = ROOT / "ghidra_export" / "disassembly.txt"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_target_window_refinement.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_target_window_refinement.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_target_window_refinement.md"


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


def rel(path: Path | str) -> str:
    p = Path(path)
    try:
        return str(p.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(p).replace("\\", "/")


def anchor_index(data: dict[str, Any]) -> dict[tuple[str, str, int], dict[str, Any]]:
    result: dict[tuple[str, str, int], dict[str, Any]] = {}
    for row in list_value(data.get("anchor_rows", [])):
        if not isinstance(row, dict):
            continue
        key = (
            str(row.get("entry", "")).lower(),
            str(row.get("candidate_symbol", "")),
            int_value(row.get("cluster_index")),
        )
        result[key] = row
    return result


def compare_score(compare: dict[str, Any], start_delta: int, compiled_skip: int) -> tuple[int, float, int, int, int, int]:
    category = classify_window(compare)
    run = compare.get("longest_common_run", {})
    run_length = int_value(run.get("length")) if isinstance(run, dict) else 0
    prefix = int_value(compare.get("matching_prefix"))
    suffix = int_value(compare.get("matching_suffix"))
    return (
        -category_order(category),
        float_value(compare.get("lcs_target_ratio")),
        run_length,
        prefix,
        suffix,
        -(abs(start_delta) + compiled_skip),
    )


def best_alignment(
    target_ops: list[str],
    compiled_ops: list[str],
    inferred_start: int,
    *,
    search_radius: int,
    max_compiled_skip: int,
    min_slice_insns: int,
) -> dict[str, Any] | None:
    best: dict[str, Any] | None = None
    best_score: tuple[int, float, int, int, int, int] | None = None
    max_start = len(target_ops) - min_slice_insns
    if max_start < 0:
        return None

    for compiled_skip in range(0, min(max_compiled_skip, len(compiled_ops) - min_slice_insns) + 1):
        compiled_slice = compiled_ops[compiled_skip:]
        if len(compiled_slice) < min_slice_insns:
            continue
        for target_start in range(max(0, inferred_start - search_radius), min(max_start, inferred_start + search_radius) + 1):
            target_end = target_start + len(compiled_slice)
            if target_end > len(target_ops):
                continue
            compare = compare_ops(target_ops[target_start:target_end], compiled_slice)
            start_delta = target_start - inferred_start
            score = compare_score(compare, start_delta, compiled_skip)
            if best_score is None or score > best_score:
                best_score = score
                best = {
                    "target_start_index": target_start,
                    "target_end_index": target_end - 1,
                    "compiled_skip": compiled_skip,
                    "compiled_slice_instruction_count": len(compiled_slice),
                    "target_start_delta": start_delta,
                    "compare": compare,
                    "category": classify_window(compare),
                }
    return best


def build_rows(args: argparse.Namespace) -> dict[str, Any]:
    window_match = read_json(args.window_match, {})
    envelope = read_json(args.envelope_plan, {})
    gap_report = read_json(args.gap_report, {})
    anchors = anchor_index(envelope)
    gap_by_key = gap_index(gap_report)
    targets_by_entry = target_by_entry(read_target_functions_with_addresses(args.target_disassembly))
    compiled_cache: dict[Path, dict[str, dict[str, object]]] = {}
    rows: list[dict[str, Any]] = []

    for row in list_value(window_match.get("rows", [])):
        if not isinstance(row, dict):
            continue
        entry = str(row.get("entry", "")).lower()
        candidate = str(row.get("candidate_symbol", ""))
        cluster_index = int_value(row.get("cluster_index"))
        anchor = anchors.get((entry, candidate, cluster_index), {})
        target = targets_by_entry.get(entry)
        gap = gap_by_key.get((entry, candidate), {})
        dump_path = repo_path(gap.get("compiled_dump", ""))
        base = {
            "entry": entry,
            "oot3d_name": row.get("oot3d_name", ""),
            "cluster_index": cluster_index,
            "candidate_symbol": candidate,
            "original_category": row.get("category", ""),
            "original_lcs_window_ratio": float_value(row.get("lcs_window_ratio")),
            "original_window_start_addr": row.get("inferred_start_addr", ""),
            "original_window_end_addr": row.get("inferred_end_addr", ""),
            "original_window_instruction_count": int_value(row.get("window_instruction_count")),
            "compiled_instruction_count": int_value(row.get("compiled_instruction_count")),
            "priority": int_value(row.get("priority")),
            "source": row.get("source", ""),
            "compiled_dump": rel(dump_path) if str(gap.get("compiled_dump", "")) else "",
        }
        if target is None:
            rows.append({**base, "refined_category": "target-missing"})
            continue
        if not dump_path.is_file():
            rows.append({**base, "refined_category": "compiled-dump-missing"})
            continue
        if dump_path not in compiled_cache:
            compiled_cache[dump_path] = read_objdump_file(dump_path)
        compiled = compiled_cache[dump_path].get(candidate)
        if compiled is None:
            rows.append({**base, "refined_category": "compiled-symbol-missing"})
            continue
        target_ops = list(target.get("ops", []))
        target_addresses = list(target.get("addresses", []))
        compiled_ops = list(compiled.get("ops", []))
        inferred_start = int_value(anchor.get("inferred_start_index", row.get("inferred_start_index")))
        refined = best_alignment(
            target_ops,
            compiled_ops,
            inferred_start,
            search_radius=args.search_radius,
            max_compiled_skip=args.max_compiled_skip,
            min_slice_insns=args.min_slice_insns,
        )
        if refined is None:
            rows.append({**base, "refined_category": "no-valid-slice"})
            continue
        compare = refined["compare"]
        target_start = int_value(refined["target_start_index"])
        target_end = int_value(refined["target_end_index"])
        lcs_instruction_count = int_value(compare.get("lcs_instruction_count"))
        compiled_slice_count = int_value(refined["compiled_slice_instruction_count"])
        refined_lcs = float_value(compare.get("lcs_target_ratio"))
        original_lcs = float_value(row.get("lcs_window_ratio"))
        rows.append(
            {
                **base,
                "refined_category": refined["category"],
                "refined_start_index": target_start,
                "refined_end_index": target_end,
                "refined_start_addr": target_addresses[target_start] if 0 <= target_start < len(target_addresses) else "",
                "refined_end_addr": target_addresses[target_end] if 0 <= target_end < len(target_addresses) else "",
                "target_start_delta": int_value(refined["target_start_delta"]),
                "compiled_skip": int_value(refined["compiled_skip"]),
                "compiled_slice_instruction_count": compiled_slice_count,
                "refined_window_instruction_count": int_value(compare.get("target_instruction_count")),
                "matching_prefix": int_value(compare.get("matching_prefix")),
                "matching_suffix": int_value(compare.get("matching_suffix")),
                "lcs_instruction_count": lcs_instruction_count,
                "refined_lcs_window_ratio": refined_lcs,
                "refined_lcs_compiled_ratio": (
                    round(lcs_instruction_count / compiled_slice_count, 4)
                    if compiled_slice_count
                    else 0.0
                ),
                "lcs_window_ratio_delta": round(refined_lcs - original_lcs, 4),
                "longest_common_run": run_text(compare),
                "first_difference": first_difference_text(compare),
                "exact_match": bool(compare.get("exact_match")),
            }
        )

    rows.sort(
        key=lambda item: (
            category_order(str(item.get("refined_category", ""))),
            -float_value(item.get("refined_lcs_window_ratio")),
            -float_value(item.get("lcs_window_ratio_delta")),
            str(item.get("entry", "")),
        )
    )
    counts = Counter(str(row.get("refined_category", "")) for row in rows)
    near_categories = {"exact-window", "codegen-near-window", "structural-near-window"}
    compared_categories = near_categories | {"semantic-window", "weak-window"}
    compared_rows = [row for row in rows if str(row.get("refined_category", "")) in compared_categories]
    improved_rows = [row for row in compared_rows if float_value(row.get("lcs_window_ratio_delta")) > 0.0]
    summary = {
        "windows": len(rows),
        "compared_windows": len(compared_rows),
        "improved_windows": len(improved_rows),
        "near_or_exact_windows": sum(1 for row in rows if str(row.get("refined_category", "")) in near_categories),
        "exact_windows": counts["exact-window"],
        "codegen_near_windows": counts["codegen-near-window"],
        "structural_near_windows": counts["structural-near-window"],
        "semantic_windows": counts["semantic-window"],
        "weak_windows": counts["weak-window"],
        "best_entry": rows[0].get("entry", "") if rows else "",
        "best_candidate": rows[0].get("candidate_symbol", "") if rows else "",
        "best_category": rows[0].get("refined_category", "") if rows else "",
        "best_lcs_window_ratio": rows[0].get("refined_lcs_window_ratio", 0.0) if rows else 0.0,
        "best_lcs_delta": rows[0].get("lcs_window_ratio_delta", 0.0) if rows else 0.0,
        "categories": dict(sorted(counts.items())),
        "search_radius": args.search_radius,
        "max_compiled_skip": args.max_compiled_skip,
        "min_slice_insns": args.min_slice_insns,
        "next_gate": "Promote only near/exact refined windows; otherwise use refined offsets as adapter/boundary evidence.",
    }
    return {
        "format": "oot3d_direct_target_window_refinement_v1",
        "summary": summary,
        "rows": rows,
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct Target Window Refinement",
        "",
        "This report searches local target-window shifts and compiled-helper prologue skips around each inferred anchor.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Windows | {summary['windows']} |",
        f"| Compared windows | {summary['compared_windows']} |",
        f"| Improved windows | {summary['improved_windows']} |",
        f"| Near/exact windows | {summary['near_or_exact_windows']} |",
        f"| Semantic windows | {summary['semantic_windows']} |",
        f"| Weak windows | {summary['weak_windows']} |",
        f"| Search radius | {summary['search_radius']} |",
        f"| Max compiled skip | {summary['max_compiled_skip']} |",
        "",
        "## Windows",
        "",
        "| Category | OOT3D | Candidate | Refined window | Shift/skip | LCS old/new | Run | First difference | Source |",
        "| --- | --- | --- | --- | ---: | ---: | --- | --- | --- |",
    ]
    for row in data["rows"]:
        lines.append(
            f"| `{row.get('refined_category', '')}` | `{row['entry']}` `{row['oot3d_name']}` | "
            f"`{row['candidate_symbol']}` | `{row.get('refined_start_addr', '')}`-`{row.get('refined_end_addr', '')}` | "
            f"{row.get('target_start_delta', '')}/{row.get('compiled_skip', '')} | "
            f"{float_value(row.get('original_lcs_window_ratio')):.4f}/{float_value(row.get('refined_lcs_window_ratio')):.4f} "
            f"({float_value(row.get('lcs_window_ratio_delta')):+.4f}) | "
            f"{row.get('longest_common_run', '')} | `{row.get('first_difference', '')}` | `{row.get('source', '')}` |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--window-match", type=Path, default=DEFAULT_WINDOW_MATCH)
    parser.add_argument("--envelope-plan", type=Path, default=DEFAULT_ENVELOPE_PLAN)
    parser.add_argument("--gap-report", type=Path, default=DEFAULT_GAP_REPORT)
    parser.add_argument("--target-disassembly", type=Path, default=DEFAULT_TARGET_DISASSEMBLY)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    parser.add_argument("--search-radius", type=int, default=24)
    parser.add_argument("--max-compiled-skip", type=int, default=24)
    parser.add_argument("--min-slice-insns", type=int, default=16)
    args = parser.parse_args()

    data = build_rows(args)
    fields = [
        "refined_category",
        "entry",
        "oot3d_name",
        "cluster_index",
        "candidate_symbol",
        "original_category",
        "original_lcs_window_ratio",
        "refined_lcs_window_ratio",
        "lcs_window_ratio_delta",
        "original_window_start_addr",
        "original_window_end_addr",
        "refined_start_addr",
        "refined_end_addr",
        "target_start_delta",
        "compiled_skip",
        "original_window_instruction_count",
        "refined_window_instruction_count",
        "compiled_instruction_count",
        "compiled_slice_instruction_count",
        "matching_prefix",
        "matching_suffix",
        "lcs_instruction_count",
        "refined_lcs_compiled_ratio",
        "longest_common_run",
        "first_difference",
        "exact_match",
        "priority",
        "source",
        "compiled_dump",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"], fields)
    write_markdown(args.out_md, data)
    summary = data["summary"]
    print(
        "direct target window refinement: "
        f"{summary['improved_windows']}/{summary['windows']} improved, "
        f"{summary['near_or_exact_windows']} near/exact, "
        f"best {summary['best_entry']} {summary['best_candidate']} {summary['best_lcs_window_ratio']}"
    )
    print(str(args.out_md.relative_to(ROOT)).replace("\\", "/"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
