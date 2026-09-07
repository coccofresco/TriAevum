from __future__ import annotations

import json
from collections import Counter, defaultdict
from pathlib import Path

from .binary import ParseError
from .character_mesh_selection import (
    CharacterMeshSelection,
    load_character_mesh_selection,
    validate_character_mesh_selection,
)
from .cmb import CmbModel, Mesh, Primitive, Shape, Vec3
from .legacy_fast_resource import (
    LegacyFastResourceOptions,
    material_primary_texture_index_for_profile,
    mesh_texture_export_profile,
)
from .skinned_export import load_cmb_payload


CHARACTER_VISIBILITY_RECONSTRUCTION_FORMAT = "oot3d_character_visibility_reconstruction_v1"


def audit_character_visibility_reconstruction(
    input_path: Path,
    output: Path,
    *,
    selection_manifest: Path | None = None,
    cmb_index: int = 0,
    cmb_name: str | None = None,
    n64_player_lib: Path | None = None,
    sample_limit: int = 100,
) -> dict[str, object]:
    data, source, embedded_name = load_cmb_payload(input_path, cmb_index, cmb_name)
    model = CmbModel.parse(data, source)
    selection = (
        load_character_mesh_selection(selection_manifest)
        if selection_manifest is not None
        else None
    )
    if selection is not None:
        validate_character_mesh_selection(selection, model)

    report = reconstruct_character_visibility(
        model,
        source=source,
        embedded_name=embedded_name,
        input_path=input_path,
        selection=selection,
        n64_player_lib=n64_player_lib,
        sample_limit=sample_limit,
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")
    return report


def reconstruct_character_visibility(
    model: CmbModel,
    *,
    source: str,
    embedded_name: str | None,
    input_path: Path,
    selection: CharacterMeshSelection | None,
    n64_player_lib: Path | None,
    sample_limit: int,
) -> dict[str, object]:
    texture_profile = mesh_texture_export_profile(
        model,
        LegacyFastResourceOptions(
            output_dir=Path("."),
            resource_root="audit",
            symbol="audit",
            include_textures=False,
            allow_nonstatic=True,
            texture_orientation="flip_y",
        ),
    )
    selected_keys = selection.primitive_keys if selection is not None else frozenset()
    primitive_records = [
        primitive_record(model, texture_profile, mesh, primitive_index, primitive, selected_keys)
        for mesh in model.meshes
        for primitive_index, primitive in enumerate(model.shapes[mesh.shape_index].primitives)
    ]

    visibility_groups = build_visibility_groups(primitive_records, sample_limit=sample_limit)
    selected_records = [record for record in primitive_records if record["selected"]]
    unselected_records = [record for record in primitive_records if not record["selected"]]
    selected_visibility_ids = sorted({int(record["visibility_id"]) for record in selected_records})
    partially_selected_visibility_ids = [
        int(group["visibility_id"])
        for group in visibility_groups
        if group["selection_state"] == "partial"
    ]
    selected_texture_names = sorted(
        {str(record["texture_name"]) for record in selected_records if record["texture_name"] is not None}
    )
    unselected_body_like_records = [
        record
        for record in unselected_records
        if record["inferred_primitive_role"] in {"limb_variant", "core_character_surface"}
    ]

    conclusions: list[str] = []
    if selection is not None:
        conclusions.append(
            "The selected subset is an explicit primitive-key draw profile, not a parser rule."
        )
        conclusions.append(
            "Unselected skinned primitives remain in the source CMB and represent alternate draw states."
        )
    if selected_visibility_ids:
        conclusions.append(
            "The selected visibility ids form a small state set: "
            + ", ".join(f"{value:02d}" for value in selected_visibility_ids)
            + "."
        )
    if partially_selected_visibility_ids:
        conclusions.append(
            "Partial visibility groups need primitive-level selection; visibility id alone is too coarse."
        )
    if unselected_body_like_records:
        conclusions.append(
            "Exporting every skinned primitive would reintroduce duplicated hands or body-like alternates."
        )

    return {
        "format": CHARACTER_VISIBILITY_RECONSTRUCTION_FORMAT,
        "input": str(input_path),
        "source": source,
        "embedded_name": embedded_name,
        "model": {
            "name": model.name,
            "bone_count": model.bone_count,
            "mesh_count": len(model.meshes),
            "shape_count": len(model.shapes),
            "primitive_count": len(primitive_records),
        },
        "selection": selection_summary(selection, selected_records, primitive_records),
        "bone_hierarchy": bone_hierarchy_summary(model),
        "visibility_groups": visibility_groups,
        "selected_visibility_ids": selected_visibility_ids,
        "partially_selected_visibility_ids": partially_selected_visibility_ids,
        "texture_counts": texture_count_summary(primitive_records),
        "selected_texture_names": selected_texture_names,
        "unselected_body_like_primitive_count": len(unselected_body_like_records),
        "n64_player_draw_reference": n64_player_draw_reference(n64_player_lib),
        "conclusions": conclusions,
    }


def primitive_record(
    model: CmbModel,
    texture_profile: object,
    mesh: Mesh,
    primitive_index: int,
    primitive: Primitive,
    selected_keys: frozenset[tuple[int, int]],
) -> dict[str, object]:
    shape = model.shapes[mesh.shape_index]
    texture_name = primary_texture_name(model, texture_profile, mesh)
    bounds = primitive_bounds(shape, primitive)
    role = infer_primitive_role(texture_name, primitive, bounds)
    return {
        "mesh_index": mesh.index,
        "shape_index": mesh.shape_index,
        "material_index": mesh.material_index,
        "visibility_id": mesh.visibility_id,
        "primitive_index": primitive_index,
        "selected": (mesh.index, primitive_index) in selected_keys,
        "texture_name": texture_name,
        "skinning_mode": primitive.skinning_mode,
        "bone_indices": list(primitive.bone_indices),
        "triangle_count": len(primitive.indices) // 3,
        "vertex_count": len(set(primitive.indices)),
        "bounds": bounds,
        "inferred_primitive_role": role,
    }


def primary_texture_name(model: CmbModel, texture_profile: object, mesh: Mesh) -> str | None:
    material = model.materials[mesh.material_index]
    texture_index = material_primary_texture_index_for_profile(model, material, texture_profile)
    if texture_index is None or texture_index < 0 or texture_index >= len(model.textures):
        return None
    return model.textures[texture_index].name


def primitive_bounds(shape: Shape, primitive: Primitive) -> dict[str, object]:
    points = [shape.positions[index] for index in primitive.indices if index < len(shape.positions)]
    if not points:
        return {
            "min": [0.0, 0.0, 0.0],
            "max": [0.0, 0.0, 0.0],
            "span": [0.0, 0.0, 0.0],
            "center": [0.0, 0.0, 0.0],
        }
    minimum = Vec3(
        min(point.x for point in points),
        min(point.y for point in points),
        min(point.z for point in points),
    )
    maximum = Vec3(
        max(point.x for point in points),
        max(point.y for point in points),
        max(point.z for point in points),
    )
    span = Vec3(maximum.x - minimum.x, maximum.y - minimum.y, maximum.z - minimum.z)
    center = Vec3(
        (minimum.x + maximum.x) * 0.5,
        (minimum.y + maximum.y) * 0.5,
        (minimum.z + maximum.z) * 0.5,
    )
    return {
        "min": rounded_vec3(minimum),
        "max": rounded_vec3(maximum),
        "span": rounded_vec3(span),
        "center": rounded_vec3(center),
    }


def rounded_vec3(value: Vec3) -> list[float]:
    return [round(value.x, 3), round(value.y, 3), round(value.z, 3)]


def infer_primitive_role(
    texture_name: str | None,
    primitive: Primitive,
    bounds: dict[str, object],
) -> str:
    texture = texture_name or ""
    lower_texture = texture.lower()
    center = bounds.get("center")
    center_x = abs(float(center[0])) if isinstance(center, list) and center else 0.0
    bone_count = len(primitive.bone_indices)

    if "eye" in lower_texture or "mouth" in lower_texture:
        return "facial_expression_surface"
    if primitive.skinning_mode in (1, 2) and bone_count <= 3 and center_x > 500.0:
        return "limb_variant"
    if primitive.skinning_mode in (1, 2) and bone_count >= 6:
        return "core_character_surface"
    if lower_texture.startswith("p_tex"):
        return "equipment_or_prop_surface"
    if primitive.skinning_mode == 0:
        return "rigid_attachment_or_expression"
    return "skinned_character_surface"


def build_visibility_groups(
    primitive_records: list[dict[str, object]],
    *,
    sample_limit: int,
) -> list[dict[str, object]]:
    by_visibility: dict[int, list[dict[str, object]]] = defaultdict(list)
    for record in primitive_records:
        by_visibility[int(record["visibility_id"])].append(record)

    groups: list[dict[str, object]] = []
    for visibility_id, records in sorted(by_visibility.items()):
        selected_count = sum(1 for record in records if record["selected"])
        if selected_count == 0:
            selection_state = "none"
        elif selected_count == len(records):
            selection_state = "full"
        else:
            selection_state = "partial"
        role_counts = Counter(str(record["inferred_primitive_role"]) for record in records)
        texture_names = sorted(
            {str(record["texture_name"]) for record in records if record["texture_name"] is not None}
        )
        skinning_modes = sorted({int(record["skinning_mode"]) for record in records})
        bone_indices = sorted({int(bone) for record in records for bone in record["bone_indices"]})
        groups.append(
            {
                "visibility_id": visibility_id,
                "selection_state": selection_state,
                "primitive_count": len(records),
                "selected_primitive_count": selected_count,
                "mesh_indices": sorted({int(record["mesh_index"]) for record in records}),
                "triangle_count": sum(int(record["triangle_count"]) for record in records),
                "selected_triangle_count": sum(
                    int(record["triangle_count"]) for record in records if record["selected"]
                ),
                "texture_names": texture_names,
                "skinning_modes": skinning_modes,
                "bone_indices": bone_indices,
                "role_counts": dict(sorted(role_counts.items())),
                "inferred_visibility_role": infer_visibility_role(records),
                "primitive_samples": records[:sample_limit],
            }
        )
    return groups


def infer_visibility_role(records: list[dict[str, object]]) -> str:
    roles = {str(record["inferred_primitive_role"]) for record in records}
    selected_count = sum(1 for record in records if record["selected"])
    has_equipment = "equipment_or_prop_surface" in roles
    has_limb = "limb_variant" in roles
    has_core = "core_character_surface" in roles
    has_face = "facial_expression_surface" in roles

    if has_face:
        return "head_face_draw_group"
    if has_core and not has_equipment:
        return "core_body_draw_group"
    if has_limb and has_equipment:
        return "limb_equipment_variant_group"
    if has_limb:
        return "limb_variant_group"
    if has_equipment:
        return "equipment_or_prop_group"
    if selected_count:
        return "selected_character_group"
    return "unclassified_draw_group"


def selection_summary(
    selection: CharacterMeshSelection | None,
    selected_records: list[dict[str, object]],
    primitive_records: list[dict[str, object]],
) -> dict[str, object]:
    if selection is None:
        return {
            "present": False,
            "selected_primitive_count": 0,
            "source_path": None,
        }
    return {
        "present": True,
        "profile_id": selection.profile_id,
        "source_path": selection.source_path,
        "source_format": selection.source_format,
        "model_name": selection.model_name,
        "texture_orientation": selection.texture_orientation,
        "uv_orientation": selection.uv_orientation,
        "position_scale": selection.position_scale,
        "selection_primitive_key_count": len(selection.primitive_keys),
        "selected_primitive_count": len(selected_records),
        "total_primitive_count": len(primitive_records),
        "selected_mesh_indices": sorted({int(record["mesh_index"]) for record in selected_records}),
        "selected_primitive_keys": [
            {"mesh_index": mesh_index, "primitive_index": primitive_index}
            for mesh_index, primitive_index in sorted(selection.primitive_keys)
        ],
    }


def texture_count_summary(primitive_records: list[dict[str, object]]) -> dict[str, int]:
    counts = Counter(
        str(record["texture_name"]) if record["texture_name"] is not None else "<none>"
        for record in primitive_records
    )
    return dict(sorted(counts.items()))


def bone_hierarchy_summary(model: CmbModel) -> list[dict[str, object]]:
    children: dict[int, list[int]] = {bone.index: [] for bone in model.skeleton.bones}
    for bone in model.skeleton.bones:
        if bone.parent_index >= 0:
            children.setdefault(bone.parent_index, []).append(bone.index)
    return [
        {
            "index": bone.index,
            "parent_index": bone.parent_index,
            "children": sorted(children.get(bone.index, [])),
            "local_translation": rounded_vec3(bone.translation),
        }
        for bone in model.skeleton.bones
    ]


def n64_player_draw_reference(n64_player_lib: Path | None) -> dict[str, object] | None:
    if n64_player_lib is None:
        return None
    if not n64_player_lib.exists():
        raise ParseError(f"{n64_player_lib}: N64 player library reference does not exist")

    lines = n64_player_lib.read_text(encoding="utf-8", errors="replace").splitlines()
    patterns = {
        "model_group_table": "gPlayerModelTypes[PLAYER_MODELGROUP_MAX]",
        "default_model_group": "/* PLAYER_MODELGROUP_DEFAULT */",
        "left_hand_open_dlists": "gPlayerLeftHandOpenDLs",
        "right_hand_open_dlists": "sPlayerRightHandOpenDLs",
        "draw_impl": "void Player_DrawImpl",
        "eye_texture_segment": "sEyeTextures[eyesIndex]",
        "mouth_texture_segment": "sMouthTextures[mouthIndex]",
        "override_default": "s32 Player_OverrideLimbDrawGameplayDefault",
        "left_hand_override": "limbIndex == PLAYER_LIMB_L_HAND",
        "right_hand_override": "limbIndex == PLAYER_LIMB_R_HAND",
        "sheath_override": "limbIndex == PLAYER_LIMB_SHEATH",
        "waist_override": "limbIndex == PLAYER_LIMB_WAIST",
    }
    anchors = {name: first_matching_line(lines, pattern) for name, pattern in patterns.items()}
    return {
        "path": str(n64_player_lib),
        "anchors": anchors,
        "snippets": {
            "default_model_group": line_snippet(lines, anchors["default_model_group"], after=4),
            "left_hand_open_dlists": line_snippet(lines, anchors["left_hand_open_dlists"], after=5),
            "right_hand_open_dlists": line_snippet(lines, anchors["right_hand_open_dlists"], after=5),
            "face_texture_segments": line_snippet(lines, anchors["eye_texture_segment"], before=3, after=4),
            "limb_override_switch": line_snippet(lines, anchors["left_hand_override"], before=2, after=48),
        },
        "interpretation": [
            "N64 Player draw uses a fixed skeleton plus per-limb display-list replacement.",
            "The default model group resolves open left hand, open right hand, sheath, and waist slots.",
            "Eyes and mouth are dynamic texture slots, not separate always-visible character bodies.",
            "OOT3D character CMB visibility groups should therefore be treated as draw-state choices.",
        ],
    }


def first_matching_line(lines: list[str], pattern: str) -> dict[str, object] | None:
    for index, line in enumerate(lines, start=1):
        if pattern in line:
            return {"line": index, "text": line.strip()}
    return None


def line_snippet(
    lines: list[str],
    anchor: dict[str, object] | None,
    *,
    before: int = 0,
    after: int = 0,
) -> list[dict[str, object]]:
    if anchor is None or not isinstance(anchor.get("line"), int):
        return []
    anchor_index = int(anchor["line"]) - 1
    start = max(0, anchor_index - before)
    end = min(len(lines), anchor_index + after + 1)
    return [
        {"line": index + 1, "text": lines[index].rstrip()}
        for index in range(start, end)
    ]
