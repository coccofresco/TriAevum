#!/usr/bin/env python3
"""Materialize source-correlation workorders for 00473ef8 handler probes."""

from __future__ import annotations

import argparse
import csv
import json
import re
from pathlib import Path
from typing import Any

from analyze_direct_state_mode_source_correlation import DEFAULT_N64_SOURCE
from probe_direct_source_subregions import rel


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CORRELATION = ROOT / "analysis" / "direct_state_mode_source_correlation.json"
DEFAULT_HANDLER_CONTEXT = ROOT / "analysis" / "direct_state_mode_handler_context.json"
DEFAULT_OUT_DIR = ROOT / "analysis" / "direct_state_mode_source_workorders"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_state_mode_source_workorders.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_state_mode_source_workorders.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_state_mode_source_workorders.md"


def read_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    return value if isinstance(value, dict) else {}


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


def slug(value: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9_.-]+", "_", value.strip())
    return cleaned.strip("_") or "workorder"


def source_excerpt(source_path: Path, source_range: str) -> str:
    match = re.match(r"^(?P<start>\d+)-(?P<end>\d+)$", source_range)
    if not match:
        return ""
    start = int(match.group("start"))
    end = int(match.group("end"))
    lines = source_path.read_text(encoding="utf-8", errors="replace").splitlines()
    excerpt: list[str] = []
    for line_no in range(start, min(end, len(lines)) + 1):
        excerpt.append(f"{line_no:04d}: {lines[line_no - 1]}")
    return "\n".join(excerpt)


def build_workorders(args: argparse.Namespace) -> dict[str, Any]:
    correlation = read_json(args.correlation)
    handler_context = read_json(args.handler_context)
    handlers = {
        str(row.get("handler", "")): row
        for row in handler_context.get("rows", [])
        if isinstance(row, dict) and row.get("handler")
    }
    candidates = [
        row
        for row in correlation.get("rows", [])
        if isinstance(row, dict) and row.get("confidence") == "candidate-source-window"
    ]
    candidates.sort(key=lambda row: (float(row.get("best_score", 0.0) or 0.0), int(row.get("matched_weight", 0) or 0)), reverse=True)
    selected = candidates[: args.limit]
    args.out_dir.mkdir(parents=True, exist_ok=True)
    rows: list[dict[str, Any]] = []
    for index, row in enumerate(selected, start=1):
        handler = str(row.get("handler", ""))
        handler_row = handlers.get(handler, {})
        workorder_id = slug(
            f"{index:02d}_{handler}_{row.get('states', '')}_{row.get('best_source_function', '')}_{row.get('best_source_range', '')}"
        )
        workorder_dir = args.out_dir / workorder_id
        workorder_dir.mkdir(parents=True, exist_ok=True)
        source_text = source_excerpt(args.n64_source, str(row.get("best_source_range", "")))
        body_excerpt = str(handler_row.get("body_excerpt", ""))
        manifest = {
            "workorder_id": workorder_id,
            "handler": handler,
            "handler_range": row.get("range", ""),
            "states": row.get("states", ""),
            "feature_tags": row.get("feature_tags", ""),
            "best_source_function": row.get("best_source_function", ""),
            "best_source_range": row.get("best_source_range", ""),
            "best_score": row.get("best_score", 0.0),
            "matched_weight": row.get("matched_weight", 0),
            "matched_tokens": row.get("matched_tokens", ""),
            "confidence": row.get("confidence", ""),
            "promotion_state": "probe-required",
            "next_probe": (
                "Isolate this handler range against the N64 source excerpt with a focused branch/body "
                "compile probe; do not assign final state names from this workorder alone."
            ),
            "paths": {
                "manifest": rel(workorder_dir / "workorder.json"),
                "source_excerpt": rel(workorder_dir / "source_excerpt.c.txt"),
                "handler_excerpt": rel(workorder_dir / "handler_excerpt.ops.txt"),
            },
        }
        write_json(workorder_dir / "workorder.json", manifest)
        (workorder_dir / "source_excerpt.c.txt").write_text(source_text + "\n", encoding="utf-8", newline="\n")
        (workorder_dir / "handler_excerpt.ops.txt").write_text(body_excerpt.replace(" | ", "\n") + "\n", encoding="utf-8", newline="\n")
        rows.append(
            {
                "workorder_id": workorder_id,
                "handler": handler,
                "handler_range": row.get("range", ""),
                "states": row.get("states", ""),
                "feature_tags": row.get("feature_tags", ""),
                "best_source_function": row.get("best_source_function", ""),
                "best_source_range": row.get("best_source_range", ""),
                "best_score": row.get("best_score", 0.0),
                "matched_weight": row.get("matched_weight", 0),
                "matched_tokens": row.get("matched_tokens", ""),
                "promotion_state": "probe-required",
                "workorder_dir": rel(workorder_dir),
            }
        )
    summary = {
        "candidate_rows": len(candidates),
        "workorders": len(rows),
        "limit": args.limit,
        "source_functions": sorted({str(row["best_source_function"]) for row in rows if row.get("best_source_function")}),
        "promotion_ready_workorders": 0,
        "next_gate": "Run focused branch/body probes for these handler/source pairs before naming states.",
    }
    return {
        "format": "oot3d_direct_state_mode_source_workorders_v1",
        "inputs": {
            "correlation": rel(args.correlation),
            "handler_context": rel(args.handler_context),
            "n64_source": rel(args.n64_source),
            "out_dir": rel(args.out_dir),
        },
        "summary": summary,
        "rows": rows,
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct State/Mode Source Workorders",
        "",
        "These workorders materialize the strongest handler/source correlations for focused branch/body probes.",
        "Every row remains `probe-required`; no state name or C promotion is implied.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Candidate rows | {summary['candidate_rows']} |",
        f"| Workorders | {summary['workorders']} |",
        f"| Limit | {summary['limit']} |",
        f"| Promotion-ready workorders | {summary['promotion_ready_workorders']} |",
        "",
        f"Next gate: {summary['next_gate']}",
        "",
        "## Workorders",
        "",
        "| Workorder | Handler | States | Source | Score | State |",
        "| --- | --- | --- | --- | ---: | --- |",
    ]
    for row in data["rows"]:
        source = f"{row['best_source_function']}:{row['best_source_range']}"
        lines.append(
            f"| `{row['workorder_id']}` | `{row['handler']}` | `{row['states']}` | "
            f"`{source}` | {float(row['best_score']):.4f} | `{row['promotion_state']}` |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--correlation", type=Path, default=DEFAULT_CORRELATION)
    parser.add_argument("--handler-context", type=Path, default=DEFAULT_HANDLER_CONTEXT)
    parser.add_argument("--n64-source", type=Path, default=DEFAULT_N64_SOURCE)
    parser.add_argument("--limit", type=int, default=12)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    args = parser.parse_args()
    for path in [args.correlation, args.handler_context, args.n64_source]:
        if not path.is_file():
            raise SystemExit(f"missing input: {path}")
    data = build_workorders(args)
    fields = [
        "workorder_id",
        "handler",
        "handler_range",
        "states",
        "feature_tags",
        "best_source_function",
        "best_source_range",
        "best_score",
        "matched_weight",
        "matched_tokens",
        "promotion_state",
        "workorder_dir",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"], fields)
    write_markdown(args.out_md, data)
    summary = data["summary"]
    print(
        "direct state/mode source workorders: "
        f"{summary['workorders']} workorders from {summary['candidate_rows']} candidate rows"
    )
    print(str(args.out_md.relative_to(ROOT)).replace("\\", "/"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
