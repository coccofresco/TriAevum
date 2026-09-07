from __future__ import annotations

import json
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

from .binary import ParseError
from .cmb import CmbModel
from .collision import collision_plane, parse_binary_collision_scene
from .legacy_fast_resource import (
    LegacyFastResourceVertex,
    batch_triangles_for_shape,
    chunk_triangles,
    material_texture,
    mesh_uv_offset,
    skeleton_world_transforms,
    uv_orientation_overrides,
)
from .zsi import ZsiFile


ModelCache = dict[tuple[str, int], CmbModel]
COLLISION_PLANE_RESIDUAL_TOLERANCE = 4.0
COLLISION_INTEGRITY_SAMPLE_LIMIT = 8


def audit_scene_package(
    scene_manifest_path: Path,
    material_audit_path: Path,
    archive_path: Path,
    output_path: Path | None = None,
) -> dict[str, object]:
    scene_manifest = json.loads(scene_manifest_path.read_text(encoding="utf-8-sig"))
    material_audit = json.loads(material_audit_path.read_text(encoding="utf-8-sig"))

    if not archive_path.is_file():
        raise ParseError(f"{archive_path}: package archive not found")

    with zipfile.ZipFile(archive_path) as archive:
        archive_names = set(archive.namelist())
        manifest_records = {
            record.get("asset_id"): record
            for record in scene_manifest.get("records", [])
            if record.get("status") == "converted"
        }

        material_records = []
        for record in material_audit.get("records", []):
            for material_record in record.get("materials", []):
                material_with_context = dict(material_record)
                material_with_context.setdefault("asset_id", record.get("asset_id"))
                material_records.append(material_with_context)
        mesh_records = [
            mesh_record
            for record in material_audit.get("records", [])
            for mesh_record in record.get("meshes", [])
        ]

        material_results = [
            audit_material_record(archive, archive_names, manifest_records, material)
            for material in material_records
        ]
        model_cache: ModelCache = {}
        mesh_results = [
            audit_mesh_record(archive, archive_names, manifest_records, mesh, model_cache)
            for mesh in mesh_records
        ]
        collision_results = audit_collision_records(archive, archive_names, scene_manifest)

    issue_counts = count_issues(material_results + mesh_results + collision_results)
    audit = {
        "scene": material_audit.get("scene"),
        "scene_manifest": str(scene_manifest_path),
        "material_audit": str(material_audit_path),
        "archive": str(archive_path),
        "archive_entry_count": len(archive_names),
        "has_manifest": "manifest.json" in archive_names,
        "has_scene_manifest": "oot3d_scene_manifest.json" in archive_names,
        "material_count": len(material_records),
        "checked_material_count": len(material_results),
        "raw_texture_stage_export_gap_material_count": sum(
            1 for result in material_results if result["actual_raw_stage_export_gap"] > 0
        ),
        "raw_texture_stage_export_gap_total": sum(
            result["actual_raw_stage_export_gap"] for result in material_results
        ),
        "mesh_count": len(mesh_records),
        "checked_mesh_count": len(mesh_results),
        "checked_vertex_buffer_count": sum(
            result["checked_vertex_buffer_count"] for result in mesh_results
        ),
        "checked_vertex_count": sum(result["checked_vertex_count"] for result in mesh_results),
        "collision_count": len(collision_results),
        "checked_collision_count": len(collision_results),
        "issue_counts": issue_counts,
        "material_results": material_results,
        "mesh_results": mesh_results,
        "collision_results": collision_results,
    }

    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(audit, indent=2) + "\n", encoding="utf-8", newline="\n")
    return audit


def audit_collision_records(
    archive: zipfile.ZipFile,
    archive_names: set[str],
    scene_manifest: dict[str, object],
) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    seen: set[str] = set()
    for record in scene_manifest_collision_records(scene_manifest):
        resources = [
            resource
            for resource in record.get("resources", [])
            if isinstance(resource, dict) and resource.get("kind") == "CollisionHeader"
        ]
        for resource in resources:
            resource_path = normalize_archive_path(resource.get("path", ""))
            if resource_path in seen:
                continue
            seen.add(resource_path)
            records.append(audit_collision_record(archive, archive_names, record, resource_path))
    return records


def scene_manifest_collision_records(scene_manifest: dict[str, object]) -> list[dict[str, object]]:
    records = [
        record
        for record in scene_manifest.get("records", [])
        if isinstance(record, dict) and record.get("status") == "converted"
    ]
    collision_record = scene_manifest.get("collision")
    if isinstance(collision_record, dict) and collision_record.get("status") == "converted":
        records.append(collision_record)
    return records


def audit_collision_record(
    archive: zipfile.ZipFile,
    archive_names: set[str],
    record: dict[str, object],
    resource_path: str,
) -> dict[str, object]:
    issues: list[str] = []
    if resource_path not in archive_names:
        issues.append("missing_collision_header_entry")
    if record.get("source") != "oot3d_zsi_native_collision":
        issues.append("collision_source_not_native_zsi")
    reference_resource_path_source = record.get("reference_resource_path_source")
    if reference_resource_path_source != "reference_o2r_discovery":
        issues.append("collision_reference_resource_path_not_reference_o2r_discovery")

    summary = record.get("summary")
    if not isinstance(summary, dict):
        issues.append("collision_missing_summary")
        reference_accepted = None
        native_accepted = None
        visual_policy_summary = {}
    else:
        reference_comparison = summary.get("reference_comparison")
        native_acceptance = summary.get("native_zsi_acceptance")
        visual_policy = summary.get("visual_diagnostic_policy")
        if not isinstance(reference_comparison, dict):
            issues.append("collision_missing_reference_comparison")
            reference_accepted = None
        else:
            reference_accepted = reference_comparison.get("accepted")
            if reference_accepted is not True:
                issues.append("collision_reference_comparison_not_accepted")
        if not isinstance(native_acceptance, dict):
            issues.append("collision_missing_native_zsi_acceptance")
            native_accepted = None
        else:
            native_accepted = native_acceptance.get("accepted")
            if native_accepted is not True:
                issues.append("collision_native_zsi_acceptance_not_accepted")
        visual_policy_summary = audit_collision_visual_diagnostic_policy(
            visual_policy,
            expected_reference_accepted=reference_accepted,
            expected_native_accepted=native_accepted,
        )
        issues.extend(visual_policy_summary["issues"])

    resource_binary_summary = audit_collision_binary_resource(
        archive,
        resource_path,
        summary if isinstance(summary, dict) else None,
    )
    issues.extend(resource_binary_summary["issues"])

    return {
        "resource_path": resource_path,
        "source": record.get("source"),
        "reference_resource_path_source": reference_resource_path_source,
        "reference_comparison_accepted": reference_accepted,
        "native_zsi_acceptance_accepted": native_accepted,
        "binary_resource": resource_binary_summary,
        "visual_diagnostic_policy": visual_policy_summary,
        "issues": issues,
    }


def audit_collision_binary_resource(
    archive: zipfile.ZipFile,
    resource_path: str,
    summary: dict[str, object] | None,
) -> dict[str, object]:
    if resource_path not in archive.namelist():
        return {
            "present": False,
            "issues": [],
        }

    issues: list[str] = []
    try:
        scene = parse_binary_collision_scene(
            archive.read(resource_path),
            f"package!{resource_path}",
        )
    except Exception as exc:
        return {
            "present": True,
            "parse_status": "error",
            "parse_error": str(exc),
            "issues": ["collision_resource_parse_error"],
        }

    actual_counts = {
        "vertex_count": len(scene.vertices),
        "polygon_count": len(scene.polygons),
        "surface_type_count": len(scene.metadata.surface_types),
        "exported_camera_record_count": len(scene.metadata.camera_data),
        "camera_position_group_count": len(scene.metadata.camera_positions),
        "water_box_count": len(scene.metadata.water_boxes),
    }
    actual_bounds = {
        "bounds_min": tuple(scene.effective_bounds_min),
        "bounds_max": tuple(scene.effective_bounds_max),
    }
    expected_counts: dict[str, int] = {}
    expected_bounds: dict[str, tuple[int, int, int]] = {}
    if isinstance(summary, dict):
        for key in actual_counts:
            if key not in summary:
                continue
            try:
                expected_counts[key] = int(summary[key])
            except (TypeError, ValueError):
                continue
        for key in actual_bounds:
            expected = expected_collision_bounds_from_summary(summary, key)
            if expected is not None:
                expected_bounds[key] = expected
    mismatch_issue_by_key = {
        "vertex_count": "collision_resource_vertex_count_mismatch",
        "polygon_count": "collision_resource_polygon_count_mismatch",
        "surface_type_count": "collision_resource_surface_type_count_mismatch",
        "exported_camera_record_count": "collision_resource_camera_data_count_mismatch",
        "camera_position_group_count": "collision_resource_camera_position_group_count_mismatch",
        "water_box_count": "collision_resource_water_box_count_mismatch",
    }
    mismatches: dict[str, dict[str, int]] = {}
    for key, expected in expected_counts.items():
        actual = actual_counts[key]
        if actual != expected:
            issues.append(mismatch_issue_by_key[key])
            mismatches[key] = {
                "expected": expected,
                "actual": actual,
            }

    bounds_mismatch_issue_by_key = {
        "bounds_min": "collision_resource_bounds_min_mismatch",
        "bounds_max": "collision_resource_bounds_max_mismatch",
    }
    bounds_mismatches: dict[str, dict[str, tuple[int, int, int]]] = {}
    for key, expected in expected_bounds.items():
        actual = actual_bounds[key]
        if actual != expected:
            issues.append(bounds_mismatch_issue_by_key[key])
            bounds_mismatches[key] = {
                "expected": expected,
                "actual": actual,
            }

    integrity = audit_collision_scene_integrity(scene)
    issues.extend(integrity["issues"])

    return {
        "present": True,
        "parse_status": "ok",
        "actual_counts": actual_counts,
        "expected_counts": expected_counts,
        "mismatches": mismatches,
        "actual_bounds": actual_bounds,
        "expected_bounds": expected_bounds,
        "bounds_mismatches": bounds_mismatches,
        "integrity": integrity,
        "issues": issues,
    }


def audit_collision_scene_integrity(scene) -> dict[str, object]:
    invalid_vertex_index_records: list[dict[str, object]] = []
    invalid_surface_type_records: list[dict[str, object]] = []
    duplicate_vertex_records: list[dict[str, object]] = []
    degenerate_triangle_records: list[dict[str, object]] = []
    zero_normal_records: list[dict[str, object]] = []
    plane_mismatch_records: list[dict[str, object]] = []
    invalid_vertex_index_count = 0
    invalid_surface_type_count = 0
    duplicate_vertex_count = 0
    degenerate_triangle_count = 0
    zero_normal_count = 0
    plane_mismatch_count = 0
    max_abs_plane_residual = 0.0

    for polygon_index, polygon in enumerate(scene.polygons):
        vertex_indices = (
            polygon.vertex_a & 0x1FFF,
            polygon.vertex_b & 0x1FFF,
            polygon.vertex_c & 0x1FFF,
        )
        base_record = {
            "polygon_index": polygon_index,
            "type": polygon.type,
            "vertex_indices": vertex_indices,
        }
        invalid_indices = [
            vertex_index
            for vertex_index in vertex_indices
            if vertex_index >= len(scene.vertices)
        ]
        if invalid_indices:
            invalid_vertex_index_count += 1
            append_sample(
                invalid_vertex_index_records,
                {
                    **base_record,
                    "invalid_indices": invalid_indices,
                    "vertex_count": len(scene.vertices),
                },
            )
            continue
        if polygon.type >= len(scene.metadata.surface_types):
            invalid_surface_type_count += 1
            append_sample(
                invalid_surface_type_records,
                {
                    **base_record,
                    "surface_type_count": len(scene.metadata.surface_types),
                },
            )
        if len(set(vertex_indices)) < 3:
            duplicate_vertex_count += 1
            append_sample(duplicate_vertex_records, base_record)

        triangle = tuple(scene.vertices[vertex_index] for vertex_index in vertex_indices)
        if collision_plane(triangle) is None:
            degenerate_triangle_count += 1
            append_sample(degenerate_triangle_records, base_record)

        if polygon.normal_x == 0 and polygon.normal_y == 0 and polygon.normal_z == 0:
            zero_normal_count += 1
            append_sample(zero_normal_records, base_record)
            continue

        normal = (
            polygon.normal_x / 32767.0,
            polygon.normal_y / 32767.0,
            polygon.normal_z / 32767.0,
        )
        residuals = [
            abs(normal[0] * vertex[0] + normal[1] * vertex[1] + normal[2] * vertex[2] + polygon.dist)
            for vertex in triangle
        ]
        polygon_max_residual = max(residuals, default=0.0)
        max_abs_plane_residual = max(max_abs_plane_residual, polygon_max_residual)
        if polygon_max_residual > COLLISION_PLANE_RESIDUAL_TOLERANCE:
            plane_mismatch_count += 1
            append_sample(
                plane_mismatch_records,
                {
                    **base_record,
                    "normal": (polygon.normal_x, polygon.normal_y, polygon.normal_z),
                    "dist": polygon.dist,
                    "max_abs_plane_residual": round(polygon_max_residual, 6),
                    "residuals": [round(value, 6) for value in residuals],
                    "tolerance": COLLISION_PLANE_RESIDUAL_TOLERANCE,
                },
            )

    issue_counts = {
        "collision_resource_polygon_vertex_index_out_of_range": invalid_vertex_index_count,
        "collision_resource_polygon_surface_type_out_of_range": invalid_surface_type_count,
        "collision_resource_polygon_duplicate_vertices": duplicate_vertex_count,
        "collision_resource_polygon_degenerate_triangle": degenerate_triangle_count,
        "collision_resource_polygon_zero_normal": zero_normal_count,
        "collision_resource_polygon_plane_mismatch": plane_mismatch_count,
    }
    issues = [
        issue
        for issue, count in issue_counts.items()
        if count > 0
    ]
    return {
        "accepted": not issues,
        "polygon_count": len(scene.polygons),
        "vertex_count": len(scene.vertices),
        "surface_type_count": len(scene.metadata.surface_types),
        "plane_residual_tolerance": COLLISION_PLANE_RESIDUAL_TOLERANCE,
        "max_abs_plane_residual": round(max_abs_plane_residual, 6),
        "issue_counts": issue_counts,
        "invalid_vertex_index_records": invalid_vertex_index_records,
        "invalid_surface_type_records": invalid_surface_type_records,
        "duplicate_vertex_records": duplicate_vertex_records,
        "degenerate_triangle_records": degenerate_triangle_records,
        "zero_normal_records": zero_normal_records,
        "plane_mismatch_records": plane_mismatch_records,
        "issues": issues,
    }


def append_sample(records: list[dict[str, object]], record: dict[str, object]) -> None:
    if len(records) < COLLISION_INTEGRITY_SAMPLE_LIMIT:
        records.append(record)


def tuple3_from_summary(value: object) -> tuple[int, int, int] | None:
    if not isinstance(value, (list, tuple)) or len(value) != 3:
        return None
    try:
        return tuple(int(item) for item in value)
    except (TypeError, ValueError):
        return None


def expected_collision_bounds_from_summary(
    summary: dict[str, object],
    key: str,
) -> tuple[int, int, int] | None:
    expected = tuple3_from_summary(summary.get(key))
    if expected is not None:
        return expected
    reference_comparison = summary.get("reference_comparison")
    if not isinstance(reference_comparison, dict):
        return None
    converted = reference_comparison.get("converted")
    if not isinstance(converted, dict):
        return None
    return tuple3_from_summary(converted.get(key))


def audit_collision_visual_diagnostic_policy(
    policy: object,
    *,
    expected_reference_accepted: object,
    expected_native_accepted: object,
) -> dict[str, object]:
    if not isinstance(policy, dict):
        return {
            "present": False,
            "issues": ["collision_missing_visual_diagnostic_policy"],
        }

    issues: list[str] = []
    if policy.get("format") != "oot3d_collision_visual_diagnostic_policy_v1":
        issues.append("collision_visual_policy_format_mismatch")
    if policy.get("collision_source") != "oot3d_zsi_native_collision":
        issues.append("collision_visual_policy_source_mismatch")
    if policy.get("visual_mesh_is_collision_source") is not False:
        issues.append("collision_visual_mesh_marked_as_source")
    if policy.get("reference_comparison_accepted") != expected_reference_accepted:
        issues.append("collision_visual_policy_reference_mismatch")
    if policy.get("native_zsi_acceptance_accepted") != expected_native_accepted:
        issues.append("collision_visual_policy_native_mismatch")
    if policy.get("reference_failed_checks_still_blocking") not in ([], None):
        issues.append("collision_visual_policy_unresolved_reference_failures")

    allowed_relaxations = {"source_gameplay_spawn_probes_match_or_diagnosed"}
    relaxations = policy.get("relaxed_checks_from_visual_diagnostics", [])
    if not isinstance(relaxations, list):
        issues.append("collision_visual_policy_invalid_relaxation_list")
        relaxations = []
    elif any(str(relaxation) not in allowed_relaxations for relaxation in relaxations):
        issues.append("collision_visual_policy_forbidden_relaxation")

    return {
        "present": True,
        "format": policy.get("format"),
        "collision_source": policy.get("collision_source"),
        "visual_mesh_is_collision_source": policy.get("visual_mesh_is_collision_source"),
        "reference_comparison_accepted": policy.get("reference_comparison_accepted"),
        "native_zsi_acceptance_accepted": policy.get("native_zsi_acceptance_accepted"),
        "relaxed_checks_from_visual_diagnostics": relaxations,
        "reference_failed_checks_still_blocking": policy.get(
            "reference_failed_checks_still_blocking",
        ),
        "issues": issues,
    }


def audit_material_record(
    archive: zipfile.ZipFile,
    archive_names: set[str],
    manifest_records: dict[str, dict[str, object]],
    material: dict[str, object],
) -> dict[str, object]:
    material_path = normalize_archive_path(material["generated_material_path"])
    expected_texture_paths = expected_material_texture_paths(manifest_records, material)
    expected_stage_count = int(material.get("exported_texture_stage_count", len(expected_texture_paths)) or 0)
    raw_stage_count = raw_texture_stage_count(material, expected_stage_count)
    baked_extra_stage_count = int(material.get("raw_texture_stage_baked_extra_count", 0) or 0)
    expected_raw_stage_export_gap = int(material.get("raw_texture_stage_export_gap", 0) or 0)
    issues: list[str] = []
    actual_texture_paths: list[str] = []

    if material_path not in archive_names:
        issues.append("missing_material_display_list")
    else:
        try:
            xml_root = ET.fromstring(archive.read(material_path).decode("utf-8"))
            actual_texture_paths = [
                normalize_archive_path(element.attrib["Path"])
                for element in xml_root.iter("SetTextureImage")
                if "Path" in element.attrib
            ]
        except (ET.ParseError, UnicodeDecodeError) as exc:
            issues.append("invalid_material_xml")
            actual_texture_paths = []
            error = str(exc)
        else:
            error = None

        if actual_texture_paths != expected_texture_paths:
            issues.append("material_texture_path_mismatch")
        if len(actual_texture_paths) != expected_stage_count:
            issues.append("material_texture_stage_count_mismatch")
        actual_raw_stage_export_gap = max(
            0,
            raw_stage_count - len(actual_texture_paths) - baked_extra_stage_count,
        )
        if actual_raw_stage_export_gap != expected_raw_stage_export_gap:
            issues.append("raw_stage_export_gap_mismatch")
        for texture_path in expected_texture_paths:
            if texture_path not in archive_names:
                issues.append("missing_expected_texture_entry")
                break
        for texture_path in actual_texture_paths:
            if texture_path not in archive_names:
                issues.append("missing_actual_texture_entry")
                break

    return {
        "asset_id": material.get("asset_id"),
        "material_index": material.get("material_index"),
        "material_path": material_path,
        "expected_texture_paths": expected_texture_paths,
        "actual_texture_paths": actual_texture_paths,
        "expected_texture_stage_count": expected_stage_count,
        "actual_texture_stage_count": len(actual_texture_paths),
        "raw_texture_stage_count": raw_stage_count,
        "raw_texture_stage_baked_extra_count": baked_extra_stage_count,
        "expected_raw_stage_export_gap": expected_raw_stage_export_gap,
        "actual_raw_stage_export_gap": max(
            0,
            raw_stage_count - len(actual_texture_paths) - baked_extra_stage_count,
        ),
        "issues": issues,
        **({"error": error} if "error" in locals() and error else {}),
    }


def audit_mesh_record(
    archive: zipfile.ZipFile,
    archive_names: set[str],
    manifest_records: dict[str, dict[str, object]],
    mesh: dict[str, object],
    model_cache: ModelCache,
) -> dict[str, object]:
    mesh_path = normalize_archive_path(mesh["generated_mesh_path"])
    expected_material_path = normalize_archive_path(mesh["generated_material_path"])
    issues: list[str] = []
    actual_material_path = None
    checked_vertex_buffer_count = 0
    checked_vertex_count = 0

    if mesh_path not in archive_names:
        issues.append("missing_mesh_display_list")
    else:
        try:
            xml_root = ET.fromstring(archive.read(mesh_path).decode("utf-8"))
            material_calls = [
                normalize_archive_path(element.attrib["Path"])
                for element in xml_root.iter("CallDisplayList")
                if "Path" in element.attrib and "_mat_" in element.attrib["Path"]
            ]
            actual_material_path = material_calls[0] if material_calls else None
        except (ET.ParseError, UnicodeDecodeError) as exc:
            issues.append("invalid_mesh_xml")
            error = str(exc)
        else:
            error = None

        if actual_material_path != expected_material_path:
            issues.append("mesh_material_call_mismatch")
        if expected_material_path not in archive_names:
            issues.append("missing_expected_material_entry")

    expected_vertex_buffers: dict[str, tuple[LegacyFastResourceVertex, ...]] = {}
    uv_orientation = mesh_uv_orientation(mesh, manifest_records)
    if has_vertex_audit_metadata(mesh):
        try:
            expected_vertex_buffers = expected_mesh_vertex_buffers(
                mesh,
                model_cache,
                uv_orientation,
            )
        except (KeyError, IndexError, ParseError, OSError, ValueError) as exc:
            issues.append("source_model_vertex_audit_failed")
            vertex_error = str(exc)
        else:
            vertex_error = None
            for vertex_path, expected_vertices in expected_vertex_buffers.items():
                if vertex_path not in archive_names:
                    issues.append("missing_vertex_buffer")
                    continue
                try:
                    actual_vertices = parse_vertex_xml(archive.read(vertex_path).decode("utf-8"))
                except (ET.ParseError, UnicodeDecodeError, ValueError) as exc:
                    issues.append("invalid_vertex_xml")
                    vertex_error = str(exc)
                    continue
                checked_vertex_buffer_count += 1
                checked_vertex_count += len(actual_vertices)
                if actual_vertices != tuple(vertex_tuple(vertex) for vertex in expected_vertices):
                    issues.append("vertex_buffer_mismatch")

    return {
        "asset_id": mesh.get("asset_id"),
        "mesh_index": mesh.get("mesh_index"),
        "mesh_path": mesh_path,
        "expected_material_path": expected_material_path,
        "actual_material_path": actual_material_path,
        "uv_orientation": uv_orientation,
        "expected_vertex_buffer_count": len(expected_vertex_buffers),
        "checked_vertex_buffer_count": checked_vertex_buffer_count,
        "checked_vertex_count": checked_vertex_count,
        "issues": issues,
        **({"error": error} if "error" in locals() and error else {}),
        **({"vertex_error": vertex_error} if "vertex_error" in locals() and vertex_error else {}),
    }


def has_vertex_audit_metadata(mesh_record: dict[str, object]) -> bool:
    return "source_zsi" in mesh_record and "cmb_index" in mesh_record


def expected_mesh_vertex_buffers(
    mesh_record: dict[str, object],
    model_cache: ModelCache,
    uv_orientation: str,
) -> dict[str, tuple[LegacyFastResourceVertex, ...]]:
    source_zsi = str(mesh_record["source_zsi"])
    cmb_index = int(mesh_record["cmb_index"])
    model = load_scene_model(source_zsi, cmb_index, model_cache)
    mesh_index = int(mesh_record["mesh_index"])
    if mesh_index < 0 or mesh_index >= len(model.meshes):
        raise ParseError(f"{source_zsi}: mesh index {mesh_index} out of range")
    mesh = model.meshes[mesh_index]
    if mesh.shape_index >= len(model.shapes):
        raise ParseError(f"{source_zsi}: mesh {mesh.index} references missing shape {mesh.shape_index}")
    shape = model.shapes[mesh.shape_index]
    material = model.materials[mesh.material_index] if mesh.material_index < len(model.materials) else None
    texture = material_texture(model, material)
    bone_transforms = skeleton_world_transforms(model.skeleton)
    mesh_path = normalize_archive_path(mesh_record["generated_mesh_path"])
    buffers: dict[str, tuple[LegacyFastResourceVertex, ...]] = {}

    for primitive_index, primitive in enumerate(shape.primitives):
        if primitive.skinning_mode not in (0,):
            raise ParseError(
                f"{source_zsi}: mesh {mesh.index} primitive {primitive_index} uses skinning mode "
                f"{primitive.skinning_mode}"
            )
        if len(primitive.bone_indices) != 1:
            raise ParseError(
                f"{source_zsi}: mesh {mesh.index} primitive {primitive_index} references "
                f"{len(primitive.bone_indices)} bones"
            )
        bone_index = primitive.bone_indices[0]
        if bone_index < 0 or bone_index >= len(bone_transforms):
            raise ParseError(
                f"{source_zsi}: mesh {mesh.index} primitive {primitive_index} references "
                f"missing bone {bone_index}"
            )
        triangles = list(chunk_triangles(primitive.indices))
        uv_overrides = uv_orientation_overrides(
            shape,
            material,
            triangles,
            texture,
            uv_orientation,
        )
        uv_offset = mesh_uv_offset(
            shape,
            material,
            triangles,
            texture,
            uv_overrides=uv_overrides,
        )
        batches = batch_triangles_for_shape(
            shape,
            texture,
            material,
            triangles,
            uv_offset,
            uv_overrides=uv_overrides,
            vertex_transform=bone_transforms[bone_index],
        )
        for batch_index, batch in enumerate(batches):
            vertex_path = f"{mesh_path}_prim_{primitive_index}_batch_{batch_index}_vtx"
            buffers[vertex_path] = batch.vertices

    return buffers


def mesh_uv_orientation(
    mesh_record: dict[str, object],
    manifest_records: dict[str, dict[str, object]],
) -> str:
    asset_id = mesh_record.get("asset_id")
    manifest_record = manifest_records.get(asset_id) if isinstance(asset_id, str) else None
    manifest_path = manifest_record.get("manifest") if manifest_record else None
    if not isinstance(manifest_path, str):
        return "normal"
    try:
        manifest = json.loads(Path(manifest_path).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return "normal"
    uv_orientation = manifest.get("uv_orientation")
    return str(uv_orientation) if uv_orientation is not None else "normal"


def load_scene_model(source_zsi: str, cmb_index: int, model_cache: ModelCache) -> CmbModel:
    key = (source_zsi, cmb_index)
    if key not in model_cache:
        cmbs = ZsiFile.from_path(Path(source_zsi)).embedded_cmbs()
        if cmb_index < 0 or cmb_index >= len(cmbs):
            raise ParseError(f"{source_zsi}: CMB index {cmb_index} out of range")
        model_cache[key] = cmbs[cmb_index].model
    return model_cache[key]


def parse_vertex_xml(xml_text: str) -> tuple[tuple[int, int, int, int, int, int, int, int, int], ...]:
    root = ET.fromstring(xml_text)
    vertices: list[tuple[int, int, int, int, int, int, int, int, int]] = []
    for element in root.iter("Vtx"):
        vertices.append(
            (
                int(element.attrib["X"]),
                int(element.attrib["Y"]),
                int(element.attrib["Z"]),
                int(element.attrib["S"]),
                int(element.attrib["T"]),
                int(element.attrib["R"]),
                int(element.attrib["G"]),
                int(element.attrib["B"]),
                int(element.attrib["A"]),
            )
        )
    return tuple(vertices)


def vertex_tuple(vertex: LegacyFastResourceVertex) -> tuple[int, int, int, int, int, int, int, int, int]:
    return (
        vertex.x,
        vertex.y,
        vertex.z,
        vertex.s,
        vertex.t,
        vertex.r,
        vertex.g,
        vertex.b,
        vertex.a,
    )


def expected_material_texture_paths(
    manifest_records: dict[str, dict[str, object]],
    material: dict[str, object],
) -> list[str]:
    asset_id = material.get("asset_id")
    manifest_record = manifest_records.get(str(asset_id), {})
    texture_resources = [
        normalize_archive_path(resource["path"])
        for resource in manifest_record.get("resources", [])
        if resource.get("kind") == "Texture"
    ]
    expected: list[str] = []
    primary_texture = material.get("selected_primary_texture")
    if primary_texture:
        expected.append(resolve_texture_resource(texture_resources, primary_texture))

    secondary_texture = material.get("selected_secondary_texture")
    if secondary_texture:
        secondary_export_path = material.get("selected_secondary_texture_export_path")
        if secondary_export_path:
            expected.append(normalize_archive_path(secondary_export_path))
        else:
            expected.append(resolve_texture_resource(texture_resources, secondary_texture))
    return expected


def raw_texture_stage_count(material: dict[str, object], fallback: int) -> int:
    selector = material.get("raw_texture_stage_selector")
    if not isinstance(selector, dict):
        return fallback
    return int(selector.get("stage_count", fallback) or 0)


def resolve_texture_resource(texture_resources: list[str], texture: dict[str, object]) -> str:
    name = str(texture["name"])
    index = texture.get("index")
    candidates = {f"{name}.rgba16"}
    if index is not None:
        candidates.add(f"{name}_{index}.rgba16")
    matches = [
        resource
        for resource in texture_resources
        if resource.rsplit("/", 1)[-1] in candidates
    ]
    if len(matches) == 1:
        return matches[0]
    if matches:
        return sorted(matches)[0]
    if texture_resources:
        return "/".join(texture_resources[0].split("/")[:-1] + [f"{name}.rgba16"])
    return f"{name}.rgba16"


def count_issues(results: list[dict[str, object]]) -> dict[str, int]:
    counts: dict[str, int] = {}
    known_issues = (
        "missing_material_display_list",
        "invalid_material_xml",
        "material_texture_path_mismatch",
        "material_texture_stage_count_mismatch",
        "raw_stage_export_gap_mismatch",
        "missing_expected_texture_entry",
        "missing_actual_texture_entry",
        "missing_mesh_display_list",
        "invalid_mesh_xml",
        "mesh_material_call_mismatch",
        "missing_expected_material_entry",
        "source_model_vertex_audit_failed",
        "missing_vertex_buffer",
        "invalid_vertex_xml",
        "vertex_buffer_mismatch",
        "missing_collision_header_entry",
        "collision_source_not_native_zsi",
        "collision_reference_resource_path_not_reference_o2r_discovery",
        "collision_missing_summary",
        "collision_missing_reference_comparison",
        "collision_reference_comparison_not_accepted",
        "collision_missing_native_zsi_acceptance",
        "collision_native_zsi_acceptance_not_accepted",
        "collision_missing_visual_diagnostic_policy",
        "collision_visual_policy_format_mismatch",
        "collision_visual_policy_source_mismatch",
        "collision_visual_mesh_marked_as_source",
        "collision_visual_policy_reference_mismatch",
        "collision_visual_policy_native_mismatch",
        "collision_visual_policy_unresolved_reference_failures",
        "collision_visual_policy_invalid_relaxation_list",
        "collision_visual_policy_forbidden_relaxation",
        "collision_resource_parse_error",
        "collision_resource_vertex_count_mismatch",
        "collision_resource_polygon_count_mismatch",
        "collision_resource_surface_type_count_mismatch",
        "collision_resource_camera_data_count_mismatch",
        "collision_resource_camera_position_group_count_mismatch",
        "collision_resource_water_box_count_mismatch",
        "collision_resource_bounds_min_mismatch",
        "collision_resource_bounds_max_mismatch",
        "collision_resource_polygon_vertex_index_out_of_range",
        "collision_resource_polygon_surface_type_out_of_range",
        "collision_resource_polygon_duplicate_vertices",
        "collision_resource_polygon_degenerate_triangle",
        "collision_resource_polygon_zero_normal",
        "collision_resource_polygon_plane_mismatch",
    )
    for issue in known_issues:
        counts[issue] = 0
    for result in results:
        for issue in result.get("issues", []):
            counts[issue] = counts.get(issue, 0) + 1
    counts["total"] = sum(counts.values())
    return counts


def normalize_archive_path(value: object) -> str:
    return str(value).replace("\\", "/").strip("/")
