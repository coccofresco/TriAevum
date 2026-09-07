#!/usr/bin/env python3
"""Classify N64/OOT3D ranker hits by practical importability."""

from __future__ import annotations

import argparse
import csv
import json
import math
from collections import Counter
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
DEFAULT_RANK_JSONS = (
    ANALYSIS / "n64_port_candidate_rank_pause_item.json",
    ANALYSIS / "n64_port_candidate_rank.json",
)


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def read_json(path: Path, default: Any) -> Any:
    if not path.is_file():
        return default
    return json.loads(path.read_text(encoding="utf-8"))


def top_hits(rank_jsons: list[Path]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    best_by_entry: dict[str, dict[str, Any]] = {}

    for rank_json in rank_jsons:
        data = read_json(rank_json, {})
        for match in data.get("matches", []):
            candidates = match.get("candidates", [])
            if not candidates:
                continue
            top = candidates[0]
            score = float(top["score"])
            runner_up = float(candidates[1]["score"]) if len(candidates) > 1 else 0.0
            row = {
                "rank_json": rel(rank_json),
                "oot3d_entry": str(match["oot3d_entry"]).lower(),
                "oot3d_name": match["oot3d_name"],
                "oot3d_path": match["oot3d_path"],
                "oot3d_lines": int(match["oot3d_lines"]),
                "score": score,
                "score_gap": round(score - runner_up, 2),
                "n64_name": top["n64_name"],
                "n64_path": top["n64_path"],
                "n64_lines": int(top["n64_lines"]),
                "evidence": top.get("evidence", {}),
            }
            current = best_by_entry.get(row["oot3d_entry"])
            if current is None or row["score"] > current["score"]:
                best_by_entry[row["oot3d_entry"]] = row

    rows.extend(best_by_entry.values())
    return rows


def evidence_count(row: dict[str, Any]) -> int:
    evidence = row.get("evidence") or {}
    return sum(len(evidence.get(key, [])) for key in ("constants", "strings", "tokens", "call_tokens"))


def evidence_text(row: dict[str, Any]) -> str:
    evidence = row.get("evidence") or {}
    parts: list[str] = []
    for key in ("constants", "strings", "tokens", "call_tokens"):
        values = evidence.get(key) or []
        if values:
            parts.append(f"{key}:{','.join(str(value) for value in values[:6])}")
    return "; ".join(parts)


def classify_row(
    row: dict[str, Any],
    fanout_counts: Counter[str],
    mapped_by_entry: dict[str, dict[str, str]],
    manual_entries: set[str],
) -> dict[str, Any]:
    n64_key = f"{row['n64_path']}::{row['n64_name']}"
    fanout = fanout_counts[n64_key]
    ratio = row["oot3d_lines"] / max(row["n64_lines"], 1)
    size_distance = abs(math.log2(max(ratio, 0.01)))
    small_enough_for_test = row["oot3d_lines"] <= 420 and row["n64_lines"] <= 520
    mapped = mapped_by_entry.get(row["oot3d_entry"])
    evidence = evidence_count(row)

    direct_shape = fanout <= 2 and 0.35 <= ratio <= 2.25
    strong_rank = row["score"] >= 220 and row["score_gap"] >= 1.5
    unambiguous = fanout == 1 and row["score_gap"] >= 1.0

    if mapped:
        bucket = "already-started"
    elif direct_shape and strong_rank and small_enough_for_test and unambiguous:
        bucket = "direct-import-test"
    elif direct_shape and row["score"] >= 220 and fanout <= 2:
        bucket = "large-direct-port"
    elif fanout >= 4 or ratio < 0.25:
        bucket = "split-or-subsystem"
    elif row["score"] >= 200 and evidence >= 6:
        bucket = "semantic-name-port"
    else:
        bucket = "weak"

    priority = (
        row["score"]
        + row["score_gap"] * 3.0
        + evidence * 4.0
        - (fanout - 1) * 55.0
        - size_distance * 35.0
        - (0.0 if small_enough_for_test else 80.0)
    )
    if bucket == "direct-import-test":
        priority += 120.0
    elif bucket == "large-direct-port":
        priority += 40.0
    elif bucket == "already-started":
        priority -= 50.0
    elif bucket == "split-or-subsystem":
        priority -= 130.0

    return {
        **row,
        "bucket": bucket,
        "priority": round(priority, 2),
        "fanout": fanout,
        "line_ratio": round(ratio, 2),
        "evidence_count": evidence,
        "manual_symbol_exists": row["oot3d_entry"] in manual_entries,
        "mapped_status": mapped.get("status", "") if mapped else "",
        "mapped_port_file": mapped.get("port_file", "") if mapped else "",
        "evidence_text": evidence_text(row),
    }


def build_rows(rank_jsons: list[Path]) -> list[dict[str, Any]]:
    rows = top_hits(rank_jsons)
    fanout_counts = Counter(f"{row['n64_path']}::{row['n64_name']}" for row in rows)
    mapped_by_entry = {row["oot3d_entry"].lower(): row for row in read_csv(ROOT / "metadata" / "n64_port_map.csv")}
    manual_entries = {
        row["entry"].lower()
        for row in read_csv(ROOT / "symbols" / "manual_symbols.csv")
        if row.get("kind", "function") == "function"
    }
    return sorted(
        (classify_row(row, fanout_counts, mapped_by_entry, manual_entries) for row in rows),
        key=lambda row: (-row["priority"], row["oot3d_entry"]),
    )


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "bucket",
        "priority",
        "oot3d_entry",
        "oot3d_name",
        "oot3d_lines",
        "n64_name",
        "n64_path",
        "n64_lines",
        "line_ratio",
        "score",
        "score_gap",
        "fanout",
        "evidence_count",
        "manual_symbol_exists",
        "mapped_status",
        "mapped_port_file",
        "rank_json",
    ]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({key: row.get(key, "") for key in fieldnames})


def write_json(path: Path, rows: list[dict[str, Any]], rank_jsons: list[Path]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    data = {
        "rank_jsons": [rel(path) for path in rank_jsons],
        "counts_by_bucket": Counter(row["bucket"] for row in rows),
        "rows": rows,
    }
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def write_markdown(path: Path, rows: list[dict[str, Any]], rank_jsons: list[Path], limit: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    counts = Counter(row["bucket"] for row in rows)
    lines = [
        "# N64 Importability Classification",
        "",
        "Generated from ranker output to separate direct import tests from split/subsystem evidence.",
        "",
        "## Inputs",
        "",
    ]
    for rank_json in rank_jsons:
        lines.append(f"- `{rel(rank_json)}`")
    lines.extend(
        [
            "",
            "## Bucket Counts",
            "",
            "| Bucket | Count | Meaning |",
            "| --- | ---: | --- |",
            f"| `direct-import-test` | {counts['direct-import-test']} | Small/medium top hit, low fan-out, plausible line ratio; try N64-derived C in compile/compare first. |",
            f"| `large-direct-port` | {counts['large-direct-port']} | Similar shape but too large for a quick exact test; port as a file lane after shared types are known. |",
            f"| `split-or-subsystem` | {counts['split-or-subsystem']} | One N64 function maps to many OOT3D helpers or size ratio is split-like; recover roles first. |",
            f"| `semantic-name-port` | {counts['semantic-name-port']} | Useful for naming/types/constants, weak for immediate exact import. |",
            f"| `already-started` | {counts['already-started']} | Already present in `metadata/n64_port_map.csv`. |",
            f"| `weak` | {counts['weak']} | Insufficient evidence for import work. |",
            "",
            "## Direct Import Test Queue",
            "",
            "| Priority | OOT3D | OOT3D lines | N64 function | N64 lines | Ratio | Score | Gap | Evidence |",
            "| ---: | --- | ---: | --- | ---: | ---: | ---: | ---: | --- |",
        ]
    )
    direct_rows = [row for row in rows if row["bucket"] == "direct-import-test"]
    for row in direct_rows[:limit]:
        lines.append(
            f"| {row['priority']:.2f} | `{row['oot3d_entry']}` `{row['oot3d_name']}` | "
            f"{row['oot3d_lines']} | `{row['n64_name']}` `{row['n64_path']}` | {row['n64_lines']} | "
            f"{row['line_ratio']:.2f} | {row['score']:.2f} | {row['score_gap']:.2f} | "
            f"{row['evidence_text'] or '-'} |"
        )
    if not direct_rows:
        lines.append("| - | - | 0 | - | 0 | 0 | 0 | 0 | - |")

    lines.extend(
        [
            "",
            "## Large Direct Port Queue",
            "",
            "| Priority | OOT3D | OOT3D lines | N64 function | N64 lines | Ratio | Score | Gap |",
            "| ---: | --- | ---: | --- | ---: | ---: | ---: | ---: |",
        ]
    )
    for row in [row for row in rows if row["bucket"] == "large-direct-port"][:limit]:
        lines.append(
            f"| {row['priority']:.2f} | `{row['oot3d_entry']}` `{row['oot3d_name']}` | "
            f"{row['oot3d_lines']} | `{row['n64_name']}` `{row['n64_path']}` | {row['n64_lines']} | "
            f"{row['line_ratio']:.2f} | {row['score']:.2f} | {row['score_gap']:.2f} |"
        )

    lines.extend(
        [
            "",
            "## Split Or Subsystem Queue",
            "",
            "| Priority | OOT3D | N64 function | Fan-out | Ratio | Use |",
            "| ---: | --- | --- | ---: | ---: | --- |",
        ]
    )
    for row in [row for row in rows if row["bucket"] == "split-or-subsystem"][:limit]:
        lines.append(
            f"| {row['priority']:.2f} | `{row['oot3d_entry']}` `{row['oot3d_name']}` | "
            f"`{row['n64_name']}` `{row['n64_path']}` | {row['fanout']} | {row['line_ratio']:.2f} | "
            "Port shared structs/enums and assign split helper roles before exact attempts. |"
        )

    lines.extend(
        [
            "",
            "## Exact-Oriented Procedure",
            "",
            "1. Take the first `direct-import-test` row.",
            "2. Extract the N64 function and the OOT3D decompile into a focused packet.",
            "3. Port the N64 body into a structured OOT3D source file with address/offset adaptation.",
            "4. Compile that file with `scripts/build-matched-objects.ps1 -Source ...` into an isolated output dir.",
            "5. Promote it into the exact baseline only after the compare report shows exact instructions.",
            "6. If the first difference is a type/prototype/literal-pool issue, fix the shared type model and rerun the whole bucket.",
            "",
        ]
    )
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rank-json", action="append", type=Path, dest="rank_jsons")
    parser.add_argument("--limit", type=int, default=30)
    parser.add_argument("--out-json", type=Path, default=ANALYSIS / "n64_importability.json")
    parser.add_argument("--out-md", type=Path, default=ANALYSIS / "n64_importability.md")
    parser.add_argument("--out-csv", type=Path, default=ANALYSIS / "n64_importability.csv")
    args = parser.parse_args()

    rank_jsons = args.rank_jsons or [path for path in DEFAULT_RANK_JSONS if path.is_file()]
    rows = build_rows(rank_jsons)
    write_json(args.out_json, rows, rank_jsons)
    write_markdown(args.out_md, rows, rank_jsons, args.limit)
    write_csv(args.out_csv, rows)
    counts = Counter(row["bucket"] for row in rows)
    print(
        "importability: "
        f"{counts['direct-import-test']} direct tests, "
        f"{counts['large-direct-port']} large direct, "
        f"{counts['split-or-subsystem']} split/subsystem, "
        f"{counts['already-started']} already started"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
