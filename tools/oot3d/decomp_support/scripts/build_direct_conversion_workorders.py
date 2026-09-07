#!/usr/bin/env python3
"""Build executable workorders for direct N64->OOT3D source conversion.

The structure/recipe miners answer what looks reusable.  This script turns
that evidence into per-source-lane workorders: target rows to handle together,
adapter rewrites to apply, exact templates to copy from, source/target packet
paths, and verification commands.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]

STAGE_BY_CATEGORY = {
    "struct-field": "field-lowering",
    "engine-call": "call-lowering",
    "subsystem-call": "subsystem-lowering",
    "subsystem-flow": "subsystem-lowering",
    "data-symbol": "data-packet",
    "source-lane": "lane-slicing",
}

LANE_MODE_WEIGHT = {
    "source-lane-slicing": 600,
    "direct-source-lowering": 520,
    "exact-seed-replacement": 500,
    "helper-to-direct-lowering": 460,
    "adapter-completion": 420,
    "exact-template": 120,
}

STATUS_WEIGHT = {
    "structured-port-started": 120,
    "exact-seed-started": 90,
    "matched-c": 15,
}

READINESS_WEIGHT = {
    "batch-lane-ready": 220,
    "template-backed": 190,
    "rule-backed": 170,
    "high": 160,
    "medium": 120,
    "adapter-ready": 110,
    "batch-rich": 110,
    "partial": 70,
    "low": 35,
}


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
            writer.writerow({field: row.get(field, "") for field in fieldnames})


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def int_value(row: dict[str, Any], key: str) -> int:
    try:
        return int(row.get(key, 0) or 0)
    except ValueError:
        return 0


def float_value(row: dict[str, Any], key: str) -> float:
    try:
        return float(row.get(key, 0) or 0)
    except ValueError:
        return 0.0


def parse_counts(value: str) -> Counter[str]:
    counts: Counter[str] = Counter()
    for part in str(value or "").split():
        if ":" not in part:
            continue
        name, raw_count = part.rsplit(":", 1)
        try:
            counts[name] += int(raw_count)
        except ValueError:
            continue
    return counts


def format_counts(counter: Counter[str], limit: int = 10) -> str:
    return " ".join(f"{name}:{count}" for name, count in counter.most_common(limit))


def parse_entry_tokens(value: str) -> list[dict[str, str]]:
    entries_by_key: dict[tuple[str, str], dict[str, str]] = {}
    for token in str(value or "").split():
        parts = token.split(":")
        if len(parts) < 2:
            continue
        entry, name = parts[0], parts[1]
        score = parts[2] if len(parts) > 2 else ""
        key = (entry, name)
        current = entries_by_key.get(key)
        if current is None:
            entries_by_key[key] = {"entry": entry, "name": name, "score": "" if score == "self" else score}
            continue
        if score == "self":
            continue
        try:
            if int(score) > int(current.get("score") or 0):
                current["score"] = score
        except ValueError:
            if not current.get("score"):
                current["score"] = score
    return sorted(entries_by_key.values(), key=lambda item: (item["entry"], item["name"]))


def adapter_index(rows: list[dict[str, str]]) -> dict[str, dict[str, str]]:
    return {row["id"]: row for row in rows if row.get("id")}


def gate_index(rows: list[dict[str, str]]) -> dict[str, dict[str, str]]:
    index: dict[str, dict[str, str]] = {}
    for row in rows:
        entry = str(row.get("oot3d_entry") or row.get("OOT3D") or "").lower()
        if entry:
            index[entry] = row
    return index


def unit_index(rows: list[dict[str, str]]) -> tuple[dict[str, dict[str, str]], dict[str, dict[str, str]]]:
    by_source: dict[str, dict[str, str]] = {}
    by_port_file: dict[str, dict[str, str]] = {}
    for row in rows:
        if row.get("source"):
            by_source[row["source"]] = row
        if row.get("port_file"):
            by_port_file[row["port_file"]] = row
    return by_source, by_port_file


def row_domain(row: dict[str, Any]) -> str:
    domain = str(row.get("domain") or "")
    if domain and ":" not in domain:
        return domain
    lane = str(row.get("n64_lane") or "")
    if "Boss_Va" in lane:
        return "boss_va"
    if "Boss_Mo" in lane:
        return "boss_mo"
    if "player" in lane.lower():
        return "player"
    if "message" in lane.lower():
        return "message"
    if "kaleido" in lane.lower() or "construct" in lane.lower():
        return "pause"
    return "other"


def row_score(row: dict[str, Any]) -> int:
    mode = str(row.get("conversion_mode", ""))
    readiness = str(row.get("automation_readiness", ""))
    status = str(row.get("status", ""))
    return (
        int_value(row, "mechanical_score")
        + LANE_MODE_WEIGHT.get(mode, 0)
        + READINESS_WEIGHT.get(readiness, 0)
        + STATUS_WEIGHT.get(status, 0)
    )


def lane_score(rows: list[dict[str, Any]], lane: dict[str, Any]) -> int:
    if not rows:
        return 0
    total = max(row_score(row) for row in rows)
    candidate_count = sum(1 for row in rows if row.get("status") != "matched-c")
    exact_count = sum(1 for row in rows if row.get("status") == "matched-c")
    split_bonus = 250 if len(rows) > 1 and candidate_count else 0
    template_bonus = 220 if lane.get("templates") else 0
    return total + candidate_count * 180 + exact_count * 35 + split_bonus + template_bonus


def build_rewrites(rows: list[dict[str, Any]], adapters: dict[str, dict[str, str]]) -> list[dict[str, Any]]:
    counts: Counter[str] = Counter()
    for row in rows:
        counts.update(parse_counts(str(row.get("top_adapters", ""))))

    rewrites: list[dict[str, Any]] = []
    for adapter_id, hits in counts.most_common():
        meta = adapters.get(adapter_id)
        if not meta:
            continue
        stage = STAGE_BY_CATEGORY.get(meta.get("category", ""), "manual")
        action = "rewrite"
        if stage == "field-lowering":
            action = "emit direct offset accessor"
        elif stage == "call-lowering":
            action = "promote callee prototype/ABI wrapper"
        elif stage == "data-packet":
            action = "name target state packet"
        elif stage == "lane-slicing":
            action = "slice converted lane by target footprint"
        elif stage == "subsystem-lowering":
            action = "lower domain helper to target packet"
        rewrites.append(
            {
                "adapter": adapter_id,
                "stage": stage,
                "category": meta.get("category", ""),
                "domain": meta.get("domain", ""),
                "confidence": meta.get("confidence", ""),
                "hits": hits,
                "n64_pattern": meta.get("n64_pattern", ""),
                "oot3d_symbol": meta.get("oot3d_symbol", ""),
                "action": action,
                "notes": meta.get("notes", ""),
            }
        )
    return rewrites


def group_by_stage(rewrites: list[dict[str, Any]]) -> list[dict[str, Any]]:
    grouped: dict[str, dict[str, Any]] = {}
    for rewrite in rewrites:
        stage = rewrite["stage"]
        item = grouped.setdefault(stage, {"stage": stage, "hits": 0, "rewrites": []})
        item["hits"] += int(rewrite["hits"])
        item["rewrites"].append(rewrite)
    return sorted(grouped.values(), key=lambda item: (-int(item["hits"]), item["stage"]))


def verification_commands(rows: list[dict[str, Any]], unit_by_source: dict[str, dict[str, str]]) -> list[str]:
    commands: list[str] = []
    seen: set[str] = set()
    for row in rows:
        unit = unit_by_source.get(str(row.get("port_file", "")))
        if not unit:
            continue
        name = unit.get("unit", "")
        if not name or name in seen:
            continue
        seen.add(name)
        commands.append(
            f"powershell -NoProfile -ExecutionPolicy Bypass -File scripts\\refresh-structured-port-units.ps1 -Unit {name} -SkipDefaultBuild"
        )
    commands.extend(
        [
            "python scripts\\structured_c_match_gate.py --skip-build",
            "powershell -NoProfile -ExecutionPolicy Bypass -File scripts\\build-matched-objects.ps1",
            "python scripts\\report_porting_metrics.py",
        ]
    )
    return commands


def work_actions(lane: dict[str, Any], rows: list[dict[str, Any]], rewrites: list[dict[str, Any]]) -> list[str]:
    modes = {str(row.get("conversion_mode", "")) for row in rows}
    actions: list[str] = []
    if "source-lane-slicing" in modes:
        actions.append("Convert the N64 lane once, then split by target offsets, FUN calls, DAT refs, and entry size.")
    if "direct-source-lowering" in modes:
        actions.append("Lower helper-heavy structured C into direct offset loads/stores before profile search.")
    if "exact-seed-replacement" in modes:
        actions.append("Replace exact-seed blocks with N64 control flow only after field/call/data rewrites are named.")
    if "adapter-completion" in modes:
        actions.append("Complete missing high-hit adapters before attempting another C match pass.")
    if any(rewrite["stage"] == "animation-abi" for rewrite in rewrites):
        actions.append("Use resolved hard-float Animation_Change packets before changing compiler profiles.")
    if not actions:
        strategy = str(lane.get("batch_strategy", "Apply adapter rewrites and rerun the gate."))
        actions.append(strategy)
    actions.append("After each source edit, run the lane refresh, structured gate, full matched build, and metrics report.")
    return actions


def target_rows(rows: list[dict[str, Any]], gate: dict[str, dict[str, str]]) -> list[dict[str, Any]]:
    output: list[dict[str, Any]] = []
    for row in sorted(rows, key=lambda item: (-row_score(item), item.get("oot3d_entry", ""))):
        entry = str(row.get("oot3d_entry", "")).lower()
        gate_row = gate.get(entry, {})
        output.append(
            {
                "entry": row.get("oot3d_entry", ""),
                "name": row.get("oot3d_name", ""),
                "n64_name": str(row.get("n64_lane", "")).split("::")[-1],
                "status": row.get("status", ""),
                "conversion_mode": row.get("conversion_mode", ""),
                "automation_readiness": row.get("automation_readiness", ""),
                "score": row_score(row),
                "port_file": row.get("port_file", ""),
                "n64_extract": row.get("n64_extract", ""),
                "target_offsets": row.get("evidence", ""),
                "top_adapters": row.get("top_adapters", ""),
                "closest_templates": row.get("closest_templates", ""),
                "gate_category": gate_row.get("category", ""),
                "gate_lcs": gate_row.get("lcs_instruction_count", ""),
                "gate_prefix": gate_row.get("matching_prefix", ""),
                "gate_run": gate_row.get("longest_common_run", ""),
            }
        )
    return output


def workorder_for_lane(
    lane: dict[str, Any],
    rows: list[dict[str, Any]],
    adapters: dict[str, dict[str, str]],
    gate: dict[str, dict[str, str]],
    unit_by_source: dict[str, dict[str, str]],
) -> dict[str, Any]:
    rewrites = build_rewrites(rows, adapters)
    score = lane_score(rows, lane)
    candidate_rows = [row for row in rows if row.get("status") != "matched-c"]
    exact_rows = [row for row in rows if row.get("status") == "matched-c"]
    template_tokens = parse_entry_tokens(str(lane.get("templates", "")))
    return {
        "lane": lane["n64_lane"],
        "slug": slug(lane["n64_lane"]),
        "domain": row_domain(lane),
        "priority": score,
        "readiness": lane.get("readiness", ""),
        "rows": len(rows),
        "candidate_rows": len(candidate_rows),
        "matched_templates": len(exact_rows),
        "structures": lane.get("replicable_structures", ""),
        "top_adapters": lane.get("top_adapters", ""),
        "templates": template_tokens,
        "targets": target_rows(rows, gate),
        "rewrite_stages": group_by_stage(rewrites),
        "rewrites": rewrites,
        "actions": work_actions(lane, rows, rewrites),
        "verification_commands": verification_commands(rows, unit_by_source),
    }


def batch_sort_key(workorder: dict[str, Any]) -> tuple[int, int, int, str]:
    batch_ready = 1 if workorder["candidate_rows"] and workorder["rows"] > 1 else 0
    template_backed = 1 if workorder["candidate_rows"] and workorder["templates"] else 0
    return (-batch_ready, -template_backed, -int(workorder["priority"]), str(workorder["lane"]))


def write_workorder_markdown(path: Path, workorder: dict[str, Any]) -> None:
    lines: list[str] = [
        f"# Direct Conversion Workorder: {workorder['lane']}",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Priority | {workorder['priority']} |",
        f"| Domain | `{workorder['domain']}` |",
        f"| Readiness | `{workorder['readiness']}` |",
        f"| Rows | {workorder['rows']} |",
        f"| Candidate rows | {workorder['candidate_rows']} |",
        f"| Matched templates | {workorder['matched_templates']} |",
        "",
        "## Batch Shape",
        "",
        f"- Structures: `{workorder['structures']}`",
        f"- Top adapters: `{workorder['top_adapters']}`",
        "",
        "## Targets",
        "",
        "| OOT3D | Status | Mode | Score | Gate | LCS | Input |",
        "| --- | --- | --- | ---: | --- | ---: | --- |",
    ]
    for target in workorder["targets"]:
        gate = target.get("gate_category") or ""
        lcs = target.get("gate_lcs") or ""
        lines.append(
            f"| `{target['entry']}` `{target['name']}` | `{target['status']}` | "
            f"`{target['conversion_mode']}` | {target['score']} | `{gate}` | {lcs} | "
            f"`{target['n64_extract']}` |"
        )

    lines.extend(["", "## Exact Templates", ""])
    if workorder["templates"]:
        lines.extend(["| Entry | Name | Score |", "| --- | --- | ---: |"])
        for template in workorder["templates"]:
            lines.append(f"| `{template['entry']}` | `{template['name']}` | {template['score']} |")
    else:
        lines.append("- None recorded for this lane.")

    lines.extend(["", "## Rewrite Plan", ""])
    for stage in workorder["rewrite_stages"]:
        lines.append(f"### {stage['stage']}")
        lines.append("")
        lines.append("| Adapter | Hits | N64 pattern | OOT3D shape | Action |")
        lines.append("| --- | ---: | --- | --- | --- |")
        for rewrite in stage["rewrites"][:18]:
            lines.append(
                f"| `{rewrite['adapter']}` | {rewrite['hits']} | `{rewrite['n64_pattern']}` | "
                f"`{rewrite['oot3d_symbol']}` | {rewrite['action']} |"
            )
        lines.append("")

    lines.extend(["## Actions", ""])
    for action in workorder["actions"]:
        lines.append(f"- {action}")

    lines.extend(["", "## Verification Commands", ""])
    for command in workorder["verification_commands"]:
        lines.append(f"- `{command}`")

    while lines and not lines[-1]:
        lines.pop()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_index_markdown(path: Path, summary: dict[str, Any], workorders: list[dict[str, Any]]) -> None:
    lines: list[str] = [
        "# Direct Conversion Workorders",
        "",
        "Per-lane workorders generated from the direct conversion blueprints.  These are the batch-first inputs for porting N64 source into OOT3D-shaped C.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
    ]
    for key, value in summary.items():
        lines.append(f"| {key.replace('_', ' ').title()} | {value} |")
    lines.extend(
        [
            "",
            "## Queue",
            "",
            "| Rank | Lane | Domain | Priority | Candidates | Templates | Structures | Workorder |",
            "| ---: | --- | --- | ---: | ---: | ---: | --- | --- |",
        ]
    )
    for rank, workorder in enumerate(workorders, start=1):
        md_path = f"analysis/direct_conversion_workorders/{workorder['slug']}.md"
        lines.append(
            f"| {rank} | `{workorder['lane']}` | `{workorder['domain']}` | {workorder['priority']} | "
            f"{workorder['candidate_rows']} | {workorder['matched_templates']} | "
            f"`{workorder['structures']}` | `{md_path}` |"
        )

    lines.extend(
        [
            "",
            "## Use",
            "",
            "1. Pick the highest-ranked lane with candidate rows.",
            "2. Apply the lane rewrite plan to the N64 extract, preserving the exact-template direct-offset style.",
            "3. Edit the maintained C source or create a structured source unit for the lane targets.",
            "4. Run the workorder verification commands and promote only exact C rows.",
        ]
    )

    while lines and not lines[-1]:
        lines.pop()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def analyze(args: argparse.Namespace) -> dict[str, Any]:
    function_rows = read_csv(args.function_blueprints)
    lane_rows = read_csv(args.lane_blueprints)
    adapters = adapter_index(read_csv(args.adapters))
    gate = gate_index(read_csv(args.structured_gate_csv))
    unit_by_source, _unit_by_port = unit_index(read_csv(args.structured_units))

    rows_by_lane: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in function_rows:
        lane_name = row.get("n64_lane", "")
        if lane_name:
            rows_by_lane[lane_name].append(row)

    lane_by_name = {row["n64_lane"]: row for row in lane_rows if row.get("n64_lane")}
    for lane_name, rows in rows_by_lane.items():
        lane_by_name.setdefault(
            lane_name,
            {
                "n64_lane": lane_name,
                "domain": row_domain(rows[0]) if rows else "other",
                "readiness": "low",
                "replicable_structures": "",
                "top_adapters": "",
                "templates": "",
                "batch_strategy": "Apply adapter rewrites and re-run the structured gate.",
            },
        )

    workorders = [
        workorder_for_lane(lane_by_name[lane_name], rows, adapters, gate, unit_by_source)
        for lane_name, rows in rows_by_lane.items()
    ]
    workorders.sort(key=batch_sort_key)

    summary = {
        "lanes": len(workorders),
        "candidate_lanes": sum(1 for item in workorders if item["candidate_rows"]),
        "targets": sum(item["rows"] for item in workorders),
        "candidate_targets": sum(item["candidate_rows"] for item in workorders),
        "matched_template_targets": sum(item["matched_templates"] for item in workorders),
        "batch_ready_lanes": sum(1 for item in workorders if item["candidate_rows"] and item["rows"] > 1),
        "template_backed_lanes": sum(1 for item in workorders if item["candidate_rows"] and item["templates"]),
    }
    return {"summary": summary, "workorders": workorders}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--function-blueprints",
        type=Path,
        default=ROOT / "analysis" / "direct_conversion_function_blueprints.csv",
    )
    parser.add_argument(
        "--lane-blueprints",
        type=Path,
        default=ROOT / "analysis" / "direct_conversion_lane_blueprints.csv",
    )
    parser.add_argument("--adapters", type=Path, default=ROOT / "metadata" / "n64_to_oot3d_adapters.csv")
    parser.add_argument("--structured-gate-csv", type=Path, default=ROOT / "analysis" / "structured_c_match_gate.csv")
    parser.add_argument("--structured-units", type=Path, default=ROOT / "metadata" / "structured_port_units.csv")
    parser.add_argument("--out-dir", type=Path, default=ROOT / "analysis" / "direct_conversion_workorders")
    args = parser.parse_args()

    data = analyze(args)
    out_dir: Path = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    for old_file in out_dir.glob("*.json"):
        old_file.unlink()
    for old_file in out_dir.glob("*.md"):
        old_file.unlink()

    for workorder in data["workorders"]:
        stem = workorder["slug"]
        write_json(out_dir / f"{stem}.json", workorder)
        write_workorder_markdown(out_dir / f"{stem}.md", workorder)

    index_rows = []
    for rank, workorder in enumerate(data["workorders"], start=1):
        index_rows.append(
            {
                "rank": rank,
                "lane": workorder["lane"],
                "domain": workorder["domain"],
                "priority": workorder["priority"],
                "readiness": workorder["readiness"],
                "rows": workorder["rows"],
                "candidate_rows": workorder["candidate_rows"],
                "matched_templates": workorder["matched_templates"],
                "structures": workorder["structures"],
                "workorder_md": rel(out_dir / f"{workorder['slug']}.md"),
                "workorder_json": rel(out_dir / f"{workorder['slug']}.json"),
            }
        )

    write_csv(
        out_dir / "index.csv",
        index_rows,
        [
            "rank",
            "lane",
            "domain",
            "priority",
            "readiness",
            "rows",
            "candidate_rows",
            "matched_templates",
            "structures",
            "workorder_md",
            "workorder_json",
        ],
    )
    write_json(out_dir / "index.json", data)
    write_index_markdown(out_dir / "index.md", data["summary"], data["workorders"])

    summary = data["summary"]
    print(
        "direct workorders: "
        f"{summary['candidate_lanes']} candidate lanes, "
        f"{summary['candidate_targets']} candidate targets, "
        f"{summary['batch_ready_lanes']} batch-ready lanes"
    )
    print(rel(out_dir / "index.md"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
