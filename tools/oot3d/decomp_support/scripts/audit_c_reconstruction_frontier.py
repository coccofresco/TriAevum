#!/usr/bin/env python3
"""Build the real C reconstruction frontier.

This report is stricter than the structured match gate: exact asm seeds are
still treated as work to do even after moving from naked asm to full inline asm,
while a materialized N64-derived packet that only compiles is counted as
compilable evidence, not as matched C.
"""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_READINESS = ROOT / "analysis" / "c_conversion_readiness.csv"
DEFAULT_PROBE_MATCH = ROOT / "analysis" / "direct_packet_probe_match.csv"
DEFAULT_TEMPLATE_PROBE_MATCH = ROOT / "analysis" / "direct_template_probe_match.csv"
DEFAULT_IN_SOURCE_C_PROBE = ROOT / "analysis" / "in_source_c_reconstruction_probe.csv"
DEFAULT_SYMBOL_SCAN = ROOT / "analysis" / "direct_packet_symbol_scan.csv"
DEFAULT_SPLIT_WORKORDERS = ROOT / "analysis" / "direct_split_workorders.csv"
DEFAULT_OUT_JSON = ROOT / "analysis" / "c_reconstruction_frontier.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "c_reconstruction_frontier.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "c_reconstruction_frontier.md"


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


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


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def by_entry(rows: list[dict[str, str]]) -> dict[str, dict[str, str]]:
    return {str(row.get("entry") or row.get("oot3d_entry", "")).lower(): row for row in rows}


def by_function(rows: list[dict[str, str]]) -> dict[str, dict[str, str]]:
    return {str(row.get("function", "")): row for row in rows if row.get("function")}


def best_probe_match(*matches: dict[str, str]) -> dict[str, str]:
    order = {
        "exact-c": 0,
        "codegen-near": 1,
        "structural-near": 2,
        "semantic-started": 3,
        "semantic-gap": 4,
        "compiled-symbol-missing": 5,
        "target-missing": 6,
        "dump-missing": 7,
        "compile-blocked": 8,
    }
    candidates = [match for match in matches if match]
    if not candidates:
        return {}
    return sorted(candidates, key=lambda row: order.get(str(row.get("category", "")), 99))[0]


def in_source_probe_match(row: dict[str, str]) -> dict[str, str]:
    if not row:
        return {}
    return {
        "category": row.get("category", ""),
        "compiled_probe": "True" if int_value(row.get("returncode")) == 0 else "False",
        "lcs_target_ratio": row.get("lcs_target_ratio", ""),
        "first_difference": row.get("first_difference", ""),
        "packet": row.get("variant_source", ""),
        "probe_source": row.get("variant_source", ""),
        "probe_origin": "in-source-c",
        "define": row.get("define", ""),
        "variant_style": row.get("variant_style", ""),
        "compile_primary_blocker": row.get("first_difference", ""),
    }


def split_index(rows: list[dict[str, str]]) -> dict[str, dict[str, Any]]:
    grouped: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        entry = str(row.get("entry", "")).lower()
        if entry:
            grouped[entry].append(row)

    index: dict[str, dict[str, Any]] = {}
    for entry, entry_rows in grouped.items():
        complete = [row for row in entry_rows if row.get("source_span_status") == "complete"]
        best = sorted(entry_rows, key=lambda row: -int_value(row.get("priority")))[0]
        index[entry] = {
            "split_workorders": len(entry_rows),
            "complete_split_spans": len(complete),
            "best_split_priority": int_value(best.get("priority")),
            "best_split_reason": best.get("reason", ""),
            "best_split_symbol": best.get("candidate_symbol", ""),
            "best_split_source": best.get("source", ""),
        }
    return index


def probe_status(match: dict[str, str], materialized_status: str) -> str:
    if match.get("category") in {"exact-c", "exact"}:
        return "exact"
    if match.get("category") in {"codegen-near", "structural-near"}:
        return "near"
    if match.get("category") in {"semantic-started", "semantic-gap"}:
        return "started"
    if match:
        return "blocked"
    if materialized_status == "ok":
        return "materialized-uncompared"
    return "missing"


def frontier_status(
    row: dict[str, str],
    match: dict[str, str],
    split: dict[str, Any],
) -> tuple[str, str]:
    conversion = row.get("conversion_status", "")
    style = row.get("implementation_style", "")
    materialized = row.get("materialized_status", "")
    probe = probe_status(match, materialized)
    in_source_c = match.get("probe_origin") == "in-source-c"

    if conversion == "promote-now":
        return "ready-to-promote-c", "Compiled C is exact but not yet in the matched source list."
    if conversion == "matched-c":
        return "matched-real-c", "Already exact as compiler-shaped C."
    if conversion == "matched-c-inline-asm":
        return "matched-c-inline-asm", "Already exact, but still depends on inline asm constraints."
    if conversion == "batch-lowering-candidate":
        if style == "plain-c":
            return "structured-c-needs-lowering", "Maintained C exists; batch direct-offset/source-shape lowering is the next step."
        return "inline-c-needs-lowering", "Maintained C exists but still uses inline asm; lower shape/prologue constraints next."
    if conversion in {"matched-exact-seed-asm", "matched-inline-asm-seed"}:
        if probe == "exact":
            if in_source_c:
                return (
                    "in-source-c-ready-to-promote",
                    "A guarded in-source C reconstruction compiled exact; replace the exact seed after a guarded build.",
                )
            return "compiled-packet-ready-to-promote", "A materialized N64-derived packet compiled exact; replace the exact seed after a guarded build."
        if probe == "near":
            if in_source_c:
                return (
                    "in-source-c-near-seed",
                    "A guarded in-source C reconstruction compiles and is near; attack codegen/source shape before replacing the seed.",
                )
            return "compiled-packet-near-seed", "A materialized packet compiles and is near; attack codegen/source shape before replacing the seed."
        if probe == "started":
            if in_source_c:
                return (
                    "in-source-c-started-seed",
                    "A guarded in-source C reconstruction compiles but is not close enough; fix ABI/frame shape before replacing the seed.",
                )
            if int_value(split.get("complete_split_spans")) > 0:
                return "compiled-packet-split-seed", "A compiled packet and source split spans exist; reconstruct the split/helper C before replacing the seed."
            return "compiled-packet-started-seed", "A materialized packet compiles but is not close enough; finish adapters or slicing."
        if probe == "blocked":
            if in_source_c:
                return "in-source-c-compile-blocked", "A guarded in-source C reconstruction exists, but compile or compare is blocked."
            return "materialized-packet-compile-blocked", "A materialized packet exists, but compile or compare is blocked."
        if materialized == "ok":
            return "materialized-packet-needs-compare", "A materialized packet exists; run/refresh compile and target comparison."
        return "seed-needs-materialized-c", "No materialized C packet exists yet for this exact seed."
    return "not-on-c-frontier", "Keep in the wider analysis queue."


def next_action(frontier: str, row: dict[str, str], split: dict[str, Any], match: dict[str, str]) -> str:
    if frontier == "in-source-c-ready-to-promote":
        return "Promote the guarded in-source C reconstruction, rebuild matched objects, and disable the exact asm seed only after exact compare stays green."
    if frontier in {"ready-to-promote-c", "compiled-packet-ready-to-promote"}:
        return "Promote the C source, rebuild matched objects, and delete/disable the exact asm seed only after exact compare stays green."
    if frontier == "structured-c-needs-lowering":
        return "Apply the shared direct-offset adapters/shape anchors to the maintained source and rerun the structured gate."
    if frontier == "inline-c-needs-lowering":
        return "Reduce inline asm constraints while preserving the exact baseline, then rerun the structured gate."
    if frontier == "compiled-packet-near-seed":
        return "Run source-shape/profile search on the materialized packet and compare before touching the seed."
    if frontier == "in-source-c-near-seed":
        return "Use the guarded in-source C reconstruction as the baseline, then tune codegen/source shape before disabling the exact seed."
    if frontier == "compiled-packet-split-seed":
        return f"Start from split candidate `{split.get('best_split_symbol', '')}` at `{split.get('best_split_source', '')}`."
    if frontier == "compiled-packet-started-seed":
        category = match.get("category", "")
        return f"Use the compiled packet as the C baseline, then fix adapters/slicing; current probe category is `{category}`."
    if frontier == "in-source-c-started-seed":
        category = match.get("category", "")
        return f"Keep the guarded in-source C reconstruction as the C baseline, then fix ABI/frame shape; current probe category is `{category}`."
    if frontier == "materialized-packet-needs-compare":
        return "Run probe_direct_packet_compilability.py and compare_direct_packet_probe_matches.py for this packet."
    if frontier == "materialized-packet-compile-blocked":
        blocker = match.get("compile_primary_blocker", "") or match.get("category", "")
        return f"Fix the materialized packet compile/compare blocker `{blocker}`, then rerun the probe gate."
    if frontier == "in-source-c-compile-blocked":
        blocker = match.get("compile_primary_blocker", "") or match.get("category", "")
        return f"Fix the guarded in-source C reconstruction blocker `{blocker}`, then rerun the in-source probe."
    if frontier == "seed-needs-materialized-c":
        return "Add adapter coverage/materialize a direct packet from the N64 extract before attempting source replacement."
    return row.get("next_action", "")


def build_rows(args: argparse.Namespace) -> list[dict[str, Any]]:
    probe_matches = by_entry(read_csv(args.probe_match))
    template_probe_matches = by_entry(read_csv(args.template_probe_match))
    in_source_probes = by_function(read_csv(args.in_source_c_probe))
    symbol_scans = by_entry(read_csv(args.symbol_scan))
    splits = split_index(read_csv(args.split_workorders))

    rows: list[dict[str, Any]] = []
    for source_row in read_csv(args.readiness):
        entry = str(source_row.get("oot3d_entry", "")).lower()
        match = best_probe_match(probe_matches.get(entry, {}), template_probe_matches.get(entry, {}))
        if not match:
            match = in_source_probe_match(in_source_probes.get(source_row.get("oot3d_name", ""), {}))
        scan = symbol_scans.get(entry, {})
        split = splits.get(entry, {})
        frontier, reason = frontier_status(source_row, match, split)
        target_insns = int_value(source_row.get("target_instruction_count"))
        in_source_c = match.get("probe_origin") == "in-source-c"
        row = {
            "frontier_status": frontier,
            "entry": entry,
            "oot3d_name": source_row.get("oot3d_name", ""),
            "n64_name": source_row.get("n64_name", ""),
            "port_file": source_row.get("port_file", ""),
            "conversion_status": source_row.get("conversion_status", ""),
            "implementation_style": source_row.get("implementation_style", ""),
            "matched_baseline_exact": source_row.get("matched_baseline_exact", ""),
            "target_instruction_count": target_insns,
            "materialized_status": "in-source-c"
            if in_source_c
            else source_row.get("materialized_status", "") or ("ok" if match else ""),
            "materialized_output": source_row.get("materialized_output", "") or match.get("packet", ""),
            "probe_category": match.get("category", ""),
            "probe_source": match.get("probe_source", ""),
            "probe_origin": match.get("probe_origin", "materialized-packet" if match else ""),
            "probe_compiled": match.get("compiled_probe", ""),
            "probe_lcs_ratio": match.get("lcs_target_ratio", ""),
            "probe_first_difference": match.get("first_difference", ""),
            "in_source_define": match.get("define", "") if in_source_c else "",
            "best_symbol": scan.get("best_symbol", ""),
            "best_symbol_is_mapped": scan.get("best_symbol_is_mapped", ""),
            "best_symbol_category": scan.get("scan_category", ""),
            "split_workorders": int_value(split.get("split_workorders")),
            "complete_split_spans": int_value(split.get("complete_split_spans")),
            "best_split_symbol": split.get("best_split_symbol", ""),
            "best_split_source": split.get("best_split_source", ""),
            "frontier_reason": reason,
            "next_action": next_action(frontier, source_row, split, match),
        }
        rows.append(row)

    rows.sort(key=sort_key)
    return rows


def sort_key(row: dict[str, Any]) -> tuple[int, int, str]:
    order = {
        "ready-to-promote-c": 0,
        "compiled-packet-ready-to-promote": 1,
        "in-source-c-ready-to-promote": 2,
        "structured-c-needs-lowering": 3,
        "inline-c-needs-lowering": 4,
        "compiled-packet-near-seed": 5,
        "in-source-c-near-seed": 6,
        "compiled-packet-split-seed": 7,
        "compiled-packet-started-seed": 8,
        "in-source-c-started-seed": 9,
        "materialized-packet-compile-blocked": 10,
        "in-source-c-compile-blocked": 11,
        "materialized-packet-needs-compare": 12,
        "seed-needs-materialized-c": 13,
        "matched-real-c": 14,
        "matched-c-inline-asm": 15,
        "not-on-c-frontier": 16,
    }
    return (
        order.get(str(row.get("frontier_status", "")), 99),
        -int_value(row.get("target_instruction_count")),
        str(row.get("entry", "")),
    )


def file_batches(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    grouped: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        grouped[str(row.get("port_file", ""))].append(row)

    batches: list[dict[str, Any]] = []
    for port_file, file_rows in grouped.items():
        statuses = Counter(str(row.get("frontier_status", "")) for row in file_rows)
        open_rows = [
            row
            for row in file_rows
            if row.get("frontier_status") not in {"matched-real-c", "matched-c-inline-asm", "not-on-c-frontier"}
        ]
        compiled_packets = sum(1 for row in file_rows if row.get("probe_category"))
        in_source_c_probes = sum(1 for row in file_rows if row.get("probe_origin") == "in-source-c")
        compiled_packets -= in_source_c_probes
        complete_splits = sum(int_value(row.get("complete_split_spans")) for row in file_rows)
        if statuses["structured-c-needs-lowering"] or statuses["inline-c-needs-lowering"]:
            action = "lower maintained structured C"
        elif complete_splits:
            action = "reconstruct split/helper C"
        elif statuses["materialized-packet-compile-blocked"]:
            action = "fix materialized packet blockers"
        elif compiled_packets:
            action = "replace exact seeds from compiled packets"
        elif in_source_c_probes:
            action = "shape guarded in-source C probes"
        else:
            action = "materialize missing C packets"
        batches.append(
            {
                "port_file": port_file,
                "functions": len(file_rows),
                "open_functions": len(open_rows),
                "target_instructions": sum(int_value(row.get("target_instruction_count")) for row in file_rows),
                "open_target_instructions": sum(int_value(row.get("target_instruction_count")) for row in open_rows),
                "compiled_packets": compiled_packets,
                "in_source_c_probes": in_source_c_probes,
                "complete_split_spans": complete_splits,
                "frontier_statuses": dict(sorted(statuses.items())),
                "recommended_batch": action,
            }
        )

    batches.sort(key=lambda row: (-int_value(row["open_target_instructions"]), row["port_file"]))
    return batches


def summarize(rows: list[dict[str, Any]], batches: list[dict[str, Any]]) -> dict[str, Any]:
    statuses = Counter(str(row.get("frontier_status", "")) for row in rows)
    probe_categories = Counter(str(row.get("probe_category", "")) for row in rows if row.get("probe_category"))
    in_source_statuses = {
        "in-source-c-ready-to-promote",
        "in-source-c-near-seed",
        "in-source-c-started-seed",
        "in-source-c-compile-blocked",
    }
    return {
        "functions": len(rows),
        "files": len({row.get("port_file", "") for row in rows if row.get("port_file")}),
        "open_functions": sum(
            1
            for row in rows
            if row.get("frontier_status") not in {"matched-real-c", "matched-c-inline-asm", "not-on-c-frontier"}
        ),
        "open_target_instructions": sum(
            int_value(row.get("target_instruction_count"))
            for row in rows
            if row.get("frontier_status") not in {"matched-real-c", "matched-c-inline-asm", "not-on-c-frontier"}
        ),
        "ready_to_promote_functions": statuses["ready-to-promote-c"]
        + statuses["compiled-packet-ready-to-promote"]
        + statuses["in-source-c-ready-to-promote"],
        "matched_real_c_functions": statuses["matched-real-c"],
        "matched_inline_c_functions": statuses["matched-c-inline-asm"],
        "structured_c_batch_functions": statuses["structured-c-needs-lowering"]
        + statuses["inline-c-needs-lowering"],
        "compiled_packet_seed_functions": statuses["compiled-packet-near-seed"]
        + statuses["compiled-packet-split-seed"]
        + statuses["compiled-packet-started-seed"],
        "in_source_c_seed_functions": sum(statuses[status] for status in in_source_statuses),
        "compile_blocked_seed_functions": statuses["materialized-packet-compile-blocked"],
        "seed_needs_materialized_c_functions": statuses["seed-needs-materialized-c"],
        "complete_split_spans": sum(int_value(row.get("complete_split_spans")) for row in rows),
        "status_counts": dict(sorted(statuses.items())),
        "probe_category_counts": dict(sorted(probe_categories.items())),
        "file_batches": len(batches),
    }


def write_csv_report(path: Path, rows: list[dict[str, Any]]) -> None:
    fieldnames = [
        "frontier_status",
        "entry",
        "oot3d_name",
        "n64_name",
        "port_file",
        "conversion_status",
        "implementation_style",
        "matched_baseline_exact",
        "target_instruction_count",
        "materialized_status",
        "materialized_output",
        "probe_category",
        "probe_source",
        "probe_origin",
        "probe_compiled",
        "probe_lcs_ratio",
        "probe_first_difference",
        "in_source_define",
        "best_symbol",
        "best_symbol_is_mapped",
        "best_symbol_category",
        "split_workorders",
        "complete_split_spans",
        "best_split_symbol",
        "best_split_source",
        "frontier_reason",
        "next_action",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fieldnames})


def write_md(path: Path, rows: list[dict[str, Any]], batches: list[dict[str, Any]], summary: dict[str, Any]) -> None:
    status_text = ", ".join(f"{key}: {value}" for key, value in summary["status_counts"].items()) or "none"
    probe_text = ", ".join(f"{key}: {value}" for key, value in summary["probe_category_counts"].items()) or "none"
    lines = [
        "# C Reconstruction Frontier",
        "",
        "This report tracks real C reconstruction work. Exact asm seeds, whether naked or full inline asm, are treated as open work; compiled direct packets are treated as evidence until they match the target.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Functions tracked | {summary['functions']} |",
        f"| Files tracked | {summary['files']} |",
        f"| Open C-reconstruction functions | {summary['open_functions']} |",
        f"| Open target instructions | {summary['open_target_instructions']} |",
        f"| Ready-to-promote C functions | {summary['ready_to_promote_functions']} |",
        f"| Matched real C functions | {summary['matched_real_c_functions']} |",
        f"| Matched inline-C functions | {summary['matched_inline_c_functions']} |",
        f"| Structured C batch functions | {summary['structured_c_batch_functions']} |",
        f"| Exact-seed functions with compiled packets | {summary['compiled_packet_seed_functions']} |",
        f"| Exact-seed functions with in-source C probes | {summary['in_source_c_seed_functions']} |",
        f"| Exact-seed functions with compile-blocked packets | {summary['compile_blocked_seed_functions']} |",
        f"| Exact-seed functions needing materialized C | {summary['seed_needs_materialized_c_functions']} |",
        f"| Complete split/helper source spans | {summary['complete_split_spans']} |",
        "",
        f"- Frontier statuses: {status_text}",
        f"- Probe categories: {probe_text}",
        "",
        "## File Batch Queue",
        "",
        "| File | Open funcs | Open insns | Compiled packets | In-source C | Complete split spans | Recommended batch | Statuses |",
        "| --- | ---: | ---: | ---: | ---: | ---: | --- | --- |",
    ]
    for batch in batches:
        if int_value(batch.get("open_functions")) == 0:
            continue
        statuses = ", ".join(f"{key}:{value}" for key, value in batch["frontier_statuses"].items())
        lines.append(
            f"| `{batch['port_file']}` | {batch['open_functions']} | {batch['open_target_instructions']} | "
            f"{batch['compiled_packets']} | {batch['in_source_c_probes']} | {batch['complete_split_spans']} | "
            f"{batch['recommended_batch']} | `{statuses}` |"
        )

    lines.extend(
        [
            "",
            "## Function Frontier",
            "",
            "| Status | OOT3D | File | Style | Target | Probe | Source | Split | Next action |",
            "| --- | --- | --- | --- | ---: | --- | --- | --- | --- |",
        ]
    )
    for row in rows:
        probe = row.get("probe_category") or "-"
        probe_source = row.get("probe_source") or "-"
        split = "-"
        if int_value(row.get("complete_split_spans")):
            split = f"{row.get('complete_split_spans')} complete, best `{row.get('best_split_symbol', '')}`"
        lines.append(
            f"| `{row['frontier_status']}` | `{row['entry']}` `{row['oot3d_name']}` | `{row['port_file']}` | "
            f"`{row['implementation_style']}` | {row['target_instruction_count']} | `{probe}` | "
            f"`{probe_source}` | {split} | {row['next_action']} |"
        )

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--readiness", type=Path, default=DEFAULT_READINESS)
    parser.add_argument("--probe-match", type=Path, default=DEFAULT_PROBE_MATCH)
    parser.add_argument("--template-probe-match", type=Path, default=DEFAULT_TEMPLATE_PROBE_MATCH)
    parser.add_argument("--in-source-c-probe", type=Path, default=DEFAULT_IN_SOURCE_C_PROBE)
    parser.add_argument("--symbol-scan", type=Path, default=DEFAULT_SYMBOL_SCAN)
    parser.add_argument("--split-workorders", type=Path, default=DEFAULT_SPLIT_WORKORDERS)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    args = parser.parse_args()

    rows = build_rows(args)
    batches = file_batches(rows)
    summary = summarize(rows, batches)
    write_json(args.out_json, {"summary": summary, "file_batches": batches, "rows": rows})
    write_csv_report(args.out_csv, rows)
    write_md(args.out_md, rows, batches, summary)
    print(
        "c reconstruction frontier: "
        f"{summary['open_functions']} open functions, "
        f"{summary['ready_to_promote_functions']} ready to promote, "
        f"{summary['structured_c_batch_functions']} structured C batch functions, "
        f"{summary['compiled_packet_seed_functions']} exact seeds with compiled packets, "
        f"{summary['in_source_c_seed_functions']} exact seeds with in-source C probes"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
