from __future__ import annotations

from collections import Counter
import json
from pathlib import Path

from .binary import ParseError
from .romfs_inventory import sorted_counter


CHARACTER_CONVERSION_MANIFEST_FORMAT = "oot3d_character_conversion_manifest_v1"
SKINNED_BIND_POSE_NATIVE_MANIFEST_FORMAT = "oot3d_skinned_bind_pose_native_export_v1"


def export_character_conversion_manifest(
    skinned_binding_manifest_path: Path,
    animation_like_audit_path: Path,
    cmab_audit_path: Path,
    output_path: Path | None = None,
    *,
    profile_id: str,
    model_archive: str,
    model_cmb: str,
    auxiliary_archives: list[str] | None = None,
    pose_batch_manifest_path: Path | None = None,
    native_bind_pose_manifest_path: Path | None = None,
    n64_reference_audit_path: Path | None = None,
    reference_label: str = "oot_n64_time_normalized_reference",
    runtime_skinning_diagnostic_csab_names: list[str] | None = None,
    sample_limit: int = 25,
) -> dict[str, object]:
    """Compose a character-level conversion gate from existing offline manifests."""

    for path, label in (
        (skinned_binding_manifest_path, "skinned animation binding manifest"),
        (animation_like_audit_path, "ANB/FACEB audit"),
        (cmab_audit_path, "CMAB audit"),
    ):
        if not path.is_file():
            raise ParseError(f"{path}: {label} not found")

    skinned_binding = load_json(skinned_binding_manifest_path)
    animation_like = load_json(animation_like_audit_path)
    cmab_audit = load_json(cmab_audit_path)
    pose_batch = None
    if pose_batch_manifest_path is not None:
        if not pose_batch_manifest_path.is_file():
            raise ParseError(f"{pose_batch_manifest_path}: skinned animation pose batch manifest not found")
        pose_batch = load_json(pose_batch_manifest_path)
    native_bind_pose = None
    if native_bind_pose_manifest_path is not None:
        if not native_bind_pose_manifest_path.is_file():
            raise ParseError(f"{native_bind_pose_manifest_path}: native skinned bind-pose manifest not found")
        native_bind_pose = load_json(native_bind_pose_manifest_path)
    n64_reference_audit = None
    if n64_reference_audit_path is not None:
        if not n64_reference_audit_path.is_file():
            raise ParseError(f"{n64_reference_audit_path}: N64 animation reference audit not found")
        n64_reference_audit = load_json(n64_reference_audit_path)

    archive_scope = normalize_archive_scope(model_archive, auxiliary_archives or [])
    target = find_target(skinned_binding, model_archive, model_cmb)
    animations = list(target.get("animations", [])) if target else []
    animation_summary = csab_animation_summary(animations, sample_limit)
    animation_like_summary = auxiliary_animation_like_summary(
        animation_like,
        archive_scope,
        model_archive,
        animation_summary["csab_stems"],
        sample_limit,
    )
    material_summary = cmab_material_animation_summary(
        cmab_audit,
        archive_scope,
        sample_limit,
    )
    pose_volume_summary = pose_volume_validation_summary(
        pose_batch,
        pose_batch_manifest_path,
        model_archive,
        model_cmb,
        sample_limit,
    )
    native_bind_pose_summary = native_bind_pose_conversion_summary(
        native_bind_pose,
        native_bind_pose_manifest_path,
        model_archive,
        model_cmb,
        sample_limit,
    )
    target_summary = target_conversion_summary(target, native_bind_pose_summary)
    n64_reference_summary = n64_reference_validation_summary(n64_reference_audit, n64_reference_audit_path)
    issues = character_conversion_issues(
        target_summary,
        animation_summary,
        animation_like_summary,
        material_summary,
        pose_volume_summary,
        native_bind_pose_summary,
        n64_reference_summary,
    )
    issue_counts = issue_counts_by_occurrence(issues)

    manifest: dict[str, object] = {
        "format": CHARACTER_CONVERSION_MANIFEST_FORMAT,
        "profile_id": profile_id,
        "source_manifests": {
            "skinned_animation_binding": str(skinned_binding_manifest_path),
            "animation_like_audit": str(animation_like_audit_path),
            "cmab_audit": str(cmab_audit_path),
            "skinned_animation_pose_batch": (
                str(pose_batch_manifest_path) if pose_batch_manifest_path is not None else None
            ),
            "skinned_bind_pose_native": (
                str(native_bind_pose_manifest_path) if native_bind_pose_manifest_path is not None else None
            ),
            "n64_animation_reference_audit": (
                str(n64_reference_audit_path) if n64_reference_audit_path is not None else None
            ),
        },
        "source_archives": {
            "model_archive": normalize_path(model_archive),
            "model_cmb": normalize_path(model_cmb),
            "auxiliary_archives": archive_scope[1:],
        },
        "conversion_strategy": {
            "source_of_truth": "original_oot3d_character_containers",
            "fork_resource_strategy": "offline_decode_to_fork_native_ir",
            "runtime_policy": "minimal_skeleton_skinning_animation_material_players",
            "raw_oot3d_runtime_support": "not_enabled_by_default",
            "case_specific_fixes": "disallowed",
        },
        "validation_policy": {
            "reference_label": reference_label,
            "framerate_policy": "compare time-normalized poses and events, not raw frame numbers",
            "required_checks": [
                "skeleton pose correspondence",
                "animation timing correspondence after framerate normalization",
                "skinned deformation correspondence",
                "bone and model orientation correspondence",
                "similar geometry volume occupancy",
                "material and face-state feature binding coverage",
            ],
            "pilot_scope": "Link bambino is the first reference profile; fixes must remain data-driven and reusable.",
        },
        "target": target_summary,
        "csab_animation_tracks": without_internal_fields(animation_summary),
        "pose_volume_validation": pose_volume_summary,
        "n64_reference_validation": n64_reference_summary,
        "auxiliary_animation_payloads": animation_like_summary,
        "material_animation_payloads": material_summary,
        "runtime_readiness": runtime_readiness(
            target_summary,
            animation_summary,
            animation_like_summary,
            material_summary,
            pose_volume_summary,
            native_bind_pose_summary,
            n64_reference_summary,
        ),
        "runtime_diagnostics": {
            "skinning_diagnostic_csab_names": [
                normalize_path(csab_name)
                for csab_name in (runtime_skinning_diagnostic_csab_names or [])
                if normalize_path(csab_name)
            ],
            "skinning_diagnostic_selection": (
                "explicit_profile_manifest"
                if runtime_skinning_diagnostic_csab_names
                else "first_valid_pose_sample_for_target"
            ),
        },
        "issue_counts": sorted_counter(issue_counts),
        "issues": issues[:sample_limit],
        "fallback_policy": [
            "This manifest is a promotion gate, not runtime playback.",
            "N64 actor behavior remains authoritative until a specific OOT3D delta is proven.",
            "The fork should consume normalized IR/resources, not raw OOT3D archives, unless a narrow exception is justified.",
        ],
    }

    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")
    return manifest


def load_json(path: Path) -> dict[str, object]:
    return json.loads(path.read_text(encoding="utf-8"))


def normalize_archive_scope(model_archive: str, auxiliary_archives: list[str]) -> list[str]:
    scope: list[str] = []
    for archive in [model_archive, *auxiliary_archives]:
        normalized = normalize_path(archive)
        if normalized and normalized not in scope:
            scope.append(normalized)
    return scope


def normalize_path(value: object) -> str:
    return str(value).replace("\\", "/").strip("/")


def find_target(
    skinned_binding: dict[str, object],
    model_archive: str,
    model_cmb: str,
) -> dict[str, object] | None:
    archive = normalize_path(model_archive)
    cmb = normalize_path(model_cmb)
    for target in skinned_binding.get("targets", []):
        if not isinstance(target, dict):
            continue
        if normalize_path(target.get("archive_path")) == archive and normalize_path(target.get("target_cmb_name")) == cmb:
            return target
    return None


def target_conversion_summary(
    target: dict[str, object] | None,
    native_bind_pose_summary: dict[str, object],
) -> dict[str, object]:
    if target is None:
        return {
            "status": "missing",
            "target_id": None,
            "archive_path": None,
            "target_cmb_name": None,
            "model_name": None,
            "bone_count": 0,
            "bind_pose": None,
            "native_bind_pose": native_bind_pose_summary,
            "counts": {},
        }

    bind_pose = target.get("bind_pose", {})
    counts = bind_pose.get("counts", {}) if isinstance(bind_pose, dict) else {}
    bind_export = bind_pose.get("export") if isinstance(bind_pose, dict) else None
    return {
        "status": "resolved",
        "target_id": target.get("target_id"),
        "archive_path": normalize_path(target.get("archive_path")),
        "target_cmb_name": normalize_path(target.get("target_cmb_name")),
        "model_name": target.get("model_name"),
        "bone_count": target.get("bone_count"),
        "support_status_counts": target.get("support_status_counts", {}),
        "target_resolution_status_counts": target.get("target_resolution_status_counts", {}),
        "bind_pose": {
            "export": bind_export,
            "export_file_exists": bool(bind_export and Path(str(bind_export)).is_file()),
            "package_entry": bind_pose.get("package_entry") if isinstance(bind_pose, dict) else None,
        },
        "native_bind_pose": native_bind_pose_summary,
        "counts": counts,
    }


def native_bind_pose_conversion_summary(
    native_bind_pose: dict[str, object] | None,
    native_bind_pose_manifest_path: Path | None,
    model_archive: str,
    model_cmb: str,
    sample_limit: int,
) -> dict[str, object]:
    if native_bind_pose is None:
        return {
            "status": "not_provided",
            "manifest": None,
            "top_display_list": None,
            "resource_count": 0,
            "missing_source_file_count": 0,
            "issue_count": 0,
        }

    resources = native_bind_pose.get("resources", [])
    resources = resources if isinstance(resources, list) else []
    counts = native_bind_pose.get("counts", {})
    counts = counts if isinstance(counts, dict) else {}
    issues = native_bind_pose.get("issues", [])
    issues = issues if isinstance(issues, list) else []
    resource_kind_counts: Counter[str] = Counter()
    missing_source_files: list[dict[str, object]] = []
    for resource in resources:
        if not isinstance(resource, dict):
            continue
        resource_kind_counts[str(resource.get("kind", "<missing>"))] += 1
        source_file = resource.get("file")
        if not source_file or not Path(str(source_file)).is_file():
            missing_source_files.append(
                {
                    "kind": resource.get("kind"),
                    "path": resource.get("path"),
                    "file": source_file,
                }
            )

    source = normalize_path(native_bind_pose.get("source"))
    embedded_name = normalize_path(native_bind_pose.get("embedded_name"))
    source_matches_target = normalize_path(model_archive) in source and embedded_name == normalize_path(model_cmb)
    issue_count = int(counts.get("issue_count", len(issues)) or 0)
    top_display_list = normalize_path(native_bind_pose.get("top_display_list"))
    diagnostic_display_list = normalize_path(native_bind_pose.get("diagnostic_display_list"))
    display_profiles = native_bind_pose.get("display_profiles", [])
    if not isinstance(display_profiles, list):
        display_profiles = []
    visibility_groups = native_bind_pose.get("visibility_groups", [])
    if not isinstance(visibility_groups, list):
        visibility_groups = []
    mesh_records = native_bind_pose.get("mesh_records", [])
    if not isinstance(mesh_records, list):
        mesh_records = []
    selected_material_indices = native_bind_pose.get("selected_material_indices", [])
    if not isinstance(selected_material_indices, list):
        selected_material_indices = []
    selected_material_display_lists = native_bind_pose.get("selected_material_display_lists", [])
    if not isinstance(selected_material_display_lists, list):
        selected_material_display_lists = []
    status = "ready"
    if native_bind_pose.get("format") != SKINNED_BIND_POSE_NATIVE_MANIFEST_FORMAT:
        status = "invalid_format"
    elif not top_display_list:
        status = "missing_top_display_list"
    elif not resources:
        status = "missing_resources"
    elif missing_source_files:
        status = "missing_source_files"
    elif issue_count != 0:
        status = "export_has_issues"
    elif not source_matches_target:
        status = "target_mismatch"
    elif not selected_material_indices:
        status = "missing_selected_material_indices"
    elif len(selected_material_display_lists) != len(selected_material_indices):
        status = "selected_material_display_list_count_mismatch"

    return {
        "status": status,
        "manifest": str(native_bind_pose_manifest_path) if native_bind_pose_manifest_path is not None else None,
        "format": native_bind_pose.get("format"),
        "source": native_bind_pose.get("source"),
        "embedded_name": native_bind_pose.get("embedded_name"),
        "source_matches_target": source_matches_target,
        "resource_root": native_bind_pose.get("resource_root"),
        "symbol": native_bind_pose.get("symbol"),
        "selection_source": native_bind_pose.get("selection_source"),
        "selection_profile_id": native_bind_pose.get("selection_profile_id"),
        "selection_model_name": native_bind_pose.get("selection_model_name"),
        "selection_primitive_key_count": len(native_bind_pose.get("selection_primitive_keys", []))
        if isinstance(native_bind_pose.get("selection_primitive_keys"), list)
        else 0,
        "top_display_list": top_display_list,
        "diagnostic_display_profile": native_bind_pose.get("diagnostic_display_profile"),
        "diagnostic_display_list": diagnostic_display_list,
        "export_profile": native_bind_pose.get("export_profile"),
        "display_profile_count": len(display_profiles),
        "display_profiles": display_profiles,
        "visibility_group_count": len(visibility_groups),
        "visibility_groups": visibility_groups,
        "mesh_record_count": len(mesh_records),
        "mesh_records": mesh_records,
        "selected_material_index_count": len(selected_material_indices),
        "selected_material_indices": selected_material_indices,
        "selected_material_display_list_count": len(selected_material_display_lists),
        "selected_material_display_lists": selected_material_display_lists,
        "position_source": native_bind_pose.get("position_source"),
        "normal_source": native_bind_pose.get("normal_source"),
        "diagnostic_preview_position_source": native_bind_pose.get("diagnostic_preview_position_source"),
        "static_bind_pose_contract": native_bind_pose.get("static_bind_pose_contract"),
        "texture_orientation": native_bind_pose.get("texture_orientation"),
        "baked_texture_orientation": native_bind_pose.get("baked_texture_orientation"),
        "uv_orientation": native_bind_pose.get("uv_orientation"),
        "position_bounds": native_bind_pose.get("position_bounds", {}),
        "resource_count": len(resources),
        "resource_kind_counts": sorted_counter(resource_kind_counts),
        "counts": counts,
        "issue_count": issue_count,
        "missing_source_file_count": len(missing_source_files),
        "sample_missing_source_files": missing_source_files[:sample_limit],
        "sample_issues": issues[:sample_limit],
    }


def csab_animation_summary(animations: list[object], sample_limit: int) -> dict[str, object]:
    frame_slots: list[int] = []
    aggregate_counts: Counter[str] = Counter()
    encoding_counts: Counter[str] = Counter()
    validation_counts: Counter[str] = Counter()
    missing_exports: list[dict[str, object]] = []
    samples: list[dict[str, object]] = []
    stems: set[str] = set()

    for animation in animations:
        if not isinstance(animation, dict):
            continue
        csab_name = normalize_path(animation.get("csab_name"))
        stems.add(path_stem(csab_name))
        frame_slot_count = int(animation.get("frame_slot_count") or 0)
        if frame_slot_count:
            frame_slots.append(frame_slot_count)
        counts = animation.get("counts", {})
        if isinstance(counts, dict):
            for key, value in counts.items():
                if key == "encoding_counts" and isinstance(value, dict):
                    for encoding, encoding_count in value.items():
                        if isinstance(encoding_count, int) and not isinstance(encoding_count, bool):
                            encoding_counts[str(encoding)] += encoding_count
                elif isinstance(value, int) and not isinstance(value, bool):
                    aggregate_counts[str(key)] += value
        validation = animation.get("validation", {})
        validation_status = "missing"
        if isinstance(validation, dict):
            validation_status = "valid" if validation.get("valid") is True else str(validation.get("status", "invalid"))
        validation_counts[validation_status] += 1

        export_file = str(animation.get("track_export_file", ""))
        if export_file and not Path(export_file).is_file():
            missing_exports.append(sample_animation(animation))
        if len(samples) < sample_limit:
            samples.append(sample_animation(animation))

    return {
        "count": len([animation for animation in animations if isinstance(animation, dict)]),
        "frame_slot_min": min(frame_slots, default=0),
        "frame_slot_max": max(frame_slots, default=0),
        "aggregate_counts": dict(sorted(aggregate_counts.items())),
        "encoding_counts": dict(sorted(encoding_counts.items())),
        "validation_status_counts": dict(sorted(validation_counts.items())),
        "missing_export_count": len(missing_exports),
        "sample_missing_exports": missing_exports[:sample_limit],
        "sample_tracks": samples,
        "csab_stems": stems,
    }


def sample_animation(animation: dict[str, object]) -> dict[str, object]:
    return {
        "csab_name": normalize_path(animation.get("csab_name")),
        "track_package_entry": normalize_path(animation.get("track_package_entry")),
        "frame_slot_count": animation.get("frame_slot_count"),
        "target_resolution_status": animation.get("target_resolution_status"),
        "target_support_status": animation.get("target_support_status"),
    }


def auxiliary_animation_like_summary(
    animation_like: dict[str, object],
    archive_scope: list[str],
    model_archive: str,
    csab_stems: object,
    sample_limit: int,
) -> dict[str, object]:
    stem_set = set(csab_stems) if isinstance(csab_stems, set) else set()
    records = scoped_records(animation_like, archive_scope)
    type_counts: Counter[str] = Counter()
    archive_type_counts: Counter[str] = Counter()
    stem_sets: dict[str, set[str]] = {}
    samples: list[dict[str, object]] = []
    faceb_without_csab: list[str] = []
    faceb_tracks: list[dict[str, object]] = []
    faceb_value0_values: set[int] = set()
    faceb_value1_values: set[int] = set()
    faceb_entry_total = 0

    for record in records:
        file_type = normalize_path(record.get("type")).lower()
        type_counts[file_type] += 1
        archive_type_counts[f"{normalize_path(record.get('archive_path'))}:{file_type}"] += 1
        stem = path_stem(record.get("embedded_name"))
        stem_sets.setdefault(file_type, set()).add(stem)
        if file_type == "faceb" and normalize_path(record.get("archive_path")) == normalize_path(model_archive):
            metadata = record.get("metadata", {})
            entries = metadata.get("entries", []) if isinstance(metadata, dict) else []
            entries = [entry for entry in entries if isinstance(entry, dict)]
            faceb_entry_total += len(entries)
            for entry in entries:
                value0 = entry.get("value0")
                value1 = entry.get("value1")
                if isinstance(value0, int) and not isinstance(value0, bool):
                    faceb_value0_values.add(value0)
                if isinstance(value1, int) and not isinstance(value1, bool):
                    faceb_value1_values.add(value1)
            if stem not in stem_set:
                faceb_without_csab.append(stem)
            else:
                faceb_tracks.append(faceb_track_record(record, stem, entries))
        if len(samples) < sample_limit:
            samples.append(auxiliary_payload_sample(record))

    stem_overlap = {
        "csab_faceb_overlap_count": len(stem_set & stem_sets.get("faceb", set())),
        "faceb_without_csab_count": len(set(faceb_without_csab)),
        "sample_faceb_without_csab": sorted(set(faceb_without_csab))[:sample_limit],
        "csab_anb_overlap_count": len(stem_set & stem_sets.get("anb", set())),
        "anb_unique_stem_count": len(stem_sets.get("anb", set())),
        "faceb_unique_stem_count": len(stem_sets.get("faceb", set())),
    }

    return {
        "archive_scope": archive_scope,
        "payload_type_counts": sorted_counter(type_counts),
        "archive_type_counts": sorted_counter(archive_type_counts),
        "stem_overlap": stem_overlap,
        "faceb_track_count": len(faceb_tracks),
        "faceb_entry_total": faceb_entry_total,
        "faceb_value0_values": sorted(faceb_value0_values),
        "faceb_value1_values": sorted(faceb_value1_values),
        "faceb_hold_value": 255,
        "faceb_tracks": faceb_tracks,
        "sample_payloads": samples,
    }


def cmab_material_animation_summary(
    cmab_audit: dict[str, object],
    archive_scope: list[str],
    sample_limit: int,
) -> dict[str, object]:
    records = scoped_records(cmab_audit, archive_scope)
    resolution_counts: Counter[str] = Counter()
    support_counts: Counter[str] = Counter()
    unresolved: list[dict[str, object]] = []
    samples: list[dict[str, object]] = []
    bindings: list[dict[str, object]] = []
    texture_name_counts_by_role: Counter[str] = Counter()
    total_mmad = 0

    for record in records:
        resolution = str(record.get("target_resolution_status", "<missing>"))
        resolution_counts[resolution] += 1
        candidates = record.get("target_candidates", [])
        if isinstance(candidates, list) and len(candidates) == 1 and isinstance(candidates[0], dict):
            support_counts[str(candidates[0].get("support_status", "<missing>"))] += 1
        layout = record.get("layout", {})
        if isinstance(layout, dict):
            total_mmad += int(layout.get("mmad_count", 0) or 0)
        sample = cmab_payload_sample(record)
        if not resolution.startswith("single_"):
            unresolved.append(sample)
        elif isinstance(record.get("target_cmb"), dict):
            binding = cmab_material_binding_record(record)
            bindings.append(binding)
            texture_name_counts_by_role[str(binding.get("role", "material"))] += len(binding.get("texture_names", []))
        if len(samples) < sample_limit:
            samples.append(sample)

    return {
        "count": len(records),
        "total_mmad_count": total_mmad,
        "target_resolution_counts": sorted_counter(resolution_counts),
        "target_support_counts": sorted_counter(support_counts),
        "unresolved_or_ambiguous_count": len(unresolved),
        "binding_count": len(bindings),
        "texture_name_counts_by_role": sorted_counter(texture_name_counts_by_role),
        "bindings": bindings,
        "sample_unresolved_or_ambiguous": unresolved[:sample_limit],
        "sample_payloads": samples,
    }


def scoped_records(manifest: dict[str, object], archive_scope: list[str]) -> list[dict[str, object]]:
    scope = set(archive_scope)
    return [
        record
        for record in manifest.get("records", [])
        if isinstance(record, dict) and normalize_path(record.get("archive_path")) in scope
    ]


def auxiliary_payload_sample(record: dict[str, object]) -> dict[str, object]:
    metadata = record.get("metadata", {})
    sample = {
        "archive_path": normalize_path(record.get("archive_path")),
        "embedded_name": normalize_path(record.get("embedded_name")),
        "type": normalize_path(record.get("type")).lower(),
        "size": record.get("size"),
    }
    if isinstance(metadata, dict):
        for key in ("frame_count_candidate", "entry_count", "size_match_status", "magic_status"):
            if key in metadata:
                sample[key] = metadata[key]
    return sample


def faceb_track_record(
    record: dict[str, object],
    stem: str,
    entries: list[dict[str, object]],
) -> dict[str, object]:
    return {
        "archive_path": normalize_path(record.get("archive_path")),
        "faceb_name": normalize_path(record.get("embedded_name")),
        "stem": stem,
        "entry_count": len(entries),
        "events": [
            {
                "frame": int(entry.get("frame", 0) or 0),
                "eye_index": int(entry.get("value0", 255) or 0),
                "mouth_index": int(entry.get("value1", 255) or 0),
            }
            for entry in entries
        ],
    }


def cmab_payload_sample(record: dict[str, object]) -> dict[str, object]:
    layout = record.get("layout", {})
    return {
        "archive_path": normalize_path(record.get("archive_path")),
        "cmab_name": normalize_path(record.get("cmab_name")),
        "target_resolution_status": record.get("target_resolution_status"),
        "frame_count_candidate": record.get("frame_count_candidate"),
        "loop_mode_candidate": record.get("loop_mode_candidate"),
        "mmad_count": layout.get("mmad_count") if isinstance(layout, dict) else None,
    }


def cmab_material_binding_record(record: dict[str, object]) -> dict[str, object]:
    layout = record.get("layout", {})
    texture_names = (
        [normalize_path(name) for name in layout.get("string_table_names", [])]
        if isinstance(layout, dict) and isinstance(layout.get("string_table_names"), list)
        else []
    )
    target = record.get("target_cmb", {})
    target = target if isinstance(target, dict) else {}
    return {
        "archive_path": normalize_path(record.get("archive_path")),
        "cmab_name": normalize_path(record.get("cmab_name")),
        "role": cmab_texture_role(texture_names),
        "target_resolution_status": record.get("target_resolution_status"),
        "target_cmb": normalize_path(target.get("embedded_name")) if isinstance(target, dict) else "",
        "frame_count_candidate": record.get("frame_count_candidate"),
        "loop_mode_candidate": record.get("loop_mode_candidate"),
        "mmad_count": layout.get("mmad_count") if isinstance(layout, dict) else None,
        "texture_names": texture_names,
    }


def cmab_texture_role(texture_names: list[str]) -> str:
    lowered = [name.lower() for name in texture_names]
    if lowered and all(name.startswith("c_eye") for name in lowered):
        return "eye"
    if lowered and all(name.startswith("c_mouth") for name in lowered):
        return "mouth"
    return "material"


def pose_volume_validation_summary(
    pose_batch: dict[str, object] | None,
    pose_batch_manifest_path: Path | None,
    model_archive: str,
    model_cmb: str,
    sample_limit: int,
) -> dict[str, object]:
    if pose_batch is None or pose_batch_manifest_path is None:
        return {
            "status": "not_provided",
            "pose_export_count": 0,
            "validation_error_count": 0,
            "missing_pose_sample_file_count": 0,
            "all_sampled_pose_rows_finite": False,
            "aggregate_counts": {},
            "position_bounds": {},
            "position_extent": {},
            "aabb_volume": 0.0,
            "sample_pose_exports": [],
        }

    records = [
        record
        for record in pose_batch.get("records", [])
        if isinstance(record, dict)
        and record.get("status") == "exported"
        and normalize_path(record.get("archive_path")) == normalize_path(model_archive)
        and normalize_path(record.get("target_cmb_name")) == normalize_path(model_cmb)
    ]
    aggregate_counts: Counter[str] = Counter()
    validation_status_counts: Counter[str] = Counter()
    position_bounds: dict[str, list[float]] = {}
    missing_pose_samples: list[dict[str, object]] = []
    samples: list[dict[str, object]] = []
    frame_counts: list[int] = []
    max_position_delta = 0.0

    for record in records:
        merge_numeric_counts(aggregate_counts, record.get("counts", {}))
        frame_count = record.get("frame_count_candidate")
        if isinstance(frame_count, int) and not isinstance(frame_count, bool):
            frame_counts.append(frame_count)
        counts = record.get("counts", {})
        if isinstance(counts, dict):
            max_position_delta = max(
                max_position_delta,
                float(counts.get("max_position_delta_from_bind", 0.0) or 0.0),
            )

        pose_path = resolve_pose_sample_path(pose_batch_manifest_path, pose_batch, record)
        if not pose_path.is_file():
            missing_pose_samples.append(sample_pose_record(record, None))
            continue

        pose_sample = load_json(pose_path)
        validation = pose_sample.get("validation", {})
        validation_status = "missing"
        if isinstance(validation, dict):
            validation_status = "valid" if validation.get("valid") is True else str(validation.get("status", "invalid"))
        validation_status_counts[validation_status] += 1
        merge_position_bounds(position_bounds, pose_sample.get("position_bounds", {}))
        if len(samples) < sample_limit:
            samples.append(sample_pose_record(record, pose_sample))

    validation_error_count = int(aggregate_counts.get("validation_error_count", 0))
    invalid_sample_count = sum(
        int(count)
        for status, count in validation_status_counts.items()
        if str(status) != "valid"
    )
    sampled_vertex_rows = int(aggregate_counts.get("sampled_vertex_rows", 0))
    finite_pose_rows = int(aggregate_counts.get("finite_pose_rows", 0))
    position_extent = position_extent_from_bounds(position_bounds)
    return {
        "status": (
            "validated"
            if records and not missing_pose_samples and validation_error_count == 0 and invalid_sample_count == 0
            else "incomplete"
        ),
        "pose_batch_manifest": str(pose_batch_manifest_path),
        "pose_export_count": len(records),
        "missing_pose_sample_file_count": len(missing_pose_samples),
        "validation_status_counts": sorted_counter(validation_status_counts),
        "invalid_pose_sample_count": invalid_sample_count,
        "validation_error_count": validation_error_count,
        "all_sampled_pose_rows_finite": sampled_vertex_rows > 0 and finite_pose_rows == sampled_vertex_rows,
        "frame_count_min": min(frame_counts, default=0),
        "frame_count_max": max(frame_counts, default=0),
        "max_position_delta_from_bind": round_float(max_position_delta),
        "aggregate_counts": dict(sorted(aggregate_counts.items())),
        "position_bounds": rounded_bounds(position_bounds),
        "position_extent": position_extent,
        "aabb_volume": round_float(
            float(position_extent.get("x", 0.0))
            * float(position_extent.get("y", 0.0))
            * float(position_extent.get("z", 0.0))
        ),
        "sample_missing_pose_samples": missing_pose_samples[:sample_limit],
        "sample_pose_exports": samples,
        "validation_note": (
            "Offline OOT3D pose deformation samples are finite and volume-bounded; "
            "N64 visual correspondence still requires a separate time-normalized reference comparison."
        ),
    }


def resolve_pose_sample_path(
    manifest_path: Path,
    manifest: dict[str, object],
    record: dict[str, object],
) -> Path:
    pose_export = Path(str(record.get("pose_sample_export", "")))
    if pose_export.is_absolute() or pose_export.exists():
        return pose_export
    output = Path(str(manifest.get("output", manifest_path.parent)))
    return output / pose_export


def sample_pose_record(record: dict[str, object], pose_sample: dict[str, object] | None) -> dict[str, object]:
    sample: dict[str, object] = {
        "csab_name": normalize_path(record.get("csab_name")),
        "pose_sample_export": normalize_path(record.get("pose_sample_export")),
        "frame_count_candidate": record.get("frame_count_candidate"),
        "sample_frames": record.get("sample_frames", []),
    }
    counts = record.get("counts", {})
    if isinstance(counts, dict):
        sample["validation_error_count"] = counts.get("validation_error_count", 0)
        sample["max_position_delta_from_bind"] = counts.get("max_position_delta_from_bind", 0)
    if pose_sample is not None:
        sample["position_bounds"] = pose_sample.get("position_bounds", {})
    return sample


def merge_numeric_counts(target: Counter[str], source: object) -> None:
    if not isinstance(source, dict):
        return
    for key, value in source.items():
        if isinstance(value, int) and not isinstance(value, bool):
            target[str(key)] += value


def merge_position_bounds(target: dict[str, list[float]], source: object) -> None:
    if not isinstance(source, dict):
        return
    for axis in ("x", "y", "z"):
        axis_bounds = source.get(axis)
        if not isinstance(axis_bounds, dict):
            continue
        min_value = axis_bounds.get("min")
        max_value = axis_bounds.get("max")
        if not isinstance(min_value, (int, float)) or not isinstance(max_value, (int, float)):
            continue
        current = target.get(axis)
        if current is None:
            target[axis] = [float(min_value), float(max_value)]
        else:
            current[0] = min(current[0], float(min_value))
            current[1] = max(current[1], float(max_value))


def rounded_bounds(bounds: dict[str, list[float]]) -> dict[str, dict[str, float]]:
    return {
        axis: {"min": round_float(values[0]), "max": round_float(values[1])}
        for axis, values in sorted(bounds.items())
    }


def position_extent_from_bounds(bounds: dict[str, list[float]]) -> dict[str, float]:
    return {
        axis: round_float(values[1] - values[0])
        for axis, values in sorted(bounds.items())
    }


def round_float(value: float) -> float:
    return round(float(value), 6)


def n64_reference_validation_summary(
    n64_reference_audit: dict[str, object] | None,
    n64_reference_audit_path: Path | None,
) -> dict[str, object]:
    if n64_reference_audit is None or n64_reference_audit_path is None:
        return {
            "status": "not_provided",
            "n64_reference_audit": None,
            "blocker_count": 0,
            "matched_oot3d_count": 0,
            "unmatched_oot3d_count": 0,
            "ambiguous_oot3d_count": 0,
            "numeric_pose_comparison_ready": False,
            "numeric_pose_comparison_status": "not_started",
        }

    blocker_counts = n64_reference_audit.get("blocker_counts", {})
    blocker_count = int(blocker_counts.get("total", 0) or 0) if isinstance(blocker_counts, dict) else 0
    mapping = n64_reference_audit.get("candidate_mapping", {})
    reference = n64_reference_audit.get("n64_reference", {})
    skeleton = reference.get("skeleton", {}) if isinstance(reference, dict) else {}
    data_summary = reference.get("player_animation_data_summary", {}) if isinstance(reference, dict) else {}
    payload_decode = reference.get("player_animation_payload_decode", {}) if isinstance(reference, dict) else {}
    payload_frame_ir = payload_decode.get("frame_ir", {}) if isinstance(payload_decode, dict) else {}
    skeleton_pose_reference = reference.get("skeleton_pose_reference", {}) if isinstance(reference, dict) else {}
    time_samples = n64_reference_audit.get("time_normalized_pose_samples", {})
    limb_mapping = n64_reference_audit.get("limb_mapping", {})
    pose_error_metric = n64_reference_audit.get("pose_error_metric", {})
    validation_scope = n64_reference_audit.get("validation_scope", {})
    matched_count = int(mapping.get("matched_oot3d_count", 0) or 0) if isinstance(mapping, dict) else 0
    player_animation_data_count = (
        int(reference.get("player_animation_data_count", 0) or 0)
        if isinstance(reference, dict)
        else 0
    )
    frame_counts_ready = (
        blocker_count == 0
        and matched_count > 0
        and player_animation_data_count > 0
        and reference.get("player_animation_data_count_status") == "matched_by_xml_order"
        if isinstance(reference, dict)
        else False
    )
    payload_decode_ready = (
        blocker_count == 0
        and frame_counts_ready
        and payload_decode.get("status") == "decoded"
        if isinstance(payload_decode, dict)
        else False
    )
    time_samples_ready = (
        blocker_count == 0
        and payload_decode_ready
        and time_samples.get("status") == "sampled"
        if isinstance(time_samples, dict)
        else False
    )
    limb_mapping_ready = (
        blocker_count == 0
        and time_samples_ready
        and limb_mapping.get("status") == "candidate_ready_for_metric"
        if isinstance(limb_mapping, dict)
        else False
    )
    pose_error_metric_measured = (
        blocker_count == 0
        and limb_mapping_ready
        and pose_error_metric.get("status") == "measured"
        if isinstance(pose_error_metric, dict)
        else False
    )
    status = "incomplete"
    if blocker_count == 0 and matched_count > 0:
        if pose_error_metric_measured:
            status = "candidate_mapping_frame_counts_payload_decode_time_samples_limb_mapping_and_pose_metric_measured"
        elif limb_mapping_ready:
            status = "candidate_mapping_frame_counts_payload_decode_time_samples_and_limb_mapping_ready"
        elif time_samples_ready:
            status = "candidate_mapping_frame_counts_payload_decode_and_time_samples_ready"
        elif payload_decode_ready:
            status = "candidate_mapping_frame_counts_and_payload_decode_ready"
        elif frame_counts_ready:
            status = "candidate_mapping_and_frame_counts_ready"
        else:
            status = "candidate_mapping_ready"
    return {
        "status": status,
        "n64_reference_audit": str(n64_reference_audit_path),
        "reference_version": n64_reference_audit.get("reference_version"),
        "validation_scope_status": (
            validation_scope.get("status") if isinstance(validation_scope, dict) else None
        ),
        "n64_skeleton_status": skeleton.get("status") if isinstance(skeleton, dict) else None,
        "n64_skeleton_name": skeleton.get("name") if isinstance(skeleton, dict) else None,
        "n64_skeleton_limb_count": skeleton.get("limb_count") if isinstance(skeleton, dict) else None,
        "n64_player_animation_count": (
            reference.get("player_animation_count") if isinstance(reference, dict) else None
        ),
        "n64_player_animation_data_count": player_animation_data_count,
        "n64_player_animation_data_count_status": (
            reference.get("player_animation_data_count_status") if isinstance(reference, dict) else None
        ),
        "n64_frame_count_min": (
            data_summary.get("frame_count_min") if isinstance(data_summary, dict) else None
        ),
        "n64_frame_count_max": (
            data_summary.get("frame_count_max") if isinstance(data_summary, dict) else None
        ),
        "n64_frame_count_total": (
            data_summary.get("frame_count_total") if isinstance(data_summary, dict) else None
        ),
        "n64_payload_decode_status": payload_decode.get("status") if isinstance(payload_decode, dict) else None,
        "n64_payload_decode_count": payload_decode.get("decoded_count") if isinstance(payload_decode, dict) else 0,
        "n64_payload_decode_issue_count": payload_decode.get("issue_count") if isinstance(payload_decode, dict) else 0,
        "n64_payload_decode_status_counts": (
            payload_decode.get("status_counts", {}) if isinstance(payload_decode, dict) else {}
        ),
        "n64_payload_frame_ir_limb_count": (
            payload_frame_ir.get("limb_count") if isinstance(payload_frame_ir, dict) else None
        ),
        "n64_payload_frame_ir_layout": (
            payload_frame_ir.get("layout") if isinstance(payload_frame_ir, dict) else None
        ),
        "n64_skeleton_pose_reference_status": (
            skeleton_pose_reference.get("status") if isinstance(skeleton_pose_reference, dict) else None
        ),
        "n64_skeleton_pose_reference_limb_count": (
            skeleton_pose_reference.get("limb_count") if isinstance(skeleton_pose_reference, dict) else 0
        ),
        "n64_skeleton_pose_reference_issue_count": (
            skeleton_pose_reference.get("issue_count") if isinstance(skeleton_pose_reference, dict) else 0
        ),
        "n64_time_normalized_pose_sample_status": time_samples.get("status") if isinstance(time_samples, dict) else None,
        "n64_time_normalized_compared_match_count": (
            time_samples.get("compared_match_count") if isinstance(time_samples, dict) else 0
        ),
        "n64_time_normalized_sample_pair_count": (
            time_samples.get("sample_pair_count") if isinstance(time_samples, dict) else 0
        ),
        "n64_time_normalized_pose_sample_issue_count": (
            time_samples.get("issue_count") if isinstance(time_samples, dict) else 0
        ),
        "n64_limb_mapping_status": limb_mapping.get("status") if isinstance(limb_mapping, dict) else None,
        "n64_limb_mapping_count": limb_mapping.get("mapping_count") if isinstance(limb_mapping, dict) else 0,
        "n64_limb_mapping_issue_count": limb_mapping.get("issue_count") if isinstance(limb_mapping, dict) else 0,
        "n64_limb_mapping_root_motion_bone": (
            limb_mapping.get("oot3d_root_motion_bone") if isinstance(limb_mapping, dict) else None
        ),
        "n64_limb_mapping_auxiliary_animated_bone_count": (
            len(limb_mapping.get("oot3d_auxiliary_animated_bones", [])) if isinstance(limb_mapping, dict) else 0
        ),
        "n64_limb_mapping_unanimated_bone_count": (
            len(limb_mapping.get("oot3d_unanimated_bones", [])) if isinstance(limb_mapping, dict) else 0
        ),
        "n64_pose_error_metric_status": (
            pose_error_metric.get("status") if isinstance(pose_error_metric, dict) else None
        ),
        "n64_pose_error_metric_acceptance_status": (
            pose_error_metric.get("acceptance_status") if isinstance(pose_error_metric, dict) else None
        ),
        "n64_pose_error_metric_compared_match_count": (
            pose_error_metric.get("compared_match_count") if isinstance(pose_error_metric, dict) else 0
        ),
        "n64_pose_error_metric_sample_pair_count": (
            pose_error_metric.get("sample_pair_count") if isinstance(pose_error_metric, dict) else 0
        ),
        "n64_pose_error_metric_measured_pair_count": (
            pose_error_metric.get("measured_pair_count") if isinstance(pose_error_metric, dict) else 0
        ),
        "n64_pose_error_metric_issue_count": (
            pose_error_metric.get("issue_count") if isinstance(pose_error_metric, dict) else 0
        ),
        "n64_pose_error_metric_scale_factor_oot3d_per_n64": (
            pose_error_metric.get("scale_factor_oot3d_per_n64", {}) if isinstance(pose_error_metric, dict) else {}
        ),
        "n64_pose_error_metric_extent_ratio_oot3d_per_n64": (
            pose_error_metric.get("extent_ratio_oot3d_per_n64", {}) if isinstance(pose_error_metric, dict) else {}
        ),
        "n64_pose_error_metric_normalized_extent_mean_abs_delta": (
            pose_error_metric.get("normalized_extent_mean_abs_delta", {}) if isinstance(pose_error_metric, dict) else {}
        ),
        "n64_pose_error_metric_normalized_extent_max_abs_delta": (
            pose_error_metric.get("normalized_extent_max_abs_delta", {}) if isinstance(pose_error_metric, dict) else {}
        ),
        "matched_oot3d_count": matched_count,
        "unmatched_oot3d_count": int(mapping.get("unmatched_oot3d_count", 0) or 0) if isinstance(mapping, dict) else 0,
        "ambiguous_oot3d_count": int(mapping.get("ambiguous_oot3d_count", 0) or 0) if isinstance(mapping, dict) else 0,
        "candidate_status_counts": mapping.get("status_counts", {}) if isinstance(mapping, dict) else {},
        "timing_status_counts": mapping.get("timing_status_counts", {}) if isinstance(mapping, dict) else {},
        "blocker_count": blocker_count,
        "blocker_counts": blocker_counts if isinstance(blocker_counts, dict) else {},
        "numeric_pose_comparison_ready": pose_error_metric_measured,
        "numeric_pose_comparison_status": (
            "requires_n64_oot3d_pose_error_thresholds"
            if pose_error_metric_measured
            else "requires_n64_oot3d_pose_error_metric"
            if limb_mapping_ready
            else "requires_n64_oot3d_limb_mapping_pose_error_metric"
            if time_samples_ready
            else "requires_n64_player_animation_pose_sampler"
            if payload_decode_ready
            else "requires_n64_player_animation_payload_decoder"
            if frame_counts_ready
            else "requires_n64_player_animation_frame_decoder"
        ),
    }


def path_stem(value: object) -> str:
    name = normalize_path(value).split("/")[-1]
    return name.rsplit(".", 1)[0].lower()


def character_conversion_issues(
    target_summary: dict[str, object],
    animation_summary: dict[str, object],
    animation_like_summary: dict[str, object],
    material_summary: dict[str, object],
    pose_volume_summary: dict[str, object],
    native_bind_pose_summary: dict[str, object],
    n64_reference_summary: dict[str, object],
) -> list[dict[str, object]]:
    issues: list[dict[str, object]] = []
    if target_summary["status"] != "resolved":
        issues.append({"type": "missing_target", "severity": "error"})
    bind_pose = target_summary.get("bind_pose")
    if isinstance(bind_pose, dict) and not bind_pose.get("export_file_exists"):
        issues.append({"type": "missing_bind_pose_export", "severity": "error"})
    counts = target_summary.get("counts", {})
    if isinstance(counts, dict) and int(counts.get("validation_error_count", 0) or 0) != 0:
        issues.append(
            {
                "type": "bind_pose_validation_errors",
                "severity": "error",
                "count": counts.get("validation_error_count"),
            }
        )
    native_status = str(native_bind_pose_summary.get("status", "not_provided"))
    if native_status not in ("not_provided", "ready"):
        issues.append(
            {
                "type": "native_bind_pose_not_ready",
                "severity": "error",
                "status": native_status,
                "count": max(
                    1,
                    int(native_bind_pose_summary.get("missing_source_file_count", 0) or 0)
                    + int(native_bind_pose_summary.get("issue_count", 0) or 0),
                ),
            }
        )
    if int(animation_summary.get("count", 0)) == 0:
        issues.append({"type": "missing_csab_tracks", "severity": "error"})
    if int(animation_summary.get("missing_export_count", 0)) != 0:
        issues.append(
            {
                "type": "missing_csab_track_export",
                "severity": "error",
                "count": animation_summary.get("missing_export_count"),
            }
        )
    validation_counts = animation_summary.get("validation_status_counts", {})
    if isinstance(validation_counts, dict):
        invalid_count = sum(
            int(count)
            for status, count in validation_counts.items()
            if str(status) != "valid"
        )
        if invalid_count:
            issues.append(
                {
                    "type": "invalid_csab_track_validation",
                    "severity": "error",
                    "count": invalid_count,
                }
            )

    stem_overlap = animation_like_summary.get("stem_overlap", {})
    if isinstance(stem_overlap, dict) and int(stem_overlap.get("faceb_without_csab_count", 0) or 0) != 0:
        issues.append(
            {
                "type": "faceb_without_primary_csab_track",
                "severity": "warning",
                "count": stem_overlap.get("faceb_without_csab_count"),
            }
        )
    if int(material_summary.get("unresolved_or_ambiguous_count", 0) or 0) != 0:
        issues.append(
            {
                "type": "cmab_target_unresolved_or_ambiguous",
                "severity": "warning",
                "count": material_summary.get("unresolved_or_ambiguous_count"),
            }
        )
    pose_status = str(pose_volume_summary.get("status", "not_provided"))
    if pose_status != "not_provided":
        missing_pose_samples = int(pose_volume_summary.get("missing_pose_sample_file_count", 0) or 0)
        validation_errors = int(pose_volume_summary.get("validation_error_count", 0) or 0)
        invalid_pose_samples = int(pose_volume_summary.get("invalid_pose_sample_count", 0) or 0)
        if pose_status != "validated":
            issues.append(
                {
                    "type": "pose_volume_validation_incomplete",
                    "severity": "error",
                    "count": max(1, missing_pose_samples + validation_errors + invalid_pose_samples),
                }
            )
        if not bool(pose_volume_summary.get("all_sampled_pose_rows_finite")):
            issues.append(
                {
                    "type": "pose_volume_nonfinite_rows",
                    "severity": "error",
                    "count": max(1, validation_errors),
                }
            )
    n64_status = str(n64_reference_summary.get("status", "not_provided"))
    if n64_status not in (
        "not_provided",
        "candidate_mapping_ready",
        "candidate_mapping_and_frame_counts_ready",
        "candidate_mapping_frame_counts_and_payload_decode_ready",
        "candidate_mapping_frame_counts_payload_decode_and_time_samples_ready",
        "candidate_mapping_frame_counts_payload_decode_time_samples_and_limb_mapping_ready",
        "candidate_mapping_frame_counts_payload_decode_time_samples_limb_mapping_and_pose_metric_measured",
    ):
        issues.append(
            {
                "type": "n64_reference_candidate_mapping_incomplete",
                "severity": "error",
                "count": max(1, int(n64_reference_summary.get("blocker_count", 0) or 0)),
            }
        )
    return issues


def runtime_readiness(
    target_summary: dict[str, object],
    animation_summary: dict[str, object],
    animation_like_summary: dict[str, object],
    material_summary: dict[str, object],
    pose_volume_summary: dict[str, object],
    native_bind_pose_summary: dict[str, object],
    n64_reference_summary: dict[str, object],
) -> dict[str, object]:
    payload_counts = animation_like_summary.get("payload_type_counts", {})
    faceb_count = int(payload_counts.get("faceb", 0)) if isinstance(payload_counts, dict) else 0
    anb_count = int(payload_counts.get("anb", 0)) if isinstance(payload_counts, dict) else 0
    cmab_count = int(material_summary.get("count", 0) or 0)
    cmab_unresolved = int(material_summary.get("unresolved_or_ambiguous_count", 0) or 0)
    pose_status = str(pose_volume_summary.get("status", "not_provided"))
    n64_reference_status = str(n64_reference_summary.get("status", "not_provided"))
    n64_reference_ready_statuses = {
        "candidate_mapping_ready",
        "candidate_mapping_and_frame_counts_ready",
        "candidate_mapping_frame_counts_and_payload_decode_ready",
        "candidate_mapping_frame_counts_payload_decode_and_time_samples_ready",
        "candidate_mapping_frame_counts_payload_decode_time_samples_and_limb_mapping_ready",
        "candidate_mapping_frame_counts_payload_decode_time_samples_limb_mapping_and_pose_metric_measured",
    }
    return {
        "skinned_model_ir_ready": target_summary.get("status") == "resolved",
        "native_bind_pose_provided": native_bind_pose_summary.get("status") != "not_provided",
        "native_bind_pose_ready": native_bind_pose_summary.get("status") == "ready",
        "native_bind_pose_resource_count": native_bind_pose_summary.get("resource_count", 0),
        "skeleton_player_required": target_summary.get("status") == "resolved",
        "skinning_player_required": target_summary.get("status") == "resolved",
        "csab_player_required": int(animation_summary.get("count", 0) or 0) > 0,
        "pose_volume_validation_provided": pose_status != "not_provided",
        "pose_volume_validation_ready": (
            pose_status == "validated" and bool(pose_volume_summary.get("all_sampled_pose_rows_finite"))
        ),
        "n64_reference_candidate_mapping_provided": n64_reference_status != "not_provided",
        "n64_reference_candidate_mapping_ready": n64_reference_status in n64_reference_ready_statuses,
        "n64_reference_frame_counts_ready": n64_reference_status in (
            "candidate_mapping_and_frame_counts_ready",
            "candidate_mapping_frame_counts_and_payload_decode_ready",
            "candidate_mapping_frame_counts_payload_decode_and_time_samples_ready",
            "candidate_mapping_frame_counts_payload_decode_time_samples_and_limb_mapping_ready",
            "candidate_mapping_frame_counts_payload_decode_time_samples_limb_mapping_and_pose_metric_measured",
        ),
        "n64_reference_payload_decode_ready": (
            n64_reference_status in (
                "candidate_mapping_frame_counts_and_payload_decode_ready",
                "candidate_mapping_frame_counts_payload_decode_and_time_samples_ready",
                "candidate_mapping_frame_counts_payload_decode_time_samples_and_limb_mapping_ready",
                "candidate_mapping_frame_counts_payload_decode_time_samples_limb_mapping_and_pose_metric_measured",
            )
        ),
        "n64_time_normalized_pose_samples_ready": (
            n64_reference_status
            in (
                "candidate_mapping_frame_counts_payload_decode_and_time_samples_ready",
                "candidate_mapping_frame_counts_payload_decode_time_samples_and_limb_mapping_ready",
                "candidate_mapping_frame_counts_payload_decode_time_samples_limb_mapping_and_pose_metric_measured",
            )
        ),
        "n64_limb_mapping_ready": (
            n64_reference_status
            in (
                "candidate_mapping_frame_counts_payload_decode_time_samples_and_limb_mapping_ready",
                "candidate_mapping_frame_counts_payload_decode_time_samples_limb_mapping_and_pose_metric_measured",
            )
        ),
        "n64_pose_error_metric_measured": (
            n64_reference_status
            == "candidate_mapping_frame_counts_payload_decode_time_samples_limb_mapping_and_pose_metric_measured"
        ),
        "n64_numeric_pose_comparison_ready": bool(n64_reference_summary.get("numeric_pose_comparison_ready")),
        "faceb_player_required": faceb_count > 0,
        "anb_decoder_required": anb_count > 0,
        "cmab_material_player_required": cmab_count > 0,
        "cmab_material_binding_ready": cmab_count == 0 or cmab_unresolved == 0,
        "runtime_enablement": "blocked_until_player_and_material_feature_gates_pass",
    }


def issue_counts_by_occurrence(issues: list[dict[str, object]]) -> Counter[str]:
    counts: Counter[str] = Counter()
    for issue in issues:
        occurrence_count = issue.get("count", 1)
        if not isinstance(occurrence_count, int) or isinstance(occurrence_count, bool):
            occurrence_count = 1
        counts[str(issue["type"])] += occurrence_count
    counts["total"] = sum(counts.values())
    return counts


def without_internal_fields(animation_summary: dict[str, object]) -> dict[str, object]:
    return {
        key: value
        for key, value in animation_summary.items()
        if key != "csab_stems"
    }
