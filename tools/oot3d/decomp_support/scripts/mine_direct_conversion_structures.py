#!/usr/bin/env python3
"""Mine reusable direct N64->OOT3D conversion structures.

This sits above the adapter audit.  The lower-level reports tell us which
tokens/offsets/calls are known; this script turns that evidence into a batch
conversion blueprint for each mapped row and each shared N64 source lane.
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

COMMENT_RE = re.compile(r"/\*.*?\*/|//[^\r\n]*", re.DOTALL)
STRING_RE = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
CALL_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")
CONTROL_RE = re.compile(r"\b(if|else|switch|case|for|while|do|return|break|continue)\b")
MEMBER_RE = re.compile(
    r"\b(?:this|play|player|msgCtx|interfaceCtx|actor|boomerang|boomTarget|floorPoly|wallPoly)"
    r"->(?:[A-Za-z_][A-Za-z0-9_]*)(?:\.[A-Za-z_][A-Za-z0-9_]*)*"
)
DIRECT_OFFSET_RE = re.compile(
    r"\*\s*\([^)]*\)\s*\(\s*[^)\n]+?\+\s*(?:0x[0-9a-fA-F]+|\d+)\s*\)"
)
OFFSET_MACRO_RE = re.compile(r"\bOOT3D_[A-Z0-9_]*OFFSET[A-Z0-9_]*\b")
OOT_HELPER_RE = re.compile(r"\boot3d_[A-Za-z0-9_]+\b")
FUN_RE = re.compile(r"\bFUN_[0-9a-fA-F]{8}\b")
DAT_RE = re.compile(r"\bDAT_[0-9a-fA-F]{8}\b")
ANIM_RE = re.compile(r"\b(?:Animation_Change|Animation_GetLastFrame|SkelAnime_Update)\b")
TIMER_RE = re.compile(r"\b(?:timer|Timer|frame|Frame|curFrame|playSpeed)\b")
STATE_RE = re.compile(r"\b(?:actionFunc|Setup|state|State|mode|Mode|msgMode|burst|isDead)\b")

IGNORE_CALLS = {
    "ABS",
    "ARRAY_COUNT",
    "CHECK_BTN_ALL",
    "COLPOLY_GET_NORMAL",
    "CONVEYOR_DIRECTION_TO_BINANG",
    "FALLTHROUGH",
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

ADAPTER_STAGE = {
    "struct-field": "field-lowering",
    "engine-call": "call-lowering",
    "subsystem-call": "subsystem-lowering",
    "subsystem-flow": "subsystem-lowering",
    "data-symbol": "data-packet",
    "source-lane": "lane-slicing",
}

STAGE_META = {
    "field-lowering": {
        "title": "N64 member path -> OOT3D direct field access",
        "trigger": "`this->field`, `play->field`, or `msgCtx->field` adapter hits",
        "oot3d_shape": "typed direct offset loads/stores or narrow offset macros",
        "automation": "mechanical when the member adapter and target offset are both known",
    },
    "call-lowering": {
        "title": "N64 engine call -> recovered OOT3D callee/ABI",
        "trigger": "`Math_*`, `Animation_*`, `SurfaceType_*`, message, random, or sfx calls",
        "oot3d_shape": "`FUN_*` prototype, hard-float wrapper, or named semantic helper",
        "automation": "mechanical after ABI shape is known",
    },
    "data-packet": {
        "title": "N64 global/static state -> OOT3D DAT packet",
        "trigger": "`gSaveContext`, register lanes, text globals, or repeated target DAT refs",
        "oot3d_shape": "named `DAT_*` packet plus narrow typed accessors",
        "automation": "mechanical once the data packet is named",
    },
    "lane-slicing": {
        "title": "One N64 source lane -> multiple OOT3D targets",
        "trigger": "multiple mapped rows share the same N64 source function",
        "oot3d_shape": "split helpers selected by target offsets, calls, DATs, and small entry footprints",
        "automation": "batch-first; avoid isolated function work",
    },
    "helper-lowering": {
        "title": "Structured helper C -> target-shaped C",
        "trigger": "source has many local helpers while target has direct offsets/stores",
        "oot3d_shape": "decompiler-like loads/stores and target ABI call order",
        "automation": "template-guided; exact matching usually needs this before profile search",
    },
    "state-slot": {
        "title": "Action/state slot writes",
        "trigger": "`actionFunc`, setup helpers, state modes, or target function pointer refs",
        "oot3d_shape": "direct action pointer stores and byte/halfword state fields",
        "automation": "mechanical after target action function symbols are named",
    },
    "animation-abi": {
        "title": "Animation_Change hard-float expansion",
        "trigger": "N64 `Animation_Change`/`SkelAnime` flow or resolved VFP target evidence",
        "oot3d_shape": "r0/r1/r2 plus s0-s3 hard-float call sequence",
        "automation": "mechanical with `animation_change_abi` evidence",
    },
    "timing-profile": {
        "title": "Literal timer/profile adaptation",
        "trigger": "N64 timers survive but OOT3D uses profile-specific literal schedule",
        "oot3d_shape": "domain profile constants such as BossVa zapper burst timings",
        "automation": "semi-mechanical; needs one checked profile per subsystem",
    },
    "subsystem-lowering": {
        "title": "Subsystem-specific helper flow",
        "trigger": "domain helper adapters such as BossVa effects, setup calls, or message/pause split helpers",
        "oot3d_shape": "small local wrapper or direct call packet matching the target subsystem ABI",
        "automation": "mechanical after one checked wrapper/profile exists for the subsystem",
    },
}

CLASS_MODE = {
    "matched-template": "exact-template",
    "direct-batch-candidate": "direct-source-lowering",
    "exact-seed-direct-candidate": "exact-seed-replacement",
    "helper-to-direct-candidate": "helper-to-direct-lowering",
    "split-lane-candidate": "source-lane-slicing",
    "adapter-completion-candidate": "adapter-completion",
}


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def slug(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value).strip("_")


def read_text(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def code_only(text: str) -> str:
    return STRING_RE.sub("", COMMENT_RE.sub("", text))


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


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def int_value(row: dict[str, Any], key: str) -> int:
    try:
        return int(row.get(key, 0) or 0)
    except ValueError:
        return 0


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


def format_counts(counter: Counter[str], limit: int = 8) -> str:
    return " ".join(f"{name}:{count}" for name, count in counter.most_common(limit))


def top_unique(items: list[str], limit: int = 5) -> str:
    seen: list[str] = []
    for item in items:
        if item and item not in seen:
            seen.append(item)
        if len(seen) >= limit:
            break
    return " ".join(seen)


def extract_path(row: dict[str, str], out_root: Path) -> Path:
    unit_dir = out_root / slug(row["port_file"])
    prefix = f"{row['oot3d_entry']}_{row['oot3d_name']}__n64_{row['n64_name']}"
    matches = sorted(unit_dir.glob(f"{slug(prefix)}*.c"))
    if matches:
        return matches[0]
    return unit_dir / f"{slug(prefix)}.c"


def adapter_meta(path: Path) -> dict[str, dict[str, str]]:
    return {row["id"]: row for row in read_csv(path) if row.get("id")}


def recipe_meta(path: Path) -> dict[str, dict[str, Any]]:
    data = read_json(path, {})
    return {
        str(row.get("oot3d_entry", "")).lower(): row
        for row in data.get("candidate_recipes", [])
        if row.get("oot3d_entry")
    }


def row_domain(row: dict[str, Any]) -> str:
    domain = str(row.get("n64_domain") or row.get("domain") or "")
    if domain:
        return domain
    source = str(row.get("n64_source") or row.get("port_file") or "").lower()
    name = str(row.get("n64_name") or "")
    if "boss_va" in source or name.startswith("BossVa_"):
        return "boss_va"
    if "boss_mo" in source or name.startswith("BossMo_"):
        return "boss_mo"
    if "player" in source or name.startswith("Player_"):
        return "player"
    if "message" in source or name.startswith("Message_"):
        return "message"
    if "kaleido" in source or "construct" in source or name == "Regs_InitDataImpl":
        return "pause"
    return "other"


def source_metrics(n64_code: str, oot_code: str) -> dict[str, int]:
    n64_calls = [call for call in CALL_RE.findall(n64_code) if call not in IGNORE_CALLS]
    oot_helpers = OOT_HELPER_RE.findall(oot_code)
    return {
        "n64_call_count": len(n64_calls),
        "n64_unique_calls": len(set(n64_calls)),
        "n64_member_refs": len(MEMBER_RE.findall(n64_code)),
        "n64_control_tokens": len(CONTROL_RE.findall(n64_code)),
        "oot_direct_offsets": len(DIRECT_OFFSET_RE.findall(oot_code)),
        "oot_offset_macros": len(OFFSET_MACRO_RE.findall(oot_code)),
        "oot_helpers": len(oot_helpers),
        "oot_unique_helpers": len(set(oot_helpers)),
        "oot_fun_refs": len(FUN_RE.findall(oot_code)),
        "oot_dat_refs": len(DAT_RE.findall(oot_code)),
    }


def infer_stages(
    row: dict[str, Any],
    counts: Counter[str],
    adapters: dict[str, dict[str, str]],
    n64_code: str,
    metrics: dict[str, int],
) -> list[str]:
    stages: set[str] = set()
    categories: Counter[str] = Counter()
    for adapter_id in counts:
        category = adapters.get(adapter_id, {}).get("category", "")
        if category:
            categories[category] += 1
        stage = ADAPTER_STAGE.get(category)
        if stage:
            stages.add(stage)

    if int_value(row, "struct_field_hits") > 0 or int_value(row, "known_direct_field_hits") > 0:
        stages.add("field-lowering")
    if int_value(row, "engine_call_hits") > 0:
        stages.add("call-lowering")
    if int_value(row, "data_symbol_hits") > 0 or categories["data-symbol"] > 0:
        stages.add("data-packet")
    if int_value(row, "same_n64_function_rows") > 1:
        stages.add("lane-slicing")

    helper_refs = int_value(row, "source_helper_refs")
    direct_offsets = int_value(row, "target_direct_offsets")
    if helper_refs >= 12 and (direct_offsets >= 8 or int_value(row, "target_direct_stores") >= 4):
        stages.add("helper-lowering")
    if metrics["oot_helpers"] >= 30 and direct_offsets >= 8 and row.get("status") != "matched-c":
        stages.add("helper-lowering")

    state_adapter = any(
        token in adapter_id
        for adapter_id in counts
        for token in ("action", "setup", "state", "mode", "textbox", "burst", "is_dead")
    )
    actor_or_state_domain = row_domain(row) in {"boss_va", "boss_mo", "player", "message"}
    has_explicit_state = bool(re.search(r"\b(?:actionFunc|Setup|msgMode|burst|isDead|stateFlags)\b", n64_code))
    if state_adapter or has_explicit_state or (actor_or_state_domain and int_value(row, "target_fun_refs") >= 8):
        stages.add("state-slot")
    if ANIM_RE.search(n64_code) or {"engine_anim_change", "engine_anim_change_full_abi"} & set(counts):
        stages.add("animation-abi")
    if TIMER_RE.search(n64_code) and (
        "bossva_zapper_timer_scale" in counts or "bossva_timer2" in counts or row_domain(row) == "boss_va"
    ):
        stages.add("timing-profile")

    order = [
        "field-lowering",
        "call-lowering",
        "data-packet",
        "lane-slicing",
        "helper-lowering",
        "state-slot",
        "animation-abi",
        "timing-profile",
        "subsystem-lowering",
    ]
    return [stage for stage in order if stage in stages]


def mechanical_score(row: dict[str, Any], stages: list[str], recipe: dict[str, Any] | None) -> int:
    score = 0
    score += int_value(row, "high_confidence_hits") * 4
    score += int_value(row, "struct_field_hits") * 9
    score += int_value(row, "engine_call_hits") * 7
    score += int_value(row, "data_symbol_hits") * 3
    score += int_value(row, "target_direct_offsets") * 2
    score += int_value(row, "target_direct_stores") * 2
    score += len(stages) * 45
    if row.get("status") == "matched-c":
        score += 300
    if recipe and recipe.get("closest_templates"):
        score += 180
    if int_value(row, "same_n64_function_rows") > 1:
        score += 100
    return score


def automation_readiness(row: dict[str, Any], stages: list[str], recipe: dict[str, Any] | None) -> str:
    if row.get("status") == "matched-c":
        return "proven-exact-template"
    if recipe and recipe.get("closest_templates"):
        return "template-backed"
    if "lane-slicing" in stages and int_value(row, "same_n64_function_rows") > 1:
        return "batch-lane-ready"
    if len(stages) >= 4 and int_value(row, "target_direct_offsets") >= 10:
        return "rule-backed"
    if int_value(row, "adapter_readiness") or row.get("adapter_readiness") in {"batch-rich", "adapter-ready"}:
        return "adapter-backed"
    return "manual-semantics"


def evidence_summary(row: dict[str, Any], metrics: dict[str, int]) -> str:
    parts = [
        f"offsets={int_value(row, 'target_direct_offsets')}",
        f"stores={int_value(row, 'target_direct_stores')}",
        f"struct={int_value(row, 'struct_field_hits')}",
        f"engine={int_value(row, 'engine_call_hits')}",
        f"data={int_value(row, 'data_symbol_hits')}",
    ]
    helpers = int_value(row, "source_helper_refs") or metrics["oot_helpers"]
    if helpers:
        parts.append(f"helpers={helpers}")
    if int_value(row, "same_n64_function_rows") > 1:
        parts.append(f"lane_rows={int_value(row, 'same_n64_function_rows')}")
    return " ".join(parts)


def next_action(row: dict[str, Any], stages: list[str], readiness: str, recipe: dict[str, Any] | None) -> str:
    if row.get("status") == "matched-c":
        return "Use as an exact template for candidates sharing its adapters and target direct-offset style."
    if "helper-lowering" in stages and recipe and recipe.get("closest_templates"):
        return "Lower helper-heavy structured C toward the closest exact template, then run the structured C gate."
    if "lane-slicing" in stages:
        return "Convert the shared N64 source lane once and split by target offset/call/DAT footprints."
    if "field-lowering" in stages and "call-lowering" in stages:
        return "Apply member and engine-call rewrites first, then replace exact-seed chunks with N64 control flow."
    if "data-packet" in stages:
        return "Name the repeated data packets before source conversion."
    return "Complete missing adapters, then re-run this miner."


def build_blueprints(
    rows: list[dict[str, str]],
    adapters: dict[str, dict[str, str]],
    recipes: dict[str, dict[str, Any]],
    n64_out_root: Path,
) -> list[dict[str, Any]]:
    port_cache: dict[str, str] = {}
    blueprints: list[dict[str, Any]] = []

    for row in rows:
        entry = str(row.get("oot3d_entry", "")).lower()
        n64_path = ROOT / row.get("n64_extract", "")
        if not n64_path.is_file():
            n64_path = extract_path(row, n64_out_root)
        n64_code = code_only(read_text(n64_path))
        port_file = row.get("port_file", "")
        oot_code = port_cache.setdefault(port_file, code_only(read_text(ROOT / port_file)))
        metrics = source_metrics(n64_code, oot_code)
        counts = parse_counts(row.get("top_adapter_ids", ""))
        recipe = recipes.get(entry)
        stages = infer_stages(row, counts, adapters, n64_code, metrics)
        readiness = automation_readiness(row, stages, recipe)
        score = mechanical_score(row, stages, recipe)
        mode = CLASS_MODE.get(row.get("direct_class", ""), row.get("direct_class", "manual-review"))

        closest_templates = ""
        if row.get("status") == "matched-c":
            closest_templates = f"{row['oot3d_entry']}:{row['oot3d_name']}:self"
        elif recipe:
            closest_templates = str(recipe.get("closest_templates", ""))

        blueprints.append(
            {
                "mechanical_score": score,
                "oot3d_entry": row.get("oot3d_entry", ""),
                "oot3d_name": row.get("oot3d_name", ""),
                "domain": row_domain(row),
                "status": row.get("status", ""),
                "direct_class": row.get("direct_class", ""),
                "conversion_mode": mode,
                "automation_readiness": readiness,
                "structure_count": len(stages),
                "replicable_structures": " ".join(stages),
                "n64_lane": f"{row.get('n64_source', '')}::{row.get('n64_name', '')}",
                "top_adapters": format_counts(counts, 8),
                "closest_templates": closest_templates,
                "evidence": evidence_summary(row, metrics),
                "n64_calls": metrics["n64_call_count"],
                "n64_members": metrics["n64_member_refs"],
                "n64_control_tokens": metrics["n64_control_tokens"],
                "oot_helpers": metrics["oot_helpers"],
                "oot_direct_offsets": metrics["oot_direct_offsets"],
                "next_action": next_action(row, stages, readiness, recipe),
                "port_file": row.get("port_file", ""),
                "n64_extract": rel(n64_path),
            }
        )

    blueprints.sort(key=lambda item: (-int(item["mechanical_score"]), item["oot3d_entry"]))
    for index, row in enumerate(blueprints, start=1):
        row["rank"] = index
    return blueprints


def aggregate_families(
    blueprints: list[dict[str, Any]], adapters: dict[str, dict[str, str]]
) -> list[dict[str, Any]]:
    families: dict[str, dict[str, Any]] = {}

    for stage, meta in STAGE_META.items():
        families[stage] = {
            "stage": stage,
            "title": meta["title"],
            "trigger": meta["trigger"],
            "oot3d_shape": meta["oot3d_shape"],
            "automation": meta["automation"],
            "rows": 0,
            "matched_templates": 0,
            "candidate_rows": 0,
            "domains": Counter(),
            "classes": Counter(),
            "adapters": Counter(),
            "examples": [],
        }

    for row in blueprints:
        stages = str(row["replicable_structures"]).split()
        counts = parse_counts(str(row["top_adapters"]))
        for stage in stages:
            if stage not in families:
                families[stage] = {
                    "stage": stage,
                    "title": stage,
                    "trigger": "",
                    "oot3d_shape": "",
                    "automation": "",
                    "rows": 0,
                    "matched_templates": 0,
                    "candidate_rows": 0,
                    "domains": Counter(),
                    "classes": Counter(),
                    "adapters": Counter(),
                    "examples": [],
                }
            item = families[stage]
            item["rows"] += 1
            if row["status"] == "matched-c":
                item["matched_templates"] += 1
            else:
                item["candidate_rows"] += 1
            item["domains"][str(row["domain"])] += 1
            item["classes"][str(row["direct_class"])] += 1
            item["examples"].append(f"{row['oot3d_entry']}:{row['oot3d_name']}")
            for adapter_id, count in counts.items():
                category = adapters.get(adapter_id, {}).get("category", "")
                adapter_stage = ADAPTER_STAGE.get(category, "")
                if adapter_stage == stage or (stage == "field-lowering" and category == "struct-field"):
                    item["adapters"][adapter_id] += count

    rows: list[dict[str, Any]] = []
    for stage, item in families.items():
        if not item["rows"]:
            continue
        rows.append(
            {
                "stage": stage,
                "title": item["title"],
                "trigger": item["trigger"],
                "oot3d_shape": item["oot3d_shape"],
                "automation": item["automation"],
                "rows": item["rows"],
                "matched_templates": item["matched_templates"],
                "candidate_rows": item["candidate_rows"],
                "domains": format_counts(item["domains"], 6),
                "classes": format_counts(item["classes"], 6),
                "top_adapters": format_counts(item["adapters"], 8),
                "examples": top_unique(item["examples"], 6),
            }
        )
    rows.sort(key=lambda row: (-int(row["rows"]), row["stage"]))
    return rows


def lane_strategy(group: list[dict[str, Any]]) -> str:
    stages = set()
    for row in group:
        stages.update(str(row["replicable_structures"]).split())
    matched = [row for row in group if row["status"] == "matched-c"]
    candidates = [row for row in group if row["status"] != "matched-c"]
    if "lane-slicing" in stages and matched:
        return "Convert the shared N64 lane once, using the matched split helpers as templates for target-shaped slicing."
    if "lane-slicing" in stages:
        return "Create lane-level slices from target footprints before replacing exact-seed bodies."
    if matched and candidates:
        return "Mine exact templates in the lane, then apply the same field/call lowering to remaining candidates."
    if "helper-lowering" in stages:
        return "Lower the current structured source to direct offsets before further compiler-profile search."
    return "Apply adapter rewrites and re-run the structured gate."


def aggregate_lanes(blueprints: list[dict[str, Any]]) -> list[dict[str, Any]]:
    groups: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in blueprints:
        groups[str(row["n64_lane"])].append(row)

    rows: list[dict[str, Any]] = []
    for lane, group in groups.items():
        domains = Counter(str(row["domain"]) for row in group)
        statuses = Counter(str(row["status"]) for row in group)
        structures = Counter()
        adapters = Counter()
        templates: list[str] = []
        candidates = 0
        matched = 0
        for row in group:
            structures.update(str(row["replicable_structures"]).split())
            adapters.update(parse_counts(str(row["top_adapters"])))
            if row["status"] == "matched-c":
                matched += 1
                templates.append(f"{row['oot3d_entry']}:{row['oot3d_name']}")
            else:
                candidates += 1
            if row.get("closest_templates"):
                templates.extend(str(row["closest_templates"]).split())
        max_score = max(int(row["mechanical_score"]) for row in group)
        ready = "high" if matched and candidates else "medium" if candidates and len(group) > 1 else "low"
        rows.append(
            {
                "n64_lane": lane,
                "domain": format_counts(domains, 2),
                "rows": len(group),
                "matched_templates": matched,
                "candidate_rows": candidates,
                "readiness": ready,
                "max_mechanical_score": max_score,
                "statuses": format_counts(statuses, 4),
                "replicable_structures": format_counts(structures, 8),
                "top_adapters": format_counts(adapters, 8),
                "templates": top_unique(templates, 5),
                "entries": " ".join(f"{row['oot3d_entry']}:{row['oot3d_name']}" for row in group),
                "batch_strategy": lane_strategy(group),
            }
        )
    rows.sort(key=lambda row: (-int(row["candidate_rows"]), -int(row["matched_templates"]), -int(row["max_mechanical_score"])))
    return rows


def summarize(blueprints: list[dict[str, Any]], families: list[dict[str, Any]], lanes: list[dict[str, Any]], gate: dict[str, Any]) -> dict[str, Any]:
    exact = sum(1 for row in blueprints if row["status"] == "matched-c")
    candidates = len(blueprints) - exact
    template_backed = sum(
        1
        for row in blueprints
        if row["status"] != "matched-c" and row["closest_templates"]
    )
    batch_rows = sum(
        1
        for row in blueprints
        if row["status"] != "matched-c"
        and row["automation_readiness"] in {"template-backed", "batch-lane-ready", "rule-backed"}
    )
    return {
        "mapped_rows": len(blueprints),
        "matched_templates": exact,
        "candidate_rows": candidates,
        "template_backed_candidates": template_backed,
        "batch_ready_candidates": batch_rows,
        "structure_families": len(families),
        "source_lanes": len(lanes),
        "high_or_medium_lanes": sum(1 for row in lanes if row["readiness"] in {"high", "medium"}),
        "structured_gate_exact": gate.get("exact_c_functions", 0),
        "structured_gate_exact_instructions": gate.get("exact_c_target_instructions", 0),
        "readiness": dict(Counter(str(row["automation_readiness"]) for row in blueprints)),
        "domains": dict(Counter(str(row["domain"]) for row in blueprints)),
        "conversion_modes": dict(Counter(str(row["conversion_mode"]) for row in blueprints)),
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct Conversion Structure Mining",
        "",
        "This report checks whether imported N64 decompilation sources share repeatable structures that can be converted into OOT3D-shaped C in batches.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Mapped rows | {summary['mapped_rows']} |",
        f"| Matched C templates | {summary['matched_templates']} |",
        f"| Non-matched candidates | {summary['candidate_rows']} |",
        f"| Template-backed candidates | {summary['template_backed_candidates']} |",
        f"| Batch-ready candidates | {summary['batch_ready_candidates']} |",
        f"| Structure families observed | {summary['structure_families']} |",
        f"| Source lanes | {summary['source_lanes']} |",
        f"| High/medium source lanes | {summary['high_or_medium_lanes']} |",
        f"| Structured gate exact functions | {summary['structured_gate_exact']} |",
        "",
        "## Verdict",
        "",
        "Yes: the imported files show repeatable conversion structures. The converter should emit target-shaped C rather than polished OoT-style C: direct offsets, recovered OOT3D callees, named DAT packets, lane slices, and only then compiler-profile tuning.",
        "",
        "## Structure Families",
        "",
        "| Family | Rows | Templates | Candidates | Domains | Output Shape | Top Adapters |",
        "| --- | ---: | ---: | ---: | --- | --- | --- |",
    ]
    for row in data["families"]:
        lines.append(
            f"| `{row['stage']}` {row['title']} | {row['rows']} | {row['matched_templates']} | "
            f"{row['candidate_rows']} | `{row['domains']}` | {row['oot3d_shape']} | `{row['top_adapters']}` |"
        )

    lines.extend(
        [
            "",
            "## Batch Source Lanes",
            "",
            "| N64 Lane | Rows | Templates | Candidates | Readiness | Structures | Templates/Seeds | Batch Strategy |",
            "| --- | ---: | ---: | ---: | --- | --- | --- | --- |",
        ]
    )
    for row in data["lanes"]:
        lines.append(
            f"| `{row['n64_lane']}` | {row['rows']} | {row['matched_templates']} | {row['candidate_rows']} | "
            f"`{row['readiness']}` | `{row['replicable_structures']}` | `{row['templates']}` | {row['batch_strategy']} |"
        )

    lines.extend(
        [
            "",
            "## Highest Leverage Blueprints",
            "",
            "| Rank | OOT3D | Mode | Readiness | Score | Structures | Templates | Evidence | Next Action |",
            "| ---: | --- | --- | --- | ---: | --- | --- | --- | --- |",
        ]
    )
    for row in data["blueprints"][:18]:
        lines.append(
            f"| {row['rank']} | `{row['oot3d_entry']}` `{row['oot3d_name']}` | `{row['conversion_mode']}` | "
            f"`{row['automation_readiness']}` | {row['mechanical_score']} | `{row['replicable_structures']}` | "
            f"`{row['closest_templates']}` | `{row['evidence']}` | {row['next_action']} |"
        )

    lines.extend(
        [
            "",
            "## Direct Converter Grammar",
            "",
            "| Stage | Trigger | Emitted OOT3D Shape | Automation Rule |",
            "| --- | --- | --- | --- |",
        ]
    )
    for row in data["families"]:
        lines.append(
            f"| `{row['stage']}` | {row['trigger']} | {row['oot3d_shape']} | {row['automation']} |"
        )

    lines.extend(
        [
            "",
            "## Recommended Workflow",
            "",
            "1. Pick a source lane, not a single function, from `Batch Source Lanes`.",
            "2. Apply field, call, and data-packet adapter rewrites to the extracted N64 source.",
            "3. If a matched template exists, copy its direct-offset and ABI style before any compiler-profile search.",
            "4. For split lanes, slice the converted N64 lane by target offset/call/DAT footprints.",
            "5. Run `scripts/structured_c_match_gate.py`; promote only exact rows and keep non-exact rows in the blueprint.",
            "",
        ]
    )

    while lines and not lines[-1]:
        lines.pop()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def analyze(args: argparse.Namespace) -> dict[str, Any]:
    rows = read_csv(args.direct_rows)
    adapters = adapter_meta(args.adapters)
    recipes = recipe_meta(args.recipes)
    gate = read_json(args.structured_gate, {})
    blueprints = build_blueprints(rows, adapters, recipes, args.n64_out_root)
    families = aggregate_families(blueprints, adapters)
    lanes = aggregate_lanes(blueprints)
    return {
        "summary": summarize(blueprints, families, lanes, gate),
        "families": families,
        "lanes": lanes,
        "blueprints": blueprints,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--direct-rows", type=Path, default=ROOT / "analysis" / "direct_source_conversion_rows.csv")
    parser.add_argument("--adapters", type=Path, default=ROOT / "metadata" / "n64_to_oot3d_adapters.csv")
    parser.add_argument("--recipes", type=Path, default=ROOT / "analysis" / "direct_source_conversion_recipes.json")
    parser.add_argument("--structured-gate", type=Path, default=ROOT / "analysis" / "structured_c_match_gate.json")
    parser.add_argument("--n64-out-root", type=Path, default=ROOT / "analysis" / "n64_port_units")
    parser.add_argument("--out-json", type=Path, default=ROOT / "analysis" / "direct_conversion_structure_mining.json")
    parser.add_argument("--out-md", type=Path, default=ROOT / "analysis" / "direct_conversion_structure_mining.md")
    parser.add_argument("--out-families-csv", type=Path, default=ROOT / "analysis" / "direct_conversion_structure_families.csv")
    parser.add_argument("--out-lanes-csv", type=Path, default=ROOT / "analysis" / "direct_conversion_lane_blueprints.csv")
    parser.add_argument("--out-blueprints-csv", type=Path, default=ROOT / "analysis" / "direct_conversion_function_blueprints.csv")
    args = parser.parse_args()

    data = analyze(args)
    write_json(args.out_json, data)
    write_markdown(args.out_md, data)
    write_csv(
        args.out_families_csv,
        data["families"],
        [
            "stage",
            "title",
            "trigger",
            "oot3d_shape",
            "automation",
            "rows",
            "matched_templates",
            "candidate_rows",
            "domains",
            "classes",
            "top_adapters",
            "examples",
        ],
    )
    write_csv(
        args.out_lanes_csv,
        data["lanes"],
        [
            "n64_lane",
            "domain",
            "rows",
            "matched_templates",
            "candidate_rows",
            "readiness",
            "max_mechanical_score",
            "statuses",
            "replicable_structures",
            "top_adapters",
            "templates",
            "entries",
            "batch_strategy",
        ],
    )
    write_csv(
        args.out_blueprints_csv,
        data["blueprints"],
        [
            "rank",
            "mechanical_score",
            "oot3d_entry",
            "oot3d_name",
            "domain",
            "status",
            "direct_class",
            "conversion_mode",
            "automation_readiness",
            "structure_count",
            "replicable_structures",
            "n64_lane",
            "top_adapters",
            "closest_templates",
            "evidence",
            "n64_calls",
            "n64_members",
            "n64_control_tokens",
            "oot_helpers",
            "oot_direct_offsets",
            "next_action",
            "port_file",
            "n64_extract",
        ],
    )

    summary = data["summary"]
    print(
        "direct structure mining: "
        f"{summary['structure_families']} families, "
        f"{summary['batch_ready_candidates']} batch-ready candidates, "
        f"{summary['template_backed_candidates']} template-backed candidates"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
