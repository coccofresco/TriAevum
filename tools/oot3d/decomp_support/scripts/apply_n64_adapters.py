#!/usr/bin/env python3
"""Audit reusable N64->OOT3D adapters against extracted N64 port units."""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


ROOT = Path(__file__).resolve().parents[1]

CALL_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")
MACRO_RE = re.compile(r"\b[A-Z][A-Z0-9_]{2,}\b")
MEMBER_RE = re.compile(
    r"\b(?:this|play|player|msgCtx|interfaceCtx|actor|boomerang|boomTarget|floorPoly|wallPoly)"
    r"->(?:[A-Za-z_][A-Za-z0-9_]*)(?:\.[A-Za-z_][A-Za-z0-9_]*)*"
)
COMMENT_RE = re.compile(r"/\*.*?\*/|//[^\r\n]*", re.DOTALL)
STRING_RE = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')

IGNORE_CALLS = {
    "ABS",
    "ARRAY_COUNT",
    "CHECK_BTN_ALL",
    "COLPOLY_GET_NORMAL",
    "CONVEYOR_DIRECTION_TO_BINANG",
    "PRINTF",
    "PRINTF_COLOR_GREEN",
    "PRINTF_RST",
    "SFX_PLAY_CENTERED",
    "T",
    "for",
    "if",
    "sizeof",
    "switch",
    "while",
}
IGNORE_MACROS = {
    "NULL",
    "TRUE",
    "FALSE",
    "ABS",
    "ARRAY_COUNT",
    "PRINTF",
    "PRINTF_RST",
}
MATCH_MODES = {"literal", "token", "member", "regex", "map-n64-name", "map-n64-source"}


@dataclass(frozen=True)
class Adapter:
    id: str
    category: str
    domain: str
    match_mode: str
    n64_pattern: str
    oot3d_symbol: str
    confidence: str
    notes: str


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def slug(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value).strip("_")


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def write_csv(path: Path, rows: list[dict[str, Any]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({key: row.get(key, "") for key in fieldnames})


def read_text(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def code_only(text: str) -> str:
    return STRING_RE.sub("", COMMENT_RE.sub("", text))


def extract_path(row: dict[str, str], out_root: Path) -> Path:
    unit_dir = out_root / slug(row["port_file"])
    prefix = f"{row['oot3d_entry']}_{row['oot3d_name']}__n64_{row['n64_name']}"
    matches = sorted(unit_dir.glob(f"{slug(prefix)}*.c"))
    if matches:
        return matches[0]
    return unit_dir / f"{slug(prefix)}.c"


def source_domain(port_file: str) -> str:
    if "Boss_Va" in port_file:
        return "boss_va"
    if "message" in port_file:
        return "message"
    if "kaleido" in port_file:
        return "pause"
    if "player" in port_file:
        return "player"
    if "large_direct" in port_file:
        return "large_direct_exact_seed"
    if "split_kaleido" in port_file:
        return "split_kaleido_exact_seed"
    return "other"


def n64_domain(row: dict[str, str]) -> str:
    source = row.get("n64_source", "")
    name = row.get("n64_name", "")
    if "Boss_Va" in source or name.startswith("BossVa_"):
        return "boss_va"
    if "Boss_Mo" in source or name.startswith("BossMo_"):
        return "boss_mo"
    if "player" in source.lower() or name.startswith("Player_"):
        return "player"
    if "z_message" in source or name.startswith("Message_"):
        return "message"
    if "kaleido" in source.lower() or name.startswith("KaleidoScope_") or name == "Regs_InitDataImpl":
        return "pause"
    return "other"


def adapter_applies(adapter: Adapter, row: dict[str, str]) -> bool:
    if adapter.domain == "global":
        return True
    if adapter.category == "source-lane":
        return True
    return adapter.domain == n64_domain(row)


def load_adapters(path: Path) -> list[Adapter]:
    adapters: list[Adapter] = []
    seen: set[str] = set()
    for line_no, row in enumerate(read_csv(path), start=2):
        adapter = Adapter(
            id=row.get("id", "").strip(),
            category=row.get("category", "").strip(),
            domain=row.get("domain", "").strip(),
            match_mode=row.get("match_mode", "").strip(),
            n64_pattern=row.get("n64_pattern", "").strip(),
            oot3d_symbol=row.get("oot3d_symbol", "").strip(),
            confidence=row.get("confidence", "").strip(),
            notes=row.get("notes", "").strip(),
        )
        if not adapter.id:
            raise SystemExit(f"{path}:{line_no}: missing adapter id")
        if adapter.id in seen:
            raise SystemExit(f"{path}:{line_no}: duplicate adapter id {adapter.id}")
        if adapter.match_mode not in MATCH_MODES:
            raise SystemExit(f"{path}:{line_no}: unsupported match mode {adapter.match_mode}")
        if not adapter.n64_pattern or not adapter.oot3d_symbol:
            raise SystemExit(f"{path}:{line_no}: missing pattern or symbol for {adapter.id}")
        seen.add(adapter.id)
        adapters.append(adapter)
    return adapters


def adapter_regex(adapter: Adapter) -> re.Pattern[str]:
    if adapter.match_mode == "regex":
        return re.compile(adapter.n64_pattern)
    if adapter.match_mode == "token":
        return re.compile(rf"\b{re.escape(adapter.n64_pattern)}\b")
    if adapter.match_mode == "member":
        return re.compile(rf"(?<![A-Za-z0-9_]){re.escape(adapter.n64_pattern)}(?![A-Za-z0-9_\.])")
    return re.compile(re.escape(adapter.n64_pattern))


def count_adapter(adapter: Adapter, row: dict[str, str], code: str) -> int:
    if adapter.match_mode == "map-n64-name":
        return 1 if row.get("n64_name") == adapter.n64_pattern else 0
    if adapter.match_mode == "map-n64-source":
        return 1 if row.get("n64_source") == adapter.n64_pattern else 0
    return len(adapter_regex(adapter).findall(code))


def text_adapter(adapter: Adapter) -> bool:
    return not adapter.match_mode.startswith("map-")


def readiness(row: dict[str, Any]) -> str:
    text_hits = int(row["text_hits"])
    distinct = int(row["distinct_text_adapters"])
    high = int(row["high_confidence_hits"])
    if text_hits >= 120 and distinct >= 20 and high >= 80:
        return "batch-rich"
    if text_hits >= 50 and distinct >= 12 and high >= 30:
        return "adapter-ready"
    if text_hits >= 15 and distinct >= 6:
        return "partial"
    if text_hits:
        return "low"
    return "none"


def top_counter(counter: Counter[str], limit: int) -> list[dict[str, Any]]:
    return [{"name": key, "count": value} for key, value in counter.most_common(limit)]


def update_uncovered(counter: Counter[str], values: Iterable[str], covered: set[str]) -> None:
    for value in values:
        if value in covered:
            continue
        counter[value] += 1


def analyze(
    port_map: list[dict[str, str]],
    adapters: list[Adapter],
    n64_out_root: Path,
) -> dict[str, Any]:
    adapter_rows: dict[str, dict[str, Any]] = {
        adapter.id: {
            "id": adapter.id,
            "category": adapter.category,
            "domain": adapter.domain,
            "match_mode": adapter.match_mode,
            "n64_pattern": adapter.n64_pattern,
            "oot3d_symbol": adapter.oot3d_symbol,
            "confidence": adapter.confidence,
            "hits": 0,
            "rows": 0,
            "text_hits": 0,
            "source_lane_hits": 0,
        }
        for adapter in adapters
    }
    category_counts: Counter[str] = Counter(adapter.category for adapter in adapters)
    domain_counts: Counter[str] = Counter(adapter.domain for adapter in adapters)
    confidence_counts: Counter[str] = Counter(adapter.confidence for adapter in adapters)
    readiness_counts: Counter[str] = Counter()
    status_counts: Counter[str] = Counter()
    row_summaries: list[dict[str, Any]] = []
    uncovered_calls: Counter[str] = Counter()
    uncovered_members: Counter[str] = Counter()
    uncovered_macros: Counter[str] = Counter()

    for row in port_map:
        path = extract_path(row, n64_out_root)
        text = read_text(path)
        code = code_only(text)
        status_counts[row.get("status", "")] += 1
        row_adapters = [adapter for adapter in adapters if adapter_applies(adapter, row)]
        covered_tokens = {adapter.n64_pattern for adapter in row_adapters if adapter.match_mode == "token"}
        covered_members = {adapter.n64_pattern for adapter in row_adapters if adapter.match_mode == "member"}

        hits_by_adapter: list[dict[str, Any]] = []
        text_hits = 0
        source_lane_hits = 0
        high_confidence_hits = 0

        for adapter in row_adapters:
            hits = count_adapter(adapter, row, code)
            if hits == 0:
                continue
            adapter_rows[adapter.id]["hits"] += hits
            adapter_rows[adapter.id]["rows"] += 1
            if text_adapter(adapter):
                adapter_rows[adapter.id]["text_hits"] += hits
                text_hits += hits
            else:
                adapter_rows[adapter.id]["source_lane_hits"] += hits
                source_lane_hits += hits
            if adapter.confidence == "high":
                high_confidence_hits += hits
            hits_by_adapter.append(
                {
                    "id": adapter.id,
                    "category": adapter.category,
                    "domain": adapter.domain,
                    "confidence": adapter.confidence,
                    "hits": hits,
                    "oot3d_symbol": adapter.oot3d_symbol,
                }
            )

        distinct_text = sum(1 for item in hits_by_adapter if not item["id"].startswith("lane_"))
        summary = {
            "oot3d_entry": row["oot3d_entry"],
            "oot3d_name": row["oot3d_name"],
            "n64_source": row["n64_source"],
            "n64_name": row["n64_name"],
            "port_file": row["port_file"],
            "status": row["status"],
            "domain": source_domain(row["port_file"]),
            "n64_domain": n64_domain(row),
            "n64_extract": rel(path),
            "text_hits": text_hits,
            "source_lane_hits": source_lane_hits,
            "total_hits": text_hits + source_lane_hits,
            "distinct_text_adapters": distinct_text,
            "high_confidence_hits": high_confidence_hits,
            "top_adapters": sorted(hits_by_adapter, key=lambda item: (-int(item["hits"]), item["id"]))[:8],
        }
        summary["readiness"] = readiness(summary)
        readiness_counts[str(summary["readiness"])] += 1
        row_summaries.append(summary)

        calls = [call for call in CALL_RE.findall(code) if call not in IGNORE_CALLS]
        macros = [macro for macro in MACRO_RE.findall(code) if macro not in IGNORE_MACROS]
        update_uncovered(uncovered_calls, calls, covered_tokens)
        update_uncovered(uncovered_members, MEMBER_RE.findall(code), covered_members)
        update_uncovered(uncovered_macros, macros, covered_tokens)

    sorted_adapter_rows = sorted(
        adapter_rows.values(),
        key=lambda item: (-int(item["hits"]), item["category"], item["id"]),
    )
    sorted_row_summaries = sorted(
        row_summaries,
        key=lambda item: (
            {"batch-rich": 0, "adapter-ready": 1, "partial": 2, "low": 3, "none": 4}.get(
                str(item["readiness"]),
                9,
            ),
            -int(item["text_hits"]),
            item["oot3d_entry"],
        ),
    )

    return {
        "summary": {
            "adapter_entries": len(adapters),
            "mapped_rows": len(port_map),
            "rows_with_text_hits": sum(1 for row in row_summaries if row["text_hits"] > 0),
            "rows_with_source_lane_hits": sum(1 for row in row_summaries if row["source_lane_hits"] > 0),
            "total_text_hits": sum(int(row["text_hits"]) for row in row_summaries),
            "total_source_lane_hits": sum(int(row["source_lane_hits"]) for row in row_summaries),
            "adapter_categories": dict(sorted(category_counts.items())),
            "adapter_domains": dict(sorted(domain_counts.items())),
            "adapter_confidence": dict(sorted(confidence_counts.items())),
            "row_readiness": dict(sorted(readiness_counts.items())),
            "map_statuses": dict(sorted(status_counts.items())),
        },
        "rows": sorted_row_summaries,
        "adapters": sorted_adapter_rows,
        "uncovered": {
            "calls": top_counter(uncovered_calls, 40),
            "member_paths": top_counter(uncovered_members, 40),
            "macros": top_counter(uncovered_macros, 40),
        },
    }


def write_json(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def write_row_csv(path: Path, data: dict[str, Any]) -> None:
    rows = []
    for row in data["rows"]:
        rows.append(
            {
                "readiness": row["readiness"],
                "oot3d_entry": row["oot3d_entry"],
                "oot3d_name": row["oot3d_name"],
                "n64_source": row["n64_source"],
                "n64_name": row["n64_name"],
                "status": row["status"],
                "domain": row["domain"],
                "n64_domain": row["n64_domain"],
                "text_hits": row["text_hits"],
                "source_lane_hits": row["source_lane_hits"],
                "distinct_text_adapters": row["distinct_text_adapters"],
                "high_confidence_hits": row["high_confidence_hits"],
                "port_file": row["port_file"],
                "n64_extract": row["n64_extract"],
                "top_adapters": " ".join(f"{item['id']}:{item['hits']}" for item in row["top_adapters"]),
            }
        )
    write_csv(
        path,
        rows,
        [
            "readiness",
            "oot3d_entry",
            "oot3d_name",
            "n64_source",
            "n64_name",
            "status",
            "domain",
            "n64_domain",
            "text_hits",
            "source_lane_hits",
            "distinct_text_adapters",
            "high_confidence_hits",
            "port_file",
            "n64_extract",
            "top_adapters",
        ],
    )


def write_adapter_csv(path: Path, data: dict[str, Any]) -> None:
    write_csv(
        path,
        data["adapters"],
        [
            "id",
            "category",
            "domain",
            "match_mode",
            "n64_pattern",
            "oot3d_symbol",
            "confidence",
            "hits",
            "rows",
            "text_hits",
            "source_lane_hits",
        ],
    )


def dict_counts(value: dict[str, Any]) -> str:
    return ", ".join(f"{key}: {count}" for key, count in value.items()) or "none"


def write_counter_table(lines: list[str], title: str, rows: list[dict[str, Any]]) -> None:
    lines.extend(["", f"## {title}", "", "| Name | Count |", "| --- | ---: |"])
    if not rows:
        lines.append("| - | 0 |")
    for row in rows:
        lines.append(f"| `{row['name']}` | {row['count']} |")


def write_markdown(path: Path, data: dict[str, Any], adapter_path: Path) -> None:
    summary = data["summary"]
    lines = [
        "# N64 Adapter Coverage",
        "",
        "Generated from `metadata/n64_port_map.csv`, extracted N64 source units, and the adapter table.",
        "",
        f"- Adapter table: `{rel(adapter_path)}`",
        f"- Adapter entries: {summary['adapter_entries']}",
        f"- Mapped rows scanned: {summary['mapped_rows']}",
        f"- Rows with text adapter hits: {summary['rows_with_text_hits']}",
        f"- Rows with source-lane hits: {summary['rows_with_source_lane_hits']}",
        f"- Total text adapter hits: {summary['total_text_hits']}",
        f"- Total source-lane hits: {summary['total_source_lane_hits']}",
        f"- Adapter categories: {dict_counts(summary['adapter_categories'])}",
        f"- Adapter confidence: {dict_counts(summary['adapter_confidence'])}",
        f"- Row readiness: {dict_counts(summary['row_readiness'])}",
        f"- Map statuses: {dict_counts(summary['map_statuses'])}",
        "",
        "## Batch Readiness",
        "",
        "| Readiness | OOT3D | N64 function | N64 domain | Status | Text hits | Distinct adapters | High-confidence hits | Top adapters |",
        "| --- | --- | --- | --- | --- | ---: | ---: | ---: | --- |",
    ]
    for row in data["rows"]:
        top = " ".join(f"`{item['id']}:{item['hits']}`" for item in row["top_adapters"][:5])
        lines.append(
            f"| `{row['readiness']}` | `{row['oot3d_entry']}` `{row['oot3d_name']}` | "
            f"`{row['n64_name']}` | `{row['n64_domain']}` | `{row['status']}` | {row['text_hits']} | "
            f"{row['distinct_text_adapters']} | {row['high_confidence_hits']} | {top} |"
        )

    lines.extend(
        [
            "",
            "## Top Adapter Hits",
            "",
            "| Adapter | Category | Domain | Confidence | Hits | Rows | OOT3D symbol |",
            "| --- | --- | --- | --- | ---: | ---: | --- |",
        ]
    )
    for row in data["adapters"][:40]:
        if int(row["hits"]) == 0:
            continue
        lines.append(
            f"| `{row['id']}` | `{row['category']}` | `{row['domain']}` | `{row['confidence']}` | "
            f"{row['hits']} | {row['rows']} | `{row['oot3d_symbol']}` |"
        )

    write_counter_table(lines, "Top Uncovered N64 Calls", data["uncovered"]["calls"])
    write_counter_table(lines, "Top Uncovered N64 Member Paths", data["uncovered"]["member_paths"])
    write_counter_table(lines, "Top Uncovered N64 Macros", data["uncovered"]["macros"])

    lines.extend(
        [
            "",
            "## Interpretation",
            "",
            "The adapter table turns the previous pattern report into a batch-auditable conversion layer.",
            "`batch-rich` and `adapter-ready` rows are the best next targets for direct C reconstruction because",
            "many of their N64 calls and member paths already have an OOT3D primitive. Uncovered calls and fields",
            "are the next symbols to name before trying to force compiler matching.",
            "",
        ]
    )

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--map", type=Path, default=ROOT / "metadata" / "n64_port_map.csv")
    parser.add_argument("--adapters", type=Path, default=ROOT / "metadata" / "n64_to_oot3d_adapters.csv")
    parser.add_argument("--n64-out-root", type=Path, default=ROOT / "analysis" / "n64_port_units")
    parser.add_argument("--out-json", type=Path, default=ROOT / "analysis" / "n64_adapter_coverage.json")
    parser.add_argument("--out-md", type=Path, default=ROOT / "analysis" / "n64_adapter_coverage.md")
    parser.add_argument("--out-rows-csv", type=Path, default=ROOT / "analysis" / "n64_adapter_coverage.csv")
    parser.add_argument(
        "--out-adapters-csv",
        type=Path,
        default=ROOT / "analysis" / "n64_adapter_hits.csv",
    )
    args = parser.parse_args()

    adapters = load_adapters(args.adapters)
    data = analyze(read_csv(args.map), adapters, args.n64_out_root)
    write_json(args.out_json, data)
    write_markdown(args.out_md, data, args.adapters)
    write_row_csv(args.out_rows_csv, data)
    write_adapter_csv(args.out_adapters_csv, data)

    summary = data["summary"]
    print(
        f"scanned {summary['mapped_rows']} mapped rows with {summary['adapter_entries']} adapters; "
        f"{summary['rows_with_text_hits']} rows have text hits; "
        f"{summary['total_text_hits']} text hits and {summary['total_source_lane_hits']} source-lane hits"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
