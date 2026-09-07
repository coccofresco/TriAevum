#!/usr/bin/env python3
"""Refine focused adapter probe windows that start in data-like target runs."""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import Counter
from pathlib import Path
from typing import Any

from analyze_direct_window_adapter_probe_diagnostics import count_leading_unusual
from compare_direct_target_window_probes import (
    category_order,
    classify_window,
    first_difference_text,
    run_text,
)
from compare_runtime_objects import compare_ops


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DIAGNOSTICS = ROOT / "analysis" / "direct_window_adapter_probe_diagnostics.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_data_like_boundary_refinement.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_data_like_boundary_refinement.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_data_like_boundary_refinement.md"
LINE_RE = re.compile(r"^\s*(?:(?P<index>\d+)\s+)?(?P<addr>[0-9a-fA-F]+):\s+(?P<op>.+?)\s*$")


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


def read_indexed_ops(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    if not path.is_file():
        return rows
    for ordinal, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines()):
        match = LINE_RE.match(line)
        if not match:
            continue
        rows.append(
            {
                "ordinal": ordinal,
                "index": int_value(match.group("index")),
                "addr": match.group("addr").lower(),
                "op": match.group("op").strip(),
            }
        )
    return rows


def compare_score(compare: dict[str, Any], target_skip: int, compiled_extra_skip: int) -> tuple[int, float, int, int, int, int]:
    category = classify_window(compare)
    run = compare.get("longest_common_run", {})
    run_length = int_value(run.get("length")) if isinstance(run, dict) else 0
    return (
        -category_order(category),
        float_value(compare.get("lcs_target_ratio")),
        run_length,
        int_value(compare.get("matching_prefix")),
        int_value(compare.get("matching_suffix")),
        -(target_skip + compiled_extra_skip),
    )


def build_candidates(
    target_rows: list[dict[str, Any]],
    compiled_rows: list[dict[str, Any]],
    *,
    leading_unusual: int,
    search_after_leading: int,
    max_compiled_extra_skip: int,
    min_slice_insns: int,
) -> list[dict[str, Any]]:
    candidates: list[dict[str, Any]] = []
    target_ops = [str(row["op"]) for row in target_rows]
    compiled_ops = [str(row["op"]) for row in compiled_rows]
    if len(target_ops) < min_slice_insns or len(compiled_ops) < min_slice_insns:
        return candidates

    target_skips = {0}
    first_code_skip = max(0, leading_unusual)
    for target_skip in range(first_code_skip, min(len(target_ops) - min_slice_insns + 1, first_code_skip + search_after_leading + 1)):
        target_skips.add(target_skip)

    for target_skip in sorted(target_skips):
        for compiled_extra_skip in range(0, min(max_compiled_extra_skip, len(compiled_ops) - min_slice_insns) + 1):
            available = min(len(target_ops) - target_skip, len(compiled_ops) - compiled_extra_skip)
            if available < min_slice_insns:
                continue
            target_slice = target_ops[target_skip : target_skip + available]
            compiled_slice = compiled_ops[compiled_extra_skip : compiled_extra_skip + available]
            compare = compare_ops(target_slice, compiled_slice)
            target_start = target_rows[target_skip]
            target_end = target_rows[target_skip + available - 1]
            lcs_instruction_count = int_value(compare.get("lcs_instruction_count"))
            compiled_count = int_value(compare.get("compiled_instruction_count"))
            candidates.append(
                {
                    "target_skip": target_skip,
                    "compiled_extra_skip": compiled_extra_skip,
                    "target_start_addr": target_start["addr"],
                    "target_end_addr": target_end["addr"],
                    "target_start_index": target_start["index"],
                    "target_end_index": target_end["index"],
                    "slice_instruction_count": available,
                    "category": classify_window(compare),
                    "matching_prefix": int_value(compare.get("matching_prefix")),
                    "matching_suffix": int_value(compare.get("matching_suffix")),
                    "lcs_instruction_count": lcs_instruction_count,
                    "lcs_window_ratio": float_value(compare.get("lcs_target_ratio")),
                    "lcs_compiled_ratio": round(lcs_instruction_count / compiled_count, 4) if compiled_count else 0.0,
                    "longest_common_run": run_text(compare),
                    "first_difference": first_difference_text(compare),
                    "exact_match": bool(compare.get("exact_match")),
                    "is_boundary_correction": target_skip >= first_code_skip and target_skip > 0,
                }
            )
    return candidates


def best_candidate(candidates: list[dict[str, Any]]) -> dict[str, Any] | None:
    best: dict[str, Any] | None = None
    best_score: tuple[int, float, int, int, int, int] | None = None
    for candidate in candidates:
        compare = {
            "target_instruction_count": candidate.get("slice_instruction_count", 0),
            "compiled_instruction_count": candidate.get("slice_instruction_count", 0),
            "lcs_target_ratio": candidate.get("lcs_window_ratio", 0.0),
            "matching_prefix": candidate.get("matching_prefix", 0),
            "matching_suffix": candidate.get("matching_suffix", 0),
            "longest_common_run": {"length": str(candidate.get("longest_common_run", "0@0/0")).split("@", 1)[0]},
            "exact_match": candidate.get("exact_match", False),
        }
        score = compare_score(compare, int_value(candidate.get("target_skip")), int_value(candidate.get("compiled_extra_skip")))
        if best_score is None or score > best_score:
            best_score = score
            best = candidate
    return best


def flatten_row(source: dict[str, Any], candidate: dict[str, Any] | None, baseline_lcs: float) -> dict[str, Any]:
    if candidate is None:
        return {
            "entry": source.get("entry", ""),
            "oot3d_name": source.get("oot3d_name", ""),
            "candidate_symbol": source.get("candidate_symbol", ""),
            "status": "no-valid-candidate",
            "original_lcs_window_ratio": baseline_lcs,
        }
    lcs = float_value(candidate.get("lcs_window_ratio"))
    near_categories = {"exact-window", "codegen-near-window", "structural-near-window"}
    return {
        "entry": source.get("entry", ""),
        "oot3d_name": source.get("oot3d_name", ""),
        "candidate_symbol": source.get("candidate_symbol", ""),
        "status": "promotion-candidate" if str(candidate.get("category", "")) in near_categories else "boundary-refined-blocked",
        "category": candidate.get("category", ""),
        "original_refined_window": source.get("refined_window", ""),
        "original_compiled_skip": int_value(source.get("compiled_skip")),
        "original_lcs_window_ratio": baseline_lcs,
        "target_skip": int_value(candidate.get("target_skip")),
        "compiled_extra_skip": int_value(candidate.get("compiled_extra_skip")),
        "effective_compiled_skip": int_value(source.get("compiled_skip")) + int_value(candidate.get("compiled_extra_skip")),
        "target_start_addr": candidate.get("target_start_addr", ""),
        "target_end_addr": candidate.get("target_end_addr", ""),
        "target_start_index": candidate.get("target_start_index", ""),
        "target_end_index": candidate.get("target_end_index", ""),
        "slice_instruction_count": int_value(candidate.get("slice_instruction_count")),
        "lcs_instruction_count": int_value(candidate.get("lcs_instruction_count")),
        "lcs_window_ratio": lcs,
        "lcs_compiled_ratio": float_value(candidate.get("lcs_compiled_ratio")),
        "lcs_window_ratio_delta": round(lcs - baseline_lcs, 4),
        "matching_prefix": int_value(candidate.get("matching_prefix")),
        "matching_suffix": int_value(candidate.get("matching_suffix")),
        "longest_common_run": candidate.get("longest_common_run", ""),
        "first_difference": candidate.get("first_difference", ""),
        "exact_match": bool(candidate.get("exact_match")),
        "probe_manifest": source.get("probe_manifest", ""),
        "source_excerpt": source.get("source_excerpt", ""),
        "next_gate": (
            "Candidate is near/exact; inspect source and target control flow before maintained-C promotion."
            if str(candidate.get("category", "")) in near_categories
            else "Boundary is past the data-like run but still blocked by semantic/call-shape differences."
        ),
    }


def build_report(args: argparse.Namespace) -> dict[str, Any]:
    diagnostics = read_json(args.diagnostics, {})
    rows: list[dict[str, Any]] = []
    candidate_rows: list[dict[str, Any]] = []
    for source in list_value(diagnostics.get("rows", [])):
        if not isinstance(source, dict):
            continue
        if source.get("diagnostic_class") != "target-window-starts-in-data-like-run":
            continue
        manifest = read_json(repo_path(source.get("probe_manifest", "")), {})
        target_path = repo_path(manifest.get("target_window_ops", source.get("target_window_ops", "")))
        compiled_path = repo_path(manifest.get("compiled_helper_body_ops", source.get("compiled_helper_body_ops", "")))
        target_rows = read_indexed_ops(target_path)
        compiled_rows = read_indexed_ops(compiled_path)
        leading_unusual = int_value(source.get("leading_unusual_target_ops")) or count_leading_unusual(
            [str(row["op"]) for row in target_rows]
        )
        candidates = build_candidates(
            target_rows,
            compiled_rows,
            leading_unusual=leading_unusual,
            search_after_leading=args.search_after_leading,
            max_compiled_extra_skip=args.max_compiled_extra_skip,
            min_slice_insns=args.min_slice_insns,
        )
        base_lcs = float_value(source.get("lcs_window_ratio"))
        best = best_candidate(candidates)
        rows.append(
            {
                **flatten_row(source, best, base_lcs),
                "leading_unusual_target_ops": leading_unusual,
                "candidate_count": len(candidates),
                "corrected_candidate_count": sum(1 for candidate in candidates if candidate.get("is_boundary_correction")),
            }
        )
        for candidate in candidates:
            candidate_rows.append(
                {
                    "entry": source.get("entry", ""),
                    "candidate_symbol": source.get("candidate_symbol", ""),
                    "category": candidate.get("category", ""),
                    "target_skip": candidate.get("target_skip", 0),
                    "compiled_extra_skip": candidate.get("compiled_extra_skip", 0),
                    "target_start_addr": candidate.get("target_start_addr", ""),
                    "target_end_addr": candidate.get("target_end_addr", ""),
                    "slice_instruction_count": candidate.get("slice_instruction_count", 0),
                    "lcs_window_ratio": candidate.get("lcs_window_ratio", 0.0),
                    "lcs_window_ratio_delta": round(float_value(candidate.get("lcs_window_ratio")) - base_lcs, 4),
                    "longest_common_run": candidate.get("longest_common_run", ""),
                    "first_difference": candidate.get("first_difference", ""),
                }
            )

    rows.sort(
        key=lambda row: (
            category_order(str(row.get("category", ""))),
            -float_value(row.get("lcs_window_ratio")),
            -float_value(row.get("lcs_window_ratio_delta")),
            str(row.get("entry", "")),
        )
    )
    candidate_rows.sort(
        key=lambda row: (
            str(row.get("entry", "")),
            str(row.get("candidate_symbol", "")),
            category_order(str(row.get("category", ""))),
            -float_value(row.get("lcs_window_ratio")),
        )
    )
    counts = Counter(str(row.get("category", "")) for row in rows)
    near_categories = {"exact-window", "codegen-near-window", "structural-near-window"}
    improved_rows = [row for row in rows if float_value(row.get("lcs_window_ratio_delta")) > 0.0]
    summary = {
        "packets": len(rows),
        "data_like_packets": len(rows),
        "candidate_rows": len(candidate_rows),
        "corrected_candidates": sum(int_value(row.get("corrected_candidate_count")) for row in rows),
        "improved_packets": len(improved_rows),
        "near_or_exact_packets": sum(1 for row in rows if str(row.get("category", "")) in near_categories),
        "categories": dict(sorted(counts.items())),
        "best_entry": rows[0].get("entry", "") if rows else "",
        "best_candidate_symbol": rows[0].get("candidate_symbol", "") if rows else "",
        "best_category": rows[0].get("category", "") if rows else "",
        "best_start_addr": rows[0].get("target_start_addr", "") if rows else "",
        "best_lcs_window_ratio": rows[0].get("lcs_window_ratio", 0.0) if rows else 0.0,
        "best_lcs_delta": rows[0].get("lcs_window_ratio_delta", 0.0) if rows else 0.0,
        "search_after_leading": args.search_after_leading,
        "max_compiled_extra_skip": args.max_compiled_extra_skip,
        "min_slice_insns": args.min_slice_insns,
        "next_gate": "Use near/exact corrected windows for source promotion; otherwise keep the packet blocked for semantic/call-shape analysis.",
    }
    return {
        "format": "oot3d_direct_data_like_boundary_refinement_v1",
        "summary": summary,
        "rows": rows,
        "candidate_rows": candidate_rows,
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct Data-Like Boundary Refinement",
        "",
        "This report moves focused target windows past decoded data/literal-like starts and retests the packet alignment.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Data-like packets | {summary['data_like_packets']} |",
        f"| Candidate rows | {summary['candidate_rows']} |",
        f"| Corrected candidates | {summary['corrected_candidates']} |",
        f"| Improved packets | {summary['improved_packets']} |",
        f"| Near/exact packets | {summary['near_or_exact_packets']} |",
        f"| Best LCS | {float_value(summary['best_lcs_window_ratio']):.4f} |",
        "",
        "## Best Corrections",
        "",
        "| Status | Category | OOT3D | Candidate | Window | Skip T/C | LCS old/new | Run | Next gate |",
        "| --- | --- | --- | --- | --- | ---: | ---: | --- | --- |",
    ]
    for row in data["rows"]:
        lines.append(
            f"| `{row.get('status', '')}` | `{row.get('category', '')}` | `{row.get('entry', '')}` `{row.get('oot3d_name', '')}` | "
            f"`{row.get('candidate_symbol', '')}` | `{row.get('target_start_addr', '')}`-`{row.get('target_end_addr', '')}` | "
            f"{row.get('target_skip', '')}/{row.get('compiled_extra_skip', '')} | "
            f"{float_value(row.get('original_lcs_window_ratio')):.4f}/{float_value(row.get('lcs_window_ratio')):.4f} "
            f"({float_value(row.get('lcs_window_ratio_delta')):+.4f}) | "
            f"{row.get('longest_common_run', '')} | {row.get('next_gate', '')} |"
        )
    lines.extend(["", "## Candidate Starts", ""])
    for candidate in data["candidate_rows"][:25]:
        lines.append(
            f"- `{candidate.get('entry', '')}` `{candidate.get('candidate_symbol', '')}` "
            f"`{candidate.get('category', '')}` start `{candidate.get('target_start_addr', '')}` "
            f"skip {candidate.get('target_skip', '')}/{candidate.get('compiled_extra_skip', '')} "
            f"LCS {float_value(candidate.get('lcs_window_ratio')):.4f} "
            f"({float_value(candidate.get('lcs_window_ratio_delta')):+.4f})"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--diagnostics", type=Path, default=DEFAULT_DIAGNOSTICS)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    parser.add_argument("--search-after-leading", type=int, default=24)
    parser.add_argument("--max-compiled-extra-skip", type=int, default=24)
    parser.add_argument("--min-slice-insns", type=int, default=16)
    args = parser.parse_args()

    data = build_report(args)
    fields = [
        "status",
        "category",
        "entry",
        "oot3d_name",
        "candidate_symbol",
        "original_refined_window",
        "original_compiled_skip",
        "original_lcs_window_ratio",
        "target_skip",
        "compiled_extra_skip",
        "effective_compiled_skip",
        "target_start_addr",
        "target_end_addr",
        "target_start_index",
        "target_end_index",
        "slice_instruction_count",
        "lcs_instruction_count",
        "lcs_window_ratio",
        "lcs_compiled_ratio",
        "lcs_window_ratio_delta",
        "matching_prefix",
        "matching_suffix",
        "longest_common_run",
        "first_difference",
        "leading_unusual_target_ops",
        "candidate_count",
        "corrected_candidate_count",
        "probe_manifest",
        "source_excerpt",
        "next_gate",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"], fields)
    write_markdown(args.out_md, data)
    summary = data["summary"]
    print(
        "direct data-like boundary refinement: "
        f"{summary['data_like_packets']} packets, "
        f"{summary['improved_packets']} improved, "
        f"{summary['near_or_exact_packets']} near/exact, "
        f"best {summary['best_entry']} {summary['best_candidate_symbol']} "
        f"{summary['best_start_addr']} {summary['best_lcs_window_ratio']}"
    )
    print(str(args.out_md.relative_to(ROOT)).replace("\\", "/"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
