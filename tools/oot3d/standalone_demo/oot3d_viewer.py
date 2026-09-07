from __future__ import annotations

import argparse
import json
import math
import time
from pathlib import Path
import tkinter as tk

from oot3d_demo import (
    accessor_data,
    floor_y_at,
    parse_collision_xml,
    read_glb,
    read_json,
    write_json,
)


VIEWER_TEST_FORMAT = "oot3d_standalone_demo_viewer_self_test_v1"


def vec_sub(a: tuple[float, float, float], b: tuple[float, float, float]) -> tuple[float, float, float]:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def vec_dot(a: tuple[float, float, float], b: tuple[float, float, float]) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def vec_cross(a: tuple[float, float, float], b: tuple[float, float, float]) -> tuple[float, float, float]:
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def vec_norm(v: tuple[float, float, float]) -> tuple[float, float, float]:
    length = math.sqrt(vec_dot(v, v))
    if length <= 1e-8:
        return (0.0, 0.0, 1.0)
    return (v[0] / length, v[1] / length, v[2] / length)


class MeshFrames:
    def __init__(self, triangles_by_frame: list[list[tuple[tuple[float, float, float], ...]]]) -> None:
        self.triangles_by_frame = triangles_by_frame
        self.frame_count = len(triangles_by_frame)

    def frame(self, index: int) -> list[tuple[tuple[float, float, float], ...]]:
        if not self.triangles_by_frame:
            return []
        return self.triangles_by_frame[index % len(self.triangles_by_frame)]


def load_static_mesh(path: Path, *, scale: float, limit: int | None = None) -> list[tuple[tuple[float, float, float], ...]]:
    gltf, bin_chunk = read_glb(path)
    meshes = gltf.get("meshes", [])
    assert isinstance(meshes, list)
    triangles: list[tuple[tuple[float, float, float], ...]] = []
    for mesh in meshes:
        assert isinstance(mesh, dict)
        primitives = mesh.get("primitives", [])
        assert isinstance(primitives, list)
        for primitive in primitives:
            assert isinstance(primitive, dict)
            attrs = primitive.get("attributes", {})
            assert isinstance(attrs, dict)
            if "POSITION" not in attrs:
                continue
            positions_raw, comps, _ = accessor_data(gltf, bin_chunk, int(attrs["POSITION"]))
            if comps != 3:
                continue
            positions = [
                (
                    float(positions_raw[i]) * scale,
                    float(positions_raw[i + 1]) * scale,
                    float(positions_raw[i + 2]) * scale,
                )
                for i in range(0, len(positions_raw), 3)
            ]
            indices = primitive_indices(gltf, bin_chunk, primitive, len(positions))
            for i in range(0, len(indices) - 2, 3):
                triangles.append((positions[indices[i]], positions[indices[i + 1]], positions[indices[i + 2]]))
                if limit is not None and len(triangles) >= limit:
                    return triangles
    return triangles


def primitive_indices(
    gltf: dict[str, object],
    bin_chunk: bytes,
    primitive: dict[str, object],
    position_count: int,
) -> list[int]:
    if "indices" not in primitive:
        return list(range(position_count))
    idx_raw, _, _ = accessor_data(gltf, bin_chunk, int(primitive["indices"]))
    return [int(v) for v in idx_raw]


def load_morph_mesh_frames(
    path: Path,
    *,
    scale: float,
    limit_per_frame: int | None = None,
) -> MeshFrames:
    gltf, bin_chunk = read_glb(path)
    meshes = gltf.get("meshes", [])
    assert isinstance(meshes, list)
    frame_count = 1
    for mesh in meshes:
        assert isinstance(mesh, dict)
        primitives = mesh.get("primitives", [])
        assert isinstance(primitives, list)
        for primitive in primitives:
            assert isinstance(primitive, dict)
            frame_count = max(frame_count, 1 + len(primitive.get("targets", [])))

    frames: list[list[tuple[tuple[float, float, float], ...]]] = [[] for _ in range(frame_count)]
    for mesh in meshes:
        assert isinstance(mesh, dict)
        primitives = mesh.get("primitives", [])
        assert isinstance(primitives, list)
        for primitive in primitives:
            assert isinstance(primitive, dict)
            attrs = primitive.get("attributes", {})
            assert isinstance(attrs, dict)
            if "POSITION" not in attrs:
                continue
            base_raw, comps, _ = accessor_data(gltf, bin_chunk, int(attrs["POSITION"]))
            if comps != 3:
                continue
            base = [
                (float(base_raw[i]), float(base_raw[i + 1]), float(base_raw[i + 2]))
                for i in range(0, len(base_raw), 3)
            ]
            targets = primitive.get("targets", [])
            target_positions: list[list[tuple[float, float, float]]] = []
            if isinstance(targets, list):
                for target in targets:
                    assert isinstance(target, dict)
                    if "POSITION" not in target:
                        continue
                    delta_raw, delta_comps, _ = accessor_data(gltf, bin_chunk, int(target["POSITION"]))
                    if delta_comps != 3:
                        continue
                    target_positions.append(
                        [
                            (
                                float(delta_raw[i]),
                                float(delta_raw[i + 1]),
                                float(delta_raw[i + 2]),
                            )
                            for i in range(0, len(delta_raw), 3)
                        ]
                    )
            indices = primitive_indices(gltf, bin_chunk, primitive, len(base))
            for frame_index in range(frame_count):
                if limit_per_frame is not None and len(frames[frame_index]) >= limit_per_frame:
                    continue
                if frame_index == 0 or frame_index - 1 >= len(target_positions):
                    positions = base
                else:
                    delta = target_positions[frame_index - 1]
                    positions = [
                        (
                            base[i][0] + delta[i][0],
                            base[i][1] + delta[i][1],
                            base[i][2] + delta[i][2],
                        )
                        for i in range(len(base))
                    ]
                scaled = [(p[0] * scale, p[1] * scale, p[2] * scale) for p in positions]
                for i in range(0, len(indices) - 2, 3):
                    if limit_per_frame is not None and len(frames[frame_index]) >= limit_per_frame:
                        break
                    frames[frame_index].append(
                        (scaled[indices[i]], scaled[indices[i + 1]], scaled[indices[i + 2]])
                    )
    return MeshFrames(frames)


def mesh_bounds(triangles: list[tuple[tuple[float, float, float], ...]]) -> tuple[tuple[float, float, float], tuple[float, float, float]]:
    points = [v for tri in triangles for v in tri]
    return (
        (min(p[0] for p in points), min(p[1] for p in points), min(p[2] for p in points)),
        (max(p[0] for p in points), max(p[1] for p in points), max(p[2] for p in points)),
    )


class DemoViewer:
    def __init__(self, manifest_path: Path, *, room_limit: int, link_limit: int) -> None:
        self.manifest_path = manifest_path
        self.manifest = read_json(manifest_path)
        assets = self.manifest["assets"]
        assert isinstance(assets, dict)
        self.collision = parse_collision_xml(Path(str(assets["collision_xml"])))
        self.room_tris = load_static_mesh(Path(str(assets["room_glb"])), scale=10000.0, limit=room_limit)
        self.link_frames = load_morph_mesh_frames(Path(str(assets["link_child_runtime_glb"])), scale=82.0, limit_per_frame=link_limit)
        link_min, _link_max = mesh_bounds(self.link_frames.frame(0))
        self.link_y_offset = -link_min[1]

        movement = self.manifest.get("movement", {})
        assert isinstance(movement, dict)
        spawn = movement.get("spawn", {"x": 0.0, "y": 0.0, "z": 0.0})
        assert isinstance(spawn, dict)
        self.pos = [
            float(spawn.get("x", 0.0)),
            float(spawn.get("y", 0.0)),
            float(spawn.get("z", 0.0)),
        ]
        floor = floor_y_at(self.collision, self.pos[0], self.pos[2], self.pos[1] + 120.0)
        if floor is not None:
            self.pos[1] = floor
        self.speed = float(movement.get("speed_units_per_second", 45.0))
        self.radius = float(movement.get("radius_units", 12.0))
        self.keys: set[str] = set()
        self.anim_time = 0.0
        self.last_time = time.perf_counter()

        self.root = tk.Tk()
        self.root.title("OOT3D standalone link-house data PoC")
        self.canvas = tk.Canvas(self.root, width=960, height=720, bg="#f5f2ea", highlightthickness=0)
        self.canvas.pack(fill=tk.BOTH, expand=True)
        self.root.bind("<KeyPress>", self.on_key_press)
        self.root.bind("<KeyRelease>", self.on_key_release)

    def on_key_press(self, event: tk.Event) -> None:
        self.keys.add(str(event.keysym).lower())

    def on_key_release(self, event: tk.Event) -> None:
        self.keys.discard(str(event.keysym).lower())

    def run(self) -> None:
        self.root.after(16, self.tick)
        self.root.mainloop()

    def tick(self) -> None:
        now = time.perf_counter()
        dt = min(now - self.last_time, 0.05)
        self.last_time = now
        moved = self.update_player(dt)
        if moved:
            self.anim_time += dt * 8.0
        self.draw()
        self.root.after(33, self.tick)

    def update_player(self, dt: float) -> bool:
        dx = 0.0
        dz = 0.0
        if "w" in self.keys or "up" in self.keys:
            dz -= 1.0
        if "s" in self.keys or "down" in self.keys:
            dz += 1.0
        if "a" in self.keys or "left" in self.keys:
            dx -= 1.0
        if "d" in self.keys or "right" in self.keys:
            dx += 1.0
        length = math.hypot(dx, dz)
        if length <= 0.0:
            return False
        dx /= length
        dz /= length
        next_x = self.pos[0] + dx * self.speed * dt
        next_z = self.pos[2] + dz * self.speed * dt
        next_x = max(self.collision.bounds_min[0] + self.radius, min(self.collision.bounds_max[0] - self.radius, next_x))
        next_z = max(self.collision.bounds_min[2] + self.radius, min(self.collision.bounds_max[2] - self.radius, next_z))
        floor = floor_y_at(self.collision, next_x, next_z, self.pos[1] + 120.0)
        if floor is None:
            return False
        self.pos = [next_x, floor, next_z]
        return True

    def camera_basis(self) -> tuple[tuple[float, float, float], tuple[float, float, float], tuple[float, float, float], tuple[float, float, float]]:
        eye = (self.pos[0], self.pos[1] + 88.0, self.pos[2] + 220.0)
        target = (self.pos[0], self.pos[1] + 36.0, self.pos[2])
        forward = vec_norm(vec_sub(target, eye))
        right = vec_norm(vec_cross(forward, (0.0, 1.0, 0.0)))
        up = vec_cross(right, forward)
        return eye, forward, right, up

    def project(self, p: tuple[float, float, float], width: int, height: int) -> tuple[int, int] | None:
        eye, forward, right, up = self.camera_basis()
        rel = vec_sub(p, eye)
        z = vec_dot(rel, forward)
        if z <= 8.0:
            return None
        x = vec_dot(rel, right)
        y = vec_dot(rel, up)
        focal = width * 0.82
        return (int(width * 0.5 + focal * x / z), int(height * 0.54 - focal * y / z))

    def draw(self) -> None:
        width = max(int(self.canvas.winfo_width()), 1)
        height = max(int(self.canvas.winfo_height()), 1)
        self.canvas.delete("all")
        self.draw_tris(self.room_tris, "#59636a", width, height)
        frame_index = int(self.anim_time) % max(self.link_frames.frame_count, 1)
        link_tris = [self.transform_link_tri(t) for t in self.link_frames.frame(frame_index)]
        self.draw_tris(link_tris, "#146b44", width, height)
        self.canvas.create_text(
            14,
            12,
            anchor="nw",
            fill="#1f2933",
            font=("Consolas", 11),
            text=(
                "OOT3D data PoC | WASD/arrows move | "
                f"pos=({self.pos[0]:.1f},{self.pos[1]:.1f},{self.pos[2]:.1f}) frame={frame_index}"
            ),
        )

    def transform_link_tri(
        self,
        tri: tuple[tuple[float, float, float], ...],
    ) -> tuple[tuple[float, float, float], ...]:
        return tuple(
            (
                self.pos[0] + v[0],
                self.pos[1] + self.link_y_offset + v[1],
                self.pos[2] + v[2],
            )
            for v in tri
        )

    def draw_tris(
        self,
        triangles: list[tuple[tuple[float, float, float], ...]],
        color: str,
        width: int,
        height: int,
    ) -> None:
        for tri in triangles:
            pts = [self.project(v, width, height) for v in tri]
            if any(p is None for p in pts):
                continue
            p0, p1, p2 = pts
            assert p0 is not None and p1 is not None and p2 is not None
            self.canvas.create_line(p0[0], p0[1], p1[0], p1[1], fill=color)
            self.canvas.create_line(p1[0], p1[1], p2[0], p2[1], fill=color)
            self.canvas.create_line(p2[0], p2[1], p0[0], p0[1], fill=color)


def self_test(args: argparse.Namespace) -> dict[str, object]:
    manifest_path = Path(args.manifest)
    manifest = read_json(manifest_path)
    assets = manifest["assets"]
    assert isinstance(assets, dict)
    collision = parse_collision_xml(Path(str(assets["collision_xml"])))
    room = load_static_mesh(Path(str(assets["room_glb"])), scale=10000.0, limit=64)
    link = load_morph_mesh_frames(Path(str(assets["link_child_runtime_glb"])), scale=82.0, limit_per_frame=64)
    movement = manifest.get("movement", {})
    assert isinstance(movement, dict)
    spawn = movement.get("spawn", {"x": 0.0, "y": 0.0, "z": 0.0})
    assert isinstance(spawn, dict)
    floor = floor_y_at(collision, float(spawn.get("x", 0.0)), float(spawn.get("z", 0.0)), 120.0)
    issues: list[str] = []
    if not room:
        issues.append("room_mesh_empty")
    if link.frame_count < 2:
        issues.append("link_animation_frame_count_too_small")
    if not link.frame(0):
        issues.append("link_mesh_empty")
    if floor is None:
        issues.append("spawn_floor_missing")
    result = {
        "format": VIEWER_TEST_FORMAT,
        "status": "valid" if not issues else "invalid",
        "manifest": str(manifest_path),
        "room_triangle_sample_count": len(room),
        "link_frame_count": link.frame_count,
        "link_triangle_sample_count": len(link.frame(0)),
        "spawn_floor_y": floor,
        "issue_count": len(issues),
        "issues": issues,
    }
    output = Path(args.output) if args.output else manifest_path.with_name("viewer_self_test.json")
    write_json(output, result)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="Interactive viewer for the OOT3D standalone data PoC scaffold.")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--output", type=Path, default=None)
    parser.add_argument("--room-triangle-limit", type=int, default=1400)
    parser.add_argument("--link-triangle-limit", type=int, default=900)
    args = parser.parse_args()
    if args.self_test:
        result = self_test(args)
        print(json.dumps(result, indent=2))
        return 0 if result["status"] == "valid" else 1
    viewer = DemoViewer(args.manifest, room_limit=args.room_triangle_limit, link_limit=args.link_triangle_limit)
    viewer.run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
