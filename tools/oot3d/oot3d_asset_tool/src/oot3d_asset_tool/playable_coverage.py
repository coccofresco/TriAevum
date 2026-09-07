from __future__ import annotations

import argparse
import hashlib
import json
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable


FORMAT = "oot3d_playable_asset_coverage_v1"
TIER_NAMES = {
    0: "inventoried",
    1: "decoded",
    2: "packaged",
    3: "runtime_candidate",
    4: "playable",
    5: "parity_validated",
}


@dataclass(frozen=True)
class InputSpec:
    key: str
    relative_path: str
    required: bool = True


INPUT_SPECS = (
    InputSpec("romfs_inventory", "inventory/oot3d_romfs_inventory_summary.json"),
    InputSpec("scene_metadata", "zsi_scene_metadata_audit/oot3d_zsi_scene_metadata_summary.json"),
    InputSpec("material_inventory", "material_stage_inventory/oot3d_material_stage_inventory_summary.json"),
    InputSpec("ctxb_audit", "ctxb_texture_audit/oot3d_ctxb_texture_summary.json"),
    InputSpec("ctxb_export", "ctxb_texture_export/ctxb_texture_export_summary.json"),
    InputSpec("actor_inventory", "actor_inventory/oot3d_actor_inventory_summary.json"),
    InputSpec("static_actor_package", "static_actor_export/static_actor_package_summary.json"),
    InputSpec("skinned_bind_pose", "skinned_bind_pose_batch/skinned_bind_pose_batch_summary.json"),
    InputSpec("skinned_animation", "skinned_animation_batch/skinned_animation_batch_summary.json"),
    InputSpec("collision_package", "collision_activation/collision_activation_full_visual_package_summary.json"),
    InputSpec("kankyo_audit", "kankyo_environment_audit/oot3d_kankyo_environment_summary.json"),
    InputSpec("kankyo_package", "kankyo_environment_export/kankyo_environment_package_summary.json"),
    InputSpec("audio_audit", "audio_asset_audit/oot3d_audio_asset_summary.json"),
    InputSpec("route_manifest", "kokiri_runtime_route/oot3d_kokiri_runtime_manifest_enabled_summary.json"),
    InputSpec("route_static_actors", "kokiri_static_actor_binding/oot3d_kokiri_static_actor_binding_summary.json"),
    InputSpec("route_kankyo", "kokiri_kankyo_binding/oot3d_kokiri_kankyo_binding_summary.json"),
    InputSpec("route_audio", "kokiri_audio_binding/oot3d_kokiri_audio_binding_summary.json"),
    InputSpec("link_child", "character_conversion/link_child_character_conversion_summary.json"),
)


def _read_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(value, dict):
        raise ValueError(f"{path}: expected a JSON object")
    return value


def _int(value: Any) -> int:
    if value is None:
        return 0
    if isinstance(value, bool):
        return int(value)
    return int(value)


def _path_value(data: dict[str, Any], *keys: str, default: Any = 0) -> Any:
    current: Any = data
    for key in keys:
        if not isinstance(current, dict) or key not in current:
            return default
        current = current[key]
    return current


def _tier_counts(total: int, decoded: int = 0, packaged: int = 0, runtime: int = 0,
                 playable: int = 0, parity: int = 0) -> dict[str, int]:
    values = [total, decoded, packaged, runtime, playable, parity]
    if any(value < 0 for value in values):
        raise ValueError(f"negative coverage count: {values}")
    for previous, current in zip(values, values[1:]):
        if current > previous:
            raise ValueError(f"coverage tiers are not monotonic: {values}")
    return {TIER_NAMES[index]: value for index, value in enumerate(values)}


def _family(family_id: str, unit: str, counts: dict[str, int], blockers: dict[str, int],
            evidence: list[str], metrics: dict[str, Any] | None = None) -> dict[str, Any]:
    total = counts["inventoried"]
    return {
        "family_id": family_id,
        "unit": unit,
        "tier_counts": counts,
        "tier_percentages": {
            key: round(value * 100.0 / total, 4) if total else 0.0 for key, value in counts.items()
        },
        "blocker_counts": {key: value for key, value in sorted(blockers.items()) if value},
        "evidence_inputs": evidence,
        "metrics": metrics or {},
    }


def _build_families(inputs: dict[str, dict[str, Any]]) -> list[dict[str, Any]]:
    inventory = inputs["romfs_inventory"]
    scene = inputs["scene_metadata"]
    materials = inputs["material_inventory"]
    ctxb = inputs["ctxb_audit"]
    ctxb_export = inputs["ctxb_export"]
    actors = inputs["actor_inventory"]
    static_package = inputs["static_actor_package"]
    skinned_bind = inputs["skinned_bind_pose"]
    skinned_anim = inputs["skinned_animation"]
    collision = inputs["collision_package"]
    kankyo = inputs["kankyo_audit"]
    kankyo_package = inputs["kankyo_package"]
    audio = inputs["audio_audit"]
    route = inputs["route_manifest"]
    route_actors = inputs["route_static_actors"]
    route_kankyo = inputs["route_kankyo"]
    route_audio = inputs["route_audio"]
    link_child = inputs["link_child"]

    room_total = _int(scene["room_count_total"])
    route_rooms = _int(_path_value(route, "resource_target_kind_counts", "room_mesh"))
    collision_total = _int(scene["scene_stem_count"])
    collision_decoded = _int(scene["collision_candidate_total"])
    collision_packaged = _int(collision["accepted_scene_count"])
    route_collisions = _int(_path_value(route, "resource_target_kind_counts", "scene_collision"))

    material_total = _int(materials["material_count"])
    material_export_ready = _int(
        _path_value(materials, "raw_texture_stage_export_classification_counts", "covered_by_current_export")
    ) + _int(
        _path_value(materials, "raw_texture_stage_export_classification_counts", "covered_by_baked_extra_texture_stage")
    )
    material_gap = _int(materials["raw_texture_stage_export_gap_material_count"])

    ctxb_total = _int(ctxb["ctxb_count"])
    ctxb_decoded = _int(ctxb["parsed_ctxb_count"])
    ctxb_packaged = _int(ctxb_export["exported_count"])

    actor_total = _int(_path_value(actors, "cmb_counts", "parsed"))
    actor_rigid_packaged = _int(static_package["converted"])
    actor_skinned_packaged = _int(_path_value(skinned_bind, "counts", "exported"))
    actor_packaged = min(actor_total, actor_rigid_packaged + actor_skinned_packaged)

    route_visual_entries = _int(route_actors["visual_actor_entry_count"])
    route_ready_entries = _int(route_actors["ready_static_actor_entry_count"])

    csab_total = _int(_path_value(actors, "animation_file_counts", "csab"))
    csab_packaged = _int(skinned_anim["exported"])
    link_csab = _int(link_child["csab_animation_count"])
    cmab_total = _int(_path_value(actors, "animation_file_counts", "cmab"))
    link_cmab = _int(link_child["cmab_count"])

    kankyo_total = _int(_path_value(kankyo_package, "source_audit_summary", "cmb_counts", "parsed"))
    kankyo_packaged_count = _int(kankyo_package["converted"])
    kankyo_runtime = _int(route_kankyo["ready_environment_record_count"])

    audio_total = _int(audio["file_count"])
    native_audio_ready = (
        route_audio.get("playback_status") == "native_bcsar_bcstm_runtime"
        and _int(route_audio["ready_runtime_record_count"]) > 0
    )
    audio_packaged = audio_total if native_audio_ready else 0
    audio_runtime_native = audio_total if native_audio_ready else 0

    model_total = _int(_path_value(inventory, "model_counts", "parsed"))
    model_rigid_ready = _int(_path_value(inventory, "model_counts", "rigid_export_candidate"))

    return [
        _family(
            "cmb_models", "CMB model", _tier_counts(model_total, model_total),
            {"not_rigid_export_candidate": model_total - model_rigid_ready,
             "missing_global_package_identity": model_total},
            ["romfs_inventory"], {"rigid_export_candidate_count": model_rigid_ready},
        ),
        _family(
            "scene_rooms", "room", _tier_counts(room_total, room_total, route_rooms, route_rooms),
            {"missing_global_scene_package": room_total - route_rooms,
             "missing_runtime_playthrough_evidence": route_rooms},
            ["scene_metadata", "route_manifest"],
            {"route_local_runtime_candidate_count": route_rooms},
        ),
        _family(
            "scene_collisions", "scene stem", _tier_counts(
                collision_total, collision_decoded, collision_packaged, min(route_collisions, collision_packaged)
            ),
            {"no_native_collision_candidate": collision_total - collision_decoded,
             "decoded_but_not_packaged": collision_decoded - collision_packaged,
             "packaged_without_global_runtime_binding": collision_packaged - min(route_collisions, collision_packaged),
             "missing_runtime_traversal_evidence": min(route_collisions, collision_packaged)},
            ["scene_metadata", "collision_package", "route_manifest"],
        ),
        _family(
            "material_programs", "CMB material", _tier_counts(material_total, material_total),
            {"raw_texture_stage_export_gap": material_gap,
             "missing_global_material_package_identity": material_total},
            ["material_inventory"],
            {"current_export_ready_count": material_export_ready,
             "current_export_ready_percentage": round(material_export_ready * 100.0 / material_total, 4)},
        ),
        _family(
            "ctxb_textures", "CTXB texture", _tier_counts(ctxb_total, ctxb_decoded, ctxb_packaged),
            {"missing_runtime_catalog_binding": ctxb_packaged},
            ["ctxb_audit", "ctxb_export"],
        ),
        _family(
            "actor_models", "actor CMB model", _tier_counts(actor_total, actor_total, actor_packaged),
            {"missing_global_actor_profile_binding": actor_packaged},
            ["actor_inventory", "static_actor_package", "skinned_bind_pose"],
            {"rigid_packaged_count": actor_rigid_packaged,
             "skinned_bind_pose_packaged_count": actor_skinned_packaged},
        ),
        _family(
            "route_visual_actor_entries", "route visual actor entry",
            _tier_counts(route_visual_entries, route_visual_entries, route_ready_entries, route_ready_entries),
            {"route_visual_actor_not_runtime_ready": route_visual_entries - route_ready_entries,
             "missing_runtime_playthrough_evidence": route_ready_entries},
            ["route_static_actors"],
        ),
        _family(
            "skeletal_animations", "CSAB payload",
            _tier_counts(csab_total, csab_total, min(csab_packaged, csab_total), min(link_csab, csab_packaged)),
            {"unresolved_or_unpackaged_csab": csab_total - min(csab_packaged, csab_total),
             "packaged_without_runtime_character_profile": min(csab_packaged, csab_total) - min(link_csab, csab_packaged),
             "missing_runtime_playthrough_evidence": min(link_csab, csab_packaged)},
            ["actor_inventory", "skinned_animation", "link_child"],
        ),
        _family(
            "material_animations", "CMAB payload", _tier_counts(cmab_total, cmab_total, link_cmab, link_cmab),
            {"cmab_not_in_runtime_profile": cmab_total - link_cmab,
             "missing_runtime_playthrough_evidence": link_cmab},
            ["actor_inventory", "link_child"],
        ),
        _family(
            "kankyo_models", "kankyo CMB model",
            _tier_counts(kankyo_total, kankyo_total, kankyo_packaged_count, min(kankyo_runtime, kankyo_packaged_count)),
            {"packaged_without_global_environment_binding": kankyo_packaged_count - min(kankyo_runtime, kankyo_packaged_count),
             "missing_runtime_playthrough_evidence": min(kankyo_runtime, kankyo_packaged_count)},
            ["kankyo_audit", "kankyo_package", "route_kankyo"],
        ),
        _family(
            "audio_containers", "BCSAR/BCSTM container",
            _tier_counts(audio_total, audio_total, audio_packaged, audio_runtime_native),
            {"native_audio_decoder_or_handoff_missing": audio_total - audio_packaged,
             "missing_runtime_playthrough_evidence": audio_runtime_native},
            ["audio_audit", "route_audio"],
            {"native_audio_runtime_ready": native_audio_ready},
        ),
        _family(
            "link_child_profile", "character profile", _tier_counts(1, 1, 1, 1),
            {"missing_runtime_playthrough_evidence": 1},
            ["link_child"],
            {"animation_count": link_csab,
             "native_bind_pose_resource_count": _int(link_child["native_bind_pose_resource_count"])},
        ),
    ]


def _opportunities(families: list[dict[str, Any]]) -> list[dict[str, Any]]:
    by_id = {family["family_id"]: family for family in families}

    def gap(family_id: str, source_tier: str, target_tier: str) -> int:
        counts = by_id[family_id]["tier_counts"]
        return max(0, _int(counts[source_tier]) - _int(counts[target_tier]))

    material_gap = _int(by_id["material_programs"]["blocker_counts"].get("raw_texture_stage_export_gap", 0))
    catalog_gain = sum(
        gap(family_id, "packaged", "runtime_candidate")
        for family_id in (
            "scene_rooms",
            "scene_collisions",
            "ctxb_textures",
            "actor_models",
            "skeletal_animations",
            "material_animations",
            "kankyo_models",
        )
    )
    rows = [
        ("global_asset_catalog", catalog_gain, 3,
         "Generate one native-identity catalog consumed by every provider; this is the prerequisite for horizontal runtime coverage."),
        ("global_scene_provider", gap("scene_rooms", "decoded", "runtime_candidate"), 3,
         "Generalize scene/setup/room packaging and lookup beyond the Kokiri route."),
        ("global_actor_profile_provider", gap("actor_models", "packaged", "runtime_candidate"), 4,
         "Generate actor/object/profile bindings from native identities instead of route-specific switches."),
        ("global_texture_catalog_binding", gap("ctxb_textures", "packaged", "runtime_candidate"), 2,
         "Bind already exported CTXB resources through the global OOT3D catalog."),
        ("material_signature_campaign", material_gap, 4,
         "Resolve the highest-yield native material signatures and rerun the complete batch."),
        ("global_collision_provider", gap("scene_collisions", "packaged", "runtime_candidate"), 3,
         "Bind accepted native collision profiles globally and add traversal evidence."),
        ("character_profile_campaign", gap("actor_models", "packaged", "runtime_candidate"), 6,
         "Promote packaged skinned models and CSAB clips through reusable character profiles."),
        ("native_audio_playback", gap("audio_containers", "decoded", "runtime_candidate"), 6,
         "Implement a reusable BCSAR/BCSTM decoder or handoff; route mapping alone is N64 fallback."),
    ]
    opportunities = []
    for opportunity_id, coverage_gain, cost, action in rows:
        opportunities.append({
            "opportunity_id": opportunity_id,
            "coverage_gain": coverage_gain,
            "estimated_cost_units": cost,
            "priority_score": round(coverage_gain / cost, 4) if cost else 0.0,
            "action": action,
        })
    return sorted(opportunities, key=lambda row: (-row["priority_score"], row["opportunity_id"]))


def build_playable_coverage_baseline(work_root: Path, *, max_age_days: int = 45,
                                     now: datetime | None = None) -> dict[str, Any]:
    now = now or datetime.now(timezone.utc)
    if now.tzinfo is None:
        now = now.replace(tzinfo=timezone.utc)
    loaded: dict[str, dict[str, Any]] = {}
    input_records: list[dict[str, Any]] = []
    missing_required: list[str] = []
    stale_inputs: list[str] = []

    for spec in INPUT_SPECS:
        path = work_root / spec.relative_path
        record: dict[str, Any] = {
            "key": spec.key,
            "path": str(path),
            "required": spec.required,
            "present": path.is_file(),
        }
        if not path.is_file():
            if spec.required:
                missing_required.append(spec.key)
            input_records.append(record)
            continue
        data = _read_json(path)
        loaded[spec.key] = data
        modified = datetime.fromtimestamp(path.stat().st_mtime, tz=timezone.utc)
        age_days = max(0.0, (now - modified).total_seconds() / 86400.0)
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        record.update({
            "modified_utc": modified.isoformat(),
            "age_days": round(age_days, 3),
            "stale": age_days > max_age_days,
            "byte_length": path.stat().st_size,
            "sha256": digest,
        })
        if record["stale"]:
            stale_inputs.append(spec.key)
        input_records.append(record)

    families: list[dict[str, Any]] = []
    opportunities: list[dict[str, Any]] = []
    status = "blocked_missing_required_inputs" if missing_required else "complete"
    if not missing_required:
        families = _build_families(loaded)
        opportunities = _opportunities(families)

    return {
        "format": FORMAT,
        "generated_utc": now.astimezone(timezone.utc).isoformat(),
        "work_root": str(work_root),
        "status": status,
        "max_input_age_days": max_age_days,
        "missing_required_inputs": missing_required,
        "stale_inputs": stale_inputs,
        "input_count": len(input_records),
        "present_input_count": sum(1 for record in input_records if record["present"]),
        "inputs": input_records,
        "tier_definitions": {TIER_NAMES[index]: index for index in sorted(TIER_NAMES)},
        "family_count": len(families),
        "families": families,
        "opportunities": opportunities,
        "notes": [
            "Tier counts are conservative and require identity-preserving evidence at every stage.",
            "Export-ready metrics do not count as packaged unless a package summary exists.",
            "Runtime-candidate records do not count as playable without playthrough/runtime evidence.",
            "N64 audio fallback mappings never count as native OOT3D audio runtime coverage.",
        ],
    }


def render_markdown(report: dict[str, Any]) -> str:
    lines = [
        "# OOT3D Playable Asset Coverage Baseline",
        "",
        f"- Status: `{report['status']}`",
        f"- Generated: `{report['generated_utc']}`",
        f"- Work root: `{report['work_root']}`",
        f"- Inputs: `{report['present_input_count']}/{report['input_count']}` present",
        f"- Stale inputs: `{len(report['stale_inputs'])}`",
        "",
    ]
    if report["missing_required_inputs"]:
        lines.extend(["## Missing Inputs", ""])
        lines.extend(f"- `{key}`" for key in report["missing_required_inputs"])
        lines.append("")
        return "\n".join(lines) + "\n"

    lines.extend([
        "## Coverage",
        "",
        "| Family | Unit | T0 | T1 | T2 | T3 | T4 | T5 |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |",
    ])
    for family in report["families"]:
        counts = family["tier_counts"]
        lines.append(
            f"| `{family['family_id']}` | {family['unit']} | {counts['inventoried']} | "
            f"{counts['decoded']} | {counts['packaged']} | {counts['runtime_candidate']} | "
            f"{counts['playable']} | {counts['parity_validated']} |"
        )

    lines.extend(["", "## Highest-Yield Opportunities", "",
                  "| Rank | Opportunity | Gain | Cost | Score | Action |",
                  "| ---: | --- | ---: | ---: | ---: | --- |"])
    for index, opportunity in enumerate(report["opportunities"], start=1):
        lines.append(
            f"| {index} | `{opportunity['opportunity_id']}` | {opportunity['coverage_gain']} | "
            f"{opportunity['estimated_cost_units']} | {opportunity['priority_score']} | "
            f"{opportunity['action']} |"
        )

    lines.extend(["", "## Blockers", ""])
    for family in report["families"]:
        if not family["blocker_counts"]:
            continue
        lines.append(f"### `{family['family_id']}`")
        lines.append("")
        for blocker, count in family["blocker_counts"].items():
            lines.append(f"- `{blocker}`: {count}")
        lines.append("")

    lines.extend(["## Evidence Freshness", "",
                  "| Input | Present | Age days | Stale | Path |",
                  "| --- | --- | ---: | --- | --- |"])
    for record in report["inputs"]:
        lines.append(
            f"| `{record['key']}` | {str(record['present']).lower()} | {record.get('age_days', '')} | "
            f"{str(record.get('stale', False)).lower()} | `{record['path']}` |"
        )
    lines.append("")
    return "\n".join(lines)


def write_playable_coverage_baseline(work_root: Path, output_json: Path, output_md: Path,
                                     *, max_age_days: int = 45) -> dict[str, Any]:
    report = build_playable_coverage_baseline(work_root, max_age_days=max_age_days)
    output_json.parent.mkdir(parents=True, exist_ok=True)
    output_md.parent.mkdir(parents=True, exist_ok=True)
    output_json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")
    output_md.write_text(render_markdown(report), encoding="utf-8", newline="\n")
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Build the global playable OOT3D asset coverage baseline.")
    parser.add_argument("--work-root", type=Path, required=True)
    parser.add_argument("--output-json", type=Path, required=True)
    parser.add_argument("--output-md", type=Path, required=True)
    parser.add_argument("--max-age-days", type=int, default=45)
    parser.add_argument("--require-complete", action="store_true")
    args = parser.parse_args(argv)
    report = write_playable_coverage_baseline(
        args.work_root, args.output_json, args.output_md, max_age_days=args.max_age_days
    )
    print(args.output_json)
    print(args.output_md)
    if args.require_complete and report["status"] != "complete":
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
