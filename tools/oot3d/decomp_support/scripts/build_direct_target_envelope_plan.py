#!/usr/bin/env python3
"""Group internal direct-split anchors into OOT3D target envelope work items."""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SPLIT_SUGGESTIONS = ROOT / "analysis" / "direct_target_split_suggestions.json"
DEFAULT_GAP_REPORT = ROOT / "analysis" / "direct_split_gap_report.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_target_envelope_plan.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_target_envelope_plan.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_target_envelope_plan.md"


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


def index_gap_rows(data: dict[str, Any]) -> dict[tuple[str, str], dict[str, Any]]:
    result: dict[tuple[str, str], dict[str, Any]] = {}
    for row in list_value(data.get("rows", [])):
        if not isinstance(row, dict):
            continue
        entry = str(row.get("entry", "")).lower()
        symbol = str(row.get("candidate_symbol", ""))
        if entry and symbol:
            result[(entry, symbol)] = row
    return result


def anchor_rows(data: dict[str, Any], gap_report: dict[str, Any]) -> list[dict[str, Any]]:
    gap_by_key = index_gap_rows(gap_report)
    rows: list[dict[str, Any]] = []
    for row in list_value(data.get("rows", [])):
        if not isinstance(row, dict):
            continue
        kind = str(row.get("kind", ""))
        boundary = str(row.get("boundary_kind", ""))
        entry = str(row.get("entry", "")).lower()
        symbol = str(row.get("candidate_symbol", ""))
        gap = gap_by_key.get((entry, symbol), {})
        if kind not in {"body-anchor", "reshape-anchor", "entry-adjacent"}:
            continue
        if kind == "entry-adjacent" and str(gap.get("gap_class", "")) != "semantic-gap-needs-better-candidate":
            continue
        rows.append(
            {
                "entry": entry,
                "oot3d_name": row.get("oot3d_name", ""),
                "target_name": row.get("target_name", ""),
                "domain": row.get("domain", ""),
                "candidate_symbol": symbol,
                "kind": kind,
                "boundary_kind": boundary,
                "priority": int_value(row.get("priority")),
                "target_instruction_count": int_value(row.get("target_instruction_count")),
                "candidate_instruction_count": int_value(row.get("candidate_instruction_count")),
                "inferred_start_index": int_value(row.get("inferred_start_index")),
                "inferred_end_index": int_value(row.get("inferred_end_index")),
                "inferred_start_addr": row.get("inferred_start_addr", ""),
                "inferred_end_addr": row.get("inferred_end_addr", ""),
                "target_run_addr": row.get("target_run_addr", ""),
                "run_length": int_value(row.get("run_length")),
                "lcs_target_ratio": float_value(row.get("lcs_target_ratio")),
                "lcs_symbol_ratio": float_value(row.get("lcs_symbol_ratio")),
                "source_span_status": row.get("source_span_status", ""),
                "gap_class": gap.get("gap_class", ""),
                "next_action": row.get("next_action", ""),
                "source": row.get("source", ""),
            }
        )
    return rows


def cluster_anchors(rows: list[dict[str, Any]], max_gap: int) -> list[dict[str, Any]]:
    ordered = sorted(rows, key=lambda row: (int(row["inferred_start_index"]), -int(row["priority"])))
    clusters: list[dict[str, Any]] = []
    for row in ordered:
        start = int(row["inferred_start_index"])
        end = int(row["inferred_end_index"])
        if not clusters or start - int(clusters[-1]["end_index"]) > max_gap:
            clusters.append(
                {
                    "start_index": start,
                    "end_index": end,
                    "start_addr": row.get("inferred_start_addr", ""),
                    "end_addr": row.get("inferred_end_addr", ""),
                    "anchors": [row],
                }
            )
            continue
        cluster = clusters[-1]
        if end > int(cluster["end_index"]):
            cluster["end_index"] = end
            cluster["end_addr"] = row.get("inferred_end_addr", "") or cluster.get("end_addr", "")
        cluster["anchors"].append(row)
    for cluster in clusters:
        anchors = list_value(cluster.get("anchors"))
        cluster["anchor_count"] = len(anchors)
        cluster["candidate_symbols"] = ", ".join(str(anchor["candidate_symbol"]) for anchor in anchors)
        cluster["span_instruction_count"] = int(cluster["end_index"]) - int(cluster["start_index"]) + 1
    return clusters


def classify_entry(rows: list[dict[str, Any]], clusters: list[dict[str, Any]]) -> tuple[str, str]:
    complete_body = [
        row for row in rows
        if row["kind"] == "body-anchor" and row["source_span_status"] == "complete"
    ]
    if len(complete_body) >= 2:
        return "multi-anchor-envelope", "Create target-window probes for each cluster and compare against candidate helpers."
    if complete_body:
        return "single-anchor-envelope", "Use the anchor as a target-window probe seed before adding manual symbols."
    if any(row["kind"] == "reshape-anchor" for row in rows):
        return "reshape-envelope", "Treat the mapped function as a reshape candidate and inspect prologue/state setup."
    return "entry-remap-or-truncated", "Resolve entry-adjacent or truncated source evidence before target-window probing."


def build_plan(split_suggestions: dict[str, Any], gap_report: dict[str, Any], max_gap: int) -> dict[str, Any]:
    anchors = anchor_rows(split_suggestions, gap_report)
    grouped: dict[str, list[dict[str, Any]]] = {}
    for row in anchors:
        grouped.setdefault(str(row["entry"]), []).append(row)

    rows: list[dict[str, Any]] = []
    detail_rows: list[dict[str, Any]] = []
    for entry, entry_rows in grouped.items():
        clusters = cluster_anchors(entry_rows, max_gap)
        classification, next_gate = classify_entry(entry_rows, clusters)
        target_instruction_count = max(int(row["target_instruction_count"]) for row in entry_rows)
        best = sorted(entry_rows, key=lambda row: (-int(row["priority"]), int(row["inferred_start_index"])))[0]
        body_anchors = [row for row in entry_rows if row["kind"] == "body-anchor"]
        complete_anchors = [row for row in entry_rows if row["source_span_status"] == "complete"]
        gap_classes = Counter(str(row["gap_class"]) for row in entry_rows if row.get("gap_class"))
        cluster_text = "; ".join(
            f"{cluster['start_addr']}-{cluster['end_addr']}:{cluster['anchor_count']}"
            for cluster in clusters
        )
        rows.append(
            {
                "entry": entry,
                "oot3d_name": best.get("oot3d_name", ""),
                "target_name": best.get("target_name", ""),
                "domain": best.get("domain", ""),
                "classification": classification,
                "priority": int(best["priority"]),
                "target_instruction_count": target_instruction_count,
                "anchor_count": len(entry_rows),
                "body_anchor_count": len(body_anchors),
                "complete_anchor_count": len(complete_anchors),
                "cluster_count": len(clusters),
                "clusters": cluster_text,
                "gap_classes": dict(sorted(gap_classes.items())),
                "top_candidate_symbol": best.get("candidate_symbol", ""),
                "top_anchor_addr": best.get("inferred_start_addr", ""),
                "top_anchor_index": best.get("inferred_start_index", 0),
                "top_lcs_symbol_ratio": best.get("lcs_symbol_ratio", 0.0),
                "next_gate": next_gate,
            }
        )
        for cluster_index, cluster in enumerate(clusters, start=1):
            for anchor in list_value(cluster.get("anchors")):
                detail_rows.append(
                    {
                        "entry": entry,
                        "cluster_index": cluster_index,
                        "cluster_start_addr": cluster.get("start_addr", ""),
                        "cluster_end_addr": cluster.get("end_addr", ""),
                        "cluster_anchor_count": cluster.get("anchor_count", 0),
                        **anchor,
                    }
                )

    rows.sort(key=lambda row: (-int(row["priority"]), -int(row["body_anchor_count"]), str(row["entry"])))
    detail_rows.sort(
        key=lambda row: (
            str(row["entry"]),
            int(row["cluster_index"]),
            int(row["inferred_start_index"]),
            -int(row["priority"]),
        )
    )
    classifications = Counter(str(row["classification"]) for row in rows)
    summary = {
        "entries": len(rows),
        "anchors": len(anchors),
        "body_anchors": sum(int(row["body_anchor_count"]) for row in rows),
        "complete_anchors": sum(int(row["complete_anchor_count"]) for row in rows),
        "clusters": sum(int(row["cluster_count"]) for row in rows),
        "multi_anchor_envelopes": classifications.get("multi-anchor-envelope", 0),
        "single_anchor_envelopes": classifications.get("single-anchor-envelope", 0),
        "classifications": dict(sorted(classifications.items())),
        "top_entry": rows[0]["entry"] if rows else "",
        "top_classification": rows[0]["classification"] if rows else "",
        "max_cluster_gap": max_gap,
        "next_gate": "Generate target-window probes for multi-anchor envelopes before adding manual function symbols.",
    }
    return {
        "format": "oot3d_direct_target_envelope_plan_v1",
        "summary": summary,
        "rows": rows,
        "anchor_rows": detail_rows,
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct Target Envelope Plan",
        "",
        "This plan groups internal direct-split anchors by OOT3D target function so large targets can be narrowed before C promotion.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Entries | {summary['entries']} |",
        f"| Anchors | {summary['anchors']} |",
        f"| Body anchors | {summary['body_anchors']} |",
        f"| Complete anchors | {summary['complete_anchors']} |",
        f"| Clusters | {summary['clusters']} |",
        f"| Multi-anchor envelopes | {summary['multi_anchor_envelopes']} |",
        f"| Single-anchor envelopes | {summary['single_anchor_envelopes']} |",
        f"| Max cluster gap | {summary['max_cluster_gap']} |",
        "",
        "## Entries",
        "",
        "| Class | OOT3D | Anchors | Clusters | Top anchor | Target insns | Top candidate | Next gate |",
        "| --- | --- | ---: | --- | --- | ---: | --- | --- |",
    ]
    for row in data["rows"]:
        lines.append(
            f"| `{row['classification']}` | `{row['entry']}` `{row['oot3d_name']}` | "
            f"{row['anchor_count']} ({row['body_anchor_count']} body) | `{row['clusters']}` | "
            f"`{row['top_anchor_addr']}` idx {row['top_anchor_index']} | "
            f"{row['target_instruction_count']} | `{row['top_candidate_symbol']}` | {row['next_gate']} |"
        )
    lines.extend(
        [
            "",
            "## Anchor Detail",
            "",
            "| OOT3D | Cluster | Anchor | Candidate | Kind | Run | LCS target/symbol | Source |",
            "| --- | ---: | --- | --- | --- | ---: | ---: | --- |",
        ]
    )
    for row in data["anchor_rows"]:
        lines.append(
            f"| `{row['entry']}` `{row['oot3d_name']}` | {row['cluster_index']} | "
            f"`{row['inferred_start_addr']}`-`{row['inferred_end_addr']}` | "
            f"`{row['candidate_symbol']}` | `{row['kind']}` `{row['gap_class']}` | "
            f"{row['run_length']} | {float_value(row['lcs_target_ratio']):.4f}/{float_value(row['lcs_symbol_ratio']):.4f} | "
            f"`{row['source']}` |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--split-suggestions", type=Path, default=DEFAULT_SPLIT_SUGGESTIONS)
    parser.add_argument("--gap-report", type=Path, default=DEFAULT_GAP_REPORT)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    parser.add_argument("--max-cluster-gap", type=int, default=128)
    args = parser.parse_args()

    data = build_plan(
        read_json(args.split_suggestions, {}),
        read_json(args.gap_report, {}),
        args.max_cluster_gap,
    )
    fields = [
        "entry",
        "oot3d_name",
        "target_name",
        "domain",
        "classification",
        "priority",
        "target_instruction_count",
        "anchor_count",
        "body_anchor_count",
        "complete_anchor_count",
        "cluster_count",
        "clusters",
        "gap_classes",
        "top_candidate_symbol",
        "top_anchor_addr",
        "top_anchor_index",
        "top_lcs_symbol_ratio",
        "next_gate",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"], fields)
    write_markdown(args.out_md, data)
    summary = data["summary"]
    print(
        "direct target envelope plan: "
        f"{summary['entries']} entries, {summary['anchors']} anchors, "
        f"{summary['multi_anchor_envelopes']} multi-anchor envelopes"
    )
    print(str(args.out_md.relative_to(ROOT)).replace("\\", "/"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
