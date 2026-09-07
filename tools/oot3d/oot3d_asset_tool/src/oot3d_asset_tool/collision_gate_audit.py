from __future__ import annotations

import json
import re
import tempfile
from collections import Counter
from pathlib import Path

from .binary import ParseError
from .collision import (
    DEFAULT_REFERENCE_FLOOR_PROBE_COUNT,
    DEFAULT_REFERENCE_FLOOR_QUERY_ABOVE,
    DEFAULT_REFERENCE_FLOOR_TOLERANCE,
    DEFAULT_VISUAL_MESH_FLOOR_PROBE_COUNT,
    COLLISION_RESOURCE_FORMATS,
    discover_scene_collision_path_from_o2r,
    export_zsi_scene_collision,
    validate_collision_resource_format,
)
from .romfs_inventory import sorted_counter
from .zsi import ZsiFile
from .zsi_scene_audit import zsi_scene_role, zsi_scene_stem


ROOM_ZSI_RE = re.compile(r"^(.+)_(\d+)(?:_dd)?_info\.zsi$", re.IGNORECASE)


def audit_zsi_collision_gates(
    scene_root: Path,
    reference_o2r: Path,
    output_path: Path | None = None,
    *,
    scenes: tuple[str, ...] = (),
    limit: int | None = None,
    sample_limit: int = 100,
    include_records: bool = True,
    resource_format: str = "binary",
    reference_floor_probe_count: int = DEFAULT_REFERENCE_FLOOR_PROBE_COUNT,
    reference_floor_query_above: float = DEFAULT_REFERENCE_FLOOR_QUERY_ABOVE,
    reference_floor_tolerance: float = DEFAULT_REFERENCE_FLOOR_TOLERANCE,
    visual_room_dir: Path | None = None,
    visual_floor_probe_count: int = DEFAULT_VISUAL_MESH_FLOOR_PROBE_COUNT,
) -> dict[str, object]:
    if not scene_root.is_dir():
        raise ParseError(f"{scene_root}: expected an extracted OOT3D scene directory")
    if not reference_o2r.exists():
        raise ParseError(f"{reference_o2r}: reference OTR/O2R archive not found")
    if visual_room_dir is not None and not visual_room_dir.is_dir():
        raise ParseError(f"{visual_room_dir}: expected an extracted OOT3D scene directory")
    if limit is not None and limit <= 0:
        raise ParseError("collision gate audit limit must be positive")
    if sample_limit < 0:
        raise ParseError("collision gate audit sample limit must be non-negative")
    if visual_floor_probe_count <= 0:
        raise ParseError("visual floor probe count must be positive")
    validate_collision_resource_format(resource_format)

    scene_filter = {scene.lower() for scene in scenes}
    records: list[dict[str, object]] = []
    all_records: list[dict[str, object]] = []
    sample_records: list[dict[str, object]] = []
    status_counts: Counter[str] = Counter()
    failed_check_counts: Counter[str] = Counter()
    visual_mesh_audit_status_counts: Counter[str] = Counter()
    visual_mesh_diagnosis_counts: Counter[str] = Counter()
    visual_source_spawn_audit_status_counts: Counter[str] = Counter()
    selected_scene_files = [
        path
        for path in sorted(scene_root.rglob("*.zsi"))
        if zsi_scene_role(path) == "scene"
        and (not scene_filter or zsi_scene_stem(path) in scene_filter)
    ]
    if limit is not None:
        selected_scene_files = selected_scene_files[:limit]

    with tempfile.TemporaryDirectory(prefix="oot3d_collision_gate_") as tmp:
        temp_root = Path(tmp)
        for scene_zsi_path in selected_scene_files:
            record = audit_zsi_collision_gate_record(
                scene_zsi_path,
                scene_root,
                reference_o2r,
                temp_root,
                resource_format=resource_format,
                reference_floor_probe_count=reference_floor_probe_count,
                reference_floor_query_above=reference_floor_query_above,
                reference_floor_tolerance=reference_floor_tolerance,
                visual_room_dir=visual_room_dir,
                visual_floor_probe_count=visual_floor_probe_count,
            )
            all_records.append(record)
            status_counts[str(record["status"])] += 1
            failed_check_counts.update(str(check) for check in record.get("failed_checks", []))
            visual_audit = record.get("visual_mesh_floor_audit")
            if isinstance(visual_audit, dict):
                visual_mesh_audit_status_counts[str(visual_audit.get("status", "unknown"))] += 1
                diagnosis = visual_audit.get("diagnosis")
                if diagnosis is not None:
                    visual_mesh_diagnosis_counts[str(diagnosis)] += 1
            visual_spawn_audit = record.get("visual_source_spawn_floor_audit")
            if isinstance(visual_spawn_audit, dict):
                visual_source_spawn_audit_status_counts[
                    str(visual_spawn_audit.get("status", "unknown"))
                ] += 1
            if include_records:
                records.append(record)
            if len(sample_records) < sample_limit:
                sample_records.append(record)

    scene_status = summarize_collision_gate_scenes(all_records)

    audit = {
        "format": "oot3d_zsi_collision_gate_audit_v1",
        "scene_root": str(scene_root),
        "reference_o2r": str(reference_o2r),
        "scene_filter": sorted(scene_filter),
        "limit": limit,
        "sample_limit": sample_limit,
        "resource_format": resource_format,
        "reference_floor_probe_count": reference_floor_probe_count,
        "reference_floor_query_above": reference_floor_query_above,
        "reference_floor_tolerance": reference_floor_tolerance,
        "visual_room_dir": str(visual_room_dir) if visual_room_dir is not None else None,
        "visual_floor_probe_count": visual_floor_probe_count,
        "scene_file_count": len(selected_scene_files),
        "accepted_count": status_counts["accepted"],
        "rejected_count": status_counts["rejected"],
        "fallback_count": sum(
            count
            for status, count in status_counts.items()
            if status not in {"accepted", "rejected"}
        ),
        "status_counts": sorted_counter(status_counts),
        "failed_check_counts": sorted_counter(failed_check_counts),
        "visual_mesh_audit_status_counts": sorted_counter(visual_mesh_audit_status_counts),
        "visual_mesh_diagnosis_counts": sorted_counter(visual_mesh_diagnosis_counts),
        "visual_source_spawn_audit_status_counts": sorted_counter(
            visual_source_spawn_audit_status_counts
        ),
        "scene_status_counts": scene_status["scene_status_counts"],
        "accepted_scene_count": scene_status["accepted_scene_count"],
        "blocked_scene_count": scene_status["blocked_scene_count"],
        "fallback_scene_count": scene_status["fallback_scene_count"],
        "scene_records": scene_status["scene_records"],
        "sample_records": sample_records,
        "records": records if include_records else [],
        "notes": [
            "Each accepted record was decoded from native OOT3D ZSI collision data and compared against the N64/Shipwright reference resource.",
            "Records that cannot resolve a reference collision path remain fallback candidates; the audit does not enable runtime replacement.",
        ],
    }
    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(
            json.dumps(audit, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
    return audit


def export_zsi_collision_activation_manifest(
    scene_root: Path,
    reference_o2r: Path,
    output_path: Path,
    resource_output_dir: Path,
    *,
    scenes: tuple[str, ...] = (),
    limit: int | None = None,
    sample_limit: int = 100,
    resource_format: str = "binary",
    reference_floor_probe_count: int = DEFAULT_REFERENCE_FLOOR_PROBE_COUNT,
    reference_floor_query_above: float = DEFAULT_REFERENCE_FLOOR_QUERY_ABOVE,
    reference_floor_tolerance: float = DEFAULT_REFERENCE_FLOOR_TOLERANCE,
    visual_room_dir: Path | None = None,
    visual_floor_probe_count: int = DEFAULT_VISUAL_MESH_FLOOR_PROBE_COUNT,
) -> dict[str, object]:
    audit = audit_zsi_collision_gates(
        scene_root,
        reference_o2r,
        None,
        scenes=scenes,
        limit=limit,
        sample_limit=sample_limit,
        include_records=True,
        resource_format=resource_format,
        reference_floor_probe_count=reference_floor_probe_count,
        reference_floor_query_above=reference_floor_query_above,
        reference_floor_tolerance=reference_floor_tolerance,
        visual_room_dir=visual_room_dir,
        visual_floor_probe_count=visual_floor_probe_count,
    )

    accepted_by_candidate = {
        (str(record["scene"]), str(record["path"])): record
        for record in audit.get("records", [])
        if record.get("status") == "accepted"
    }
    activation_records: list[dict[str, object]] = []
    fallback_records: list[dict[str, object]] = []
    resource_output_dir.mkdir(parents=True, exist_ok=True)

    for scene_record in audit.get("scene_records", []):
        scene = str(scene_record["scene"])
        if scene_record.get("status") != "accepted":
            fallback_records.append(
                {
                    "scene": scene,
                    "status": scene_record.get("status"),
                    "reason": "n64_fallback_retained",
                    "file_count": scene_record.get("file_count"),
                    "rejected_paths": scene_record.get("rejected_paths", []),
                    "fallback_paths": scene_record.get("fallback_paths", []),
                    "rejected_records": scene_record.get("rejected_records", []),
                    "fallback_file_records": scene_record.get("fallback_file_records", []),
                }
            )
            continue

        accepted_key = (scene, str(scene_record["activation_candidate_path"]))
        accepted_record = accepted_by_candidate[accepted_key]
        source_zsi = scene_root / str(accepted_record["path"])
        resource_path = str(accepted_record["reference_resource_path"])
        visual_models = None
        if visual_room_dir is not None:
            visual_models = load_scene_visual_room_models(visual_room_dir, scene)
        result = export_zsi_scene_collision(
            source_zsi,
            resource_output_dir / scene,
            resource_path,
            resource_format=resource_format,
            reference_o2r=reference_o2r,
            reference_floor_probe_count=reference_floor_probe_count,
            reference_floor_query_above=reference_floor_query_above,
            reference_floor_tolerance=reference_floor_tolerance,
            require_reference_match=True,
            visual_models=visual_models,
            visual_floor_probe_count=visual_floor_probe_count,
        )
        activation_records.append(
            {
                "status": "converted",
                "kind": "CollisionHeader",
                "scene": scene,
                "source": "oot3d_zsi_native_collision",
                "source_zsi": str(source_zsi),
                "resource_path": resource_path,
                "reference_resource_path_source": accepted_record.get(
                    "reference_resource_path_source"
                ),
                "resource_format": resource_format,
                "activation_policy": "accepted_by_native_zsi_reference_gate",
                "summary": result.stats,
                "resources": [result.resource.__dict__],
            }
        )

    manifest = {
        "format": "oot3d_zsi_collision_activation_manifest_v1",
        "scene_root": str(scene_root),
        "reference_o2r": str(reference_o2r),
        "resource_output_dir": str(resource_output_dir),
        "resource_format": resource_format,
        "activation_policy": (
            "Only scene records accepted by audit-zsi-collision-gates are exported. "
            "All rejected, missing-reference, or no-candidate scenes retain N64 fallback."
        ),
        "audit_status_counts": audit.get("status_counts", {}),
        "audit_scene_status_counts": audit.get("scene_status_counts", {}),
        "accepted_scene_count": len(activation_records),
        "fallback_scene_count": len(fallback_records),
        "records": activation_records,
        "fallback_records": fallback_records,
    }
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")
    return manifest


def summarize_collision_gate_scenes(records: list[dict[str, object]]) -> dict[str, object]:
    grouped: dict[str, list[dict[str, object]]] = {}
    for record in records:
        grouped.setdefault(str(record["scene"]), []).append(record)

    scene_records: list[dict[str, object]] = []
    status_counts: Counter[str] = Counter()
    for scene, scene_records_raw in sorted(grouped.items()):
        accepted = [record for record in scene_records_raw if record.get("status") == "accepted"]
        rejected = [record for record in scene_records_raw if record.get("status") == "rejected"]
        if accepted:
            scene_status = "accepted"
            activation_candidate = accepted[0]
        elif rejected:
            scene_status = "blocked"
            activation_candidate = None
        else:
            scene_status = "fallback"
            activation_candidate = None
        status_counts[scene_status] += 1
        scene_records.append(
            {
                "scene": scene,
                "status": scene_status,
                "file_count": len(scene_records_raw),
                "accepted_file_count": len(accepted),
                "rejected_file_count": len(rejected),
                "fallback_file_count": len(scene_records_raw) - len(accepted) - len(rejected),
                "accepted_paths": [str(record["path"]) for record in accepted],
                "rejected_paths": [str(record["path"]) for record in rejected],
                "fallback_paths": [
                    str(record["path"])
                    for record in scene_records_raw
                    if record.get("status") not in {"accepted", "rejected"}
                ],
                "rejected_records": [
                    compact_collision_gate_blocker(record)
                    for record in rejected
                ],
                "fallback_file_records": [
                    compact_collision_gate_blocker(record)
                    for record in scene_records_raw
                    if record.get("status") not in {"accepted", "rejected"}
                ],
                "activation_candidate_path": (
                    str(activation_candidate["path"])
                    if activation_candidate is not None
                    else None
                ),
                "activation_candidate_reference_resource_path": (
                    str(activation_candidate["reference_resource_path"])
                    if activation_candidate is not None
                    else None
                ),
                "activation_candidate_reference_resource_path_source": (
                    str(activation_candidate["reference_resource_path_source"])
                    if activation_candidate is not None
                    else None
                ),
            }
        )
    return {
        "scene_status_counts": sorted_counter(status_counts),
        "accepted_scene_count": status_counts["accepted"],
        "blocked_scene_count": status_counts["blocked"],
        "fallback_scene_count": status_counts["fallback"],
        "scene_records": scene_records,
    }


def compact_collision_gate_blocker(record: dict[str, object]) -> dict[str, object]:
    compact = {
        "scene": record.get("scene"),
        "path": record.get("path"),
        "status": record.get("status"),
        "reference_resource_path": record.get("reference_resource_path"),
        "reference_resource_path_source": record.get("reference_resource_path_source"),
        "failed_checks": record.get("failed_checks", []),
        "error": record.get("error"),
        "collision_candidate_count": record.get("collision_candidate_count"),
        "missing_reference_semantics": record.get("missing_reference_semantics", []),
        "surface_semantics_present": record.get("surface_semantics_present"),
        "reference_floor_hit_rate": record.get("reference_floor_hit_rate"),
        "reference_floor_match_rate": record.get("reference_floor_match_rate"),
        "reference_max_bounds_delta": record.get("reference_max_bounds_delta"),
        "source_exit_list_accepted": record.get("source_exit_list_accepted"),
        "reference_exit_index_usage": record.get("reference_exit_index_usage", {}),
        "converted_exit_index_usage": record.get("converted_exit_index_usage", {}),
        "visual_diagnostic_policy": record.get("visual_diagnostic_policy"),
    }
    native_acceptance = record.get("native_zsi_acceptance")
    if isinstance(native_acceptance, dict):
        compact["native_zsi_acceptance"] = {
            "accepted": native_acceptance.get("accepted"),
            "failed_checks": native_acceptance.get("failed_checks", []),
            "source_spawn_floor_probe_policy": native_acceptance.get(
                "source_spawn_floor_probe_policy",
                {},
            ),
        }
    return {
        key: value
        for key, value in compact.items()
        if value is not None
    }


def audit_zsi_collision_gate_record(
    scene_zsi_path: Path,
    scene_root: Path,
    reference_o2r: Path,
    temp_root: Path,
    *,
    resource_format: str,
    reference_floor_probe_count: int,
    reference_floor_query_above: float,
    reference_floor_tolerance: float,
    visual_room_dir: Path | None,
    visual_floor_probe_count: int,
) -> dict[str, object]:
    scene = zsi_scene_stem(scene_zsi_path)
    record: dict[str, object] = {
        "scene": scene,
        "path": scene_zsi_path.relative_to(scene_root).as_posix(),
    }
    try:
        candidate_count = len(ZsiFile.from_path(scene_zsi_path).collision_header_candidates())
    except Exception as exc:
        return {
            **record,
            "status": "zsi_parse_error",
            "error": str(exc),
        }
    record["collision_candidate_count"] = candidate_count
    if candidate_count <= 0:
        return {
            **record,
            "status": "no_collision_candidate",
        }

    try:
        resource_path, resource_path_source = reference_collision_resource_path(reference_o2r, scene)
    except Exception as exc:
        return {
            **record,
            "status": "reference_resource_lookup_error",
            "error": str(exc),
        }
    if resource_path is None:
        return {
            **record,
            "status": "missing_reference_collision_resource",
            "reference_resource_path": None,
        }
    record["reference_resource_path"] = resource_path
    record["reference_resource_path_source"] = resource_path_source

    visual_models = None
    if visual_room_dir is not None:
        try:
            visual_models = load_scene_visual_room_models(visual_room_dir, scene)
            record["visual_room_model_count"] = len(visual_models)
            record["visual_room_indices"] = sorted({room_index for room_index, _model in visual_models})
        except Exception as exc:
            record["visual_mesh_floor_audit"] = {
                "status": "visual_model_load_error",
                "role": "diagnostic_only_not_collision_source",
                "error": str(exc),
            }
            record["visual_source_spawn_floor_audit"] = {
                "status": "visual_model_load_error",
                "role": "diagnostic_only_not_collision_source",
                "error": str(exc),
            }

    try:
        result = export_zsi_scene_collision(
            scene_zsi_path,
            temp_root / scene,
            resource_path,
            resource_format=resource_format,
            reference_o2r=reference_o2r,
            reference_floor_probe_count=reference_floor_probe_count,
            reference_floor_query_above=reference_floor_query_above,
            reference_floor_tolerance=reference_floor_tolerance,
            require_reference_match=False,
            visual_models=visual_models,
            visual_floor_probe_count=visual_floor_probe_count,
        )
    except Exception as exc:
        return {
            **record,
            "status": "export_or_compare_error",
            "error": str(exc),
        }

    stats = result.stats
    reference_comparison = dict(stats.get("reference_comparison", {}))
    native_acceptance = dict(stats.get("native_zsi_acceptance", {}))
    visual_mesh_floor_audit = compact_visual_mesh_floor_audit(
        dict(stats.get("visual_mesh_floor_audit", {}))
    )
    if visual_mesh_floor_audit:
        record["visual_mesh_floor_audit"] = visual_mesh_floor_audit
    visual_source_spawn_floor_audit = compact_visual_mesh_source_spawn_floor_audit(
        dict(stats.get("visual_source_spawn_floor_audit", {}))
    )
    if visual_source_spawn_floor_audit:
        record["visual_source_spawn_floor_audit"] = visual_source_spawn_floor_audit
    visual_diagnostic_policy = stats.get("visual_diagnostic_policy")
    if isinstance(visual_diagnostic_policy, dict):
        record["visual_diagnostic_policy"] = compact_visual_diagnostic_policy(
            visual_diagnostic_policy
        )
    reference_failed_checks = list(reference_comparison.get("failed_checks", []))
    native_failed_checks = [
        f"native_zsi.{name}"
        for name in native_acceptance.get("failed_checks", [])
    ]
    failed_checks = reference_failed_checks + native_failed_checks
    accepted = bool(reference_comparison.get("accepted")) and bool(
        native_acceptance.get("accepted")
    )
    return {
        **record,
        "status": "accepted" if accepted else "rejected",
        "accepted": accepted,
        "failed_checks": failed_checks,
        "vertex_count": stats["vertex_count"],
        "polygon_count": stats["polygon_count"],
        "surface_type_count": stats["surface_type_count"],
        "water_box_count": stats["water_box_count"],
        "camera_data_count": stats["exported_camera_record_count"],
        "source_spawn_floor_probe_count": stats["source_spawn_floor_probe_count"],
        "source_exit_list_command_count": stats["source_exit_list_audit"]["exit_list_command_count"],
        "reference_floor_hit_rate": reference_comparison.get("floor_hit_rate"),
        "reference_floor_match_rate": reference_comparison.get("floor_match_rate"),
        "reference_max_bounds_delta": reference_comparison.get("max_bounds_delta"),
        "geometry_bounds_delta": compact_bounds_delta(
            dict(reference_comparison.get("geometry_bounds_delta", {}))
        ),
        "resource_bounds_delta": compact_bounds_delta(
            dict(reference_comparison.get("resource_bounds_delta", {}))
        ),
        "missing_reference_semantics": reference_comparison.get(
            "missing_reference_semantics",
            [],
        ),
        "surface_semantics_present": reference_comparison.get("surface_semantics_present"),
        "polygon_category_ratio_accepted": reference_comparison.get(
            "polygon_category_ratio_summary",
            {},
        ).get("accepted"),
        "polygon_category_ratio_records": reference_comparison.get(
            "polygon_category_ratio_summary",
            {},
        ).get("records", []),
        "polygon_flag_coverage_accepted": reference_comparison.get(
            "polygon_flag_coverage_summary",
            {},
        ).get("accepted"),
        "polygon_flag_coverage_records": reference_comparison.get(
            "polygon_flag_coverage_summary",
            {},
        ).get("records", []),
        "reference_exit_index_usage": reference_comparison.get("reference_exit_index_usage", {}),
        "converted_exit_index_usage": reference_comparison.get("converted_exit_index_usage", {}),
        "surface_exit_index_usage_summary": reference_comparison.get(
            "surface_exit_index_usage_summary",
            {},
        ),
        "source_spawn_floor_probe_summary": compact_source_spawn_floor_probe_summary(
            dict(reference_comparison.get("source_spawn_floor_probes", {}))
        ),
        "source_exit_list_accepted": stats["source_exit_list_audit"]["accepted"],
        "native_zsi_acceptance": {
            "accepted": native_acceptance.get("accepted"),
            "failed_checks": native_acceptance.get("failed_checks", []),
            "checks": native_acceptance.get("checks", {}),
            "source_spawn_floor_probe_policy": native_acceptance.get(
                "source_spawn_floor_probe_policy",
                {},
            ),
        },
        "reference_summary": reference_comparison.get("reference"),
        "converted_summary": reference_comparison.get("converted"),
    }


def reference_collision_resource_path(
    reference_o2r: Path,
    scene: str,
) -> tuple[str | None, str]:
    discovered = discover_scene_collision_path_from_o2r(reference_o2r, scene)
    if discovered is not None:
        return discovered, "reference_o2r_discovery"
    return None, "not_found"


def load_scene_visual_room_models(scene_root: Path, scene: str) -> list[tuple[int, object]]:
    rooms: list[tuple[int, Path]] = []
    for path in sorted(scene_root.glob(f"{scene}_*_info.zsi")):
        if zsi_scene_role(path) != "room" or zsi_scene_stem(path) != scene:
            continue
        match = ROOM_ZSI_RE.match(path.name)
        if match is None:
            continue
        rooms.append((int(match.group(2)), path))
    if not rooms:
        raise ParseError(f"{scene_root}: no room ZSI files found for scene {scene!r}")

    models: list[tuple[int, object]] = []
    for room_index, path in rooms:
        for embedded in ZsiFile.from_path(path).embedded_cmbs():
            models.append((room_index, embedded.model))
    if not models:
        raise ParseError(f"{scene_root}: no embedded room CMBs found for scene {scene!r}")
    return models


def compact_visual_mesh_floor_audit(audit: dict[str, object]) -> dict[str, object]:
    if not audit:
        return {}
    compact = {
        "format": audit.get("format"),
        "status": audit.get("status"),
        "role": audit.get("role"),
        "diagnosis": audit.get("diagnosis"),
        "room_indices": audit.get("room_indices", []),
        "room_model_count": audit.get("room_model_count"),
        "probe_count": audit.get("probe_count"),
        "floor_probe_count_limit": audit.get("floor_probe_count_limit"),
        "floor_query_above": audit.get("floor_query_above"),
        "floor_tolerance": audit.get("floor_tolerance"),
        "min_floor_match_rate": audit.get("min_floor_match_rate"),
        "match_rate_margin": audit.get("match_rate_margin"),
        "error": audit.get("error"),
    }
    for section_name in ("native_zsi", "n64_reference"):
        section = audit.get(section_name)
        if isinstance(section, dict):
            compact[section_name] = compact_visual_floor_metrics(section)
    return {
        key: value
        for key, value in compact.items()
        if value is not None
    }


def compact_visual_mesh_source_spawn_floor_audit(audit: dict[str, object]) -> dict[str, object]:
    if not audit:
        return {}
    compact = {
        "format": audit.get("format"),
        "status": audit.get("status"),
        "role": audit.get("role"),
        "room_indices": audit.get("room_indices", []),
        "room_model_count": audit.get("room_model_count"),
        "probe_count": audit.get("probe_count"),
        "floor_query_above": audit.get("floor_query_above"),
        "floor_tolerance": audit.get("floor_tolerance"),
        "error": audit.get("error"),
    }
    visual_metrics = audit.get("visual_mesh")
    if isinstance(visual_metrics, dict):
        compact["visual_mesh"] = compact_visual_floor_metrics(visual_metrics)
    return {
        key: value
        for key, value in compact.items()
        if value is not None
    }


def compact_visual_diagnostic_policy(policy: dict[str, object]) -> dict[str, object]:
    return {
        key: value
        for key, value in {
            "format": policy.get("format"),
            "collision_source": policy.get("collision_source"),
            "visual_mesh_is_collision_source": policy.get("visual_mesh_is_collision_source"),
            "n64_reference_role": policy.get("n64_reference_role"),
            "visual_mesh_role": policy.get("visual_mesh_role"),
            "reference_comparison_accepted": policy.get("reference_comparison_accepted"),
            "native_zsi_acceptance_accepted": policy.get("native_zsi_acceptance_accepted"),
            "visual_floor_audit_status": policy.get("visual_floor_audit_status"),
            "visual_floor_diagnosis": policy.get("visual_floor_diagnosis"),
            "visual_source_spawn_audit_status": policy.get("visual_source_spawn_audit_status"),
            "relaxed_checks_from_visual_diagnostics": policy.get(
                "relaxed_checks_from_visual_diagnostics",
                [],
            ),
            "reference_failed_checks_still_blocking": policy.get(
                "reference_failed_checks_still_blocking",
                [],
            ),
        }.items()
        if value is not None
    }


def compact_visual_floor_metrics(section: dict[str, object]) -> dict[str, object]:
    compact = {
        key: section.get(key)
        for key in (
            "hit_count",
            "match_count",
            "hit_rate",
            "match_rate",
            "failure_count",
            "max_abs_delta",
            "average_abs_delta",
            "failures_sample",
        )
        if key in section
    }
    bounds_delta = section.get("bounds_delta_from_visual")
    if isinstance(bounds_delta, dict):
        absolute = bounds_delta.get("absolute")
        if isinstance(absolute, (list, tuple)) and absolute:
            compact["max_bounds_delta_from_visual"] = max(float(value) for value in absolute)
    return compact


def compact_source_spawn_floor_probe_summary(summary: dict[str, object]) -> dict[str, object]:
    if not summary:
        return {}
    return {
        key: summary.get(key)
        for key in (
            "accepted",
            "probe_count",
            "required_probe_count",
            "query_above",
            "floor_tolerance",
            "converted_hit_count",
            "converted_match_count",
            "converted_hit_rate",
            "converted_match_rate",
            "converted_total_match_rate",
            "converted_failure_count",
            "converted_failures_sample",
            "unsupported_source_probe_count",
            "unsupported_source_probe_sample",
            "reference_hit_rate",
            "reference_match_rate",
        )
        if key in summary
    }


def compact_bounds_delta(bounds_delta: dict[str, object]) -> dict[str, object]:
    if not bounds_delta:
        return {}
    compact = {
        "reference_min": bounds_delta.get("reference_min"),
        "reference_max": bounds_delta.get("reference_max"),
        "converted_min": bounds_delta.get("converted_min"),
        "converted_max": bounds_delta.get("converted_max"),
        "delta_min": bounds_delta.get("delta_min"),
        "delta_max": bounds_delta.get("delta_max"),
        "absolute": bounds_delta.get("absolute"),
    }
    absolute = compact.get("absolute")
    if isinstance(absolute, (list, tuple)) and absolute:
        compact["max_abs_delta"] = max(float(value) for value in absolute)
    return {
        key: value
        for key, value in compact.items()
        if value is not None
    }
