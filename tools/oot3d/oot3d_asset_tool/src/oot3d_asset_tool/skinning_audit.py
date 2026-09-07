from __future__ import annotations

import json
from collections import Counter, defaultdict
from pathlib import Path

from .actor_inventory import (
    CURRENT_RIGID_EXPORT_TARGET_SUPPORT,
    model_primitive_bone_counts,
    model_skinning_mode_counts,
    model_support_status,
)
from .binary import ParseError
from .cmb import CmbModel
from .csab_tracks import is_cmb_file, is_csab_file, resolve_csab_target
from .romfs_inventory import sorted_counter
from .zar import ZarArchive, ZarFile


def audit_actor_skinning_queue(
    actor_root: Path,
    output_path: Path | None = None,
    *,
    sample_limit: int = 100,
) -> dict[str, object]:
    if not actor_root.is_dir():
        raise ParseError(f"{actor_root}: expected an extracted OOT3D actor directory")

    paths = sorted(path for path in actor_root.rglob("*") if path.is_file())
    zar_paths = [path for path in paths if path.suffix.lower() == ".zar"]
    loose_cmb_paths = [path for path in paths if path.suffix.lower() == ".cmb"]

    parse_errors: list[dict[str, object]] = []
    skinned_model_records: list[dict[str, object]] = []
    csab_blocked_samples: list[dict[str, object]] = []
    target_csab_counts: defaultdict[str, int] = defaultdict(int)
    target_csab_samples: defaultdict[str, list[str]] = defaultdict(list)

    model_counts: Counter[str] = Counter()
    support_status_counts: Counter[str] = Counter()
    skinned_support_status_counts: Counter[str] = Counter()
    skinned_archive_paths: set[str] = set()
    skinned_archives_with_csab: set[str] = set()
    skinned_archives_with_cmab: set[str] = set()
    skinning_mode_primitive_counts: Counter[str] = Counter()
    skinned_primitive_mode_counts: Counter[str] = Counter()
    primitive_bone_count_counts: Counter[str] = Counter()
    skinning_mode_bone_count_counts: Counter[str] = Counter()
    bone_count_counts: Counter[str] = Counter()
    aggregate_profile = empty_primitive_profile()

    csab_counts: Counter[str] = Counter()
    csab_target_support_counts: Counter[str] = Counter()
    csab_target_resolution_counts: Counter[str] = Counter()

    for path in loose_cmb_paths:
        model_counts["discovered"] += 1
        try:
            model = CmbModel.from_path(path)
        except Exception as exc:
            parse_errors.append(parse_error(path, actor_root, "cmb_model", str(exc)))
            continue
        add_model_audit(
            model,
            path,
            actor_root,
            "cmb",
            None,
            model_counts,
            support_status_counts,
            skinned_support_status_counts,
            skinned_archive_paths,
            skinned_archives_with_csab,
            skinned_archives_with_cmab,
            skinning_mode_primitive_counts,
            skinned_primitive_mode_counts,
            primitive_bone_count_counts,
            skinning_mode_bone_count_counts,
            bone_count_counts,
            aggregate_profile,
            skinned_model_records,
            archive_has_csab=False,
            archive_has_cmab=False,
        )

    for path in zar_paths:
        archive_path = path.relative_to(actor_root).as_posix()
        try:
            archive = ZarArchive.from_path(path)
        except Exception as exc:
            parse_errors.append(parse_error(path, actor_root, "zar_archive", str(exc)))
            continue

        archive_has_csab = any(is_csab_file(file) for file in archive.files)
        archive_has_cmab = any(
            file.type_name == "cmab" or file.name.lower().endswith(".cmab")
            for file in archive.files
        )
        cmb_records: list[dict[str, object]] = []

        for file in archive.files:
            if not is_cmb_file(file):
                continue
            model_counts["discovered"] += 1
            try:
                model = CmbModel.parse(archive.read_file(file), f"{path}!{file.name}")
            except Exception as exc:
                parse_errors.append(
                    parse_error(
                        path,
                        actor_root,
                        "cmb_model",
                        str(exc),
                        embedded_name=file.name,
                    )
                )
                continue
            support_status = model_support_status(model)
            cmb_records.append(
                {
                    "embedded_name": file.name,
                    "model_name": model.name,
                    "bone_count": model.bone_count,
                    "support_status": support_status,
                }
            )
            add_model_audit(
                model,
                path,
                actor_root,
                "zar",
                file,
                model_counts,
                support_status_counts,
                skinned_support_status_counts,
                skinned_archive_paths,
                skinned_archives_with_csab,
                skinned_archives_with_cmab,
                skinning_mode_primitive_counts,
                skinned_primitive_mode_counts,
                primitive_bone_count_counts,
                skinning_mode_bone_count_counts,
                bone_count_counts,
                aggregate_profile,
                skinned_model_records,
                archive_has_csab=archive_has_csab,
                archive_has_cmab=archive_has_cmab,
            )

        for file in archive.files:
            if not is_csab_file(file):
                continue
            csab_counts["considered"] += 1
            try:
                target_status, target_candidates = resolve_csab_target(
                    archive,
                    file,
                    cmb_records,
                )
            except Exception as exc:
                csab_counts["failed"] += 1
                parse_errors.append(
                    parse_error(
                        path,
                        actor_root,
                        "csab_target_resolution",
                        str(exc),
                        embedded_name=file.name,
                    )
                )
                continue
            csab_target_resolution_counts[target_status] += 1
            if len(target_candidates) != 1:
                csab_counts["target_unresolved_or_missing"] += 1
                continue

            target = target_candidates[0]
            support_status = str(target["support_status"])
            csab_target_support_counts[support_status] += 1
            if support_status in CURRENT_RIGID_EXPORT_TARGET_SUPPORT:
                csab_counts["target_export_supported_by_current_rigid_path"] += 1
                continue
            if support_status.startswith("needs_skinning"):
                csab_counts["target_needs_skinning_support"] += 1
                target_name = str(target["embedded_name"])
                target_key = f"{archive_path}!{target_name}"
                target_csab_counts[target_key] += 1
                if len(target_csab_samples[target_key]) < 5:
                    target_csab_samples[target_key].append(file.name)
                if len(csab_blocked_samples) < sample_limit:
                    csab_blocked_samples.append(
                        {
                            "archive_path": archive_path,
                            "csab_name": file.name,
                            "target_resolution_status": target_status,
                            "target_cmb_name": target_name,
                            "target_support_status": support_status,
                            "target_model_name": target.get("model_name"),
                            "target_bone_count": target.get("bone_count"),
                        }
                    )
                continue
            csab_counts["target_needs_unknown_support"] += 1

    for record in skinned_model_records:
        target_key = f"{record['container_path']}!{record['embedded_name']}"
        csab_count = target_csab_counts[target_key]
        record["resolved_csab_target_count"] = csab_count
        record["resolved_csab_target_samples"] = target_csab_samples[target_key]

    pilot_candidates = sorted(
        (
            record
            for record in skinned_model_records
            if int(record["resolved_csab_target_count"]) > 0
        ),
        key=skinning_pilot_sort_key,
    )[:sample_limit]
    top_csab_targets = sorted(
        skinned_model_records,
        key=lambda record: (
            -int(record["resolved_csab_target_count"]),
            str(record["container_path"]),
            str(record["embedded_name"]),
        ),
    )[:sample_limit]

    model_counts["parsed"] = sum(support_status_counts.values())
    model_counts["parse_errors"] = len(
        [error for error in parse_errors if error.get("kind") == "cmb_model"]
    )

    audit = {
        "format": "oot3d_actor_skinning_queue_v1",
        "actor_root": str(actor_root),
        "file_count": len(paths),
        "archive_count": len(zar_paths),
        "loose_cmb_count": len(loose_cmb_paths),
        "model_counts": {
            "discovered": model_counts["discovered"],
            "parsed": model_counts["parsed"],
            "parse_errors": model_counts["parse_errors"],
            "skinned_model_count": sum(skinned_support_status_counts.values()),
            "skinned_archive_count": len(skinned_archive_paths),
            "skinned_archives_with_csab": len(skinned_archives_with_csab),
            "skinned_archives_with_cmab": len(skinned_archives_with_cmab),
        },
        "support_status_counts": sorted_counter(support_status_counts),
        "skinned_support_status_counts": sorted_counter(skinned_support_status_counts),
        "skinning_mode_primitive_counts": sorted_counter(skinning_mode_primitive_counts),
        "skinned_primitive_mode_counts": sorted_counter(skinned_primitive_mode_counts),
        "primitive_bone_count_counts": sorted_counter(primitive_bone_count_counts),
        "skinning_mode_bone_count_counts": sorted_counter(skinning_mode_bone_count_counts),
        "bone_count_counts": sorted_counter(bone_count_counts),
        "skinned_primitive_profile": aggregate_profile,
        "csab_counts": {
            "considered": csab_counts["considered"],
            "failed": csab_counts["failed"],
            "target_export_supported_by_current_rigid_path": csab_counts[
                "target_export_supported_by_current_rigid_path"
            ],
            "target_needs_skinning_support": csab_counts["target_needs_skinning_support"],
            "target_unresolved_or_missing": csab_counts["target_unresolved_or_missing"],
            "target_needs_unknown_support": csab_counts["target_needs_unknown_support"],
        },
        "csab_target_resolution_counts": sorted_counter(csab_target_resolution_counts),
        "csab_target_support_counts": sorted_counter(csab_target_support_counts),
        "pilot_candidates": compact_model_records(pilot_candidates),
        "top_csab_targets": compact_model_records(top_csab_targets),
        "csab_blocked_samples": csab_blocked_samples,
        "skinned_model_records": skinned_model_records,
        "parse_error_count": len(parse_errors),
        "parse_errors": parse_errors,
    }
    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(
            json.dumps(audit, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
    return audit


def add_model_audit(
    model: CmbModel,
    path: Path,
    actor_root: Path,
    container_type: str,
    embedded_file: ZarFile | None,
    model_counts: Counter[str],
    support_status_counts: Counter[str],
    skinned_support_status_counts: Counter[str],
    skinned_archive_paths: set[str],
    skinned_archives_with_csab: set[str],
    skinned_archives_with_cmab: set[str],
    skinning_mode_primitive_counts: Counter[str],
    skinned_primitive_mode_counts: Counter[str],
    primitive_bone_count_counts: Counter[str],
    skinning_mode_bone_count_counts: Counter[str],
    bone_count_counts: Counter[str],
    aggregate_profile: dict[str, object],
    skinned_model_records: list[dict[str, object]],
    *,
    archive_has_csab: bool,
    archive_has_cmab: bool,
) -> None:
    support_status = model_support_status(model)
    support_status_counts[support_status] += 1
    bone_count_counts[str(model.bone_count)] += 1
    skinning_modes = model_skinning_mode_counts(model)
    skinning_mode_primitive_counts.update(skinning_modes)
    primitive_bone_counts, skinning_mode_bone_counts = model_primitive_bone_counts(model)
    primitive_bone_count_counts.update(primitive_bone_counts)
    skinning_mode_bone_count_counts.update(skinning_mode_bone_counts)
    profile = primitive_skinning_profile(model)

    if not support_status.startswith("needs_skinning"):
        return

    archive_path = path.relative_to(actor_root).as_posix()
    skinned_archive_paths.add(archive_path)
    if archive_has_csab:
        skinned_archives_with_csab.add(archive_path)
    if archive_has_cmab:
        skinned_archives_with_cmab.add(archive_path)
    skinned_support_status_counts[support_status] += 1
    skinned_primitive_mode_counts.update(
        {
            mode: count
            for mode, count in skinning_modes.items()
            if mode != "0"
        }
    )
    merge_primitive_profile(aggregate_profile, profile)
    skinned_model_records.append(
        {
            "container_path": archive_path,
            "container_type": container_type,
            "embedded_name": embedded_file.name if embedded_file is not None else None,
            "embedded_index": embedded_file.index if embedded_file is not None else None,
            "embedded_type": embedded_file.type_name if embedded_file is not None else None,
            "model_name": model.name,
            "support_status": support_status,
            "bone_count": model.bone_count,
            "skeleton": model.skeleton.summary(),
            "mesh_count": len(model.meshes),
            "shape_count": len(model.shapes),
            "material_count": len(model.materials),
            "texture_count": len(model.textures),
            "skinning_mode_primitive_counts": sorted_counter(skinning_modes),
            "primitive_bone_count_counts": sorted_counter(primitive_bone_counts),
            "skinning_mode_bone_count_counts": sorted_counter(skinning_mode_bone_counts),
            "primitive_profile": profile,
        }
    )


def empty_primitive_profile() -> dict[str, object]:
    return {
        "primitive_count": 0,
        "skinned_primitive_count": 0,
        "rigid_primitive_count": 0,
        "triangle_count": 0,
        "skinned_triangle_count": 0,
        "index_count": 0,
        "skinned_index_count": 0,
        "max_bone_palette_size": 0,
        "max_bone_index": -1,
        "invalid_bone_index_primitives": 0,
        "duplicate_bone_palette_primitives": 0,
        "empty_bone_palette_primitives": 0,
    }


def primitive_skinning_profile(model: CmbModel) -> dict[str, object]:
    profile = empty_primitive_profile()
    for shape in model.shapes:
        for primitive in shape.primitives:
            is_skinned = primitive.skinning_mode != 0
            triangle_count = len(primitive.indices) // 3
            index_count = len(primitive.indices)
            bone_palette_size = len(primitive.bone_indices)
            profile["primitive_count"] = int(profile["primitive_count"]) + 1
            profile["triangle_count"] = int(profile["triangle_count"]) + triangle_count
            profile["index_count"] = int(profile["index_count"]) + index_count
            profile["max_bone_palette_size"] = max(
                int(profile["max_bone_palette_size"]),
                bone_palette_size,
            )
            if primitive.bone_indices:
                profile["max_bone_index"] = max(
                    int(profile["max_bone_index"]),
                    max(primitive.bone_indices),
                )
            else:
                profile["empty_bone_palette_primitives"] = (
                    int(profile["empty_bone_palette_primitives"]) + 1
                )
            if any(index >= model.bone_count for index in primitive.bone_indices):
                profile["invalid_bone_index_primitives"] = (
                    int(profile["invalid_bone_index_primitives"]) + 1
                )
            if len(set(primitive.bone_indices)) != len(primitive.bone_indices):
                profile["duplicate_bone_palette_primitives"] = (
                    int(profile["duplicate_bone_palette_primitives"]) + 1
                )
            if is_skinned:
                profile["skinned_primitive_count"] = int(profile["skinned_primitive_count"]) + 1
                profile["skinned_triangle_count"] = (
                    int(profile["skinned_triangle_count"]) + triangle_count
                )
                profile["skinned_index_count"] = int(profile["skinned_index_count"]) + index_count
            else:
                profile["rigid_primitive_count"] = int(profile["rigid_primitive_count"]) + 1
    return profile


def merge_primitive_profile(
    aggregate: dict[str, object],
    profile: dict[str, object],
) -> None:
    for key in (
        "primitive_count",
        "skinned_primitive_count",
        "rigid_primitive_count",
        "triangle_count",
        "skinned_triangle_count",
        "index_count",
        "skinned_index_count",
        "invalid_bone_index_primitives",
        "duplicate_bone_palette_primitives",
        "empty_bone_palette_primitives",
    ):
        aggregate[key] = int(aggregate[key]) + int(profile[key])
    aggregate["max_bone_palette_size"] = max(
        int(aggregate["max_bone_palette_size"]),
        int(profile["max_bone_palette_size"]),
    )
    aggregate["max_bone_index"] = max(
        int(aggregate["max_bone_index"]),
        int(profile["max_bone_index"]),
    )


def skinning_pilot_sort_key(record: dict[str, object]) -> tuple[object, ...]:
    support_rank = {
        "needs_skinning_mode_1_support": 0,
        "needs_skinning_mode_2_support": 1,
        "needs_skinning_mode_1_and_2_support": 2,
    }.get(str(record["support_status"]), 9)
    profile = record["primitive_profile"]
    if not isinstance(profile, dict):
        raise ParseError("skinned model record is missing primitive profile")
    return (
        support_rank,
        int(record["bone_count"]),
        int(profile["skinned_primitive_count"]),
        int(profile["skinned_triangle_count"]),
        -int(record["resolved_csab_target_count"]),
        str(record["container_path"]),
        str(record["embedded_name"]),
    )


def compact_model_records(records: list[dict[str, object]]) -> list[dict[str, object]]:
    compact: list[dict[str, object]] = []
    for record in records:
        profile = record["primitive_profile"]
        if not isinstance(profile, dict):
            raise ParseError("skinned model record is missing primitive profile")
        compact.append(
            {
                "container_path": record["container_path"],
                "embedded_name": record["embedded_name"],
                "model_name": record["model_name"],
                "support_status": record["support_status"],
                "bone_count": record["bone_count"],
                "resolved_csab_target_count": record["resolved_csab_target_count"],
                "skinned_primitive_count": profile["skinned_primitive_count"],
                "skinned_triangle_count": profile["skinned_triangle_count"],
                "max_bone_palette_size": profile["max_bone_palette_size"],
                "resolved_csab_target_samples": record["resolved_csab_target_samples"],
            }
        )
    return compact


def parse_error(
    path: Path,
    actor_root: Path,
    kind: str,
    message: str,
    *,
    embedded_name: str | None = None,
) -> dict[str, object]:
    return {
        "path": path.relative_to(actor_root).as_posix(),
        "kind": kind,
        "embedded_name": embedded_name,
        "message": message,
    }
