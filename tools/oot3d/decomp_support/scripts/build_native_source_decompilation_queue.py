#!/usr/bin/env python3
"""Build an OOT3D-first native source decompilation queue.

This queue is intentionally narrower than the broad N64 porting rankers. It
prioritizes targets that already have OOT3D evidence: code.bin/Ghidra target
windows, maintained source lanes, compiled probes, structured gates, and
materialized target-shaped C. N64 source metadata is kept as secondary semantic
context only.
"""

from __future__ import annotations

import csv
import json
from collections import Counter
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_FRONTIER = ROOT / "analysis" / "c_reconstruction_frontier.csv"
DEFAULT_BLOCKERS = ROOT / "analysis" / "direct_frontier_blocker_synthesis.csv"
DEFAULT_RECIPES = ROOT / "analysis" / "direct_source_conversion_recipes.csv"
DEFAULT_UNITS = ROOT / "metadata" / "structured_port_units.csv"
DEFAULT_OUT_JSON = ROOT / "analysis" / "native_source_decompilation_queue.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "native_source_decompilation_queue.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "native_source_decompilation_queue.md"


BLOCKER_ORDER = {
    "structured-c-lowering-ready": 0,
    "inline-c-lowering-needed": 20,
    "compiled-packet-semantic-started": 40,
    "target-envelope-too-large": 60,
    "state-mode-workorder-branch-exhausted": 80,
}

STATUS_ORDER = {
    "structured-c-needs-lowering": 0,
    "inline-c-needs-lowering": 10,
    "in-source-c-near-seed": 15,
    "in-source-c-started-seed": 25,
    "compiled-packet-split-seed": 30,
    "compiled-packet-started-seed": 40,
    "matched-c-inline-asm": 90,
}

RECIPE_ORDER = {
    "template-backed-lowering": 0,
    "template-backed-direct": 5,
    "direct-batch-candidate": 10,
    "adapter-completion": 20,
    "exact-seed-replacement": 30,
    "source-lane-split": 40,
    "manual-reverse-needed": 80,
}


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = [
        "rank",
        "priority_score",
        "entry",
        "oot3d_name",
        "domain",
        "port_file",
        "unit",
        "frontier_status",
        "blocker_class",
        "recipe",
        "target_instruction_count",
        "probe_category",
        "proof_state",
        "primary_evidence",
        "secondary_n64_reference",
        "next_gate",
    ]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fields})


def write_json(path: Path, rows: list[dict[str, Any]], summary: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps({"summary": summary, "queue": rows}, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def to_int(value: str | int | None) -> int:
    try:
        return int(value or 0)
    except ValueError:
        return 0


def to_float(value: str | float | None) -> float:
    try:
        return float(value or 0.0)
    except ValueError:
        return 0.0


def truthy(value: str | bool | None) -> bool:
    if isinstance(value, bool):
        return value
    return str(value or "").strip().lower() in {"1", "true", "yes", "y"}


def by_key(rows: list[dict[str, str]], key: str) -> dict[str, dict[str, str]]:
    return {row.get(key, ""): row for row in rows if row.get(key)}


def first_by_key(rows: list[dict[str, str]], key: str) -> dict[str, dict[str, str]]:
    indexed: dict[str, dict[str, str]] = {}
    for row in rows:
        value = row.get(key, "")
        if value and value not in indexed:
            indexed[value] = row
    return indexed


def unit_by_port(rows: list[dict[str, str]]) -> dict[str, dict[str, str]]:
    indexed: dict[str, dict[str, str]] = {}
    for row in rows:
        port_file = row.get("port_file", "")
        if port_file:
            indexed.setdefault(port_file, row)
    return indexed


def proof_state(frontier: dict[str, str], blocker: dict[str, str], recipe: dict[str, str]) -> str:
    parts = ["oot3d-target-window"]
    if frontier.get("port_file"):
        parts.append("maintained-source-lane")
    if frontier.get("materialized_status") == "ok" and frontier.get("materialized_output"):
        parts.append("materialized-target-c")
    if truthy(frontier.get("probe_compiled")):
        parts.append("compiled-probe")
    if to_int(frontier.get("complete_split_spans")) or to_int(blocker.get("complete_split_spans")):
        parts.append("complete-source-split")
    if recipe.get("closest_templates"):
        parts.append("exact-template-neighborhood")
    return "+".join(parts)


def primary_evidence(frontier: dict[str, str], blocker: dict[str, str], unit: dict[str, str]) -> str:
    evidence = []
    for key in ("port_file", "materialized_output", "probe_source", "best_split_source"):
        value = frontier.get(key, "")
        if value:
            evidence.append(value)
    if blocker.get("best_split_source") and blocker["best_split_source"] not in evidence:
        evidence.append(blocker["best_split_source"])
    if unit.get("status_report"):
        evidence.append(unit["status_report"])
    return " | ".join(evidence)


def secondary_n64_reference(frontier: dict[str, str], recipe: dict[str, str]) -> str:
    refs = []
    if recipe.get("n64_source"):
        refs.append(recipe["n64_source"])
    name = frontier.get("n64_name") or recipe.get("n64_name")
    if name:
        refs.append(name)
    return " | ".join(refs)


def priority_score(frontier: dict[str, str], blocker: dict[str, str], recipe: dict[str, str]) -> float:
    blocker_class = blocker.get("blocker_class", "")
    status = frontier.get("frontier_status", "")
    recipe_name = recipe.get("recipe", "")
    target = to_int(frontier.get("target_instruction_count") or blocker.get("target_instruction_count"))

    score = 0.0
    score += BLOCKER_ORDER.get(blocker_class, 100)
    score += STATUS_ORDER.get(status, 50)
    score += RECIPE_ORDER.get(recipe_name, 25)
    score += min(target, 5000) / 1000.0

    if blocker.get("queue_state") == "actionable":
        score -= 5.0
    if frontier.get("materialized_status") == "ok":
        score -= 2.0
    if truthy(frontier.get("probe_compiled")):
        score -= 2.0
    if to_int(frontier.get("complete_split_spans")) or to_int(blocker.get("complete_split_spans")):
        score -= 2.0
    if recipe.get("closest_templates"):
        score -= 1.0
    if frontier.get("matched_baseline_exact") == "yes" and status == "matched-c-inline-asm":
        score += 20.0
    return round(score, 4)


def build_rows() -> list[dict[str, Any]]:
    frontier_rows = read_csv(DEFAULT_FRONTIER)
    blocker_rows = read_csv(DEFAULT_BLOCKERS)
    recipe_rows = read_csv(DEFAULT_RECIPES)
    unit_rows = read_csv(DEFAULT_UNITS)

    blockers = by_key(blocker_rows, "entry")
    recipes = first_by_key(recipe_rows, "oot3d_entry")
    units = unit_by_port(unit_rows)

    rows: list[dict[str, Any]] = []
    for frontier in frontier_rows:
        entry = frontier.get("entry", "")
        if not entry:
            continue
        blocker = blockers.get(entry, {})
        recipe = recipes.get(entry, {})
        unit = units.get(frontier.get("port_file", ""), {})
        next_gate = blocker.get("next_gate") or frontier.get("next_action") or recipe.get("action")
        row = {
            "entry": entry,
            "oot3d_name": frontier.get("oot3d_name", ""),
            "domain": recipe.get("domain", ""),
            "port_file": frontier.get("port_file", ""),
            "unit": unit.get("unit", ""),
            "frontier_status": frontier.get("frontier_status", ""),
            "blocker_class": blocker.get("blocker_class", ""),
            "recipe": recipe.get("recipe", ""),
            "target_instruction_count": to_int(frontier.get("target_instruction_count")),
            "probe_category": blocker.get("probe_category") or frontier.get("probe_category", ""),
            "proof_state": proof_state(frontier, blocker, recipe),
            "primary_evidence": primary_evidence(frontier, blocker, unit),
            "secondary_n64_reference": secondary_n64_reference(frontier, recipe),
            "next_gate": next_gate,
        }
        row["priority_score"] = priority_score(frontier, blocker, recipe)
        rows.append(row)

    rows.sort(key=lambda row: (row["priority_score"], row["target_instruction_count"], row["entry"]))
    for rank, row in enumerate(rows, 1):
        row["rank"] = rank
    return rows


def summarize(rows: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "rows": len(rows),
        "domains": dict(Counter(row.get("domain") or "unknown" for row in rows)),
        "blockers": dict(Counter(row.get("blocker_class") or "unknown" for row in rows)),
        "frontier_statuses": dict(Counter(row.get("frontier_status") or "unknown" for row in rows)),
        "top_entry": rows[0]["entry"] if rows else "",
        "top_name": rows[0]["oot3d_name"] if rows else "",
    }


def md_escape(value: Any) -> str:
    text = str(value or "")
    return text.replace("|", "\\|")


def write_md(path: Path, rows: list[dict[str, Any]], summary: dict[str, Any]) -> None:
    lines = [
        "# Native Source Decompilation Queue",
        "",
        "This generated queue supports the OOT3D source-decompilation goal. It ranks work by native OOT3D evidence first: code.bin/Ghidra target windows, maintained source lanes, compiled probes, structured gates, materialized target-shaped C, and exact OOT3D template neighborhoods. N64 source references are retained only as secondary semantic context.",
        "",
        "## Policy",
        "",
        "- Primary evidence: OOT3D code.bin-derived exports, Ghidra target disassembly/decompiles, maintained OOT3D source lanes, compiled ARM probes, and native asset/code reports.",
        "- Secondary evidence: N64 source names, source lanes, constants, and gameplay patterns. These can guide hypotheses but cannot promote source or behavior without OOT3D confirmation.",
        "- Validation evidence: emulator traces and screenshots. These can validate runtime meaning but must not become runtime data or source of truth.",
        "",
        "## Summary",
        "",
        f"- Queue rows: {summary['rows']}",
        f"- Top entry: `{summary['top_entry']}` `{summary['top_name']}`",
        f"- Domains: {', '.join(f'{key}:{value}' for key, value in sorted(summary['domains'].items()))}",
        f"- Blockers: {', '.join(f'{key}:{value}' for key, value in sorted(summary['blockers'].items()))}",
        "",
    ]

    if rows:
        top = rows[0]
        lines.extend(
            [
                "## Next Work Unit",
                "",
                f"- Entry: `{top['entry']}` `{top['oot3d_name']}`",
                f"- Source lane: `{top['port_file']}`",
                f"- Structured unit: `{top['unit']}`",
                f"- Target instructions: {top['target_instruction_count']}",
                f"- Proof state: `{top['proof_state']}`",
                f"- Primary evidence: `{top['primary_evidence']}`",
                f"- Secondary N64 reference: `{top['secondary_n64_reference']}`",
                f"- Next gate: {top['next_gate']}",
                "",
                "Focused gate command:",
                "",
                "```powershell",
                "python .\\scripts\\structured_c_match_gate.py --unit boss_va_zapper",
                "```",
                "",
            ]
        )

    lines.extend(
        [
            "## Queue",
            "",
            "| Rank | Score | Entry | Name | Domain | Status | Blocker | Insns | Proof state | Next gate |",
            "| ---: | ---: | --- | --- | --- | --- | --- | ---: | --- | --- |",
        ]
    )
    for row in rows:
        lines.append(
            f"| {row['rank']} | {row['priority_score']:.4f} | `{row['entry']}` | `{md_escape(row['oot3d_name'])}` | "
            f"`{md_escape(row['domain'])}` | `{md_escape(row['frontier_status'])}` | `{md_escape(row['blocker_class'])}` | "
            f"{row['target_instruction_count']} | `{md_escape(row['proof_state'])}` | {md_escape(row['next_gate'])} |"
        )
    lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    rows = build_rows()
    summary = summarize(rows)
    write_json(DEFAULT_OUT_JSON, rows, summary)
    write_csv(DEFAULT_OUT_CSV, rows)
    write_md(DEFAULT_OUT_MD, rows, summary)
    print(f"wrote {DEFAULT_OUT_MD.relative_to(ROOT)} ({len(rows)} rows)")


if __name__ == "__main__":
    main()
