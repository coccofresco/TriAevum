from __future__ import annotations

from collections import Counter, defaultdict
import json
from pathlib import Path

from .binary import ParseError
from .romfs_inventory import sorted_counter


def audit_skinned_animation_readiness(
    bind_pose_manifest_path: Path,
    csab_track_manifest_path: Path,
    output_path: Path | None = None,
    *,
    sample_limit: int = 100,
) -> dict[str, object]:
    if not bind_pose_manifest_path.is_file():
        raise ParseError(f"{bind_pose_manifest_path}: skinned bind-pose batch manifest not found")
    if not csab_track_manifest_path.is_file():
        raise ParseError(f"{csab_track_manifest_path}: skinned CSAB track batch manifest not found")

    bind_manifest = json.loads(bind_pose_manifest_path.read_text(encoding="utf-8"))
    csab_manifest = json.loads(csab_track_manifest_path.read_text(encoding="utf-8"))
    bind_records = exported_bind_pose_records(bind_manifest)
    track_records = exported_track_records(csab_manifest)

    bind_by_key: dict[tuple[str, str], dict[str, object]] = {}
    duplicate_bind_keys: list[dict[str, object]] = []
    bind_export_missing: list[dict[str, object]] = []
    for record in bind_records:
        key = bind_record_key(record)
        if key in bind_by_key:
            duplicate_bind_keys.append(key_record(key, record))
        else:
            bind_by_key[key] = record
        if not Path(str(record.get("output", ""))).is_file():
            bind_export_missing.append(key_record(key, record))

    track_targets: dict[tuple[str, str], list[dict[str, object]]] = defaultdict(list)
    missing_bind_targets: list[dict[str, object]] = []
    bone_count_mismatches: list[dict[str, object]] = []
    non_skinned_exported_tracks: list[dict[str, object]] = []
    track_export_missing: list[dict[str, object]] = []
    support_status_counts: Counter[str] = Counter()
    resolution_status_counts: Counter[str] = Counter()
    aggregate_track_counts: Counter[str] = Counter()

    for record in track_records:
        key = track_record_key(record)
        track_targets[key].append(record)
        support_status = str(record.get("target_support_status", "<missing>"))
        support_status_counts[support_status] += 1
        resolution_status_counts[str(record.get("target_resolution_status", "<missing>"))] += 1
        merge_count_fields(aggregate_track_counts, record.get("counts", {}))
        if not support_status.startswith("needs_skinning"):
            non_skinned_exported_tracks.append(key_record(key, record))

        track_path = resolve_track_export(csab_track_manifest_path, csab_manifest, record)
        if not track_path.is_file():
            track_export_missing.append(key_record(key, record))

        bind_record = bind_by_key.get(key)
        if bind_record is None:
            missing_bind_targets.append(key_record(key, record))
            continue
        target_bone_count = target_bone_count_from_track(record)
        bind_bone_count = int(bind_record.get("bone_count", -1))
        if target_bone_count is not None and target_bone_count != bind_bone_count:
            bone_count_mismatches.append(
                {
                    **key_record(key, record),
                    "track_target_bone_count": target_bone_count,
                    "bind_pose_bone_count": bind_bone_count,
                }
            )

    referenced_keys = set(track_targets)
    unused_bind_keys = sorted(key for key in bind_by_key if key not in referenced_keys)
    usage_counts = Counter(len(records) for records in track_targets.values())
    unique_models_by_support: dict[str, set[tuple[str, str]]] = defaultdict(set)
    for key, records in track_targets.items():
        for record in records:
            unique_models_by_support[str(record.get("target_support_status", "<missing>"))].add(key)

    referenced_bind_counts = aggregate_bind_counts(
        record for key, record in bind_by_key.items() if key in referenced_keys
    )
    unused_bind_counts = aggregate_bind_counts(
        record for key, record in bind_by_key.items() if key not in referenced_keys
    )
    top_targets = sorted(
        (
            {
                "animation_count": len(records),
                "archive_path": key[0],
                "target_cmb_name": key[1],
                "support_status": str(records[0].get("target_support_status", "<missing>")),
            }
            for key, records in track_targets.items()
        ),
        key=lambda item: (
            -int(item["animation_count"]),
            str(item["archive_path"]),
            str(item["target_cmb_name"]),
        ),
    )
    issue_counts = {
        "duplicate_bind_pose_key": len(duplicate_bind_keys),
        "missing_bind_pose_target": len(missing_bind_targets),
        "bone_count_mismatch": len(bone_count_mismatches),
        "non_skinned_exported_track": len(non_skinned_exported_tracks),
        "missing_bind_pose_export_file": len(bind_export_missing),
        "missing_track_export_file": len(track_export_missing),
    }
    issue_counts["total"] = sum(issue_counts.values())

    audit: dict[str, object] = {
        "format": "oot3d_skinned_animation_readiness_audit_v1",
        "bind_pose_manifest": str(bind_pose_manifest_path),
        "csab_track_manifest": str(csab_track_manifest_path),
        "bind_pose_manifest_format": bind_manifest.get("format"),
        "csab_track_manifest_format": csab_manifest.get("format"),
        "bind_pose_export_count": len(bind_records),
        "csab_track_export_count": len(track_records),
        "unique_csab_target_count": len(track_targets),
        "csab_targets_with_bind_pose_count": len(referenced_keys - {track_record_key(item) for item in missing_bind_targets}),
        "unused_bind_pose_export_count": len(unused_bind_keys),
        "issue_counts": issue_counts,
        "support_status_counts": sorted_counter(support_status_counts),
        "target_resolution_status_counts": sorted_counter(resolution_status_counts),
        "unique_model_support_status_counts": {
            status: len(keys) for status, keys in sorted(unique_models_by_support.items())
        },
        "animation_count_per_target_counts": {
            str(key): usage_counts[key] for key in sorted(usage_counts)
        },
        "max_animation_count_per_target": max(usage_counts, default=0),
        "aggregate_track_counts": sorted_counter(aggregate_track_counts),
        "referenced_bind_pose_counts": referenced_bind_counts,
        "unused_bind_pose_counts": unused_bind_counts,
        "sample_top_targets": top_targets[:sample_limit],
        "sample_unused_bind_pose_targets": [
            {"archive_path": key[0], "target_cmb_name": key[1]}
            for key in unused_bind_keys[:sample_limit]
        ],
        "sample_duplicate_bind_pose_keys": duplicate_bind_keys[:sample_limit],
        "sample_missing_bind_pose_targets": missing_bind_targets[:sample_limit],
        "sample_bone_count_mismatches": bone_count_mismatches[:sample_limit],
        "sample_non_skinned_exported_tracks": non_skinned_exported_tracks[:sample_limit],
        "sample_missing_bind_pose_export_files": bind_export_missing[:sample_limit],
        "sample_missing_track_export_files": track_export_missing[:sample_limit],
        "fallback_policy": [
            "This audit proves offline association coverage only.",
            "It does not route Shipwright actor models or animations to OOT3D assets by itself.",
            "N64 actor models and animations remain fallback until explicit runtime binding is implemented.",
        ],
    }
    if output_path is not None:
        write_json(output_path, audit)
    return audit


def export_skinned_animation_binding_manifest(
    bind_pose_manifest_path: Path,
    csab_track_manifest_path: Path,
    output_path: Path | None = None,
    *,
    bind_pose_archive_prefix: str = "objects/oot3d/skinned_bind_pose",
    csab_track_archive_prefix: str = "animations/oot3d/csab/skinned",
    sample_limit: int = 100,
) -> dict[str, object]:
    if not bind_pose_manifest_path.is_file():
        raise ParseError(f"{bind_pose_manifest_path}: skinned bind-pose batch manifest not found")
    if not csab_track_manifest_path.is_file():
        raise ParseError(f"{csab_track_manifest_path}: skinned CSAB track batch manifest not found")

    bind_manifest = json.loads(bind_pose_manifest_path.read_text(encoding="utf-8"))
    csab_manifest = json.loads(csab_track_manifest_path.read_text(encoding="utf-8"))
    readiness = audit_skinned_animation_readiness(
        bind_pose_manifest_path,
        csab_track_manifest_path,
        sample_limit=sample_limit,
    )
    bind_by_key = {
        bind_record_key(record): record
        for record in exported_bind_pose_records(bind_manifest)
    }
    track_targets: dict[tuple[str, str], list[dict[str, object]]] = defaultdict(list)
    for record in exported_track_records(csab_manifest):
        track_targets[track_record_key(record)].append(record)

    targets: list[dict[str, object]] = []
    for key in sorted(track_targets):
        bind_record = bind_by_key[key]
        tracks = sorted(
            track_targets[key],
            key=lambda record: (
                normalize_path(record.get("csab_name")),
                normalize_path(record.get("track_export")),
            ),
        )
        resolution_counts = Counter(
            str(record.get("target_resolution_status", "<missing>"))
            for record in tracks
        )
        target_support_counts = Counter(
            str(record.get("target_support_status", "<missing>"))
            for record in tracks
        )
        targets.append(
            {
                "target_id": safe_binding_id(key[0], key[1]),
                "archive_path": key[0],
                "target_cmb_name": key[1],
                "model_name": bind_record.get("model_name"),
                "bone_count": bind_record.get("bone_count"),
                "support_status_counts": sorted_counter(target_support_counts),
                "target_resolution_status_counts": sorted_counter(resolution_counts),
                "bind_pose": {
                    "export": bind_record.get("output"),
                    "package_entry": package_entry_for_bind_pose(
                        bind_pose_manifest_path,
                        bind_manifest,
                        bind_record,
                        bind_pose_archive_prefix,
                    ),
                    "counts": bind_record.get("counts", {}),
                },
                "animation_count": len(tracks),
                "animations": [
                    binding_animation_record(
                        csab_track_manifest_path,
                        csab_manifest,
                        record,
                        csab_track_archive_prefix,
                    )
                    for record in tracks
                ],
            }
        )

    unused_bind_pose_targets = []
    for key in sorted(key for key in bind_by_key if key not in track_targets):
        record = bind_by_key[key]
        unused_bind_pose_targets.append(
            {
                "target_id": safe_binding_id(key[0], key[1]),
                "archive_path": key[0],
                "target_cmb_name": key[1],
                "model_name": record.get("model_name"),
                "bone_count": record.get("bone_count"),
                "bind_pose_export": record.get("output"),
                "bind_pose_package_entry": package_entry_for_bind_pose(
                    bind_pose_manifest_path,
                    bind_manifest,
                    record,
                    bind_pose_archive_prefix,
                ),
                "counts": record.get("counts", {}),
            }
        )

    manifest: dict[str, object] = {
        "format": "oot3d_skinned_animation_binding_manifest_v1",
        "bind_pose_manifest": str(bind_pose_manifest_path),
        "csab_track_manifest": str(csab_track_manifest_path),
        "bind_pose_archive_prefix": normalize_path(bind_pose_archive_prefix),
        "csab_track_archive_prefix": normalize_path(csab_track_archive_prefix),
        "readiness_summary": {
            "bind_pose_export_count": readiness["bind_pose_export_count"],
            "csab_track_export_count": readiness["csab_track_export_count"],
            "unique_csab_target_count": readiness["unique_csab_target_count"],
            "csab_targets_with_bind_pose_count": readiness[
                "csab_targets_with_bind_pose_count"
            ],
            "unused_bind_pose_export_count": readiness["unused_bind_pose_export_count"],
            "issue_counts": readiness["issue_counts"],
            "support_status_counts": readiness["support_status_counts"],
            "unique_model_support_status_counts": readiness[
                "unique_model_support_status_counts"
            ],
            "target_resolution_status_counts": readiness[
                "target_resolution_status_counts"
            ],
            "animation_count_per_target_counts": readiness[
                "animation_count_per_target_counts"
            ],
            "max_animation_count_per_target": readiness[
                "max_animation_count_per_target"
            ],
            "referenced_bind_pose_counts": readiness["referenced_bind_pose_counts"],
            "unused_bind_pose_counts": readiness["unused_bind_pose_counts"],
            "aggregate_track_counts": readiness["aggregate_track_counts"],
        },
        "target_count": len(targets),
        "animation_count": sum(int(target["animation_count"]) for target in targets),
        "unused_bind_pose_target_count": len(unused_bind_pose_targets),
        "targets": targets,
        "unused_bind_pose_targets": unused_bind_pose_targets,
        "fallback_policy": [
            "This is an offline binding manifest for future runtime work.",
            "It does not route Shipwright actor models or animations to OOT3D assets by itself.",
            "N64 actor models and animations remain fallback until explicit runtime binding is implemented.",
        ],
    }
    if output_path is not None:
        write_json(output_path, manifest)
    return manifest


def exported_bind_pose_records(manifest: dict[str, object]) -> list[dict[str, object]]:
    return [
        record
        for record in manifest.get("records", [])
        if isinstance(record, dict) and record.get("status") == "exported"
    ]


def exported_track_records(manifest: dict[str, object]) -> list[dict[str, object]]:
    return [
        record
        for record in manifest.get("records", [])
        if isinstance(record, dict) and record.get("status") == "exported"
    ]


def bind_record_key(record: dict[str, object]) -> tuple[str, str]:
    return (
        normalize_path(record.get("container_path")),
        normalize_path(record.get("embedded_name")),
    )


def track_record_key(record: dict[str, object]) -> tuple[str, str]:
    return (
        normalize_path(record.get("archive_path")),
        normalize_path(record.get("target_cmb_name")),
    )


def normalize_path(value: object) -> str:
    return str(value).replace("\\", "/").strip("/")


def key_record(key: tuple[str, str], record: dict[str, object]) -> dict[str, object]:
    result: dict[str, object] = {
        "archive_path": key[0],
        "target_cmb_name": key[1],
    }
    for field in (
        "csab_name",
        "track_export",
        "target_support_status",
        "target_resolution_status",
        "output",
        "model_name",
    ):
        if field in record:
            result[field] = record[field]
    return result


def target_bone_count_from_track(record: dict[str, object]) -> int | None:
    validation = record.get("validation")
    if isinstance(validation, dict) and "target_bone_count" in validation:
        return int(validation["target_bone_count"])
    return None


def resolve_track_export(
    manifest_path: Path,
    manifest: dict[str, object],
    record: dict[str, object],
) -> Path:
    path = Path(str(record.get("track_export", "")))
    if path.is_absolute() or path.exists():
        return path
    root = Path(str(manifest.get("output", manifest_path.parent)))
    return root / path


def package_entry_for_bind_pose(
    manifest_path: Path,
    manifest: dict[str, object],
    record: dict[str, object],
    archive_prefix: str,
) -> str:
    output = Path(str(record.get("output", "")))
    relative = output.name
    roots = [
        Path(str(manifest.get("output_dir", ""))),
        manifest_path.parent,
    ]
    for root in roots:
        try:
            if root:
                relative = output.relative_to(root).as_posix()
                break
        except ValueError:
            continue
    return normalize_path(f"{archive_prefix}/{relative}")


def package_entry_for_track(
    record: dict[str, object],
    archive_prefix: str,
) -> str:
    return normalize_path(f"{archive_prefix}/{normalize_path(record.get('track_export'))}")


def binding_animation_record(
    manifest_path: Path,
    manifest: dict[str, object],
    record: dict[str, object],
    archive_prefix: str,
) -> dict[str, object]:
    track_path = resolve_track_export(manifest_path, manifest, record)
    result: dict[str, object] = {
        "csab_name": normalize_path(record.get("csab_name")),
        "track_export": normalize_path(record.get("track_export")),
        "track_export_file": str(track_path),
        "track_package_entry": package_entry_for_track(record, archive_prefix),
        "target_support_status": record.get("target_support_status"),
        "target_resolution_status": record.get("target_resolution_status"),
        "frame_slot_count": record.get("frame_slot_count"),
        "counts": record.get("counts", {}),
    }
    validation = record.get("validation")
    if isinstance(validation, dict):
        result["validation"] = {
            key: validation[key]
            for key in (
                "valid",
                "status",
                "target_bone_count",
                "sampled_pose_frames",
                "sampled_channel_values",
                "finite_world_matrix_entries",
                "non_f32_channel_blocks",
            )
            if key in validation
        }
    return result


def safe_binding_id(archive_path: str, target_cmb_name: str) -> str:
    safe = []
    for char in f"{archive_path}__{target_cmb_name}":
        if char.isalnum():
            safe.append(char)
        else:
            safe.append("_")
    return "_".join(part for part in "".join(safe).split("_") if part).lower()


def merge_count_fields(target: Counter[str], source: object) -> None:
    if not isinstance(source, dict):
        return
    for key, value in source.items():
        if isinstance(value, int) and not isinstance(value, bool):
            target[str(key)] += value


def aggregate_bind_counts(records) -> dict[str, int]:
    counts: Counter[str] = Counter()
    for record in records:
        source = record.get("counts")
        if not isinstance(source, dict):
            continue
        for key in (
            "mesh_count",
            "skinned_primitive_count",
            "mode_1_primitive_count",
            "mode_2_primitive_count",
            "skinned_vertex_rows",
            "skeleton_bone_count",
            "finite_bind_world_matrix_entries",
            "validation_error_count",
        ):
            counts[key] += int(source.get(key, 0))
    return {key: counts[key] for key in sorted(counts)}


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8", newline="\n")
