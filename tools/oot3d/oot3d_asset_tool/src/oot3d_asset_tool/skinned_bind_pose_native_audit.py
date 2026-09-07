from __future__ import annotations

import json
import math
import re
import xml.etree.ElementTree as ET
from collections import Counter
from pathlib import Path

from .binary import ParseError
from .cmb import Vec3

AUDIT_FORMAT = "oot3d_skinned_bind_pose_native_contract_audit_v1"
NATIVE_MANIFEST_FORMAT = "oot3d_skinned_bind_pose_native_export_v1"
BIND_POSE_FORMAT = "oot3d_skinned_bind_pose_export_v1"


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
            "min": [round_float(self.min_x), round_float(self.min_y), round_float(self.min_z)],
            "max": [round_float(self.max_x), round_float(self.max_y), round_float(self.max_z)],
            "span": [
                round_float(self.max_x - self.min_x),
                round_float(self.max_y - self.min_y),
                round_float(self.max_z - self.min_z),
            ],
        }


def audit_skinned_bind_pose_native_contract(
    bind_pose_path: Path,
    native_manifest_path: Path,
    output: Path,
    *,
    position_tolerance: float = 1.0,
    sample_limit: int = 50,
) -> dict[str, object]:
    bind_pose = read_json(bind_pose_path)
    native_manifest = read_json(native_manifest_path)

    issues: list[dict[str, object]] = []
    if bind_pose.get("format") != BIND_POSE_FORMAT:
        add_issue(issues, sample_limit, "unexpected_bind_pose_format", format=bind_pose.get("format"))
    if native_manifest.get("format") != NATIVE_MANIFEST_FORMAT:
        add_issue(issues, sample_limit, "unexpected_native_manifest_format", format=native_manifest.get("format"))
    if native_manifest.get("position_source") != "source_position":
        add_issue(issues, sample_limit, "native_position_source_not_source_position", value=native_manifest.get("position_source"))
    if native_manifest.get("diagnostic_preview_position_source") not in (None, "bind_pose_preview_position"):
        add_issue(
            issues,
            sample_limit,
            "unexpected_diagnostic_preview_position_source",
            value=native_manifest.get("diagnostic_preview_position_source"),
        )

    selection_primitive_keys = native_selection_primitive_keys(native_manifest)
    expected_skinned_primitive_bounds = referenced_bind_pose_primitive_bounds(
        bind_pose,
        "source_position",
        rigid_key="bind_pose_preview_position",
        round_values=True,
        primitive_filter=selection_primitive_keys,
    )
    source_bounds = merge_bounds(expected_skinned_primitive_bounds.values())
    preview_bounds = referenced_bind_pose_bounds(
        bind_pose,
        "bind_pose_preview_position",
        round_values=False,
        primitive_filter=selection_primitive_keys,
    )
    native_bounds, native_counts, native_primitive_bounds = native_vertex_bounds(
        native_manifest,
        native_manifest_path,
        issues,
        sample_limit,
    )
    primitive_mismatch_count = compare_skinned_primitive_bounds(
        expected_skinned_primitive_bounds,
        native_primitive_bounds,
        issues,
        sample_limit,
        position_tolerance,
    )
    if source_bounds.count == 0:
        add_issue(issues, sample_limit, "empty_referenced_source_position_bounds")
    if native_bounds.count == 0:
        add_issue(issues, sample_limit, "empty_native_vertex_bounds")

    source_span = span_tuple(source_bounds)
    preview_span = span_tuple(preview_bounds)
    preview_to_source_span_ratio = [
        round_float(preview_span[index] / source_span[index]) if source_span[index] > 0 else None
        for index in range(3)
    ]
    audit = {
        "format": AUDIT_FORMAT,
        "status": "valid" if not issues else "invalid",
        "bind_pose": str(bind_pose_path),
        "native_manifest": str(native_manifest_path),
        "contract": {
            "native_position_source": "source_position",
            "native_normal_source": "source_normal",
            "diagnostic_preview_position_source": "bind_pose_preview_position",
            "position_tolerance": position_tolerance,
            "selection_filter": "native_manifest.selection_primitive_keys" if selection_primitive_keys is not None else None,
        },
        "manifest_contract": {
            "position_source": native_manifest.get("position_source"),
            "normal_source": native_manifest.get("normal_source"),
            "diagnostic_preview_position_source": native_manifest.get("diagnostic_preview_position_source"),
            "static_bind_pose_contract": native_manifest.get("static_bind_pose_contract"),
            "selection_profile_id": native_manifest.get("selection_profile_id"),
            "selection_source": native_manifest.get("selection_source"),
        },
        "counts": {
            "selection_primitive_key_count": len(selection_primitive_keys) if selection_primitive_keys is not None else 0,
            "referenced_source_position_count": source_bounds.count,
            "referenced_preview_position_count": preview_bounds.count,
            "native_vertex_count": native_bounds.count,
            "native_vertex_resource_count": native_counts["vertex_resource"],
            "missing_native_vertex_file_count": native_counts["missing_vertex_file"],
            "invalid_native_vertex_file_count": native_counts["invalid_vertex_file"],
            "checked_skinned_primitive_count": len(expected_skinned_primitive_bounds),
            "skinned_primitive_bounds_mismatch_count": primitive_mismatch_count,
            "issue_count": len(issues),
        },
        "bounds": {
            "referenced_source_position_rounded": source_bounds.to_json(),
            "referenced_bind_pose_preview_position": preview_bounds.to_json(),
            "native_vertices": native_bounds.to_json(),
            "preview_to_source_span_ratio": preview_to_source_span_ratio,
        },
        "issues": issues[:sample_limit],
    }

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(audit, indent=2) + "\n", encoding="utf-8")
    return audit


def read_json(path: Path) -> dict[str, object]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8-sig"))
    except OSError as exc:
        raise ParseError(f"{path}: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise ParseError(f"{path}: invalid JSON: {exc}") from exc
    if not isinstance(payload, dict):
        raise ParseError(f"{path}: expected JSON object")
    return payload


def native_selection_primitive_keys(native_manifest: dict[str, object]) -> frozenset[tuple[int, int]] | None:
    raw_keys = native_manifest.get("selection_primitive_keys")
    if not isinstance(raw_keys, list) or not raw_keys:
        return None
    keys: set[tuple[int, int]] = set()
    for raw_key in raw_keys:
        if not isinstance(raw_key, dict):
            continue
        mesh_index = raw_key.get("mesh_index")
        primitive_index = raw_key.get("primitive_index")
        if not isinstance(mesh_index, int) or not isinstance(primitive_index, int):
            continue
        if mesh_index < 0 or primitive_index < 0:
            continue
        keys.add((mesh_index, primitive_index))
    return frozenset(keys) if keys else None


def referenced_bind_pose_bounds(
    bind_pose: dict[str, object],
    key: str,
    *,
    round_values: bool,
    primitive_filter: frozenset[tuple[int, int]] | None = None,
) -> Bounds:
    return merge_bounds(
        referenced_bind_pose_primitive_bounds(
            bind_pose,
            key,
            round_values=round_values,
            primitive_filter=primitive_filter,
        ).values()
    )


def referenced_bind_pose_primitive_bounds(
    bind_pose: dict[str, object],
    key: str,
    *,
    rigid_key: str | None = None,
    round_values: bool,
    primitive_filter: frozenset[tuple[int, int]] | None = None,
) -> dict[tuple[int, int], Bounds]:
    primitive_bounds: dict[tuple[int, int], Bounds] = {}
    meshes = bind_pose.get("meshes")
    if not isinstance(meshes, list):
        return primitive_bounds
    for mesh in meshes:
        if not isinstance(mesh, dict):
            continue
        mesh_index = mesh.get("mesh_index")
        if not isinstance(mesh_index, int):
            continue
        primitives = mesh.get("primitives")
        if not isinstance(primitives, list):
            continue
        for primitive in primitives:
            if not isinstance(primitive, dict):
                continue
            primitive_index = primitive.get("primitive_index")
            if not isinstance(primitive_index, int):
                continue
            primitive_key = (mesh_index, primitive_index)
            if primitive_filter is not None and primitive_key not in primitive_filter:
                continue
            position_key = rigid_key if primitive.get("skinning_mode") == 0 and rigid_key is not None else key
            vertices = primitive.get("vertices")
            indices = primitive.get("indices")
            if not isinstance(vertices, list) or not isinstance(indices, list):
                continue
            bounds = Bounds()
            for index in indices:
                if not isinstance(index, int) or index < 0 or index >= len(vertices):
                    continue
                vertex = vertices[index]
                if not isinstance(vertex, dict):
                    continue
                position = read_vec3(vertex.get(position_key))
                if round_values:
                    position = Vec3(round_s16(position.x), round_s16(position.y), round_s16(position.z))
                bounds.add(position)
            primitive_bounds[primitive_key] = bounds
    return primitive_bounds


def merge_bounds(items) -> Bounds:
    bounds = Bounds()
    total_count = 0
    for item in items:
        if not isinstance(item, Bounds) or item.count == 0:
            continue
        total_count += item.count
        bounds.add(Vec3(item.min_x, item.min_y, item.min_z))
        bounds.add(Vec3(item.max_x, item.max_y, item.max_z))
    if total_count > 0:
        bounds.count = total_count
    return bounds


def native_vertex_bounds(
    native_manifest: dict[str, object],
    native_manifest_path: Path,
    issues: list[dict[str, object]],
    sample_limit: int,
) -> tuple[Bounds, Counter[str], dict[tuple[int, int], Bounds]]:
    bounds = Bounds()
    counts: Counter[str] = Counter()
    primitive_bounds: dict[tuple[int, int], Bounds] = {}
    resources = native_manifest.get("resources")
    if not isinstance(resources, list):
        add_issue(issues, sample_limit, "native_manifest_missing_resource_list")
        return bounds, counts, primitive_bounds

    for resource in resources:
        if not isinstance(resource, dict) or resource.get("kind") != "Vertex":
            continue
        counts["vertex_resource"] += 1
        primitive_key = native_vertex_primitive_key(resource)
        if primitive_key is not None and primitive_key not in primitive_bounds:
            primitive_bounds[primitive_key] = Bounds()
        file_path = resolve_resource_file(native_manifest_path, resource)
        if not file_path.is_file():
            counts["missing_vertex_file"] += 1
            add_issue(issues, sample_limit, "missing_native_vertex_file", file=str(file_path))
            continue
        try:
            root = ET.parse(file_path).getroot()
            if root.tag != "Vertex":
                raise ParseError(f"unexpected root tag {root.tag}")
            for element in root.findall("Vtx"):
                position = Vec3(
                    float(int(element.attrib["X"])),
                    float(int(element.attrib["Y"])),
                    float(int(element.attrib["Z"])),
                )
                bounds.add(position)
                if primitive_key is not None:
                    primitive_bounds[primitive_key].add(position)
        except Exception as exc:
            counts["invalid_vertex_file"] += 1
            add_issue(issues, sample_limit, "invalid_native_vertex_file", file=str(file_path), error=str(exc))
    return bounds, counts, primitive_bounds


def native_vertex_primitive_key(resource: dict[str, object]) -> tuple[int, int] | None:
    path = resource.get("path")
    if not isinstance(path, str):
        return None
    match = re.search(r"_mesh_(\d+)_prim_(\d+)_batch_\d+_vtx$", Path(path).name)
    if match is None:
        return None
    return int(match.group(1)), int(match.group(2))


def compare_skinned_primitive_bounds(
    expected: dict[tuple[int, int], Bounds],
    actual: dict[tuple[int, int], Bounds],
    issues: list[dict[str, object]],
    sample_limit: int,
    tolerance: float,
) -> int:
    mismatch_count = 0
    for key, expected_bounds in sorted(expected.items()):
        actual_bounds = actual.get(key)
        if actual_bounds is None or actual_bounds.count == 0:
            mismatch_count += 1
            add_issue(
                issues,
                sample_limit,
                "missing_native_skinned_primitive_vertex_bounds",
                mesh_index=key[0],
                primitive_index=key[1],
            )
            continue
        delta = compare_bounds(expected_bounds, actual_bounds)
        max_delta = max(delta.values(), default=math.inf)
        if max_delta > tolerance:
            mismatch_count += 1
            add_issue(
                issues,
                sample_limit,
                "native_skinned_primitive_bounds_do_not_match_source_position",
                mesh_index=key[0],
                primitive_index=key[1],
                max_abs_delta=round_float(max_delta),
                tolerance=tolerance,
            )
    return mismatch_count


def resolve_resource_file(native_manifest_path: Path, resource: dict[str, object]) -> Path:
    file_value = resource.get("file")
    if isinstance(file_value, str) and file_value:
        path = Path(file_value)
        if path.is_absolute() or path.exists():
            return path
        return native_manifest_path.parent / path
    path_value = resource.get("path")
    if isinstance(path_value, str) and path_value:
        return native_manifest_path.parent / Path(path_value).name
    return native_manifest_path.parent / "<missing>"


def compare_bounds(expected: Bounds, actual: Bounds) -> dict[str, float]:
    if expected.count == 0 or actual.count == 0:
        return {"min_x": math.inf, "min_y": math.inf, "min_z": math.inf, "max_x": math.inf, "max_y": math.inf, "max_z": math.inf}
    return {
        "min_x": abs(expected.min_x - actual.min_x),
        "min_y": abs(expected.min_y - actual.min_y),
        "min_z": abs(expected.min_z - actual.min_z),
        "max_x": abs(expected.max_x - actual.max_x),
        "max_y": abs(expected.max_y - actual.max_y),
        "max_z": abs(expected.max_z - actual.max_z),
    }


def span_tuple(bounds: Bounds) -> tuple[float, float, float]:
    if bounds.count == 0:
        return (0.0, 0.0, 0.0)
    return (
        bounds.max_x - bounds.min_x,
        bounds.max_y - bounds.min_y,
        bounds.max_z - bounds.min_z,
    )


def read_vec3(value: object) -> Vec3:
    if isinstance(value, list) and len(value) == 3 and all(isinstance(item, (int, float)) for item in value):
        return Vec3(float(value[0]), float(value[1]), float(value[2]))
    raise ParseError("expected Vec3 array")


def add_issue(issues: list[dict[str, object]], sample_limit: int, reason: str, **kwargs: object) -> None:
    if len(issues) >= sample_limit:
        return
    issue = {"reason": reason}
    issue.update(kwargs)
    issues.append(issue)


def round_s16(value: float) -> float:
    return float(max(-32768, min(32767, int(round(value)))))


def round_float(value: float) -> float:
    if not math.isfinite(value):
        return value
    if math.isclose(value, round(value), abs_tol=0.00001):
        return float(round(value))
    return round(value, 6)
