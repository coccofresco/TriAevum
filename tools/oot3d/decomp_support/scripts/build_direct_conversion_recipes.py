#!/usr/bin/env python3
"""Build reusable direct N64->OOT3D source conversion recipes.

The lower-level audits answer whether a mapped function has adapters or target
offsets. This script lifts that data into batch recipes: common conversion
structures, exact templates to mine, candidate rows, and the source lanes that
should be converted as groups instead of one function at a time.
"""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]

MATCHED_CLASS = "matched-template"

CLASS_ACTIONS = {
    "direct-batch-candidate": "Apply adapter rewrites, lower helpers to direct offsets, then run the structured C gate.",
    "helper-to-direct-candidate": "Use the closest exact template and rewrite helper-heavy C into target decompiler-shaped loads/stores.",
    "exact-seed-direct-candidate": "Replace exact-seed body chunks with N64 source logic after field/call/data adapters are named.",
    "split-lane-candidate": "Slice the shared N64 source lane by target offsets, calls, DAT packets, and small target entry boundaries.",
    "adapter-completion-candidate": "Complete missing high-value adapters before attempting a direct source conversion.",
    "source-lane-candidate": "Review as a grouped source lane; avoid isolated symbol promotion until the split role is known.",
    "manual-reverse-needed": "Keep in manual queue until new N64/OOT3D adapters or semantic names are available.",
}

STRUCTURE_STAGES = {
    "member_field_to_direct_offset": "field-lowering",
    "engine_call_remap": "call-lowering",
    "state_packet_global": "data-packet",
    "shared_source_lane_split": "lane-slicing",
    "helper_to_decompiler_shape": "helper-lowering",
    "action_state_slot": "state-slot",
}

STRUCTURE_ACTIONS = {
    "member_field_to_direct_offset": "Generate typed offset accessors or direct `(base + offset)` expressions from adapter hits.",
    "engine_call_remap": "Promote stable N64 engine calls to OOT3D callee prototypes and ABI wrappers.",
    "state_packet_global": "Name repeated DAT packets and keep source conditions/data-flow instead of re-reversing constants.",
    "shared_source_lane_split": "Convert the N64 file/function once, then split output by target function footprints.",
    "helper_to_decompiler_shape": "Lower current wrapper-heavy maintained C to the target's direct load/store shape for codegen.",
    "action_state_slot": "Emit direct action/state pointer stores after target function pointers are identified.",
}

DOMAIN_STRATEGIES = {
    "boss_va": (
        "Use `oot3d_boss_va_zapper_hold` and setup helpers as exact templates; convert the remaining zapper states "
        "with direct offset loads/stores plus named animation/math/effect calls."
    ),
    "player": (
        "Promote the Player/Actor field table into a generated rewrite map, then lower collision and action sources "
        "toward direct offsets before compiler-profile searches."
    ),
    "message": (
        "Treat `Message_Update` as a split lane: keep N64 branch flow, use the exact init/start templates, and name "
        "the message DAT packets before lowering the common body."
    ),
    "pause": (
        "Treat `Regs_InitDataImpl` and `KaleidoScope_DrawUIOverlay` as source lanes; slice by global packets and "
        "small target helpers, using the four exact pause splits as templates."
    ),
    "boss_mo": (
        "Start with engine math/random call remaps and target offsets; the current lane has no matched template yet."
    ),
}


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


def write_csv(path: Path, rows: list[dict[str, Any]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fieldnames})


def parse_counts(value: str) -> Counter[str]:
    counts: Counter[str] = Counter()
    for part in value.split():
        if ":" not in part:
            continue
        name, raw_count = part.rsplit(":", 1)
        try:
            counts[name] += int(raw_count)
        except ValueError:
            continue
    return counts


def format_counts(counter: Counter[str], limit: int = 8) -> str:
    return " ".join(f"{name}:{count}" for name, count in counter.most_common(limit))


def row_domain(row: dict[str, Any]) -> str:
    domain = str(row.get("n64_domain") or "")
    if domain:
        return domain
    source = str(row.get("n64_source") or "").lower()
    name = str(row.get("n64_name") or "")
    if "boss_va" in source or name.startswith("BossVa_"):
        return "boss_va"
    if "boss_mo" in source or name.startswith("BossMo_"):
        return "boss_mo"
    if "player" in source or name.startswith("Player_"):
        return "player"
    if "message" in source or name.startswith("Message_"):
        return "message"
    if "kaleido" in source or name == "Regs_InitDataImpl":
        return "pause"
    return "other"


def int_value(row: dict[str, Any], key: str) -> int:
    try:
        return int(row.get(key, 0) or 0)
    except ValueError:
        return 0


def candidate_score(row: dict[str, Any]) -> int:
    return int_value(row, "priority")


def adapter_weight(adapter: dict[str, Any]) -> int:
    category = str(adapter.get("category", ""))
    adapter_id = str(adapter.get("id", ""))
    if adapter_id == "global_save_context":
        return 1
    if category == "struct-field":
        return 12
    if category == "subsystem-call":
        return 10
    if category == "source-lane":
        return 10
    if category == "engine-call":
        return 8
    if category == "data-symbol":
        return 2
    return 4


def template_score(
    candidate: dict[str, Any],
    template: dict[str, Any],
    adapter_meta: dict[str, dict[str, Any]],
) -> tuple[int, Counter[str]]:
    candidate_counts = parse_counts(str(candidate.get("top_adapter_ids", "")))
    template_counts = parse_counts(str(template.get("top_adapter_ids", "")))
    shared = candidate_counts & template_counts
    same_domain = row_domain(candidate) == row_domain(template)
    same_source = candidate.get("n64_source") == template.get("n64_source")
    same_name = candidate.get("n64_name") == template.get("n64_name")
    strong_shared = 0
    weighted_overlap = 0

    for adapter_id, count in shared.items():
        meta = adapter_meta.get(adapter_id, {"id": adapter_id})
        category = str(meta.get("category", ""))
        domain = str(meta.get("domain", ""))
        if domain != "global" and category in {"struct-field", "subsystem-call", "source-lane", "data-symbol"}:
            strong_shared += count
        weighted_overlap += count * adapter_weight(meta)

    if not same_domain and not same_source and not same_name and strong_shared < 2:
        return 0, shared

    score = weighted_overlap
    if same_domain:
        score += 35
    if same_source:
        score += 30
    if same_name:
        score += 50
    return score, shared


def closest_templates(
    row: dict[str, Any],
    templates: list[dict[str, Any]],
    adapter_meta: dict[str, dict[str, Any]],
    limit: int = 3,
) -> list[dict[str, Any]]:
    scored = []
    for template in templates:
        score, shared = template_score(row, template, adapter_meta)
        if score <= 0:
            continue
        scored.append(
            {
                "entry": template["oot3d_entry"],
                "name": template["oot3d_name"],
                "n64_name": template["n64_name"],
                "domain": row_domain(template),
                "score": score,
                "shared_adapters": format_counts(shared, 6),
            }
        )
    return sorted(scored, key=lambda item: (-int(item["score"]), item["entry"]))[:limit]


def recipe_class(row: dict[str, Any], templates: list[dict[str, Any]]) -> str:
    direct_class = str(row.get("direct_class", ""))
    if direct_class == "direct-batch-candidate" and templates:
        return "template-backed-direct"
    if direct_class == "helper-to-direct-candidate" and templates:
        return "template-backed-lowering"
    if direct_class == "split-lane-candidate":
        return "source-lane-split"
    if direct_class == "exact-seed-direct-candidate":
        return "exact-seed-replacement"
    if direct_class == "adapter-completion-candidate":
        return "adapter-completion"
    return direct_class or "manual-review"


def load_animation_summary(path: Path) -> dict[str, Any]:
    data = read_json(path, {})
    summary = data.get("summary", {}) if isinstance(data, dict) else {}
    rows = data.get("rows", []) if isinstance(data, dict) else []
    top_unmapped = [
        row
        for row in rows
        if row.get("category") in {"unmapped-small-resolved", "unmapped-batch-family", "unmapped-resolved"}
    ][:12]
    return {
        "summary": summary,
        "top_unmapped": [
            {
                "entry": row.get("entry", ""),
                "function": row.get("function", ""),
                "category": row.get("category", ""),
                "priority_score": row.get("priority_score", 0),
                "top_signatures": row.get("top_signatures", ""),
            }
            for row in top_unmapped
        ],
    }


def build_structures(rows: list[dict[str, str]]) -> list[dict[str, Any]]:
    structures: list[dict[str, Any]] = []
    for row in rows:
        structure_id = row["id"]
        matched = int(row.get("matched_templates", 0) or 0)
        candidates = int(row.get("candidate_rows", 0) or 0)
        structures.append(
            {
                "id": structure_id,
                "stage": STRUCTURE_STAGES.get(structure_id, "manual"),
                "title": row.get("title", ""),
                "rows": int(row.get("rows", 0) or 0),
                "matched_templates": matched,
                "candidate_rows": candidates,
                "domains": row.get("domains", ""),
                "automation": "template-backed" if matched and candidates else ("candidate-only" if candidates else "template-only"),
                "rewrite": row.get("rewrite", ""),
                "batch_action": STRUCTURE_ACTIONS.get(structure_id, row.get("rewrite", "")),
                "examples": row.get("examples", ""),
            }
        )
    return structures


def build_source_lanes(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    groups: dict[tuple[str, str], list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        groups[(str(row.get("n64_source", "")), str(row.get("n64_name", "")))].append(row)

    lanes = []
    for (source, name), group in groups.items():
        if len(group) < 2:
            continue
        statuses = Counter(str(row.get("status", "")) for row in group)
        classes = Counter(str(row.get("direct_class", "")) for row in group)
        adapters: Counter[str] = Counter()
        for row in group:
            adapters.update(parse_counts(str(row.get("top_adapter_ids", ""))))
        lanes.append(
            {
                "n64_source": source,
                "n64_name": name,
                "domain": row_domain(group[0]),
                "rows": len(group),
                "matched_templates": sum(1 for row in group if row.get("direct_class") == MATCHED_CLASS),
                "candidate_rows": sum(1 for row in group if row.get("direct_class") != MATCHED_CLASS),
                "statuses": format_counts(statuses, 5),
                "classes": format_counts(classes, 6),
                "top_adapters": format_counts(adapters, 8),
                "entries": " ".join(f"{row['oot3d_entry']}:{row['oot3d_name']}" for row in group[:10]),
            }
        )
    return sorted(lanes, key=lambda row: (-int(row["candidate_rows"]), -int(row["rows"]), row["n64_source"], row["n64_name"]))


def build_domains(rows: list[dict[str, Any]], structures: list[dict[str, Any]]) -> list[dict[str, Any]]:
    by_domain: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        by_domain[row_domain(row)].append(row)

    domains: list[dict[str, Any]] = []
    for domain, group in by_domain.items():
        statuses = Counter(str(row.get("status", "")) for row in group)
        classes = Counter(str(row.get("direct_class", "")) for row in group)
        adapters: Counter[str] = Counter()
        for row in group:
            adapters.update(parse_counts(str(row.get("top_adapter_ids", ""))))
        supported_structures = [
            structure["id"]
            for structure in structures
            if domain in str(structure.get("domains", "")) or domain == "other"
        ]
        domains.append(
            {
                "domain": domain,
                "rows": len(group),
                "matched_templates": sum(1 for row in group if row.get("direct_class") == MATCHED_CLASS),
                "candidate_rows": sum(1 for row in group if row.get("direct_class") != MATCHED_CLASS),
                "statuses": format_counts(statuses, 6),
                "classes": format_counts(classes, 8),
                "top_adapters": format_counts(adapters, 10),
                "structures": " ".join(supported_structures[:8]),
                "strategy": DOMAIN_STRATEGIES.get(domain, "Build adapters first; no stable direct-conversion lane has been proven yet."),
            }
        )
    return sorted(domains, key=lambda row: (-int(row["candidate_rows"]), row["domain"]))


def build_candidates(
    rows: list[dict[str, Any]],
    templates: list[dict[str, Any]],
    adapter_meta: dict[str, dict[str, Any]],
) -> list[dict[str, Any]]:
    candidates: list[dict[str, Any]] = []
    for row in sorted(
        (row for row in rows if row.get("direct_class") != MATCHED_CLASS),
        key=lambda item: (-candidate_score(item), item.get("oot3d_entry", "")),
    ):
        nearby = closest_templates(row, templates, adapter_meta)
        candidates.append(
            {
                "rank": len(candidates) + 1,
                "recipe": recipe_class(row, nearby),
                "direct_class": row.get("direct_class", ""),
                "priority": row.get("priority", ""),
                "readiness": row.get("adapter_readiness", ""),
                "oot3d_entry": row.get("oot3d_entry", ""),
                "oot3d_name": row.get("oot3d_name", ""),
                "domain": row_domain(row),
                "n64_source": row.get("n64_source", ""),
                "n64_name": row.get("n64_name", ""),
                "target_direct_offsets": row.get("target_direct_offsets", ""),
                "struct_field_hits": row.get("struct_field_hits", ""),
                "engine_call_hits": row.get("engine_call_hits", ""),
                "data_symbol_hits": row.get("data_symbol_hits", ""),
                "source_helper_refs": row.get("source_helper_refs", ""),
                "same_n64_function_rows": row.get("same_n64_function_rows", ""),
                "top_adapters": row.get("top_adapter_ids", ""),
                "closest_templates": " ".join(
                    f"{item['entry']}:{item['name']}:{item['score']}" for item in nearby
                ),
                "shared_template_adapters": " | ".join(
                    f"{item['entry']} {item['shared_adapters']}" for item in nearby if item["shared_adapters"]
                ),
                "action": CLASS_ACTIONS.get(str(row.get("direct_class", "")), "Review manually."),
            }
        )
    return candidates


def build_adapter_rows(path: Path) -> list[dict[str, Any]]:
    rows = []
    for row in read_csv(path):
        hits = int(row.get("hits", 0) or 0)
        if hits <= 0:
            continue
        rows.append(
            {
                "id": row.get("id", ""),
                "category": row.get("category", ""),
                "domain": row.get("domain", ""),
                "confidence": row.get("confidence", ""),
                "n64_pattern": row.get("n64_pattern", ""),
                "oot3d_symbol": row.get("oot3d_symbol", ""),
                "hits": hits,
                "rows": int(row.get("rows", 0) or 0),
                "rewrite_stage": STRUCTURE_STAGES.get(
                    {
                        "struct-field": "member_field_to_direct_offset",
                        "engine-call": "engine_call_remap",
                        "subsystem-call": "engine_call_remap",
                        "data-symbol": "state_packet_global",
                        "source-lane": "shared_source_lane_split",
                    }.get(row.get("category", ""), ""),
                    "manual",
                ),
            }
        )
    return sorted(rows, key=lambda item: (-int(item["hits"]), item["category"], item["id"]))


def build_adapter_meta(path: Path) -> dict[str, dict[str, Any]]:
    return {row.get("id", ""): dict(row) for row in read_csv(path) if row.get("id")}


def analyze(
    direct_rows: list[dict[str, str]],
    structure_rows: list[dict[str, str]],
    adapter_hits_path: Path,
    animation_path: Path,
) -> dict[str, Any]:
    rows: list[dict[str, Any]] = [dict(row) for row in direct_rows]
    structures = build_structures(structure_rows)
    templates = [row for row in rows if row.get("direct_class") == MATCHED_CLASS]
    adapter_meta = build_adapter_meta(adapter_hits_path)
    candidates = build_candidates(rows, templates, adapter_meta)
    source_lanes = build_source_lanes(rows)
    domains = build_domains(rows, structures)
    adapters = build_adapter_rows(adapter_hits_path)
    animation = load_animation_summary(animation_path)
    classes = Counter(str(row.get("direct_class", "")) for row in rows)
    statuses = Counter(str(row.get("status", "")) for row in rows)

    return {
        "summary": {
            "mapped_rows": len(rows),
            "matched_templates": len(templates),
            "candidate_rows": len(candidates),
            "template_backed_candidates": sum(1 for row in candidates if str(row["closest_templates"]).strip()),
            "source_lanes": len(source_lanes),
            "common_structures": len(structures),
            "high_value_adapters": len(adapters),
            "classes": dict(sorted(classes.items())),
            "statuses": dict(sorted(statuses.items())),
        },
        "structures": structures,
        "domains": domains,
        "source_lanes": source_lanes,
        "candidate_recipes": candidates,
        "adapter_rewrites": adapters,
        "animation_change": animation,
    }


def write_json(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    anim_summary = data["animation_change"]["summary"]
    lines = [
        "# Direct N64 Source Conversion Recipes",
        "",
        "This report checks whether the imported N64 files share repeatable structures that can be converted mechanically into OOT3D-shaped C.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Mapped N64/OOT3D rows | {summary['mapped_rows']} |",
        f"| Matched C templates to mine | {summary['matched_templates']} |",
        f"| Non-matched conversion candidates | {summary['candidate_rows']} |",
        f"| Candidates with a reusable exact template | {summary['template_backed_candidates']} |",
        f"| Reused N64 source lanes | {summary['source_lanes']} |",
        f"| Common conversion structures | {summary['common_structures']} |",
        f"| Adapter rewrites with current hits | {summary['high_value_adapters']} |",
        f"| Animation_Change target functions | {anim_summary.get('target_functions', 0)} |",
        f"| Animation_Change fully resolved functions | {anim_summary.get('fully_resolved_functions', 0)} |",
        "",
        "## Verdict",
        "",
        "The imported files do have repeatable structure. The practical converter should not try to emit polished OOT-style structs first; it should emit target-shaped C: direct offsets, named engine callees, named DAT packets, and source-lane slicing. That shape is already proven by the matched pause, message, and BossVa templates.",
        "",
        "## Replicable Structures",
        "",
        "| Stage | Structure | Rows | Matched templates | Candidate rows | Automation | Domains | Batch action |",
        "| --- | --- | ---: | ---: | ---: | --- | --- | --- |",
    ]
    for row in data["structures"]:
        lines.append(
            f"| `{row['stage']}` | `{row['id']}` {row['title']} | {row['rows']} | "
            f"{row['matched_templates']} | {row['candidate_rows']} | `{row['automation']}` | "
            f"`{row['domains']}` | {row['batch_action']} |"
        )

    lines.extend(
        [
            "",
            "## Domain Recipes",
            "",
            "| Domain | Rows | Matched templates | Candidates | Structures | Top adapters | Strategy |",
            "| --- | ---: | ---: | ---: | --- | --- | --- |",
        ]
    )
    for row in data["domains"]:
        lines.append(
            f"| `{row['domain']}` | {row['rows']} | {row['matched_templates']} | {row['candidate_rows']} | "
            f"`{row['structures']}` | `{row['top_adapters']}` | {row['strategy']} |"
        )

    lines.extend(
        [
            "",
            "## Source Lanes To Convert As Batches",
            "",
            "| N64 source lane | Domain | Rows | Matched templates | Candidates | Classes | Top adapters | Entries |",
            "| --- | --- | ---: | ---: | ---: | --- | --- | --- |",
        ]
    )
    for row in data["source_lanes"]:
        lines.append(
            f"| `{row['n64_source']}` `{row['n64_name']}` | `{row['domain']}` | {row['rows']} | "
            f"{row['matched_templates']} | {row['candidate_rows']} | `{row['classes']}` | "
            f"`{row['top_adapters']}` | `{row['entries']}` |"
        )

    lines.extend(
        [
            "",
            "## Candidate Recipe Queue",
            "",
            "| Rank | Recipe | OOT3D | N64 lane | Readiness | Offsets | Struct | Engine | Data | Helpers | Closest exact templates | Action |",
            "| ---: | --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | --- | --- |",
        ]
    )
    for row in data["candidate_recipes"][:18]:
        lines.append(
            f"| {row['rank']} | `{row['recipe']}` | `{row['oot3d_entry']}` `{row['oot3d_name']}` | "
            f"`{row['n64_name']}` | `{row['readiness']}` | {row['target_direct_offsets']} | "
            f"{row['struct_field_hits']} | {row['engine_call_hits']} | {row['data_symbol_hits']} | "
            f"{row['source_helper_refs']} | `{row['closest_templates']}` | {row['action']} |"
        )

    lines.extend(
        [
            "",
            "## Top Adapter Rewrites",
            "",
            "| Stage | Adapter | Category | Domain | Hits | Rows | N64 pattern | OOT3D symbol |",
            "| --- | --- | --- | --- | ---: | ---: | --- | --- |",
        ]
    )
    for row in data["adapter_rewrites"][:30]:
        lines.append(
            f"| `{row['rewrite_stage']}` | `{row['id']}` | `{row['category']}` | `{row['domain']}` | "
            f"{row['hits']} | {row['rows']} | `{row['n64_pattern']}` | `{row['oot3d_symbol']}` |"
        )

    lines.extend(
        [
            "",
            "## Animation ABI Batch Hook",
            "",
            f"- Resolved functions: `{anim_summary.get('resolved_functions', 0)}`",
            f"- Fully resolved functions: `{anim_summary.get('fully_resolved_functions', 0)}`",
            f"- Mapped source-ready groups: `{anim_summary.get('mapped_ready', 0)}`",
            f"- Unmapped small resolved groups: `{anim_summary.get('unmapped_small_resolved', 0)}`",
            f"- Unmapped batch-family groups: `{anim_summary.get('unmapped_batch_family', 0)}`",
            "",
            "| Entry | Function | Category | Score | Signature |",
            "| --- | --- | --- | ---: | --- |",
        ]
    )
    for row in data["animation_change"]["top_unmapped"][:10]:
        lines.append(
            f"| `{row['entry']}` | `{row['function']}` | `{row['category']}` | "
            f"{row['priority_score']} | `{row['top_signatures']}` |"
        )

    lines.extend(
        [
            "",
            "## Implementation Shape",
            "",
            "1. Generate a domain rewrite table from `adapter_rewrites` and apply it to the N64 extract.",
            "2. For `template-backed-*` rows, copy the closest exact template's offset/call style before tuning compiler output.",
            "3. For source lanes, convert the N64 function/file once and split by target footprint instead of reviewing each target function independently.",
            "4. Keep helper wrappers only while validating semantics; lower them to direct offsets before match attempts when the target decompile uses direct loads/stores.",
            "5. Run `scripts/structured_c_match_gate.py` after each batch and promote only exact rows.",
            "",
        ]
    )

    while lines and not lines[-1]:
        lines.pop()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--direct-rows", type=Path, default=ROOT / "analysis" / "direct_source_conversion_rows.csv")
    parser.add_argument("--structures", type=Path, default=ROOT / "analysis" / "direct_source_conversion_structures.csv")
    parser.add_argument("--adapter-hits", type=Path, default=ROOT / "analysis" / "n64_adapter_hits.csv")
    parser.add_argument("--animation-queue", type=Path, default=ROOT / "analysis" / "animation_change_port_queue.json")
    parser.add_argument("--out-json", type=Path, default=ROOT / "analysis" / "direct_source_conversion_recipes.json")
    parser.add_argument("--out-md", type=Path, default=ROOT / "analysis" / "direct_source_conversion_recipes.md")
    parser.add_argument("--out-csv", type=Path, default=ROOT / "analysis" / "direct_source_conversion_recipes.csv")
    args = parser.parse_args()

    data = analyze(read_csv(args.direct_rows), read_csv(args.structures), args.adapter_hits, args.animation_queue)
    write_json(args.out_json, data)
    write_markdown(args.out_md, data)
    write_csv(
        args.out_csv,
        data["candidate_recipes"],
        [
            "rank",
            "recipe",
            "direct_class",
            "priority",
            "readiness",
            "oot3d_entry",
            "oot3d_name",
            "domain",
            "n64_source",
            "n64_name",
            "target_direct_offsets",
            "struct_field_hits",
            "engine_call_hits",
            "data_symbol_hits",
            "source_helper_refs",
            "same_n64_function_rows",
            "top_adapters",
            "closest_templates",
            "shared_template_adapters",
            "action",
        ],
    )

    summary = data["summary"]
    print(
        "direct recipes: "
        f"{summary['common_structures']} structures, "
        f"{summary['matched_templates']} templates, "
        f"{summary['candidate_rows']} candidates, "
        f"{summary['template_backed_candidates']} template-backed"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
