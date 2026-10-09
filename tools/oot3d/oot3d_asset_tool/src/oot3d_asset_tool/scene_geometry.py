"""Offline world-space geometry queries; no renderer or game-state assumptions."""
from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Iterable

from .binary import ParseError
from .cmb import CmbModel
from .legacy_fast_resource import skeleton_world_transforms, transform_position

Vector = tuple[float, float, float]


def yaw_degrees(binary_angle: int) -> float:
    """Signed binary angle -> degrees, zero toward +Z, +90 toward +X."""
    return ((binary_angle + 32768) % 65536 - 32768) * (180.0 / 32768)


def forward(yaw: float, pitch: float = 0.0) -> list[float]:
    y, p = math.radians(yaw), math.radians(pitch)
    return [math.sin(y) * math.cos(p), math.sin(p), math.cos(y) * math.cos(p)]


def visual_bounds(models: Iterable[CmbModel]) -> dict:
    """Union of referenced rigid CMB vertices after hierarchical bone transforms.

    Includes all visibility groups. Skinned primitives are explicitly excluded;
    this is neither collision bounds nor a prediction of active draw visibility.
    """
    low, high = [math.inf] * 3, [-math.inf] * 3
    count, skipped = 0, 0
    for model in models:
        # Validate hierarchy first: the shared transform helper assumes a tree.
        for index in range(len(model.skeleton.bones)):
            visited = set()
            current = index
            while current >= 0:
                if current >= len(model.skeleton.bones) or current in visited:
                    raise ParseError("CMB bone hierarchy is cyclic or out of range")
                visited.add(current)
                current = model.skeleton.bones[current].parent_index
        transforms = skeleton_world_transforms(model.skeleton)
        for mesh in model.meshes:
            if not 0 <= mesh.shape_index < len(model.shapes):
                raise ParseError("CMB mesh shape is out of range")
            shape = model.shapes[mesh.shape_index]
            for primitive in shape.primitives:
                if primitive.skinning_mode != 0 or len(primitive.bone_indices) != 1:
                    skipped += 1
                    continue
                bone = primitive.bone_indices[0]
                if not 0 <= bone < len(transforms):
                    raise ParseError("CMB rigid primitive bone is out of range")
                for index in sorted(set(primitive.indices)):
                    if not 0 <= index < len(shape.positions):
                        raise ParseError("CMB vertex index is out of range")
                    pos = transform_position(transforms[bone], shape.positions[index])
                    for axis, value in enumerate((pos.x, pos.y, pos.z)):
                        if not math.isfinite(value):
                            raise ParseError("CMB transformed vertex is non-finite")
                        low[axis], high[axis] = min(low[axis], value), max(high[axis], value)
                    count += 1
    return {"kind": "visual", "min": low if count else None, "max": high if count else None,
            "status": "partial" if skipped else "decoded" if count else "unavailable",
            "referencedVertexCount": count, "skippedPrimitiveCount": skipped,
            "scope": "all embedded CMB visibility groups, rigid bind transforms; no actors or animation"}


def entrance_poses(entrance: dict, scene_setup: int, height: float = 60.0) -> list[dict]:
    if not math.isfinite(height):
        raise ValueError("eye height must be finite")
    if entrance.get("position") is None:
        return []
    eye = list(entrance["position"])
    eye[1] += height
    result = []
    for suffix, offset in (("forward", 0.0), ("back", 180.0)):
        yaw = (entrance["yawDegrees"] + offset + 180) % 360 - 180
        result.append({"id": f"setup-{scene_setup}/entrance-{entrance['index']}/{suffix}",
                       "sceneSetup": scene_setup, "roomId": entrance["roomId"],
                       "entranceIndex": entrance["index"], "position": eye,
                       "yawDegrees": yaw, "pitchDegrees": 0.0, "forward": forward(yaw),
                       "status": "unverified", "reason": "spawn plus eye height; no visibility/clearance check"})
    return result


def overview_pose(bounds: dict, scene_setup: int, room: int) -> dict | None:
    if bounds["min"] is None:
        return None
    low, high = bounds["min"], bounds["max"]
    center = [(a + b) / 2 for a, b in zip(low, high)]
    reach = max(60.0, math.dist(low, high) * 0.6)
    eye = [center[0], high[1] + reach * 0.5, center[2] + reach]
    delta = [a - b for a, b in zip(center, eye)]
    yaw = math.degrees(math.atan2(delta[0], delta[2]))
    pitch = math.degrees(math.atan2(delta[1], math.hypot(delta[0], delta[2])))
    return {"id": f"setup-{scene_setup}/room-{room}/overview", "sceneSetup": scene_setup,
            "roomId": room, "position": eye, "yawDegrees": yaw, "pitchDegrees": pitch,
            "forward": forward(yaw, pitch), "lookAt": center, "status": "unverified",
            "reason": "visual AABB heuristic; may be inside geometry, outside the room, or occluded"}


@dataclass(frozen=True)
class RayPolygon:
    indices: tuple[int, int, int]
    surface_type: int | None = None
    surface_words: tuple[int, int] | None = None
    source_index: int = 0
    native_normal: tuple[int, int, int] | None = None


@dataclass(frozen=True)
class CollisionMesh:
    identity: str
    vertices: tuple[Vector, ...]
    polygons: tuple[RayPolygon, ...]


def collision_ray(meshes: Iterable[CollisionMesh], x: float, z: float,
                  y_origin: float | None = None) -> dict:
    """All triangle intersections on a downward ray, including ceilings/backfaces.

    Coincident hits on distinct polygons are retained (e.g. shared triangle
    edges). Hits are sorted by distance, mesh identity and original polygon index.
    """
    meshes = tuple(meshes)
    if not all(math.isfinite(v) for v in (x, z)) or (y_origin is not None and not math.isfinite(y_origin)):
        raise ValueError("ray coordinates must be finite")
    if y_origin is None:
        y_origin = max((p[1] for m in meshes for p in m.vertices), default=0.0) + 1.0
    hits = []
    for mesh in meshes:
        for polygon in mesh.polygons:
            if any(i < 0 or i >= len(mesh.vertices) for i in polygon.indices):
                raise ParseError(f"{mesh.identity}: collision polygon index out of range")
            a, b, c = (mesh.vertices[i] for i in polygon.indices)
            # XZ barycentric intersection avoids treating vertical walls as floors.
            det = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2])
            if abs(det) < 1e-10:
                continue
            u = ((b[2] - c[2]) * (x - c[0]) + (c[0] - b[0]) * (z - c[2])) / det
            v = ((c[2] - a[2]) * (x - c[0]) + (a[0] - c[0]) * (z - c[2])) / det
            w = 1 - u - v
            if min(u, v, w) < -1e-9:
                continue
            y = u * a[1] + v * b[1] + w * c[1]
            if y > y_origin + 1e-8:
                continue
            ab, ac = [b[i] - a[i] for i in range(3)], [c[i] - a[i] for i in range(3)]
            normal = [ab[1]*ac[2]-ab[2]*ac[1], ab[2]*ac[0]-ab[0]*ac[2], ab[0]*ac[1]-ab[1]*ac[0]]
            length = math.sqrt(sum(n*n for n in normal))
            hits.append({"position": [x, y, z], "distance": max(0.0, y_origin-y),
                         "normal": [n/length for n in normal], "normalSource": "triangle winding",
                         "nativeNormalS16": polygon.native_normal,
                         "mesh": mesh.identity, "polygonIndex": polygon.source_index,
                         "surfaceType": polygon.surface_type, "surfaceWords": polygon.surface_words})
    hits.sort(key=lambda h: (h["distance"], h["mesh"], h["polygonIndex"]))
    return {"origin": [x, y_origin, z], "direction": [0, -1, 0],
            "status": "hit" if hits else "no-hit", "hits": hits,
            "method": "static collision triangles; no dynamic actors, surface filtering or game collision flags"}
