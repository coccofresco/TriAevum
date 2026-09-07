from __future__ import annotations

from collections import Counter
from pathlib import Path
from typing import Any

from .kokiri_actor_asset_readiness import read_json_file, write_json
from .romfs_inventory import sorted_counter


PLAN_FORMAT = "oot3d_kokiri_audio_binding_plan_v2"
RUNTIME_MANIFEST_FORMAT = "oot3d_kokiri_runtime_manifest_v1"
AUDIO_ASSET_AUDIT_FORMAT = "oot3d_audio_asset_audit_v1"
ROUTE_ID = "link_house_to_kokiri_forest"

READY_AUDIO_SUPPORT = "present_valid_audio_container_header"
MAPPED_NATIVE_AUDIO = "mapped_to_oot3d_native_audio"
NEEDS_AUDIO_MAPPING = "needs_route_audio_mapping"
NATIVE_PLAYBACK = "native_bcsar_bcstm_runtime"


def export_kokiri_audio_binding_plan(
    runtime_manifest_path: Path,
    audio_asset_audit_path: Path,
    output_path: Path | None = None,
    *,
    sample_limit: int = 100,
) -> dict[str, object]:
    runtime_manifest = read_json_file(runtime_manifest_path, "Kokiri runtime manifest")
    audio_asset_audit = read_json_file(audio_asset_audit_path, "OOT3D audio asset audit")

    if runtime_manifest.get("format") != RUNTIME_MANIFEST_FORMAT:
        raise ValueError(f"{runtime_manifest_path}: unexpected Kokiri runtime manifest format")
    if runtime_manifest.get("route_id") != ROUTE_ID:
        raise ValueError(f"{runtime_manifest_path}: unexpected Kokiri route id")
    if audio_asset_audit.get("format") != AUDIO_ASSET_AUDIT_FORMAT:
        raise ValueError(f"{audio_asset_audit_path}: unexpected OOT3D audio asset audit format")

    audio_summary = object_value(runtime_manifest, "audio_summary")
    runtime_mapping = object_value(audio_summary, "runtime_mapping")
    profiles = array_values(runtime_mapping, "profiles")
    sound_setting_records = array_values(runtime_mapping, "records")
    support_source_records = audio_support_source_records(audio_asset_audit)

    issue_counts: Counter[str] = Counter()
    profile_status_counts: Counter[str] = Counter()
    sound_setting_status_counts: Counter[str] = Counter()
    support_status_counts: Counter[str] = Counter()

    profile_binding_records = [
        audio_profile_binding_record(profile, issue_counts)
        for profile in profiles
    ]
    for record in profile_binding_records:
        profile_status_counts[str(record.get("binding_status", "unknown"))] += 1

    sound_setting_binding_records = [
        audio_sound_setting_binding_record(record, issue_counts)
        for record in sound_setting_records
    ]
    for record in sound_setting_binding_records:
        sound_setting_status_counts[str(record.get("binding_status", "unknown"))] += 1

    support_records = [
        audio_support_record(record, issue_counts)
        for record in support_source_records
    ]
    for record in support_records:
        support_status_counts[str(record.get("support_status", "unknown"))] += 1

    audit_file_count = int_value(audio_asset_audit.get("file_count"), len(support_records))
    if audit_file_count != len(support_records):
        issue_counts["audio_support_records_not_complete"] += max(0, audit_file_count - len(support_records))

    audio_support_issue_total = int_value(audio_asset_audit.get("issue_count"), 0)
    if audio_support_issue_total:
        issue_counts["audio_asset_audit_issues"] += audio_support_issue_total

    ready_support_records = [
        record for record in support_records if record.get("support_status") == READY_AUDIO_SUPPORT
    ]
    ready_binding_issue_total = sum(
        int(value)
        for key, value in issue_counts.items()
        if key.startswith("ready_")
    )

    sound_setting_count = int_value(runtime_mapping.get("sound_setting_count"), len(sound_setting_records))
    mapped_sound_setting_count = int_value(
        runtime_mapping.get("mapped_record_count"),
        sound_setting_status_counts[MAPPED_NATIVE_AUDIO],
    )
    unresolved_sound_setting_count = int_value(
        runtime_mapping.get("unresolved_record_count"),
        sound_setting_count - mapped_sound_setting_count,
    )
    profile_count = int_value(runtime_mapping.get("profile_count"), len(profiles))
    mapped_profile_count = int_value(
        runtime_mapping.get("mapped_profile_count"),
        profile_status_counts[MAPPED_NATIVE_AUDIO],
    )
    unresolved_profile_count = int_value(
        runtime_mapping.get("unresolved_profile_count"),
        profile_count - mapped_profile_count,
    )
    route_audio_binding_status = (
        MAPPED_NATIVE_AUDIO
        if unresolved_sound_setting_count == 0 and unresolved_profile_count == 0
        else NEEDS_AUDIO_MAPPING
    )

    plan: dict[str, object] = {
        "format": PLAN_FORMAT,
        "runtime_manifest": str(runtime_manifest_path),
        "audio_asset_audit": str(audio_asset_audit_path),
        "route_id": runtime_manifest.get("route_id"),
        "runtime_enablement_status": runtime_manifest.get("runtime_enablement_status"),
        "audio_summary_status": audio_summary.get("status"),
        "consumer": audio_summary.get("consumer", "oot3d_native_audio_service"),
        "playback_status": NATIVE_PLAYBACK,
        "route_audio_binding_status": route_audio_binding_status,
        "sound_setting_count": sound_setting_count,
        "mapped_sound_setting_count": mapped_sound_setting_count,
        "unresolved_sound_setting_count": unresolved_sound_setting_count,
        "route_audio_profile_count": profile_count,
        "mapped_route_audio_profile_count": mapped_profile_count,
        "unresolved_route_audio_profile_count": unresolved_profile_count,
        "audio_support_file_count": audit_file_count,
        "audio_support_byte_total": int_value(
            audio_asset_audit.get("total_size"),
            sum(int_value(record.get("size"), 0) for record in support_records),
        ),
        "audio_support_extension_counts": audio_asset_audit.get("extension_counts", {}),
        "audio_support_category_counts": audio_asset_audit.get("category_counts", {}),
        "audio_support_issue_total": audio_support_issue_total,
        "ready_audio_support_record_count": len(ready_support_records),
        "ready_audio_support_byte_total": sum(
            int_value(record.get("size"), 0)
            for record in ready_support_records
        ),
        "ready_runtime_record_count": profile_status_counts[MAPPED_NATIVE_AUDIO],
        "profile_binding_status_counts": sorted_counter(profile_status_counts),
        "sound_setting_binding_status_counts": sorted_counter(sound_setting_status_counts),
        "audio_support_status_counts": sorted_counter(support_status_counts),
        "binding_issue_counts": sorted_counter(issue_counts),
        "ready_binding_issue_total": ready_binding_issue_total,
        "profile_binding_records": profile_binding_records[:sample_limit],
        "sound_setting_records": sound_setting_binding_records[:sample_limit],
        "audio_support_records": support_records[:sample_limit],
        "runtime_policy": [
            "OOT3D BCSAR/BCSTM assets are decoded by the native audio service.",
            "Scene command 0x15 is preserved and routed as native sound spec, nature ambience, and full BCSAR sound id.",
            "No N64 sequence remapping or runtime audio-asset substitution is permitted.",
            "No raw OOT3D audio payloads are copied into git by this plan.",
        ],
    }
    if output_path is not None:
        write_json(output_path, plan)
    return plan


def audio_profile_binding_record(
    profile: dict[str, object],
    issue_counts: Counter[str],
) -> dict[str, object]:
    source_status = str(profile.get("status", "unknown"))
    native_settings = object_value(profile, "native_audio_settings")
    binding_status = MAPPED_NATIVE_AUDIO if source_status == "mapped" and native_settings else NEEDS_AUDIO_MAPPING
    if binding_status != MAPPED_NATIVE_AUDIO:
        issue_counts["unresolved_audio_profile_mapping"] += 1

    return {
        "path": profile.get("path"),
        "scene_stem": profile.get("scene_stem"),
        "setup_index": profile.get("setup_index"),
        "oot3d_sound_settings": profile.get("oot3d_sound_settings", {}),
        "native_audio_settings": native_settings,
        "mapping_reason": profile.get("mapping_reason"),
        "record_count": profile.get("record_count"),
        "source_status": source_status,
        "binding_status": binding_status,
        "playback_status": NATIVE_PLAYBACK,
    }


def audio_sound_setting_binding_record(
    record: dict[str, object],
    issue_counts: Counter[str],
) -> dict[str, object]:
    source_status = str(record.get("status", "unknown"))
    native_settings = object_value(record, "native_audio_settings")
    binding_status = MAPPED_NATIVE_AUDIO if source_status == "mapped" and native_settings else NEEDS_AUDIO_MAPPING
    if binding_status != MAPPED_NATIVE_AUDIO:
        issue_counts["unresolved_audio_sound_setting_mapping"] += 1

    return {
        "path": record.get("path"),
        "scene_stem": record.get("scene_stem"),
        "setup_index": record.get("setup_index"),
        "oot3d_sound_settings": record.get("oot3d_sound_settings", {}),
        "native_audio_settings": native_settings,
        "mapping_reason": record.get("mapping_reason"),
        "source_status": source_status,
        "binding_status": binding_status,
        "playback_status": NATIVE_PLAYBACK,
    }


def audio_support_source_records(audio_asset_audit: dict[str, object]) -> list[dict[str, object]]:
    records = array_values(audio_asset_audit, "records")
    if records:
        return records
    return array_values(audio_asset_audit, "sample_records")


def audio_support_record(
    record: dict[str, object],
    issue_counts: Counter[str],
) -> dict[str, object]:
    issues = array_value(record.get("issues"))
    section_records = [
        section
        for section in array_value(record.get("sections"))
        if isinstance(section, dict)
    ]
    sections = [
        {
            "index": section.get("index"),
            "type": section.get("type"),
            "magic": section.get("magic"),
            "size": section.get("size"),
            "in_bounds": section.get("in_bounds"),
            "declared_size_matches_table_size": section.get("declared_size_matches_table_size"),
        }
        for section in section_records
    ]

    ready = (
        not issues
        and bool(record.get("declared_size_matches_file_size"))
        and all(bool(section.get("in_bounds")) for section in section_records)
        and all(bool(section.get("declared_size_matches_table_size")) for section in section_records)
    )
    support_status = READY_AUDIO_SUPPORT if ready else "audio_container_header_audit_issue"
    if not ready:
        issue_counts["ready_audio_support_header_issue"] += 1

    return {
        "path": record.get("path"),
        "extension": record.get("extension"),
        "category": record.get("category"),
        "size": record.get("size"),
        "magic": record.get("magic"),
        "bom_hex": record.get("bom_hex"),
        "byte_order": record.get("byte_order"),
        "version": record.get("version"),
        "header_size": record.get("header_size"),
        "section_count": record.get("section_count"),
        "section_layout": section_layout(section_records),
        "sections": sections,
        "support_status": support_status,
        "issue_count": len(issues),
    }


def section_layout(sections: list[dict[str, object]]) -> str:
    return ",".join(
        f"{section.get('type')}:{section.get('magic')}"
        for section in sections
    )


def object_value(container: dict[str, object], key: str) -> dict[str, object]:
    value = container.get(key)
    return value if isinstance(value, dict) else {}


def array_values(container: dict[str, object], key: str) -> list[dict[str, object]]:
    return [
        value
        for value in array_value(container.get(key))
        if isinstance(value, dict)
    ]


def array_value(value: Any) -> list[dict[str, object]]:
    return value if isinstance(value, list) else []


def int_value(value: object, default: int) -> int:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    return default
