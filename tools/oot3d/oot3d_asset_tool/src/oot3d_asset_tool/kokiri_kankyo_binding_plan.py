from __future__ import annotations

from collections import Counter
from pathlib import Path
from typing import Any

from .kokiri_actor_asset_readiness import read_json_file, write_json
from .romfs_inventory import sorted_counter


READY_KANKYO_SELECTION = "ready_kankyo_environment_binding"
FILTER_ONLY_STATUS = "n64_filter_only_no_kankyo_resource_needed"
SELECTED_KANKYO_STATUS = "selected_kankyo_archive_candidate"


def export_kokiri_kankyo_binding_plan(
    runtime_manifest_path: Path,
    kankyo_export_manifest_path: Path,
    output_path: Path | None = None,
    *,
    sample_limit: int = 100,
) -> dict[str, object]:
    runtime_manifest = read_json_file(runtime_manifest_path, "Kokiri runtime manifest")
    kankyo_export_manifest = read_json_file(
        kankyo_export_manifest_path,
        "kankyo environment export manifest",
    )

    selection = (
        runtime_manifest.get("kankyo_summary", {})
        if isinstance(runtime_manifest.get("kankyo_summary"), dict)
        else {}
    ).get("runtime_selection", {})
    if not isinstance(selection, dict):
        selection = {}

    profile_records = [
        record
        for record in selection.get("records", [])
        if isinstance(record, dict)
    ]
    converted_records = [
        record
        for record in kankyo_export_manifest.get("records", [])
        if isinstance(record, dict) and record.get("status") == "converted"
    ]

    converted_by_archive = collect_converted_by_archive(converted_records)
    selected_archives: set[str] = set()
    selected_environment_groups: set[str] = set()
    profile_binding_records: list[dict[str, object]] = []
    ready_records_by_asset_id: dict[str, dict[str, object]] = {}
    status_counts: Counter[str] = Counter()
    profile_status_counts: Counter[str] = Counter()
    issue_counts: Counter[str] = Counter()

    for profile in profile_records:
        record = kankyo_profile_binding_record(
            profile,
            converted_by_archive,
            issue_counts,
            sample_limit=sample_limit,
        )
        profile_binding_records.append(record)
        status = str(record.get("binding_status", "unknown"))
        profile_status_counts[status] += 1
        status_counts[str(profile.get("status", "unknown"))] += 1

        if status != READY_KANKYO_SELECTION:
            continue

        for archive_name in record.get("archive_candidates", []):
            selected_archives.add(str(archive_name))
        for group_name in record.get("environment_groups", []):
            selected_environment_groups.add(str(group_name))
        for ready_record in record.get("ready_environment_records", []):
            if not isinstance(ready_record, dict):
                continue
            asset_id = str(ready_record.get("asset_id", ""))
            if asset_id:
                ready_records_by_asset_id[asset_id] = ready_record

    ready_environment_records = [
        ready_records_by_asset_id[asset_id]
        for asset_id in sorted(ready_records_by_asset_id)
    ]
    ready_resource_count = sum(
        int(record.get("resource_count", 0))
        for record in ready_environment_records
    )
    ready_top_display_list_count = sum(
        1
        for record in ready_environment_records
        if record.get("top_display_list_resource_status") == "present"
    )
    group_counts = Counter(
        str(record.get("environment_group", "unknown"))
        for record in ready_environment_records
    )
    archive_counts = Counter(
        str(record.get("archive_path", "unknown"))
        for record in ready_environment_records
    )

    ready_issue_total = sum(
        int(value)
        for key, value in issue_counts.items()
        if key.startswith("ready_")
    )

    plan: dict[str, object] = {
        "format": "oot3d_kokiri_kankyo_binding_plan_v1",
        "runtime_manifest": str(runtime_manifest_path),
        "kankyo_export_manifest": str(kankyo_export_manifest_path),
        "route_id": runtime_manifest.get("route_id"),
        "runtime_enablement_status": runtime_manifest.get("runtime_enablement_status"),
        "skybox_profile_count": len(profile_records),
        "selected_kankyo_profile_count": profile_status_counts[READY_KANKYO_SELECTION],
        "n64_filter_only_profile_count": status_counts[FILTER_ONLY_STATUS],
        "unresolved_selection_count": int(selection.get("unresolved_selection_count", 0)),
        "selected_archives": sorted(selected_archives),
        "selected_archive_count": len(selected_archives),
        "selected_environment_groups": sorted(selected_environment_groups),
        "ready_environment_record_count": len(ready_environment_records),
        "ready_environment_resource_count": ready_resource_count,
        "ready_top_display_list_count": ready_top_display_list_count,
        "ready_environment_archive_counts": sorted_counter(archive_counts),
        "ready_environment_group_counts": sorted_counter(group_counts),
        "profile_source_status_counts": sorted_counter(status_counts),
        "profile_binding_status_counts": sorted_counter(profile_status_counts),
        "binding_issue_counts": sorted_counter(issue_counts),
        "ready_binding_issue_total": ready_issue_total,
        "profile_records": profile_binding_records[:sample_limit],
        "ready_environment_records": ready_environment_records[:sample_limit],
        "runtime_policy": [
            "Only ready_kankyo_environment_binding profiles are OOT3D kankyo draw candidates.",
            "N64 filter-only skybox profiles remain Shipwright/N64 environment fallback.",
            "Generated kankyo resources stay outside git and must be installed before runtime drawing can use them.",
            "CMAB playback, CTXB parameter binding, and final sky draw transforms remain separate runtime work.",
        ],
    }
    if output_path is not None:
        write_json(output_path, plan)
    return plan


def collect_converted_by_archive(
    records: list[dict[str, object]],
) -> dict[str, list[dict[str, object]]]:
    result: dict[str, list[dict[str, object]]] = {}
    for record in records:
        archive_path = str(record.get("archive_path", ""))
        if not archive_path:
            continue
        result.setdefault(archive_path, []).append(record)
    return result


def kankyo_profile_binding_record(
    profile: dict[str, object],
    converted_by_archive: dict[str, list[dict[str, object]]],
    issue_counts: Counter[str],
    *,
    sample_limit: int,
) -> dict[str, object]:
    runtime_selection = profile.get("runtime_selection", {})
    if not isinstance(runtime_selection, dict):
        runtime_selection = {}

    base: dict[str, object] = {
        "skybox_id": profile.get("skybox_id"),
        "skybox_name": profile.get("skybox_name"),
        "weather_or_unk_05": profile.get("weather_or_unk_05"),
        "indoors": profile.get("indoors"),
        "setup_count": profile.get("setup_count"),
        "locations": profile.get("locations", []),
        "source_status": profile.get("status"),
    }

    if profile.get("status") == FILTER_ONLY_STATUS:
        return {
            **base,
            "binding_status": "n64_filter_only_fallback",
            "fallback": runtime_selection.get("fallback", "shipwright_n64_sky_environment"),
        }

    if profile.get("status") != SELECTED_KANKYO_STATUS:
        return {
            **base,
            "binding_status": "needs_kankyo_runtime_selection",
            "runtime_selection_kind": runtime_selection.get("kind", "unknown"),
        }

    archive_candidates = [
        str(value)
        for value in runtime_selection.get("archive_candidates", [])
        if str(value)
    ]
    environment_groups = {
        str(value)
        for value in runtime_selection.get("environment_groups", [])
        if str(value)
    }
    ready_records: list[dict[str, object]] = []
    missing_archives: list[str] = []
    group_counts: Counter[str] = Counter()

    for archive_name in archive_candidates:
        archive_records = converted_by_archive.get(archive_name)
        if not archive_records:
            missing_archives.append(archive_name)
            issue_counts["ready_missing_kankyo_archive_records"] += 1
            continue
        for export_record in archive_records:
            if environment_groups and str(export_record.get("environment_group")) not in environment_groups:
                continue
            compact = compact_kankyo_environment_record(export_record)
            ready_records.append(compact)
            group_counts[str(compact.get("environment_group", "unknown"))] += 1
            if compact.get("top_display_list_resource_status") != "present":
                issue_counts["ready_missing_top_display_list_resource"] += 1

    if not ready_records:
        issue_counts["ready_empty_kankyo_environment_selection"] += 1

    return {
        **base,
        "binding_status": READY_KANKYO_SELECTION,
        "archive_candidates": archive_candidates,
        "environment_groups": sorted(environment_groups),
        "missing_archive_candidates": missing_archives,
        "ready_environment_record_count": len(ready_records),
        "ready_environment_resource_count": sum(
            int(record.get("resource_count", 0))
            for record in ready_records
        ),
        "ready_environment_group_counts": sorted_counter(group_counts),
        "ready_environment_records": ready_records[:sample_limit],
    }


def compact_kankyo_environment_record(record: dict[str, object]) -> dict[str, object]:
    top_display_list = top_display_list_path(record)
    return {
        "asset_id": record.get("asset_id"),
        "archive_path": record.get("archive_path"),
        "embedded_name": record.get("embedded_name"),
        "model_name": record.get("model_name"),
        "environment_group": record.get("environment_group"),
        "resource_root": record.get("resource_root"),
        "symbol": record.get("symbol"),
        "resource_count": record.get("resource_count"),
        "resource_kind_counts": resource_kind_counts(record.get("resources", [])),
        "top_display_list": top_display_list,
        "top_display_list_resource_status": (
            "present"
            if display_list_resource_present(record, top_display_list)
            else "missing_display_list_resource"
        ),
    }


def resource_kind_counts(resources: object) -> dict[str, int]:
    counts: Counter[str] = Counter()
    if not isinstance(resources, list):
        return {}
    for resource in resources:
        if isinstance(resource, dict):
            counts[str(resource.get("kind", "unknown"))] += 1
    return sorted_counter(counts)


def top_display_list_path(record: dict[str, object]) -> str | None:
    resource_root = str(record.get("resource_root", "")).strip("/")
    symbol = str(record.get("symbol", "")).strip("/")
    if not resource_root or not symbol:
        return None
    return f"{resource_root}/{symbol}"


def display_list_resource_present(record: dict[str, object], path: str | None) -> bool:
    if path is None:
        return False
    resources: Any = record.get("resources", [])
    if not isinstance(resources, list):
        return False
    return any(
        isinstance(resource, dict)
        and resource.get("kind") == "DisplayList"
        and str(resource.get("path", "")).strip("/") == path
        for resource in resources
    )
