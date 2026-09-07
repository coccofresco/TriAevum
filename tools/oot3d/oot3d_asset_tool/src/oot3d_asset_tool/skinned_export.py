from __future__ import annotations

import hashlib
import json
import math
import re
from collections import Counter
from pathlib import Path

from .binary import BinaryView, ParseError
from .cmb import CmbModel, Color, Material, Primitive, Shape, Texture, Vec2, Vec3
from .csab_tracks import is_cmb_file
from .legacy_fast_resource import Matrix4, skeleton_world_transforms, transform_direction, transform_position
from .skinning_layout_audit import (
    SKINNING_INDEX_SLOT,
    SKINNING_WEIGHT_SLOT,
    attribute_row,
    choose_influence_width,
    read_vld,
    vertex_attribute_info,
)
from .zar import ZarArchive, ZarFile
from .zsi import ZsiFile

BATCH_MANIFEST_NAME = "skinned_bind_pose_batch_manifest.json"


def export_skinned_bind_pose(
    path: Path,
    output_path: Path,
    *,
    cmb_index: int = 0,
    cmb_name: str | None = None,
    sample_limit: int = 100,
) -> dict[str, object]:
    data, source, embedded_name = load_cmb_payload(path, cmb_index, cmb_name)
    export = export_cmb_skinned_bind_pose(
        data,
        source=source,
        embedded_name=embedded_name,
        sample_limit=sample_limit,
    )
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        json.dumps(export, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    return export


def batch_export_actor_skinned_bind_poses(
    actor_root: Path,
    output_dir: Path,
    *,
    sample_limit: int = 25,
) -> Path:
    if not actor_root.is_dir():
        raise ParseError(f"{actor_root}: expected an extracted OOT3D actor directory")

    output_dir.mkdir(parents=True, exist_ok=True)
    export_dir = output_dir / "exports"
    export_dir.mkdir(parents=True, exist_ok=True)

    paths = sorted(path for path in actor_root.rglob("*") if path.is_file())
    loose_cmb_paths = [path for path in paths if path.suffix.lower() == ".cmb"]
    zar_paths = [path for path in paths if path.suffix.lower() == ".zar"]

    counts: Counter[str] = Counter()
    influence_width_rows: Counter[str] = Counter()
    nonzero_influence_rows: Counter[str] = Counter()
    records: list[dict[str, object]] = []
    parse_errors: list[dict[str, object]] = []

    def add_model(
        data: bytes,
        *,
        container_path: Path,
        container_type: str,
        embedded_name: str | None = None,
        embedded_index: int | None = None,
        embedded_type: str | None = None,
    ) -> None:
        counts["considered_cmb"] += 1
        source = f"{container_path}!{embedded_name}" if embedded_name else str(container_path)
        relative_container = container_path.relative_to(actor_root).as_posix()
        try:
            export = export_cmb_skinned_bind_pose(
                data,
                source=source,
                embedded_name=embedded_name,
                sample_limit=sample_limit,
            )
        except Exception as exc:
            counts["failed"] += 1
            parse_errors.append(
                {
                    "container_path": relative_container,
                    "container_type": container_type,
                    "embedded_name": embedded_name,
                    "embedded_index": embedded_index,
                    "embedded_type": embedded_type,
                    "message": str(exc),
                }
            )
            return

        counts["parsed"] += 1
        export_counts = export["counts"]
        if not isinstance(export_counts, dict):
            raise ParseError(f"{source}: skinned export counts are not a JSON object")
        if int(export_counts["skinned_primitive_count"]) <= 0:
            counts["skipped_unskinned"] += 1
            return

        counts["exported"] += 1
        counts["mesh_count"] += int(export_counts["mesh_count"])
        counts["skinned_primitive_count"] += int(export_counts["skinned_primitive_count"])
        counts["mode_1_primitive_count"] += int(export_counts["mode_1_primitive_count"])
        counts["mode_2_primitive_count"] += int(export_counts["mode_2_primitive_count"])
        counts["rigid_primitives_inside_skinned_meshes"] += int(
            export_counts["rigid_primitives_inside_skinned_meshes"]
        )
        counts["skinned_vertex_rows"] += int(export_counts["skinned_vertex_rows"])
        counts["skeleton_bone_count"] += int(export_counts["skeleton_bone_count"])
        counts["finite_bind_world_matrix_entries"] += int(
            export_counts["finite_bind_world_matrix_entries"]
        )
        counts["validation_error_count"] += int(export_counts["validation_error_count"])
        merge_export_counts(influence_width_rows, export_counts.get("influence_width_rows", {}))
        merge_export_counts(nonzero_influence_rows, export_counts.get("nonzero_influence_rows", {}))

        safe_name = skinned_export_filename(relative_container, embedded_name)
        output_path = export_dir / safe_name
        output_path.write_text(
            json.dumps(export, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )

        records.append(
            {
                "status": "exported",
                "container_path": relative_container,
                "container_type": container_type,
                "embedded_name": embedded_name,
                "embedded_index": embedded_index,
                "embedded_type": embedded_type,
                "model_name": export["model_name"],
                "bone_count": export["bone_count"],
                "output": str(output_path),
                "counts": export_counts,
            }
        )

    for path in loose_cmb_paths:
        add_model(
            path.read_bytes(),
            container_path=path,
            container_type="cmb",
        )

    for path in zar_paths:
        try:
            archive = ZarArchive.from_path(path)
        except Exception as exc:
            counts["failed_archives"] += 1
            parse_errors.append(
                {
                    "container_path": path.relative_to(actor_root).as_posix(),
                    "container_type": "zar",
                    "embedded_name": None,
                    "message": str(exc),
                }
            )
            continue
        for file in archive.files:
            if not is_cmb_file(file):
                continue
            add_model(
                archive.read_file(file),
                container_path=path,
                container_type="zar",
                embedded_name=file.name,
                embedded_index=file.index,
                embedded_type=file.type_name,
            )

    manifest = {
        "format": "oot3d_skinned_bind_pose_batch_v1",
        "actor_root": str(actor_root),
        "output_dir": str(output_dir),
        "export_dir": str(export_dir),
        "file_count": len(paths),
        "archive_count": len(zar_paths),
        "loose_cmb_count": len(loose_cmb_paths),
        "counts": {
            "considered_cmb": counts["considered_cmb"],
            "parsed": counts["parsed"],
            "exported": counts["exported"],
            "skipped_unskinned": counts["skipped_unskinned"],
            "failed": counts["failed"],
            "failed_archives": counts["failed_archives"],
            "mesh_count": counts["mesh_count"],
            "skinned_primitive_count": counts["skinned_primitive_count"],
            "mode_1_primitive_count": counts["mode_1_primitive_count"],
            "mode_2_primitive_count": counts["mode_2_primitive_count"],
            "rigid_primitives_inside_skinned_meshes": counts[
                "rigid_primitives_inside_skinned_meshes"
            ],
            "skinned_vertex_rows": counts["skinned_vertex_rows"],
            "skeleton_bone_count": counts["skeleton_bone_count"],
            "finite_bind_world_matrix_entries": counts["finite_bind_world_matrix_entries"],
            "validation_error_count": counts["validation_error_count"],
        },
        "influence_width_rows": dict(sorted(influence_width_rows.items())),
        "nonzero_influence_rows": dict(sorted(nonzero_influence_rows.items())),
        "records": records,
        "parse_error_count": len(parse_errors),
        "parse_errors": parse_errors,
    }
    manifest_path = output_dir / BATCH_MANIFEST_NAME
    manifest_path.write_text(
        json.dumps(manifest, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    return manifest_path


def export_cmb_skinned_bind_pose(
    data: bytes,
    *,
    source: str,
    embedded_name: str | None = None,
    sample_limit: int = 100,
) -> dict[str, object]:
    model = CmbModel.parse(data, source)
    view = BinaryView(data, source)
    sklm_off = view.u32(0x30)
    vatr_off = view.u32(0x38)
    shp_off = sklm_off + view.u32(sklm_off + 0x0C)
    if view.bytes(shp_off, 4) != b"shp ":
        raise ParseError(f"{source}: expected SHP chunk at 0x{shp_off:x}")
    if view.bytes(vatr_off, 4) != b"vatr":
        raise ParseError(f"{source}: expected VATR chunk at 0x{vatr_off:x}")

    shape_count = view.u32(shp_off + 0x08)
    sepd_offsets = [shp_off + view.u16(shp_off + 0x10 + index * 2) for index in range(shape_count)]
    vlds = [read_vld(view, vatr_off, slot) for slot in range(8)]
    bone_transforms = skeleton_world_transforms(model.skeleton)
    skeleton_bones = skeleton_bone_records(model, bone_transforms)

    counts: Counter[str] = Counter()
    validation_errors: list[dict[str, object]] = []
    meshes: list[dict[str, object]] = []
    vertex_samples: list[dict[str, object]] = []

    for mesh in model.meshes:
        if mesh.shape_index >= len(model.shapes):
            validation_errors.append(
                {
                    "mesh_index": mesh.index,
                    "shape_index": mesh.shape_index,
                    "reason": "mesh_references_missing_shape",
                }
            )
            continue

        shape = model.shapes[mesh.shape_index]
        if not shape_has_bind_pose_primitives(shape):
            continue

        sepd_off = sepd_offsets[shape.index]
        vertex_count = max_vertex_index(shape) + 1
        index_attr = vertex_attribute_info(
            view,
            vatr_off,
            sepd_offsets,
            sepd_off,
            SKINNING_INDEX_SLOT,
            vlds,
            vertex_count,
        )
        weight_attr = vertex_attribute_info(
            view,
            vatr_off,
            sepd_offsets,
            sepd_off,
            SKINNING_WEIGHT_SLOT,
            vlds,
            vertex_count,
        )

        mesh_primitives: list[dict[str, object]] = []
        for primitive_index, primitive in enumerate(shape.primitives):
            if primitive.skinning_mode not in (0, 1, 2):
                counts["unsupported_primitives"] += 1
                continue
            if primitive.skinning_mode == 0:
                counts["rigid_primitives"] += 1
                counts["rigid_primitives_inside_skinned_meshes"] += 1
            else:
                counts["skinned_primitives"] += 1
            counts[f"mode_{primitive.skinning_mode}_primitives"] += 1
            influences, primitive_errors = primitive_vertex_influences(
                view,
                shape,
                primitive,
                index_attr,
                weight_attr,
                bone_transforms,
            )
            validation_errors.extend(
                {
                    "mesh_index": mesh.index,
                    "shape_index": shape.index,
                    "primitive_index": primitive_index,
                    **error,
                }
                for error in primitive_errors
            )
            source_vertex_indices = sorted(set(primitive.indices))
            source_to_local_index = {
                vertex_index: local_index
                for local_index, vertex_index in enumerate(source_vertex_indices)
            }
            local_indices = [
                source_to_local_index[vertex_index]
                for vertex_index in primitive.indices
            ]
            if len(local_indices) % 3 != 0:
                validation_errors.append(
                    {
                        "mesh_index": mesh.index,
                        "shape_index": shape.index,
                        "primitive_index": primitive_index,
                        "reason": "primitive_index_count_not_triangular",
                        "index_count": len(local_indices),
                    }
                )
            for record in influences:
                counts["skinned_vertex_rows"] += 1
                counts[f"influence_width_{len(record['influences'])}"] += 1
                counts[f"nonzero_influences_{record['nonzero_influence_count']}"] += 1
                if len(vertex_samples) < sample_limit:
                    vertex_samples.append(
                        {
                            "mesh_index": mesh.index,
                            "shape_index": shape.index,
                            "primitive_index": primitive_index,
                            **record,
                        }
                    )

            mesh_primitives.append(
                {
                    "primitive_index": primitive_index,
                    "skinning_mode": primitive.skinning_mode,
                    "triangle_count": len(primitive.indices) // 3,
                    "index_count": len(local_indices),
                    "unique_vertex_count": len(influences),
                    "bone_palette": list(primitive.bone_indices),
                    "influence_width": influence_width_for_primitive(
                        primitive,
                        index_attr,
                        weight_attr,
                    ),
                    "indices": local_indices,
                    "source_indices": list(primitive.indices),
                    "vertices": influences,
                }
            )

        meshes.append(
            {
                "mesh_index": mesh.index,
                "shape_index": shape.index,
                "material_index": mesh.material_index,
                "visibility_id": mesh.visibility_id,
                "primitives": mesh_primitives,
            }
        )

    finite_bind_world_matrix_entries = sum(
        1
        for bone in skeleton_bones
        for row in bone["bind_world_matrix"]  # type: ignore[index]
        for value in row
        if isinstance(value, float) and math.isfinite(value)
    )
    if finite_bind_world_matrix_entries != model.bone_count * 16:
        validation_errors.append(
            {
                "reason": "non_finite_skeleton_bind_world_matrix",
                "finite_entries": finite_bind_world_matrix_entries,
                "expected_entries": model.bone_count * 16,
            }
        )

    export = {
        "format": "oot3d_skinned_bind_pose_export_v1",
        "source": source,
        "embedded_name": embedded_name,
        "model_name": model.name,
        "bone_count": model.bone_count,
        "skeleton": model.skeleton.summary(),
        "skeleton_bones": skeleton_bones,
        "textures": texture_records(model.textures),
        "materials": material_records(model.materials, model.textures),
        "counts": {
            "mesh_count": len(meshes),
            "rigid_primitive_count": counts["rigid_primitives"],
            "skinned_primitive_count": counts["skinned_primitives"],
            "mode_0_primitive_count": counts["mode_0_primitives"],
            "mode_1_primitive_count": counts["mode_1_primitives"],
            "mode_2_primitive_count": counts["mode_2_primitives"],
            "rigid_primitives_inside_skinned_meshes": counts[
                "rigid_primitives_inside_skinned_meshes"
            ],
            "skinned_vertex_rows": counts["skinned_vertex_rows"],
            "influence_width_rows": {
                key.removeprefix("influence_width_"): value
                for key, value in sorted(counts.items())
                if key.startswith("influence_width_")
            },
            "nonzero_influence_rows": {
                key.removeprefix("nonzero_influences_"): value
                for key, value in sorted(counts.items())
                if key.startswith("nonzero_influences_")
            },
            "skeleton_bone_count": len(skeleton_bones),
            "finite_bind_world_matrix_entries": finite_bind_world_matrix_entries,
            "validation_error_count": len(validation_errors),
        },
        "vertex_samples": vertex_samples,
        "meshes": meshes,
        "validation_errors": validation_errors[:sample_limit],
    }
    return export


def load_cmb_payload(
    path: Path,
    cmb_index: int,
    cmb_name: str | None,
) -> tuple[bytes, str, str | None]:
    suffix = path.suffix.lower()
    if suffix == ".cmb":
        return path.read_bytes(), str(path), None
    if suffix == ".zar":
        archive = ZarArchive.from_path(path)
        cmb_files = [file for file in archive.files if is_cmb_file(file)]
        if not cmb_files:
            raise ParseError(f"{path}: ZAR archive contains no CMB files")
        selected = select_named_or_indexed_cmb(path, cmb_files, cmb_index, cmb_name)
        return archive.read_file(selected), f"{path}!{selected.name}", selected.name
    if suffix == ".zsi":
        zsi = ZsiFile.from_path(path)
        cmbs = zsi.embedded_cmbs()
        if not cmbs:
            raise ParseError(f"{path}: ZSI file contains no embedded CMB")
        if cmb_name:
            normalized_name = cmb_name.replace("\\", "/")
            for cmb in cmbs:
                if cmb.model.name in (cmb_name, normalized_name):
                    return (
                        zsi.data[cmb.offset : cmb.offset + cmb.size],
                        cmb.model.source,
                        cmb.model.name,
                    )
            raise ParseError(f"{path}: no embedded CMB named {cmb_name!r}")
        if cmb_index < 0 or cmb_index >= len(cmbs):
            raise ParseError(f"{path}: CMB index {cmb_index} out of range 0..{len(cmbs)-1}")
        cmb = cmbs[cmb_index]
        return zsi.data[cmb.offset : cmb.offset + cmb.size], cmb.model.source, cmb.model.name
    raise ParseError(f"{path}: expected .cmb, .zar, or .zsi input")


def select_named_or_indexed_cmb(
    path: Path,
    cmb_files: list[ZarFile],
    cmb_index: int,
    cmb_name: str | None,
) -> ZarFile:
    if cmb_name:
        normalized_name = cmb_name.replace("\\", "/")
        for file in cmb_files:
            if (
                file.name == cmb_name
                or file.name == normalized_name
                or Path(file.name).name == cmb_name
                or Path(file.name).name == normalized_name
            ):
                return file
        raise ParseError(f"{path}: no embedded CMB named {cmb_name!r}")
    if cmb_index < 0 or cmb_index >= len(cmb_files):
        raise ParseError(f"{path}: CMB index {cmb_index} out of range 0..{len(cmb_files)-1}")
    return cmb_files[cmb_index]


def shape_has_bind_pose_primitives(shape: Shape) -> bool:
    return any(primitive.indices and primitive.skinning_mode in (0, 1, 2) for primitive in shape.primitives)


def max_vertex_index(shape: Shape) -> int:
    return max(
        (max(primitive.indices) for primitive in shape.primitives if primitive.indices),
        default=-1,
    )


def primitive_vertex_influences(
    view: BinaryView,
    shape: Shape,
    primitive: Primitive,
    index_attr: dict[str, object],
    weight_attr: dict[str, object],
    bone_transforms: tuple[Matrix4, ...],
) -> tuple[list[dict[str, object]], list[dict[str, object]]]:
    records: list[dict[str, object]] = []
    errors: list[dict[str, object]] = []
    width = influence_width_for_primitive(primitive, index_attr, weight_attr)
    if width is None:
        return records, [{"reason": "cannot_infer_influence_width"}]

    for vertex_index in sorted(set(primitive.indices)):
        influences = vertex_influences(
            view,
            primitive,
            index_attr,
            weight_attr,
            width,
            vertex_index,
        )
        weight_sum = round(sum(float(item["weight_percent"]) for item in influences), 4)
        if not math.isclose(weight_sum, 100.0, abs_tol=0.001):
            errors.append(
                {
                    "reason": "weight_sum_not_100",
                    "vertex_index": vertex_index,
                    "weight_sum": weight_sum,
                }
            )
        invalid_indices = [
            item
            for item in influences
            if float(item["weight_percent"]) > 0.0
            and int(item["palette_index"]) >= len(primitive.bone_indices)
        ]
        if invalid_indices:
            errors.append(
                {
                    "reason": "palette_index_out_of_range",
                    "vertex_index": vertex_index,
                    "invalid_indices": invalid_indices,
                }
            )

        source_position = shape.positions[vertex_index]
        source_normal = (
            shape.normals[vertex_index]
            if vertex_index < len(shape.normals)
            else Vec3(0.0, 0.0, 1.0)
        )
        source_uv0 = (
            shape.uv0[vertex_index]
            if vertex_index < len(shape.uv0)
            else Vec2(0.0, 0.0)
        )
        source_color = (
            shape.colors[vertex_index]
            if vertex_index < len(shape.colors)
            else Color(255, 255, 255, 255)
        )
        preview_position, preview_normal = weighted_bind_pose_preview(
            source_position,
            source_normal,
            influences,
            bone_transforms,
        )
        if not vec3_is_finite(preview_position) or not vec3_is_finite(preview_normal):
            errors.append(
                {
                    "reason": "non_finite_bind_pose_preview",
                    "vertex_index": vertex_index,
                }
            )

        records.append(
            {
                "vertex_index": vertex_index,
                "source_position": vec3_to_list(source_position),
                "source_normal": vec3_to_list(source_normal),
                "source_uv0": vec2_to_list(source_uv0),
                "source_color_rgba": color_to_list(source_color),
                "bind_pose_preview_position": vec3_to_list(preview_position),
                "bind_pose_preview_normal": vec3_to_list(preview_normal),
                "weight_sum_percent": weight_sum,
                "nonzero_influence_count": sum(
                    1
                    for influence in influences
                    if float(influence["weight_percent"]) > 0.000001
                ),
                "influences": influences,
            }
        )
    return records, errors


def texture_records(textures: tuple[Texture, ...]) -> list[dict[str, object]]:
    return [
        {
            "index": texture.index,
            "name": texture.name,
            "width": texture.width,
            "height": texture.height,
            "format": f"0x{texture.texture_format:x}",
            "data_type": f"0x{texture.data_type:x}",
            "data_size": len(texture.data),
        }
        for texture in textures
    ]


def material_records(
    materials: tuple[Material, ...],
    textures: tuple[Texture, ...],
) -> list[dict[str, object]]:
    return [
        {
            "index": material.index,
            "texture_indices": list(material.texture_indices),
            "texture_names": material_texture_names(material, textures),
            "texture_mappers_used": material.texture_mappers_used,
            "texture_coords_used": material.texture_coords_used,
            "texture_mappers": [
                {
                    "index": mapper.index,
                    "min_filter": f"0x{mapper.min_filter:x}",
                    "mag_filter": f"0x{mapper.mag_filter:x}",
                    "wrap_s": f"0x{mapper.wrap_s:x}",
                    "wrap_t": f"0x{mapper.wrap_t:x}",
                }
                for mapper in material.texture_mappers
            ],
            "texture_coords": [
                {
                    "matrix_mode": coord.matrix_mode,
                    "reference_camera": coord.reference_camera,
                    "mapping_method": coord.mapping_method,
                    "coordinate_index": coord.coordinate_index,
                    "scale": vec2_to_list(coord.scale),
                    "rotation": round_float(coord.rotation),
                    "translation": vec2_to_list(coord.translation),
                }
                for coord in material.texture_coords
            ],
            "cull_back": material.cull_back,
            "alpha_test": material.alpha_test,
            "alpha_reference": material.alpha_reference,
            "alpha_function": f"0x{material.alpha_function:x}",
            "depth_test": material.depth_test,
            "depth_write": material.depth_write,
            "depth_function": f"0x{material.depth_function:x}",
            "blend_mode": material.blend_mode,
            "blend_src": f"0x{material.blend_src:x}",
            "blend_dst": f"0x{material.blend_dst:x}",
            "blend_equation": f"0x{material.blend_equation:x}",
            "color_blend_src": f"0x{material.color_blend_src:x}",
            "color_blend_dst": f"0x{material.color_blend_dst:x}",
            "color_blend_equation": f"0x{material.color_blend_equation:x}",
            "blend_color_alpha": round_float(material.blend_color_alpha),
        }
        for material in materials
    ]


def material_texture_names(
    material: Material,
    textures: tuple[Texture, ...],
) -> list[str | None]:
    names: list[str | None] = []
    for texture_index in material.texture_indices:
        if 0 <= texture_index < len(textures):
            names.append(textures[texture_index].name)
        else:
            names.append(None)
    return names


def influence_width_for_primitive(
    primitive: Primitive,
    index_attr: dict[str, object],
    weight_attr: dict[str, object],
) -> int | None:
    if primitive.skinning_mode == 0:
        return 1 if len(primitive.bone_indices) == 1 else None
    if primitive.skinning_mode == 1:
        return 1
    if primitive.skinning_mode == 2:
        return choose_influence_width(index_attr, weight_attr)
    return None


def vertex_influences(
    view: BinaryView,
    primitive: Primitive,
    index_attr: dict[str, object],
    weight_attr: dict[str, object],
    width: int,
    vertex_index: int,
) -> list[dict[str, object]]:
    if primitive.skinning_mode == 0:
        global_bone = primitive.bone_indices[0] if primitive.bone_indices else None
        return [
            {
                "palette_index": 0,
                "bone_index": global_bone,
                "weight_percent": 100.0,
                "weight": 1.0,
            }
        ]

    palette_indices = [
        int(round(value))
        for value in attribute_row(view, index_attr, width, vertex_index)
    ]
    if primitive.skinning_mode == 1:
        weight_percents = [100.0]
    else:
        weights = attribute_row(view, weight_attr, width, vertex_index)
        weight_percents = [
            weight * 100.0 if weight_attr["kind"] == "data" else weight
            for weight in weights
        ]

    influences: list[dict[str, object]] = []
    for palette_index, weight_percent in zip(palette_indices, weight_percents):
        global_bone = (
            primitive.bone_indices[palette_index]
            if 0 <= palette_index < len(primitive.bone_indices)
            else None
        )
        influences.append(
            {
                "palette_index": palette_index,
                "bone_index": global_bone,
                "weight_percent": round_float(weight_percent),
                "weight": round_float(weight_percent / 100.0),
            }
        )
    return influences


def weighted_bind_pose_preview(
    position: Vec3,
    normal: Vec3,
    influences: list[dict[str, object]],
    bone_transforms: tuple[Matrix4, ...],
) -> tuple[Vec3, Vec3]:
    out_position = Vec3(0.0, 0.0, 0.0)
    out_normal = Vec3(0.0, 0.0, 0.0)
    for influence in influences:
        bone_index = influence["bone_index"]
        weight = float(influence["weight"])
        if bone_index is None or weight <= 0.0:
            continue
        if bone_index < 0 or bone_index >= len(bone_transforms):
            continue
        transformed_position = transform_position(bone_transforms[bone_index], position)
        transformed_normal = transform_direction(bone_transforms[bone_index], normal)
        out_position = Vec3(
            out_position.x + transformed_position.x * weight,
            out_position.y + transformed_position.y * weight,
            out_position.z + transformed_position.z * weight,
        )
        out_normal = Vec3(
            out_normal.x + transformed_normal.x * weight,
            out_normal.y + transformed_normal.y * weight,
            out_normal.z + transformed_normal.z * weight,
        )
    out_normal = normalize_or_default(out_normal, normal)
    return out_position, out_normal


def normalize_or_default(value: Vec3, default: Vec3) -> Vec3:
    length = math.sqrt(value.x * value.x + value.y * value.y + value.z * value.z)
    if length <= 0.000001:
        return default
    return Vec3(value.x / length, value.y / length, value.z / length)


def vec3_to_list(value: Vec3) -> list[float]:
    return [round_float(value.x), round_float(value.y), round_float(value.z)]


def vec2_to_list(value: Vec2) -> list[float]:
    return [round_float(value.x), round_float(value.y)]


def color_to_list(value: Color) -> list[int]:
    return [value.r, value.g, value.b, value.a]


def vec3_is_finite(value: Vec3) -> bool:
    return math.isfinite(value.x) and math.isfinite(value.y) and math.isfinite(value.z)


def round_float(value: float) -> float:
    if math.isclose(value, round(value), abs_tol=0.00001):
        return float(round(value))
    return round(value, 6)


def merge_export_counts(target: Counter[str], source: object) -> None:
    if not isinstance(source, dict):
        return
    for key, value in source.items():
        target[str(key)] += int(value)


def skinned_export_filename(container_path: str, embedded_name: str | None) -> str:
    base = container_path if embedded_name is None else f"{container_path}_{embedded_name}"
    digest = hashlib.sha1(base.encode("utf-8")).hexdigest()[:10]
    safe = re.sub(r"[^A-Za-z0-9_.-]+", "_", base).strip("._")
    if not safe:
        safe = "skinned_cmb"
    return f"{safe}_{digest}.json"


def skeleton_bone_records(
    model: CmbModel,
    bone_transforms: tuple[Matrix4, ...],
) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    for bone in model.skeleton.bones:
        matrix = bone_transforms[bone.index]
        records.append(
            {
                "index": bone.index,
                "parent_index": bone.parent_index,
                "scale": vec3_to_list(bone.scale),
                "rotation": vec3_to_list(bone.rotation),
                "translation": vec3_to_list(bone.translation),
                "bind_world_matrix": matrix_to_lists(matrix),
                "bind_world_translation": [
                    round_float(matrix[0][3]),
                    round_float(matrix[1][3]),
                    round_float(matrix[2][3]),
                ],
            }
        )
    return records


def matrix_to_lists(matrix: Matrix4) -> list[list[float]]:
    return [[round_float(value) for value in row] for row in matrix]
