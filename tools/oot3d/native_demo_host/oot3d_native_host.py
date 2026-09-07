from __future__ import annotations

import argparse
import json
import math
import sys
import time
from pathlib import Path


STANDALONE_DIR = Path(__file__).resolve().parents[1] / "standalone_demo"
if str(STANDALONE_DIR) not in sys.path:
    sys.path.insert(0, str(STANDALONE_DIR))

from oot3d_demo import (  # noqa: E402
    accessor_data,
    draw_tri_wire,
    floor_hit_at,
    glb_triangles,
    parse_collision_xml,
    project_iso,
    read_glb,
    read_json,
    write_json,
    write_png_rgb,
)
from oot3d_viewer import (  # noqa: E402
    load_morph_mesh_frames,
    load_static_mesh,
    mesh_bounds,
    vec_cross,
    vec_dot,
    vec_norm,
    vec_sub,
)


HOST_FORMAT = "oot3d_native_demo_host_probe_v1"
TRACE_FORMAT = "oot3d_native_demo_host_trace_v1"
VIEWER_TEST_FORMAT = "oot3d_native_demo_host_viewer_self_test_v1"


def require_manifest_policy(manifest: dict[str, object], issues: list[str]) -> None:
    policy = manifest.get("policy")
    if not isinstance(policy, dict):
        issues.append("manifest_policy_missing")
        return
    if policy.get("no_shipwright_runtime_replacement") is not True:
        issues.append("shipwright_runtime_replacement_not_forbidden")
    if policy.get("no_n64_runtime_asset_substitution") is not True:
        issues.append("n64_runtime_asset_substitution_not_forbidden")
    if policy.get("runtime_asset_policy") != "oot3d_native_or_offline_derived_only":
        issues.append("runtime_asset_policy_not_oot3d_native")


def glb_morph_frame_count(path: Path) -> int:
    gltf, _bin_chunk = read_glb(path)
    frame_count = 1
    meshes = gltf.get("meshes", [])
    if not isinstance(meshes, list):
        return frame_count
    for mesh in meshes:
        if not isinstance(mesh, dict):
            continue
        primitives = mesh.get("primitives", [])
        if not isinstance(primitives, list):
            continue
        for primitive in primitives:
            if not isinstance(primitive, dict):
                continue
            targets = primitive.get("targets", [])
            if isinstance(targets, list):
                frame_count = max(frame_count, 1 + len(targets))
    return frame_count


def glb_vertex_count(path: Path) -> int:
    gltf, bin_chunk = read_glb(path)
    meshes = gltf.get("meshes", [])
    if not isinstance(meshes, list):
        return 0
    count = 0
    for mesh in meshes:
        if not isinstance(mesh, dict):
            continue
        primitives = mesh.get("primitives", [])
        if not isinstance(primitives, list):
            continue
        for primitive in primitives:
            if not isinstance(primitive, dict):
                continue
            attrs = primitive.get("attributes", {})
            if not isinstance(attrs, dict) or "POSITION" not in attrs:
                continue
            positions, comps, _ = accessor_data(gltf, bin_chunk, int(attrs["POSITION"]))
            if comps == 3:
                count += len(positions) // 3
    return count


def run_host(manifest_path: Path, output_root: Path, *, width: int, height: int) -> dict[str, object]:
    manifest = read_json(manifest_path)
    issues: list[str] = []
    require_manifest_policy(manifest, issues)

    assets = manifest.get("assets", {})
    if not isinstance(assets, dict):
        raise RuntimeError(f"{manifest_path}: missing assets block")

    collision_path = Path(str(assets.get("collision_xml", "")))
    room_path = Path(str(assets.get("room_glb", "")))
    link_path = Path(str(assets.get("link_child_runtime_glb", "")))
    for label, path in (
        ("collision_xml", collision_path),
        ("room_glb", room_path),
        ("link_child_runtime_glb", link_path),
    ):
        if not path.is_file():
            issues.append(f"missing_asset:{label}")

    output_root.mkdir(parents=True, exist_ok=True)
    trace_path = output_root / "native_host_trace.json"
    preview_path = output_root / "native_host_preview.png"
    viewer_self_test_path = output_root / "native_host_viewer_self_test.json"

    scene = parse_collision_xml(collision_path)
    room_tris = glb_triangles(room_path, limit=3500)
    link_tris = glb_triangles(link_path, limit=1800)
    room_vertex_count = glb_vertex_count(room_path)
    link_vertex_count = glb_vertex_count(link_path)
    link_frame_count = glb_morph_frame_count(link_path)
    if not room_tris:
        issues.append("room_mesh_empty")
    if not link_tris:
        issues.append("link_mesh_empty")
    if link_frame_count < 2:
        issues.append("link_animation_morph_frames_missing")

    movement = manifest.get("movement", {})
    if not isinstance(movement, dict):
        movement = {}
    spawn = movement.get("spawn", {"x": 0.0, "y": 0.0, "z": 0.0})
    if not isinstance(spawn, dict):
        spawn = {"x": 0.0, "y": 0.0, "z": 0.0}
    x = float(spawn.get("x", 0.0))
    z = float(spawn.get("z", 0.0))
    first_floor = floor_hit_at(scene, x, z, float(spawn.get("y", 0.0)) + 120.0)
    y = first_floor.y if first_floor is not None else float(spawn.get("y", 0.0))
    if first_floor is None:
        issues.append("spawn_floor_missing")

    speed = float(movement.get("speed_units_per_second", 45.0))
    radius = float(movement.get("radius_units", 12.0))
    dt = 1.0 / 30.0
    commands = [(1.0, 0.0, 60), (0.0, -1.0, 45), (-1.0, 0.0, 60), (0.0, 1.0, 45)]
    frames: list[dict[str, object]] = []
    floor_hits = 0
    blocked = 0
    frame = 0
    last_floor_index = first_floor.polygon_index if first_floor is not None else -1
    last_surface_type = first_floor.type_index if first_floor is not None else -1

    for stick_x, stick_z, count in commands:
        length = math.hypot(stick_x, stick_z)
        ndx = stick_x / length if length > 0.0 else 0.0
        ndz = stick_z / length if length > 0.0 else 0.0
        for _ in range(count):
            old_x, old_y, old_z = x, y, z
            next_x = x + ndx * speed * dt
            next_z = z + ndz * speed * dt
            unclamped_x = next_x
            unclamped_z = next_z
            next_x = max(scene.bounds_min[0] + radius, min(scene.bounds_max[0] - radius, next_x))
            next_z = max(scene.bounds_min[2] + radius, min(scene.bounds_max[2] - radius, next_z))
            floor = floor_hit_at(scene, next_x, next_z, y + 120.0)
            did_block = floor is None
            if floor is None:
                blocked += 1
                vx = vy = vz = 0.0
            else:
                x, y, z = next_x, floor.y, next_z
                vx = (x - old_x) / dt
                vy = (y - old_y) / dt
                vz = (z - old_z) / dt
                last_floor_index = floor.polygon_index
                last_surface_type = floor.type_index
                floor_hits += 1
            yaw = math.degrees(math.atan2(ndx, ndz)) if length > 0.0 else 0.0
            frames.append(
                {
                    "frame": frame,
                    "dt": dt,
                    "input": {"stick_x": ndx, "stick_z": ndz, "buttons": []},
                    "position": [round(x, 4), round(y, 4), round(z, 4)],
                    "rotation": {"yaw_degrees": round(yaw, 4)},
                    "velocity": [round(vx, 4), round(vy, 4), round(vz, 4)],
                    "action_state": "native_probe_walk" if not did_block else "native_probe_blocked",
                    "animation": {
                        "source": str(movement.get("animation_source", "child/anim/nml_run_free.csab")),
                        "frame": frame % max(link_frame_count, 1),
                        "frame_count": link_frame_count,
                    },
                    "floor": {
                        "hit": not did_block,
                        "polygon_index": last_floor_index if not did_block else -1,
                        "surface_type": last_surface_type if not did_block else -1,
                    },
                    "collision_flags": {
                        "floor_hit": not did_block,
                        "bounds_clamped": next_x != unclamped_x or next_z != unclamped_z,
                        "blocked": did_block,
                    },
                    "camera": {
                        "kind": "native_probe_follow_camera",
                        "position": [round(x, 4), round(y + 76.0, 4), round(z + 180.0, 4)],
                        "target": [round(x, 4), round(y + 32.0, 4), round(z, 4)],
                    },
                }
            )
            frame += 1

    trace = {
        "format": TRACE_FORMAT,
        "status": "valid" if floor_hits > 0 and blocked < len(frames) else "invalid",
        "manifest": str(manifest_path),
        "frame_count": len(frames),
        "floor_hit_count": floor_hits,
        "blocked_step_count": blocked,
        "schema": "oot3d_native_host_trace_for_future_reference_compare_v1",
        "frames": frames,
        "frames_sample": frames[:20] + frames[-5:],
    }
    write_json(trace_path, trace)
    render_preview(preview_path, width, height, room_tris, link_tris, (x, y, z))
    viewer_test = viewer_self_test(manifest_path, viewer_self_test_path)

    if trace["status"] != "valid":
        issues.append("native_host_trace_invalid")
    if viewer_test["status"] != "valid":
        issues.append("native_host_viewer_self_test_invalid")
    result = {
        "format": HOST_FORMAT,
        "status": "valid" if not issues else "invalid",
        "host_class": "standalone_native_asset_host_probe",
        "claim": "autonomous_native_asset_launch_path_seed_not_runtime/three_ds_recomp_parity_demo",
        "manifest": str(manifest_path),
        "trace": str(trace_path),
        "preview_png": str(preview_path),
        "viewer_self_test": str(viewer_self_test_path),
        "runtime_n64_asset_substitution_used": False,
        "shipwright_replacement_path_used": False,
        "engine_target": "dedicated_shipwright_runtime/three_ds_recomp_fork",
        "current_host_runtime": "python_probe_pending_runtime/three_ds_recomp_port",
        "room_triangle_count": len(room_tris),
        "room_vertex_count": room_vertex_count,
        "link_triangle_count": len(link_tris),
        "link_vertex_count": link_vertex_count,
        "link_animation_frame_count": link_frame_count,
        "collision_polygon_count": len(scene.polygons),
        "trace_frame_count": len(frames),
        "viewer_self_test_status": viewer_test["status"],
        "issue_count": len(issues),
        "issues": issues,
        "remaining_promotion_blockers": [
            "port_host_probe_to_dedicated_runtime/three_ds_recomp_target",
            "replace_python_probe_movement_with_engine_player_update_path",
            "compare_against_oot3d_reference_trace",
        ],
    }
    write_json(output_root / "native_host_probe.json", result)
    return result


class NativeHostViewer:
    def __init__(self, manifest_path: Path, *, room_limit: int, link_limit: int) -> None:
        import tkinter as tk

        self.tk = tk
        self.manifest_path = manifest_path
        self.manifest = read_json(manifest_path)
        assets = self.manifest["assets"]
        assert isinstance(assets, dict)
        self.collision = parse_collision_xml(Path(str(assets["collision_xml"])))
        self.room_tris = load_static_mesh(Path(str(assets["room_glb"])), scale=10000.0, limit=room_limit)
        self.link_frames = load_morph_mesh_frames(
            Path(str(assets["link_child_runtime_glb"])), scale=82.0, limit_per_frame=link_limit
        )
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
        floor = floor_hit_at(self.collision, self.pos[0], self.pos[2], self.pos[1] + 120.0)
        if floor is not None:
            self.pos[1] = floor.y
        self.speed = float(movement.get("speed_units_per_second", 45.0))
        self.radius = float(movement.get("radius_units", 12.0))
        self.keys: set[str] = set()
        self.anim_time = 0.0
        self.last_time = time.perf_counter()

        self.root = tk.Tk()
        self.root.title("OOT3D native host probe")
        self.canvas = tk.Canvas(self.root, width=960, height=720, bg="#f4f2ec", highlightthickness=0)
        self.canvas.pack(fill=tk.BOTH, expand=True)
        self.root.bind("<KeyPress>", self.on_key_press)
        self.root.bind("<KeyRelease>", self.on_key_release)

    def on_key_press(self, event: object) -> None:
        self.keys.add(str(getattr(event, "keysym", "")).lower())

    def on_key_release(self, event: object) -> None:
        self.keys.discard(str(getattr(event, "keysym", "")).lower())

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
        floor = floor_hit_at(self.collision, next_x, next_z, self.pos[1] + 120.0)
        if floor is None:
            return False
        self.pos = [next_x, floor.y, next_z]
        return True

    def camera_basis(
        self,
    ) -> tuple[tuple[float, float, float], tuple[float, float, float], tuple[float, float, float], tuple[float, float, float]]:
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
        self.draw_tris(self.room_tris, "#526170", width, height)
        frame_index = int(self.anim_time) % max(self.link_frames.frame_count, 1)
        link_tris = [self.transform_link_tri(t) for t in self.link_frames.frame(frame_index)]
        self.draw_tris(link_tris, "#147047", width, height)
        self.canvas.create_text(
            14,
            12,
            anchor="nw",
            fill="#1f2933",
            font=("Consolas", 11),
            text=(
                "OOT3D native host probe | WASD/arrows move | "
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


def viewer_self_test(manifest_path: Path, output: Path) -> dict[str, object]:
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
    x = float(spawn.get("x", 0.0))
    z = float(spawn.get("z", 0.0))
    floor = floor_hit_at(collision, x, z, float(spawn.get("y", 0.0)) + 120.0)
    issues: list[str] = []
    if not room:
        issues.append("room_mesh_empty")
    if link.frame_count < 2:
        issues.append("link_animation_frame_count_too_small")
    if not link.frame(0):
        issues.append("link_mesh_empty")
    if floor is None:
        issues.append("spawn_floor_missing")
        y = float(spawn.get("y", 0.0))
    else:
        y = floor.y
    step_floor = floor_hit_at(collision, x + 1.5, z, y + 120.0)
    if step_floor is None:
        issues.append("scripted_step_floor_missing")
    result = {
        "format": VIEWER_TEST_FORMAT,
        "status": "valid" if not issues else "invalid",
        "manifest": str(manifest_path),
        "room_triangle_sample_count": len(room),
        "link_frame_count": link.frame_count,
        "link_triangle_sample_count": len(link.frame(0)),
        "spawn_floor_y": y,
        "scripted_step_floor_y": step_floor.y if step_floor is not None else None,
        "issue_count": len(issues),
        "issues": issues,
    }
    write_json(output, result)
    return result


def render_preview(
    output: Path,
    width: int,
    height: int,
    room_tris: list[tuple[tuple[float, float, float], ...]],
    link_tris: list[tuple[tuple[float, float, float], ...]],
    player_pos: tuple[float, float, float],
) -> None:
    pixels = bytearray([244, 242, 236] * width * height)
    player_x, player_y, player_z = player_pos
    link_scale = 82.0
    room_scale = 10000.0
    link_min_y = min((v[1] * link_scale for tri in link_tris for v in tri), default=0.0)

    def transform_room(v: tuple[float, float, float]) -> tuple[float, float, float]:
        return (v[0] * room_scale, v[1] * room_scale, v[2] * room_scale)

    def transform_link(v: tuple[float, float, float]) -> tuple[float, float, float]:
        return (
            player_x + v[0] * link_scale,
            player_y - link_min_y + v[1] * link_scale,
            player_z + v[2] * link_scale,
        )

    all_points = [transform_room(v) for tri in room_tris for v in tri]
    all_points.extend(transform_link(v) for tri in link_tris for v in tri)
    projected = [project_iso(p) for p in all_points]
    min_px = min(p[0] for p in projected)
    max_px = max(p[0] for p in projected)
    min_py = min(p[1] for p in projected)
    max_py = max(p[1] for p in projected)
    scale = 0.84 * min(width / max(max_px - min_px, 1e-6), height / max(max_py - min_py, 1e-6))

    def to_screen(v: tuple[float, float, float]) -> tuple[int, int]:
        px, py = project_iso(v)
        return (int((px - min_px) * scale + width * 0.08), int((py - min_py) * scale + height * 0.08))

    for tri in room_tris:
        draw_tri_wire(pixels, width, height, tuple(to_screen(transform_room(v)) for v in tri), (65, 78, 88))
    for tri in link_tris:
        draw_tri_wire(pixels, width, height, tuple(to_screen(transform_link(v)) for v in tri), (20, 112, 71))
    write_png_rgb(output, width, height, bytes(pixels))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run the OOT3D native demo host probe.")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, default=None)
    parser.add_argument("--width", type=int, default=960)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--launch-viewer", action="store_true")
    parser.add_argument("--room-triangle-limit", type=int, default=1400)
    parser.add_argument("--link-triangle-limit", type=int, default=900)
    args = parser.parse_args(argv)
    manifest_path = args.manifest.resolve()
    output_root = args.output_root
    if output_root is None:
        output_root = manifest_path.parent / "native_host"
    result = run_host(manifest_path, output_root, width=args.width, height=args.height)
    print(json.dumps(result, indent=2))
    if args.launch_viewer and result["status"] == "valid":
        viewer = NativeHostViewer(
            manifest_path,
            room_limit=args.room_triangle_limit,
            link_limit=args.link_triangle_limit,
        )
        viewer.run()
    return 0 if result["status"] == "valid" else 1


if __name__ == "__main__":
    raise SystemExit(main())
