from __future__ import annotations

import json
import math
from collections import Counter
from pathlib import Path
from xml.sax.saxutils import escape

from .binary import ParseError
from .cmb import CmbModel, Color, Material, Primitive, Shape, Texture, Vec2, Vec3
from .character_mesh_selection import (
    load_character_mesh_selection,
    validate_character_mesh_selection,
)
from .legacy_fast_resource import (
    TEXTURE_ORIENTATION_FLIP_X,
    TEXTURE_ORIENTATION_FLIP_XY,
    TEXTURE_ORIENTATION_FLIP_Y,
    TEXTURE_ORIENTATION_NORMAL,
    ConvertedResource,
    LegacyFastResourceOptions,
    LegacyFastResourceVertex,
    batch_triangles,
    chunk_triangles,
    export_materials,
    export_textures,
    integer_repeat_offset,
    material_primary_texture_index_for_profile,
    material_texture_mapper,
    mesh_texture_export_profile,
    normal_to_n64,
    normalize_resource_root,
    normalize_uv_orientation,
    round_s16,
    skeleton_world_transforms,
    transform_direction,
    transform_position,
    transform_uv,
    triangles_xml,
    vertex_xml,
    write_text,
    Matrix4,
)
from .skinned_export import export_cmb_skinned_bind_pose, load_cmb_payload

NATIVE_MANIFEST_FORMAT = "oot3d_skinned_bind_pose_native_export_v1"
MAX_F3D_VERTICES = 32


def round_float(value: float) -> float:
    if math.isclose(value, round(value), abs_tol=0.00001):
        return float(round(value))
    return round(value, 6)


class Bounds:
    def __init__(self) -> None:
        self.count = 0
        self.min_x = math.inf
        self.min_y = math.inf
        self.min_z = math.inf
        self.max_x = -math.inf
        self.max_y = -math.inf
        self.max_z = -math.inf

    def add(self, value: Vec3) -> None:
        self.count += 1
        self.min_x = min(self.min_x, value.x)
        self.min_y = min(self.min_y, value.y)
        self.min_z = min(self.min_z, value.z)
        self.max_x = max(self.max_x, value.x)
        self.max_y = max(self.max_y, value.y)
        self.max_z = max(self.max_z, value.z)

    def to_json(self) -> dict[str, object]:
        if self.count == 0:
            return {"count": 0, "min": None, "max": None, "span": None}
        return {
            "count": self.count,
            "min": [
                round_float(self.min_x),
                round_float(self.min_y),
                round_float(self.min_z),
            ],
            "max": [
                round_float(self.max_x),
                round_float(self.max_y),
                round_float(self.max_z),
            ],
            "span": [
                round_float(self.max_x - self.min_x),
                round_float(self.max_y - self.min_y),
                round_float(self.max_z - self.min_z),
            ],
        }


def export_skinned_bind_pose_native(
    path: Path,
    output_dir: Path,
    *,
    resource_root: str,
    symbol: str,
    cmb_index: int = 0,
    cmb_name: str | None = None,
    sample_limit: int = 100,
    selection_manifest: Path | None = None,
    texture_orientation: str | None = None,
    uv_orientation: str = TEXTURE_ORIENTATION_NORMAL,
) -> dict[str, object]:
    data, source, embedded_name = load_cmb_payload(path, cmb_index, cmb_name)
    model = CmbModel.parse(data, source)
    selection = (
        load_character_mesh_selection(selection_manifest)
        if selection_manifest is not None
        else None
    )
    if selection is not None:
        validate_character_mesh_selection(selection, model)
    texture_orientation = (
        texture_orientation
        if texture_orientation is not None
        else selection.texture_orientation
        if selection is not None
        else None
    )
    uv_orientation = selection.uv_orientation if selection is not None and selection.uv_orientation else uv_orientation
    bind_pose = export_cmb_skinned_bind_pose(
        data,
        source=source,
        embedded_name=embedded_name,
        sample_limit=sample_limit,
    )
    if bind_pose.get("format") != "oot3d_skinned_bind_pose_export_v1":
        raise ParseError(f"{source}: unexpected skinned bind-pose export format")

    options = LegacyFastResourceOptions(
        output_dir=output_dir,
        resource_root=resource_root,
        symbol=symbol,
        include_textures=True,
        allow_nonstatic=True,
        texture_orientation=texture_orientation,
        uv_orientation=uv_orientation,
    )
    profile = mesh_texture_export_profile(model, options)
    normalized_root = normalize_resource_root(resource_root)
    output_dir.mkdir(parents=True, exist_ok=True)

    resources: list[ConvertedResource] = []
    texture_paths = export_textures(model, options, profile, resources)
    material_paths = export_materials(model, options, profile, texture_paths, resources)

    bind_pose_mesh_by_index = {
        int(mesh.get("mesh_index", -1)): mesh
        for mesh in bind_pose_meshes(bind_pose)
        if isinstance(mesh.get("mesh_index"), int)
    }
    bone_transforms = skeleton_world_transforms(model.skeleton)
    mesh_calls: list[str] = []
    mesh_records: list[dict[str, object]] = []
    native_counts: Counter[str] = Counter()
    issues: list[dict[str, object]] = []
    selection_primitive_keys = selection.primitive_keys if selection is not None else None
    selection_mesh_indices = selection.mesh_indices if selection is not None else None
    for mesh in model.meshes:
        if selection_mesh_indices is not None and mesh.index not in selection_mesh_indices:
            native_counts["filtered_mesh"] += 1
            continue
        mesh_call = export_native_model_bind_pose_mesh(
            mesh,
            bind_pose_mesh_by_index.get(mesh.index),
            model,
            bone_transforms,
            output_dir=output_dir,
            resource_root=normalized_root,
            symbol=symbol,
            profile=profile,
            material_paths=material_paths,
            resources=resources,
            native_counts=native_counts,
            issues=issues,
            sample_limit=sample_limit,
            selection_primitive_keys=selection_primitive_keys,
        )
        if mesh_call is not None:
            mesh_calls.append(mesh_call)
            mesh_records.append(native_mesh_record(model, mesh, mesh_call, selection_primitive_keys))

    top_display_list = f"{normalized_root}/{symbol}"
    top_file = output_dir / symbol
    display_profiles, visibility_groups = export_native_bind_pose_draw_sets(
        mesh_records,
        output_dir=output_dir,
        resource_root=normalized_root,
        symbol=symbol,
        resources=resources,
    )
    diagnostic_profile = next(
        (profile for profile in display_profiles if profile.get("name") == "all_meshes"),
        display_profiles[0] if display_profiles else {},
    )
    lines = ["<DisplayList Version=\"0\">"]
    for mesh_call in mesh_calls:
        lines.append(f"\t<CallDisplayList Path=\"{escape(mesh_call)}\"/>")
    lines.append("\t<EndDisplayList/>")
    lines.append("</DisplayList>")
    write_text(top_file, "\n".join(lines) + "\n")
    resources.append(ConvertedResource("DisplayList", top_display_list, str(top_file)))
    native_counts["top_display_list"] += 1
    selected_material_indices = sorted(
        {
            int(record["material_index"])
            for record in mesh_records
            if isinstance(record.get("material_index"), int) and int(record["material_index"]) >= 0
        }
    )
    selected_material_display_lists = [
        {
            "material_index": material_index,
            "path": material_paths[material_index],
        }
        for material_index in selected_material_indices
        if material_index in material_paths
    ]

    manifest = {
        "format": NATIVE_MANIFEST_FORMAT,
        "source": source,
        "embedded_name": embedded_name,
        "model_name": model.name,
        "resource_root": normalized_root,
        "symbol": symbol,
        "top_display_list": top_display_list,
        "selection_source": selection.source_path if selection is not None else None,
        "selection_profile_id": selection.profile_id if selection is not None else None,
        "selection_model_name": selection.model_name if selection is not None else None,
        "selection_primitive_keys": [
            {"mesh_index": mesh_index, "primitive_index": primitive_index}
            for mesh_index, primitive_index in sorted(selection_primitive_keys or ())
        ],
        "diagnostic_display_profile": diagnostic_profile.get("name"),
        "diagnostic_display_list": diagnostic_profile.get("path"),
        "display_profiles": display_profiles,
        "visibility_groups": visibility_groups,
        "mesh_records": mesh_records,
        "selected_material_indices": selected_material_indices,
        "selected_material_display_lists": selected_material_display_lists,
        "export_profile": profile.name,
        "bind_pose_format": bind_pose.get("format"),
        "position_source": "source_position",
        "normal_source": "source_normal",
        "diagnostic_preview_position_source": "bind_pose_preview_position",
        "static_bind_pose_contract": (
            "Native static bind-pose vertices are emitted from CMB source_position. "
            "bind_pose_preview_position is a diagnostic weighted skeleton preview and must not feed the native "
            "static display-list export."
        ),
        "texture_orientation": profile.texture_orientation,
        "baked_texture_orientation": profile.baked_texture_orientation,
        "uv_orientation": profile.uv_orientation,
        "position_bounds": bind_pose_position_bounds(bind_pose),
        "counts": {
            "mesh_count": len(mesh_calls),
            "skinned_bind_pose_mesh_count": native_counts["skinned_bind_pose_mesh"],
            "rigid_bind_pose_mesh_count": native_counts["rigid_bind_pose_mesh"],
            "primitive_count": native_counts["primitive"],
            "skinned_primitive_count": native_counts["skinned_primitive"],
            "rigid_primitive_count": native_counts["rigid_primitive"],
            "triangle_count": native_counts["triangle"],
            "vertex_count": native_counts["vertex"],
            "filtered_mesh_count": native_counts["filtered_mesh"],
            "filtered_primitive_count": native_counts["filtered_primitive"],
            "texture_resource_count": sum(1 for resource in resources if resource.kind == "Texture"),
            "material_display_list_count": len(model.materials),
            "selected_material_index_count": len(selected_material_indices),
            "selected_material_display_list_count": len(selected_material_display_lists),
            "vertex_resource_count": sum(1 for resource in resources if resource.kind == "Vertex"),
            "display_list_resource_count": sum(1 for resource in resources if resource.kind == "DisplayList"),
            "issue_count": len(issues),
        },
        "resources": [resource.__dict__ for resource in resources],
        "issues": issues[:sample_limit],
    }
    manifest_path = output_dir / "native_bind_pose_manifest.json"
    write_text(manifest_path, json.dumps(manifest, indent=2) + "\n")
    return manifest


def bind_pose_meshes(bind_pose: dict[str, object]) -> list[dict[str, object]]:
    meshes = bind_pose.get("meshes")
    if not isinstance(meshes, list):
        raise ParseError("skinned bind-pose export has no mesh array")
    return [mesh for mesh in meshes if isinstance(mesh, dict)]


def bind_pose_position_bounds(bind_pose: dict[str, object]) -> dict[str, object]:
    source_bounds = Bounds()
    preview_bounds = Bounds()
    referenced_source_bounds = Bounds()
    referenced_preview_bounds = Bounds()

    for mesh in bind_pose_meshes(bind_pose):
        primitives = mesh.get("primitives")
        if not isinstance(primitives, list):
            continue
        for primitive in primitives:
            if not isinstance(primitive, dict):
                continue
            vertices = primitive.get("vertices")
            indices = primitive.get("indices")
            if not isinstance(vertices, list):
                continue
            for vertex in vertices:
                if isinstance(vertex, dict):
                    source_bounds.add(read_vec3(vertex.get("source_position")))
                    preview_bounds.add(read_vec3(vertex.get("bind_pose_preview_position")))
            if not isinstance(indices, list):
                continue
            for index in indices:
                if not isinstance(index, int) or index < 0 or index >= len(vertices):
                    continue
                vertex = vertices[index]
                if isinstance(vertex, dict):
                    referenced_source_bounds.add(read_vec3(vertex.get("source_position")))
                    referenced_preview_bounds.add(read_vec3(vertex.get("bind_pose_preview_position")))

    return {
        "source_position": source_bounds.to_json(),
        "bind_pose_preview_position": preview_bounds.to_json(),
        "referenced_source_position": referenced_source_bounds.to_json(),
        "referenced_bind_pose_preview_position": referenced_preview_bounds.to_json(),
    }


def native_mesh_record(
    model: CmbModel,
    mesh,
    mesh_path: str,
    selection_primitive_keys: frozenset[tuple[int, int]] | None = None,
) -> dict[str, object]:
    shape = model.shapes[mesh.shape_index]
    mode_counts: Counter[str] = Counter()
    triangle_count = 0
    primitive_indices: list[int] = []
    rigid_bone_indices: set[int] = set()
    for primitive_index, primitive in enumerate(shape.primitives):
        if not primitive.indices:
            continue
        if selection_primitive_keys is not None and (mesh.index, primitive_index) not in selection_primitive_keys:
            continue
        primitive_indices.append(primitive_index)
        mode_counts[str(primitive.skinning_mode)] += 1
        if primitive.skinning_mode == 0:
            rigid_bone_indices.update(int(bone_index) for bone_index in primitive.bone_indices)
        triangle_count += len(primitive.indices) // 3
    material = model.materials[mesh.material_index] if 0 <= mesh.material_index < len(model.materials) else None
    texture_names: list[str | None] = []
    if material is not None:
        for texture_index in material.texture_indices:
            texture_names.append(
                model.textures[texture_index].name
                if 0 <= texture_index < len(model.textures)
                else None
            )
    record = {
        "mesh_index": mesh.index,
        "shape_index": mesh.shape_index,
        "material_index": mesh.material_index,
        "visibility_id": mesh.visibility_id,
        "path": mesh_path,
        "primitive_indices": primitive_indices,
        "primitive_mode_counts": dict(sorted(mode_counts.items())),
        "has_skinned_primitives": any(mode in mode_counts for mode in ("1", "2")),
        "has_rigid_primitives": "0" in mode_counts,
        "triangle_count": triangle_count,
        "texture_names": texture_names,
    }
    if rigid_bone_indices:
        sorted_rigid_bone_indices = sorted(rigid_bone_indices)
        record["rigid_bone_indices"] = sorted_rigid_bone_indices
        if len(sorted_rigid_bone_indices) == 1:
            record["rigid_bone_index"] = sorted_rigid_bone_indices[0]
    return record


def export_native_bind_pose_draw_sets(
    mesh_records: list[dict[str, object]],
    *,
    output_dir: Path,
    resource_root: str,
    symbol: str,
    resources: list[ConvertedResource],
) -> tuple[list[dict[str, object]], list[dict[str, object]]]:
    display_profiles = [
        export_mesh_record_display_list(
            "all_meshes",
            [record for record in mesh_records],
            output_dir=output_dir,
            resource_root=resource_root,
            symbol=symbol,
            resources=resources,
        ),
        export_mesh_record_display_list(
            "skinned_primitives_only",
            [record for record in mesh_records if bool(record.get("has_skinned_primitives"))],
            output_dir=output_dir,
            resource_root=resource_root,
            symbol=symbol,
            resources=resources,
        ),
        export_mesh_record_display_list(
            "rigid_primitives_only",
            [record for record in mesh_records if bool(record.get("has_rigid_primitives"))],
            output_dir=output_dir,
            resource_root=resource_root,
            symbol=symbol,
            resources=resources,
        ),
    ]
    visibility_groups = []
    visibility_ids = sorted(
        {
            int(record["visibility_id"])
            for record in mesh_records
            if isinstance(record.get("visibility_id"), int)
        }
    )
    for visibility_id in visibility_ids:
        records = [record for record in mesh_records if record.get("visibility_id") == visibility_id]
        group = export_mesh_record_display_list(
            f"visibility_{visibility_id:02d}",
            records,
            output_dir=output_dir,
            resource_root=resource_root,
            symbol=symbol,
            resources=resources,
        )
        group["visibility_id"] = visibility_id
        visibility_groups.append(group)
    return display_profiles, visibility_groups


def export_mesh_record_display_list(
    name: str,
    records: list[dict[str, object]],
    *,
    output_dir: Path,
    resource_root: str,
    symbol: str,
    resources: list[ConvertedResource],
) -> dict[str, object]:
    display_name = f"{symbol}_{name}"
    display_path = f"{resource_root}/{display_name}"
    display_file = output_dir / display_name
    lines = ["<DisplayList Version=\"0\">"]
    for record in records:
        mesh_path = str(record.get("path", ""))
        if mesh_path:
            lines.append(f"\t<CallDisplayList Path=\"{escape(mesh_path)}\"/>")
    lines.append("\t<EndDisplayList/>")
    lines.append("</DisplayList>")
    write_text(display_file, "\n".join(lines) + "\n")
    resources.append(ConvertedResource("DisplayList", display_path, str(display_file)))
    return {
        "name": name,
        "path": display_path,
        "mesh_count": len(records),
        "mesh_indices": [record.get("mesh_index") for record in records],
        "triangle_count": sum(int(record.get("triangle_count", 0) or 0) for record in records),
        "has_skinned_primitives": any(bool(record.get("has_skinned_primitives")) for record in records),
        "has_rigid_primitives": any(bool(record.get("has_rigid_primitives")) for record in records),
    }


def export_native_model_bind_pose_mesh(
    mesh,
    bind_pose_mesh: dict[str, object] | None,
    model: CmbModel,
    bone_transforms: tuple[Matrix4, ...],
    *,
    output_dir: Path,
    resource_root: str,
    symbol: str,
    profile,
    material_paths: dict[int, str],
    resources: list[ConvertedResource],
    native_counts: Counter[str],
    issues: list[dict[str, object]],
    sample_limit: int,
    selection_primitive_keys: frozenset[tuple[int, int]] | None = None,
) -> str | None:
    mesh_index = mesh.index
    if mesh.shape_index < 0 or mesh.shape_index >= len(model.shapes):
        add_issue(issues, sample_limit, mesh_index, None, "mesh_references_missing_shape")
        return None

    shape = model.shapes[mesh.shape_index]
    material_index = mesh.material_index
    material = model.materials[material_index] if 0 <= material_index < len(model.materials) else None
    material_path = material_paths.get(material.index) if material is not None else None
    texture = primary_texture_for_material(model, material, profile)
    primitive_paths: list[str] = []

    skinned_primitives: dict[int, dict[str, object]] = {}
    if bind_pose_mesh is not None and isinstance(bind_pose_mesh.get("primitives"), list):
        for primitive_json in bind_pose_mesh["primitives"]:
            if isinstance(primitive_json, dict) and isinstance(primitive_json.get("primitive_index"), int):
                skinned_primitives[int(primitive_json["primitive_index"])] = primitive_json

    mesh_has_skinned_primitive = False
    mesh_has_rigid_primitive = False
    for primitive_index, primitive in enumerate(shape.primitives):
        if not primitive.indices:
            continue
        if selection_primitive_keys is not None and (mesh_index, primitive_index) not in selection_primitive_keys:
            native_counts["filtered_primitive"] += 1
            continue
        if primitive.skinning_mode in (1, 2):
            primitive_json = skinned_primitives.get(primitive_index)
            if primitive_json is None:
                add_issue(issues, sample_limit, mesh_index, None, "missing_skinned_bind_pose_primitive")
                continue
            primitive_path = export_native_skinned_bind_pose_primitive(
                primitive_json,
                mesh_index=mesh_index,
                material=material,
                texture=texture,
                output_dir=output_dir,
                resource_root=resource_root,
                symbol=symbol,
                profile=profile,
                resources=resources,
                native_counts=native_counts,
                issues=issues,
                sample_limit=sample_limit,
            )
            if primitive_path is not None:
                primitive_paths.append(primitive_path)
                mesh_has_skinned_primitive = True
            continue

        if primitive.skinning_mode == 0:
            primitive_path = export_native_rigid_bind_pose_primitive(
                primitive,
                shape,
                primitive_index=primitive_index,
                mesh_index=mesh_index,
                bone_transforms=bone_transforms,
                material=material,
                texture=texture,
                output_dir=output_dir,
                resource_root=resource_root,
                symbol=symbol,
                profile=profile,
                resources=resources,
                native_counts=native_counts,
                issues=issues,
                sample_limit=sample_limit,
            )
            if primitive_path is not None:
                primitive_paths.append(primitive_path)
                mesh_has_rigid_primitive = True
            continue

        add_issue(issues, sample_limit, mesh_index, None, "unsupported_skinning_mode")

    for primitive_json in skinned_primitives.values():
        primitive_index = int(primitive_json.get("primitive_index", -1))
        if selection_primitive_keys is not None and (mesh_index, primitive_index) not in selection_primitive_keys:
            continue
        if primitive_index < 0 or primitive_index >= len(shape.primitives):
            add_issue(issues, sample_limit, mesh_index, primitive_json, "orphaned_skinned_bind_pose_primitive")
            continue
        if shape.primitives[primitive_index].skinning_mode not in (0, 1, 2):
            add_issue(issues, sample_limit, mesh_index, primitive_json, "skinned_bind_pose_primitive_mode_mismatch")

    if not primitive_paths:
        return None

    mesh_name = f"{symbol}_mesh_{mesh_index}"
    mesh_path = f"{resource_root}/{mesh_name}"
    mesh_file = output_dir / mesh_name
    lines = ["<DisplayList Version=\"0\">"]
    lines.append("\t<ClearGeometryMode G_CULL_BACK=\"1\" G_CULL_FRONT=\"1\" G_CULL_BOTH=\"1\"/>")
    geometry_attrs = [
        'G_ZBUFFER="1"',
        'G_SHADE="1"',
        'G_LIGHTING="1"',
        'G_SHADING_SMOOTH="1"',
    ]
    if material is None or material.cull_back:
        geometry_attrs.insert(2, 'G_CULL_BACK="1"')
    lines.append(f"\t<SetGeometryMode {' '.join(geometry_attrs)}/>")
    if material_path is not None:
        lines.append(f"\t<CallDisplayList Path=\"{escape(material_path)}\"/>")
    for primitive_path in primitive_paths:
        lines.append(f"\t<CallDisplayList Path=\"{escape(primitive_path)}\"/>")
    lines.append("\t<EndDisplayList/>")
    lines.append("</DisplayList>")
    write_text(mesh_file, "\n".join(lines) + "\n")
    resources.append(ConvertedResource("DisplayList", mesh_path, str(mesh_file)))
    if mesh_has_skinned_primitive:
        native_counts["skinned_bind_pose_mesh"] += 1
    if mesh_has_rigid_primitive:
        native_counts["rigid_bind_pose_mesh"] += 1
    return mesh_path


def export_native_skinned_bind_pose_primitive(
    primitive: dict[str, object],
    *,
    mesh_index: int,
    material: Material | None,
    texture: Texture | None,
    output_dir: Path,
    resource_root: str,
    symbol: str,
    profile,
    resources: list[ConvertedResource],
    native_counts: Counter[str],
    issues: list[dict[str, object]],
    sample_limit: int,
) -> str | None:
    primitive_path = export_native_bind_pose_primitive(
        primitive,
        mesh_index=mesh_index,
        material=material,
        texture=texture,
        output_dir=output_dir,
        resource_root=resource_root,
        symbol=symbol,
        profile=profile,
        resources=resources,
        native_counts=native_counts,
        issues=issues,
        sample_limit=sample_limit,
    )
    if primitive_path is not None:
        native_counts["skinned_primitive"] += 1
    return primitive_path


def export_native_bind_pose_mesh(
    mesh: dict[str, object],
    model: CmbModel,
    *,
    output_dir: Path,
    resource_root: str,
    symbol: str,
    profile,
    material_paths: dict[int, str],
    resources: list[ConvertedResource],
    native_counts: Counter[str],
    issues: list[dict[str, object]],
    sample_limit: int,
) -> str | None:
    mesh_index = int(mesh.get("mesh_index", 0))
    material_index = int(mesh.get("material_index", -1))
    material = model.materials[material_index] if 0 <= material_index < len(model.materials) else None
    material_path = material_paths.get(material.index) if material is not None else None
    texture = primary_texture_for_material(model, material, profile)
    primitive_paths: list[str] = []

    primitives = mesh.get("primitives")
    if not isinstance(primitives, list):
        return None

    for primitive in primitives:
        if not isinstance(primitive, dict):
            continue
        primitive_path = export_native_skinned_bind_pose_primitive(
            primitive,
            mesh_index=mesh_index,
            material=material,
            texture=texture,
            output_dir=output_dir,
            resource_root=resource_root,
            symbol=symbol,
            profile=profile,
            resources=resources,
            native_counts=native_counts,
            issues=issues,
            sample_limit=sample_limit,
        )
        if primitive_path is not None:
            primitive_paths.append(primitive_path)

    if not primitive_paths:
        return None

    mesh_name = f"{symbol}_mesh_{mesh_index}"
    mesh_path = f"{resource_root}/{mesh_name}"
    mesh_file = output_dir / mesh_name
    lines = ["<DisplayList Version=\"0\">"]
    lines.append("\t<ClearGeometryMode G_CULL_BACK=\"1\" G_CULL_FRONT=\"1\" G_CULL_BOTH=\"1\"/>")
    geometry_attrs = [
        'G_ZBUFFER="1"',
        'G_SHADE="1"',
        'G_LIGHTING="1"',
        'G_SHADING_SMOOTH="1"',
    ]
    if material is None or material.cull_back:
        geometry_attrs.insert(2, 'G_CULL_BACK="1"')
    lines.append(f"\t<SetGeometryMode {' '.join(geometry_attrs)}/>")
    if material_path is not None:
        lines.append(f"\t<CallDisplayList Path=\"{escape(material_path)}\"/>")
    for primitive_path in primitive_paths:
        lines.append(f"\t<CallDisplayList Path=\"{escape(primitive_path)}\"/>")
    lines.append("\t<EndDisplayList/>")
    lines.append("</DisplayList>")
    write_text(mesh_file, "\n".join(lines) + "\n")
    resources.append(ConvertedResource("DisplayList", mesh_path, str(mesh_file)))
    native_counts["skinned_bind_pose_mesh"] += 1
    return mesh_path


def export_native_bind_pose_primitive(
    primitive: dict[str, object],
    *,
    mesh_index: int,
    material: Material | None,
    texture: Texture | None,
    output_dir: Path,
    resource_root: str,
    symbol: str,
    profile,
    resources: list[ConvertedResource],
    native_counts: Counter[str],
    issues: list[dict[str, object]],
    sample_limit: int,
) -> str | None:
    vertices_json = primitive.get("vertices")
    indices_json = primitive.get("indices")
    if not isinstance(vertices_json, list) or not isinstance(indices_json, list):
        add_issue(issues, sample_limit, mesh_index, primitive, "missing_vertices_or_indices")
        return None

    vertices = [read_bind_pose_vertex(vertex) for vertex in vertices_json if isinstance(vertex, dict)]
    indices = [int(index) for index in indices_json if isinstance(index, int)]
    if len(vertices) != len(vertices_json) or len(indices) != len(indices_json) or len(indices) % 3 != 0:
        add_issue(issues, sample_limit, mesh_index, primitive, "invalid_vertex_or_index_array")
        return None
    if any(index < 0 or index >= len(vertices) for index in indices):
        add_issue(issues, sample_limit, mesh_index, primitive, "index_out_of_range")
        return None

    triangles = chunk_triangles(tuple(indices))
    uv_offset = bind_pose_uv_offset(vertices, triangles, material, texture, profile)
    primitive_index = int(primitive.get("primitive_index", 0))
    primitive_name = f"{symbol}_mesh_{mesh_index}_prim_{primitive_index}"
    primitive_path = f"{resource_root}/{primitive_name}"
    primitive_file = output_dir / primitive_name
    batch_paths: list[str] = []

    for batch_index, batch in enumerate(batch_triangles(triangles)):
        batch_vertices = tuple(
            to_native_bind_pose_vertex(
                vertices[original],
                material=material,
                texture=texture,
                profile=profile,
                uv_offset=uv_offset,
            )
            for original in batch.original_indices
        )
        vtx_name = f"{primitive_name}_batch_{batch_index}_vtx"
        tri_name = f"{primitive_name}_batch_{batch_index}_tri"
        vtx_path = f"{resource_root}/{vtx_name}"
        tri_path = f"{resource_root}/{tri_name}"
        write_text(output_dir / vtx_name, vertex_xml(batch_vertices))
        write_text(output_dir / tri_name, triangles_xml(vtx_path, tuple(batch.triangles)))
        resources.append(ConvertedResource("Vertex", vtx_path, str(output_dir / vtx_name)))
        resources.append(ConvertedResource("DisplayList", tri_path, str(output_dir / tri_name)))
        batch_paths.append(tri_path)

    lines = ["<DisplayList Version=\"0\">"]
    for batch_path in batch_paths:
        lines.append(f"\t<CallDisplayList Path=\"{escape(batch_path)}\"/>")
    lines.append("\t<EndDisplayList/>")
    lines.append("</DisplayList>")
    write_text(primitive_file, "\n".join(lines) + "\n")
    resources.append(ConvertedResource("DisplayList", primitive_path, str(primitive_file)))

    native_counts["primitive"] += 1
    native_counts["triangle"] += len(triangles)
    native_counts["vertex"] += len(vertices)
    return primitive_path


def export_native_rigid_bind_pose_primitive(
    primitive: Primitive,
    shape: Shape,
    *,
    primitive_index: int,
    mesh_index: int,
    bone_transforms: tuple[Matrix4, ...],
    material: Material | None,
    texture: Texture | None,
    output_dir: Path,
    resource_root: str,
    symbol: str,
    profile,
    resources: list[ConvertedResource],
    native_counts: Counter[str],
    issues: list[dict[str, object]],
    sample_limit: int,
) -> str | None:
    if len(primitive.bone_indices) != 1:
        add_issue(issues, sample_limit, mesh_index, None, "rigid_primitive_requires_single_bone")
        return None
    bone_index = primitive.bone_indices[0]
    if bone_index < 0 or bone_index >= len(bone_transforms):
        add_issue(issues, sample_limit, mesh_index, None, "rigid_primitive_bone_out_of_range")
        return None

    indices = list(primitive.indices)
    if len(indices) % 3 != 0:
        add_issue(issues, sample_limit, mesh_index, None, "rigid_primitive_index_count_not_triangular")
        return None
    if any(index < 0 or index >= len(shape.positions) for index in indices):
        add_issue(issues, sample_limit, mesh_index, None, "rigid_primitive_index_out_of_range")
        return None

    triangles = chunk_triangles(tuple(indices))
    transform = bone_transforms[bone_index] if profile.apply_bone_transform else None
    uv_offset = rigid_bind_pose_uv_offset(shape, triangles, material, texture, profile)
    primitive_name = f"{symbol}_mesh_{mesh_index}_prim_{primitive_index}"
    primitive_path = f"{resource_root}/{primitive_name}"
    primitive_file = output_dir / primitive_name
    batch_paths: list[str] = []

    for batch_index, batch in enumerate(batch_triangles(triangles)):
        batch_vertices = tuple(
            to_native_rigid_bind_pose_vertex(
                shape,
                original,
                material=material,
                texture=texture,
                profile=profile,
                uv_offset=uv_offset,
                vertex_transform=transform,
            )
            for original in batch.original_indices
        )
        vtx_name = f"{primitive_name}_batch_{batch_index}_vtx"
        tri_name = f"{primitive_name}_batch_{batch_index}_tri"
        vtx_path = f"{resource_root}/{vtx_name}"
        tri_path = f"{resource_root}/{tri_name}"
        write_text(output_dir / vtx_name, vertex_xml(batch_vertices))
        write_text(output_dir / tri_name, triangles_xml(vtx_path, tuple(batch.triangles)))
        resources.append(ConvertedResource("Vertex", vtx_path, str(output_dir / vtx_name)))
        resources.append(ConvertedResource("DisplayList", tri_path, str(output_dir / tri_name)))
        batch_paths.append(tri_path)

    lines = ["<DisplayList Version=\"0\">"]
    for batch_path in batch_paths:
        lines.append(f"\t<CallDisplayList Path=\"{escape(batch_path)}\"/>")
    lines.append("\t<EndDisplayList/>")
    lines.append("</DisplayList>")
    write_text(primitive_file, "\n".join(lines) + "\n")
    resources.append(ConvertedResource("DisplayList", primitive_path, str(primitive_file)))

    native_counts["primitive"] += 1
    native_counts["rigid_primitive"] += 1
    native_counts["triangle"] += len(triangles)
    native_counts["vertex"] += len(set(indices))
    return primitive_path


def primary_texture_for_material(model: CmbModel, material: Material | None, profile) -> Texture | None:
    if material is None:
        return None
    texture_index = material_primary_texture_index_for_profile(model, material, profile)
    if texture_index is None or texture_index < 0 or texture_index >= len(model.textures):
        return None
    return model.textures[texture_index]


def add_issue(
    issues: list[dict[str, object]],
    sample_limit: int,
    mesh_index: int,
    primitive: dict[str, object] | None,
    reason: str,
) -> None:
    if len(issues) >= sample_limit:
        return
    issues.append(
        {
            "mesh_index": mesh_index,
            "primitive_index": primitive.get("primitive_index") if primitive is not None else None,
            "reason": reason,
        }
    )


def read_bind_pose_vertex(vertex: dict[str, object]) -> dict[str, object]:
    return {
        "position": read_vec3(vertex.get("source_position")),
        "normal": read_vec3(vertex.get("source_normal"), default=Vec3(0.0, 0.0, 1.0)),
        "uv0": read_vec2(vertex.get("source_uv0")),
        "color": read_color(vertex.get("source_color_rgba")),
    }


def read_vec3(value: object, *, default: Vec3 | None = None) -> Vec3:
    if isinstance(value, list) and len(value) == 3 and all(isinstance(item, (int, float)) for item in value):
        return Vec3(float(value[0]), float(value[1]), float(value[2]))
    if default is not None:
        return default
    raise ParseError("expected finite Vec3 array")


def read_vec2(value: object) -> Vec2:
    if isinstance(value, list) and len(value) == 2 and all(isinstance(item, (int, float)) for item in value):
        return Vec2(float(value[0]), float(value[1]))
    return Vec2(0.0, 0.0)


def read_color(value: object) -> Color:
    if isinstance(value, list) and len(value) == 4 and all(isinstance(item, int) for item in value):
        return Color(
            max(0, min(255, int(value[0]))),
            max(0, min(255, int(value[1]))),
            max(0, min(255, int(value[2]))),
            max(0, min(255, int(value[3]))),
        )
    return Color(255, 255, 255, 255)


def bind_pose_uv_offset(
    vertices: list[dict[str, object]],
    triangles: list[tuple[int, int, int]],
    material: Material | None,
    texture: Texture | None,
    profile,
) -> Vec2:
    if material is None or texture is None:
        return Vec2(0.0, 0.0)
    mapper = material_texture_mapper(material, texture.index)
    if mapper is None:
        return Vec2(0.0, 0.0)

    min_s = math.inf
    min_t = math.inf
    for triangle in triangles:
        for index in triangle:
            uv = native_bind_pose_uv(vertices[index], material, texture, profile)
            min_s = min(min_s, uv.x)
            min_t = min(min_t, uv.y)
    if not math.isfinite(min_s) or not math.isfinite(min_t):
        return Vec2(0.0, 0.0)
    return Vec2(
        float(integer_repeat_offset(min_s, mapper.wrap_s)),
        float(integer_repeat_offset(min_t, mapper.wrap_t)),
    )


def to_native_bind_pose_vertex(
    vertex: dict[str, object],
    *,
    material: Material | None,
    texture: Texture | None,
    profile,
    uv_offset: Vec2,
) -> LegacyFastResourceVertex:
    position = vertex["position"]
    normal = vertex["normal"]
    color = vertex["color"]
    uv = native_bind_pose_uv(vertex, material, texture, profile)
    uv = Vec2(uv.x + uv_offset.x, uv.y + uv_offset.y)

    width = texture.width if texture is not None else 32
    height = texture.height if texture is not None else 32
    if color == Color(255, 255, 255, 255):
        r, g, b = normal_to_n64(normal)
        a = 255
    else:
        r, g, b, a = color.r, color.g, color.b, color.a

    return LegacyFastResourceVertex(
        x=round_s16(position.x),
        y=round_s16(position.y),
        z=round_s16(position.z),
        s=round_s16(uv.x * width * 32.0),
        t=round_s16(uv.y * height * 32.0),
        r=r,
        g=g,
        b=b,
        a=a,
    )


def native_bind_pose_uv(
    vertex: dict[str, object],
    material: Material | None,
    texture: Texture | None,
    profile,
) -> Vec2:
    uv = vertex["uv0"]
    uv = transform_uv(material, uv, texture.index if texture is not None else None)
    orientation = normalize_uv_orientation(profile.uv_orientation)
    if orientation == TEXTURE_ORIENTATION_NORMAL:
        return uv
    flip_x = orientation in (TEXTURE_ORIENTATION_FLIP_X, TEXTURE_ORIENTATION_FLIP_XY)
    flip_y = orientation in (TEXTURE_ORIENTATION_FLIP_Y, TEXTURE_ORIENTATION_FLIP_XY)
    return Vec2((1.0 - uv.x) if flip_x else uv.x, (1.0 - uv.y) if flip_y else uv.y)


def rigid_bind_pose_uv_offset(
    shape: Shape,
    triangles: list[tuple[int, int, int]],
    material: Material | None,
    texture: Texture | None,
    profile,
) -> Vec2:
    if material is None or texture is None:
        return Vec2(0.0, 0.0)
    mapper = material_texture_mapper(material, texture.index)
    if mapper is None:
        return Vec2(0.0, 0.0)

    min_s = math.inf
    min_t = math.inf
    for triangle in triangles:
        for index in triangle:
            uv = rigid_bind_pose_uv(shape, index, material, texture, profile)
            min_s = min(min_s, uv.x)
            min_t = min(min_t, uv.y)
    if not math.isfinite(min_s) or not math.isfinite(min_t):
        return Vec2(0.0, 0.0)
    return Vec2(
        float(integer_repeat_offset(min_s, mapper.wrap_s)),
        float(integer_repeat_offset(min_t, mapper.wrap_t)),
    )


def to_native_rigid_bind_pose_vertex(
    shape: Shape,
    index: int,
    *,
    material: Material | None,
    texture: Texture | None,
    profile,
    uv_offset: Vec2,
    vertex_transform: Matrix4 | None,
) -> LegacyFastResourceVertex:
    position = shape.positions[index]
    normal = shape.normals[index] if index < len(shape.normals) else Vec3(0.0, 0.0, 1.0)
    if vertex_transform is not None:
        position = transform_position(vertex_transform, position)
        normal = transform_direction(vertex_transform, normal)
    color = shape.colors[index] if index < len(shape.colors) else Color(255, 255, 255, 255)
    uv = rigid_bind_pose_uv(shape, index, material, texture, profile)
    uv = Vec2(uv.x + uv_offset.x, uv.y + uv_offset.y)

    width = texture.width if texture is not None else 32
    height = texture.height if texture is not None else 32
    if color == Color(255, 255, 255, 255):
        r, g, b = normal_to_n64(normal)
        a = 255
    else:
        r, g, b, a = color.r, color.g, color.b, color.a

    return LegacyFastResourceVertex(
        x=round_s16(position.x),
        y=round_s16(position.y),
        z=round_s16(position.z),
        s=round_s16(uv.x * width * 32.0),
        t=round_s16(uv.y * height * 32.0),
        r=r,
        g=g,
        b=b,
        a=a,
    )


def rigid_bind_pose_uv(
    shape: Shape,
    index: int,
    material: Material | None,
    texture: Texture | None,
    profile,
) -> Vec2:
    uv = shape.uv0[index] if index < len(shape.uv0) else Vec2(0.0, 0.0)
    uv = transform_uv(material, uv, texture.index if texture is not None else None)
    orientation = normalize_uv_orientation(profile.uv_orientation)
    if orientation == TEXTURE_ORIENTATION_NORMAL:
        return uv
    flip_x = orientation in (TEXTURE_ORIENTATION_FLIP_X, TEXTURE_ORIENTATION_FLIP_XY)
    flip_y = orientation in (TEXTURE_ORIENTATION_FLIP_Y, TEXTURE_ORIENTATION_FLIP_XY)
    return Vec2((1.0 - uv.x) if flip_x else uv.x, (1.0 - uv.y) if flip_y else uv.y)
