from __future__ import annotations

import hashlib
import json
import math
import re
from pathlib import Path

from .binary import ParseError
from .cmb import (
    CmbModel,
    Material,
    MaterialTexture,
    TextureCoord,
    material_lighting_block_summary,
)
from .legacy_fast_resource import (
    first_valid_texture,
    has_raw_texture_stage_active_mapper_pair_candidate,
    has_raw_texture_stage_direct_primary_with_exported_secondary_candidate,
    has_raw_texture_stage_exported_order_candidate,
    has_raw_texture_stage_mapper_slot_same_texture_candidate,
    has_raw_texture_stage_primary_prefix_secondary_candidate,
    has_raw_texture_stage_repeated_mapper_prefix_candidate,
    has_raw_texture_stage_repeated_primary_raw_secondary_candidate,
    has_raw_texture_stage_single_direct_primary_candidate,
    has_raw_texture_stage_single_material_ref_primary_candidate,
    has_raw_texture_stage_single_scene_direct_primary_candidate,
    has_raw_texture_stage_single_stage_duplicate_same_texture_candidate,
    has_raw_texture_stage_prefix_selected_primary_candidate,
    has_same_texture_transformed_secondary_candidate,
    material_primary_texture_index,
    material_primary_texture_slot,
    material_is_blended,
    raw_texture_stage_baked_extra_texture_index,
    raw_texture_stage_baked_secondary_texture_filename,
    secondary_material_texture,
    secondary_material_texture_slot,
    secondary_texture_coord_export_status,
    secondary_valid_texture_slot,
    transformed_secondary_texture_filename,
)
from .texture_stage import (
    RAW_MATERIAL_TEXTURE_STAGE_CANDIDATE_END,
    RAW_MATERIAL_TEXTURE_STAGE_CANDIDATE_START,
    raw_texture_stage_selector,
)
from .zsi import ZsiFile


KNOWN_MATERIAL_ISSUES = (
    "missing_material",
    "missing_primary_texture",
    "multi_texture_without_distinct_secondary",
    "repeated_active_texture_index",
    "rotated_texture_coord",
    "non_uv0_texture_coord",
    "secondary_texture_coord_transform_not_exported",
)

KNOWN_TEXTURE_STAGE_RISKS = (
    "multi_texture_stage_selection_unverified",
    "secondary_stage_approximated",
    "secondary_stage_not_distinct",
    "repeated_active_texture_index",
    "raw_stage_count_exceeds_mapper_count",
    "raw_stage_export_gap",
)

MAX_F3D_TEXTURE_STAGES = 2


def audit_scene_materials(
    scene_dir: Path,
    scene: str,
    output_path: Path | None = None,
    *,
    shipwright_root: str | None = None,
    resource_prefix: str | None = None,
) -> dict[str, object]:
    scene = sanitize_path_part(scene)
    shipwright_root = (shipwright_root or f"scenes/overworld/{scene}").strip("/")
    resource_prefix = (resource_prefix or f"{shipwright_root}/oot3d").strip("/")

    records: list[dict[str, object]] = []
    for room_index, zsi_path in discover_scene_room_zsis(scene_dir, scene):
        zsi = ZsiFile.from_path(zsi_path)
        cmbs = zsi.embedded_cmbs()
        for cmb in cmbs:
            room_name = f"{scene}_room_{room_index}"
            asset_id = room_name if len(cmbs) == 1 else f"{room_name}_cmb_{cmb.index}"
            symbol = f"gOot3d{to_pascal_case(scene)}Room{room_index}"
            if len(cmbs) != 1:
                symbol += f"Cmb{cmb.index}"
            records.append(
                audit_model_materials(
                    cmb.model,
                    room_index=room_index,
                    cmb_index=cmb.index,
                    asset_id=asset_id,
                    resource_root=f"{resource_prefix}/{asset_id}",
                    symbol=symbol,
                    source_zsi=zsi_path,
                )
            )

    mesh_records = [
        mesh
        for record in records
        for mesh in record["meshes"]
    ]
    material_records = [
        material
        for record in records
        for material in record["materials"]
    ]
    texture_stage_risk_records = [
        texture_stage_mesh_record(mesh)
        for mesh in mesh_records
        if mesh.get("texture_stage_risk_tags")
    ]
    raw_stage_selector_counts = count_raw_texture_stage_selectors(material_records)
    raw_stage_export_classification_counts = count_raw_stage_export_classifications(
        material_records
    )
    raw_stage_export_gap_classification_counts = count_raw_stage_export_classifications(
        [
            material
            for material in material_records
            if material["raw_texture_stage_export_gap"] > 0
        ]
    )
    raw_stage_export_blocker_counts = count_raw_stage_export_blockers(material_records)
    raw_stage_alignment_case_counts = count_raw_stage_alignment_cases(material_records)
    raw_stage_export_gap_alignment_case_counts = count_raw_stage_alignment_cases(
        [
            material
            for material in material_records
            if material["raw_texture_stage_export_gap"] > 0
        ]
    )
    raw_stage_resolution_case_counts = count_raw_stage_resolution_cases(material_records)
    raw_stage_export_gap_resolution_case_counts = count_raw_stage_resolution_cases(
        [
            material
            for material in material_records
            if material["raw_texture_stage_export_gap"] > 0
        ]
    )
    raw_stage_export_order_case_counts = count_raw_stage_export_order_cases(material_records)
    raw_stage_export_gap_order_case_counts = count_raw_stage_export_order_cases(
        [
            material
            for material in material_records
            if material["raw_texture_stage_export_gap"] > 0
        ]
    )
    raw_stage_f3d_limit_case_counts = count_raw_stage_f3d_limit_cases(material_records)
    raw_stage_slot_bounds_case_counts = count_raw_stage_slot_bounds_cases(material_records)
    raw_stage_export_gap_slot_bounds_case_counts = count_raw_stage_slot_bounds_cases(
        [
            material
            for material in material_records
            if material["raw_texture_stage_export_gap"] > 0
        ]
    )
    raw_stage_slot_sequence_case_counts = count_raw_stage_slot_sequence_cases(material_records)
    raw_stage_export_gap_slot_sequence_case_counts = count_raw_stage_slot_sequence_cases(
        [
            material
            for material in material_records
            if material["raw_texture_stage_export_gap"] > 0
        ]
    )
    raw_stage_oob_delta_counts = count_raw_stage_oob_deltas(material_records)
    raw_stage_export_gap_oob_delta_counts = count_raw_stage_oob_deltas(
        [
            material
            for material in material_records
            if material["raw_texture_stage_export_gap"] > 0
        ]
    )
    raw_stage_oob_delta_signature_counts = count_raw_stage_oob_delta_signatures(
        material_records
    )
    raw_stage_export_gap_oob_delta_signature_counts = count_raw_stage_oob_delta_signatures(
        [
            material
            for material in material_records
            if material["raw_texture_stage_export_gap"] > 0
        ]
    )
    raw_stage_unexported_stage_position_counts = (
        count_raw_stage_unexported_texture_stage_positions(material_records)
    )
    raw_stage_export_gap_unexported_stage_position_counts = (
        count_raw_stage_unexported_texture_stage_positions(
            [
                material
                for material in material_records
                if material["raw_texture_stage_export_gap"] > 0
            ]
        )
    )
    raw_stage_non_gap_unexported_stage_position_counts = (
        count_raw_stage_unexported_texture_stage_positions(
            [
                material
                for material in material_records
                if material["raw_texture_stage_export_gap"] == 0
            ]
        )
    )
    raw_stage_unexported_material_ref_stage_position_counts = (
        count_raw_stage_unexported_material_ref_positions(material_records)
    )
    raw_stage_unexported_material_index_ref_stage_position_counts = (
        count_raw_stage_unexported_material_index_ref_positions(material_records)
    )
    raw_stage_export_gap_unexported_material_ref_stage_position_counts = (
        count_raw_stage_unexported_material_ref_positions(
            [
                material
                for material in material_records
                if material["raw_texture_stage_export_gap"] > 0
            ]
        )
    )
    raw_stage_export_gap_unexported_material_index_ref_stage_position_counts = (
        count_raw_stage_unexported_material_index_ref_positions(
            [
                material
                for material in material_records
                if material["raw_texture_stage_export_gap"] > 0
            ]
        )
    )
    raw_stage_non_gap_unexported_material_ref_stage_position_counts = (
        count_raw_stage_unexported_material_ref_positions(
            [
                material
                for material in material_records
                if material["raw_texture_stage_export_gap"] == 0
            ]
        )
    )
    raw_stage_non_gap_unexported_material_index_ref_stage_position_counts = (
        count_raw_stage_unexported_material_index_ref_positions(
            [
                material
                for material in material_records
                if material["raw_texture_stage_export_gap"] == 0
            ]
        )
    )
    issue_counts: dict[str, int] = {issue: 0 for issue in KNOWN_MATERIAL_ISSUES}
    for mesh in mesh_records:
        for issue in mesh["issues"]:
            issue_counts[issue] = issue_counts.get(issue, 0) + 1

    audit = {
        "scene": scene,
        "scene_dir": str(scene_dir),
        "room_count": len({record["room"] for record in records}),
        "cmb_count": len(records),
        "mesh_count": len(mesh_records),
        "material_count": len(material_records),
        "multi_texture_material_count": sum(
            1 for material in material_records if material["active_texture_slot_count"] > 1
        ),
        "secondary_texture_material_count": sum(
            1 for material in material_records if material["selected_secondary_texture"] is not None
        ),
        "texture_stage_risk_material_count": sum(
            1 for material in material_records if material["texture_stage_risk_tags"]
        ),
        "texture_stage_risk_mesh_count": len(texture_stage_risk_records),
        "texture_stage_risk_counts": count_texture_stage_risks(texture_stage_risk_records),
        "raw_texture_stage_selector_counts": raw_stage_selector_counts,
        "raw_texture_stage_mapper_mismatch_count": sum(
            1
            for material in material_records
            if not material["raw_texture_stage_selector"]["matches_texture_mappers_used"]
        ),
        "raw_texture_stage_baked_extra_material_count": sum(
            1
            for material in material_records
            if material.get("raw_texture_stage_baked_extra_count", 0) > 0
        ),
        "raw_texture_stage_baked_extra_total": sum(
            material.get("raw_texture_stage_baked_extra_count", 0)
            for material in material_records
        ),
        "raw_texture_stage_export_gap_material_count": sum(
            1 for material in material_records if material["raw_texture_stage_export_gap"] > 0
        ),
        "raw_texture_stage_export_gap_total": sum(
            material["raw_texture_stage_export_gap"] for material in material_records
        ),
        "raw_texture_stage_export_classification_counts": raw_stage_export_classification_counts,
        "raw_texture_stage_export_gap_classification_counts": (
            raw_stage_export_gap_classification_counts
        ),
        "raw_texture_stage_export_blocker_counts": raw_stage_export_blocker_counts,
        "raw_texture_stage_alignment_case_counts": raw_stage_alignment_case_counts,
        "raw_texture_stage_export_gap_alignment_case_counts": (
            raw_stage_export_gap_alignment_case_counts
        ),
        "raw_texture_stage_resolution_case_counts": raw_stage_resolution_case_counts,
        "raw_texture_stage_export_gap_resolution_case_counts": (
            raw_stage_export_gap_resolution_case_counts
        ),
        "raw_texture_stage_export_order_case_counts": raw_stage_export_order_case_counts,
        "raw_texture_stage_export_gap_order_case_counts": raw_stage_export_gap_order_case_counts,
        "raw_texture_stage_f3d_limit_case_counts": raw_stage_f3d_limit_case_counts,
        "raw_texture_stage_slot_bounds_case_counts": raw_stage_slot_bounds_case_counts,
        "raw_texture_stage_export_gap_slot_bounds_case_counts": (
            raw_stage_export_gap_slot_bounds_case_counts
        ),
        "raw_texture_stage_slot_sequence_case_counts": raw_stage_slot_sequence_case_counts,
        "raw_texture_stage_export_gap_slot_sequence_case_counts": (
            raw_stage_export_gap_slot_sequence_case_counts
        ),
        "raw_texture_stage_oob_delta_counts": raw_stage_oob_delta_counts,
        "raw_texture_stage_export_gap_oob_delta_counts": (
            raw_stage_export_gap_oob_delta_counts
        ),
        "raw_texture_stage_oob_delta_signature_counts": (
            raw_stage_oob_delta_signature_counts
        ),
        "raw_texture_stage_export_gap_oob_delta_signature_counts": (
            raw_stage_export_gap_oob_delta_signature_counts
        ),
        "raw_texture_stage_unexported_stage_position_counts": (
            raw_stage_unexported_stage_position_counts
        ),
        "raw_texture_stage_export_gap_unexported_stage_position_counts": (
            raw_stage_export_gap_unexported_stage_position_counts
        ),
        "raw_texture_stage_non_gap_unexported_stage_position_counts": (
            raw_stage_non_gap_unexported_stage_position_counts
        ),
        "raw_texture_stage_unexported_material_ref_stage_position_counts": (
            raw_stage_unexported_material_ref_stage_position_counts
        ),
        "raw_texture_stage_unexported_material_index_ref_stage_position_counts": (
            raw_stage_unexported_material_index_ref_stage_position_counts
        ),
        "raw_texture_stage_export_gap_unexported_material_ref_stage_position_counts": (
            raw_stage_export_gap_unexported_material_ref_stage_position_counts
        ),
        "raw_texture_stage_export_gap_unexported_material_index_ref_stage_position_counts": (
            raw_stage_export_gap_unexported_material_index_ref_stage_position_counts
        ),
        "raw_texture_stage_non_gap_unexported_material_ref_stage_position_counts": (
            raw_stage_non_gap_unexported_material_ref_stage_position_counts
        ),
        "raw_texture_stage_non_gap_unexported_material_index_ref_stage_position_counts": (
            raw_stage_non_gap_unexported_material_index_ref_stage_position_counts
        ),
        "primary_texture_coord_transform_count": sum(
            1 for material in material_records if material["selected_primary_texture_coord_transformed"]
        ),
        "secondary_texture_coord_transform_count": sum(
            1 for material in material_records if material["selected_secondary_texture_coord_transformed"]
        ),
        "alpha_test_material_count": sum(1 for material in material_records if material["alpha_test"]),
        "blended_material_count": sum(1 for material in material_records if material["blended"]),
        "issue_counts": issue_counts,
        "texture_stage_risk_records": texture_stage_risk_records,
        "records": records,
    }

    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(audit, indent=2) + "\n", encoding="utf-8", newline="\n")
    return audit


def audit_model_materials(
    model: CmbModel,
    *,
    room_index: int,
    cmb_index: int,
    asset_id: str,
    resource_root: str,
    symbol: str,
    source_zsi: Path,
) -> dict[str, object]:
    materials = [
        material_audit_record(model, material, resource_root, symbol)
        for material in model.materials
    ]
    material_by_index = {material["material_index"]: material for material in materials}
    meshes = []
    for mesh in model.meshes:
        shape = model.shapes[mesh.shape_index] if mesh.shape_index < len(model.shapes) else None
        material = material_by_index.get(mesh.material_index)
        texture_stage_tags = material["texture_stage_risk_tags"] if material else []
        triangles = (
            sum(len(primitive.indices) // 3 for primitive in shape.primitives)
            if shape is not None
            else 0
        )
        meshes.append(
            {
                "room": room_index,
                "cmb_index": cmb_index,
                "asset_id": asset_id,
                "model_name": model.name,
                "source_zsi": str(source_zsi),
                "source_cmb": model.source,
                "mesh_index": mesh.index,
                "shape_index": mesh.shape_index,
                "material_index": mesh.material_index,
                "visibility_id": mesh.visibility_id,
                "triangle_count": triangles,
                "vertex_count": len(shape.positions) if shape is not None else 0,
                "generated_mesh_path": f"{resource_root}/{symbol}_mesh_{mesh.index}",
                "generated_material_path": material["generated_material_path"] if material else None,
                "selected_primary_texture": material["selected_primary_texture"] if material else None,
                "selected_secondary_texture": material["selected_secondary_texture"] if material else None,
                "selected_primary_slot": material["selected_primary_slot"] if material else None,
                "selected_secondary_slot": material["selected_secondary_slot"] if material else None,
                "texture_indices": material["texture_indices"] if material else (),
                "texture_mappers_used": material["texture_mappers_used"] if material else 0,
                "texture_coords_used": material["texture_coords_used"] if material else 0,
                "active_texture_slot_count": material["active_texture_slot_count"] if material else 0,
                "texture_stage_selection_strategy": (
                    material["texture_stage_selection_strategy"] if material else None
                ),
                "texture_stage_confidence": material["texture_stage_confidence"] if material else "missing_material",
                "texture_stage_risk_tags": texture_stage_tags,
                "raw_texture_stage_selector": material["raw_texture_stage_selector"] if material else None,
                "raw_texture_stage_slot_bounds": material["raw_texture_stage_slot_bounds"] if material else None,
                "raw_texture_stage_unexported_valid_textures": (
                    material["raw_texture_stage_unexported_valid_textures"] if material else []
                ),
                "raw_texture_stage_unexported_material_refs": (
                    material["raw_texture_stage_unexported_material_refs"] if material else []
                ),
                "raw_texture_stage_unexported_material_index_refs": (
                    material["raw_texture_stage_unexported_material_index_refs"] if material else []
                ),
                "exported_texture_stage_count": material["exported_texture_stage_count"] if material else 0,
                "raw_texture_stage_export_gap": material["raw_texture_stage_export_gap"] if material else 0,
                "raw_texture_stage_export_classification": (
                    material["raw_texture_stage_export_classification"] if material else None
                ),
                "raw_texture_stage_alignment_case": (
                    material["raw_texture_stage_alignment_case"] if material else None
                ),
                "raw_texture_stage_resolution_case": (
                    material["raw_texture_stage_resolution_case"] if material else None
                ),
                "raw_texture_stage_export_order_case": (
                    material["raw_texture_stage_export_order_case"] if material else None
                ),
                "raw_texture_stage_f3d_limit_case": (
                    material["raw_texture_stage_f3d_limit_case"] if material else None
                ),
                "raw_material_analysis": material["raw_material_analysis"] if material else None,
                "alpha_test": material["alpha_test"] if material else False,
                "blended": material["blended"] if material else False,
                "issues": material_issues(material) if material else ["missing_material"],
            }
        )

    return {
        "room": room_index,
        "cmb_index": cmb_index,
        "asset_id": asset_id,
        "source_zsi": str(source_zsi),
        "source_cmb": model.source,
        "model_name": model.name,
        "static_candidate": model.is_static_candidate(),
        "texture_count": len(model.textures),
        "material_count": len(model.materials),
        "mesh_count": len(model.meshes),
        "textures": [
            {
                "index": texture.index,
                "name": texture.name,
                "width": texture.width,
                "height": texture.height,
                "format": f"0x{texture.texture_format:x}",
                "data_type": f"0x{texture.data_type:x}",
            }
            for texture in model.textures
        ],
        "materials": materials,
        "meshes": meshes,
    }


def material_audit_record(
    model: CmbModel,
    material: Material,
    resource_root: str,
    symbol: str,
) -> dict[str, object]:
    primary_index = material_primary_texture_index(model, material)
    primary_slot = material_primary_texture_slot(material, primary_index)
    secondary_texture = secondary_material_texture(model, material, primary_index)
    secondary_slot = (
        secondary_material_texture_slot(material, primary_index)
        if secondary_texture is not None
        else None
    )
    primary_coord = texture_coord_for_slot(material, primary_slot)
    secondary_coord = texture_coord_for_slot(material, secondary_slot)
    secondary_coord_export_status = (
        secondary_texture_coord_export_status(material, primary_index)
        if secondary_texture is not None
        else "none"
    )
    secondary_index = secondary_texture.index if secondary_texture else None
    baked_extra_texture_index = raw_texture_stage_baked_extra_texture_index(
        model,
        material,
        primary_index,
        secondary_index,
    )
    if secondary_texture is not None and secondary_coord_export_status == "baked_texture":
        secondary_texture_export_path = (
            f"{resource_root}/"
            f"{transformed_secondary_texture_filename(secondary_texture, material, secondary_slot)}"
        )
    elif secondary_texture is not None and baked_extra_texture_index is not None:
        baked_extra_filename = raw_texture_stage_baked_secondary_texture_filename(
            secondary_texture,
            model.textures[baked_extra_texture_index],
            material,
        )
        secondary_texture_export_path = f"{resource_root}/{baked_extra_filename}"
    else:
        secondary_texture_export_path = None
    raw_selector = raw_texture_stage_selector(material)
    baked_extra_stage_positions = (
        raw_texture_stage_baked_extra_stage_positions(
            raw_selector,
            baked_extra_texture_index,
        )
        if baked_extra_texture_index is not None
        else set()
    )
    raw_texture_stage_candidates = raw_texture_stage_texture_candidates(
        model,
        material,
        raw_selector,
        primary_index,
        secondary_index,
        baked_extra_stage_positions=baked_extra_stage_positions,
    )
    mapper_count = material.texture_mappers_used or len(material.texture_mappers)
    coord_count = material.texture_coords_used or len(material.texture_coords)
    active_texture_slot_count = sum(
        1
        for mapper in material.texture_mappers[:mapper_count]
        if mapper.index >= 0
    )

    record = {
        "material_index": material.index,
        "generated_material_path": f"{resource_root}/{symbol}_mat_{material.index}",
        "texture_indices": material.texture_indices,
        "texture_mappers_used": material.texture_mappers_used,
        "texture_coords_used": material.texture_coords_used,
        "active_texture_slot_count": active_texture_slot_count,
        "selected_primary_slot": primary_slot,
        "selected_primary_texture": texture_ref(model, primary_index),
        "selected_primary_texture_coord": (
            texture_coord_record(primary_slot, primary_coord, active=True, selected_primary=True)
            if primary_coord is not None and primary_slot is not None
            else None
        ),
        "selected_primary_texture_coord_transformed": texture_coord_is_transformed(primary_coord),
        "selected_secondary_slot": secondary_slot,
        "selected_secondary_texture": texture_ref(model, secondary_texture.index if secondary_texture else None),
        "selected_secondary_texture_export_path": secondary_texture_export_path,
        "selected_secondary_texture_coord": (
            texture_coord_record(secondary_slot, secondary_coord, active=True, selected_secondary=True)
            if secondary_coord is not None and secondary_slot is not None
            else None
        ),
        "selected_secondary_texture_coord_transformed": texture_coord_is_transformed(secondary_coord),
        "selected_secondary_texture_coord_export_status": secondary_coord_export_status,
        "mapper_slots": [
            mapper_slot_record(model, slot, mapper, primary_slot, secondary_slot, active=slot < mapper_count)
            for slot, mapper in enumerate(material.texture_mappers)
        ],
        "texture_coord_slots": [
            texture_coord_record(
                slot,
                coord,
                active=slot < coord_count,
                selected_primary=slot == primary_slot,
                selected_secondary=slot == secondary_slot,
            )
            for slot, coord in enumerate(material.texture_coords)
        ],
        "cull_back": material.cull_back,
        "alpha_test": material.alpha_test,
        "alpha_reference": material.alpha_reference,
        "alpha_function": f"0x{material.alpha_function:x}",
        "depth_test": material.depth_test,
        "depth_write": material.depth_write,
        "depth_function": f"0x{material.depth_function:x}",
        "blend_mode": material.blend_mode,
        "blend_color_alpha": material.blend_color_alpha,
        "blended": material_is_blended(material),
        "combiner_status": "not_parsed",
        "exported_texture_stage_count": exported_texture_stage_count(model, primary_index, secondary_texture),
        "raw_texture_stage_selector": raw_selector,
        "raw_texture_stage_slot_bounds": raw_texture_stage_slot_bounds(model, raw_selector),
        "raw_texture_stage_texture_candidates": raw_texture_stage_candidates,
        "raw_texture_stage_baked_extra_textures": (
            raw_texture_stage_baked_extra_textures(raw_texture_stage_candidates)
        ),
        "raw_texture_stage_baked_extra_count": len(baked_extra_stage_positions),
        "raw_texture_stage_unexported_valid_textures": (
            raw_texture_stage_unexported_valid_textures(raw_texture_stage_candidates)
        ),
        "raw_texture_stage_unexported_material_refs": (
            raw_texture_stage_unexported_material_refs(raw_texture_stage_candidates)
        ),
        "raw_texture_stage_unexported_material_index_refs": (
            raw_texture_stage_unexported_material_index_refs(raw_texture_stage_candidates)
        ),
        "raw_texture_stage_candidate_summary": raw_texture_stage_candidate_summary(
            raw_texture_stage_candidates
        ),
        "raw_material_analysis": raw_material_analysis(material),
    }
    record["raw_texture_stage_export_gap"] = max(
        0,
        record["raw_texture_stage_selector"]["stage_count"]
        - record["exported_texture_stage_count"]
        - record["raw_texture_stage_baked_extra_count"],
    )
    record["raw_texture_stage_export_classification"] = (
        raw_texture_stage_export_classification(
            record["raw_texture_stage_candidate_summary"],
            record["exported_texture_stage_count"],
            record["raw_texture_stage_export_gap"],
            record["raw_texture_stage_baked_extra_count"],
        )
    )
    record["raw_texture_stage_alignment_case"] = raw_texture_stage_alignment_case(record)
    record["raw_texture_stage_resolution_case"] = raw_texture_stage_resolution_case(record)
    record["raw_texture_stage_export_order_case"] = raw_texture_stage_export_order_case(record)
    record["raw_texture_stage_f3d_limit_case"] = raw_texture_stage_f3d_limit_case(record)
    record["texture_stage_selection_strategy"] = texture_stage_selection_strategy(
        model,
        material,
        primary_index,
        secondary_texture,
    )
    record["texture_stage_confidence"] = texture_stage_confidence(record)
    record["texture_stage_risk_tags"] = texture_stage_risk_tags(record)
    return record


def texture_stage_selection_strategy(
    model: CmbModel,
    material: Material,
    primary_index: int | None,
    secondary_texture: object | None,
) -> str:
    if secondary_texture is None:
        if primary_index is not None and first_valid_texture(material) is None:
            return "raw_selector_primary"
        if has_raw_texture_stage_single_stage_duplicate_same_texture_candidate(
            material,
            primary_index,
        ):
            return "raw_selector_single_stage_duplicate_same_texture"
        if has_raw_texture_stage_single_direct_primary_candidate(
            model,
            material,
            primary_index,
        ):
            return "raw_selector_single_direct_primary"
        if has_raw_texture_stage_single_material_ref_primary_candidate(
            model,
            material,
            primary_index,
        ):
            return "raw_selector_single_material_ref_primary"
        if has_raw_texture_stage_single_scene_direct_primary_candidate(
            model,
            material,
            primary_index,
        ):
            return "raw_selector_single_scene_direct_primary"
        return "first_valid_mapper"
    if has_raw_texture_stage_repeated_mapper_prefix_candidate(material, primary_index):
        return "raw_selector_repeated_mapper_prefix"
    if (
        secondary_texture is not None
        and has_raw_texture_stage_repeated_primary_raw_secondary_candidate(
            model,
            material,
            primary_index,
            getattr(secondary_texture, "index", None),
        )
    ):
        return "raw_selector_repeated_primary_raw_secondary"
    if has_raw_texture_stage_active_mapper_pair_candidate(material, primary_index):
        return "raw_selector_active_mapper_pair"
    if has_raw_texture_stage_direct_primary_with_exported_secondary_candidate(
        model,
        material,
        primary_index,
    ):
        return "raw_selector_direct_primary_plus_exported_secondary"
    if (
        secondary_texture is not None
        and raw_texture_stage_baked_extra_texture_index(
            model,
            material,
            primary_index,
            getattr(secondary_texture, "index", None),
        )
        is not None
    ):
        return "raw_selector_prefix_primary_secondary_baked_extra"
    if has_raw_texture_stage_prefix_selected_primary_candidate(material, primary_index):
        return "raw_selector_prefix_primary_plus_raw_secondary"
    if has_raw_texture_stage_primary_prefix_secondary_candidate(
        model,
        material,
        primary_index,
    ):
        return "raw_selector_primary_plus_direct_raw_secondary"
    if (
        secondary_texture is not None
        and has_raw_texture_stage_exported_order_candidate(
            material,
            primary_index,
            getattr(secondary_texture, "index", None),
        )
    ):
        return "raw_selector_exported_order"
    if secondary_valid_texture_slot(material, primary_index) is None:
        if (
            secondary_texture is not None
            and primary_index is not None
            and getattr(secondary_texture, "index", None) == primary_index
            and has_raw_texture_stage_mapper_slot_same_texture_candidate(material, primary_index)
        ):
            return "raw_selector_mapper_slot_same_texture"
        if (
            secondary_texture is not None
            and primary_index is not None
            and getattr(secondary_texture, "index", None) == primary_index
            and has_same_texture_transformed_secondary_candidate(material, primary_index)
        ):
            return "same_texture_transformed_secondary"
        return "first_valid_mapper_plus_raw_selector_secondary"
    return "first_valid_mapper_plus_distinct_secondary"


def texture_coord_for_slot(material: Material, slot: int | None) -> TextureCoord | None:
    if slot is None or slot >= len(material.texture_coords):
        return None
    if material.texture_coords_used and slot >= material.texture_coords_used:
        return None
    coord = material.texture_coords[slot]
    if coord.coordinate_index != 0:
        return None
    return coord


def mapper_slot_record(
    model: CmbModel,
    slot: int,
    mapper: MaterialTexture,
    primary_slot: int | None,
    secondary_slot: int | None,
    *,
    active: bool,
) -> dict[str, object]:
    return {
        "slot": slot,
        "active": active,
        "texture_index": mapper.index,
        "texture": texture_ref(model, mapper.index),
        "wrap_s": f"0x{mapper.wrap_s:x}",
        "wrap_t": f"0x{mapper.wrap_t:x}",
        "min_filter": f"0x{mapper.min_filter:x}",
        "mag_filter": f"0x{mapper.mag_filter:x}",
        "selected_primary": slot == primary_slot,
        "selected_secondary": slot == secondary_slot,
    }


def texture_coord_record(
    slot: int,
    coord: TextureCoord,
    *,
    active: bool,
    selected_primary: bool = False,
    selected_secondary: bool = False,
) -> dict[str, object]:
    return {
        "slot": slot,
        "active": active,
        "selected_primary": selected_primary,
        "selected_secondary": selected_secondary,
        "matrix_mode": coord.matrix_mode,
        "reference_camera": coord.reference_camera,
        "mapping_method": coord.mapping_method,
        "coordinate_index": coord.coordinate_index,
        "scale": [coord.scale.x, coord.scale.y],
        "rotation": coord.rotation,
        "translation": [coord.translation.x, coord.translation.y],
    }


def texture_coord_is_transformed(coord: TextureCoord | None) -> bool:
    if coord is None:
        return False
    return (
        coord.coordinate_index != 0
        or abs(coord.scale.x - 1.0) > 0.000001
        or abs(coord.scale.y - 1.0) > 0.000001
        or abs(coord.rotation) > 0.000001
        or abs(coord.translation.x) > 0.000001
        or abs(coord.translation.y) > 0.000001
    )


def raw_texture_stage_texture_candidates(
    model: CmbModel,
    material: Material,
    selector: dict[str, object],
    primary_index: int | None,
    secondary_index: int | None,
    *,
    baked_extra_stage_positions: set[int] | None = None,
) -> list[dict[str, object]]:
    stage_indices = selector.get("stage_indices")
    if not isinstance(stage_indices, list):
        return []
    baked_extra_stage_positions = baked_extra_stage_positions or set()
    mapper_count = material.texture_mappers_used or len(material.texture_mappers)
    active_mapper_indices = {
        mapper.index
        for mapper in material.texture_mappers[:mapper_count]
        if mapper.index >= 0
    }
    mapper_indices = {
        mapper.index
        for mapper in material.texture_mappers
        if mapper.index >= 0
    }
    candidates: list[dict[str, object]] = []
    for stage_position, raw_stage_index in enumerate(stage_indices):
        if not isinstance(raw_stage_index, int):
            continue
        texture = texture_ref(model, raw_stage_index)
        material_ref = raw_stage_material_ref(model, raw_stage_index)
        matches_primary = primary_index is not None and raw_stage_index == primary_index
        matches_secondary = secondary_index is not None and raw_stage_index == secondary_index
        baked_into_secondary = stage_position in baked_extra_stage_positions
        candidates.append(
            {
                "stage_position": stage_position,
                "raw_stage_index": raw_stage_index,
                "valid_texture_index": texture is not None,
                "texture": texture,
                "valid_material_index": material_ref is not None,
                "material_ref": material_ref,
                "matches_material_primary_texture": (
                    bool(material_ref)
                    and material_ref["primary_texture_index"] == raw_stage_index
                ),
                "matches_selected_primary": matches_primary,
                "matches_selected_secondary": matches_secondary,
                "baked_into_secondary": baked_into_secondary,
                "matches_active_mapper": raw_stage_index in active_mapper_indices,
                "matches_any_mapper": raw_stage_index in mapper_indices,
                "currently_exported": matches_primary or matches_secondary or baked_into_secondary,
            }
        )
    return candidates


def raw_texture_stage_baked_extra_stage_positions(
    selector: dict[str, object],
    baked_extra_texture_index: int | None,
) -> set[int]:
    if baked_extra_texture_index is None:
        return set()
    stage_indices = selector.get("stage_indices")
    if not isinstance(stage_indices, list):
        return set()
    return {
        stage_position
        for stage_position, raw_stage_index in enumerate(stage_indices)
        if raw_stage_index == baked_extra_texture_index
    }


def raw_texture_stage_candidate_summary(candidates: list[dict[str, object]]) -> dict[str, object]:
    valid_candidates = [
        candidate for candidate in candidates if candidate["valid_texture_index"]
    ]
    unexported_valid_candidates = [
        candidate
        for candidate in valid_candidates
        if not candidate["currently_exported"]
    ]
    unexported_material_ref_candidates = [
        candidate
        for candidate in unexported_valid_candidates
        if candidate["matches_material_primary_texture"]
    ]
    return {
        "stage_count": len(candidates),
        "valid_texture_candidate_count": len(valid_candidates),
        "active_mapper_candidate_count": sum(
            1 for candidate in candidates if candidate["matches_active_mapper"]
        ),
        "exported_candidate_count": sum(
            1 for candidate in candidates if candidate["currently_exported"]
        ),
        "baked_extra_candidate_count": sum(
            1 for candidate in candidates if candidate.get("baked_into_secondary")
        ),
        "selected_primary_in_raw": any(
            candidate["matches_selected_primary"] for candidate in candidates
        ),
        "selected_secondary_in_raw": any(
            candidate["matches_selected_secondary"] for candidate in candidates
        ),
        "unexported_valid_texture_candidate_count": len(unexported_valid_candidates),
        "unexported_material_ref_candidate_count": len(unexported_material_ref_candidates),
        "first_unexported_valid_texture": (
            unexported_valid_candidates[0]["texture"]
            if unexported_valid_candidates
            else None
        ),
        "first_unexported_material_ref": (
            unexported_material_ref_candidates[0]["material_ref"]
            if unexported_material_ref_candidates
            else None
        ),
    }


def raw_texture_stage_unexported_valid_textures(
    candidates: list[dict[str, object]],
) -> list[dict[str, object]]:
    return [
        {
            "stage_position": candidate["stage_position"],
            "raw_stage_index": candidate["raw_stage_index"],
            "texture": candidate["texture"],
        }
        for candidate in candidates
        if candidate["valid_texture_index"] and not candidate["currently_exported"]
    ]


def raw_texture_stage_baked_extra_textures(
    candidates: list[dict[str, object]],
) -> list[dict[str, object]]:
    return [
        {
            "stage_position": candidate["stage_position"],
            "raw_stage_index": candidate["raw_stage_index"],
            "texture": candidate["texture"],
        }
        for candidate in candidates
        if candidate["valid_texture_index"] and candidate.get("baked_into_secondary")
    ]


def raw_texture_stage_unexported_material_refs(
    candidates: list[dict[str, object]],
) -> list[dict[str, object]]:
    return [
        {
            "stage_position": candidate["stage_position"],
            "raw_stage_index": candidate["raw_stage_index"],
            "material_ref": candidate["material_ref"],
        }
        for candidate in candidates
        if (
            candidate["valid_texture_index"]
            and not candidate["currently_exported"]
            and candidate["matches_material_primary_texture"]
        )
    ]


def raw_texture_stage_unexported_material_index_refs(
    candidates: list[dict[str, object]],
) -> list[dict[str, object]]:
    return [
        {
            "stage_position": candidate["stage_position"],
            "raw_stage_index": candidate["raw_stage_index"],
            "material_ref": candidate["material_ref"],
        }
        for candidate in candidates
        if (
            candidate["valid_texture_index"]
            and not candidate["currently_exported"]
            and candidate["valid_material_index"]
        )
    ]


def raw_stage_material_ref(
    model: CmbModel,
    raw_stage_index: int,
) -> dict[str, object] | None:
    if raw_stage_index < 0 or raw_stage_index >= len(model.materials):
        return None
    material = model.materials[raw_stage_index]
    primary_texture_index = material_primary_texture_index(model, material)
    return {
        "material_position": raw_stage_index,
        "material_index": material.index,
        "primary_texture_index": primary_texture_index,
        "primary_texture": texture_ref(model, primary_texture_index),
    }


def raw_texture_stage_export_classification(
    summary: dict[str, object],
    exported_texture_stage_count: int,
    raw_stage_export_gap: int,
    baked_extra_stage_count: int = 0,
) -> dict[str, object]:
    stage_count = int(summary.get("stage_count", 0) or 0)
    valid_count = int(summary.get("valid_texture_candidate_count", 0) or 0)
    unexported_count = int(summary.get("unexported_valid_texture_candidate_count", 0) or 0)
    selected_primary_in_raw = bool(summary.get("selected_primary_in_raw"))
    blockers: list[str] = []
    opportunities: list[str] = []

    if raw_stage_export_gap <= 0:
        if baked_extra_stage_count > 0:
            status = "covered_by_baked_extra_texture_stage"
            reason = "extra raw texture stage is baked into an exported texture stage"
        else:
            status = "covered_by_current_export"
            reason = "raw stage count is already covered by exported texture stages"
    else:
        if stage_count > MAX_F3D_TEXTURE_STAGES:
            blockers.append("raw_stage_count_exceeds_f3d_two_texture_limit")
        if valid_count < stage_count:
            blockers.append("raw_stage_slot_not_texture_index")
        if not selected_primary_in_raw:
            blockers.append("selected_primary_not_in_raw_selector")
        if unexported_count == 0:
            blockers.append("no_distinct_valid_raw_texture_candidate")
        if exported_texture_stage_count >= MAX_F3D_TEXTURE_STAGES and unexported_count > 0:
            blockers.append("no_free_f3d_texture_stage")

        if (
            stage_count <= MAX_F3D_TEXTURE_STAGES
            and exported_texture_stage_count < MAX_F3D_TEXTURE_STAGES
            and selected_primary_in_raw
            and unexported_count == 1
        ):
            opportunities.append("probe_second_texture_from_raw_selector")

        if opportunities and not blockers:
            status = "candidate_two_texture_raw_secondary"
            reason = "one unexported raw texture candidate fits the two-texture F3D path"
        elif "raw_stage_count_exceeds_f3d_two_texture_limit" in blockers:
            status = "blocked_f3d_texture_stage_limit"
            reason = "raw stage count exceeds the direct TEXEL0/TEXEL1 export limit"
        elif (
            "selected_primary_not_in_raw_selector" in blockers
            or "raw_stage_slot_not_texture_index" in blockers
        ):
            status = "blocked_candidate_alignment"
            reason = "raw stage slots are not proven to map to the selected texture order"
        elif "no_distinct_valid_raw_texture_candidate" in blockers:
            status = "blocked_no_distinct_texture_candidate"
            reason = "raw selector exposes no distinct valid texture candidate to export"
        else:
            status = "needs_combiner_decoder"
            reason = "raw selector needs material combiner decoding before export"

    return {
        "status": status,
        "reason": reason,
        "raw_stage_count": stage_count,
        "exported_texture_stage_count": exported_texture_stage_count,
        "baked_extra_stage_count": baked_extra_stage_count,
        "raw_stage_export_gap": raw_stage_export_gap,
        "valid_texture_candidate_count": valid_count,
        "unexported_valid_texture_candidate_count": unexported_count,
        "blockers": blockers,
        "opportunities": opportunities,
    }


def raw_texture_stage_alignment_case(material: dict[str, object]) -> str:
    classification = material["raw_texture_stage_export_classification"]
    status = str(classification["status"])
    if status != "blocked_candidate_alignment":
        return status

    summary = material["raw_texture_stage_candidate_summary"]
    stage_count = int(summary.get("stage_count", 0) or 0)
    valid_count = int(summary.get("valid_texture_candidate_count", 0) or 0)
    exported_count = int(summary.get("exported_candidate_count", 0) or 0)
    unexported_count = int(summary.get("unexported_valid_texture_candidate_count", 0) or 0)
    active_count = int(summary.get("active_mapper_candidate_count", 0) or 0)
    primary_in_raw = bool(summary.get("selected_primary_in_raw"))
    has_primary = material["selected_primary_texture"] is not None
    has_secondary = material["selected_secondary_texture"] is not None

    if not has_primary and valid_count == 0:
        return "untextured_all_raw_slots_oob"
    if (
        has_primary
        and stage_count == 2
        and valid_count == 0
        and not primary_in_raw
    ):
        return "stage2_all_raw_slots_oob_primary_selected"
    if (
        has_primary
        and stage_count == 2
        and valid_count == 1
        and primary_in_raw
        and unexported_count == 0
    ):
        return "stage2_primary_only_second_raw_slot_oob"
    if (
        has_primary
        and stage_count == 2
        and valid_count == 2
        and not primary_in_raw
        and active_count == 0
        and exported_count == 0
    ):
        return "stage2_two_valid_raw_textures_primary_not_selected"
    if (
        has_primary
        and stage_count == 2
        and valid_count == 1
        and not primary_in_raw
        and active_count == 0
        and exported_count == 0
    ):
        return "stage2_one_valid_raw_texture_primary_not_selected"
    return (
        f"other_stage{stage_count}_valid{valid_count}_exported{exported_count}_"
        f"unexported{unexported_count}_active{active_count}_"
        f"primaryraw_{'yes' if primary_in_raw else 'no'}_"
        f"primary_{'yes' if has_primary else 'no'}_"
        f"secondary_{'yes' if has_secondary else 'no'}"
    )


def raw_texture_stage_resolution_case(material: dict[str, object]) -> str:
    if int(material["raw_texture_stage_export_gap"]) <= 0:
        if int(material.get("raw_texture_stage_baked_extra_count", 0) or 0) > 0:
            return "covered_by_baked_extra_texture_stage"
        return "covered_by_current_export"

    classification = material["raw_texture_stage_export_classification"]
    status = str(classification["status"])
    bounds = material["raw_texture_stage_slot_bounds"]
    sequence_case = str(bounds["sequence_case"])

    if status == "blocked_f3d_texture_stage_limit":
        if sequence_case == "contiguous_all_valid":
            return "blocked_f3d_limit_all_raw_slots_valid"
        if sequence_case == "valid_prefix_then_texture_count_boundary_oob_run":
            return "blocked_f3d_limit_valid_prefix_boundary_oob"
        if sequence_case == "texture_count_boundary_oob_run":
            return "blocked_f3d_limit_boundary_oob"
        if sequence_case == "contiguous_oob_run_offset_from_texture_count":
            return "blocked_f3d_limit_offset_oob"
        return f"blocked_f3d_limit_{sequence_case}"

    if sequence_case == "valid_prefix_then_texture_count_boundary_oob_run":
        return "probable_non_texture_boundary_oob_after_valid_prefix"
    if sequence_case == "texture_count_boundary_oob_run":
        return "probable_non_texture_boundary_oob"
    if sequence_case == "contiguous_oob_run_offset_from_texture_count":
        return "unknown_oob_texture_or_material_indirection"
    if sequence_case == "contiguous_all_valid":
        return "all_raw_slots_valid_but_unresolved"
    return f"{status}_{sequence_case}"


def raw_texture_stage_export_order_case(material: dict[str, object]) -> str:
    candidates = material["raw_texture_stage_texture_candidates"]
    if not isinstance(candidates, list) or not candidates:
        return "no_raw_stage_candidates"

    exported_indices = [
        index
        for index in (
            texture_ref_index(material["selected_primary_texture"]),
            texture_ref_index(material["selected_secondary_texture"]),
        )
        if index is not None
    ]
    if not exported_indices:
        return "no_exported_texture_stages"

    raw_indices = [
        int(candidate["raw_stage_index"])
        for candidate in candidates
        if isinstance(candidate, dict) and isinstance(candidate.get("raw_stage_index"), int)
    ]
    if not raw_indices:
        return "no_raw_stage_indices"

    raw_prefix = raw_indices[: len(exported_indices)]
    if raw_prefix == exported_indices:
        return "raw_prefix_matches_exported_order"

    candidate_by_index = {
        int(candidate["raw_stage_index"]): candidate
        for candidate in candidates
        if isinstance(candidate, dict) and isinstance(candidate.get("raw_stage_index"), int)
    }
    raw_prefix_candidates = [
        candidate_by_index[index] for index in raw_prefix if index in candidate_by_index
    ]
    if any(not candidate.get("valid_texture_index") for candidate in raw_prefix_candidates):
        return "raw_prefix_contains_oob_slot"

    primary_index = exported_indices[0]
    secondary_index = (
        exported_indices[1]
        if len(exported_indices) > 1 and exported_indices[1] != primary_index
        else None
    )
    if secondary_index is not None and raw_prefix == [secondary_index, primary_index]:
        return "raw_prefix_reverses_exported_order"

    if secondary_index is not None and raw_prefix and raw_prefix[0] == secondary_index:
        return "raw_prefix_starts_with_exported_secondary"

    if raw_prefix and raw_prefix[0] == primary_index:
        return "raw_prefix_starts_with_exported_primary_then_differs"

    if primary_index not in raw_indices:
        return "selected_primary_absent_from_raw_order"

    if any(index not in exported_indices for index in raw_prefix):
        return "raw_prefix_uses_unexported_valid_texture"

    return "raw_prefix_differs_from_exported_order"


def raw_texture_stage_f3d_limit_case(material: dict[str, object]) -> str:
    classification = material["raw_texture_stage_export_classification"]
    if classification["status"] != "blocked_f3d_texture_stage_limit":
        return "not_f3d_limit"

    summary = material["raw_texture_stage_candidate_summary"]
    stage_count = int(summary.get("stage_count", 0) or 0)
    valid_count = int(summary.get("valid_texture_candidate_count", 0) or 0)
    exported_count = int(summary.get("exported_candidate_count", 0) or 0)
    unexported_count = int(summary.get("unexported_valid_texture_candidate_count", 0) or 0)
    order_case = str(material["raw_texture_stage_export_order_case"])
    unexported_positions = [
        int(texture["stage_position"])
        for texture in material["raw_texture_stage_unexported_valid_textures"]
        if isinstance(texture, dict) and isinstance(texture.get("stage_position"), int)
    ]
    position_suffix = (
        "_".join(f"p{position}" for position in unexported_positions)
        if unexported_positions
        else "none"
    )
    if (
        stage_count == 3
        and valid_count == 3
        and exported_count == 2
        and unexported_count == 1
        and order_case == "raw_prefix_matches_exported_order"
        and unexported_positions == [2]
    ):
        return "stage3_prefix_matches_exported_order_one_extra_p2"
    return (
        f"stage{stage_count}_valid{valid_count}_exported{exported_count}_"
        f"unexported{unexported_count}_{order_case}_extra_{position_suffix}"
    )


def texture_ref_index(texture: object) -> int | None:
    if not isinstance(texture, dict):
        return None
    index = texture.get("index")
    return index if isinstance(index, int) else None


def raw_texture_stage_slot_bounds(
    model: CmbModel,
    selector: dict[str, object],
) -> dict[str, object]:
    stage_count = int(selector.get("stage_count", 0) or 0)
    raw_slots = selector.get("raw_stage_slots")
    if not isinstance(raw_slots, list):
        raw_slots = []
    active_slots = raw_slots[:stage_count]
    slot_records: list[dict[str, object]] = []
    valid_positions: list[int] = []
    oob_positions: list[int] = []
    negative_positions: list[int] = []
    non_integer_positions: list[int] = []
    oob_deltas: list[int] = []

    for position, value in enumerate(active_slots):
        status = "non_integer"
        delta = None
        if isinstance(value, int):
            if value < 0:
                status = "negative"
                negative_positions.append(position)
            elif value < len(model.textures):
                status = "valid_texture_index"
                valid_positions.append(position)
            else:
                status = "oob_texture_index"
                delta = value - len(model.textures)
                oob_positions.append(position)
                oob_deltas.append(delta)
        else:
            non_integer_positions.append(position)
        slot_records.append(
            {
                "stage_position": position,
                "raw_stage_index": value,
                "status": status,
                "oob_delta_from_texture_count": delta,
            }
        )

    case = raw_texture_stage_slot_bounds_case(
        stage_count,
        valid_positions,
        oob_positions,
        negative_positions,
        non_integer_positions,
    )
    sequence_case = raw_texture_stage_slot_sequence_case(
        len(model.textures),
        active_slots,
    )
    return {
        "texture_count": len(model.textures),
        "stage_count": stage_count,
        "active_raw_stage_slots": active_slots,
        "valid_texture_slot_count": len(valid_positions),
        "oob_texture_slot_count": len(oob_positions),
        "negative_slot_count": len(negative_positions),
        "non_integer_slot_count": len(non_integer_positions),
        "valid_positions": valid_positions,
        "oob_positions": oob_positions,
        "negative_positions": negative_positions,
        "non_integer_positions": non_integer_positions,
        "oob_deltas_from_texture_count": oob_deltas,
        "oob_delta_signature": raw_texture_stage_oob_delta_signature(oob_deltas),
        "case": case,
        "sequence_case": sequence_case,
        "slots": slot_records,
    }


def raw_texture_stage_slot_bounds_case(
    stage_count: int,
    valid_positions: list[int],
    oob_positions: list[int],
    negative_positions: list[int],
    non_integer_positions: list[int],
) -> str:
    if stage_count <= 0:
        return "stage0_no_slots"
    parts = [f"stage{stage_count}"]
    if valid_positions:
        parts.append(f"valid_{stage_positions_key(valid_positions)}")
    if oob_positions:
        parts.append(f"oob_{stage_positions_key(oob_positions)}")
    if negative_positions:
        parts.append(f"negative_{stage_positions_key(negative_positions)}")
    if non_integer_positions:
        parts.append(f"non_integer_{stage_positions_key(non_integer_positions)}")
    if len(parts) == 1:
        parts.append("no_active_slot_records")
    return "__".join(parts)


def stage_positions_key(positions: list[int]) -> str:
    return "_".join(f"p{position}" for position in positions)


def raw_texture_stage_slot_sequence_case(
    texture_count: int,
    active_slots: list[object],
) -> str:
    if not active_slots:
        return "empty"
    if not all(isinstance(value, int) for value in active_slots):
        return "contains_non_integer"
    values = [int(value) for value in active_slots]
    if any(value < 0 for value in values):
        return "contains_negative"
    is_contiguous = all(
        values[index + 1] == values[index] + 1
        for index in range(len(values) - 1)
    )
    first_oob_position = next(
        (
            position
            for position, value in enumerate(values)
            if value >= texture_count
        ),
        None,
    )
    if first_oob_position is None:
        return "contiguous_all_valid" if is_contiguous else "noncontiguous_all_valid"
    if not is_contiguous:
        return "noncontiguous_with_oob"
    if values[first_oob_position] == texture_count:
        if first_oob_position == 0:
            return "texture_count_boundary_oob_run"
        return "valid_prefix_then_texture_count_boundary_oob_run"
    return "contiguous_oob_run_offset_from_texture_count"


def raw_texture_stage_oob_delta_signature(oob_deltas: list[int]) -> str:
    if not oob_deltas:
        return "none"
    return "delta_" + "_".join(str(delta) for delta in oob_deltas)


def texture_ref(model: CmbModel, texture_index: int | None) -> dict[str, object] | None:
    if texture_index is None or texture_index < 0 or texture_index >= len(model.textures):
        return None
    texture = model.textures[texture_index]
    return {
        "index": texture.index,
        "name": texture.name,
        "width": texture.width,
        "height": texture.height,
        "format": f"0x{texture.texture_format:x}",
        "data_type": f"0x{texture.data_type:x}",
    }


def material_issues(material: dict[str, object]) -> list[str]:
    issues: list[str] = []
    if material["selected_primary_texture"] is None and any(
        index >= 0 for index in material["texture_indices"]
    ):
        issues.append("missing_primary_texture")
    if (
        material["active_texture_slot_count"] > 1
        and material["selected_secondary_texture"] is None
        and not texture_stage_strategy_resolves_repeated_texture(material)
    ):
        issues.append("multi_texture_without_distinct_secondary")
    if has_repeated_active_texture(material):
        issues.append("repeated_active_texture_index")
    if has_rotated_texture_coord(material):
        issues.append("rotated_texture_coord")
    if has_non_uv0_texture_coord(material):
        issues.append("non_uv0_texture_coord")
    if (
        material["selected_secondary_texture_coord_transformed"]
        and material.get("selected_secondary_texture_coord_export_status") != "baked_texture"
    ):
        issues.append("secondary_texture_coord_transform_not_exported")
    return issues


def texture_stage_confidence(material: dict[str, object]) -> str:
    if material["selected_primary_texture"] is None:
        return "untextured_or_missing"
    if material["texture_stage_selection_strategy"] == "raw_selector_repeated_mapper_prefix":
        return "raw_selector_repeated_mapper_prefix"
    if material["texture_stage_selection_strategy"] == "raw_selector_repeated_primary_raw_secondary":
        return "raw_selector_repeated_primary_raw_secondary"
    if material["texture_stage_selection_strategy"] == "raw_selector_active_mapper_pair":
        return "raw_selector_active_mapper_pair"
    if material["texture_stage_selection_strategy"] == "raw_selector_exported_order":
        return "raw_selector_exported_order"
    if (
        material["texture_stage_selection_strategy"]
        == "raw_selector_direct_primary_plus_exported_secondary"
    ):
        return "raw_selector_direct_primary_plus_exported_secondary"
    if (
        material["texture_stage_selection_strategy"]
        == "raw_selector_prefix_primary_plus_raw_secondary"
    ):
        return "raw_selector_prefix_primary_plus_raw_secondary"
    if (
        material["texture_stage_selection_strategy"]
        == "raw_selector_primary_plus_direct_raw_secondary"
    ):
        return "raw_selector_primary_plus_direct_raw_secondary"
    if (
        material["texture_stage_selection_strategy"]
        == "raw_selector_prefix_primary_secondary_baked_extra"
    ):
        return "raw_selector_prefix_primary_secondary_baked_extra"
    if material["texture_stage_selection_strategy"] == "raw_selector_mapper_slot_same_texture":
        return "raw_selector_mapper_slot_same_texture"
    if (
        material["texture_stage_selection_strategy"]
        == "raw_selector_single_stage_duplicate_same_texture"
    ):
        return "raw_selector_single_stage_duplicate_same_texture"
    if material["texture_stage_selection_strategy"] == "raw_selector_single_direct_primary":
        return "raw_selector_single_direct_primary"
    if material["texture_stage_selection_strategy"] == "raw_selector_single_material_ref_primary":
        return "raw_selector_single_material_ref_primary"
    if material["texture_stage_selection_strategy"] == "raw_selector_single_scene_direct_primary":
        return "raw_selector_single_scene_direct_primary"
    if material["texture_stage_selection_strategy"] == "same_texture_transformed_secondary":
        return "same_texture_transformed_secondary"
    if material["active_texture_slot_count"] <= 1:
        if material["selected_primary_slot"] is None:
            return "raw_selector_primary"
        if material["selected_secondary_texture"] is not None:
            return "single_mapper_raw_selector_secondary"
        return "single_texture_mapper"
    return "unverified_multi_texture_mapper"


def texture_stage_risk_tags(material: dict[str, object]) -> list[str]:
    tags: list[str] = []
    resolved_repeated_mapper_prefix = (
        material["texture_stage_selection_strategy"] == "raw_selector_repeated_mapper_prefix"
    )
    resolved_repeated_primary_raw_secondary = (
        material["texture_stage_selection_strategy"]
        == "raw_selector_repeated_primary_raw_secondary"
    )
    resolved_active_mapper_pair = (
        material["texture_stage_selection_strategy"] == "raw_selector_active_mapper_pair"
    )
    resolved_exported_order = (
        material["texture_stage_selection_strategy"] == "raw_selector_exported_order"
    )
    resolved_direct_primary_with_exported_secondary = (
        material["texture_stage_selection_strategy"]
        == "raw_selector_direct_primary_plus_exported_secondary"
    )
    resolved_prefix_primary = (
        material["texture_stage_selection_strategy"]
        == "raw_selector_prefix_primary_plus_raw_secondary"
    )
    resolved_primary_prefix_direct_secondary = (
        material["texture_stage_selection_strategy"]
        == "raw_selector_primary_plus_direct_raw_secondary"
    )
    resolved_prefix_primary_secondary_baked_extra = (
        material["texture_stage_selection_strategy"]
        == "raw_selector_prefix_primary_secondary_baked_extra"
    )
    resolved_mapper_slot_same_texture = (
        material["texture_stage_selection_strategy"] == "raw_selector_mapper_slot_same_texture"
    )
    resolved_single_stage_duplicate = (
        material["texture_stage_selection_strategy"]
        == "raw_selector_single_stage_duplicate_same_texture"
    )
    resolved_single_direct_primary = (
        material["texture_stage_selection_strategy"] == "raw_selector_single_direct_primary"
    )
    resolved_single_material_ref_primary = (
        material["texture_stage_selection_strategy"] == "raw_selector_single_material_ref_primary"
    )
    resolved_single_scene_direct_primary = (
        material["texture_stage_selection_strategy"] == "raw_selector_single_scene_direct_primary"
    )
    resolved_same_texture_secondary = (
        material["texture_stage_selection_strategy"] == "same_texture_transformed_secondary"
    )
    if (
        not resolved_repeated_mapper_prefix
        and not resolved_repeated_primary_raw_secondary
        and not resolved_active_mapper_pair
        and not resolved_exported_order
        and not resolved_direct_primary_with_exported_secondary
        and not resolved_prefix_primary
        and not resolved_primary_prefix_direct_secondary
        and not resolved_prefix_primary_secondary_baked_extra
        and not resolved_mapper_slot_same_texture
        and not resolved_single_stage_duplicate
        and not resolved_single_direct_primary
        and not resolved_single_material_ref_primary
        and not resolved_single_scene_direct_primary
        and not resolved_same_texture_secondary
    ):
        if material["active_texture_slot_count"] > 1:
            tags.append("multi_texture_stage_selection_unverified")
            if material["selected_secondary_texture"] is not None:
                tags.append("secondary_stage_approximated")
            else:
                tags.append("secondary_stage_not_distinct")
        elif material["selected_secondary_texture"] is not None:
            tags.append("secondary_stage_approximated")
    if (
        material["raw_texture_stage_selector"]["stage_count_exceeds_texture_mappers_used"]
        and not resolved_prefix_primary_secondary_baked_extra
    ):
        tags.append("raw_stage_count_exceeds_mapper_count")
    if material["raw_texture_stage_export_gap"] > 0:
        tags.append("raw_stage_export_gap")
    if has_repeated_active_texture(material):
        tags.append("repeated_active_texture_index")
    return tags


def texture_stage_mesh_record(mesh: dict[str, object]) -> dict[str, object]:
    return {
        "room": mesh["room"],
        "asset_id": mesh["asset_id"],
        "source_zsi": mesh["source_zsi"],
        "source_cmb": mesh["source_cmb"],
        "mesh_index": mesh["mesh_index"],
        "shape_index": mesh["shape_index"],
        "material_index": mesh["material_index"],
        "triangle_count": mesh["triangle_count"],
        "generated_mesh_path": mesh["generated_mesh_path"],
        "generated_material_path": mesh["generated_material_path"],
        "texture_indices": mesh["texture_indices"],
        "active_texture_slot_count": mesh["active_texture_slot_count"],
        "selected_primary_slot": mesh["selected_primary_slot"],
        "selected_primary_texture": mesh["selected_primary_texture"],
        "selected_secondary_slot": mesh["selected_secondary_slot"],
        "selected_secondary_texture": mesh["selected_secondary_texture"],
        "texture_stage_selection_strategy": mesh["texture_stage_selection_strategy"],
        "texture_stage_confidence": mesh["texture_stage_confidence"],
        "texture_stage_risk_tags": mesh["texture_stage_risk_tags"],
        "raw_texture_stage_selector": mesh["raw_texture_stage_selector"],
        "raw_texture_stage_slot_bounds": mesh["raw_texture_stage_slot_bounds"],
        "raw_texture_stage_unexported_valid_textures": (
            mesh["raw_texture_stage_unexported_valid_textures"]
        ),
        "raw_texture_stage_unexported_material_refs": (
            mesh["raw_texture_stage_unexported_material_refs"]
        ),
        "raw_texture_stage_unexported_material_index_refs": (
            mesh["raw_texture_stage_unexported_material_index_refs"]
        ),
        "exported_texture_stage_count": mesh["exported_texture_stage_count"],
        "raw_texture_stage_export_gap": mesh["raw_texture_stage_export_gap"],
        "raw_texture_stage_export_classification": mesh[
            "raw_texture_stage_export_classification"
        ],
        "raw_texture_stage_alignment_case": mesh["raw_texture_stage_alignment_case"],
        "raw_texture_stage_resolution_case": mesh["raw_texture_stage_resolution_case"],
        "raw_texture_stage_export_order_case": mesh["raw_texture_stage_export_order_case"],
        "raw_texture_stage_f3d_limit_case": mesh["raw_texture_stage_f3d_limit_case"],
        "raw_material_analysis": mesh["raw_material_analysis"],
    }


def exported_texture_stage_count(
    model: CmbModel,
    primary_index: int | None,
    secondary_texture: object | None,
) -> int:
    count = 0
    if primary_index is not None and 0 <= primary_index < len(model.textures):
        count += 1
    if secondary_texture is not None:
        count += 1
    return count


def raw_material_analysis(material: Material) -> dict[str, object]:
    raw = material.raw_material
    nonzero_words = raw_material_nonzero_words(raw)
    return {
        "size": len(raw),
        "sha256": hashlib.sha256(raw).hexdigest() if raw else None,
        "nonzero_word_count": len(nonzero_words),
        "texture_stage_candidate_range": [
            RAW_MATERIAL_TEXTURE_STAGE_CANDIDATE_START,
            RAW_MATERIAL_TEXTURE_STAGE_CANDIDATE_END,
        ],
        "texture_stage_candidate_nonzero_words": raw_material_nonzero_words(
            raw,
            RAW_MATERIAL_TEXTURE_STAGE_CANDIDATE_START,
            RAW_MATERIAL_TEXTURE_STAGE_CANDIDATE_END,
        ),
        "material_lighting_block": material_lighting_block_summary(material.lighting_block),
    }


def raw_material_nonzero_words(
    raw: bytes,
    start: int = 0,
    end: int | None = None,
) -> list[dict[str, object]]:
    if not raw:
        return []
    end = min(len(raw), len(raw) if end is None else end)
    words: list[dict[str, object]] = []
    for offset in range(start, end - ((end - start) % 4), 4):
        value = int.from_bytes(raw[offset : offset + 4], "little")
        if value:
            words.append({"offset": f"0x{offset:03x}", "u32": f"0x{value:08x}"})
    return words


def count_texture_stage_risks(records: list[dict[str, object]]) -> dict[str, int]:
    counts = {risk: 0 for risk in KNOWN_TEXTURE_STAGE_RISKS}
    for record in records:
        for risk in record["texture_stage_risk_tags"]:
            counts[risk] = counts.get(risk, 0) + 1
    counts["total"] = sum(counts.values())
    return counts


def count_raw_texture_stage_selectors(materials: list[dict[str, object]]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for material in materials:
        selector = material["raw_texture_stage_selector"]
        key = f"stage_count_{selector['stage_count']}"
        counts[key] = counts.get(key, 0) + 1
    return dict(sorted(counts.items()))


def count_raw_stage_export_classifications(materials: list[dict[str, object]]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for material in materials:
        classification = material["raw_texture_stage_export_classification"]
        status = str(classification["status"])
        counts[status] = counts.get(status, 0) + 1
    return dict(sorted(counts.items()))


def count_raw_stage_export_blockers(materials: list[dict[str, object]]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for material in materials:
        classification = material["raw_texture_stage_export_classification"]
        for blocker in classification["blockers"]:
            blocker = str(blocker)
            counts[blocker] = counts.get(blocker, 0) + 1
    return dict(sorted(counts.items()))


def count_raw_stage_alignment_cases(materials: list[dict[str, object]]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for material in materials:
        case = str(material["raw_texture_stage_alignment_case"])
        counts[case] = counts.get(case, 0) + 1
    return dict(sorted(counts.items()))


def count_raw_stage_resolution_cases(materials: list[dict[str, object]]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for material in materials:
        case = str(material["raw_texture_stage_resolution_case"])
        counts[case] = counts.get(case, 0) + 1
    return dict(sorted(counts.items()))


def count_raw_stage_export_order_cases(materials: list[dict[str, object]]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for material in materials:
        case = str(material["raw_texture_stage_export_order_case"])
        counts[case] = counts.get(case, 0) + 1
    return dict(sorted(counts.items()))


def count_raw_stage_f3d_limit_cases(materials: list[dict[str, object]]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for material in materials:
        case = str(material["raw_texture_stage_f3d_limit_case"])
        if case == "not_f3d_limit":
            continue
        counts[case] = counts.get(case, 0) + 1
    return dict(sorted(counts.items()))


def count_raw_stage_slot_bounds_cases(materials: list[dict[str, object]]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for material in materials:
        bounds = material["raw_texture_stage_slot_bounds"]
        case = str(bounds["case"])
        counts[case] = counts.get(case, 0) + 1
    return dict(sorted(counts.items()))


def count_raw_stage_slot_sequence_cases(materials: list[dict[str, object]]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for material in materials:
        bounds = material["raw_texture_stage_slot_bounds"]
        case = str(bounds["sequence_case"])
        counts[case] = counts.get(case, 0) + 1
    return dict(sorted(counts.items()))


def count_raw_stage_oob_deltas(materials: list[dict[str, object]]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for material in materials:
        bounds = material["raw_texture_stage_slot_bounds"]
        for delta in bounds["oob_deltas_from_texture_count"]:
            key = f"delta_{delta}"
            counts[key] = counts.get(key, 0) + 1
    return dict(sorted(counts.items()))


def count_raw_stage_oob_delta_signatures(materials: list[dict[str, object]]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for material in materials:
        bounds = material["raw_texture_stage_slot_bounds"]
        key = str(bounds["oob_delta_signature"])
        counts[key] = counts.get(key, 0) + 1
    return dict(sorted(counts.items()))


def count_raw_stage_unexported_texture_stage_positions(
    materials: list[dict[str, object]],
) -> dict[str, int]:
    counts: dict[str, int] = {}
    for material in materials:
        for texture in material["raw_texture_stage_unexported_valid_textures"]:
            key = f"stage_position_{texture['stage_position']}"
            counts[key] = counts.get(key, 0) + 1
    return dict(sorted(counts.items()))


def count_raw_stage_unexported_material_ref_positions(
    materials: list[dict[str, object]],
) -> dict[str, int]:
    counts: dict[str, int] = {}
    for material in materials:
        for material_ref in material["raw_texture_stage_unexported_material_refs"]:
            key = f"stage_position_{material_ref['stage_position']}"
            counts[key] = counts.get(key, 0) + 1
    return dict(sorted(counts.items()))


def count_raw_stage_unexported_material_index_ref_positions(
    materials: list[dict[str, object]],
) -> dict[str, int]:
    counts: dict[str, int] = {}
    for material in materials:
        for material_ref in material["raw_texture_stage_unexported_material_index_refs"]:
            key = f"stage_position_{material_ref['stage_position']}"
            counts[key] = counts.get(key, 0) + 1
    return dict(sorted(counts.items()))


def has_repeated_active_texture(material: dict[str, object]) -> bool:
    if texture_stage_strategy_resolves_repeated_texture(material):
        return False
    indices = [
        slot["texture_index"]
        for slot in material["mapper_slots"]
        if slot["active"] and slot["texture_index"] >= 0
    ]
    return len(indices) != len(set(indices))


def texture_stage_strategy_resolves_repeated_texture(material: dict[str, object]) -> bool:
    return material.get("texture_stage_selection_strategy") in {
        "raw_selector_repeated_mapper_prefix",
        "raw_selector_repeated_primary_raw_secondary",
        "raw_selector_mapper_slot_same_texture",
        "raw_selector_single_stage_duplicate_same_texture",
        "same_texture_transformed_secondary",
    }


def has_rotated_texture_coord(material: dict[str, object]) -> bool:
    for coord in material["texture_coord_slots"]:
        if (
            not coord["active"]
            or not math.isfinite(coord["rotation"])
            or abs(coord["rotation"]) <= 0.000001
        ):
            continue
        if coord["selected_primary"]:
            continue
        if (
            coord["selected_secondary"]
            and material.get("selected_secondary_texture_coord_export_status") == "baked_texture"
        ):
            continue
        return True
    return False


def has_non_uv0_texture_coord(material: dict[str, object]) -> bool:
    return any(
        coord["active"] and coord["coordinate_index"] != 0
        for coord in material["texture_coord_slots"]
    )


def discover_scene_room_zsis(scene_dir: Path, scene: str) -> list[tuple[int, Path]]:
    if not scene_dir.is_dir():
        raise ParseError(f"{scene_dir}: expected a scene directory")
    pattern = re.compile(rf"^{re.escape(scene)}_(\d+)_info\.zsi$", re.IGNORECASE)
    rooms: list[tuple[int, Path]] = []
    for path in scene_dir.glob(f"{scene}_*_info.zsi"):
        match = pattern.match(path.name)
        if match:
            rooms.append((int(match.group(1)), path))
    if not rooms:
        raise ParseError(f"{scene_dir}: no room ZSI files found for scene {scene!r}")
    return sorted(rooms, key=lambda item: item[0])


def to_pascal_case(value: str) -> str:
    parts = re.split(r"[^A-Za-z0-9]+", value)
    return "".join(part[:1].upper() + part[1:] for part in parts if part)


def sanitize_path_part(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_\\-]+", "_", value).strip("_") or "asset"
