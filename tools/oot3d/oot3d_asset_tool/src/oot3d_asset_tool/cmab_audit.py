from __future__ import annotations

import json
import math
import struct
from collections import Counter
from pathlib import Path

from .actor_inventory import (
    ANIMATION_HEADER_FIELD_OFFSETS,
    ANIMATION_LIKE_TYPES,
    KNOWN_ANIMATION_TYPES,
    animation_declared_size,
    animation_header_fields,
    animation_header_version,
    archive_support_status,
    cmb_candidate_stems,
    format_magic,
    model_support_status,
    normalized_asset_stem,
    sorted_counter,
    sorted_deep_nested_counter,
    stem_contained,
)
from .binary import ParseError
from .cmb import CmbModel
from .csab_target_audit import candidate_record, candidate_records
from .csab_tracks import is_cmb_file
from .zar import ZarArchive, ZarFile

CMAB_HEADER_MADS_OFFSET = 0x34
CMAB_STRING_TABLE_OFFSET_CANDIDATE = 0x18
CMAB_FRAME_COUNT_CANDIDATE_OFFSET = 0x24
CMAB_LOOP_MODE_CANDIDATE_OFFSET = 0x28
CMAB_TEXTURE_DATA_OFFSET_CANDIDATE = 0x1C
CMAB_TEXTURE_PAYLOAD_OFFSET_CANDIDATE = 0x30
CMAB_MMAD_HEADER_SIZE = 0x24
CMAB_MMAD_SCALAR_KEYFRAME_SIZE = 0x08
CMAB_SOURCE_CURVE_HEADER_SIZE = 0x10
CMAB_SOURCE_CURVE_LINEAR_POINT_SIZE = 0x08
CMAB_SOURCE_CURVE_HERMITE_POINT_SIZE = 0x10
CMAB_TXPT_RECORD_SIZE = 0x18
CMAB_MADS_MAGIC = b"mads"
CMAB_MMAD_MAGIC = b"mmad"
CMAB_STRT_MAGIC = b"strt"
CMAB_TXPT_MAGIC = b"txpt"


def audit_cmab_payloads(
    actor_root: Path,
    output_path: Path | None = None,
    *,
    sample_limit: int = 100,
    include_records: bool = True,
) -> dict[str, object]:
    if not actor_root.is_dir():
        raise ParseError(f"{actor_root}: expected an extracted OOT3D actor directory")

    archive_count = 0
    archives_with_cmab = 0
    cmab_count = 0
    total_mmad_count = 0

    archive_parse_errors: list[dict[str, object]] = []
    cmb_parse_errors: list[dict[str, object]] = []
    records: list[dict[str, object]] = []
    sample_records: list[dict[str, object]] = []
    unresolved_target_samples: list[dict[str, object]] = []

    archive_cmab_counts: Counter[str] = Counter()
    archive_cmab_count_counts: Counter[str] = Counter()
    archive_support_counts: Counter[str] = Counter()
    cmab_by_cmb_count_counts: Counter[str] = Counter()
    size_values: list[int] = []
    magic_counts: Counter[str] = Counter()
    declared_size_match_counts: Counter[str] = Counter()
    header_version_counts: Counter[str] = Counter()
    header_field_counts: dict[str, Counter[str]] = {
        f"word_{offset:02x}": Counter()
        for offset in ANIMATION_HEADER_FIELD_OFFSETS["cmab"]
    }
    layout_status_counts: Counter[str] = Counter()
    mads_header_marker_counts: Counter[str] = Counter()
    mads_record_count_counts: Counter[str] = Counter()
    mads_stride_counts: Counter[str] = Counter()
    mads_record_count_matches_mmad_count_counts: Counter[str] = Counter()
    first_mmad_offset_matches_mads_stride_counts: Counter[str] = Counter()
    mads_record_offsets_match_mmad_offsets_counts: Counter[str] = Counter()
    string_table_marker_counts: Counter[str] = Counter()
    string_table_offset_counts: Counter[str] = Counter()
    mmads_per_payload_counts: Counter[str] = Counter()
    mmads_native_type_counts: Counter[str] = Counter()
    mmads_native_type_status_counts: Counter[str] = Counter()
    mmads_native_target_material_counts: Counter[str] = Counter()
    mmads_native_target_component_or_stage_counts: Counter[str] = Counter()
    mmads_native_target_selector_status_counts: Counter[str] = Counter()
    mmads_native_target_selector_counts: Counter[str] = Counter()
    mmads_native_value_kind_counts: Counter[str] = Counter()
    mmads_native_channel_offset_count_counts: Counter[str] = Counter()
    mmads_native_channel_offset_status_counts: Counter[str] = Counter()
    total_native_channel_offset_count = 0
    total_native_channel_offset_present_count = 0
    mmads_native_source_curve_count_counts: Counter[str] = Counter()
    mmads_native_source_curve_status_counts: Counter[str] = Counter()
    mmads_native_source_curve_type_counts: Counter[str] = Counter()
    total_native_source_curve_count = 0
    total_native_source_curve_decoded_count = 0
    total_native_source_curve_point_count = 0
    mmads_scalar_track_status_counts: Counter[str] = Counter()
    mmads_keyframe_count_counts: Counter[str] = Counter()
    mmads_component_scalar_track_count_counts: Counter[str] = Counter()
    mmads_component_scalar_track_status_counts: Counter[str] = Counter()
    total_component_scalar_track_count = 0
    total_component_scalar_keyframe_count = 0
    txpt_marker_status_counts: Counter[str] = Counter()
    txpt_texture_count_counts: Counter[str] = Counter()
    txpt_texture_format_counts: Counter[str] = Counter()
    txpt_texture_data_type_counts: Counter[str] = Counter()
    total_txpt_texture_count = 0
    frame_count_candidate_counts: Counter[str] = Counter()
    loop_mode_candidate_counts: Counter[str] = Counter()
    target_resolution_counts: Counter[str] = Counter()
    target_support_counts: Counter[str] = Counter()

    for zar_path in sorted(actor_root.rglob("*.zar")):
        archive_count += 1
        archive_rel = zar_path.relative_to(actor_root).as_posix()
        try:
            archive = ZarArchive.from_path(zar_path)
        except Exception as exc:
            archive_parse_errors.append(
                {
                    "archive_path": archive_rel,
                    "reason": str(exc),
                }
            )
            continue

        cmab_files = [file for file in archive.files if is_cmab_file(file)]
        if cmab_files:
            archives_with_cmab += 1
            archive_cmab_counts[archive_rel] = len(cmab_files)
            archive_cmab_count_counts[str(len(cmab_files))] += 1

        cmb_records = parsed_cmb_records(
            archive,
            zar_path,
            archive_rel,
            cmb_parse_errors,
        )
        archive_status = cmab_archive_support_status(archive, cmb_records)

        for cmab_file in cmab_files:
            data = archive.read_file(cmab_file)
            cmab_count += 1
            size_values.append(len(data))
            archive_support_counts[archive_status] += 1
            cmab_by_cmb_count_counts[str(len(cmb_records))] += 1

            magic = format_magic(data[:4])
            magic_counts[magic] += 1
            declared_size = animation_declared_size("cmab", data)
            declared_size_match_counts[declared_size_match_status(declared_size, len(data))] += 1
            header_version = animation_header_version("cmab", data)
            if header_version is not None:
                header_version_counts[str(header_version)] += 1
            header_fields = animation_header_fields("cmab", data)
            for field_name, value in header_fields.items():
                header_field_counts[field_name][str(value)] += 1

            layout = cmab_layout_metadata(data)
            cmab_texture_names = tuple(
                str(name) for name in layout.get("string_table_names", [])
            )
            layout_status_counts[str(layout["status"])] += 1
            mads_header_marker_counts[str(layout["mads_header_marker_status"])] += 1
            if layout.get("mads_record_count") is not None:
                mads_record_count_counts[str(layout["mads_record_count"])] += 1
            if layout.get("mads_stride") is not None:
                mads_stride_counts[str(layout["mads_stride"])] += 1
            mads_record_count_matches_mmad_count_counts[
                yes_no(layout["mads_record_count_matches_mmad_count"])
            ] += 1
            first_mmad_offset_matches_mads_stride_counts[
                yes_no(layout["first_mmad_offset_matches_mads_stride"])
            ] += 1
            mads_record_offsets_match_mmad_offsets_counts[
                yes_no(layout["mads_record_offsets_match_mmad_offsets"])
            ] += 1
            string_table_marker_counts[str(layout["string_table_marker_status"])] += 1
            if layout.get("string_table_offset_candidate") is not None:
                string_table_offset_counts[
                    str(layout["string_table_offset_candidate"])
                ] += 1
            mmads_per_payload_counts[str(layout["mmad_count"])] += 1
            total_mmad_count += int(layout["mmad_count"])
            for mmads_record in layout.get("mmad_records", []):
                if not isinstance(mmads_record, dict):
                    continue
                scalar_status = (
                    "scalar_keyframes_decoded"
                    if mmads_record.get("scalar_keyframes_decoded")
                    else "raw_or_non_scalar_payload"
                )
                mmads_scalar_track_status_counts[scalar_status] += 1
                if mmads_record.get("keyframe_count_candidate") is not None:
                    mmads_keyframe_count_counts[
                        str(mmads_record["keyframe_count_candidate"])
                    ] += 1
                if mmads_record.get("native_type") is not None:
                    mmads_native_type_counts[str(mmads_record["native_type"])] += 1
                if mmads_record.get("native_type_status") is not None:
                    mmads_native_type_status_counts[
                        str(mmads_record["native_type_status"])
                    ] += 1
                if mmads_record.get("target_material_index") is not None:
                    mmads_native_target_material_counts[
                        str(mmads_record["target_material_index"])
                    ] += 1
                if mmads_record.get("target_component_or_stage_index") is not None:
                    mmads_native_target_component_or_stage_counts[
                        str(mmads_record["target_component_or_stage_index"])
                    ] += 1
                selector_status = str(
                    mmads_record.get("target_selector_status", "selector_absent")
                )
                mmads_native_target_selector_status_counts[selector_status] += 1
                if mmads_record.get("target_selector") is not None:
                    mmads_native_target_selector_counts[
                        str(mmads_record["target_selector"])
                    ] += 1
                if mmads_record.get("native_value_kind") is not None:
                    mmads_native_value_kind_counts[
                        str(mmads_record["native_value_kind"])
                    ] += 1
                native_channel_offsets = mmads_record.get("native_channel_offsets", [])
                if isinstance(native_channel_offsets, list):
                    total_native_channel_offset_count += len(native_channel_offsets)
                    mmads_native_channel_offset_count_counts[
                        str(len(native_channel_offsets))
                    ] += 1
                    for channel_offset in native_channel_offsets:
                        if not isinstance(channel_offset, dict):
                            continue
                        channel_status = (
                            "native_channel_offset_present"
                            if channel_offset.get("present")
                            else "native_channel_offset_absent"
                        )
                        mmads_native_channel_offset_status_counts[channel_status] += 1
                        if channel_offset.get("present"):
                            total_native_channel_offset_present_count += 1
                native_source_curves = mmads_record.get("native_source_curves", [])
                if isinstance(native_source_curves, list):
                    total_native_source_curve_count += len(native_source_curves)
                    mmads_native_source_curve_count_counts[
                        str(len(native_source_curves))
                    ] += 1
                    for source_curve in native_source_curves:
                        if not isinstance(source_curve, dict):
                            continue
                        curve_status = str(source_curve.get("status", "unknown"))
                        mmads_native_source_curve_status_counts[curve_status] += 1
                        curve_type = (
                            str(source_curve["type"])
                            if source_curve.get("decoded")
                            and source_curve.get("type") is not None
                            else "undecoded"
                        )
                        mmads_native_source_curve_type_counts[curve_type] += 1
                        if source_curve.get("decoded"):
                            total_native_source_curve_decoded_count += 1
                            points = source_curve.get("points", [])
                            if isinstance(points, list):
                                total_native_source_curve_point_count += len(points)
                component_tracks = mmads_record.get("component_scalar_tracks", [])
                if isinstance(component_tracks, list):
                    total_component_scalar_track_count += len(component_tracks)
                    mmads_component_scalar_track_count_counts[
                        str(len(component_tracks))
                    ] += 1
                    for component_track in component_tracks:
                        if not isinstance(component_track, dict):
                            continue
                        component_status = (
                            "component_scalar_keyframes_decoded"
                            if component_track.get("keyframes_decoded")
                            else "component_scalar_raw_or_bounds_mismatch"
                        )
                        mmads_component_scalar_track_status_counts[component_status] += 1
                        if component_track.get("keyframe_sample_count") is not None:
                            total_component_scalar_keyframe_count += int(
                                component_track.get("keyframe_count_candidate") or 0
                            )
            txpt_marker_status_counts[str(layout["txpt_marker_status"])] += 1
            txpt_texture_records = layout.get("txpt_texture_records", [])
            if isinstance(txpt_texture_records, list):
                txpt_texture_count_counts[str(len(txpt_texture_records))] += 1
                total_txpt_texture_count += len(txpt_texture_records)
                for texture_record in txpt_texture_records:
                    if not isinstance(texture_record, dict):
                        continue
                    txpt_texture_format_counts[
                        f"0x{int(texture_record.get('texture_format', 0)):04x}"
                    ] += 1
                    txpt_texture_data_type_counts[
                        f"0x{int(texture_record.get('data_type', 0)):04x}"
                    ] += 1

            frame_count = header_word(data, CMAB_FRAME_COUNT_CANDIDATE_OFFSET)
            loop_mode = header_word(data, CMAB_LOOP_MODE_CANDIDATE_OFFSET)
            if frame_count is not None:
                frame_count_candidate_counts[str(frame_count)] += 1
            if loop_mode is not None:
                loop_mode_candidate_counts[str(loop_mode)] += 1

            target_status, target_candidates = resolve_cmab_target(
                cmab_file.name,
                cmb_records,
                cmab_texture_names,
            )
            target_resolution_counts[target_status] += 1
            if len(target_candidates) == 1:
                target_support_counts[str(target_candidates[0]["support_status"])] += 1

            record = cmab_record(
                archive_rel,
                cmab_file,
                len(data),
                magic,
                declared_size,
                header_version,
                header_fields,
                layout,
                archive_status,
                len(cmb_records),
                target_status,
                target_candidates,
            )
            if include_records:
                records.append(record)
            if len(sample_records) < sample_limit:
                sample_records.append(record)
            if len(target_candidates) != 1 and len(unresolved_target_samples) < sample_limit:
                unresolved_target_samples.append(
                    unresolved_target_record(
                        archive_rel,
                        cmab_file.name,
                        cmb_records,
                        target_status,
                        cmab_texture_names,
                    )
                )

    resolved_target_count = sum(
        count
        for status, count in target_resolution_counts.items()
        if status
        in {
            "single_exact_name_match",
            "single_contained_name_match",
            "single_texture_name_match",
            "single_cmb_archive_fallback",
        }
    )
    audit: dict[str, object] = {
        "format": "oot3d_cmab_payload_audit_v2",
        "actor_root": str(actor_root),
        "archive_count": archive_count,
        "archives_with_cmab": archives_with_cmab,
        "cmab_count": cmab_count,
        "total_mmad_count": total_mmad_count,
        "archive_parse_error_count": len(archive_parse_errors),
        "cmb_parse_error_count": len(cmb_parse_errors),
        "cmab_size_summary": size_summary(size_values),
        "magic_counts": sorted_counter(magic_counts),
        "declared_size_match_counts": sorted_counter(declared_size_match_counts),
        "header_version_counts": sorted_counter(header_version_counts),
        "header_field_counts": sorted_deep_nested_counter({"cmab": header_field_counts})[
            "cmab"
        ],
        "layout_status_counts": sorted_counter(layout_status_counts),
        "mads_header_marker_counts": sorted_counter(mads_header_marker_counts),
        "mads_record_count_counts": sorted_counter(mads_record_count_counts),
        "mads_stride_counts": sorted_counter(mads_stride_counts),
        "mads_record_count_matches_mmad_count_counts": sorted_counter(
            mads_record_count_matches_mmad_count_counts
        ),
        "first_mmad_offset_matches_mads_stride_counts": sorted_counter(
            first_mmad_offset_matches_mads_stride_counts
        ),
        "mads_record_offsets_match_mmad_offsets_counts": sorted_counter(
            mads_record_offsets_match_mmad_offsets_counts
        ),
        "string_table_marker_counts": sorted_counter(string_table_marker_counts),
        "string_table_offset_counts": sorted_counter(string_table_offset_counts),
        "mmads_per_payload_counts": sorted_counter(mmads_per_payload_counts),
        "mmads_native_type_counts": sorted_counter(mmads_native_type_counts),
        "mmads_native_type_status_counts": sorted_counter(
            mmads_native_type_status_counts
        ),
        "mmads_native_target_material_counts": sorted_counter(
            mmads_native_target_material_counts
        ),
        "mmads_native_target_component_or_stage_counts": sorted_counter(
            mmads_native_target_component_or_stage_counts
        ),
        "mmads_native_target_selector_status_counts": sorted_counter(
            mmads_native_target_selector_status_counts
        ),
        "mmads_native_target_selector_counts": sorted_counter(
            mmads_native_target_selector_counts
        ),
        "mmads_native_value_kind_counts": sorted_counter(mmads_native_value_kind_counts),
        "mmads_native_channel_offset_count_counts": sorted_counter(
            mmads_native_channel_offset_count_counts
        ),
        "mmads_native_channel_offset_status_counts": sorted_counter(
            mmads_native_channel_offset_status_counts
        ),
        "total_native_channel_offset_count": total_native_channel_offset_count,
        "total_native_channel_offset_present_count": total_native_channel_offset_present_count,
        "mmads_native_source_curve_count_counts": sorted_counter(
            mmads_native_source_curve_count_counts
        ),
        "mmads_native_source_curve_status_counts": sorted_counter(
            mmads_native_source_curve_status_counts
        ),
        "mmads_native_source_curve_type_counts": sorted_counter(
            mmads_native_source_curve_type_counts
        ),
        "total_native_source_curve_count": total_native_source_curve_count,
        "total_native_source_curve_decoded_count": total_native_source_curve_decoded_count,
        "total_native_source_curve_point_count": total_native_source_curve_point_count,
        "mmads_scalar_track_status_counts": sorted_counter(mmads_scalar_track_status_counts),
        "mmads_keyframe_count_counts": sorted_counter(mmads_keyframe_count_counts),
        "mmads_component_scalar_track_count_counts": sorted_counter(
            mmads_component_scalar_track_count_counts
        ),
        "mmads_component_scalar_track_status_counts": sorted_counter(
            mmads_component_scalar_track_status_counts
        ),
        "total_component_scalar_track_count": total_component_scalar_track_count,
        "total_component_scalar_keyframe_count": total_component_scalar_keyframe_count,
        "txpt_marker_status_counts": sorted_counter(txpt_marker_status_counts),
        "txpt_texture_count_counts": sorted_counter(txpt_texture_count_counts),
        "txpt_texture_format_counts": sorted_counter(txpt_texture_format_counts),
        "txpt_texture_data_type_counts": sorted_counter(txpt_texture_data_type_counts),
        "total_txpt_texture_count": total_txpt_texture_count,
        "frame_count_candidate_counts": sorted_counter(frame_count_candidate_counts),
        "loop_mode_candidate_counts": sorted_counter(loop_mode_candidate_counts),
        "archive_support_counts": sorted_counter(archive_support_counts),
        "archive_cmab_count_counts": sorted_counter(archive_cmab_count_counts),
        "archive_cmab_counts": sorted_counter(archive_cmab_counts),
        "cmab_by_cmb_count_counts": sorted_counter(cmab_by_cmb_count_counts),
        "target_resolution_counts": sorted_counter(target_resolution_counts),
        "target_resolved_count": resolved_target_count,
        "target_unresolved_or_ambiguous_count": cmab_count - resolved_target_count,
        "target_support_counts": sorted_counter(target_support_counts),
        "archive_parse_errors": archive_parse_errors,
        "cmb_parse_errors": cmb_parse_errors,
        "sample_records": sample_records,
        "unresolved_target_samples": unresolved_target_samples,
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


def parsed_cmb_records(
    archive: ZarArchive,
    zar_path: Path,
    archive_rel: str,
    cmb_parse_errors: list[dict[str, object]],
) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    for file in archive.files:
        if not is_cmb_file(file):
            continue
        try:
            model = CmbModel.parse(archive.read_file(file), f"{zar_path}!{file.name}")
        except Exception as exc:
            cmb_parse_errors.append(
                {
                    "archive_path": archive_rel,
                    "embedded_name": file.name,
                    "reason": str(exc),
                }
            )
            continue
        records.append(
            {
                "embedded_name": file.name,
                "model_name": model.name,
                "bone_count": model.bone_count,
                "texture_names": sorted(texture.name for texture in model.textures),
                "material_texture_names": sorted(
                    {
                        model.textures[index].name
                        for material in model.materials
                        for index in material.texture_indices
                        if 0 <= index < len(model.textures)
                    }
                ),
                "support_status": model_support_status(model),
            }
        )
    return records


def cmab_archive_support_status(
    archive: ZarArchive,
    cmb_records: list[dict[str, object]],
) -> str:
    known_animation_count = sum(
        1
        for file in archive.files
        if file.type_name in KNOWN_ANIMATION_TYPES
        or file.name.rsplit(".", 1)[-1].lower() in KNOWN_ANIMATION_TYPES
    )
    animation_like_count = sum(
        1
        for file in archive.files
        if file.type_name in ANIMATION_LIKE_TYPES
        or file.name.rsplit(".", 1)[-1].lower() in ANIMATION_LIKE_TYPES
    )
    nonstatic_cmb_count = sum(
        1
        for record in cmb_records
        if record["support_status"] != "static_cmb_export_supported"
    )
    return archive_support_status(
        len(cmb_records),
        nonstatic_cmb_count,
        known_animation_count,
        animation_like_count,
    )


def cmab_mads_record_offsets(
    data: bytes,
    mads_record_count: int | None,
) -> list[int]:
    if mads_record_count is None or mads_record_count <= 0:
        return []

    table_offset = CMAB_HEADER_MADS_OFFSET + 8
    table_size = int(mads_record_count) * 4
    if table_offset + table_size > len(data):
        return []

    offsets: list[int] = []
    for record_index in range(int(mads_record_count)):
        relative_offset = header_word(data, table_offset + record_index * 4)
        if relative_offset is None:
            return []
        offsets.append(CMAB_HEADER_MADS_OFFSET + int(relative_offset))
    return offsets


def cmab_layout_metadata(data: bytes) -> dict[str, object]:
    mads_offsets = tag_offsets(data, CMAB_MADS_MAGIC)
    mmad_offsets = tag_offsets(data, CMAB_MMAD_MAGIC)
    strt_offsets = tag_offsets(data, CMAB_STRT_MAGIC)
    txpt_offsets = tag_offsets(data, CMAB_TXPT_MAGIC)
    mads_header_marker = (
        data[CMAB_HEADER_MADS_OFFSET : CMAB_HEADER_MADS_OFFSET + 4]
        if len(data) >= CMAB_HEADER_MADS_OFFSET + 4
        else b""
    )
    mads_header_marker_status = (
        "mads_at_0x34" if mads_header_marker == CMAB_MADS_MAGIC else "missing_mads_at_0x34"
    )
    mads_record_count = header_word(data, CMAB_HEADER_MADS_OFFSET + 4)
    mads_stride = header_word(data, CMAB_HEADER_MADS_OFFSET + 8)
    mads_record_offsets = cmab_mads_record_offsets(data, mads_record_count)
    mads_record_offsets_match_mmad_offsets = mads_record_offsets == mmad_offsets
    first_mmad_offset = mmad_offsets[0] if mmad_offsets else None
    expected_first_mmad_offset = (
        CMAB_HEADER_MADS_OFFSET + mads_stride if mads_stride is not None else None
    )
    mads_record_count_matches_mmad_count = (
        mads_record_count == len(mmad_offsets) if mads_record_count is not None else False
    )
    first_mmad_offset_matches_mads_stride = (
        first_mmad_offset == expected_first_mmad_offset
        if expected_first_mmad_offset is not None
        else False
    )

    string_table_offset_candidate = header_word(data, CMAB_STRING_TABLE_OFFSET_CANDIDATE)
    texture_data_offset_candidate = header_word(data, CMAB_TEXTURE_DATA_OFFSET_CANDIDATE)
    txpt_offset_candidate = header_word(data, CMAB_TEXTURE_PAYLOAD_OFFSET_CANDIDATE)
    string_table_names = cmab_string_table_names(data, string_table_offset_candidate)
    string_table_marker_status = string_table_marker_candidate_status(
        data,
        string_table_offset_candidate,
    )
    txpt_offset_candidate_marker_status = txpt_marker_candidate_status(
        data,
        txpt_offset_candidate,
    )
    txpt_offset, txpt_offset_resolution_status = resolved_txpt_offset(
        data,
        txpt_offset_candidate,
        txpt_offsets,
    )
    txpt_marker_status = (
        "points_to_txpt" if txpt_offset is not None else txpt_offset_candidate_marker_status
    )
    txpt_texture_records = cmab_txpt_texture_records(
        data,
        txpt_offset,
        texture_data_offset_candidate,
        string_table_names,
    )
    mmads_record_offsets = (
        mads_record_offsets if mads_record_offsets_match_mmad_offsets else tag_offsets(data, CMAB_MMAD_MAGIC)
    )
    mmads_records = cmab_mmad_records(
        data,
        mmads_record_offsets,
        txpt_offset,
        string_table_offset_candidate,
        mads_record_offsets,
    )

    status = "markers_consistent"
    if mads_header_marker_status != "mads_at_0x34":
        status = "missing_header_mads"
    elif not mads_record_count_matches_mmad_count:
        status = "mads_mmad_count_mismatch"
    elif not first_mmad_offset_matches_mads_stride:
        status = "first_mmad_stride_mismatch"
    elif string_table_marker_status != "points_to_strt":
        status = "string_table_marker_mismatch"

    return {
        "status": status,
        "mads_offsets": mads_offsets,
        "mads_header_marker_status": mads_header_marker_status,
        "mads_record_count": mads_record_count,
        "mads_stride": mads_stride,
        "mads_record_offsets": mads_record_offsets,
        "mads_record_offsets_match_mmad_offsets": mads_record_offsets_match_mmad_offsets,
        "mmad_offsets": mmad_offsets,
        "mmad_count": len(mmad_offsets),
        "mads_record_count_matches_mmad_count": mads_record_count_matches_mmad_count,
        "expected_first_mmad_offset": expected_first_mmad_offset,
        "first_mmad_offset": first_mmad_offset,
        "first_mmad_offset_matches_mads_stride": first_mmad_offset_matches_mads_stride,
        "mmad_records": mmads_records,
        "string_table_offsets": strt_offsets,
        "string_table_offset_candidate": string_table_offset_candidate,
        "string_table_marker_status": string_table_marker_status,
        "string_table_name_count": len(string_table_names),
        "string_table_names": string_table_names,
        "texture_data_offset_candidate": texture_data_offset_candidate,
        "txpt_offsets": txpt_offsets,
        "txpt_offset_candidate": txpt_offset_candidate,
        "txpt_offset_candidate_marker_status": txpt_offset_candidate_marker_status,
        "txpt_offset": txpt_offset,
        "txpt_offset_resolution_status": txpt_offset_resolution_status,
        "txpt_marker_status": txpt_marker_status,
        "txpt_texture_record_count": len(txpt_texture_records),
        "txpt_texture_records": txpt_texture_records,
    }


def resolve_cmab_target(
    cmab_name: str,
    cmb_records: list[dict[str, object]],
    cmab_texture_names: tuple[str, ...] = (),
) -> tuple[str, list[dict[str, object]]]:
    if not cmb_records:
        return "no_parsed_cmb_model", []

    animation_stem = normalized_asset_stem(cmab_name)
    exact_matches = [
        record
        for record in cmb_records
        if animation_stem in cmb_candidate_stems(record)
    ]
    if len(exact_matches) == 1:
        return "single_exact_name_match", exact_matches
    if len(exact_matches) > 1:
        return "multiple_exact_name_matches", []

    contained_matches = [
        record
        for record in cmb_records
        if stem_contained(animation_stem, cmb_candidate_stems(record))
    ]
    if len(contained_matches) == 1:
        return "single_contained_name_match", contained_matches
    if len(contained_matches) > 1:
        return "multiple_contained_name_matches", []

    texture_matches = cmab_texture_target_matches(cmab_texture_names, cmb_records)
    if len(texture_matches) == 1:
        return "single_texture_name_match", [texture_matches[0]["record"]]
    if len(texture_matches) > 1:
        return "multiple_texture_name_matches", []

    if len(cmb_records) == 1:
        return "single_cmb_archive_fallback", cmb_records
    return "multiple_cmb_unresolved", []


def unresolved_target_record(
    archive_path: str,
    cmab_name: str,
    cmb_records: list[dict[str, object]],
    target_status: str,
    cmab_texture_names: tuple[str, ...] = (),
) -> dict[str, object]:
    animation_stem = normalized_asset_stem(cmab_name)
    exact_matches = [
        record
        for record in cmb_records
        if animation_stem in cmb_candidate_stems(record)
    ]
    contained_matches = [
        record
        for record in cmb_records
        if stem_contained(animation_stem, cmb_candidate_stems(record))
    ]
    texture_matches = cmab_texture_target_matches(cmab_texture_names, cmb_records)
    return {
        "archive_path": archive_path,
        "cmab_name": cmab_name,
        "animation_stem": animation_stem,
        "target_resolution_status": target_status,
        "cmb_count": len(cmb_records),
        "cmab_texture_names": list(cmab_texture_names),
        "exact_candidate_count": len(exact_matches),
        "contained_candidate_count": len(contained_matches),
        "texture_candidate_count": len(texture_matches),
        "exact_candidates": candidate_records(exact_matches),
        "contained_candidates": candidate_records(contained_matches),
        "texture_candidates": [
            texture_match_candidate_record(match) for match in texture_matches
        ],
    }


def cmab_record(
    archive_path: str,
    cmab_file: ZarFile,
    size: int,
    magic: str,
    declared_size: int | None,
    header_version: int | None,
    header_fields: dict[str, int],
    layout: dict[str, object],
    archive_support: str,
    parsed_cmb_count: int,
    target_status: str,
    target_candidates: list[dict[str, object]],
) -> dict[str, object]:
    record: dict[str, object] = {
        "archive_path": archive_path,
        "cmab_name": cmab_file.name,
        "cmab_index": cmab_file.index,
        "size": size,
        "magic": magic,
        "declared_size": declared_size,
        "declared_size_match_status": declared_size_match_status(declared_size, size),
        "header_version": header_version,
        "header_fields": header_fields,
        "frame_count_candidate": header_fields.get("word_24"),
        "loop_mode_candidate": header_fields.get("word_28"),
        "layout": layout,
        "archive_support_status": archive_support,
        "parsed_cmb_count": parsed_cmb_count,
        "target_resolution_status": target_status,
        "target_candidate_count": len(target_candidates),
    }
    if len(target_candidates) == 1:
        record["target_cmb"] = candidate_record(target_candidates[0])
    return record


def is_cmab_file(file: ZarFile) -> bool:
    return file.type_name == "cmab" or file.name.lower().endswith(".cmab")


def tag_offsets(data: bytes, tag: bytes) -> list[int]:
    if len(tag) != 4:
        raise ValueError("CMAB tag scans expect 4-byte tags")
    return [
        offset
        for offset in range(0, max(0, len(data) - 3))
        if data[offset : offset + 4] == tag
    ]


def header_word(data: bytes, offset: int) -> int | None:
    if len(data) < offset + 4:
        return None
    return int.from_bytes(data[offset : offset + 4], "little")


def string_table_marker_candidate_status(data: bytes, offset: int | None) -> str:
    if offset is None:
        return "missing_offset_candidate"
    if offset + 4 > len(data):
        return "offset_out_of_bounds"
    if data[offset : offset + 4] == CMAB_STRT_MAGIC:
        return "points_to_strt"
    return "does_not_point_to_strt"


def txpt_marker_candidate_status(data: bytes, offset: int | None) -> str:
    if offset is None or offset == 0:
        return "no_txpt_offset"
    if offset + 4 > len(data):
        return "offset_out_of_bounds"
    if data[offset : offset + 4] == CMAB_TXPT_MAGIC:
        return "points_to_txpt"
    return "does_not_point_to_txpt"


def resolved_txpt_offset(
    data: bytes,
    candidate_offset: int | None,
    marker_offsets: list[int],
) -> tuple[int | None, str]:
    if txpt_marker_candidate_status(data, candidate_offset) == "points_to_txpt":
        return candidate_offset, "candidate_points_to_txpt"
    if marker_offsets:
        return marker_offsets[0], "scanned_txpt_marker"
    return None, "no_txpt_marker"


def cmab_string_table_names(data: bytes, offset: int | None) -> list[str]:
    if offset is None or offset + 8 > len(data):
        return []
    if data[offset : offset + 4] != CMAB_STRT_MAGIC:
        return []
    string_count = header_word(data, offset + 4)
    if string_count is None:
        return []
    offsets_start = offset + 8
    offsets_end = offsets_start + string_count * 4
    if string_count < 0 or offsets_end > len(data):
        return []
    string_data_start = offsets_end
    names: list[str] = []
    for index in range(string_count):
        relative_offset = header_word(data, offsets_start + index * 4)
        if relative_offset is None:
            return []
        string_offset = string_data_start + relative_offset
        if string_offset >= len(data):
            return []
        end = data.find(b"\x00", string_offset)
        if end < 0:
            return []
        try:
            names.append(data[string_offset:end].decode("ascii"))
        except UnicodeDecodeError:
            return []
    return names


def f32_word(data: bytes, offset: int) -> float | None:
    if offset + 4 > len(data):
        return None
    return struct.unpack_from("<f", data, offset)[0]


def s16_word(data: bytes, offset: int) -> int | None:
    if offset + 2 > len(data):
        return None
    return struct.unpack_from("<h", data, offset)[0]


def s32_word(data: bytes, offset: int) -> int | None:
    if offset + 4 > len(data):
        return None
    return struct.unpack_from("<i", data, offset)[0]


def cmab_native_value_kind(native_type: int | None) -> str | None:
    return {
        1: "transform_vec2_gate_1_plus_selector",
        2: "texture_frame_int_gate_7_plus_stage",
        3: "material_color_vec4_gate_0",
        4: "constant_color_vec4_gate_0a_plus_selector",
        5: "transform_scalar_gate_4_plus_selector",
    }.get(native_type)


def cmab_native_channel_offset_table_offset(native_type: int | None) -> int:
    if native_type in {1, 2, 4, 5}:
        return 0x10
    if native_type == 3:
        return 0x0C
    return 0


def cmab_native_channel_offset_count(native_type: int | None) -> int:
    if native_type == 1:
        return 2
    if native_type in {2, 5}:
        return 1
    if native_type in {3, 4}:
        return 4
    return 0


def cmab_native_target_selector_present(native_type: int | None) -> bool:
    return native_type in {1, 2, 4, 5}


def cmab_native_channel_offsets(
    data: bytes,
    record_offset: int,
    record_size: int,
    native_type: int | None,
) -> list[dict[str, object]]:
    table_offset = cmab_native_channel_offset_table_offset(native_type)
    channel_count = cmab_native_channel_offset_count(native_type)
    if table_offset == 0 or channel_count == 0:
        return []

    offsets: list[dict[str, object]] = []
    for component_index in range(channel_count):
        entry_offset = table_offset + component_index * 2
        if entry_offset + 2 > record_size:
            break
        relative_offset = s16_word(data, record_offset + entry_offset)
        if relative_offset is None:
            break
        offsets.append(
            {
                "component_index": component_index,
                "table_offset": entry_offset,
                "relative_offset": relative_offset,
                "present": relative_offset != 0,
            }
        )
    return offsets


def cmab_source_curve_point_stride(curve_type: int | None) -> int:
    if curve_type in {1, 3}:
        return CMAB_SOURCE_CURVE_LINEAR_POINT_SIZE
    if curve_type == 2:
        return CMAB_SOURCE_CURVE_HERMITE_POINT_SIZE
    return 0


def cmab_source_curve_points_leq(points: list[dict[str, object]], frame: float) -> int:
    count = 0
    while count < len(points) and float(points[count].get("frame", 0)) <= frame:
        count += 1
    return count


def cmab_sample_hermite_segment(
    previous: dict[str, object],
    current: dict[str, object],
    previous_frame: float,
    current_frame: float,
    frame: float,
) -> float:
    delta_x = current_frame - previous_frame
    if delta_x <= 0.0:
        return float(current.get("value", 0.0))
    x_from_previous = frame - previous_frame
    t = x_from_previous / delta_x
    t_minus_one = t - 1.0
    value_delta_term = (
        (float(current.get("value", 0.0)) - float(previous.get("value", 0.0)))
        * (3.0 - 2.0 * t)
        * t
        * t
    )
    tangent_term = x_from_previous * t_minus_one * (
        t_minus_one * float(previous.get("tangent_out", 0.0))
        + t * float(current.get("tangent_in", 0.0))
    )
    return float(previous.get("value", 0.0)) + value_delta_term + tangent_term


def cmab_sample_source_curve(curve: dict[str, object], frame: float) -> float:
    if not curve.get("decoded") or not math.isfinite(frame):
        return 0.0
    points = curve.get("points", [])
    if not isinstance(points, list) or not points:
        return 0.0
    if len(points) == 1:
        return float(points[0].get("value", 0.0))

    curve_type = int(curve.get("type", 0))
    if curve_type == 1:
        upper = cmab_source_curve_points_leq(points, frame)
        if upper == 0:
            return float(points[0].get("value", 0.0))
        if upper >= len(points):
            return float(points[-1].get("value", 0.0))
        previous = points[upper - 1]
        current = points[upper]
        previous_frame = float(previous.get("frame", 0))
        current_frame = float(current.get("frame", 0))
        delta_x = current_frame - previous_frame
        if delta_x <= 0.0:
            return float(current.get("value", 0.0))
        t = (frame - previous_frame) / delta_x
        return float(previous.get("value", 0.0)) + (
            float(current.get("value", 0.0)) - float(previous.get("value", 0.0))
        ) * t

    if curve_type == 3:
        upper = cmab_source_curve_points_leq(points, frame)
        if upper == 0:
            return float(points[0].get("value", 0.0))
        return float(points[upper - 1].get("value", 0.0))

    if curve_type == 2:
        sample_frame = frame
        loop_end = float(curve.get("header_word_0c", 0))
        if curve.get("wrap_enabled") and (sample_frame < 0.0 or sample_frame > loop_end):
            previous = points[-1]
            current = points[0]
            loop_period = loop_end + 1.0
            if sample_frame < 0.0:
                sample_frame += loop_period
            return cmab_sample_hermite_segment(
                previous,
                current,
                float(previous.get("frame", 0)),
                float(current.get("frame", 0)) + loop_period,
                sample_frame,
            )
        upper = cmab_source_curve_points_leq(points, sample_frame)
        if upper == 0:
            return 0.0
        if upper >= len(points):
            return float(points[-1].get("value", 0.0))
        previous = points[upper - 1]
        current = points[upper]
        return cmab_sample_hermite_segment(
            previous,
            current,
            float(previous.get("frame", 0)),
            float(current.get("frame", 0)),
            sample_frame,
        )

    return 0.0


def cmab_native_source_curves(
    data: bytes,
    record_offset: int,
    record_size: int,
    native_channel_offsets: list[dict[str, object]],
) -> list[dict[str, object]]:
    present_offsets = sorted(
        {
            int(channel_offset["relative_offset"])
            for channel_offset in native_channel_offsets
            if channel_offset.get("present")
            and int(channel_offset.get("relative_offset", 0)) > 0
            and int(channel_offset.get("relative_offset", 0)) < record_size
        }
    )

    curves: list[dict[str, object]] = []
    for channel_offset in native_channel_offsets:
        if not channel_offset.get("present"):
            continue
        component_index = int(channel_offset.get("component_index", 0))
        relative_offset = int(channel_offset.get("relative_offset", 0))
        curve: dict[str, object] = {
            "component_index": component_index,
            "offset": relative_offset if relative_offset > 0 else 0,
            "size": 0,
            "decoded": False,
            "status": "source_curve_offset_out_of_bounds",
            "wrap_enabled": component_index != 0,
        }
        if relative_offset <= 0:
            curves.append(curve)
            continue

        next_offsets = [
            present_offset for present_offset in present_offsets if present_offset > relative_offset
        ]
        curve_end = next_offsets[0] if next_offsets else record_size
        if (
            relative_offset >= record_size
            or curve_end <= relative_offset
            or relative_offset + CMAB_SOURCE_CURVE_HEADER_SIZE > record_size
        ):
            curves.append(curve)
            continue

        absolute_curve_offset = record_offset + relative_offset
        curve["size"] = curve_end - relative_offset
        curve_type = data[absolute_curve_offset]
        point_count = header_word(data, absolute_curve_offset + 0x04)
        header_word_08 = header_word(data, absolute_curve_offset + 0x08)
        header_word_0c = header_word(data, absolute_curve_offset + 0x0C)
        stride = cmab_source_curve_point_stride(curve_type)
        curve.update(
            {
                "type": curve_type,
                "header_byte_01": data[absolute_curve_offset + 0x01],
                "header_byte_02": data[absolute_curve_offset + 0x02],
                "header_byte_03": data[absolute_curve_offset + 0x03],
                "point_count": point_count,
                "header_word_08": header_word_08,
                "header_word_0c": header_word_0c,
                "point_stride_bytes": stride,
            }
        )
        if stride == 0:
            curve["status"] = "source_curve_unsupported_type"
            curves.append(curve)
            continue
        if point_count is None:
            curve["status"] = "source_curve_points_out_of_bounds"
            curves.append(curve)
            continue
        required_size = CMAB_SOURCE_CURVE_HEADER_SIZE + int(point_count) * stride
        if required_size > int(curve["size"]) or absolute_curve_offset + required_size > len(data):
            curve["status"] = "source_curve_points_out_of_bounds"
            curves.append(curve)
            continue

        points: list[dict[str, object]] = []
        ordered = True
        for point_index in range(int(point_count)):
            point_offset = (
                absolute_curve_offset
                + CMAB_SOURCE_CURVE_HEADER_SIZE
                + point_index * stride
            )
            frame = s32_word(data, point_offset)
            value = f32_word(data, point_offset + 0x04)
            point: dict[str, object] = {
                "index": point_index,
                "frame": frame,
                "value": value,
            }
            if curve_type == 2:
                point["tangent_in"] = f32_word(data, point_offset + 0x08)
                point["tangent_out"] = f32_word(data, point_offset + 0x0C)
            if points and frame is not None and points[-1].get("frame") is not None:
                if int(frame) <= int(points[-1]["frame"]):
                    ordered = False
            points.append(point)
        curve["points"] = points
        if not ordered:
            curve["status"] = "source_curve_unordered_points"
            curves.append(curve)
            continue
        curve["decoded"] = True
        curve["status"] = "decoded"
        curve["sample_frame_0"] = cmab_sample_source_curve(curve, 0.0)
        curves.append(curve)
    return curves


def cmab_mmad_records(
    data: bytes,
    record_offsets: list[int],
    txpt_offset: int | None,
    string_table_offset: int | None,
    mads_record_offsets: list[int] | None = None,
) -> list[dict[str, object]]:
    boundary_offsets = list(record_offsets)
    if (
        txpt_offset is not None
        and txpt_offset + 4 <= len(data)
        and data[txpt_offset : txpt_offset + 4] == CMAB_TXPT_MAGIC
    ):
        boundary_offsets.append(txpt_offset)
    if (
        string_table_offset is not None
        and string_table_offset + 4 <= len(data)
        and data[string_table_offset : string_table_offset + 4] == CMAB_STRT_MAGIC
    ):
        boundary_offsets.append(string_table_offset)
    boundary_offsets = sorted(set(boundary_offsets))

    records: list[dict[str, object]] = []
    for index, offset in enumerate(record_offsets):
        next_boundaries = [
            boundary_offset
            for boundary_offset in boundary_offsets
            if boundary_offset > offset
        ]
        end_offset = next_boundaries[0] if next_boundaries else len(data)
        size = max(0, end_offset - offset)
        record: dict[str, object] = {
            "index": index,
            "offset": offset,
            "mads_record_offset": (
                mads_record_offsets[index]
                if mads_record_offsets is not None and index < len(mads_record_offsets)
                else None
            ),
            "mads_record_offset_matched": (
                mads_record_offsets[index] == offset
                if mads_record_offsets is not None and index < len(mads_record_offsets)
                else False
            ),
            "size": size,
            "scalar_keyframes_decoded": False,
            "scalar_track_status": "truncated_header",
            "native_type_status": "truncated_header",
        }
        if size < CMAB_MMAD_HEADER_SIZE or offset + CMAB_MMAD_HEADER_SIZE > len(data):
            records.append(record)
            continue

        header_words = [
            header_word(data, offset + 4 + word_index * 4)
            for word_index in range(8)
        ]
        record["header_words"] = header_words
        native_type = header_words[0] & 0xFF if header_words[0] is not None else None
        record["native_type_word"] = header_words[0]
        record["native_type"] = native_type
        record["native_type_status"] = (
            "factory_supported_1_to_5"
            if native_type in {1, 2, 3, 4, 5}
            else "factory_returns_null"
        )
        record["native_value_kind"] = cmab_native_value_kind(native_type)
        record["target_material_index"] = header_words[1]
        record["target_component_or_stage_index"] = header_words[2]
        selector_present = cmab_native_target_selector_present(native_type)
        record["target_selector_status"] = (
            "selector_present" if selector_present else "selector_absent"
        )
        record["target_selector"] = (
            s16_word(data, offset + 0x0C) if selector_present else None
        )
        record["native_channel_offset_table_offset"] = (
            cmab_native_channel_offset_table_offset(native_type)
        )
        record["native_channel_offset_count"] = cmab_native_channel_offset_count(
            native_type
        )
        native_channel_offsets = cmab_native_channel_offsets(
            data,
            offset,
            size,
            native_type,
        )
        record["native_channel_offsets"] = native_channel_offsets
        record["native_source_curves"] = cmab_native_source_curves(
            data,
            offset,
            size,
            native_channel_offsets,
        )
        packed_component_offsets = header_words[3]
        component_tracks = cmab_component_scalar_tracks(
            data,
            offset,
            size,
            packed_component_offsets,
        )
        record["component_scalar_track_offsets"] = (
            [
                packed_component_offsets & 0xFFFF,
                (packed_component_offsets >> 16) & 0xFFFF,
            ]
            if packed_component_offsets is not None
            else []
        )
        record["component_scalar_track_count"] = len(component_tracks)
        record["component_scalar_track_decoded_count"] = sum(
            1 for track in component_tracks if track.get("keyframes_decoded")
        )
        record["component_scalar_tracks"] = component_tracks
        keyframe_count = header_words[5]
        last_frame = header_words[7]
        record["keyframe_count_candidate"] = keyframe_count
        record["last_frame_candidate"] = last_frame
        if keyframe_count is None:
            record["scalar_track_status"] = "missing_keyframe_count"
            records.append(record)
            continue

        scalar_end = (
            offset
            + CMAB_MMAD_HEADER_SIZE
            + int(keyframe_count) * CMAB_MMAD_SCALAR_KEYFRAME_SIZE
        )
        record["scalar_payload_end"] = scalar_end
        if scalar_end != end_offset or scalar_end > len(data):
            record["scalar_track_status"] = "raw_or_non_scalar_payload"
            records.append(record)
            continue

        keyframes: list[dict[str, object]] = []
        for key_index in range(int(keyframe_count)):
            key_offset = (
                offset
                + CMAB_MMAD_HEADER_SIZE
                + key_index * CMAB_MMAD_SCALAR_KEYFRAME_SIZE
            )
            frame = header_word(data, key_offset)
            value_bits = header_word(data, key_offset + 4)
            value = f32_word(data, key_offset + 4)
            keyframes.append(
                {
                    "index": key_index,
                    "frame": frame,
                    "value_bits": value_bits,
                    "value": value,
                }
            )

        record["scalar_keyframes_decoded"] = True
        record["scalar_track_status"] = "scalar_keyframes_decoded"
        record["scalar_keyframe_sample_count"] = min(4, len(keyframes))
        record["scalar_keyframe_samples"] = keyframes[:4]
        if len(keyframes) > 4:
            record["scalar_keyframe_last"] = keyframes[-1]
        records.append(record)
    return records


def cmab_component_scalar_tracks(
    data: bytes,
    record_offset: int,
    record_size: int,
    packed_offsets: int | None,
) -> list[dict[str, object]]:
    if packed_offsets is None:
        return []

    candidates: list[tuple[int, int]] = []
    for component_index, track_offset in enumerate(
        (packed_offsets & 0xFFFF, (packed_offsets >> 16) & 0xFFFF)
    ):
        if track_offset == 0:
            continue
        if track_offset < 0 or track_offset + 0x10 > record_size:
            continue
        candidates.append((track_offset, component_index))
    candidates = sorted(set(candidates))

    tracks: list[dict[str, object]] = []
    for candidate_index, (track_offset, component_index) in enumerate(candidates):
        next_offsets = [
            candidate_track_offset
            for candidate_track_offset, _ in candidates[candidate_index + 1 :]
            if candidate_track_offset > track_offset
        ]
        track_end = next_offsets[0] if next_offsets else record_size
        track_size = max(0, track_end - track_offset)
        absolute_track_offset = record_offset + track_offset
        track: dict[str, object] = {
            "component_index": component_index,
            "offset": track_offset,
            "size": track_size,
            "keyframes_decoded": False,
        }
        if absolute_track_offset + 0x10 > len(data) or track_size < 0x10:
            tracks.append(track)
            continue

        header_words = [
            header_word(data, absolute_track_offset + word_index * 4)
            for word_index in range(4)
        ]
        keyframe_count = header_words[1]
        last_frame = header_words[3]
        track["header_words"] = header_words
        track["keyframe_count_candidate"] = keyframe_count
        track["last_frame_candidate"] = last_frame
        if keyframe_count is None:
            tracks.append(track)
            continue

        keyframe_start = absolute_track_offset + 0x10
        keyframe_end = keyframe_start + int(keyframe_count) * CMAB_MMAD_SCALAR_KEYFRAME_SIZE
        absolute_track_end = record_offset + track_end
        track["scalar_payload_end"] = keyframe_end
        if keyframe_end != absolute_track_end or keyframe_end > len(data):
            tracks.append(track)
            continue

        keyframes: list[dict[str, object]] = []
        for key_index in range(int(keyframe_count)):
            key_offset = keyframe_start + key_index * CMAB_MMAD_SCALAR_KEYFRAME_SIZE
            frame = header_word(data, key_offset)
            value_bits = header_word(data, key_offset + 4)
            value = f32_word(data, key_offset + 4)
            keyframes.append(
                {
                    "index": key_index,
                    "frame": frame,
                    "value_bits": value_bits,
                    "value": value,
                }
            )
        track["keyframes_decoded"] = True
        track["keyframe_sample_count"] = min(4, len(keyframes))
        track["keyframe_samples"] = keyframes[:4]
        if len(keyframes) > 4:
            track["keyframe_last"] = keyframes[-1]
        tracks.append(track)
    return tracks


def cmab_txpt_texture_records(
    data: bytes,
    txpt_offset: int | None,
    texture_data_offset: int | None,
    names: list[str],
) -> list[dict[str, object]]:
    if (
        txpt_offset is None
        or txpt_offset == 0
        or txpt_offset + 8 > len(data)
        or data[txpt_offset : txpt_offset + 4] != CMAB_TXPT_MAGIC
    ):
        return []

    texture_count = header_word(data, txpt_offset + 4)
    if texture_count is None:
        return []
    records_start = txpt_offset + 8
    records: list[dict[str, object]] = []
    for index in range(texture_count):
        record_offset = records_start + index * CMAB_TXPT_RECORD_SIZE
        if record_offset + CMAB_TXPT_RECORD_SIZE > len(data):
            records.append(
                {
                    "index": index,
                    "offset": record_offset,
                    "record_status": "truncated_record",
                }
            )
            break

        data_size = header_word(data, record_offset)
        word_04 = header_word(data, record_offset + 0x04)
        width = int.from_bytes(data[record_offset + 0x08 : record_offset + 0x0A], "little")
        height = int.from_bytes(data[record_offset + 0x0A : record_offset + 0x0C], "little")
        texture_format = int.from_bytes(
            data[record_offset + 0x0C : record_offset + 0x0E],
            "little",
        )
        data_type = int.from_bytes(
            data[record_offset + 0x0E : record_offset + 0x10],
            "little",
        )
        data_offset = header_word(data, record_offset + 0x10)
        word_14 = header_word(data, record_offset + 0x14)
        payload_status = "missing_payload_base"
        data_start: int | None = None
        if (
            texture_data_offset is not None
            and data_offset is not None
            and data_size is not None
        ):
            data_start = texture_data_offset + data_offset
            if data_start + data_size <= len(data):
                payload_status = "payload_in_bounds"
            else:
                payload_status = "payload_out_of_bounds"

        record: dict[str, object] = {
            "index": index,
            "offset": record_offset,
            "name": names[index] if index < len(names) else "",
            "data_size": data_size,
            "word_04": word_04,
            "width": width,
            "height": height,
            "texture_format": texture_format,
            "data_type": data_type,
            "data_offset": data_offset,
            "word_14": word_14,
            "payload_offset": data_start,
            "payload_status": payload_status,
        }
        records.append(record)
    return records


def cmab_texture_target_matches(
    cmab_texture_names: tuple[str, ...],
    cmb_records: list[dict[str, object]],
) -> list[dict[str, object]]:
    query_names = normalized_name_set(cmab_texture_names)
    if not query_names:
        return []

    scored: list[dict[str, object]] = []
    for record in cmb_records:
        texture_names = normalized_name_set(record.get("material_texture_names", []))
        if not texture_names:
            texture_names = normalized_name_set(record.get("texture_names", []))
        matched_names = sorted(query_names & texture_names)
        if matched_names:
            scored.append(
                {
                    "record": record,
                    "match_count": len(matched_names),
                    "matched_texture_names": matched_names,
                }
            )

    if not scored:
        return []
    max_score = max(int(item["match_count"]) for item in scored)
    return [
        item
        for item in scored
        if int(item["match_count"]) == max_score
    ]


def normalized_name_set(values: object) -> set[str]:
    if not isinstance(values, (list, tuple, set)):
        return set()
    return {str(value).lower() for value in values if str(value)}


def texture_match_candidate_record(match: dict[str, object]) -> dict[str, object]:
    record = match["record"]
    if not isinstance(record, dict):
        return {}
    result = candidate_record(record)
    result["texture_match_count"] = match.get("match_count", 0)
    result["matched_texture_names"] = match.get("matched_texture_names", [])
    return result


def declared_size_match_status(declared_size: int | None, actual_size: int) -> str:
    if declared_size is None:
        return "not_applicable"
    if declared_size == actual_size:
        return "matches"
    return "mismatch"


def size_summary(values: list[int]) -> dict[str, int]:
    if not values:
        return {
            "count": 0,
            "min": 0,
            "max": 0,
            "total": 0,
            "unique_size_count": 0,
        }
    return {
        "count": len(values),
        "min": min(values),
        "max": max(values),
        "total": sum(values),
        "unique_size_count": len(set(values)),
    }


def yes_no(value: bool) -> str:
    return "yes" if value else "no"
