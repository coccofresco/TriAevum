#!/usr/bin/env python3
"""Build source subregion split suggestions for high-skip semantic blockers."""

from __future__ import annotations

import argparse
import csv
import json
import re
from pathlib import Path
from typing import Any

from refine_direct_data_like_boundaries import read_indexed_ops


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SEMANTIC = ROOT / "analysis" / "direct_semantic_call_shape.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_source_subregion_plan.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_source_subregion_plan.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_source_subregion_plan.md"
SOURCE_LINE_RE = re.compile(r"^\s*(?P<line>\d+):\s(?P<text>.*)$")
MEM_OFFSET_RE = re.compile(r"\[(?P<base>r\d+|sp),#(?P<offset>-?\d+)\]")
OFFSET_TERMS = {
    "r4:512": ["stateFlags1"],
    "r4:54": ["yaw"],
    "r4:652": ["itemAction"],
    "r4:696": ["av1.actionVar1"],
    "r4:748": ["av2.actionVar2"],
    "r6:3756": ["respawn[RESPAWN_MODE_TOP].data"],
    "r6:3710": ["save.info.fw.roomIndex"],
    "r6:3712": ["save.info.fw.tempSwchFlags"],
    "r6:3716": ["save.info.fw.tempCollectFlags"],
}


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


def read_source_excerpt(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    if not path.is_file():
        return rows
    for ordinal, raw in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines()):
        match = SOURCE_LINE_RE.match(raw)
        if not match:
            continue
        rows.append({"ordinal": ordinal, "line": int(match.group("line")), "text": match.group("text")})
    return rows


def offsets_in_ops(ops: list[str]) -> list[str]:
    offsets: list[str] = []
    for op in ops:
        for match in MEM_OFFSET_RE.finditer(op):
            offsets.append(f"{match.group('base')}:{match.group('offset')}")
    return offsets


def source_hits(source_rows: list[dict[str, Any]], terms: list[str]) -> list[dict[str, Any]]:
    hits: list[dict[str, Any]] = []
    for row in source_rows:
        text = str(row.get("text", ""))
        matched = [term for term in terms if term in text]
        if matched:
            hits.append({"line": row["line"], "text": text.strip(), "terms": matched})
    return hits


def first_mapped_offset(offsets: list[str]) -> str:
    for offset in offsets:
        if offset in OFFSET_TERMS:
            return offset
    return ""


def prefer_increment_hit(hits: list[dict[str, Any]], compiled_window_ops: list[str]) -> dict[str, Any] | None:
    if not hits:
        return None
    first_ops = " ".join(compiled_window_ops[:3])
    if "add " not in first_ops or "str " not in first_ops:
        return None
    for hit in hits:
        text = str(hit.get("text", ""))
        if "++" in text or "+=" in text:
            return hit
    return None


def control_boundaries(source_rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    boundaries: list[dict[str, Any]] = []
    for row in source_rows:
        text = str(row.get("text", "")).strip()
        if not text:
            continue
        if text.startswith("if ") or text.startswith("if (") or text.startswith("} else") or text.startswith("else "):
            boundaries.append({"line": row["line"], "kind": "branch", "text": text})
        elif re.search(r"\w+\(.*\);$", text):
            boundaries.append({"line": row["line"], "kind": "call", "text": text})
    return boundaries


def nearest_boundary_ranges(source_rows: list[dict[str, Any]], anchor_line: int, radius: int) -> list[dict[str, Any]]:
    if not source_rows:
        return []
    lines = [int(row["line"]) for row in source_rows]
    min_line = min(lines)
    max_line = max(lines)
    boundaries = control_boundaries(source_rows)
    before = [row for row in boundaries if int(row["line"]) <= anchor_line]
    after = [row for row in boundaries if int(row["line"]) > anchor_line]
    starts = [before[-1]["line"] if before else max(min_line, anchor_line - radius), anchor_line]
    ends = [after[0]["line"] - 1 if after else min(max_line, anchor_line + radius), min(max_line, anchor_line + radius)]
    ranges: list[dict[str, Any]] = []
    for start in sorted(set(starts)):
        for end in sorted(set(ends)):
            if start <= end:
                ranges.append({"source_start_line": start, "source_end_line": end})
    return ranges


def build_report(args: argparse.Namespace) -> dict[str, Any]:
    semantic = read_json(args.semantic, {})
    rows: list[dict[str, Any]] = []
    for source in list_value(semantic.get("rows", [])):
        if not isinstance(source, dict):
            continue
        if source.get("blocker_class") != "compiled-subregion-alignment":
            continue
        source_rows = read_source_excerpt(repo_path(source.get("source_excerpt", "")))
        manifest_path = ""
        # The semantic report points to the source excerpt; the sibling manifest holds op paths.
        source_excerpt_path = repo_path(source.get("source_excerpt", ""))
        if source_excerpt_path.name == "source_excerpt.c.txt":
            manifest_path = str(source_excerpt_path.with_name("probe_manifest.json"))
        manifest = read_json(Path(manifest_path), {})
        compiled_rows = read_indexed_ops(repo_path(manifest.get("compiled_helper_body_ops", "")))
        compiled_extra_skip = int_value(source.get("compiled_extra_skip"))
        compiled_window_ops = [
            str(row.get("op", ""))
            for row in compiled_rows[compiled_extra_skip : compiled_extra_skip + args.anchor_ops]
        ]
        offsets = offsets_in_ops(compiled_window_ops)
        terms: list[str] = []
        for offset in offsets:
            terms.extend(OFFSET_TERMS.get(offset, []))
        primary_offset = first_mapped_offset(offsets)
        primary_terms = OFFSET_TERMS.get(primary_offset, [])
        hits = source_hits(source_rows, sorted(set(terms)))
        primary_hits = source_hits(source_rows, primary_terms)
        preferred_hit = prefer_increment_hit(primary_hits, compiled_window_ops)
        anchor_hit = preferred_hit if preferred_hit is not None else primary_hits[0] if primary_hits else hits[0] if hits else None
        anchor_line = int_value(anchor_hit["line"]) if anchor_hit else 0
        ranges = nearest_boundary_ranges(source_rows, anchor_line, args.source_radius) if anchor_line else []
        rows.append(
            {
                "entry": source.get("entry", ""),
                "oot3d_name": source.get("oot3d_name", ""),
                "candidate_symbol": source.get("candidate_symbol", ""),
                "blocker_class": source.get("blocker_class", ""),
                "target_window": source.get("target_window", ""),
                "compiled_extra_skip": compiled_extra_skip,
                "effective_compiled_skip": int_value(source.get("effective_compiled_skip")),
                "lcs_window_ratio": float_value(source.get("lcs_window_ratio")),
                "anchor_offsets": sorted(set(offsets)),
                "anchor_terms": sorted(set(terms)),
                "primary_anchor_offset": primary_offset,
                "primary_anchor_terms": primary_terms,
                "anchor_source_line": anchor_line,
                "anchor_source_text": anchor_hit["text"] if anchor_hit else "",
                "source_hit_count": len(hits),
                "source_hits": hits,
                "suggested_ranges": ranges,
                "suggested_range_count": len(ranges),
                "source_excerpt": source.get("source_excerpt", ""),
                "next_gate": "Materialize a probe for the suggested source subregion and rerun direct window comparison.",
            }
        )

    summary = {
        "packets": len(rows),
        "source_subregion_candidates": sum(int_value(row.get("suggested_range_count")) for row in rows),
        "anchored_packets": sum(1 for row in rows if int_value(row.get("anchor_source_line")) > 0),
        "best_entry": rows[0].get("entry", "") if rows else "",
        "best_candidate_symbol": rows[0].get("candidate_symbol", "") if rows else "",
        "best_anchor_source_line": rows[0].get("anchor_source_line", 0) if rows else 0,
        "next_gate": "Materialize and compile the suggested source subregion probes.",
    }
    return {
        "format": "oot3d_direct_source_subregion_plan_v1",
        "summary": summary,
        "rows": rows,
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct Source Subregion Plan",
        "",
        "This report turns compiled-subregion blockers into source-range candidates.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Packets | {summary['packets']} |",
        f"| Anchored packets | {summary['anchored_packets']} |",
        f"| Source subregion candidates | {summary['source_subregion_candidates']} |",
        "",
        "## Candidates",
        "",
        "| OOT3D | Candidate | Anchor | Terms | Suggested ranges | Next gate |",
        "| --- | --- | --- | --- | --- | --- |",
    ]
    for row in data["rows"]:
        ranges = ", ".join(
            f"{item['source_start_line']}-{item['source_end_line']}"
            for item in row.get("suggested_ranges", [])
        )
        terms = ", ".join(f"`{term}`" for term in row.get("anchor_terms", []))
        lines.append(
            f"| `{row.get('entry', '')}` `{row.get('oot3d_name', '')}` | "
            f"`{row.get('candidate_symbol', '')}` | "
            f"{row.get('anchor_source_line', '')}: `{row.get('anchor_source_text', '')}` | "
            f"{terms} | {ranges} | {row.get('next_gate', '')} |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--semantic", type=Path, default=DEFAULT_SEMANTIC)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    parser.add_argument("--anchor-ops", type=int, default=16)
    parser.add_argument("--source-radius", type=int, default=14)
    args = parser.parse_args()

    data = build_report(args)
    fields = [
        "entry",
        "oot3d_name",
        "candidate_symbol",
        "blocker_class",
        "target_window",
        "compiled_extra_skip",
        "effective_compiled_skip",
        "lcs_window_ratio",
        "anchor_offsets",
        "anchor_terms",
        "primary_anchor_offset",
        "primary_anchor_terms",
        "anchor_source_line",
        "anchor_source_text",
        "source_hit_count",
        "source_hits",
        "suggested_ranges",
        "suggested_range_count",
        "source_excerpt",
        "next_gate",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"], fields)
    write_markdown(args.out_md, data)
    summary = data["summary"]
    print(
        "direct source subregion plan: "
        f"{summary['packets']} packets, "
        f"{summary['anchored_packets']} anchored, "
        f"{summary['source_subregion_candidates']} source ranges"
    )
    print(str(args.out_md.relative_to(ROOT)).replace("\\", "/"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
