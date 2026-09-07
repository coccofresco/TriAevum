#!/usr/bin/env python3
"""Materialize direct N64->OOT3D workorders into rewritten source packets.

The generated files are not build inputs. They are batch conversion packets:
N64 extracts with recovered OOT3D callees, data packets, and direct-field
placeholders applied mechanically so a lane can be ported as a group.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import shutil
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
STRING_OR_COMMENT_RE = re.compile(
    r"/\*.*?\*/|//[^\r\n]*|\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'",
    re.DOTALL,
)


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


def short_slug(value: str, limit: int = 54) -> str:
    clean = slug(value)
    digest = hashlib.sha1(value.encode("utf-8")).hexdigest()[:8]
    if len(clean) <= limit:
        return clean
    return f"{clean[:limit].rstrip('_')}_{digest}"


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def read_json(path: Path, default: Any) -> Any:
    if not path.is_file():
        return default
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def write_csv(path: Path, rows: list[dict[str, Any]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fieldnames})


def load_adapters(path: Path) -> dict[str, Adapter]:
    adapters: dict[str, Adapter] = {}
    for row in read_csv(path):
        adapter_id = row.get("id", "").strip()
        if not adapter_id:
            continue
        adapters[adapter_id] = Adapter(
            id=adapter_id,
            category=row.get("category", "").strip(),
            domain=row.get("domain", "").strip(),
            match_mode=row.get("match_mode", "").strip(),
            n64_pattern=row.get("n64_pattern", "").strip(),
            oot3d_symbol=row.get("oot3d_symbol", "").strip(),
            confidence=row.get("confidence", "").strip(),
            notes=row.get("notes", "").strip(),
        )
    return adapters


def load_shape_anchors(path: Path) -> dict[str, list[dict[str, str]]]:
    anchors: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in read_csv(path):
        entry = str(row.get("oot3d_entry", "")).lower()
        if not entry:
            continue
        anchors[entry].append(
            {
                "family": row.get("family", ""),
                "source_base": row.get("source_base", ""),
                "anchor_name": row.get("anchor_name", ""),
                "target_base": row.get("target_base", ""),
                "target_access_count": row.get("target_access_count", ""),
                "source_rewrite_count": row.get("source_rewrite_count", ""),
                "candidate_declaration": row.get("candidate_declaration", ""),
                "rewrite_examples": row.get("rewrite_examples", ""),
                "action": row.get("action", ""),
            }
        )
    for rows in anchors.values():
        rows.sort(key=lambda item: -(int(item.get("source_rewrite_count", 0) or 0) * 10 + int(item.get("target_access_count", 0) or 0)))
    return anchors


def source_chunks(text: str) -> list[tuple[bool, str]]:
    chunks: list[tuple[bool, str]] = []
    cursor = 0
    for match in STRING_OR_COMMENT_RE.finditer(text):
        if match.start() > cursor:
            chunks.append((True, text[cursor : match.start()]))
        chunks.append((False, match.group(0)))
        cursor = match.end()
    if cursor < len(text):
        chunks.append((True, text[cursor:]))
    return chunks


def replacement_expr(adapter: Adapter) -> str:
    if adapter.match_mode == "member":
        base = adapter.n64_pattern.split("->", 1)[0]
        return f"OOT3D_DIRECT_FIELD({base}, {adapter.oot3d_symbol})"
    return adapter.oot3d_symbol


def adapter_regex(adapter: Adapter) -> re.Pattern[str] | None:
    if adapter.match_mode in {"map-n64-name", "map-n64-source"}:
        return None
    if adapter.match_mode == "regex":
        return re.compile(adapter.n64_pattern)
    if adapter.match_mode == "token":
        return re.compile(rf"\b{re.escape(adapter.n64_pattern)}\b")
    if adapter.match_mode == "member":
        return re.compile(rf"(?<![A-Za-z0-9_]){re.escape(adapter.n64_pattern)}(?![A-Za-z0-9_\.])")
    return re.compile(re.escape(adapter.n64_pattern))


def apply_adapter(text: str, adapter: Adapter) -> tuple[str, int]:
    pattern = adapter_regex(adapter)
    if pattern is None or not adapter.n64_pattern or not adapter.oot3d_symbol:
        return text, 0

    replacement = replacement_expr(adapter)
    total = 0
    rewritten: list[str] = []
    for is_code, chunk in source_chunks(text):
        if not is_code:
            rewritten.append(chunk)
            continue
        chunk, count = pattern.subn(replacement, chunk)
        total += count
        rewritten.append(chunk)
    return "".join(rewritten), total


def packet_header(workorder: dict[str, Any], target: dict[str, Any], source_path: Path) -> str:
    return "\n".join(
        [
            "/*",
            " * Generated direct conversion materialization.",
            " * This file is analysis input for batch porting, not maintained build source.",
            f" * Workorder: {workorder['lane']}",
            f" * Target: {target['entry']} {target['name']}",
            f" * N64 extract: {rel(source_path)}",
            " */",
            "",
            "#ifndef OOT3D_DIRECT_FIELD",
            "#define OOT3D_DIRECT_FIELD(base, offset) /* direct-field: base + offset */ (base)",
            "#endif",
            "",
        ]
    )


def target_workorder_rewrites(workorder: dict[str, Any], adapters: dict[str, Adapter]) -> list[Adapter]:
    ordered: list[Adapter] = []
    seen: set[str] = set()
    for rewrite in workorder.get("rewrites", []):
        adapter_id = str(rewrite.get("adapter", ""))
        adapter = adapters.get(adapter_id)
        if adapter is None or adapter_id in seen:
            continue
        seen.add(adapter_id)
        ordered.append(adapter)
    return ordered


def materialize_target(
    workorder: dict[str, Any],
    target: dict[str, Any],
    adapters: list[Adapter],
    shape_anchors: list[dict[str, str]],
    out_dir: Path,
) -> dict[str, Any]:
    source_path = ROOT / str(target.get("n64_extract", ""))
    target_dir = out_dir / str(target["entry"])
    target_dir.mkdir(parents=True, exist_ok=True)
    output_c = target_dir / f"{target['entry']}.direct.c"
    report_json = target_dir / "rewrite_report.json"
    report_md = target_dir / "rewrite_report.md"

    if not source_path.is_file():
        report = {
            "entry": target.get("entry", ""),
            "name": target.get("name", ""),
            "status": "missing-source",
            "source": rel(source_path),
            "output": rel(output_c),
            "rewrite_hits": [],
        }
        if shape_anchors:
            report["shape_anchors"] = shape_anchors
        write_json(report_json, report)
        return report

    original = source_path.read_text(encoding="utf-8", errors="replace")
    rewritten = original
    hits: list[dict[str, Any]] = []
    for adapter in adapters:
        rewritten, count = apply_adapter(rewritten, adapter)
        if count:
            hits.append(
                {
                    "adapter": adapter.id,
                    "category": adapter.category,
                    "match_mode": adapter.match_mode,
                    "n64_pattern": adapter.n64_pattern,
                    "oot3d_symbol": adapter.oot3d_symbol,
                    "count": count,
                    "confidence": adapter.confidence,
                }
            )

    output_c.write_text(packet_header(workorder, target, source_path) + rewritten, encoding="utf-8")
    report = {
        "entry": target.get("entry", ""),
        "name": target.get("name", ""),
        "status": "ok",
        "source": rel(source_path),
        "output": rel(output_c),
        "rewrite_count": sum(int(item["count"]) for item in hits),
        "adapter_count": len(hits),
        "rewrite_hits": hits,
    }
    if shape_anchors:
        report["shape_anchors"] = shape_anchors
    write_json(report_json, report)
    write_target_report(report_md, report)
    return report


def write_target_report(path: Path, report: dict[str, Any]) -> None:
    lines = [
        f"# Rewrite Report: {report['entry']} {report['name']}",
        "",
        f"- Status: `{report['status']}`",
        f"- Source: `{report['source']}`",
        f"- Output: `{report['output']}`",
        f"- Rewrite hits: `{report.get('rewrite_count', 0)}`",
        f"- Adapters hit: `{report.get('adapter_count', 0)}`",
        "",
        "| Adapter | Category | Count | N64 pattern | OOT3D symbol |",
        "| --- | --- | ---: | --- | --- |",
    ]
    for item in report.get("rewrite_hits", []):
        lines.append(
            f"| `{item['adapter']}` | `{item['category']}` | {item['count']} | "
            f"`{item['n64_pattern']}` | `{item['oot3d_symbol']}` |"
        )
    anchors = report.get("shape_anchors", [])
    if anchors:
        lines.extend(
            [
                "",
                "## Target Shape Anchors",
                "",
                "| Family | Declaration | Target uses | Source rewrites | Examples | Action |",
                "| --- | --- | ---: | ---: | --- | --- |",
            ]
        )
        for anchor in anchors[:8]:
            lines.append(
                f"| `{anchor['family']}` | `{anchor['candidate_declaration']}` | "
                f"{anchor['target_access_count']} | {anchor['source_rewrite_count']} | "
                f"`{anchor['rewrite_examples']}` | `{anchor['action']}` |"
            )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def is_template_target(target: dict[str, Any]) -> bool:
    return str(target.get("conversion_mode", "")) == "exact-template" or str(target.get("status", "")) == "matched-c"


def selected_targets(workorder: dict[str, Any], args: argparse.Namespace) -> list[dict[str, Any]]:
    targets = list(workorder.get("targets", []))
    if args.templates_only:
        return [target for target in targets if is_template_target(target)]
    return targets


def write_lane_report(path: Path, workorder: dict[str, Any], reports: list[dict[str, Any]]) -> None:
    lines = [
        f"# Materialized Direct Lane: {workorder['lane']}",
        "",
        f"- Domain: `{workorder['domain']}`",
        f"- Priority: `{workorder['priority']}`",
        f"- Targets: `{len(reports)}`",
        f"- Rewrite hits: `{sum(int(report.get('rewrite_count', 0)) for report in reports)}`",
        f"- Shape anchors: `{sum(len(report.get('shape_anchors', [])) for report in reports)}`",
        "",
        "| Target | Status | Rewrites | Adapters | Shape anchors | Output |",
        "| --- | --- | ---: | ---: | ---: | --- |",
    ]
    for report in reports:
        lines.append(
            f"| `{report['entry']}` `{report['name']}` | `{report['status']}` | "
            f"{report.get('rewrite_count', 0)} | {report.get('adapter_count', 0)} | "
            f"{len(report.get('shape_anchors', []))} | `{report['output']}` |"
        )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def should_materialize(workorder: dict[str, Any], args: argparse.Namespace) -> bool:
    if args.lane and str(workorder.get("lane")) != args.lane:
        return False
    if args.domain and str(workorder.get("domain")) != args.domain:
        return False
    if args.candidates_only and int(workorder.get("candidate_rows", 0) or 0) == 0:
        return False
    if args.templates_only and int(workorder.get("matched_templates", 0) or 0) == 0:
        return False
    return True


def analyze(args: argparse.Namespace) -> dict[str, Any]:
    index = read_json(args.workorders, {})
    workorders = index.get("workorders", []) if isinstance(index, dict) else []
    adapters = load_adapters(args.adapters)
    shape_anchors_by_entry = load_shape_anchors(args.shape_anchors)
    args.out_dir.mkdir(parents=True, exist_ok=True)

    all_lane_reports: list[dict[str, Any]] = []
    for old in args.out_dir.glob("*"):
        if old.is_file():
            old.unlink()
        elif old.is_dir():
            shutil.rmtree(old)
    for workorder in workorders:
        if not should_materialize(workorder, args):
            continue
        lane_slug = short_slug(str(workorder.get("lane", "")))
        lane_dir = args.out_dir / str(lane_slug)
        lane_dir.mkdir(parents=True, exist_ok=True)
        rewrite_adapters = target_workorder_rewrites(workorder, adapters)
        targets = selected_targets(workorder, args)
        reports = [
            materialize_target(
                workorder,
                target,
                rewrite_adapters,
                shape_anchors_by_entry.get(str(target.get("entry", "")).lower(), []),
                lane_dir,
            )
            for target in targets
        ]
        write_lane_report(lane_dir / "README.md", workorder, reports)
        all_lane_reports.append(
            {
                "lane": workorder["lane"],
                "slug": lane_slug,
                "domain": workorder["domain"],
                "priority": workorder["priority"],
                "targets": len(reports),
                "rewrite_hits": sum(int(report.get("rewrite_count", 0)) for report in reports),
                "adapter_hits": sum(int(report.get("adapter_count", 0)) for report in reports),
                "shape_anchors": sum(len(report.get("shape_anchors", [])) for report in reports),
                "shape_rewrite_sites": sum(
                    sum(int(anchor.get("source_rewrite_count", 0) or 0) for anchor in report.get("shape_anchors", []))
                    for report in reports
                ),
                "readme": rel(lane_dir / "README.md"),
            }
        )

    summary = {
        "lanes": len(all_lane_reports),
        "targets": sum(int(row["targets"]) for row in all_lane_reports),
        "rewrite_hits": sum(int(row["rewrite_hits"]) for row in all_lane_reports),
        "adapter_hits": sum(int(row["adapter_hits"]) for row in all_lane_reports),
        "shape_anchors": sum(int(row["shape_anchors"]) for row in all_lane_reports),
        "shape_rewrite_sites": sum(int(row["shape_rewrite_sites"]) for row in all_lane_reports),
    }
    return {"summary": summary, "lanes": all_lane_reports}


def write_index(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Materialized Direct Conversion Workorders",
        "",
        "Generated rewritten source packets for batch N64->OOT3D porting.",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Lanes | {summary['lanes']} |",
        f"| Targets | {summary['targets']} |",
        f"| Rewrite hits | {summary['rewrite_hits']} |",
        f"| Adapter-hit rows | {summary['adapter_hits']} |",
        f"| Shape anchors | {summary['shape_anchors']} |",
        f"| Shape rewrite sites | {summary['shape_rewrite_sites']} |",
        "",
        "| Lane | Domain | Priority | Targets | Rewrites | Shape anchors | Packet |",
        "| --- | --- | ---: | ---: | ---: | ---: | --- |",
    ]
    for row in data["lanes"]:
        lines.append(
            f"| `{row['lane']}` | `{row['domain']}` | {row['priority']} | {row['targets']} | "
            f"{row['rewrite_hits']} | {row['shape_anchors']} | `{row['readme']}` |"
        )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--workorders",
        type=Path,
        default=ROOT / "analysis" / "direct_conversion_workorders" / "index.json",
    )
    parser.add_argument("--adapters", type=Path, default=ROOT / "metadata" / "n64_to_oot3d_adapters.csv")
    parser.add_argument("--shape-anchors", type=Path, default=ROOT / "analysis" / "direct_shape_anchor_windows.csv")
    parser.add_argument("--out-dir", type=Path, default=ROOT / "analysis" / "direct_conversion_materialized")
    parser.add_argument("--lane", default="", help="materialize only one exact N64 lane name")
    parser.add_argument("--domain", default="", help="materialize only one domain")
    parser.add_argument("--candidates-only", action="store_true", help="skip template-only lanes")
    parser.add_argument("--templates-only", action="store_true", help="materialize only exact matched-template targets")
    args = parser.parse_args()
    if args.candidates_only and args.templates_only:
        parser.error("--candidates-only and --templates-only cannot be used together")

    data = analyze(args)
    write_json(args.out_dir / "index.json", data)
    write_csv(
        args.out_dir / "index.csv",
        data["lanes"],
        [
            "lane",
            "slug",
            "domain",
            "priority",
            "targets",
            "rewrite_hits",
            "adapter_hits",
            "shape_anchors",
            "shape_rewrite_sites",
            "readme",
        ],
    )
    write_index(args.out_dir / "index.md", data)

    summary = data["summary"]
    print(
        "materialized direct workorders: "
        f"{summary['lanes']} lanes, {summary['targets']} targets, "
        f"{summary['rewrite_hits']} rewrites, {summary['shape_anchors']} shape anchors"
    )
    print(rel(args.out_dir / "index.md"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
