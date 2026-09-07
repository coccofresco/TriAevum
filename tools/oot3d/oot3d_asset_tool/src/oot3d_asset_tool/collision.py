from __future__ import annotations

import math
import struct
import zipfile
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from typing import TypeVar

from .binary import ParseError
from .cmb import CmbModel, Material, Vec3
from .legacy_fast_resource import ConvertedResource, chunk_triangles, round_s16, write_text
from .zsi import (
    ZsiCollisionHeaderCandidate,
    ZsiCollisionPolygon,
    ZsiCollisionVertex,
    ZsiFile,
    ZsiSceneCommand,
    ZsiWaterBox,
)

FAST_RESOURCE_COLLISION = 0x4F434F4C
COLLISION_RESOURCE_FORMATS = {"binary", "xml"}
COLLISION_BUDGET_POLICIES = {"none", "preserve-floors", "largest"}
STATIC_LOOKUP_SUBDIV_AMOUNT = (16, 4, 16)
STATIC_LOOKUP_SUBDIV_OVERLAP = 50.0
STATIC_LOOKUP_SUBDIV_MIN = 150.0
STATIC_LOOKUP_SAFETY_MARGIN = 512
COLPOLY_FLOOR_NORMAL_Y = int(32767 * 0.5)
COLPOLY_CEILING_NORMAL_Y = int(32767 * -0.8)
DEFAULT_REFERENCE_FLOOR_PROBE_COUNT = 512
DEFAULT_REFERENCE_FLOOR_QUERY_ABOVE = 50.0
DEFAULT_REFERENCE_FLOOR_TOLERANCE = 96.0
DEFAULT_REFERENCE_MIN_FLOOR_HIT_RATE = 0.85
DEFAULT_REFERENCE_BOUNDS_TOLERANCE = 2048.0
DEFAULT_REFERENCE_CATEGORY_MIN_RATIO = 0.5
DEFAULT_REFERENCE_CATEGORY_MAX_RATIO = 4.0
DEFAULT_REFERENCE_EXIT_INDEX_MIN_RATIO = 0.1
DEFAULT_REFERENCE_EXIT_INDEX_MAX_RATIO = 16.0
DEFAULT_REFERENCE_POLYGON_FLAG_MIN_RATIO = 0.85
DEFAULT_REFERENCE_WATER_BOX_EDGE_TOLERANCE = 256
DEFAULT_REFERENCE_WATER_BOX_MIN_OVERLAP_RATIO = 0.75
DEFAULT_VISUAL_MESH_FLOOR_PROBE_COUNT = 512
DEFAULT_VISUAL_MESH_DIAGNOSIS_MATCH_MARGIN = 0.15
ZSI_ACTOR_ENTRY_SIZE = 0x10
ZSI_ENTRANCE_ENTRY_SIZE = 2
ZSI_PLAYER_ACTOR_ID = 0
ZSI_SPAWN_LIST_COMMAND_ID = 0x00
ZSI_ENTRANCE_LIST_COMMAND_ID = 0x06
ZSI_EXIT_LIST_COMMAND_ID = 0x13
ZSI_CUTSCENE_COMMAND_ID = 0x17
ZSI_LAYOUT_PREFIX_CANDIDATES = (0x10, 0x00, 0x04, 0x08, 0x0C)
ZSI_DIRECT_FILE_OFFSET_COMMAND_IDS = {
    0x00,
    0x01,
    0x03,
    0x04,
    0x06,
    0x0A,
    0x0B,
    0x0C,
    0x0D,
    0x0E,
    0x0F,
    0x13,
    0x17,
    0x18,
}
ZSI_EXIT_VALUE_SAMPLE_LIMIT = 32
T = TypeVar("T")

POLYGON_FLAG_SEMANTICS = (
    "polygon.conveyor",
    "polygon.ignore_camera",
    "polygon.ignore_entities",
    "polygon.ignore_projectiles",
)

WALL_PROPERTY_FLAGS = (
    0,
    1,
    3,
    5,
    8,
    16,
    32,
    64,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
)

FOLIAGE_TEXTURE_TOKENS = (
    "kusa",
    "tuta",
    "_ha_",
    "ha_",
)


@dataclass(frozen=True)
class CollisionPolygon:
    type: int
    vertex_a: int
    vertex_b: int
    vertex_c: int
    normal_x: int
    normal_y: int
    normal_z: int
    dist: int


@dataclass(frozen=True)
class SurfaceType:
    data1: int
    data2: int


@dataclass(frozen=True)
class CameraData:
    stype: int
    num_data: int
    camera_pos_data_index: int


@dataclass(frozen=True)
class CameraPositionData:
    pos: tuple[int, int, int]
    rot: tuple[int, int, int]
    other: tuple[int, int, int]


@dataclass(frozen=True)
class WaterBox:
    x_min: int
    y_surface: int
    z_min: int
    x_length: int
    z_length: int
    properties: int


@dataclass(frozen=True)
class CollisionMetadata:
    camera_data: tuple[CameraData, ...] = ()
    camera_positions: tuple[CameraPositionData, ...] = ()
    water_boxes: tuple[WaterBox, ...] = ()
    surface_types: tuple[SurfaceType, ...] = ()


@dataclass(frozen=True)
class CollisionScene:
    vertices: tuple[tuple[int, int, int], ...]
    polygons: tuple[CollisionPolygon, ...]
    metadata: CollisionMetadata
    source: str = ""
    source_format: str = "shipwright_collision_header"
    bounds_min: tuple[int, int, int] | None = None
    bounds_max: tuple[int, int, int] | None = None

    @property
    def effective_bounds_min(self) -> tuple[int, int, int]:
        if self.bounds_min is not None:
            return self.bounds_min
        return collision_bounds(self.vertices)[0]

    @property
    def effective_bounds_max(self) -> tuple[int, int, int]:
        if self.bounds_max is not None:
            return self.bounds_max
        return collision_bounds(self.vertices)[1]


@dataclass(frozen=True)
class CollisionBuildResult:
    resource: ConvertedResource
    stats: dict[str, object]


@dataclass(frozen=True)
class StaticLookupPolygonEstimate:
    index: int
    category: str
    node_count: int


@dataclass(frozen=True)
class StaticLookupEstimate:
    node_count: int
    node_budget: int | None
    polygon_estimates: tuple[StaticLookupPolygonEstimate, ...]


@dataclass
class CollisionBuilder:
    vertices: list[tuple[int, int, int]] = field(default_factory=list)
    vertex_indices: dict[tuple[int, int, int], int] = field(default_factory=dict)
    polygons: list[CollisionPolygon] = field(default_factory=list)
    skipped: Counter[str] = field(default_factory=Counter)

    def vertex_index(self, vertex: tuple[int, int, int]) -> int:
        existing = self.vertex_indices.get(vertex)
        if existing is not None:
            return existing
        index = len(self.vertices)
        if index > 0x1FFF:
            raise ParseError("collision vertex index limit exceeded (max 8192 unique vertices)")
        self.vertex_indices[vertex] = index
        self.vertices.append(vertex)
        return index


def discover_scene_collision_path_from_o2r(path: Path, scene: str) -> str | None:
    scene_dir = f"{scene.lower()}_scene"
    filename_prefix = f"{scene.lower()}_scenecollisionheader"
    with zipfile.ZipFile(path) as archive:
        candidates = sorted(
            {
                name
                for name in archive.namelist()
                if collision_resource_path_matches_scene(
                    name,
                    scene_dir=scene_dir,
                    filename_prefix=filename_prefix,
                )
            },
            key=scene_collision_header_candidate_priority,
        )
    if not candidates:
        return None
    if len(candidates) > 1:
        raise ParseError(
            f"{path}: found multiple collision headers for {scene!r}: "
            + ", ".join(candidates)
        )
    return candidates[0]


def collision_resource_path_matches_scene(
    resource_path: str,
    *,
    scene_dir: str,
    filename_prefix: str,
) -> bool:
    normalized = resource_path.replace("\\", "/").lower()
    parts = normalized.split("/")
    if len(parts) < 2:
        return False
    filename = parts[-1]
    return (
        scene_dir in parts[:-1]
        and filename.startswith(filename_prefix)
        and "collisionheader" in filename
    )


def scene_collision_header_candidate_priority(path: str) -> tuple[int, int, str]:
    normalized = path.replace("\\", "/").lower()
    if normalized.startswith("scenes/shared/"):
        scope_priority = 0
    elif normalized.startswith("scenes/nonmq/"):
        scope_priority = 1
    elif normalized.startswith("scenes/mq/"):
        scope_priority = 2
    else:
        scope_priority = 3
    return (scope_priority, normalized.count("/"), normalized)


def export_scene_collision(
    models: list[tuple[int, CmbModel]],
    output_dir: Path,
    resource_path: str,
    *,
    metadata: CollisionMetadata | None = None,
    resource_format: str = "binary",
) -> CollisionBuildResult:
    builder = build_scene_collision(models)
    if not builder.vertices or not builder.polygons:
        raise ParseError("scene collision export produced no polygons")

    resource_name = resource_path.rstrip("/").split("/")[-1]
    file_path = output_dir / resource_name
    write_collision_resource(
        file_path,
        tuple(builder.vertices),
        tuple(builder.polygons),
        metadata or CollisionMetadata(),
        resource_format=resource_format,
    )

    resource = ConvertedResource("CollisionHeader", resource_path, str(file_path))
    stats: dict[str, object] = {
        "resource_format": resource_format,
        "resource_path": resource_path,
        "vertex_count": len(builder.vertices),
        "polygon_count": len(builder.polygons),
        "skipped": dict(builder.skipped),
        "metadata": {
            "camera_data_count": len((metadata or CollisionMetadata()).camera_data),
            "camera_position_count": len((metadata or CollisionMetadata()).camera_positions),
            "water_box_count": len((metadata or CollisionMetadata()).water_boxes),
            "surface_type_count": len((metadata or CollisionMetadata()).surface_types),
        },
    }
    return CollisionBuildResult(resource=resource, stats=stats)


def export_zsi_scene_collision(
    scene_zsi_path: Path,
    output_dir: Path,
    resource_path: str,
    *,
    candidate_index: int = 0,
    resource_format: str = "binary",
    static_lookup_node_budget: int | None = None,
    static_lookup_filter_policy: str = "none",
    reference_o2r: Path | None = None,
    reference_resource_path: str | None = None,
    reference_floor_probe_count: int = DEFAULT_REFERENCE_FLOOR_PROBE_COUNT,
    reference_floor_query_above: float = DEFAULT_REFERENCE_FLOOR_QUERY_ABOVE,
    reference_floor_tolerance: float = DEFAULT_REFERENCE_FLOOR_TOLERANCE,
    require_reference_match: bool = False,
    visual_models: list[tuple[int, CmbModel]] | None = None,
    visual_floor_probe_count: int = DEFAULT_VISUAL_MESH_FLOOR_PROBE_COUNT,
) -> CollisionBuildResult:
    validate_collision_resource_format(resource_format)
    validate_collision_budget_policy(static_lookup_filter_policy)
    if require_reference_match and reference_o2r is None:
        raise ParseError("--require-reference-match requires --reference-o2r")
    zsi = ZsiFile.from_path(scene_zsi_path)
    candidates = zsi.collision_header_candidates()
    if not candidates:
        raise ParseError(f"{scene_zsi_path}: no decoded static collision candidate found")
    if candidate_index < 0 or candidate_index >= len(candidates):
        raise ParseError(
            f"{scene_zsi_path}: collision candidate index {candidate_index} "
            f"out of range 0..{len(candidates) - 1}"
        )

    candidate = candidates[candidate_index]
    scene = normalize_zsi_collision_candidate(candidate, source=str(scene_zsi_path))
    vertices = scene.vertices
    polygons = scene.polygons
    metadata = scene.metadata
    skipped_degenerate_polygons = len(candidate.polygons) - len(polygons)
    source_spawn_floor_probes, source_spawn_floor_probe_audit = (
        zsi_player_spawn_floor_probe_audit(zsi)
    )
    source_exit_list_audit = zsi_exit_list_audit(zsi)
    reference_scene: CollisionScene | None = None
    reference_path: str | None = None
    if reference_o2r is not None:
        reference_path = reference_resource_path or resource_path
        reference_scene = load_collision_scene_from_o2r(reference_o2r, reference_path)

    static_lookup_summary: dict[str, object] | None = None
    if static_lookup_node_budget is not None:
        polygons, static_lookup_summary = apply_static_lookup_budget(
            vertices,
            polygons,
            node_budget=static_lookup_node_budget,
            policy=static_lookup_filter_policy,
        )
        scene = CollisionScene(
            vertices=vertices,
            polygons=polygons,
            metadata=metadata,
            source=scene.source,
            source_format=scene.source_format,
            bounds_min=scene.bounds_min,
            bounds_max=scene.bounds_max,
        )
    if not polygons:
        raise ParseError(f"{scene_zsi_path}: decoded collision candidate contains no exportable polygons")

    visual_source_spawn_floor_audit: dict[str, object] | None = None
    if visual_models:
        visual_source_spawn_floor_audit = visual_mesh_source_spawn_floor_audit(
            visual_models,
            source_spawn_floor_probes,
            floor_query_above=reference_floor_query_above,
            floor_tolerance=reference_floor_tolerance,
        )

    reference_comparison: dict[str, object] | None = None
    native_acceptance: dict[str, object] | None = None
    if reference_o2r is not None and reference_scene is not None and reference_path is not None:
        reference_comparison = compare_collision_scenes(
            reference_scene,
            scene,
            floor_probe_count=reference_floor_probe_count,
            floor_query_above=reference_floor_query_above,
            floor_tolerance=reference_floor_tolerance,
            source_spawn_floor_probes=source_spawn_floor_probes,
        )
        native_acceptance = native_zsi_collision_acceptance_gate(
            source_spawn_floor_probe_audit,
            source_exit_list_audit,
            reference_comparison,
            visual_source_spawn_floor_audit=visual_source_spawn_floor_audit,
        )
        failed_checks = list(reference_comparison["failed_checks"])
        failed_checks.extend(
            f"native_zsi.{name}"
            for name in native_acceptance["failed_checks"]
        )
        if require_reference_match and failed_checks:
            raise ParseError(
                f"{scene_zsi_path}: OOT3D collision does not match reference "
                f"{reference_o2r}!{reference_path}: "
                + "; ".join(failed_checks)
            )

    visual_mesh_floor_audit: dict[str, object] | None = None
    if visual_models:
        visual_mesh_floor_audit = visual_mesh_collision_floor_audit(
            visual_models,
            scene,
            reference_scene=reference_scene,
            floor_probe_count=visual_floor_probe_count,
            floor_query_above=reference_floor_query_above,
            floor_tolerance=reference_floor_tolerance,
        )

    visual_diagnostic_policy = collision_visual_diagnostic_policy(
        reference_comparison,
        native_acceptance,
        visual_mesh_floor_audit,
        visual_source_spawn_floor_audit,
    )

    output_dir.mkdir(parents=True, exist_ok=True)
    resource_name = resource_path.rstrip("/").split("/")[-1]
    file_path = output_dir / resource_name
    write_collision_resource(
        file_path,
        vertices,
        polygons,
        metadata,
        resource_format=resource_format,
        bounds_min=scene.effective_bounds_min,
        bounds_max=scene.effective_bounds_max,
    )

    resource = ConvertedResource("CollisionHeader", resource_path, str(file_path))
    stats: dict[str, object] = {
        "resource_format": resource_format,
        "resource_path": resource_path,
        "source": str(scene_zsi_path),
        "candidate_index": candidate_index,
        "candidate_offset": candidate.offset,
        "command_argument": candidate.command_argument,
        "bounds_min": scene.effective_bounds_min,
        "bounds_max": scene.effective_bounds_max,
        "vertex_count": len(vertices),
        "polygon_count": len(polygons),
        "source_polygon_count": len(candidate.polygons),
        "skipped_degenerate_polygon_count": skipped_degenerate_polygons,
        "surface_type_count": len(metadata.surface_types),
        "water_box_count": len(metadata.water_boxes),
        "decoded_camera_record_count": len(candidate.bg_cam_info),
        "effective_camera_record_count": len(candidate.effective_bg_cam_info),
        "exported_camera_record_count": len(metadata.camera_data),
        "camera_position_vec_count": len(candidate.camera_position_vectors),
        "camera_position_group_count": len(metadata.camera_positions),
        "source_spawn_floor_probe_count": len(source_spawn_floor_probes),
        "source_spawn_floor_probe_audit": source_spawn_floor_probe_audit,
        "source_exit_list_audit": source_exit_list_audit,
        "coordinate_transform": "effective OOT3D collision vertex (x, y, z) -> Shipwright/N64 (x, y, z)",
        "plane_source": "native OOT3D polygon normal/dist; effective vertices are used for triangle membership and degenerate rejection",
        "axis_audit": collision_axis_audit(candidate, vertices),
        "surface_type_offset": candidate.effective_surface_type_offset,
        "surface_type_audit": surface_type_audit(candidate),
        "surface_type_semantic_audit": surface_type_semantic_audit(candidate),
        "camera_data_offset": candidate.effective_bgcam_offset,
        "camera_position_offset": candidate.camera_position_offset,
        "visual_diagnostic_policy": visual_diagnostic_policy,
        "notes": [
            "degenerate polygons are skipped rather than exporting zero-area planes",
        ],
    }
    if static_lookup_summary is not None:
        stats["static_lookup"] = static_lookup_summary
    if reference_comparison is not None:
        stats["reference_comparison"] = reference_comparison
    if native_acceptance is not None:
        stats["native_zsi_acceptance"] = native_acceptance
    if visual_mesh_floor_audit is not None:
        stats["visual_mesh_floor_audit"] = visual_mesh_floor_audit
    if visual_source_spawn_floor_audit is not None:
        stats["visual_source_spawn_floor_audit"] = visual_source_spawn_floor_audit
    return CollisionBuildResult(resource=resource, stats=stats)


def collision_visual_diagnostic_policy(
    reference_comparison: dict[str, object] | None,
    native_acceptance: dict[str, object] | None,
    visual_mesh_floor_audit: dict[str, object] | None,
    visual_source_spawn_floor_audit: dict[str, object] | None,
) -> dict[str, object]:
    spawn_policy = {}
    if isinstance(native_acceptance, dict):
        maybe_spawn_policy = native_acceptance.get("source_spawn_floor_probe_policy")
        if isinstance(maybe_spawn_policy, dict):
            spawn_policy = maybe_spawn_policy

    relaxed_checks: list[str] = []
    if spawn_policy.get("status") == "source_spawn_failures_match_oot3d_visual_divergence":
        relaxed_checks.append("source_gameplay_spawn_probes_match_or_diagnosed")

    visual_floor_status = None
    visual_floor_diagnosis = None
    if isinstance(visual_mesh_floor_audit, dict):
        visual_floor_status = visual_mesh_floor_audit.get("status")
        visual_floor_diagnosis = visual_mesh_floor_audit.get("diagnosis")

    visual_spawn_status = None
    if isinstance(visual_source_spawn_floor_audit, dict):
        visual_spawn_status = visual_source_spawn_floor_audit.get("status")

    reference_failed_checks = []
    if isinstance(reference_comparison, dict):
        reference_failed_checks = list(reference_comparison.get("failed_checks", []))

    return {
        "format": "oot3d_collision_visual_diagnostic_policy_v1",
        "collision_source": "oot3d_zsi_native_collision",
        "visual_mesh_is_collision_source": False,
        "n64_reference_role": "validation_oracle_not_collision_input",
        "visual_mesh_role": "diagnostic_oracle_not_collision_input",
        "reference_comparison_required": reference_comparison is not None,
        "reference_comparison_accepted": (
            bool(reference_comparison.get("accepted"))
            if isinstance(reference_comparison, dict)
            else None
        ),
        "native_zsi_acceptance_accepted": (
            bool(native_acceptance.get("accepted"))
            if isinstance(native_acceptance, dict)
            else None
        ),
        "visual_floor_audit_status": visual_floor_status,
        "visual_floor_diagnosis": visual_floor_diagnosis,
        "visual_source_spawn_audit_status": visual_spawn_status,
        "relaxed_checks_from_visual_diagnostics": relaxed_checks,
        "forbidden_visual_relaxations": [
            "reference_geometry_bounds",
            "reference_floor_wall_ceiling_category_ratios",
            "reference_floor_walkability",
            "camera_data_exact",
            "camera_position_exact",
            "surface_exit_index_usage",
            "water_box_coverage",
            "polygon_flag_coverage",
            "surface_type_record_structure",
            "native_collision_record_structure",
            "archive_resource_priority",
        ],
        "reference_failed_checks_still_blocking": reference_failed_checks,
        "decision_rule": (
            "Visual CMB room meshes are diagnostic-only. They cannot provide "
            "collision geometry or relax N64/Shipwright reference checks. The "
            "only accepted visual diagnostic relaxation is source-spawn static "
            "floor probing when every failed native-ZSI source spawn also fails "
            "against the OOT3D visual mesh at the same source position."
        ),
    }


def validate_collision_resource_format(resource_format: str) -> None:
    if resource_format not in COLLISION_RESOURCE_FORMATS:
        raise ValueError(f"unsupported collision resource format: {resource_format}")


def validate_collision_budget_policy(policy: str) -> None:
    if policy not in COLLISION_BUDGET_POLICIES:
        raise ValueError(f"unsupported collision budget policy: {policy}")


def write_collision_resource(
    path: Path,
    vertices: tuple[tuple[int, int, int], ...],
    polygons: tuple[CollisionPolygon, ...],
    metadata: CollisionMetadata,
    *,
    resource_format: str = "binary",
    bounds_min: tuple[int, int, int] | None = None,
    bounds_max: tuple[int, int, int] | None = None,
) -> None:
    validate_collision_resource_format(resource_format)
    if resource_format == "xml":
        write_text(
            path,
            collision_header_xml(
                vertices,
                polygons,
                metadata,
                bounds_min=bounds_min,
                bounds_max=bounds_max,
            ),
        )
        return
    write_binary_collision_resource(
        path,
        vertices,
        polygons,
        metadata,
        bounds_min=bounds_min,
        bounds_max=bounds_max,
    )


def write_binary_collision_resource(
    path: Path,
    vertices: tuple[tuple[int, int, int], ...],
    polygons: tuple[CollisionPolygon, ...],
    metadata: CollisionMetadata,
    *,
    bounds_min: tuple[int, int, int] | None = None,
    bounds_max: tuple[int, int, int] | None = None,
) -> None:
    if not vertices:
        raise ParseError("collision resource requires at least one vertex")
    if len(vertices) > 0x7FFFFFFF:
        raise ParseError("collision vertex count exceeds binary resource limit")
    if len(polygons) > 0xFFFFFFFF:
        raise ParseError("collision polygon count exceeds binary resource limit")

    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as f:
        f.write(bytes([0, 0, 0, 0]))
        f.write(struct.pack("<I", FAST_RESOURCE_COLLISION))
        f.write(struct.pack("<I", 0))
        f.write(struct.pack("<Q", 0xDEADBEEFDEADBEEF))
        f.write(struct.pack("<I", 0))
        f.write(struct.pack("<Q", 0))
        f.write(struct.pack("<I", 0))
        while f.tell() < 0x40:
            f.write(struct.pack("<I", 0))

        effective_bounds_min, effective_bounds_max = collision_resource_bounds(
            vertices,
            bounds_min=bounds_min,
            bounds_max=bounds_max,
        )
        f.write(struct.pack("<hhhhhh", *effective_bounds_min, *effective_bounds_max))
        f.write(struct.pack("<i", len(vertices)))
        for vertex in vertices:
            f.write(struct.pack("<hhh", *vertex))

        f.write(struct.pack("<I", len(polygons)))
        for polygon in polygons:
            f.write(
                struct.pack(
                    "<HHHHhhhh",
                    polygon.type,
                    polygon.vertex_a,
                    polygon.vertex_b,
                    polygon.vertex_c,
                    polygon.normal_x,
                    polygon.normal_y,
                    polygon.normal_z,
                    polygon.dist,
                )
            )

        surface_types = metadata.surface_types or (SurfaceType(0, 0),)
        f.write(struct.pack("<I", len(surface_types)))
        for surface_type in surface_types:
            f.write(struct.pack("<II", surface_type.data2, surface_type.data1))

        f.write(struct.pack("<I", len(metadata.camera_data)))
        for entry in metadata.camera_data:
            f.write(struct.pack("<Hhi", entry.stype, entry.num_data, entry.camera_pos_data_index))

        f.write(struct.pack("<i", len(metadata.camera_positions) * 3))
        for entry in metadata.camera_positions:
            f.write(struct.pack("<hhh", *entry.pos))
            f.write(struct.pack("<hhh", *entry.rot))
            f.write(struct.pack("<hhh", *entry.other))

        f.write(struct.pack("<i", len(metadata.water_boxes)))
        for water_box in metadata.water_boxes:
            f.write(
                struct.pack(
                    "<hhhhhi",
                    water_box.x_min,
                    water_box.y_surface,
                    water_box.z_min,
                    water_box.x_length,
                    water_box.z_length,
                    water_box.properties,
                )
            )


def apply_static_lookup_budget(
    vertices: tuple[tuple[int, int, int], ...],
    polygons: tuple[CollisionPolygon, ...],
    *,
    node_budget: int,
    policy: str,
) -> tuple[tuple[CollisionPolygon, ...], dict[str, object]]:
    if node_budget <= 0:
        raise ParseError("collision static lookup node budget must be positive")
    estimate = estimate_static_lookup(vertices, polygons, node_budget=node_budget)
    if policy == "none":
        if estimate.node_count > node_budget:
            raise ParseError(
                f"collision static lookup estimate {estimate.node_count} exceeds budget {node_budget}"
            )
        return polygons, static_lookup_summary(estimate, (), policy)

    target_budget = max(1, node_budget - STATIC_LOOKUP_SAFETY_MARGIN)
    estimates_by_index = {entry.index: entry for entry in estimate.polygon_estimates}
    if policy == "preserve-floors":
        order = sorted(
            estimate.polygon_estimates,
            key=lambda entry: (entry.category == "floor", -entry.node_count, entry.index),
        )
    elif policy == "largest":
        order = sorted(
            estimate.polygon_estimates,
            key=lambda entry: (-entry.node_count, entry.index),
        )
    else:
        validate_collision_budget_policy(policy)
        raise AssertionError("unreachable collision budget policy")

    removed_indices: set[int] = set()
    node_count = estimate.node_count
    for entry in order:
        if node_count <= target_budget:
            break
        removed_indices.add(entry.index)
        node_count -= entry.node_count

    if node_count > target_budget:
        raise ParseError(
            f"collision static lookup estimate {node_count} still exceeds budget {target_budget}"
        )

    filtered_polygons = tuple(
        polygon for index, polygon in enumerate(polygons)
        if index not in removed_indices
    )
    removed = tuple(estimates_by_index[index] for index in sorted(removed_indices))
    filtered_estimate = StaticLookupEstimate(node_count, node_budget, ())
    return filtered_polygons, static_lookup_summary(estimate, removed, policy, filtered_estimate)


def static_lookup_summary(
    estimate: StaticLookupEstimate,
    removed: tuple[StaticLookupPolygonEstimate, ...],
    policy: str,
    filtered_estimate: StaticLookupEstimate | None = None,
) -> dict[str, object]:
    before_by_category: dict[str, dict[str, int]] = {}
    for entry in estimate.polygon_estimates:
        category = before_by_category.setdefault(entry.category, {"polygons": 0, "nodes": 0})
        category["polygons"] += 1
        category["nodes"] += entry.node_count

    removed_by_category: dict[str, dict[str, int]] = {}
    for entry in removed:
        category = removed_by_category.setdefault(entry.category, {"polygons": 0, "nodes": 0})
        category["polygons"] += 1
        category["nodes"] += entry.node_count

    after_count = filtered_estimate.node_count if filtered_estimate is not None else estimate.node_count
    return {
        "policy": policy,
        "node_budget": estimate.node_budget,
        "safety_margin": STATIC_LOOKUP_SAFETY_MARGIN if removed else 0,
        "estimated_nodes_before": estimate.node_count,
        "estimated_nodes_after": after_count,
        "removed_polygon_count": len(removed),
        "before_by_category": before_by_category,
        "removed_by_category": removed_by_category,
        "removed_top": [
            {
                "index": entry.index,
                "category": entry.category,
                "nodes": entry.node_count,
            }
            for entry in sorted(removed, key=lambda item: -item.node_count)[:16]
        ],
    }


def floor_probe_result_summary(
    vertices: tuple[tuple[int, int, int], ...],
    polygon_entries: tuple[tuple[int, CollisionPolygon], ...],
    probes: tuple[tuple[float, float, float], ...],
    query_above: float,
) -> list[dict[str, object]]:
    results: list[dict[str, object]] = []
    for probe_index, (x, expected_y, z) in enumerate(probes):
        entry: dict[str, object] = {
            "probe_index": probe_index,
            "x": round(x, 3),
            "expected_y": round(expected_y, 3),
            "z": round(z, 3),
        }
        hit = raycast_floor_probe(vertices, polygon_entries, x, expected_y + query_above, z)
        if hit is None:
            entry["hit"] = False
        else:
            polygon_index, floor_y, polygon = hit
            entry.update(
                {
                    "hit": True,
                    "polygon_index": polygon_index,
                    "type": polygon.type,
                    "normal_y": polygon.normal_y,
                    "floor_y": round(floor_y, 3),
                    "delta_y": round(floor_y - expected_y, 3),
                }
            )
        results.append(entry)
    return results


def zsi_player_spawn_floor_probe_audit(
    zsi: ZsiFile,
) -> tuple[tuple[tuple[float, float, float], ...], dict[str, object]]:
    probes: list[tuple[float, float, float]] = []
    probe_records: list[dict[str, object]] = []
    deferred_probe_records: list[dict[str, object]] = []
    setup_records: list[dict[str, object]] = []
    seen: set[tuple[int, int, int]] = set()
    seen_all: set[tuple[int, int, int]] = set()
    data = zsi.data

    for setup in zsi.scene_setups():
        spawn_command = setup.first_command(ZSI_SPAWN_LIST_COMMAND_ID)
        if spawn_command is None:
            continue
        setup_role = (
            "cutscene"
            if setup.first_command(ZSI_CUTSCENE_COMMAND_ID) is not None
            else "gameplay"
        )

        spawn_candidate = select_zsi_player_spawn_candidate(
            data,
            spawn_command.argument,
            spawn_command.parameter,
        )
        if spawn_candidate is None:
            setup_records.append(
                {
                    "setup_index": setup.index,
                    "status": "no_valid_player_spawn_list",
                    "spawn_argument": spawn_command.argument,
                    "spawn_count": spawn_command.parameter,
                }
            )
            continue

        entrance_command = setup.first_command(ZSI_ENTRANCE_LIST_COMMAND_ID)
        entrance_candidate = select_zsi_entrance_candidate(
            data,
            entrance_command.argument,
            entrance_command.parameter,
            spawn_count=int(spawn_candidate["entry_count"]),
        ) if entrance_command is not None else None
        used_spawn_indices = (
            sorted({int(entry["spawn"]) for entry in entrance_candidate["entries"]})
            if entrance_candidate is not None
            else list(range(int(spawn_candidate["entry_count"])))
        )

        setup_probe_count = 0
        setup_required_probe_count = 0
        setup_deferred_probe_count = 0
        invalid_spawn_indices: list[int] = []
        non_player_spawn_indices: list[int] = []
        spawn_entries = list(spawn_candidate["entries"])
        for spawn_index in used_spawn_indices:
            if spawn_index < 0 or spawn_index >= len(spawn_entries):
                invalid_spawn_indices.append(spawn_index)
                continue
            entry = spawn_entries[spawn_index]
            if int(entry["actor_id"]) != ZSI_PLAYER_ACTOR_ID:
                non_player_spawn_indices.append(spawn_index)
                continue
            x, y, z = (int(value) for value in entry["pos"])
            if not all(-20000 <= value <= 20000 for value in (x, y, z)):
                invalid_spawn_indices.append(spawn_index)
                continue
            key = (x, y, z)
            if key in seen_all:
                continue
            seen_all.add(key)
            setup_probe_count += 1
            probe_record = {
                "setup_index": setup.index,
                "setup_role": setup_role,
                "spawn_index": spawn_index,
                "x": x,
                "y": y,
                "z": z,
                "spawn_start_delta": spawn_candidate["start_delta"],
                "entrance_start_delta": (
                    entrance_candidate.get("start_delta")
                    if entrance_candidate is not None
                    else None
                ),
            }
            if setup_role == "gameplay":
                if key in seen:
                    continue
                seen.add(key)
                probes.append((float(x), float(y), float(z)))
                setup_required_probe_count += 1
                probe_records.append(probe_record)
            else:
                setup_deferred_probe_count += 1
                deferred_probe_records.append(probe_record)

        setup_records.append(
            {
                "setup_index": setup.index,
                "setup_role": setup_role,
                "status": "decoded_source_player_spawn_floor_probes",
                "spawn_argument": spawn_command.argument,
                "spawn_start_offset": spawn_candidate["start_offset"],
                "spawn_start_delta": spawn_candidate["start_delta"],
                "spawn_entry_count": spawn_candidate["entry_count"],
                "spawn_player_entry_count": spawn_candidate["player_entry_count"],
                "entrance_argument": entrance_command.argument if entrance_command else None,
                "entrance_start_offset": (
                    entrance_candidate.get("start_offset")
                    if entrance_candidate is not None
                    else None
                ),
                "entrance_start_delta": (
                    entrance_candidate.get("start_delta")
                    if entrance_candidate is not None
                    else None
                ),
                "used_spawn_indices": used_spawn_indices,
                "probe_count": setup_probe_count,
                "required_probe_count": setup_required_probe_count,
                "deferred_probe_count": setup_deferred_probe_count,
                "invalid_spawn_indices": invalid_spawn_indices,
                "non_player_spawn_indices": non_player_spawn_indices,
            }
        )

    audit = {
        "status": (
            "decoded_source_player_spawn_floor_probes"
            if probes
            else "no_source_player_spawn_floor_probes"
        ),
        "scene_zsi": str(zsi.path),
        "probe_count": len(probes),
        "required_gameplay_probe_count": len(probes),
        "deferred_cutscene_probe_count": len(deferred_probe_records),
        "total_decoded_probe_count": len(probe_records) + len(deferred_probe_records),
        "unique_probe_count": len(seen),
        "total_unique_probe_count": len(seen_all),
        "setup_count": len(setup_records),
        "setup_records": setup_records,
        "probes_sample": probe_records[:32],
        "deferred_cutscene_probes_sample": deferred_probe_records[:32],
        "decode_policy": (
            "Scene spawn command 0x00 is decoded as ActorEntry records; "
            "entrance command 0x06 selects the used spawn indices. "
            "ZSI section prefix candidates are selected structurally, not by scene name. "
            "Gameplay setup spawns are required for environmental collision acceptance; "
            "setup headers carrying cutscene command 0x17 are audited separately for the cutscene gate."
        ),
    }
    return tuple(probes), audit


def select_zsi_player_spawn_candidate(
    data: bytes,
    offset: int,
    count: int,
) -> dict[str, object] | None:
    if count <= 0:
        return None
    candidates: list[dict[str, object]] = []
    for start_delta in ZSI_LAYOUT_PREFIX_CANDIDATES:
        start = offset + start_delta
        end = start + count * ZSI_ACTOR_ENTRY_SIZE
        if start < 0 or end > len(data):
            continue
        entries = [
            read_zsi_actor_entry(data, start + index * ZSI_ACTOR_ENTRY_SIZE, index)
            for index in range(count)
        ]
        player_entries = [
            entry
            for entry in entries
            if int(entry["actor_id"]) == ZSI_PLAYER_ACTOR_ID
        ]
        plausible_player_entries = [
            entry
            for entry in player_entries
            if all(-20000 <= int(value) <= 20000 for value in entry["pos"])
        ]
        candidate = {
            "start_offset": start,
            "start_delta": start_delta,
            "entry_count": count,
            "player_entry_count": len(player_entries),
            "plausible_player_entry_count": len(plausible_player_entries),
            "entries": entries,
        }
        candidates.append(candidate)

    if not candidates:
        return None
    candidates.sort(
        key=lambda candidate: (
            int(candidate["plausible_player_entry_count"]),
            int(candidate["player_entry_count"]),
            1 if int(candidate["start_delta"]) == 0x10 else 0,
            -int(candidate["start_delta"]),
        ),
        reverse=True,
    )
    selected = candidates[0]
    if int(selected["plausible_player_entry_count"]) == 0:
        return None
    return selected


def select_zsi_entrance_candidate(
    data: bytes,
    offset: int,
    count: int,
    *,
    spawn_count: int,
) -> dict[str, object] | None:
    if count <= 0 or spawn_count <= 0:
        return None
    candidates: list[dict[str, object]] = []
    for start_delta in ZSI_LAYOUT_PREFIX_CANDIDATES:
        start = offset + start_delta
        end = start + count * ZSI_ENTRANCE_ENTRY_SIZE
        if start < 0 or end > len(data):
            continue
        entries = [
            read_zsi_entrance_entry(data, start + index * ZSI_ENTRANCE_ENTRY_SIZE, index)
            for index in range(count)
        ]
        valid_entries = [
            entry
            for entry in entries
            if 0 <= int(entry["spawn"]) < spawn_count and -1 <= int(entry["room"]) <= 63
        ]
        sequential_entries = 0
        previous_spawn = -1
        for entry in entries:
            spawn = int(entry["spawn"])
            if previous_spawn >= 0 and spawn == previous_spawn + 1:
                sequential_entries += 1
            previous_spawn = spawn
        candidates.append(
            {
                "start_offset": start,
                "start_delta": start_delta,
                "entry_count": count,
                "valid_entry_count": len(valid_entries),
                "sequential_entry_count": sequential_entries,
                "entries": entries,
            }
        )

    if not candidates:
        return None
    candidates.sort(
        key=lambda candidate: (
            int(candidate["valid_entry_count"]),
            int(candidate["sequential_entry_count"]),
            1 if int(candidate["start_delta"]) == 0x10 else 0,
            -int(candidate["start_delta"]),
        ),
        reverse=True,
    )
    selected = candidates[0]
    if int(selected["valid_entry_count"]) != count:
        return None
    return selected


def read_zsi_actor_entry(data: bytes, offset: int, index: int) -> dict[str, object]:
    return {
        "index": index,
        "offset": offset,
        "actor_id": struct.unpack_from("<h", data, offset)[0],
        "pos": (
            struct.unpack_from("<h", data, offset + 2)[0],
            struct.unpack_from("<h", data, offset + 4)[0],
            struct.unpack_from("<h", data, offset + 6)[0],
        ),
        "rot": (
            struct.unpack_from("<h", data, offset + 8)[0],
            struct.unpack_from("<h", data, offset + 10)[0],
            struct.unpack_from("<h", data, offset + 12)[0],
        ),
        "params": struct.unpack_from("<h", data, offset + 14)[0],
    }


def read_zsi_entrance_entry(data: bytes, offset: int, index: int) -> dict[str, object]:
    return {
        "index": index,
        "offset": offset,
        "spawn": data[offset],
        "room": struct.unpack_from("<b", data, offset + 1)[0],
        "room_u8": data[offset + 1],
    }


def zsi_exit_list_audit(zsi: ZsiFile) -> dict[str, object]:
    records: list[dict[str, object]] = []
    status_counts: Counter[str] = Counter()
    exit_value_count = 0
    for setup in zsi.scene_setups():
        command = setup.first_command(ZSI_EXIT_LIST_COMMAND_ID)
        if command is None:
            continue
        payload_start = zsi_exit_payload_start_hint(zsi.data, command, setup.commands)
        payload_end = zsi_payload_end_hint(
            zsi.data,
            command,
            setup.commands,
            payload_start=payload_start,
        )
        layout = validate_zsi_exit_layout(
            zsi.data,
            command,
            setup.commands,
            payload_start=payload_start,
            payload_end=payload_end,
        )
        values = zsi_exit_values_from_payload(
            zsi.data,
            payload_start,
            payload_end,
        )
        status_counts[str(layout["status"])] += 1
        exit_value_count += len(values)
        records.append(
            {
                "setup_index": setup.index,
                "setup_role": (
                    "cutscene"
                    if setup.first_command(ZSI_CUTSCENE_COMMAND_ID) is not None
                    else "gameplay"
                ),
                "command_offset": command.offset,
                "command_argument": command.argument,
                "payload_start": payload_start,
                "payload_end": payload_end,
                "start_delta": payload_start - command.argument,
                "layout_validation": layout,
                "exit_value_count": len(values),
                "exit_values_sample": values[:16],
            }
        )

    accepted_statuses = {
        "validated_s16_exit_payload_window",
        "validated_prefixed_s16_exit_payload_window",
    }
    accepted = bool(records) and all(
        str(record["layout_validation"]["status"]) in accepted_statuses
        for record in records
    )
    return {
        "status": (
            "decoded_source_exit_lists"
            if records
            else "no_source_exit_lists"
        ),
        "accepted": accepted,
        "scene_zsi": str(zsi.path),
        "exit_list_command_count": len(records),
        "exit_value_candidate_total": exit_value_count,
        "layout_validation_counts": dict(sorted(status_counts.items())),
        "records_sample": records[:32],
        "decode_policy": (
            "Scene command 0x13 is decoded as a contiguous s16 exit list. "
            "A +0x10 payload start is selected only when the command argument "
            "overlaps the prefixed player-spawn ActorEntry block."
        ),
    }


def zsi_exit_payload_start_hint(
    data: bytes,
    command: ZsiSceneCommand,
    setup_commands: tuple[ZsiSceneCommand, ...],
) -> int:
    player_command = first_zsi_setup_command(setup_commands, ZSI_SPAWN_LIST_COMMAND_ID)
    if player_command is None:
        return command.argument
    player_start = player_command.argument + ZSI_ACTOR_ENTRY_SIZE
    player_end = player_start + player_command.parameter * ZSI_ACTOR_ENTRY_SIZE
    if (
        player_command.parameter > 0
        and player_end <= len(data)
        and all(
            struct.unpack_from("<h", data, player_start + index * ZSI_ACTOR_ENTRY_SIZE)[0] == 0
            for index in range(player_command.parameter)
        )
        and player_start <= command.argument < player_end
    ):
        return command.argument + ZSI_ACTOR_ENTRY_SIZE
    return command.argument


def first_zsi_setup_command(
    setup_commands: tuple[ZsiSceneCommand, ...],
    command_id: int,
) -> ZsiSceneCommand | None:
    for command in setup_commands:
        if command.command_id == command_id:
            return command
    return None


def zsi_payload_end_hint(
    data: bytes,
    command: ZsiSceneCommand,
    setup_commands: tuple[ZsiSceneCommand, ...],
    *,
    payload_start: int,
    default_window: int = 1024,
) -> int:
    if not (0 <= payload_start < len(data)):
        return payload_start
    if payload_start == command.argument:
        following_offsets = [
            other.argument
            for other in setup_commands
            if other.offset != command.offset
            and other.command_id in ZSI_DIRECT_FILE_OFFSET_COMMAND_IDS
            and command.argument < other.argument <= len(data)
        ]
        if following_offsets:
            return min(min(following_offsets), command.argument + default_window)
        return min(len(data), command.argument + default_window)

    following_offsets = [
        (
            other.argument + ZSI_ACTOR_ENTRY_SIZE
            if other.argument <= payload_start < other.argument + ZSI_ACTOR_ENTRY_SIZE
            else other.argument
        )
        for other in setup_commands
        if other.offset != command.offset
        and other.command_id in ZSI_DIRECT_FILE_OFFSET_COMMAND_IDS
    ]
    following_offsets = [
        offset
        for offset in following_offsets
        if payload_start < offset <= len(data)
    ]
    if following_offsets:
        return min(min(following_offsets), payload_start + default_window)
    return min(len(data), payload_start + default_window)


def validate_zsi_exit_layout(
    data: bytes,
    command: ZsiSceneCommand,
    setup_commands: tuple[ZsiSceneCommand, ...],
    *,
    payload_start: int,
    payload_end: int,
) -> dict[str, object]:
    if not (0 <= command.argument < len(data)):
        return {
            "status": "candidate_missing",
            "reason": "exit list argument is outside file",
        }
    if not (0 <= payload_start <= len(data)):
        return {
            "status": "candidate_missing",
            "reason": "exit list payload start is outside file",
            "payload_start": payload_start,
        }
    payload_size = payload_end - payload_start
    if payload_size <= 0:
        return {
            "status": "candidate_needs_validation",
            "reason": "exit list payload window is empty",
            "payload_start": payload_start,
            "payload_end_hint": payload_end,
        }
    if payload_size % 2:
        return {
            "status": "candidate_needs_validation",
            "reason": "exit list payload window is not s16 aligned",
            "payload_start": payload_start,
            "payload_end_hint": payload_end,
            "payload_size": payload_size,
        }
    return {
        "status": (
            "validated_prefixed_s16_exit_payload_window"
            if payload_start != command.argument
            else "validated_s16_exit_payload_window"
        ),
        "entry_count": payload_size // 2,
        "payload_start": payload_start,
        "start_delta": payload_start - command.argument,
        "payload_end_hint": payload_end,
        "payload_size": payload_size,
        "semantic_mapping": "code_bin_transition_mapping_confirmed",
    }


def zsi_exit_values_from_payload(
    data: bytes,
    payload_start: int,
    payload_end: int,
) -> list[dict[str, object]]:
    if not (0 <= payload_start < len(data)):
        return []
    sample_end = min(payload_end, payload_start + ZSI_EXIT_VALUE_SAMPLE_LIMIT * 2)
    values: list[dict[str, object]] = []
    for index, cursor in enumerate(range(payload_start, sample_end, 2)):
        if cursor + 2 > len(data):
            break
        value = struct.unpack_from("<h", data, cursor)[0]
        values.append(
            {
                "index": index,
                "offset": cursor,
                "value": value,
                "value_hex": f"0x{value & 0xFFFF:04x}",
            }
        )
    return values


def native_zsi_collision_acceptance_gate(
    source_spawn_floor_probe_audit: dict[str, object],
    source_exit_list_audit: dict[str, object],
    reference_comparison: dict[str, object],
    *,
    visual_source_spawn_floor_audit: dict[str, object] | None = None,
) -> dict[str, object]:
    spawn_summary = reference_comparison.get("source_spawn_floor_probes", {})
    required_spawn_count = int(
        source_spawn_floor_probe_audit.get("required_gameplay_probe_count", 0)
    )
    spawn_probe_policy = source_spawn_floor_probe_policy(
        spawn_summary,
        visual_source_spawn_floor_audit,
    )
    reference_exit_usage = dict(reference_comparison.get("reference_exit_index_usage", {}))
    converted_exit_usage = dict(reference_comparison.get("converted_exit_index_usage", {}))
    exit_usage_requires_list = bool(reference_exit_usage or converted_exit_usage)
    exit_list_count = int(source_exit_list_audit.get("exit_list_command_count", 0))
    exit_list_layout_accepted = bool(source_exit_list_audit.get("accepted"))
    exit_list_requirement_met = (
        exit_list_layout_accepted
        if exit_usage_requires_list or exit_list_count > 0
        else True
    )

    checks = {
        "source_gameplay_spawn_probes_decoded": required_spawn_count > 0,
        "source_gameplay_spawn_probes_match_or_diagnosed": bool(
            spawn_probe_policy["accepted"]
        ),
        "surface_exit_index_usage": "surface_exit_index_usage"
        not in set(reference_comparison.get("failed_checks", [])),
        "source_exit_list_layout": exit_list_requirement_met,
    }
    failed_checks = [name for name, passed in checks.items() if not passed]
    return {
        "format": "oot3d_native_zsi_collision_acceptance_v1",
        "accepted": not failed_checks,
        "failed_checks": failed_checks,
        "checks": checks,
        "source_gameplay_spawn_probe_count": required_spawn_count,
        "source_spawn_floor_probe_policy": spawn_probe_policy,
        "source_spawn_probe_summary": spawn_summary,
        "source_exit_list_status": source_exit_list_audit.get("status"),
        "source_exit_list_accepted": exit_list_layout_accepted,
        "source_exit_list_command_count": exit_list_count,
        "exit_usage_requires_source_list": exit_usage_requires_list,
        "reference_exit_index_usage": reference_exit_usage,
        "converted_exit_index_usage": converted_exit_usage,
        "decode_policy": (
            "The reference comparison validates geometry and surface behavior. "
            "This native-ZSI gate additionally requires gameplay source spawns "
            "to be decoded and either floor-matched or diagnosed as visual "
            "OOT3D/N64 spawn divergence, and requires command 0x13 exit lists "
            "to be structurally valid whenever exits are present in the "
            "surface-type usage or in the decoded source."
        ),
    }


def source_spawn_floor_probe_policy(
    spawn_summary: object,
    visual_source_spawn_floor_audit: dict[str, object] | None,
) -> dict[str, object]:
    if not isinstance(spawn_summary, dict) or not spawn_summary:
        return {
            "accepted": False,
            "status": "source_spawn_floor_summary_missing",
            "role": "native_zsi_acceptance_gate",
        }
    if bool(spawn_summary.get("accepted")):
        return {
            "accepted": True,
            "status": "native_zsi_spawn_probes_match",
            "role": "native_zsi_acceptance_gate",
        }

    converted_failure_count = int(spawn_summary.get("converted_failure_count", 0) or 0)
    converted_failures = [
        entry
        for entry in spawn_summary.get("converted_failures_sample", [])
        if isinstance(entry, dict)
    ]
    if converted_failure_count <= 0:
        return {
            "accepted": False,
            "status": "source_spawn_floor_summary_rejected_without_failures",
            "role": "native_zsi_acceptance_gate",
        }
    if len(converted_failures) < converted_failure_count:
        return {
            "accepted": False,
            "status": "source_spawn_failure_sample_incomplete",
            "role": "native_zsi_acceptance_gate",
            "converted_failure_count": converted_failure_count,
            "converted_failure_sample_count": len(converted_failures),
        }
    if (
        not isinstance(visual_source_spawn_floor_audit, dict)
        or visual_source_spawn_floor_audit.get("status")
        != "decoded_visual_mesh_source_spawn_floor_audit"
    ):
        return {
            "accepted": False,
            "status": "visual_source_spawn_floor_audit_unavailable",
            "role": "native_zsi_acceptance_gate",
            "converted_failure_count": converted_failure_count,
        }

    visual_metrics = visual_source_spawn_floor_audit.get("visual_mesh")
    if not isinstance(visual_metrics, dict):
        return {
            "accepted": False,
            "status": "visual_source_spawn_floor_metrics_missing",
            "role": "native_zsi_acceptance_gate",
            "converted_failure_count": converted_failure_count,
        }

    visual_failures = {
        int(entry["probe_index"]): entry
        for entry in visual_metrics.get("failures_sample", [])
        if isinstance(entry, dict) and "probe_index" in entry
    }
    converted_failure_indices = [
        int(entry["probe_index"])
        for entry in converted_failures
        if "probe_index" in entry
    ]
    if len(converted_failure_indices) < converted_failure_count:
        return {
            "accepted": False,
            "status": "converted_source_spawn_failure_indices_missing",
            "role": "native_zsi_acceptance_gate",
            "converted_failure_count": converted_failure_count,
            "converted_failure_index_count": len(converted_failure_indices),
        }

    visually_divergent_indices = [
        probe_index
        for probe_index in converted_failure_indices
        if probe_index in visual_failures
    ]
    visually_supported_indices = [
        probe_index
        for probe_index in converted_failure_indices
        if probe_index not in visual_failures
    ]
    if visually_supported_indices:
        return {
            "accepted": False,
            "status": "visual_mesh_supports_failed_source_spawn_floor",
            "role": "native_zsi_acceptance_gate",
            "converted_failure_count": converted_failure_count,
            "visually_supported_failure_indices": visually_supported_indices[:16],
            "visually_divergent_failure_indices": visually_divergent_indices[:16],
        }

    return {
        "accepted": True,
        "status": "source_spawn_failures_match_oot3d_visual_divergence",
        "role": "native_zsi_acceptance_gate",
        "converted_failure_count": converted_failure_count,
        "visually_divergent_failure_indices": visually_divergent_indices[:16],
        "decision_rule": (
            "Decoded source player-spawn probes are not used as hard static-floor "
            "acceptance probes when every failed native-ZSI probe also fails "
            "against the OOT3D visual mesh at the same source Y. The visual "
            "mesh remains diagnostic-only and does not add collision."
        ),
    }


def floor_probe_entry_matches(entry: dict[str, object], floor_tolerance: float) -> bool:
    return bool(entry.get("hit")) and abs(float(entry.get("delta_y", 0.0))) <= floor_tolerance


def floor_probe_acceptance_summary(
    reference: CollisionScene,
    converted: CollisionScene,
    probes: tuple[tuple[float, float, float], ...],
    *,
    query_above: float,
    floor_tolerance: float,
) -> dict[str, object]:
    reference_entries = tuple(enumerate(reference.polygons))
    converted_entries = tuple(enumerate(converted.polygons))
    reference_results = floor_probe_result_summary(
        reference.vertices,
        reference_entries,
        probes,
        query_above,
    )
    converted_results = floor_probe_result_summary(
        converted.vertices,
        converted_entries,
        probes,
        query_above,
    )
    converted_hits = [entry for entry in converted_results if entry.get("hit")]
    converted_matches = [
        entry
        for entry in converted_hits
        if floor_probe_entry_matches(entry, floor_tolerance)
    ]
    reference_hits = [entry for entry in reference_results if entry.get("hit")]
    reference_matches = [
        entry
        for entry in reference_hits
        if floor_probe_entry_matches(entry, floor_tolerance)
    ]
    required_failures: list[dict[str, object]] = []
    unsupported_probes: list[dict[str, object]] = []
    required_probe_count = 0
    for probe_index, converted_result in enumerate(converted_results):
        reference_match = floor_probe_entry_matches(reference_results[probe_index], floor_tolerance)
        if not reference_match:
            unsupported_probes.append(
                {
                    "probe_index": probe_index,
                    "probe": probes[probe_index],
                    "reference": reference_results[probe_index],
                }
            )
            continue
        required_probe_count += 1
        if not floor_probe_entry_matches(converted_result, floor_tolerance):
            required_failures.append(
                {
                    "probe_index": probe_index,
                    "probe": probes[probe_index],
                    "required_by": ["n64_reference"],
                    "converted": converted_result,
                    "reference": reference_results[probe_index],
                }
            )
    return {
        "accepted": len(required_failures) == 0,
        "probe_count": len(probes),
        "required_probe_count": required_probe_count,
        "query_above": query_above,
        "floor_tolerance": floor_tolerance,
        "converted_hit_count": len(converted_hits),
        "converted_match_count": len(converted_matches),
        "converted_hit_rate": round(len(converted_hits) / len(probes), 4) if probes else 1.0,
        "converted_match_rate": (
            round(
                (required_probe_count - len(required_failures)) / required_probe_count,
                4,
            )
            if required_probe_count
            else 1.0
        ),
        "converted_total_match_rate": round(len(converted_matches) / len(probes), 4) if probes else 1.0,
        "converted_failure_count": len(required_failures),
        "converted_failures_sample": [entry["converted"] for entry in required_failures[:16]],
        "required_failure_details_sample": required_failures[:16],
        "reference_hit_count": len(reference_hits),
        "reference_match_count": len(reference_matches),
        "reference_hit_rate": round(len(reference_hits) / len(probes), 4) if probes else 1.0,
        "reference_match_rate": round(len(reference_matches) / len(probes), 4) if probes else 1.0,
        "unsupported_source_probe_count": len(unsupported_probes),
        "unsupported_source_probe_sample": unsupported_probes[:16],
        "reference_results_sample": reference_results[:16],
        "converted_results_sample": converted_results[:16],
    }


def compare_collision_scenes(
    reference: CollisionScene,
    converted: CollisionScene,
    *,
    floor_probe_count: int = DEFAULT_REFERENCE_FLOOR_PROBE_COUNT,
    floor_query_above: float = DEFAULT_REFERENCE_FLOOR_QUERY_ABOVE,
    floor_tolerance: float = DEFAULT_REFERENCE_FLOOR_TOLERANCE,
    min_floor_hit_rate: float = DEFAULT_REFERENCE_MIN_FLOOR_HIT_RATE,
    bounds_tolerance: float = DEFAULT_REFERENCE_BOUNDS_TOLERANCE,
    category_min_ratio: float = DEFAULT_REFERENCE_CATEGORY_MIN_RATIO,
    category_max_ratio: float = DEFAULT_REFERENCE_CATEGORY_MAX_RATIO,
    exit_index_min_ratio: float = DEFAULT_REFERENCE_EXIT_INDEX_MIN_RATIO,
    exit_index_max_ratio: float = DEFAULT_REFERENCE_EXIT_INDEX_MAX_RATIO,
    polygon_flag_min_ratio: float = DEFAULT_REFERENCE_POLYGON_FLAG_MIN_RATIO,
    water_box_edge_tolerance: int = DEFAULT_REFERENCE_WATER_BOX_EDGE_TOLERANCE,
    water_box_min_overlap_ratio: float = DEFAULT_REFERENCE_WATER_BOX_MIN_OVERLAP_RATIO,
    source_spawn_floor_probes: tuple[tuple[float, float, float], ...] = (),
) -> dict[str, object]:
    probes = reference_floor_probes(reference, floor_probe_count)
    converted_entries = tuple(enumerate(converted.polygons))
    floor_results = floor_probe_result_summary(
        converted.vertices,
        converted_entries,
        probes,
        floor_query_above,
    )
    hit_results = [entry for entry in floor_results if entry.get("hit")]
    matched_results = [
        entry
        for entry in hit_results
        if abs(float(entry["delta_y"])) <= floor_tolerance
    ]
    floor_hit_rate = (len(hit_results) / len(probes)) if probes else 1.0
    floor_match_rate = (len(matched_results) / len(probes)) if probes else 1.0
    abs_deltas = [abs(float(entry["delta_y"])) for entry in hit_results]
    max_abs_floor_delta = max(abs_deltas) if abs_deltas else None
    average_abs_floor_delta = (sum(abs_deltas) / len(abs_deltas)) if abs_deltas else None
    resource_bounds_delta = collision_bounds_delta(reference, converted)
    geometry_bounds_delta = collision_geometry_bounds_delta(reference, converted)
    bounds_delta = geometry_bounds_delta
    max_bounds_delta = max(bounds_delta["absolute"])
    reference_summary = collision_scene_summary(reference)
    converted_summary = collision_scene_summary(converted)
    max_polygon_type = max((polygon.type for polygon in converted.polygons), default=-1)
    reference_semantics = collision_semantic_presence(reference)
    converted_semantics = collision_semantic_presence(converted)
    reference_exit_index_usage = collision_surface_field_usage(reference, "scene_exit_index")
    converted_exit_index_usage = collision_surface_field_usage(converted, "scene_exit_index")
    surface_exit_index_usage_summary = collision_surface_field_usage_match_summary(
        reference_exit_index_usage,
        converted_exit_index_usage,
        min_ratio=exit_index_min_ratio,
        max_ratio=exit_index_max_ratio,
    )
    metadata_match = collision_metadata_match(
        reference.metadata,
        converted.metadata,
        water_box_edge_tolerance=water_box_edge_tolerance,
        water_box_min_overlap_ratio=water_box_min_overlap_ratio,
    )
    missing_reference_semantics = [
        name
        for name, count in reference_semantics.items()
        if count > 0 and converted_semantics.get(name, 0) == 0
    ]
    polygon_flag_coverage_summary = collision_polygon_flag_coverage_summary(
        reference_semantics,
        converted_semantics,
        min_ratio=polygon_flag_min_ratio,
    )
    insufficient_polygon_flags = polygon_flag_coverage_summary["failed_records"]
    missing_reference_categories = [
        name
        for name, count in reference_summary["polygon_category_counts"].items()
        if count > 0 and converted_summary["polygon_category_counts"].get(name, 0) == 0
    ]
    category_ratio_summary = collision_category_ratio_summary(
        reference_summary["polygon_category_counts"],
        converted_summary["polygon_category_counts"],
        min_ratio=category_min_ratio,
        max_ratio=category_max_ratio,
    )
    source_spawn_floor_probe_summary = floor_probe_acceptance_summary(
        reference,
        converted,
        source_spawn_floor_probes,
        query_above=floor_query_above,
        floor_tolerance=floor_tolerance,
    )
    surface_semantics_present = not missing_reference_semantics

    checks = {
        "has_floor_probes": len(probes) > 0,
        "floor_hit_rate": floor_hit_rate >= min_floor_hit_rate,
        "floor_match_rate": floor_match_rate >= min_floor_hit_rate,
        "bounds_delta": max_bounds_delta <= bounds_tolerance,
        "polygon_categories_present": not missing_reference_categories,
        "polygon_category_ratios": bool(category_ratio_summary["accepted"]),
        "surface_type_coverage": max_polygon_type < len(converted.metadata.surface_types),
        "camera_data_exact": metadata_match["camera_data"]["accepted"],
        "camera_position_exact": metadata_match["camera_positions"]["accepted"],
        "water_box_coverage": metadata_match["water_boxes"]["accepted"],
        "surface_exit_index_usage": bool(surface_exit_index_usage_summary["accepted"]),
        "surface_semantics_present": surface_semantics_present,
        "polygon_flag_coverage": bool(polygon_flag_coverage_summary["accepted"]),
    }
    failed_checks = [name for name, passed in checks.items() if not passed]
    floor_failures = [
        entry
        for entry in floor_results
        if (not entry.get("hit")) or abs(float(entry.get("delta_y", 0.0))) > floor_tolerance
    ]
    return {
        "accepted": not failed_checks,
        "failed_checks": failed_checks,
        "thresholds": {
            "floor_probe_count": floor_probe_count,
            "floor_query_above": floor_query_above,
            "floor_tolerance": floor_tolerance,
            "min_floor_hit_rate": min_floor_hit_rate,
            "bounds_tolerance": bounds_tolerance,
            "category_min_ratio": category_min_ratio,
            "category_max_ratio": category_max_ratio,
            "exit_index_min_ratio": exit_index_min_ratio,
            "exit_index_max_ratio": exit_index_max_ratio,
            "polygon_flag_min_ratio": polygon_flag_min_ratio,
            "water_box_edge_tolerance": water_box_edge_tolerance,
            "water_box_min_overlap_ratio": water_box_min_overlap_ratio,
        },
        "reference": reference_summary,
        "converted": converted_summary,
        "bounds_delta": bounds_delta,
        "resource_bounds_delta": resource_bounds_delta,
        "geometry_bounds_delta": geometry_bounds_delta,
        "max_bounds_delta": round(max_bounds_delta, 3),
        "missing_reference_categories": missing_reference_categories,
        "polygon_category_ratio_summary": category_ratio_summary,
        "reference_semantics": reference_semantics,
        "converted_semantics": converted_semantics,
        "surface_semantics_present": surface_semantics_present,
        "reference_exit_index_usage": reference_exit_index_usage,
        "converted_exit_index_usage": converted_exit_index_usage,
        "surface_exit_index_usage_summary": surface_exit_index_usage_summary,
        "missing_reference_semantics": missing_reference_semantics,
        "insufficient_polygon_flags": insufficient_polygon_flags,
        "polygon_flag_coverage_summary": polygon_flag_coverage_summary,
        "metadata_match": metadata_match,
        "source_spawn_floor_probes": source_spawn_floor_probe_summary,
        "floor_probe_count": len(probes),
        "floor_hit_count": len(hit_results),
        "floor_match_count": len(matched_results),
        "floor_hit_rate": round(floor_hit_rate, 4),
        "floor_match_rate": round(floor_match_rate, 4),
        "max_abs_floor_delta": round(max_abs_floor_delta, 3) if max_abs_floor_delta is not None else None,
        "average_abs_floor_delta": round(average_abs_floor_delta, 3) if average_abs_floor_delta is not None else None,
        "floor_failure_count": len(floor_failures),
        "floor_failures_sample": floor_failures[:16],
    }


def visual_mesh_collision_floor_audit(
    models: list[tuple[int, CmbModel]],
    native_scene: CollisionScene,
    *,
    reference_scene: CollisionScene | None = None,
    floor_probe_count: int = DEFAULT_VISUAL_MESH_FLOOR_PROBE_COUNT,
    floor_query_above: float = DEFAULT_REFERENCE_FLOOR_QUERY_ABOVE,
    floor_tolerance: float = DEFAULT_REFERENCE_FLOOR_TOLERANCE,
    min_floor_match_rate: float = DEFAULT_REFERENCE_MIN_FLOOR_HIT_RATE,
    match_rate_margin: float = DEFAULT_VISUAL_MESH_DIAGNOSIS_MATCH_MARGIN,
) -> dict[str, object]:
    if floor_probe_count <= 0:
        raise ParseError("visual mesh floor probe count must be positive")

    room_indices = sorted({room_index for room_index, _model in models})
    try:
        builder = build_scene_collision(models)
    except ParseError as exc:
        return {
            "format": "oot3d_visual_mesh_floor_audit_v1",
            "status": "visual_proxy_build_error",
            "role": "diagnostic_only_not_collision_source",
            "room_indices": room_indices,
            "room_model_count": len(models),
            "error": str(exc),
            "notes": [
                "The diagnostic OOT3D visual floor proxy could not be built; native ZSI collision export is unchanged.",
            ],
        }
    if not builder.vertices or not builder.polygons:
        return {
            "format": "oot3d_visual_mesh_floor_audit_v1",
            "status": "no_visual_collision_proxy",
            "role": "diagnostic_only_not_collision_source",
            "room_indices": room_indices,
            "room_model_count": len(models),
            "skipped": dict(builder.skipped),
            "notes": [
                "No OOT3D visual mesh floor proxy was produced; native ZSI collision export is unchanged.",
            ],
        }

    visual_scene = CollisionScene(
        vertices=tuple(builder.vertices),
        polygons=tuple(builder.polygons),
        metadata=CollisionMetadata(),
        source="oot3d_visual_cmb_floor_proxy",
        source_format="oot3d_visual_cmb_floor_proxy",
    )
    probes = reference_floor_probes(visual_scene, floor_probe_count)
    native_results = floor_probe_result_summary(
        native_scene.vertices,
        tuple(enumerate(native_scene.polygons)),
        probes,
        floor_query_above,
    )
    native_metrics = floor_probe_result_metrics(native_results, floor_tolerance)

    reference_metrics: dict[str, object] | None = None
    reference_bounds_delta: dict[str, object] | None = None
    if reference_scene is not None:
        reference_results = floor_probe_result_summary(
            reference_scene.vertices,
            tuple(enumerate(reference_scene.polygons)),
            probes,
            floor_query_above,
        )
        reference_metrics = floor_probe_result_metrics(reference_results, floor_tolerance)
        reference_bounds_delta = collision_geometry_bounds_delta(visual_scene, reference_scene)

    return {
        "format": "oot3d_visual_mesh_floor_audit_v1",
        "status": "decoded_visual_mesh_floor_audit",
        "role": "diagnostic_only_not_collision_source",
        "decision_rule": (
            "Use this audit only to decide whether N64-reference deltas are real "
            "OOT3D visual-geometry differences or native ZSI decode issues."
        ),
        "room_indices": room_indices,
        "room_model_count": len(models),
        "visual_proxy": collision_scene_summary(visual_scene),
        "native_zsi": {
            **native_metrics,
            "bounds_delta_from_visual": collision_geometry_bounds_delta(visual_scene, native_scene),
        },
        "n64_reference": (
            {
                **reference_metrics,
                "bounds_delta_from_visual": reference_bounds_delta,
            }
            if reference_metrics is not None
            else None
        ),
        "diagnosis": visual_mesh_floor_diagnosis(
            native_metrics,
            reference_metrics,
            min_floor_match_rate=min_floor_match_rate,
            match_rate_margin=match_rate_margin,
        ),
        "probe_count": len(probes),
        "floor_probe_count_limit": floor_probe_count,
        "floor_query_above": floor_query_above,
        "floor_tolerance": floor_tolerance,
        "min_floor_match_rate": min_floor_match_rate,
        "match_rate_margin": match_rate_margin,
        "skipped": dict(builder.skipped),
        "notes": [
            "The proxy is built from filtered OOT3D visual CMB room meshes for audit only.",
            "It is not exported as collision and does not affect reference acceptance.",
        ],
    }


def visual_mesh_source_spawn_floor_audit(
    models: list[tuple[int, CmbModel]],
    source_spawn_floor_probes: tuple[tuple[float, float, float], ...],
    *,
    floor_query_above: float = DEFAULT_REFERENCE_FLOOR_QUERY_ABOVE,
    floor_tolerance: float = DEFAULT_REFERENCE_FLOOR_TOLERANCE,
) -> dict[str, object]:
    room_indices = sorted({room_index for room_index, _model in models})
    if not source_spawn_floor_probes:
        return {
            "format": "oot3d_visual_mesh_source_spawn_floor_audit_v1",
            "status": "no_source_spawn_floor_probes",
            "role": "diagnostic_only_not_collision_source",
            "room_indices": room_indices,
            "room_model_count": len(models),
            "probe_count": 0,
            "notes": [
                "No decoded OOT3D source player-spawn floor probes were available; native ZSI collision export is unchanged.",
            ],
        }

    try:
        builder = build_scene_collision(models)
    except ParseError as exc:
        return {
            "format": "oot3d_visual_mesh_source_spawn_floor_audit_v1",
            "status": "visual_proxy_build_error",
            "role": "diagnostic_only_not_collision_source",
            "room_indices": room_indices,
            "room_model_count": len(models),
            "probe_count": len(source_spawn_floor_probes),
            "error": str(exc),
            "notes": [
                "The diagnostic OOT3D visual floor proxy could not be built; native ZSI collision export is unchanged.",
            ],
        }
    if not builder.vertices or not builder.polygons:
        return {
            "format": "oot3d_visual_mesh_source_spawn_floor_audit_v1",
            "status": "no_visual_collision_proxy",
            "role": "diagnostic_only_not_collision_source",
            "room_indices": room_indices,
            "room_model_count": len(models),
            "probe_count": len(source_spawn_floor_probes),
            "skipped": dict(builder.skipped),
            "notes": [
                "No OOT3D visual mesh floor proxy was produced; native ZSI collision export is unchanged.",
            ],
        }

    visual_scene = CollisionScene(
        vertices=tuple(builder.vertices),
        polygons=tuple(builder.polygons),
        metadata=CollisionMetadata(),
        source="oot3d_visual_cmb_source_spawn_floor_proxy",
        source_format="oot3d_visual_cmb_source_spawn_floor_proxy",
    )
    visual_results = floor_probe_result_summary(
        visual_scene.vertices,
        tuple(enumerate(visual_scene.polygons)),
        source_spawn_floor_probes,
        floor_query_above,
    )
    visual_metrics = floor_probe_result_metrics(visual_results, floor_tolerance)

    return {
        "format": "oot3d_visual_mesh_source_spawn_floor_audit_v1",
        "status": "decoded_visual_mesh_source_spawn_floor_audit",
        "role": "diagnostic_only_not_collision_source",
        "decision_rule": (
            "Use this audit only to classify source-spawn floor mismatches. "
            "A visual hit does not accept the native ZSI collision; a visual "
            "miss suggests dynamic collision, setup differences, or real "
            "OOT3D scene differences need investigation."
        ),
        "room_indices": room_indices,
        "room_model_count": len(models),
        "visual_proxy": collision_scene_summary(visual_scene),
        "visual_mesh": visual_metrics,
        "probe_count": len(source_spawn_floor_probes),
        "floor_query_above": floor_query_above,
        "floor_tolerance": floor_tolerance,
        "skipped": dict(builder.skipped),
        "notes": [
            "The proxy is built from filtered OOT3D visual CMB room meshes for audit only.",
            "It is not exported as collision and does not affect reference acceptance.",
        ],
    }


def floor_probe_result_metrics(
    results: list[dict[str, object]],
    floor_tolerance: float,
) -> dict[str, object]:
    hits = [entry for entry in results if entry.get("hit")]
    matches = [
        entry
        for entry in hits
        if abs(float(entry.get("delta_y", 0.0))) <= floor_tolerance
    ]
    failures = [
        entry
        for entry in results
        if (not entry.get("hit")) or abs(float(entry.get("delta_y", 0.0))) > floor_tolerance
    ]
    return {
        "hit_count": len(hits),
        "match_count": len(matches),
        "hit_rate": round(len(hits) / len(results), 4) if results else 1.0,
        "match_rate": round(len(matches) / len(results), 4) if results else 1.0,
        "failure_count": len(failures),
        "max_abs_delta": round(
            max(abs(float(entry.get("delta_y", 0.0))) for entry in hits),
            3,
        )
        if hits
        else None,
        "average_abs_delta": round(
            sum(abs(float(entry.get("delta_y", 0.0))) for entry in hits) / len(hits),
            3,
        )
        if hits
        else None,
        "failures_sample": failures[:16],
        "results_sample": results[:16],
    }


def visual_mesh_floor_diagnosis(
    native_metrics: dict[str, object],
    reference_metrics: dict[str, object] | None,
    *,
    min_floor_match_rate: float,
    match_rate_margin: float = DEFAULT_VISUAL_MESH_DIAGNOSIS_MATCH_MARGIN,
) -> str:
    native_rate = float(native_metrics["match_rate"])
    native_match = native_rate >= min_floor_match_rate
    if reference_metrics is None:
        return "native_zsi_matches_oot3d_visual_mesh" if native_match else "native_zsi_may_not_match_oot3d_visual_mesh"

    reference_rate = float(reference_metrics["match_rate"])
    reference_match = reference_rate >= min_floor_match_rate
    if native_match and reference_match:
        return "oot3d_visual_mesh_matches_native_zsi_and_n64_reference"
    if native_match and not reference_match:
        return "oot3d_visual_geometry_diverges_from_n64_reference"
    if not native_match and reference_match:
        return "native_zsi_collision_may_not_match_oot3d_visual_mesh"
    if native_rate >= reference_rate + match_rate_margin:
        return "oot3d_visual_geometry_probably_diverges_from_n64_reference"
    if reference_rate >= native_rate + match_rate_margin:
        return "native_zsi_collision_probably_mismatches_oot3d_visual_mesh"
    return "visual_mesh_proxy_or_collision_decode_needs_review"


def collision_metadata_match(
    reference: CollisionMetadata,
    converted: CollisionMetadata,
    *,
    water_box_edge_tolerance: int = DEFAULT_REFERENCE_WATER_BOX_EDGE_TOLERANCE,
    water_box_min_overlap_ratio: float = DEFAULT_REFERENCE_WATER_BOX_MIN_OVERLAP_RATIO,
) -> dict[str, object]:
    return {
        "camera_data": sequence_match_summary(
            reference.camera_data,
            converted.camera_data,
            camera_data_summary,
        ),
        "camera_positions": sequence_match_summary(
            reference.camera_positions,
            converted.camera_positions,
            camera_position_summary,
        ),
        "water_boxes": water_box_match_summary(
            reference.water_boxes,
            converted.water_boxes,
            edge_tolerance=water_box_edge_tolerance,
            min_overlap_ratio=water_box_min_overlap_ratio,
        ),
    }


def sequence_match_summary(
    reference: tuple[T, ...],
    converted: tuple[T, ...],
    item_summary,
) -> dict[str, object]:
    reference_uses_data = len(reference) > 0
    exact_match = (not reference_uses_data) or reference == converted
    first_mismatch_index: int | None = None
    for index, (reference_item, converted_item) in enumerate(zip(reference, converted)):
        if reference_item != converted_item:
            first_mismatch_index = index
            break
    if first_mismatch_index is None and len(reference) != len(converted):
        first_mismatch_index = min(len(reference), len(converted))

    return {
        "accepted": exact_match,
        "reference_count": len(reference),
        "converted_count": len(converted),
        "exact_match": reference == converted,
        "first_mismatch_index": first_mismatch_index,
        "reference_sample": [item_summary(item) for item in reference[:4]],
        "converted_sample": [item_summary(item) for item in converted[:4]],
    }


def camera_data_summary(entry: CameraData) -> dict[str, int]:
    return {
        "stype": entry.stype,
        "num_data": entry.num_data,
        "camera_pos_data_index": entry.camera_pos_data_index,
    }


def camera_position_summary(entry: CameraPositionData) -> dict[str, tuple[int, int, int]]:
    return {
        "pos": entry.pos,
        "rot": entry.rot,
        "other": entry.other,
    }


def water_box_summary(entry: WaterBox) -> dict[str, int]:
    return {
        "x_min": entry.x_min,
        "y_surface": entry.y_surface,
        "z_min": entry.z_min,
        "x_length": entry.x_length,
        "z_length": entry.z_length,
        "properties": entry.properties,
    }


def water_box_match_summary(
    reference: tuple[WaterBox, ...],
    converted: tuple[WaterBox, ...],
    *,
    edge_tolerance: int,
    min_overlap_ratio: float,
) -> dict[str, object]:
    if edge_tolerance < 0:
        raise ParseError("water box edge tolerance must be non-negative")
    if min_overlap_ratio < 0 or min_overlap_ratio > 1:
        raise ParseError("water box min overlap ratio must be in range 0..1")

    exact_match = reference == converted
    first_mismatch_index: int | None = None
    for index, (reference_box, converted_box) in enumerate(zip(reference, converted)):
        if reference_box != converted_box and first_mismatch_index is None:
            first_mismatch_index = index
    if first_mismatch_index is None and len(reference) != len(converted):
        first_mismatch_index = min(len(reference), len(converted))

    records: list[dict[str, object]] = []
    failed_records: list[dict[str, object]] = []
    unused_converted_indices = set(range(len(converted)))
    for reference_index, reference_box in enumerate(reference):
        candidate_records: list[dict[str, object]] = []
        for converted_index in sorted(unused_converted_indices):
            record = water_box_pair_match_record(
                reference_index,
                reference_box,
                converted[converted_index],
                edge_tolerance=edge_tolerance,
                min_overlap_ratio=min_overlap_ratio,
            )
            record["reference_index"] = reference_index
            record["converted_index"] = converted_index
            candidate_records.append(record)

        accepted_records = [record for record in candidate_records if record["accepted"]]
        if accepted_records:
            selected_record = min(
                accepted_records,
                key=lambda record: (
                    record["status"] != "exact",
                    int(record["max_abs_edge_delta"]),
                    -float(record["overlap_ratio"]),
                    int(record["converted_index"]),
                ),
            )
            unused_converted_indices.remove(int(selected_record["converted_index"]))
            records.append(selected_record)
            continue

        if candidate_records:
            selected_record = min(
                candidate_records,
                key=lambda record: (
                    not bool(record["same_y_surface"]),
                    not bool(record["same_properties"]),
                    int(record["max_abs_edge_delta"]),
                    -float(record["overlap_ratio"]),
                    int(record["converted_index"]),
                ),
            )
        else:
            selected_record = {
                "index": reference_index,
                "reference_index": reference_index,
                "converted_index": None,
                "accepted": False,
                "status": "missing_converted",
                "same_y_surface": False,
                "same_properties": False,
                "edge_deltas": None,
                "max_abs_edge_delta": None,
                "overlap_ratio": 0.0,
                "reference": water_box_summary(reference_box),
                "converted": None,
            }
        records.append(selected_record)
        failed_records.append(selected_record)

    count_match = len(reference) == len(converted)
    extra_converted_indices = sorted(unused_converted_indices)
    return {
        "accepted": not failed_records,
        "reference_count": len(reference),
        "converted_count": len(converted),
        "exact_match": exact_match,
        "coverage_match": not failed_records,
        "count_match": count_match,
        "extra_converted_count": len(extra_converted_indices),
        "extra_converted_records": [
            {
                "converted_index": index,
                "converted": water_box_summary(converted[index]),
            }
            for index in extra_converted_indices
        ],
        "first_mismatch_index": first_mismatch_index,
        "match_policy": "reference_coverage_allows_native_oot3d_extras",
        "edge_tolerance": edge_tolerance,
        "min_overlap_ratio": min_overlap_ratio,
        "records": records,
        "failed_records": failed_records,
        "reference_sample": [water_box_summary(item) for item in reference[:4]],
        "converted_sample": [water_box_summary(item) for item in converted[:4]],
    }


def water_box_pair_match_record(
    index: int,
    reference: WaterBox,
    converted: WaterBox,
    *,
    edge_tolerance: int,
    min_overlap_ratio: float,
) -> dict[str, object]:
    if reference == converted:
        return {
            "index": index,
            "accepted": True,
            "status": "exact",
            "same_y_surface": True,
            "same_properties": True,
            "edge_deltas": (0, 0, 0, 0),
            "max_abs_edge_delta": 0,
            "overlap_ratio": 1.0,
            "reference": water_box_summary(reference),
            "converted": water_box_summary(converted),
        }
    same_level_and_properties = (
        reference.y_surface == converted.y_surface
        and reference.properties == converted.properties
    )
    reference_rect = water_box_rect(reference)
    converted_rect = water_box_rect(converted)
    edge_deltas = tuple(
        converted_rect[i] - reference_rect[i]
        for i in range(4)
    )
    max_abs_edge_delta = max((abs(value) for value in edge_deltas), default=0)
    overlap_ratio = water_box_overlap_ratio(reference, converted)
    accepted = (
        same_level_and_properties
        and max_abs_edge_delta <= edge_tolerance
        and overlap_ratio >= min_overlap_ratio
    )
    return {
        "index": index,
        "accepted": accepted,
        "status": "accepted" if accepted else "mismatch",
        "same_y_surface": reference.y_surface == converted.y_surface,
        "same_properties": reference.properties == converted.properties,
        "edge_deltas": edge_deltas,
        "max_abs_edge_delta": max_abs_edge_delta,
        "overlap_ratio": round(overlap_ratio, 4),
        "reference": water_box_summary(reference),
        "converted": water_box_summary(converted),
    }


def water_box_rect(water_box: WaterBox) -> tuple[int, int, int, int]:
    return (
        water_box.x_min,
        water_box.z_min,
        water_box.x_min + water_box.x_length,
        water_box.z_min + water_box.z_length,
    )


def water_box_overlap_ratio(reference: WaterBox, converted: WaterBox) -> float:
    ref_x0, ref_z0, ref_x1, ref_z1 = water_box_rect(reference)
    con_x0, con_z0, con_x1, con_z1 = water_box_rect(converted)
    inter_width = max(0, min(ref_x1, con_x1) - max(ref_x0, con_x0))
    inter_depth = max(0, min(ref_z1, con_z1) - max(ref_z0, con_z0))
    intersection = inter_width * inter_depth
    reference_area = max(0, ref_x1 - ref_x0) * max(0, ref_z1 - ref_z0)
    converted_area = max(0, con_x1 - con_x0) * max(0, con_z1 - con_z0)
    if reference_area <= 0 or converted_area <= 0:
        return 0.0
    return intersection / max(reference_area, converted_area)


def reference_floor_probes(
    reference: CollisionScene,
    max_count: int,
) -> tuple[tuple[float, float, float], ...]:
    if max_count <= 0:
        raise ParseError("reference floor probe count must be positive")
    floor_entries = [
        (index, polygon)
        for index, polygon in enumerate(reference.polygons)
        if collision_polygon_category(polygon) == "floor"
    ]
    selected = select_evenly(floor_entries, max_count)
    probes: list[tuple[float, float, float]] = []
    for _index, polygon in selected:
        triangle = collision_polygon_vertices(reference.vertices, polygon)
        if triangle is None:
            continue
        x = sum(vertex[0] for vertex in triangle) / 3.0
        z = sum(vertex[2] for vertex in triangle) / 3.0
        y = collision_polygon_floor_y_at(reference.vertices, polygon, x, z)
        if y is None:
            continue
        probes.append((x, y, z))
    return tuple(probes)


def select_evenly(items: list[T], max_count: int) -> list[T]:
    if len(items) <= max_count:
        return list(items)
    if max_count == 1:
        return [items[0]]
    selected_indices: list[int] = []
    for index in range(max_count):
        selected_index = round(index * (len(items) - 1) / (max_count - 1))
        if selected_indices and selected_index <= selected_indices[-1]:
            selected_index = selected_indices[-1] + 1
        if selected_index >= len(items):
            selected_index = len(items) - 1
        selected_indices.append(selected_index)
    return [items[index] for index in selected_indices]


def collision_scene_summary(scene: CollisionScene) -> dict[str, object]:
    category_counts = Counter(collision_polygon_category(polygon) for polygon in scene.polygons)
    surface_usage = Counter(polygon.type for polygon in scene.polygons)
    max_polygon_type = max(surface_usage, default=-1)
    vertex_bounds_min, vertex_bounds_max = collision_vertex_bounds(scene.vertices)
    return {
        "source": scene.source,
        "source_format": scene.source_format,
        "bounds_min": scene.effective_bounds_min,
        "bounds_max": scene.effective_bounds_max,
        "vertex_bounds_min": vertex_bounds_min,
        "vertex_bounds_max": vertex_bounds_max,
        "vertex_count": len(scene.vertices),
        "polygon_count": len(scene.polygons),
        "polygon_category_counts": dict(sorted(category_counts.items())),
        "surface_type_count": len(scene.metadata.surface_types),
        "used_surface_type_count": sum(1 for count in surface_usage.values() if count > 0),
        "max_polygon_type": max_polygon_type,
        "camera_data_count": len(scene.metadata.camera_data),
        "camera_position_count": len(scene.metadata.camera_positions),
        "water_box_count": len(scene.metadata.water_boxes),
    }


def collision_category_ratio_summary(
    reference_counts: dict[str, int],
    converted_counts: dict[str, int],
    *,
    min_ratio: float,
    max_ratio: float,
) -> dict[str, object]:
    if min_ratio < 0:
        raise ParseError("collision category min ratio must be non-negative")
    if max_ratio < min_ratio:
        raise ParseError("collision category max ratio must be greater than or equal to min ratio")

    categories = ("floor", "wall", "ceiling")
    total_reference_count = sum(int(reference_counts.get(category, 0)) for category in categories)
    total_converted_count = sum(int(converted_counts.get(category, 0)) for category in categories)
    total_ratio = (
        total_converted_count / total_reference_count
        if total_reference_count > 0
        else None
    )
    records: list[dict[str, object]] = []
    failed: list[dict[str, object]] = []
    for category in categories:
        reference_count = int(reference_counts.get(category, 0))
        converted_count = int(converted_counts.get(category, 0))
        if reference_count <= 0:
            record = {
                "category": category,
                "reference_count": reference_count,
                "converted_count": converted_count,
                "ratio": None,
                "status": "not_required_by_reference",
            }
            records.append(record)
            continue
        ratio = converted_count / reference_count
        below_threshold = ratio < min_ratio
        over_category_threshold = ratio > max_ratio
        accepted_total_bounded_overage = (
            over_category_threshold
            and total_ratio is not None
            and total_ratio <= max_ratio
        )
        accepted = (not below_threshold) and (
            ratio <= max_ratio or accepted_total_bounded_overage
        )
        record = {
            "category": category,
            "reference_count": reference_count,
            "converted_count": converted_count,
            "ratio": round(ratio, 4),
            "total_reference_count": total_reference_count,
            "total_converted_count": total_converted_count,
            "total_ratio": round(total_ratio, 4) if total_ratio is not None else None,
            "min_ratio": min_ratio,
            "max_ratio": max_ratio,
            "status": (
                "accepted_total_ratio_bounded_overage"
                if accepted_total_bounded_overage
                else "accepted" if accepted else "outside_threshold"
            ),
        }
        records.append(record)
        if not accepted:
            failed.append(record)
    return {
        "accepted": not failed,
        "min_ratio": min_ratio,
        "max_ratio": max_ratio,
        "total_reference_count": total_reference_count,
        "total_converted_count": total_converted_count,
        "total_ratio": round(total_ratio, 4) if total_ratio is not None else None,
        "records": records,
        "failed_records": failed,
    }


def collision_surface_field_usage_match_summary(
    reference_usage: dict[int, int],
    converted_usage: dict[int, int],
    *,
    min_ratio: float,
    max_ratio: float,
) -> dict[str, object]:
    if min_ratio < 0:
        raise ParseError("surface field usage min ratio must be non-negative")
    if max_ratio < min_ratio:
        raise ParseError("surface field usage max ratio must be greater than or equal to min ratio")

    reference_keys = set(reference_usage)
    converted_keys = set(converted_usage)
    missing_values = sorted(reference_keys - converted_keys)
    extra_values = sorted(converted_keys - reference_keys)
    records: list[dict[str, object]] = []
    failed_records: list[dict[str, object]] = []
    for value in sorted(reference_keys & converted_keys):
        reference_count = int(reference_usage[value])
        converted_count = int(converted_usage[value])
        ratio = converted_count / reference_count if reference_count else None
        accepted = ratio is None or min_ratio <= ratio <= max_ratio
        record = {
            "value": value,
            "reference_count": reference_count,
            "converted_count": converted_count,
            "ratio": round(ratio, 4) if ratio is not None else None,
            "min_ratio": min_ratio,
            "max_ratio": max_ratio,
            "status": "accepted" if accepted else "outside_threshold",
        }
        records.append(record)
        if not accepted:
            failed_records.append(record)
    return {
        "accepted": not missing_values and not extra_values and not failed_records,
        "same_values": not missing_values and not extra_values,
        "missing_values": missing_values,
        "extra_values": extra_values,
        "min_ratio": min_ratio,
        "max_ratio": max_ratio,
        "records": records,
        "failed_records": failed_records,
    }


def collision_polygon_flag_coverage_summary(
    reference_semantics: dict[str, int],
    converted_semantics: dict[str, int],
    *,
    min_ratio: float,
) -> dict[str, object]:
    if min_ratio < 0:
        raise ParseError("polygon flag min ratio must be non-negative")
    records: list[dict[str, object]] = []
    failed_records: list[dict[str, object]] = []
    for name in POLYGON_FLAG_SEMANTICS:
        reference_count = int(reference_semantics.get(name, 0))
        converted_count = int(converted_semantics.get(name, 0))
        if reference_count <= 0:
            record = {
                "name": name,
                "reference_count": reference_count,
                "converted_count": converted_count,
                "ratio": None,
                "status": "not_required_by_reference",
            }
            records.append(record)
            continue
        ratio = converted_count / reference_count
        accepted = converted_count > 0 and ratio >= min_ratio
        record = {
            "name": name,
            "reference_count": reference_count,
            "converted_count": converted_count,
            "ratio": round(ratio, 4),
            "min_ratio": min_ratio,
            "status": "accepted" if accepted else "below_threshold",
        }
        records.append(record)
        if not accepted:
            failed_records.append(record)
    return {
        "accepted": not failed_records,
        "min_ratio": min_ratio,
        "records": records,
        "failed_records": failed_records,
    }


def collision_semantic_presence(scene: CollisionScene) -> dict[str, int]:
    counts: Counter[str] = Counter()
    for polygon in scene.polygons:
        if polygon.type < len(scene.metadata.surface_types):
            decoded = decode_surface_type(scene.metadata.surface_types[polygon.type])
            for name, value in decoded.items():
                if bool(value):
                    counts[f"surface.{name}"] += 1
        flags = decode_collision_polygon_flags(polygon.vertex_a, polygon.vertex_b)
        for name in ("ignore_camera", "ignore_entities", "ignore_projectiles", "conveyor"):
            if flags[name]:
                counts[f"polygon.{name}"] += 1
    return dict(sorted(counts.items()))


def collision_surface_field_usage(scene: CollisionScene, field_name: str) -> dict[int, int]:
    surface_usage = Counter(polygon.type for polygon in scene.polygons)
    field_usage: Counter[int] = Counter()
    for surface_index, polygon_count in surface_usage.items():
        if surface_index >= len(scene.metadata.surface_types):
            continue
        decoded = decode_surface_type(scene.metadata.surface_types[surface_index])
        value = decoded.get(field_name)
        if isinstance(value, bool) or value is None:
            continue
        int_value = int(value)
        if int_value == 0:
            continue
        field_usage[int_value] += polygon_count
    return dict(sorted(field_usage.items()))


def collision_bounds_delta(reference: CollisionScene, converted: CollisionScene) -> dict[str, object]:
    reference_values = reference.effective_bounds_min + reference.effective_bounds_max
    converted_values = converted.effective_bounds_min + converted.effective_bounds_max
    delta = tuple(converted_values[index] - reference_values[index] for index in range(6))
    return {
        "axis_order": ("min_x", "min_y", "min_z", "max_x", "max_y", "max_z"),
        "bounds_source": "resource_header_bounds",
        "reference": reference_values,
        "converted": converted_values,
        "signed": delta,
        "absolute": tuple(abs(value) for value in delta),
    }


def collision_geometry_bounds_delta(reference: CollisionScene, converted: CollisionScene) -> dict[str, object]:
    reference_min, reference_max = collision_vertex_bounds(reference.vertices)
    converted_min, converted_max = collision_vertex_bounds(converted.vertices)
    reference_values = reference_min + reference_max
    converted_values = converted_min + converted_max
    delta = tuple(converted_values[index] - reference_values[index] for index in range(6))
    return {
        "axis_order": ("min_x", "min_y", "min_z", "max_x", "max_y", "max_z"),
        "bounds_source": "collision_vertex_bounds",
        "reference": reference_values,
        "converted": converted_values,
        "signed": delta,
        "absolute": tuple(abs(value) for value in delta),
    }


def collision_vertex_bounds(vertices: tuple[tuple[int, int, int], ...]) -> tuple[tuple[int, int, int], tuple[int, int, int]]:
    if not vertices:
        return (0, 0, 0), (0, 0, 0)
    xs = [vertex[0] for vertex in vertices]
    ys = [vertex[1] for vertex in vertices]
    zs = [vertex[2] for vertex in vertices]
    return (min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs))


def collision_polygon_vertices(
    vertices: tuple[tuple[int, int, int], ...],
    polygon: CollisionPolygon,
) -> tuple[tuple[int, int, int], tuple[int, int, int], tuple[int, int, int]] | None:
    try:
        return (
            vertices[polygon.vertex_a & 0x1FFF],
            vertices[polygon.vertex_b & 0x1FFF],
            vertices[polygon.vertex_c & 0x1FFF],
        )
    except IndexError:
        return None


def raycast_floor_probe(
    vertices: tuple[tuple[int, int, int], ...],
    polygon_entries: tuple[tuple[int, CollisionPolygon], ...],
    x: float,
    query_y: float,
    z: float,
) -> tuple[int, float, CollisionPolygon] | None:
    best: tuple[int, float, CollisionPolygon] | None = None
    for polygon_index, polygon in polygon_entries:
        floor_y = collision_polygon_floor_y_at(vertices, polygon, x, z)
        if floor_y is None or floor_y > query_y:
            continue
        if best is None or floor_y > best[1]:
            best = (polygon_index, floor_y, polygon)
    return best


def collision_polygon_floor_y_at(
    vertices: tuple[tuple[int, int, int], ...],
    polygon: CollisionPolygon,
    x: float,
    z: float,
) -> float | None:
    if polygon.normal_y <= COLPOLY_FLOOR_NORMAL_Y:
        return None
    triangle = collision_polygon_vertices(vertices, polygon)
    if triangle is None:
        return None
    if not point_in_triangle_xz(x, z, triangle):
        return None

    normal_y = polygon.normal_y / 32767.0
    if abs(normal_y) < 1e-6:
        return None
    floor_y = -(
        (polygon.normal_x / 32767.0) * x
        + (polygon.normal_z / 32767.0) * z
        + polygon.dist
    ) / normal_y
    if not math.isfinite(floor_y):
        return None
    return floor_y


def point_in_triangle_xz(
    x: float,
    z: float,
    triangle: tuple[tuple[int, int, int], tuple[int, int, int], tuple[int, int, int]],
) -> bool:
    ax, az = float(triangle[0][0]), float(triangle[0][2])
    bx, bz = float(triangle[1][0]), float(triangle[1][2])
    cx, cz = float(triangle[2][0]), float(triangle[2][2])
    denominator = ((bz - cz) * (ax - cx)) + ((cx - bx) * (az - cz))
    if abs(denominator) < 1e-6:
        return False
    alpha = (((bz - cz) * (x - cx)) + ((cx - bx) * (z - cz))) / denominator
    beta = (((cz - az) * (x - cx)) + ((ax - cx) * (z - cz))) / denominator
    gamma = 1.0 - alpha - beta
    epsilon = 1e-4
    return alpha >= -epsilon and beta >= -epsilon and gamma >= -epsilon


def estimate_static_lookup(
    vertices: tuple[tuple[int, int, int], ...],
    polygons: tuple[CollisionPolygon, ...],
    *,
    node_budget: int | None = None,
) -> StaticLookupEstimate:
    if not vertices or not polygons:
        return StaticLookupEstimate(0, node_budget, ())

    min_bounds, subdiv_length, subdiv_length_inv = static_lookup_dimensions(vertices)
    estimates: list[StaticLookupPolygonEstimate] = []
    node_count = 0
    for index, polygon in enumerate(polygons):
        triangle = (
            vertices[polygon.vertex_a & 0x1FFF],
            vertices[polygon.vertex_b & 0x1FFF],
            vertices[polygon.vertex_c & 0x1FFF],
        )
        polygon_node_count = estimate_polygon_static_lookup_nodes(
            triangle,
            min_bounds,
            subdiv_length,
            subdiv_length_inv,
        )
        node_count += polygon_node_count
        estimates.append(
            StaticLookupPolygonEstimate(
                index=index,
                category=collision_polygon_category(polygon),
                node_count=polygon_node_count,
            )
        )

    return StaticLookupEstimate(node_count, node_budget, tuple(estimates))


def static_lookup_dimensions(
    vertices: tuple[tuple[int, int, int], ...],
) -> tuple[tuple[float, float, float], tuple[float, float, float], tuple[float, float, float]]:
    min_bounds = (
        float(min(vertex[0] for vertex in vertices)),
        float(min(vertex[1] for vertex in vertices)),
        float(min(vertex[2] for vertex in vertices)),
    )
    max_bounds = (
        float(max(vertex[0] for vertex in vertices)),
        float(max(vertex[1] for vertex in vertices)),
        float(max(vertex[2] for vertex in vertices)),
    )
    lengths: list[float] = []
    inverses: list[float] = []
    for axis, subdiv_amount in enumerate(STATIC_LOOKUP_SUBDIV_AMOUNT):
        length = float(int((max_bounds[axis] - min_bounds[axis]) / subdiv_amount) + 1)
        length = max(length, STATIC_LOOKUP_SUBDIV_MIN)
        lengths.append(length)
        inverses.append(1.0 / length)
    return min_bounds, tuple(lengths), tuple(inverses)


def estimate_polygon_static_lookup_nodes(
    triangle: tuple[tuple[int, int, int], tuple[int, int, int], tuple[int, int, int]],
    min_bounds: tuple[float, float, float],
    subdiv_length: tuple[float, float, float],
    subdiv_length_inv: tuple[float, float, float],
) -> int:
    tri_min = tuple(float(min(vertex[axis] for vertex in triangle)) for axis in range(3))
    tri_max = tuple(float(max(vertex[axis] for vertex in triangle)) for axis in range(3))
    subdiv_min = static_lookup_subdiv_min(tri_min, min_bounds, subdiv_length, subdiv_length_inv)
    subdiv_max = static_lookup_subdiv_max(tri_max, min_bounds, subdiv_length, subdiv_length_inv)

    count = 0
    for sz in range(subdiv_min[2], subdiv_max[2] + 1):
        for sy in range(subdiv_min[1], subdiv_max[1] + 1):
            for sx in range(subdiv_min[0], subdiv_max[0] + 1):
                box_min = (
                    (subdiv_length[0] * sx + min_bounds[0]) - STATIC_LOOKUP_SUBDIV_OVERLAP,
                    (subdiv_length[1] * sy + min_bounds[1]) - STATIC_LOOKUP_SUBDIV_OVERLAP,
                    (subdiv_length[2] * sz + min_bounds[2]) - STATIC_LOOKUP_SUBDIV_OVERLAP,
                )
                box_max = (
                    box_min[0] + subdiv_length[0] + (2 * STATIC_LOOKUP_SUBDIV_OVERLAP),
                    box_min[1] + subdiv_length[1] + (2 * STATIC_LOOKUP_SUBDIV_OVERLAP),
                    box_min[2] + subdiv_length[2] + (2 * STATIC_LOOKUP_SUBDIV_OVERLAP),
                )
                if triangle_intersects_box(triangle, box_min, box_max):
                    count += 1
    return count


def static_lookup_subdiv_min(
    pos: tuple[float, float, float],
    min_bounds: tuple[float, float, float],
    subdiv_length: tuple[float, float, float],
    subdiv_length_inv: tuple[float, float, float],
) -> tuple[int, int, int]:
    result: list[int] = []
    for axis in range(3):
        delta = pos[axis] - min_bounds[axis]
        sector = int(delta * subdiv_length_inv[axis])
        if int(delta) % int(subdiv_length[axis]) < STATIC_LOOKUP_SUBDIV_OVERLAP and sector > 0:
            sector -= 1
        result.append(max(0, min(STATIC_LOOKUP_SUBDIV_AMOUNT[axis] - 1, sector)))
    return tuple(result)  # type: ignore[return-value]


def static_lookup_subdiv_max(
    pos: tuple[float, float, float],
    min_bounds: tuple[float, float, float],
    subdiv_length: tuple[float, float, float],
    subdiv_length_inv: tuple[float, float, float],
) -> tuple[int, int, int]:
    result: list[int] = []
    for axis in range(3):
        delta = pos[axis] - min_bounds[axis]
        sector = int(delta * subdiv_length_inv[axis])
        if (
            int(subdiv_length[axis]) - STATIC_LOOKUP_SUBDIV_OVERLAP
            < int(delta) % int(subdiv_length[axis])
            and sector < STATIC_LOOKUP_SUBDIV_AMOUNT[axis] - 1
        ):
            sector += 1
        result.append(max(0, min(STATIC_LOOKUP_SUBDIV_AMOUNT[axis] - 1, sector)))
    return tuple(result)  # type: ignore[return-value]


def triangle_intersects_box(
    triangle: tuple[tuple[int, int, int], tuple[int, int, int], tuple[int, int, int]],
    box_min: tuple[float, float, float],
    box_max: tuple[float, float, float],
) -> bool:
    center = tuple((box_min[axis] + box_max[axis]) * 0.5 for axis in range(3))
    half = tuple((box_max[axis] - box_min[axis]) * 0.5 for axis in range(3))
    tri = tuple(
        (
            float(vertex[0]) - center[0],
            float(vertex[1]) - center[1],
            float(vertex[2]) - center[2],
        )
        for vertex in triangle
    )
    edges = (
        vector_sub(tri[1], tri[0]),
        vector_sub(tri[2], tri[1]),
        vector_sub(tri[0], tri[2]),
    )
    axes = [
        (1.0, 0.0, 0.0),
        (0.0, 1.0, 0.0),
        (0.0, 0.0, 1.0),
        vector_cross(edges[0], edges[1]),
    ]
    for edge in edges:
        axes.extend(
            (
                vector_cross(edge, (1.0, 0.0, 0.0)),
                vector_cross(edge, (0.0, 1.0, 0.0)),
                vector_cross(edge, (0.0, 0.0, 1.0)),
            )
        )

    for axis in axes:
        if abs(axis[0]) + abs(axis[1]) + abs(axis[2]) < 1e-6:
            continue
        projections = [vector_dot(vertex, axis) for vertex in tri]
        radius = (
            half[0] * abs(axis[0])
            + half[1] * abs(axis[1])
            + half[2] * abs(axis[2])
        )
        if max(projections) < -radius or min(projections) > radius:
            return False
    return True


def vector_sub(
    a: tuple[float, float, float],
    b: tuple[float, float, float],
) -> tuple[float, float, float]:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def vector_dot(
    a: tuple[float, float, float],
    b: tuple[float, float, float],
) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def vector_cross(
    a: tuple[float, float, float],
    b: tuple[float, float, float],
) -> tuple[float, float, float]:
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def collision_polygon_category(polygon: CollisionPolygon) -> str:
    if polygon.normal_y > COLPOLY_FLOOR_NORMAL_Y:
        return "floor"
    if polygon.normal_y < COLPOLY_CEILING_NORMAL_Y:
        return "ceiling"
    return "wall"


def collision_bounds(
    vertices: tuple[tuple[int, int, int], ...],
) -> tuple[tuple[int, int, int], tuple[int, int, int]]:
    if not vertices:
        raise ParseError("collision bounds require at least one vertex")
    return (
        (
            min(vertex[0] for vertex in vertices),
            min(vertex[1] for vertex in vertices),
            min(vertex[2] for vertex in vertices),
        ),
        (
            max(vertex[0] for vertex in vertices),
            max(vertex[1] for vertex in vertices),
            max(vertex[2] for vertex in vertices),
        ),
    )


def collision_resource_bounds(
    vertices: tuple[tuple[int, int, int], ...],
    *,
    bounds_min: tuple[int, int, int] | None = None,
    bounds_max: tuple[int, int, int] | None = None,
) -> tuple[tuple[int, int, int], tuple[int, int, int]]:
    if bounds_min is None and bounds_max is None:
        return collision_bounds(vertices)
    if bounds_min is None or bounds_max is None:
        raise ParseError("collision resource bounds require both min and max values")
    if len(bounds_min) != 3 or len(bounds_max) != 3:
        raise ParseError("collision resource bounds must be 3D coordinate tuples")
    return bounds_min, bounds_max


def collision_axis_audit(
    candidate: ZsiCollisionHeaderCandidate,
    exported_vertices: tuple[tuple[int, int, int], ...],
) -> dict[str, object]:
    decoded_vertices = tuple((vertex.x, vertex.y, vertex.z) for vertex in candidate.vertices)
    decoded_bounds = collision_bounds(decoded_vertices)
    exported_bounds = collision_bounds(exported_vertices)
    header_bounds = (candidate.bounds_min, candidate.bounds_max)
    header_values = candidate.bounds_min + candidate.bounds_max
    exported_values = exported_bounds[0] + exported_bounds[1]
    signed_delta = tuple(
        exported_values[index] - header_values[index]
        for index in range(6)
    )
    return {
        "coordinate_transform": "effective OOT3D collision vertex (x, y, z) -> Shipwright/N64 (x, y, z)",
        "runtime_header_offset": candidate.offset,
        "command_argument": candidate.command_argument,
        "command_argument_prefix_size": candidate.offset - candidate.command_argument,
        "effective_vertex_offset": candidate.effective_vertex_offset,
        "raw_vertex_offset": candidate.vertex_offset,
        "decoded_effective_vertex_bounds": decoded_bounds,
        "decoded_header_bounds": header_bounds,
        "exported_vertex_bounds": exported_bounds,
        "exported_resource_bounds": header_bounds,
        "header_vs_exported_vertex_bounds": {
            "axis_order": ("min_x", "min_y", "min_z", "max_x", "max_y", "max_z"),
            "signed": signed_delta,
            "absolute": tuple(abs(value) for value in signed_delta),
            "max_abs_delta": max((abs(value) for value in signed_delta), default=0),
        },
        "section_prefix_sizes": {
            "command_argument": candidate.offset - candidate.command_argument,
            "vertices": candidate.effective_vertex_offset - candidate.vertex_offset,
            "polygons": candidate.effective_polygon_offset - candidate.polygon_offset,
            "surface_types": candidate.effective_surface_type_offset - candidate.surface_type_offset,
            "camera_data": candidate.effective_bgcam_offset - candidate.bgcam_offset,
            "water_boxes": 0x10 if candidate.effective_water_boxes else 0,
        },
        "exported_bounds_source": "decoded native OOT3D collision header bounds",
        "notes": [
            "The decoded header bounds are used to select the effective vertex table after any section prefix.",
            "Shipwright resource bounds preserve the decoded native collision header bounds.",
        ],
    }


def normalize_zsi_collision_candidate(
    candidate: ZsiCollisionHeaderCandidate,
    *,
    source: str = "",
) -> CollisionScene:
    vertices = tuple(
        zsi_collision_vertex_to_shipwright(vertex)
        for vertex in candidate.vertices
    )
    polygons: list[CollisionPolygon] = []
    for polygon in candidate.polygons:
        try:
            triangle = tuple(vertices[index] for index in polygon.vertex_indices)
        except IndexError:
            continue
        if collision_plane(triangle) is None:
            continue
        plane = zsi_collision_plane_to_shipwright(polygon)
        if plane is None:
            continue
        polygons.append(
            CollisionPolygon(
                polygon.type,
                polygon.raw_vtx_a,
                polygon.raw_vtx_b,
                polygon.raw_vtx_c,
                plane[0],
                plane[1],
                plane[2],
                plane[3],
            )
        )
    surface_types = tuple(
        SurfaceType(surface_type.data1, surface_type.data2)
        for surface_type in candidate.effective_surface_types
    )
    camera_data = tuple(
        CameraData(
            entry.setting,
            entry.count,
            candidate.camera_position_indices[index],
        )
        for index, entry in enumerate(candidate.effective_bg_cam_info)
    )
    camera_positions = camera_position_groups(candidate.camera_position_vectors)
    water_boxes = tuple(
        zsi_water_box_to_shipwright(water_box)
        for water_box in selected_zsi_water_boxes(candidate)
    )
    return CollisionScene(
        vertices=vertices,
        polygons=tuple(polygons),
        metadata=CollisionMetadata(
            camera_data=camera_data,
            camera_positions=camera_positions,
            water_boxes=water_boxes,
            surface_types=surface_types,
        ),
        source=source,
        source_format="oot3d_zsi_native_collision",
        bounds_min=candidate.bounds_min,
        bounds_max=candidate.bounds_max,
    )


def zsi_collision_candidate_to_shipwright(
    candidate: ZsiCollisionHeaderCandidate,
) -> tuple[tuple[tuple[int, int, int], ...], tuple[CollisionPolygon, ...], CollisionMetadata]:
    scene = normalize_zsi_collision_candidate(candidate)
    return scene.vertices, scene.polygons, scene.metadata


def zsi_collision_vertex_to_shipwright(vertex: ZsiCollisionVertex) -> tuple[int, int, int]:
    return (vertex.x, vertex.y, vertex.z)


def zsi_collision_plane_to_shipwright(
    polygon: ZsiCollisionPolygon,
) -> tuple[int, int, int, int] | None:
    if polygon.normal[0] == 0 and polygon.normal[1] == 0 and polygon.normal[2] == 0:
        return None
    return (
        round_s16(polygon.normal[0]),
        round_s16(polygon.normal[1]),
        round_s16(polygon.normal[2]),
        round_s16(polygon.dist),
    )


def zsi_collision_normal_to_shipwright(normal: tuple[int, int, int]) -> tuple[int, int, int]:
    return normal


def orient_collision_plane_to_zsi_normal(
    plane: tuple[int, int, int, int],
    zsi_normal: tuple[int, int, int],
) -> tuple[int, int, int, int]:
    native_normal = zsi_collision_normal_to_shipwright(zsi_normal)
    dot = (
        plane[0] * native_normal[0]
        + plane[1] * native_normal[1]
        + plane[2] * native_normal[2]
    )
    if dot < 0:
        return (-plane[0], -plane[1], -plane[2], -plane[3])
    return plane


def camera_position_groups(
    camera_position_vectors: tuple[tuple[int, int, int], ...],
) -> tuple[CameraPositionData, ...]:
    return tuple(
        CameraPositionData(
            camera_position_vectors[index],
            camera_position_vectors[index + 1],
            camera_position_vectors[index + 2],
        )
        for index in range(0, len(camera_position_vectors) - 2, 3)
    )


def surface_type_audit(candidate: ZsiCollisionHeaderCandidate) -> dict[str, object]:
    max_polygon_type = max((polygon.type for polygon in candidate.polygons), default=-1)
    raw_surface_table_end = candidate.surface_type_offset + len(candidate.surface_types) * 8
    effective_surface_table_end = (
        candidate.effective_surface_type_offset + len(candidate.effective_surface_types) * 8
    )
    first = candidate.surface_types[0] if candidate.surface_types else None
    effective_first = candidate.effective_surface_types[0] if candidate.effective_surface_types else None
    return {
        "surface_type_count": len(candidate.effective_surface_types),
        "raw_surface_type_count": len(candidate.surface_types),
        "max_polygon_type": max_polygon_type,
        "covers_all_polygon_types": max_polygon_type < len(candidate.effective_surface_types),
        "raw_surface_table_end": raw_surface_table_end,
        "effective_surface_table_end": effective_surface_table_end,
        "bgcam_offset": candidate.bgcam_offset,
        "effective_bgcam_offset": candidate.effective_bgcam_offset,
        "raw_ends_at_bgcam_offset": raw_surface_table_end == candidate.bgcam_offset,
        "effective_ends_at_effective_bgcam_offset": (
            effective_surface_table_end == candidate.effective_bgcam_offset
        ),
        "surface_type_prefix_size": candidate.effective_surface_type_offset - candidate.surface_type_offset,
        "raw_first_data2_low16_is_polygon_marker": bool(first and (first.data2 & 0xFFFF) == 0x55DA),
        "effective_first_data2_low16_is_polygon_marker": bool(
            effective_first and (effective_first.data2 & 0xFFFF) == 0x55DA
        ),
        "raw_surface_type_zero": {"data1": first.data1, "data2": first.data2} if first else None,
        "effective_surface_type_zero": (
            {"data1": effective_first.data1, "data2": effective_first.data2}
            if effective_first
            else None
        ),
    }


def surface_type_semantic_audit(candidate: ZsiCollisionHeaderCandidate) -> dict[str, object]:
    usage = Counter(polygon.type for polygon in candidate.polygons)
    decoded_surface_types: list[dict[str, object]] = []
    for index, surface_type in enumerate(candidate.effective_surface_types):
        decoded_surface_types.append(
            {
                "index": index,
                "polygon_count": usage[index],
                "data1": surface_type.data1,
                "data2": surface_type.data2,
                **decode_surface_type(SurfaceType(surface_type.data1, surface_type.data2)),
            }
        )

    field_names = tuple(
        name
        for name in decoded_surface_types[0].keys()
        if name not in {"index", "polygon_count", "data1", "data2"}
    ) if decoded_surface_types else ()
    nonzero_field_counts = {
        name: sum(1 for entry in decoded_surface_types if entry[name])
        for name in field_names
    }
    invalid_camera_data_indices = [
        entry["index"]
        for entry in decoded_surface_types
        if entry["cam_data_index"] >= candidate.bgcam_count
    ]
    return {
        "decoded_surface_types": decoded_surface_types,
        "used_surface_type_count": sum(1 for count in usage.values() if count > 0),
        "nonzero_field_counts": nonzero_field_counts,
        "invalid_camera_data_indices": invalid_camera_data_indices,
        "polygon_flag_counts": collision_polygon_flag_counts(candidate),
        "notes": [
            "SurfaceType Data1 maps to N64 SurfaceType.data[0]; Data2 maps to data[1].",
            "ignore camera/entity/projectile and conveyor flags live in polygon vertex-index high bits.",
        ],
    }


def decode_surface_type(surface_type: SurfaceType) -> dict[str, int | bool]:
    data0 = surface_type.data1 & 0xFFFFFFFF
    data1 = surface_type.data2 & 0xFFFFFFFF
    wall_property = (data0 >> 21) & 0x1F
    return {
        "cam_data_index": data0 & 0xFF,
        "scene_exit_index": (data0 >> 8) & 0x1F,
        "floor_type": (data0 >> 13) & 0x1F,
        "surface_unk_18_20": (data0 >> 18) & 0x7,
        "wall_property": wall_property,
        "wall_flags": WALL_PROPERTY_FLAGS[wall_property],
        "floor_property": (data0 >> 26) & 0xF,
        "floor_is_minus_one": bool((data0 >> 30) & 1),
        "horse_blocked": bool((data0 >> 31) & 1),
        "sfx_material": data1 & 0xF,
        "slope": (data1 >> 4) & 0x3,
        "light_setting_index": (data1 >> 6) & 0x1F,
        "echo": (data1 >> 11) & 0x3F,
        "hookshot": bool((data1 >> 17) & 1),
        "conveyor_speed": (data1 >> 18) & 0x7,
        "conveyor_direction": (data1 >> 21) & 0x3F,
        "wall_damage": bool(data1 & 0x08000000),
    }


def decode_collision_polygon_flags(flags_via: int, flags_vib: int) -> dict[str, int | bool]:
    return {
        "vertex_a": flags_via & 0x1FFF,
        "vertex_b": flags_vib & 0x1FFF,
        "flags_via_raw": flags_via & 0xE000,
        "flags_vib_raw": flags_vib & 0xE000,
        "ignore_camera": bool(flags_via & 0x2000),
        "ignore_entities": bool(flags_via & 0x4000),
        "ignore_projectiles": bool(flags_via & 0x8000),
        "conveyor": bool(flags_vib & 0x2000),
    }


def collision_polygon_flag_counts(candidate: ZsiCollisionHeaderCandidate) -> dict[str, object]:
    counts = Counter()
    flags_via_values = Counter()
    flags_vib_values = Counter()
    for polygon in candidate.polygons:
        flags = decode_collision_polygon_flags(polygon.raw_vtx_a, polygon.raw_vtx_b)
        for name in ("ignore_camera", "ignore_entities", "ignore_projectiles", "conveyor"):
            if flags[name]:
                counts[name] += 1
        flags_via_values[int(flags["flags_via_raw"])] += 1
        flags_vib_values[int(flags["flags_vib_raw"])] += 1
    return {
        "ignore_camera": counts["ignore_camera"],
        "ignore_entities": counts["ignore_entities"],
        "ignore_projectiles": counts["ignore_projectiles"],
        "conveyor": counts["conveyor"],
        "flags_via_values": dict(sorted(flags_via_values.items())),
        "flags_vib_values": dict(sorted(flags_vib_values.items())),
    }


def selected_zsi_water_boxes(candidate: ZsiCollisionHeaderCandidate) -> tuple[ZsiWaterBox, ...]:
    if candidate.effective_water_boxes:
        return candidate.effective_water_boxes
    if candidate.prefixed_water_boxes:
        return candidate.prefixed_water_boxes
    return candidate.water_boxes


def zsi_water_box_to_shipwright(water_box: ZsiWaterBox) -> WaterBox:
    return WaterBox(
        water_box.x_min,
        water_box.y_surface,
        water_box.z_min,
        water_box.x_length,
        water_box.z_length,
        water_box.properties,
    )


def build_scene_collision(models: list[tuple[int, CmbModel]]) -> CollisionBuilder:
    builder = CollisionBuilder()

    for _room_index, model in models:
        for mesh in model.meshes:
            if mesh.shape_index >= len(model.shapes):
                builder.skipped["missing_shape"] += 1
                continue

            material = model.materials[mesh.material_index] if mesh.material_index < len(model.materials) else None
            texture_names = material_texture_names(model, material)
            if not include_material_in_collision(material, texture_names):
                builder.skipped["material_filtered"] += sum(
                    len(primitive.indices) // 3 for primitive in model.shapes[mesh.shape_index].primitives
                )
                continue

            shape = model.shapes[mesh.shape_index]
            for primitive in shape.primitives:
                if primitive.skinning_mode != 0:
                    builder.skipped["skinned_primitive"] += len(primitive.indices) // 3
                    continue
                for triangle in chunk_triangles(primitive.indices):
                    try:
                        vertices = tuple(
                            round_position(shape.positions[index])
                            for index in triangle
                        )
                    except IndexError:
                        builder.skipped["bad_index"] += 1
                        continue

                    if len(set(vertices)) != 3:
                        builder.skipped["degenerate"] += 1
                        continue

                    plane = collision_plane(vertices)
                    if plane is None:
                        builder.skipped["degenerate"] += 1
                        continue

                    a = builder.vertex_index(vertices[0])
                    b = builder.vertex_index(vertices[1])
                    c = builder.vertex_index(vertices[2])
                    builder.polygons.append(CollisionPolygon(0, a, b, c, *plane))

    return builder


def include_material_in_collision(material: Material | None, texture_names: tuple[str, ...]) -> bool:
    if material is not None and (material.blend_mode != 0 or not material.depth_write):
        return False
    if any(is_water_texture(name) for name in texture_names):
        return False
    if texture_names and all(is_foliage_texture(name) for name in texture_names):
        return False
    return True


def is_water_texture(name: str) -> bool:
    lowered = name.lower()
    return "kawa" in lowered or "mizu" in lowered or "water" in lowered


def is_foliage_texture(name: str) -> bool:
    lowered = name.lower()
    return any(token in lowered for token in FOLIAGE_TEXTURE_TOKENS)


def material_texture_names(model: CmbModel, material: Material | None) -> tuple[str, ...]:
    if material is None:
        return ()
    names: list[str] = []
    for index in material.texture_indices:
        if index >= 0 and index < len(model.textures):
            names.append(model.textures[index].name)
    return tuple(names)


def round_position(position: Vec3) -> tuple[int, int, int]:
    return (round_s16(position.x), round_s16(position.y), round_s16(position.z))


def collision_plane(vertices: tuple[tuple[int, int, int], tuple[int, int, int], tuple[int, int, int]]) -> tuple[int, int, int, int] | None:
    a, b, c = vertices
    ab = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
    ac = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
    cross = (
        ab[1] * ac[2] - ab[2] * ac[1],
        ab[2] * ac[0] - ab[0] * ac[2],
        ab[0] * ac[1] - ab[1] * ac[0],
    )
    length = math.sqrt(cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2])
    if length < 1.0:
        return None

    normal = tuple(round_s16(component / length * 32767.0) for component in cross)
    dist = round_s16(-sum((normal[index] / 32767.0) * a[index] for index in range(3)))
    return (normal[0], normal[1], normal[2], dist)


def collision_header_xml(
    vertices: tuple[tuple[int, int, int], ...],
    polygons: tuple[CollisionPolygon, ...],
    metadata: CollisionMetadata,
    *,
    bounds_min: tuple[int, int, int] | None = None,
    bounds_max: tuple[int, int, int] | None = None,
) -> str:
    effective_bounds_min, effective_bounds_max = collision_resource_bounds(
        vertices,
        bounds_min=bounds_min,
        bounds_max=bounds_max,
    )
    min_x, min_y, min_z = effective_bounds_min
    max_x, max_y, max_z = effective_bounds_max

    lines = [
        (
            '<CollisionHeader Version="0" '
            f'MinBoundsX="{min_x}" MinBoundsY="{min_y}" MinBoundsZ="{min_z}" '
            f'MaxBoundsX="{max_x}" MaxBoundsY="{max_y}" MaxBoundsZ="{max_z}">'
        )
    ]
    for x, y, z in vertices:
        lines.append(f'\t<Vertex X="{x}" Y="{y}" Z="{z}"/>')
    for polygon in polygons:
        lines.append(
            "\t<Polygon "
            f'Type="{polygon.type}" VertexA="{polygon.vertex_a}" VertexB="{polygon.vertex_b}" '
            f'VertexC="{polygon.vertex_c}" NormalX="{polygon.normal_x}" '
            f'NormalY="{polygon.normal_y}" NormalZ="{polygon.normal_z}" Dist="{polygon.dist}"/>'
        )
    surface_types = metadata.surface_types or (SurfaceType(0, 0),)
    for surface_type in surface_types:
        lines.append(f'\t<PolygonType Data1="{surface_type.data1}" Data2="{surface_type.data2}"/>')
    for entry in metadata.camera_data:
        lines.append(
            "\t<CameraData "
            f'SType="{entry.stype}" NumData="{entry.num_data}" '
            f'CameraPosDataSeg="{entry.camera_pos_data_index}"/>'
        )
    for entry in metadata.camera_positions:
        lines.append(
            "\t<CameraPositionData "
            f'PosX="{entry.pos[0]}" PosY="{entry.pos[1]}" PosZ="{entry.pos[2]}" '
            f'RotX="{entry.rot[0]}" RotY="{entry.rot[1]}" RotZ="{entry.rot[2]}" '
            f'FOV="{entry.other[0]}" JfifID="{entry.other[1]}" Unknown="{entry.other[2]}"/>'
        )
    for water_box in metadata.water_boxes:
        lines.append(
            "\t<WaterBox "
            f'XMin="{water_box.x_min}" Ysurface="{water_box.y_surface}" '
            f'ZMin="{water_box.z_min}" XLength="{water_box.x_length}" '
            f'ZLength="{water_box.z_length}" Properties="{water_box.properties}"/>'
        )
    lines.append("</CollisionHeader>")
    return "\n".join(lines) + "\n"


def load_collision_scene_from_o2r(path: Path, resource_path: str) -> CollisionScene:
    with zipfile.ZipFile(path) as archive:
        data = archive.read(resource_path.strip("/"))
    return parse_binary_collision_scene(data, f"{path}!{resource_path}")


def load_collision_metadata_from_o2r(path: Path, resource_path: str) -> CollisionMetadata:
    return load_collision_scene_from_o2r(path, resource_path).metadata


def parse_binary_collision_scene(data: bytes, source: str = "<collision>") -> CollisionScene:
    if len(data) < 0x40 + 12:
        raise ParseError(f"{source}: collision resource is too small")
    if struct.unpack_from("<I", data, 4)[0] != 0x4F434F4C:
        raise ParseError(f"{source}: expected OCOL collision resource")

    offset = 0x40

    def need(size: int) -> None:
        if offset + size > len(data):
            raise ParseError(f"{source}: collision resource is truncated")

    def read_i16() -> int:
        nonlocal offset
        need(2)
        value = struct.unpack_from("<h", data, offset)[0]
        offset += 2
        return value

    def read_u16() -> int:
        nonlocal offset
        need(2)
        value = struct.unpack_from("<H", data, offset)[0]
        offset += 2
        return value

    def read_i32() -> int:
        nonlocal offset
        need(4)
        value = struct.unpack_from("<i", data, offset)[0]
        offset += 4
        return value

    def read_u32() -> int:
        nonlocal offset
        need(4)
        value = struct.unpack_from("<I", data, offset)[0]
        offset += 4
        return value

    bounds_min = (read_i16(), read_i16(), read_i16())
    bounds_max = (read_i16(), read_i16(), read_i16())
    vertex_count = read_i32()
    vertices = tuple((read_i16(), read_i16(), read_i16()) for _ in range(vertex_count))
    polygon_count = read_u32()
    polygons: list[CollisionPolygon] = []
    for _ in range(polygon_count):
        polygons.append(
            CollisionPolygon(
                read_u16(),
                read_u16(),
                read_u16(),
                read_u16(),
                read_i16(),
                read_i16(),
                read_i16(),
                read_i16(),
            )
        )

    surface_type_count = read_u32()
    surface_types: list[SurfaceType] = []
    for _ in range(surface_type_count):
        data2 = read_u32()
        data1 = read_u32()
        surface_types.append(SurfaceType(data1, data2))

    camera_data_count = read_u32()
    camera_data: list[CameraData] = []
    for _ in range(camera_data_count):
        camera_data.append(CameraData(read_u16(), read_i16(), read_i32()))

    camera_position_vec_count = read_i32()
    raw_positions = [
        (read_i16(), read_i16(), read_i16())
        for _ in range(camera_position_vec_count)
    ]
    camera_positions: list[CameraPositionData] = []
    for index in range(0, len(raw_positions) - 2, 3):
        camera_positions.append(
            CameraPositionData(
                raw_positions[index],
                raw_positions[index + 1],
                raw_positions[index + 2],
            )
        )

    water_box_count = read_i32()
    water_boxes: list[WaterBox] = []
    for _ in range(water_box_count):
        water_boxes.append(
            WaterBox(
                read_i16(),
                read_i16(),
                read_i16(),
                read_i16(),
                read_i16(),
                read_i32(),
            )
        )

    return CollisionScene(
        vertices=vertices,
        polygons=tuple(polygons),
        metadata=CollisionMetadata(
            camera_data=tuple(camera_data),
            camera_positions=tuple(camera_positions),
            water_boxes=tuple(water_boxes),
            surface_types=tuple(surface_types),
        ),
        source=source,
        source_format="shipwright_ocol",
        bounds_min=bounds_min,
        bounds_max=bounds_max,
    )


def parse_binary_collision_metadata(data: bytes, source: str = "<collision>") -> CollisionMetadata:
    return parse_binary_collision_scene(data, source).metadata
