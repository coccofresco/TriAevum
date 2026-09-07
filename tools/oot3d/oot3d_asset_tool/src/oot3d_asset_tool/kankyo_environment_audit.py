from __future__ import annotations

import json
from collections import Counter
from pathlib import Path

from .actor_inventory import (
    ANIMATION_HEADER_FIELD_OFFSETS,
    animation_declared_size,
    animation_header_fields,
    animation_header_version,
    format_magic,
    model_primitive_bone_counts,
    model_skinning_mode_counts,
    model_support_status,
    sorted_nested_counter,
)
from .binary import ParseError
from .cmb import CmbModel
from .cmab_audit import (
    CMAB_FRAME_COUNT_CANDIDATE_OFFSET,
    CMAB_LOOP_MODE_CANDIDATE_OFFSET,
    cmab_layout_metadata,
    declared_size_match_status,
    header_word,
    size_summary,
)
from .romfs_inventory import sorted_counter
from .zar import ZarArchive


def audit_kankyo_environment_assets(
    kankyo_root: Path,
    output_path: Path | None = None,
    *,
    sample_limit: int = 100,
    include_records: bool = True,
) -> dict[str, object]:
    if not kankyo_root.is_dir():
        raise ParseError(f"{kankyo_root}: expected an extracted OOT3D kankyo directory")

    archive_count = 0
    archive_size_total = 0
    archive_file_count_total = 0
    archive_parse_errors: list[dict[str, object]] = []
    cmb_parse_errors: list[dict[str, object]] = []
    records: list[dict[str, object]] = []
    sample_records: list[dict[str, object]] = []

    archive_file_count_counts: Counter[str] = Counter()
    embedded_type_counts: Counter[str] = Counter()
    cmb_support_counts: Counter[str] = Counter()
    cmb_bone_count_counts: Counter[str] = Counter()
    cmb_mesh_count_counts: Counter[str] = Counter()
    cmb_material_count_counts: Counter[str] = Counter()
    cmb_texture_count_counts: Counter[str] = Counter()
    cmb_skeleton_header_counts: Counter[str] = Counter()
    cmb_skinning_mode_primitive_counts: Counter[str] = Counter()
    cmb_primitive_bone_count_counts: Counter[str] = Counter()
    cmb_skinning_mode_bone_count_counts: Counter[str] = Counter()
    environment_model_group_counts: Counter[str] = Counter()
    material_alpha_test_counts: Counter[str] = Counter()
    material_blend_mode_counts: Counter[str] = Counter()
    material_depth_state_counts: Counter[str] = Counter()

    cmb_discovered = 0
    cmb_parsed = 0
    cmb_triangle_total = 0
    cmb_vertex_total = 0
    cmb_primitive_total = 0
    cmb_shape_total = 0
    cmab_sizes: list[int] = []
    ctxb_sizes: list[int] = []
    tbd_sizes: list[int] = []
    cmab_magic_counts: Counter[str] = Counter()
    cmab_declared_size_match_counts: Counter[str] = Counter()
    cmab_header_version_counts: Counter[str] = Counter()
    cmab_header_field_counts: dict[str, Counter[str]] = {
        f"word_{offset:02x}": Counter()
        for offset in ANIMATION_HEADER_FIELD_OFFSETS["cmab"]
    }
    cmab_layout_status_counts: Counter[str] = Counter()
    cmab_mmad_count_counts: Counter[str] = Counter()
    cmab_mads_record_count_counts: Counter[str] = Counter()
    cmab_frame_count_candidate_counts: Counter[str] = Counter()
    cmab_loop_mode_candidate_counts: Counter[str] = Counter()
    ctxb_magic_counts: Counter[str] = Counter()
    tbd_magic_counts: Counter[str] = Counter()
    tbd_version_counts: Counter[str] = Counter()
    tbd_declared_size_match_counts: Counter[str] = Counter()
    tbd_entry_count_counts: Counter[str] = Counter()

    for archive_path in sorted(kankyo_root.glob("*.zar")):
        archive_count += 1
        archive_size_total += archive_path.stat().st_size
        archive_rel = archive_path.relative_to(kankyo_root).as_posix()
        try:
            archive = ZarArchive.from_path(archive_path)
        except Exception as exc:
            archive_parse_errors.append(
                {
                    "archive_path": archive_rel,
                    "reason": str(exc),
                }
            )
            continue

        archive_file_count_total += len(archive.files)
        archive_file_count_counts[str(len(archive.files))] += 1
        archive_embedded_type_counts = Counter(file.type_name for file in archive.files)
        embedded_type_counts.update(archive_embedded_type_counts)

        archive_record: dict[str, object] = {
            "archive_path": archive_rel,
            "size": archive_path.stat().st_size,
            "file_count": len(archive.files),
            "embedded_type_counts": sorted_counter(archive_embedded_type_counts),
            "cmb_records": [],
            "cmab_records": [],
            "ctxb_records": [],
            "tbd_records": [],
        }

        for file in archive.files:
            data = archive.read_file(file)
            if file.type_name == "cmb":
                cmb_discovered += 1
                try:
                    model = CmbModel.parse(
                        data,
                        f"{archive_path}!{file.name}",
                    )
                except Exception as exc:
                    cmb_parse_errors.append(
                        {
                            "archive_path": archive_rel,
                            "embedded_name": file.name,
                            "reason": str(exc),
                        }
                    )
                    continue

                cmb_parsed += 1
                summary = model.summary()
                support_status = model_support_status(model)
                group = environment_model_group(file.name)
                primitive_bone_counts, skinning_mode_bone_counts = model_primitive_bone_counts(model)
                cmb_support_counts[support_status] += 1
                cmb_bone_count_counts[str(model.bone_count)] += 1
                cmb_mesh_count_counts[str(summary["mesh_count"])] += 1
                cmb_material_count_counts[str(len(model.materials))] += 1
                cmb_texture_count_counts[str(len(model.textures))] += 1
                cmb_skeleton_header_counts[str(model.skeleton.header_word_0c)] += 1
                cmb_skinning_mode_primitive_counts.update(model_skinning_mode_counts(model))
                cmb_primitive_bone_count_counts.update(primitive_bone_counts)
                cmb_skinning_mode_bone_count_counts.update(skinning_mode_bone_counts)
                environment_model_group_counts[group] += 1
                cmb_triangle_total += int(summary["triangle_count"])
                cmb_vertex_total += int(summary["vertex_count"])
                cmb_primitive_total += int(summary["primitive_count"])
                cmb_shape_total += int(summary["shape_count"])

                for material in model.materials:
                    material_alpha_test_counts[str(material.alpha_test)] += 1
                    material_blend_mode_counts[str(material.blend_mode)] += 1
                    material_depth_state_counts[
                        f"test={material.depth_test};write={material.depth_write}"
                    ] += 1

                archive_record["cmb_records"].append(
                    {
                        "embedded_name": file.name,
                        "size": len(data),
                        "model_name": model.name,
                        "environment_group": group,
                        "support_status": support_status,
                        "bone_count": model.bone_count,
                        "mesh_count": summary["mesh_count"],
                        "material_count": len(model.materials),
                        "texture_count": len(model.textures),
                        "primitive_count": summary["primitive_count"],
                        "triangle_count": summary["triangle_count"],
                        "vertex_count": summary["vertex_count"],
                    }
                )
            elif file.type_name == "cmab":
                cmab_sizes.append(len(data))
                cmab_magic_counts[format_magic(data[:4])] += 1
                declared_size = animation_declared_size("cmab", data)
                cmab_declared_size_match_counts[
                    declared_size_match_status(declared_size, len(data))
                ] += 1
                header_version = animation_header_version("cmab", data)
                cmab_header_version_counts[str(header_version)] += 1
                header_fields = animation_header_fields("cmab", data)
                for field, value in header_fields.items():
                    cmab_header_field_counts[field][str(value)] += 1
                layout = cmab_layout_metadata(data)
                cmab_layout_status_counts[str(layout["status"])] += 1
                cmab_mmad_count_counts[str(layout["mmad_count"])] += 1
                cmab_mads_record_count_counts[str(layout["mads_record_count"])] += 1
                frame_count = header_word(data, CMAB_FRAME_COUNT_CANDIDATE_OFFSET)
                loop_mode = header_word(data, CMAB_LOOP_MODE_CANDIDATE_OFFSET)
                cmab_frame_count_candidate_counts[str(frame_count)] += 1
                cmab_loop_mode_candidate_counts[str(loop_mode)] += 1
                archive_record["cmab_records"].append(
                    {
                        "embedded_name": file.name,
                        "size": len(data),
                        "magic": format_magic(data[:4]),
                        "declared_size": declared_size,
                        "declared_size_match_status": declared_size_match_status(
                            declared_size,
                            len(data),
                        ),
                        "header_version": header_version,
                        "frame_count_candidate": frame_count,
                        "loop_mode_candidate": loop_mode,
                        "layout_status": layout["status"],
                        "mmad_count": layout["mmad_count"],
                    }
                )
            elif file.type_name == "ctxb":
                ctxb_sizes.append(len(data))
                ctxb_magic_counts[format_magic(data[:4])] += 1
                archive_record["ctxb_records"].append(
                    {
                        "embedded_name": file.name,
                        "size": len(data),
                        "magic": format_magic(data[:4]),
                    }
                )
            elif file.type_name == "tbd":
                tbd_sizes.append(len(data))
                tbd_metadata_record = tbd_metadata(data)
                tbd_magic_counts[str(tbd_metadata_record["magic"])] += 1
                tbd_version_counts[str(tbd_metadata_record["version"])] += 1
                tbd_declared_size_match_counts[
                    str(tbd_metadata_record["declared_size_match_status"])
                ] += 1
                tbd_entry_count_counts[str(tbd_metadata_record["entry_count"])] += 1
                archive_record["tbd_records"].append(
                    {
                        "embedded_name": file.name,
                        "size": len(data),
                        **tbd_metadata_record,
                    }
                )

        if include_records:
            records.append(archive_record)
        if len(sample_records) < sample_limit:
            sample_records.append(archive_record)

    audit: dict[str, object] = {
        "format": "oot3d_kankyo_environment_audit_v1",
        "kankyo_root": str(kankyo_root),
        "archive_count": archive_count,
        "archive_size_total": archive_size_total,
        "archive_file_count_total": archive_file_count_total,
        "archive_file_count_counts": sorted_counter(archive_file_count_counts),
        "archive_parse_error_count": len(archive_parse_errors),
        "archive_parse_errors": archive_parse_errors,
        "embedded_type_counts": sorted_counter(embedded_type_counts),
        "cmb_counts": {
            "discovered": cmb_discovered,
            "parsed": cmb_parsed,
            "parse_errors": len(cmb_parse_errors),
        },
        "cmb_parse_errors": cmb_parse_errors,
        "cmb_support_counts": sorted_counter(cmb_support_counts),
        "cmb_bone_count_counts": sorted_counter(cmb_bone_count_counts),
        "cmb_mesh_count_counts": sorted_counter(cmb_mesh_count_counts),
        "cmb_material_count_counts": sorted_counter(cmb_material_count_counts),
        "cmb_texture_count_counts": sorted_counter(cmb_texture_count_counts),
        "cmb_skeleton_header_counts": sorted_counter(cmb_skeleton_header_counts),
        "cmb_skinning_mode_primitive_counts": sorted_counter(
            cmb_skinning_mode_primitive_counts
        ),
        "cmb_primitive_bone_count_counts": sorted_counter(cmb_primitive_bone_count_counts),
        "cmb_skinning_mode_bone_count_counts": sorted_counter(
            cmb_skinning_mode_bone_count_counts
        ),
        "cmb_triangle_total": cmb_triangle_total,
        "cmb_vertex_total": cmb_vertex_total,
        "cmb_primitive_total": cmb_primitive_total,
        "cmb_shape_total": cmb_shape_total,
        "environment_model_group_counts": sorted_counter(environment_model_group_counts),
        "material_alpha_test_counts": sorted_counter(material_alpha_test_counts),
        "material_blend_mode_counts": sorted_counter(material_blend_mode_counts),
        "material_depth_state_counts": sorted_counter(material_depth_state_counts),
        "cmab_count": len(cmab_sizes),
        "cmab_size_summary": size_summary(cmab_sizes),
        "cmab_magic_counts": sorted_counter(cmab_magic_counts),
        "cmab_declared_size_match_counts": sorted_counter(
            cmab_declared_size_match_counts
        ),
        "cmab_header_version_counts": sorted_counter(cmab_header_version_counts),
        "cmab_header_field_counts": sorted_nested_counter(cmab_header_field_counts),
        "cmab_layout_status_counts": sorted_counter(cmab_layout_status_counts),
        "cmab_mmad_count_counts": sorted_counter(cmab_mmad_count_counts),
        "cmab_mads_record_count_counts": sorted_counter(cmab_mads_record_count_counts),
        "cmab_frame_count_candidate_counts": sorted_counter(
            cmab_frame_count_candidate_counts
        ),
        "cmab_loop_mode_candidate_counts": sorted_counter(cmab_loop_mode_candidate_counts),
        "ctxb_count": len(ctxb_sizes),
        "ctxb_size_summary": size_summary(ctxb_sizes),
        "ctxb_magic_counts": sorted_counter(ctxb_magic_counts),
        "tbd_count": len(tbd_sizes),
        "tbd_size_summary": size_summary(tbd_sizes),
        "tbd_magic_counts": sorted_counter(tbd_magic_counts),
        "tbd_version_counts": sorted_counter(tbd_version_counts),
        "tbd_declared_size_match_counts": sorted_counter(
            tbd_declared_size_match_counts
        ),
        "tbd_entry_count_counts": sorted_counter(tbd_entry_count_counts),
        "sample_records": sample_records,
        "records": records if include_records else [],
    }

    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(
            json.dumps(audit, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
    return audit


def environment_model_group(name: str) -> str:
    normalized = name.replace("\\", "/").lower().rsplit("/", 1)[-1]
    if "tenkyu" in normalized:
        return "tenkyu_sky_dome"
    if "kumo" in normalized:
        return "kumo_cloud"
    if "sun" in normalized:
        return "sun"
    if "star" in normalized:
        return "star"
    return "other"


def tbd_metadata(data: bytes) -> dict[str, object]:
    declared_size = u32le_or_none(data, 8)
    return {
        "magic": format_magic(data[:4]),
        "version": u32le_or_none(data, 4),
        "declared_size": declared_size,
        "declared_size_match_status": (
            "matches" if declared_size == len(data) else "mismatch"
        ),
        "entry_count": u32le_or_none(data, 12),
        "first_32_bytes": data[:32].hex(" "),
    }


def u32le_or_none(data: bytes, offset: int) -> int | None:
    if len(data) < offset + 4:
        return None
    return int.from_bytes(data[offset : offset + 4], "little")
