from __future__ import annotations

import argparse
import json
import math
import os
import struct
import subprocess
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path
import xml.etree.ElementTree as ET


DEMO_FORMAT = "oot3d_standalone_demo_manifest_v1"
VERIFY_FORMAT = "oot3d_standalone_demo_verify_v1"
SIM_FORMAT = "oot3d_standalone_demo_movement_trace_v1"
READINESS_FORMAT = "oot3d_native_demo_readiness_v1"
RESOURCE_CONTRACT_FORMAT = "oot3d_native_resource_contract_v1"
NATIVE_DEMO_REQUIRED_ROLES = [
    "room_visual_mesh",
    "scene_collision",
    "player_model_and_animation",
    "player_asset_validation",
]
DEFAULT_ROMFS = Path(r"E:\ppssppvr\oot3d_decomp\work\extract\romfs")
DEFAULT_WORK_ROOT = Path(r"I:\oot3dre_work")
DEFAULT_SCENE_ID = "link_house"
DEFAULT_CSAB = "child/anim/nml_run_free.csab"


@dataclass(frozen=True)
class CollisionPoly:
    type_index: int
    a: int
    b: int
    c: int
    nx: int
    ny: int
    nz: int
    dist: int


@dataclass(frozen=True)
class CollisionScene:
    vertices: list[tuple[float, float, float]]
    polygons: list[CollisionPoly]
    bounds_min: tuple[float, float, float]
    bounds_max: tuple[float, float, float]


@dataclass(frozen=True)
class FloorHit:
    y: float
    polygon_index: int
    type_index: int
    normal: tuple[float, float, float]
    dist: int


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[3]


def tool_root_from_repo(repo_root: Path) -> Path:
    return repo_root / "tools" / "oot3d" / "oot3d_asset_tool"


def run_tool(tool_root: Path, args: list[str]) -> str:
    env = os.environ.copy()
    src = str(tool_root / "src")
    env["PYTHONPATH"] = src + (os.pathsep + env["PYTHONPATH"] if env.get("PYTHONPATH") else "")
    proc = subprocess.run(
        [sys.executable, "-m", "oot3d_asset_tool", *args],
        cwd=tool_root,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if proc.returncode != 0:
        raise RuntimeError(
            "oot3d_asset_tool failed with exit code "
            f"{proc.returncode}\ncommand: {' '.join(args)}\n{proc.stdout}"
        )
    return proc.stdout


def parse_tool_json(stdout: str) -> dict[str, object]:
    start = stdout.find("{")
    end = stdout.rfind("}")
    if start < 0 or end < start:
        raise RuntimeError(f"tool output did not contain JSON:\n{stdout}")
    return json.loads(stdout[start : end + 1])


def read_json(path: Path) -> dict[str, object]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, value: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2), encoding="utf-8", newline="\n")


def require_file(path: Path, label: str) -> None:
    if not path.is_file():
        raise FileNotFoundError(f"{label} not found: {path}")


def build_demo(args: argparse.Namespace) -> dict[str, object]:
    repo_root = Path(args.repo_root).resolve()
    tool_root = Path(args.tool_root).resolve() if args.tool_root else tool_root_from_repo(repo_root)
    romfs = Path(args.romfs).resolve()
    work_root = Path(args.work_root).resolve()
    demo_root = work_root / "standalone_demo" / DEFAULT_SCENE_ID
    scene_dir = romfs / "scene"
    room_zsi = scene_dir / "link_0_info.zsi"
    scene_zsi = scene_dir / "link_info.zsi"
    character_manifest = work_root / "character_conversion" / "link_child_character_conversion_manifest.json"
    selection_manifest = repo_root / "tools" / "oot3d" / "oot3d_asset_tool" / "profiles" / "link_child_static_base_selection.json"

    require_file(room_zsi, "OOT3D Link house room ZSI")
    require_file(scene_zsi, "OOT3D Link house scene ZSI")
    require_file(character_manifest, "Link child character conversion manifest")
    require_file(selection_manifest, "Link child primitive selection manifest")
    require_file(tool_root / "src" / "oot3d_asset_tool" / "cli.py", "OOT3D asset tool")

    room_dir = demo_root / "room"
    collision_dir = demo_root / "collision"
    character_dir = demo_root / "character"
    preview_dir = demo_root / "preview"
    simulation_dir = demo_root / "simulation"
    viewer_dir = demo_root / "viewer"
    native_host_dir = demo_root / "native_host"
    contract_dir = demo_root / "contracts"
    for directory in (
        room_dir,
        collision_dir,
        character_dir,
        preview_dir,
        simulation_dir,
        viewer_dir,
        native_host_dir,
        contract_dir,
    ):
        directory.mkdir(parents=True, exist_ok=True)

    room_glb = room_dir / "link_house_room.glb"
    room_glb_manifest = room_dir / "link_house_room.manifest.json"
    room_summary = parse_tool_json(
        run_tool(
            tool_root,
            [
                "export-static-base-glb",
                str(room_zsi),
                "--output",
                str(room_glb),
                "--manifest-output",
                str(room_glb_manifest),
                "--texture-orientation",
                "flip-y",
                "--uv-orientation",
                "normal",
            ],
        )
    )

    collision_xml = collision_dir / "collision"
    collision_summary = parse_tool_json(
        run_tool(
            tool_root,
            [
                "export-zsi-collision",
                str(scene_zsi),
                "--output",
                str(collision_dir),
                "--resource-path",
                "standalone/oot3d_demo/link_house/collision",
                "--format",
                "xml",
                "--visual-room-dir",
                str(scene_dir),
                "--visual-scene",
                "link",
            ],
        )
    )
    collision_export_summary = collision_dir / "collision_export_summary.json"
    write_json(collision_export_summary, collision_summary)

    link_runtime_glb = character_dir / "link_child_run.glb"
    link_runtime_audit = character_dir / "link_child_run.audit.json"
    link_validated_glb = character_dir / "link_child_run.validated.glb"
    link_validated_manifest = character_dir / "link_child_run.validated.manifest.json"
    link_summary = parse_tool_json(
        run_tool(
            tool_root,
            [
                "audit-minimal-runtime-player-export",
                str(character_manifest),
                "--output",
                str(link_runtime_audit),
                "--runtime-glb-output",
                str(link_runtime_glb),
                "--validated-glb-output",
                str(link_validated_glb),
                "--validated-manifest-output",
                str(link_validated_manifest),
                "--selection-manifest",
                str(selection_manifest),
                "--csab-name",
                DEFAULT_CSAB,
                "--frame-step",
                str(args.frame_step),
                "--sample-limit",
                "50",
            ],
        )
    )

    collision_stats = collision_summary.get("stats", {})
    assert isinstance(collision_stats, dict)
    spawn = extract_spawn(collision_stats)
    manifest = {
        "format": DEMO_FORMAT,
        "status": "built",
        "demo_id": DEFAULT_SCENE_ID,
        "artifact_class": "standalone_data_poc",
        "claim_scope": [
            "oot3d_native_asset_extraction",
            "offline_derived_link_child_animation_preview",
            "native_collision_floor_query_diagnostic",
            "simple_camera_render_harness",
        ],
        "non_claims": [
            "native_oot3d_player_movement_parity",
            "dedicated_shipwright_runtime/three_ds_recomp_runtime_host",
            "oot3d_reference_trace_match",
        ],
        "repo_root": str(repo_root),
        "work_root": str(work_root),
        "demo_root": str(demo_root),
        "policy": {
            "runtime_asset_policy": "oot3d_native_or_offline_derived_only",
            "n64_usage_policy": "behavior_baseline_and_delta_reference_only",
            "no_shipwright_runtime_replacement": True,
            "no_n64_runtime_asset_substitution": True,
            "generated_assets_committable": False,
        },
        "sources": {
            "room_visual": {
                "kind": "oot3d_room_zsi_embedded_cmb",
                "path": str(room_zsi),
            },
            "collision": {
                "kind": "oot3d_scene_zsi_native_collision",
                "path": str(scene_zsi),
            },
            "link_child": {
                "kind": "oot3d_offline_character_conversion_manifest",
                "path": str(character_manifest),
                "cmb_name": "child/model/childlink_v2.cmb",
                "csab_name": DEFAULT_CSAB,
                "selection_manifest": str(selection_manifest),
            },
        },
        "assets": {
            "viewer_script": str(repo_root / "tools" / "oot3d" / "standalone_demo" / "oot3d_viewer.py"),
            "room_glb": str(room_glb),
            "room_glb_manifest": str(room_glb_manifest),
            "collision_xml": str(collision_xml),
            "collision_export_summary": str(collision_export_summary),
            "link_child_runtime_glb": str(link_runtime_glb),
            "link_child_runtime_audit": str(link_runtime_audit),
            "link_child_validated_glb": str(link_validated_glb),
            "link_child_validated_manifest": str(link_validated_manifest),
            "preview_ppm": str(preview_dir / "demo_preview.ppm"),
            "preview_png": str(preview_dir / "demo_preview.png"),
            "movement_trace": str(simulation_dir / "movement_trace.json"),
            "viewer_self_test": str(viewer_dir / "viewer_self_test.json"),
            "native_demo_readiness": str(demo_root / "native_demo_readiness.json"),
            "native_resource_contract": str(contract_dir / "native_resource_contract.json"),
            "native_host_probe": str(native_host_dir / "native_host_probe.json"),
            "native_host_trace": str(native_host_dir / "native_host_trace.json"),
            "native_host_trace_compare": str(native_host_dir / "native_host_trace_compare.json"),
            "native_host_preview_png": str(native_host_dir / "native_host_preview.png"),
            "runtime/three_ds_recomp_native_resource_probe": str(native_host_dir / "runtime/three_ds_recomp_native_resource_probe.json"),
            "runtime/three_ds_recomp_native_runtime_probe": str(native_host_dir / "runtime/three_ds_recomp_native_runtime_probe.json"),
            "runtime/three_ds_recomp_native_runtime_trace": str(native_host_dir / "runtime/three_ds_recomp_native_runtime_trace.json"),
            "runtime/three_ds_recomp_native_runtime_preview_ppm": str(native_host_dir / "runtime/three_ds_recomp_native_runtime_preview.ppm"),
            "oot3d_reference_trace": str(native_host_dir / "oot3d_reference_trace.json"),
        },
        "counts": {
            "room_mesh_count": room_summary.get("mesh_count"),
            "room_primitive_count": room_summary.get("primitive_count"),
            "room_triangle_count": room_summary.get("triangle_count"),
            "room_issue_count": room_summary.get("issue_count"),
            "collision_vertex_count": collision_stats.get("vertex_count"),
            "collision_polygon_count": collision_stats.get("polygon_count"),
            "collision_surface_type_count": collision_stats.get("surface_type_count"),
            "collision_water_box_count": collision_stats.get("water_box_count"),
            "link_runtime_primitive_count": link_summary.get("runtime_primitive_count"),
            "link_matched_frame_count": link_summary.get("matched_frame_count"),
            "link_issue_count": link_summary.get("issue_count"),
        },
        "movement": {
            "controller": "demo_scripted_wasd",
            "model": "diagnostic_constant_velocity_floor_query",
            "claim": "poc_only_not_oot3d_native_movement",
            "speed_units_per_second": 45.0,
            "radius_units": 12.0,
            "height_units": 56.0,
            "spawn": spawn,
            "animation_source": DEFAULT_CSAB,
        },
        "camera": {
            "kind": "simple_follow_demo_camera",
            "offset": [0.0, 72.0, 140.0],
            "look_at_offset": [0.0, 32.0, 0.0],
            "fov_degrees": 55.0,
        },
        "commands": {
            "verify": f"{Path(__file__).name} verify --manifest {demo_root / 'demo_manifest.json'}",
            "simulate": f"{Path(__file__).name} simulate --manifest {demo_root / 'demo_manifest.json'}",
            "render_preview": f"{Path(__file__).name} render-preview --manifest {demo_root / 'demo_manifest.json'}",
            "resource_contract": f"{Path(__file__).name} resource-contract --manifest {demo_root / 'demo_manifest.json'}",
            "native_readiness": f"{Path(__file__).name} native-readiness --manifest {demo_root / 'demo_manifest.json'}",
            "native_host_probe": f"python {repo_root / 'tools' / 'oot3d' / 'native_demo_host' / 'oot3d_native_host.py'} --manifest {demo_root / 'demo_manifest.json'}",
            "trace_compare": f"python {repo_root / 'tools' / 'oot3d' / 'native_demo_host' / 'oot3d_trace_compare.py'} --candidate {native_host_dir / 'native_host_trace.json'} --reference {native_host_dir / 'oot3d_reference_trace.json'} --output {native_host_dir / 'native_host_trace_compare.json'}",
            "run_viewer": f"python {repo_root / 'tools' / 'oot3d' / 'standalone_demo' / 'oot3d_viewer.py'} --manifest {demo_root / 'demo_manifest.json'}",
        },
    }
    manifest_path = demo_root / "demo_manifest.json"
    write_json(manifest_path, manifest)
    return manifest


def extract_spawn(collision_stats: dict[str, object]) -> dict[str, float]:
    audit = collision_stats.get("source_spawn_floor_probe_audit")
    if isinstance(audit, dict):
        sample = audit.get("probes_sample")
        if isinstance(sample, list) and sample:
            first = sample[0]
            if isinstance(first, dict):
                return {
                    "x": float(first.get("x", 0.0)),
                    "y": float(first.get("y", 0.0)),
                    "z": float(first.get("z", 0.0)),
                }
    return {"x": 0.0, "y": 0.0, "z": 0.0}


def verify_demo(args: argparse.Namespace) -> dict[str, object]:
    manifest_path = Path(args.manifest)
    manifest = read_json(manifest_path)
    issues: list[str] = []
    if manifest.get("format") != DEMO_FORMAT:
        issues.append("manifest_format_mismatch")
    policy = manifest.get("policy")
    if not isinstance(policy, dict):
        issues.append("missing_policy")
    else:
        if policy.get("no_shipwright_runtime_replacement") is not True:
            issues.append("shipwright_runtime_replacement_not_forbidden")
        if policy.get("no_n64_runtime_asset_substitution") is not True:
            issues.append("n64_runtime_asset_substitution_not_forbidden")
        if policy.get("runtime_asset_policy") != "oot3d_native_or_offline_derived_only":
            issues.append("runtime_asset_policy_mismatch")

    assets = manifest.get("assets")
    if not isinstance(assets, dict):
        issues.append("missing_assets")
        assets = {}
    for key in (
        "viewer_script",
        "room_glb",
        "room_glb_manifest",
        "collision_xml",
        "collision_export_summary",
        "link_child_runtime_glb",
        "link_child_runtime_audit",
        "movement_trace",
        "preview_png",
        "viewer_self_test",
        "native_demo_readiness",
        "native_resource_contract",
    ):
        path = Path(str(assets.get(key, "")))
        if not path.is_file():
            issues.append(f"missing_asset:{key}")

    if Path(str(assets.get("room_glb", ""))).is_file() and read_glb_header(Path(str(assets["room_glb"]))) is None:
        issues.append("room_glb_header_invalid")
    if Path(str(assets.get("link_child_runtime_glb", ""))).is_file() and read_glb_header(Path(str(assets["link_child_runtime_glb"]))) is None:
        issues.append("link_child_glb_header_invalid")

    room_manifest_path = Path(str(assets.get("room_glb_manifest", "")))
    if room_manifest_path.is_file():
        room_manifest = read_json(room_manifest_path)
        counts = room_manifest.get("counts", {})
        if isinstance(counts, dict) and int(counts.get("issue_count", 1)) != 0:
            issues.append("room_glb_manifest_has_issues")

    collision_summary_path = Path(str(assets.get("collision_export_summary", "")))
    if collision_summary_path.is_file():
        collision_summary = read_json(collision_summary_path)
        stats = collision_summary.get("stats", {})
        if not isinstance(stats, dict):
            issues.append("collision_summary_missing_stats")
        else:
            if stats.get("resource_format") != "xml":
                issues.append("collision_not_xml_for_demo_gate")
            if "oot3d_zsi_native_collision" not in json.dumps(stats.get("visual_diagnostic_policy", {})):
                issues.append("collision_policy_not_native_zsi")
            if int(stats.get("vertex_count", 0)) <= 0 or int(stats.get("polygon_count", 0)) <= 0:
                issues.append("collision_empty")

    link_audit_path = Path(str(assets.get("link_child_runtime_audit", "")))
    if link_audit_path.is_file():
        link_audit = read_json(link_audit_path)
        if link_audit.get("status") != "valid":
            issues.append("link_child_runtime_audit_not_valid")
        comparison = link_audit.get("comparison", {})
        if isinstance(comparison, dict) and int(comparison.get("issue_count", 1)) != 0:
            issues.append("link_child_runtime_comparison_has_issues")

    viewer_self_test_path = Path(str(assets.get("viewer_self_test", "")))
    if viewer_self_test_path.is_file():
        viewer_self_test = read_json(viewer_self_test_path)
        if viewer_self_test.get("status") != "valid":
            issues.append("viewer_self_test_not_valid")
        if int(viewer_self_test.get("issue_count", 1)) != 0:
            issues.append("viewer_self_test_has_issues")

    movement_trace_path = Path(str(assets.get("movement_trace", "")))
    if movement_trace_path.is_file():
        movement_trace = read_json(movement_trace_path)
        if movement_trace.get("format") != SIM_FORMAT:
            issues.append("movement_trace_format_mismatch")
        if movement_trace.get("status") != "valid":
            issues.append("movement_trace_not_valid")
        if movement_trace.get("trace_schema") != "native_demo_comparison_seed_v1":
            issues.append("movement_trace_schema_mismatch")
        sample = movement_trace.get("frames_sample", [])
        if not isinstance(sample, list) or not sample:
            issues.append("movement_trace_sample_empty")
        else:
            required_frame_keys = {
                "frame",
                "dt",
                "input",
                "position",
                "rotation",
                "velocity",
                "action_state",
                "animation",
                "floor",
                "collision_flags",
                "camera",
            }
            first = sample[0]
            if not isinstance(first, dict) or not required_frame_keys.issubset(first.keys()):
                issues.append("movement_trace_frame_schema_incomplete")

    readiness_path = Path(str(assets.get("native_demo_readiness", "")))
    if readiness_path.is_file():
        readiness = read_json(readiness_path)
        if readiness.get("format") != READINESS_FORMAT:
            issues.append("native_demo_readiness_format_mismatch")
        if readiness.get("status") not in {"incomplete", "ready"}:
            issues.append("native_demo_readiness_status_invalid")

    resource_contract_path = Path(str(assets.get("native_resource_contract", "")))
    if resource_contract_path.is_file():
        resource_contract = read_json(resource_contract_path)
        if resource_contract.get("format") != RESOURCE_CONTRACT_FORMAT:
            issues.append("native_resource_contract_format_mismatch")
        if resource_contract.get("status") != "valid":
            issues.append("native_resource_contract_not_valid")
        if resource_contract.get("runtime_n64_asset_substitution_allowed") is not False:
            issues.append("native_resource_contract_allows_n64_substitution")
        if int(resource_contract.get("resource_count", 0)) < 3:
            issues.append("native_resource_contract_resource_count_too_small")

    summary = {
        "format": VERIFY_FORMAT,
        "status": "valid" if not issues else "invalid",
        "manifest": str(manifest_path),
        "issue_count": len(issues),
        "issues": issues,
        "checked_policy": True,
        "checked_assets": True,
    }
    output = Path(args.output) if args.output else manifest_path.with_name("demo_verify_summary.json")
    write_json(output, summary)
    return summary


def run_viewer_self_test(manifest_path: Path) -> dict[str, object]:
    manifest = read_json(manifest_path)
    assets = manifest.get("assets", {})
    if not isinstance(assets, dict):
        raise RuntimeError(f"{manifest_path}: manifest assets block missing")
    viewer_script = Path(str(assets.get("viewer_script", "")))
    output = Path(str(assets.get("viewer_self_test", manifest_path.with_name("viewer_self_test.json"))))
    if not viewer_script.is_file():
        raise FileNotFoundError(f"viewer script not found: {viewer_script}")
    proc = subprocess.run(
        [
            sys.executable,
            str(viewer_script),
            "--manifest",
            str(manifest_path),
            "--self-test",
            "--output",
            str(output),
        ],
        cwd=viewer_script.parent,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if proc.returncode != 0:
        raise RuntimeError(f"viewer self-test failed with exit code {proc.returncode}\n{proc.stdout}")
    return read_json(output)


def read_glb_header(path: Path) -> tuple[int, int] | None:
    data = path.read_bytes()[:12]
    if len(data) != 12:
        return None
    magic, version, length = struct.unpack_from("<III", data)
    if magic != 0x46546C67 or version != 2 or length <= 12:
        return None
    return version, length


def parse_collision_xml(path: Path) -> CollisionScene:
    root = ET.parse(path).getroot()
    vertices = [
        (float(v.attrib["X"]), float(v.attrib["Y"]), float(v.attrib["Z"]))
        for v in root.findall("Vertex")
    ]
    polygons = [
        CollisionPoly(
            type_index=int(p.attrib["Type"]),
            a=int(p.attrib["VertexA"]) & 0x1FFF,
            b=int(p.attrib["VertexB"]) & 0x1FFF,
            c=int(p.attrib["VertexC"]) & 0x1FFF,
            nx=int(p.attrib["NormalX"]),
            ny=int(p.attrib["NormalY"]),
            nz=int(p.attrib["NormalZ"]),
            dist=int(p.attrib["Dist"]),
        )
        for p in root.findall("Polygon")
    ]
    return CollisionScene(
        vertices=vertices,
        polygons=polygons,
        bounds_min=(
            float(root.attrib["MinBoundsX"]),
            float(root.attrib["MinBoundsY"]),
            float(root.attrib["MinBoundsZ"]),
        ),
        bounds_max=(
            float(root.attrib["MaxBoundsX"]),
            float(root.attrib["MaxBoundsY"]),
            float(root.attrib["MaxBoundsZ"]),
        ),
    )


def point_in_tri_xz(
    x: float,
    z: float,
    a: tuple[float, float, float],
    b: tuple[float, float, float],
    c: tuple[float, float, float],
) -> bool:
    ax, az = a[0], a[2]
    bx, bz = b[0], b[2]
    cx, cz = c[0], c[2]
    v0x, v0z = cx - ax, cz - az
    v1x, v1z = bx - ax, bz - az
    v2x, v2z = x - ax, z - az
    dot00 = v0x * v0x + v0z * v0z
    dot01 = v0x * v1x + v0z * v1z
    dot02 = v0x * v2x + v0z * v2z
    dot11 = v1x * v1x + v1z * v1z
    dot12 = v1x * v2x + v1z * v2z
    denom = dot00 * dot11 - dot01 * dot01
    if abs(denom) < 1e-8:
        return False
    inv = 1.0 / denom
    u = (dot11 * dot02 - dot01 * dot12) * inv
    v = (dot00 * dot12 - dot01 * dot02) * inv
    return u >= -1e-5 and v >= -1e-5 and (u + v) <= 1.00001


def floor_y_at(scene: CollisionScene, x: float, z: float, query_y: float) -> float | None:
    hit = floor_hit_at(scene, x, z, query_y)
    return hit.y if hit is not None else None


def floor_hit_at(scene: CollisionScene, x: float, z: float, query_y: float) -> FloorHit | None:
    best: float | None = None
    best_hit: FloorHit | None = None
    best_index = -1
    for poly in scene.polygons:
        best_index += 1
        if poly.ny < 12000:
            continue
        a = scene.vertices[poly.a]
        b = scene.vertices[poly.b]
        c = scene.vertices[poly.c]
        if not point_in_tri_xz(x, z, a, b, c):
            continue
        nx = poly.nx / 32767.0
        ny = poly.ny / 32767.0
        nz = poly.nz / 32767.0
        if abs(ny) < 1e-5:
            continue
        y = -(nx * x + nz * z + float(poly.dist)) / ny
        if y <= query_y + 96.0 and (best is None or y > best):
            best = y
            best_hit = FloorHit(
                y=y,
                polygon_index=best_index,
                type_index=poly.type_index,
                normal=(nx, ny, nz),
                dist=poly.dist,
            )
    return best_hit


def simulate_demo(args: argparse.Namespace) -> dict[str, object]:
    manifest_path = Path(args.manifest)
    manifest = read_json(manifest_path)
    assets = manifest["assets"]
    assert isinstance(assets, dict)
    scene = parse_collision_xml(Path(str(assets["collision_xml"])))
    movement = manifest.get("movement", {})
    camera = manifest.get("camera", {})
    assert isinstance(movement, dict)
    assert isinstance(camera, dict)
    spawn = movement.get("spawn", {"x": 0.0, "y": 0.0, "z": 0.0})
    assert isinstance(spawn, dict)
    speed = float(movement.get("speed_units_per_second", 45.0))
    radius = float(movement.get("radius_units", 12.0))
    animation_source = str(movement.get("animation_source", DEFAULT_CSAB))
    animation_frame_count = max(int(manifest.get("counts", {}).get("link_matched_frame_count", 1)), 1) if isinstance(manifest.get("counts"), dict) else 1
    x = float(spawn.get("x", 0.0))
    z = float(spawn.get("z", 0.0))
    floor_hit = floor_hit_at(scene, x, z, float(spawn.get("y", 0.0)) + 120.0)
    if floor_hit is None:
        y = float(spawn.get("y", 0.0))
        floor_index = -1
        floor_type = -1
        floor_normal = [0.0, 1.0, 0.0]
    else:
        y = floor_hit.y
        floor_index = floor_hit.polygon_index
        floor_type = floor_hit.type_index
        floor_normal = [round(v, 6) for v in floor_hit.normal]

    dt = 1.0 / 30.0
    script = [(1.0, 0.0, 45), (0.0, -1.0, 45), (-1.0, 0.0, 45), (0.0, 1.0, 45)]
    frames: list[dict[str, object]] = []
    floor_hits = 0
    blocked_steps = 0
    frame_index = 0
    min_x = max_x = x
    min_z = max_z = z
    cam_offset = camera.get("offset", [0.0, 72.0, 140.0])
    if not isinstance(cam_offset, list) or len(cam_offset) != 3:
        cam_offset = [0.0, 72.0, 140.0]

    for dx, dz, count in script:
        length = math.hypot(dx, dz)
        ndx, ndz = (dx / length, dz / length) if length > 0.0 else (0.0, 0.0)
        for _ in range(count):
            next_x = x + ndx * speed * dt
            next_z = z + ndz * speed * dt
            unclamped_x = next_x
            unclamped_z = next_z
            next_x = max(scene.bounds_min[0] + radius, min(scene.bounds_max[0] - radius, next_x))
            next_z = max(scene.bounds_min[2] + radius, min(scene.bounds_max[2] - radius, next_z))
            bounds_clamped = next_x != unclamped_x or next_z != unclamped_z
            next_floor_hit = floor_hit_at(scene, next_x, next_z, y + 120.0)
            vx = 0.0
            vy = 0.0
            vz = 0.0
            blocked = next_floor_hit is None
            if blocked:
                blocked_steps += 1
            else:
                old_x, old_y, old_z = x, y, z
                x, y, z = next_x, next_floor_hit.y, next_z
                vx = (x - old_x) / dt
                vy = (y - old_y) / dt
                vz = (z - old_z) / dt
                floor_index = next_floor_hit.polygon_index
                floor_type = next_floor_hit.type_index
                floor_normal = [round(v, 6) for v in next_floor_hit.normal]
                floor_hits += 1
            min_x, max_x = min(min_x, x), max(max_x, x)
            min_z, max_z = min(min_z, z), max(max_z, z)
            yaw_degrees = math.degrees(math.atan2(ndx, ndz)) if length > 0.0 else 0.0
            camera_position = [
                round(x + float(cam_offset[0]), 4),
                round(y + float(cam_offset[1]), 4),
                round(z + float(cam_offset[2]), 4),
            ]
            camera_target = [round(x, 4), round(y + 32.0, 4), round(z, 4)]
            frames.append(
                {
                    "frame": frame_index,
                    "dt": dt,
                    "input": {
                        "stick_x": round(ndx, 4),
                        "stick_z": round(ndz, 4),
                        "buttons": [],
                        "source": "scripted_diagnostic_macro",
                    },
                    "position": [round(x, 4), round(y, 4), round(z, 4)],
                    "rotation": {"yaw_degrees": round(yaw_degrees, 4)},
                    "velocity": [round(vx, 4), round(vy, 4), round(vz, 4)],
                    "action_state": "demo_walk" if not blocked else "demo_blocked",
                    "animation": {
                        "source": animation_source,
                        "frame": frame_index % animation_frame_count,
                        "model": "offline_csab_morph_preview",
                    },
                    "floor": {
                        "hit": not blocked,
                        "polygon_index": floor_index if not blocked else -1,
                        "surface_type": floor_type if not blocked else -1,
                        "normal": floor_normal if not blocked else None,
                    },
                    "collision_flags": {
                        "floor_hit": not blocked,
                        "bounds_clamped": bounds_clamped,
                        "blocked": blocked,
                    },
                    "camera": {
                        "kind": str(camera.get("kind", "simple_follow_demo_camera")),
                        "position": camera_position,
                        "target": camera_target,
                    },
                }
            )
            frame_index += 1

    trace = {
        "format": SIM_FORMAT,
        "status": "valid" if floor_hits > 0 and blocked_steps < len(frames) else "invalid",
        "trace_schema": "native_demo_comparison_seed_v1",
        "claim": "diagnostic_seed_trace_not_oot3d_parity",
        "manifest": str(manifest_path),
        "frame_count": len(frames),
        "floor_hit_count": floor_hits,
        "blocked_step_count": blocked_steps,
        "bounds_min": list(scene.bounds_min),
        "bounds_max": list(scene.bounds_max),
        "path_bounds": {
            "min_x": round(min_x, 4),
            "max_x": round(max_x, 4),
            "min_z": round(min_z, 4),
            "max_z": round(max_z, 4),
        },
        "frames_sample": frames[:20] + frames[-5:],
    }
    output = Path(args.output) if args.output else Path(str(assets["movement_trace"]))
    write_json(output, trace)
    return trace


def build_resource_contract(args: argparse.Namespace) -> dict[str, object]:
    manifest_path = Path(args.manifest)
    manifest = read_json(manifest_path)
    assets = manifest.get("assets", {})
    sources = manifest.get("sources", {})
    counts = manifest.get("counts", {})
    policy = manifest.get("policy", {})
    if not isinstance(assets, dict):
        assets = {}
    if not isinstance(sources, dict):
        sources = {}
    if not isinstance(counts, dict):
        counts = {}
    if not isinstance(policy, dict):
        policy = {}

    def source_path(name: str) -> str:
        source = sources.get(name, {})
        return str(source.get("path", "")) if isinstance(source, dict) else ""

    link_source = sources.get("link_child", {})
    link_source_dict = link_source if isinstance(link_source, dict) else {}
    resources = [
        {
            "id": "link_house_room_visual",
            "role": "room_visual_mesh",
            "source_format": "oot3d_zsi_embedded_cmb",
            "source_path": source_path("room_visual"),
            "runtime_artifact": str(assets.get("room_glb", "")),
            "runtime_format": "offline_glb_cache_from_oot3d_cmb",
            "future_loader_contract": "oot3d.scene.zsi_room_embedded_cmb_to_static_mesh",
            "native_or_directly_derived": True,
            "runtime_n64_asset_path": "",
        },
        {
            "id": "link_house_collision",
            "role": "scene_collision",
            "source_format": "oot3d_zsi_native_collision",
            "source_path": source_path("collision"),
            "runtime_artifact": str(assets.get("collision_xml", "")),
            "runtime_format": "offline_xml_cache_from_oot3d_zsi_collision",
            "future_loader_contract": "oot3d.scene.zsi_collision_to_collision_world",
            "native_or_directly_derived": True,
            "runtime_n64_asset_path": "",
        },
        {
            "id": "link_child_model_animation",
            "role": "player_model_and_animation",
            "source_format": "oot3d_cmb_plus_csab",
            "source_path": str(link_source_dict.get("path", "")),
            "source_cmb": str(link_source_dict.get("cmb_name", "")),
            "source_csab": str(link_source_dict.get("csab_name", "")),
            "runtime_artifact": str(assets.get("link_child_runtime_glb", "")),
            "runtime_format": "offline_glb_cache_from_oot3d_cmb_csab",
            "future_loader_contract": "oot3d.character.cmb_csab_to_skinned_player",
            "native_or_directly_derived": True,
            "runtime_n64_asset_path": "",
        },
        {
            "id": "link_child_validation_manifest",
            "role": "player_asset_validation",
            "source_format": "oot3d_character_conversion_manifest",
            "source_path": str(link_source_dict.get("path", "")),
            "runtime_artifact": str(assets.get("link_child_validated_manifest", "")),
            "runtime_format": "offline_validation_manifest",
            "future_loader_contract": "oot3d.character.validation_metadata",
            "native_or_directly_derived": True,
            "runtime_n64_asset_path": "",
        },
    ]

    issues: list[str] = []
    if policy.get("no_n64_runtime_asset_substitution") is not True:
        issues.append("policy_allows_n64_runtime_asset_substitution")
    if policy.get("no_shipwright_runtime_replacement") is not True:
        issues.append("policy_allows_shipwright_runtime_replacement")
    for resource in resources:
        if not resource["native_or_directly_derived"]:
            issues.append(f"resource_not_native_or_derived:{resource['id']}")
        if resource["runtime_n64_asset_path"]:
            issues.append(f"resource_declares_n64_runtime_path:{resource['id']}")
        source = Path(str(resource.get("source_path", "")))
        artifact = Path(str(resource.get("runtime_artifact", "")))
        if not source.is_file():
            issues.append(f"resource_source_missing:{resource['id']}")
        if not artifact.is_file():
            issues.append(f"resource_artifact_missing:{resource['id']}")
        if not str(resource.get("future_loader_contract", "")).startswith("oot3d."):
            issues.append(f"resource_loader_contract_not_oot3d:{resource['id']}")
    role_counts = {
        role: sum(1 for resource in resources if resource.get("role") == role)
        for role in NATIVE_DEMO_REQUIRED_ROLES
    }
    for role, count in role_counts.items():
        if count == 0:
            issues.append(f"native_demo_required_role_missing:{role}")
        elif count > 1:
            issues.append(f"native_demo_required_role_ambiguous:{role}")

    contract = {
        "format": RESOURCE_CONTRACT_FORMAT,
        "status": "valid" if not issues else "invalid",
        "manifest": str(manifest_path),
        "target_engine": "dedicated_shipwright_runtime/three_ds_recomp_fork",
        "runtime_asset_policy": "oot3d_native_or_directly_offline_derived_only",
        "n64_usage_policy": "behavior_baseline_and_delta_reference_only",
        "runtime_n64_asset_substitution_allowed": False,
        "shipwright_runtime_replacement_allowed": False,
        "resource_count": len(resources),
        "native_demo_resource_set": {
            "required_roles": NATIVE_DEMO_REQUIRED_ROLES,
            "role_counts": role_counts,
            "status": "valid" if all(count == 1 for count in role_counts.values()) else "invalid",
        },
        "resources": resources,
        "counts": {
            "room_triangle_count": counts.get("room_triangle_count"),
            "collision_polygon_count": counts.get("collision_polygon_count"),
            "link_runtime_primitive_count": counts.get("link_runtime_primitive_count"),
            "link_matched_frame_count": counts.get("link_matched_frame_count"),
        },
        "issue_count": len(issues),
        "issues": issues,
    }
    output = Path(args.output) if args.output else Path(str(assets.get("native_resource_contract", manifest_path.with_name("native_resource_contract.json"))))
    write_json(output, contract)
    return contract


def native_readiness(args: argparse.Namespace) -> dict[str, object]:
    manifest_path = Path(args.manifest)
    manifest = read_json(manifest_path)
    assets = manifest.get("assets", {})
    if not isinstance(assets, dict):
        assets = {}
    policy = manifest.get("policy", {})
    movement = manifest.get("movement", {})
    blockers: list[str] = []

    if not isinstance(policy, dict) or policy.get("no_n64_runtime_asset_substitution") is not True:
        blockers.append("native_policy_missing_no_n64_runtime_asset_substitution")
    if not isinstance(policy, dict) or policy.get("no_shipwright_runtime_replacement") is not True:
        blockers.append("native_policy_missing_no_shipwright_runtime_replacement")
    if manifest.get("artifact_class") != "standalone_data_poc":
        blockers.append("artifact_class_not_explicit_data_poc")
    if not isinstance(movement, dict) or movement.get("model") != "diagnostic_constant_velocity_floor_query":
        blockers.append("movement_model_unclassified")

    proven_now = [
        "oot3d_link_house_room_mesh_offline_export_present",
        "oot3d_link_house_native_collision_export_present",
        "oot3d_link_child_csab_derived_preview_present",
        "scripted_trace_schema_for_future_comparison_present",
    ]
    repo_root = Path(str(manifest.get("repo_root", repo_root_from_script()))).resolve()
    lus_contract_header = repo_root / "runtime/three_ds_recomp" / "include" / "ship" / "oot3d" / "Oot3dNativeResourceContract.h"
    lus_contract_source = repo_root / "runtime/three_ds_recomp" / "src" / "ship" / "oot3d" / "Oot3dNativeResourceContract.cpp"
    lus_contract_test = repo_root / "runtime/three_ds_recomp" / "tests" / "oot3d_native_resource_contract_tests.cpp"
    runtime/three_ds_recomp_seed_present = (
        lus_contract_header.is_file()
        and lus_contract_source.is_file()
        and lus_contract_test.is_file()
        and "BuildNativeDemoResourceSet" in lus_contract_header.read_text(encoding="utf-8")
        and "BuildNativeDemoResourceSet" in lus_contract_source.read_text(encoding="utf-8")
    )
    if runtime/three_ds_recomp_seed_present:
        proven_now.append("runtime/three_ds_recomp_oot3d_native_contract_validator_source_present")
        proven_now.append("runtime/three_ds_recomp_oot3d_native_demo_resource_set_source_present")
    else:
        blockers.append("runtime/three_ds_recomp_oot3d_native_contract_seed_missing")
    native_host_probe_path = Path(str(assets.get("native_host_probe", "")))
    native_host_probe: dict[str, object] | None = None
    if native_host_probe_path.is_file():
        native_host_probe = read_json(native_host_probe_path)
        if native_host_probe.get("status") == "valid" and int(native_host_probe.get("issue_count", 1)) == 0:
            proven_now.append("standalone_native_asset_host_probe_valid")
            proven_now.append("standalone_native_host_trace_and_preview_present")
            if native_host_probe.get("viewer_self_test_status") == "valid":
                proven_now.append("standalone_native_host_viewer_self_test_valid")
        else:
            blockers.append("standalone_native_asset_host_probe_invalid")
    else:
        blockers.append("standalone_native_asset_host_probe_missing")

    trace_compare_path = Path(str(assets.get("native_host_trace_compare", "")))
    trace_compare: dict[str, object] | None = None
    trace_comparison_gate_available = False
    if trace_compare_path.is_file():
        trace_compare = read_json(trace_compare_path)
        trace_comparison_gate_available = (
            trace_compare.get("format") == "oot3d_native_demo_trace_compare_v1"
            and trace_compare.get("gate_available") is True
            and trace_compare.get("status") in {"passed", "missing_reference", "failed"}
        )
        if trace_comparison_gate_available:
            proven_now.append("trace_comparison_gate_available")
            if trace_compare.get("status") == "passed":
                proven_now.append("trace_comparison_gate_passed")
        else:
            blockers.append("trace_comparison_gate_invalid")
    else:
        blockers.append("trace_comparison_gate_missing")

    resource_contract_path = Path(str(assets.get("native_resource_contract", "")))
    resource_contract: dict[str, object] | None = None
    resource_contract_valid = False
    if resource_contract_path.is_file():
        resource_contract = read_json(resource_contract_path)
        resource_contract_valid = (
            resource_contract.get("format") == RESOURCE_CONTRACT_FORMAT
            and resource_contract.get("status") == "valid"
            and int(resource_contract.get("issue_count", 1)) == 0
            and resource_contract.get("runtime_n64_asset_substitution_allowed") is False
        )
        native_demo_resource_set = resource_contract.get("native_demo_resource_set", {})
        native_demo_resource_set_valid = (
            isinstance(native_demo_resource_set, dict)
            and native_demo_resource_set.get("status") == "valid"
            and native_demo_resource_set.get("required_roles") == NATIVE_DEMO_REQUIRED_ROLES
        )
        if resource_contract_valid:
            proven_now.append("oot3d_native_resource_contract_manifest_valid")
            if native_demo_resource_set_valid:
                proven_now.append("oot3d_native_demo_resource_set_contract_valid")
            else:
                blockers.append("oot3d_native_demo_resource_set_contract_invalid")
        else:
            blockers.append("oot3d_native_resource_contract_manifest_invalid")
    else:
        blockers.append("oot3d_native_resource_contract_manifest_missing")

    runtime/three_ds_recomp_resource_probe_path = Path(str(assets.get("runtime/three_ds_recomp_native_resource_probe", "")))
    runtime/three_ds_recomp_resource_probe: dict[str, object] | None = None
    runtime/three_ds_recomp_resource_probe_valid = False
    native_source_parser_summary: dict[str, object] = {
        "room_zsi_embedded_cmb_parsed": False,
        "link_child_zar_cmb_csab_parsed": False,
        "source_triangle_count": 0,
        "source_vertex_count": 0,
        "source_bone_count": 0,
        "source_csab_frame_count": 0,
    }
    if runtime/three_ds_recomp_resource_probe_path.is_file():
        runtime/three_ds_recomp_resource_probe = read_json(runtime/three_ds_recomp_resource_probe_path)
        runtime/three_ds_recomp_resource_probe_valid = (
            runtime/three_ds_recomp_resource_probe.get("format") == "oot3d_runtime/three_ds_recomp_native_resource_probe_v1"
            and runtime/three_ds_recomp_resource_probe.get("status") == "valid"
            and int(runtime/three_ds_recomp_resource_probe.get("issue_count", 1)) == 0
            and runtime/three_ds_recomp_resource_probe.get("contract_status") == "valid"
            and runtime/three_ds_recomp_resource_probe.get("resource_set_status") == "valid"
            and runtime/three_ds_recomp_resource_probe.get("runtime_n64_asset_substitution_used") is False
            and runtime/three_ds_recomp_resource_probe.get("shipwright_replacement_path_used") is False
        )
        if runtime/three_ds_recomp_resource_probe_valid:
            proven_now.append("runtime/three_ds_recomp_oot3d_native_resource_probe_valid")
            entries = runtime/three_ds_recomp_resource_probe.get("entries", [])
            probe_entries = entries if isinstance(entries, list) else []
            by_id = {
                str(entry.get("id", "")): entry
                for entry in probe_entries
                if isinstance(entry, dict)
            }
            room_entry = by_id.get("link_house_room_visual", {})
            link_entry = by_id.get("link_child_model_animation", {})
            room_native_parsed = (
                room_entry.get("detected_source_kind") == "zsi"
                and room_entry.get("source_native_parse_succeeded") is True
                and int(room_entry.get("source_embedded_cmb_count", 0)) > 0
                and int(room_entry.get("source_triangle_count", 0)) > 0
            )
            link_native_parsed = (
                link_entry.get("detected_source_kind") == "zar_cmb_csab"
                and link_entry.get("source_native_parse_succeeded") is True
                and int(link_entry.get("source_triangle_count", 0)) > 0
                and int(link_entry.get("source_bone_count", 0)) > 0
                and int(link_entry.get("source_csab_frame_count", 0)) > 0
            )
            native_source_parser_summary = {
                "room_zsi_embedded_cmb_parsed": room_native_parsed,
                "link_child_zar_cmb_csab_parsed": link_native_parsed,
                "source_triangle_count": sum(
                    int(entry.get("source_triangle_count", 0))
                    for entry in probe_entries
                    if isinstance(entry, dict)
                ),
                "source_vertex_count": sum(
                    int(entry.get("source_vertex_count", 0))
                    for entry in probe_entries
                    if isinstance(entry, dict)
                ),
                "source_bone_count": sum(
                    int(entry.get("source_bone_count", 0))
                    for entry in probe_entries
                    if isinstance(entry, dict)
                ),
                "source_csab_frame_count": int(link_entry.get("source_csab_frame_count", 0)),
            }
            if room_native_parsed:
                proven_now.append("runtime/three_ds_recomp_oot3d_zsi_embedded_cmb_parser_valid")
            else:
                blockers.append("runtime/three_ds_recomp_oot3d_zsi_embedded_cmb_parser_missing")
            if link_native_parsed:
                proven_now.append("runtime/three_ds_recomp_oot3d_zar_cmb_csab_parser_valid")
            else:
                blockers.append("runtime/three_ds_recomp_oot3d_zar_cmb_csab_parser_missing")
        else:
            blockers.append("runtime/three_ds_recomp_oot3d_native_resource_probe_invalid")
    else:
        blockers.append("runtime/three_ds_recomp_oot3d_native_resource_probe_missing")

    runtime/three_ds_recomp_runtime_probe_path = Path(str(assets.get("runtime/three_ds_recomp_native_runtime_probe", "")))
    runtime/three_ds_recomp_runtime_probe: dict[str, object] | None = None
    runtime/three_ds_recomp_runtime_probe_valid = False
    if runtime/three_ds_recomp_runtime_probe_path.is_file():
        runtime/three_ds_recomp_runtime_probe = read_json(runtime/three_ds_recomp_runtime_probe_path)
        trace_path = Path(str(runtime/three_ds_recomp_runtime_probe.get("trace", "")))
        preview_path = Path(str(runtime/three_ds_recomp_runtime_probe.get("preview_ppm", "")))
        runtime/three_ds_recomp_runtime_probe_valid = (
            runtime/three_ds_recomp_runtime_probe.get("format") == "oot3d_runtime/three_ds_recomp_native_runtime_probe_v1"
            and runtime/three_ds_recomp_runtime_probe.get("status") == "valid"
            and int(runtime/three_ds_recomp_runtime_probe.get("issue_count", 1)) == 0
            and runtime/three_ds_recomp_runtime_probe.get("trace_status") == "valid"
            and int(runtime/three_ds_recomp_runtime_probe.get("trace_frame_count", 0)) > 0
            and int(runtime/three_ds_recomp_runtime_probe.get("floor_hit_count", 0)) > 0
            and runtime/three_ds_recomp_runtime_probe.get("preview_status") == "valid"
            and trace_path.is_file()
            and preview_path.is_file()
            and runtime/three_ds_recomp_runtime_probe.get("runtime_n64_asset_substitution_used") is False
            and runtime/three_ds_recomp_runtime_probe.get("shipwright_replacement_path_used") is False
        )
        if runtime/three_ds_recomp_runtime_probe_valid:
            proven_now.append("runtime/three_ds_recomp_oot3d_cpp_native_runtime_probe_valid")
            proven_now.append("runtime/three_ds_recomp_oot3d_cpp_trace_and_preview_present")
        else:
            blockers.append("runtime/three_ds_recomp_oot3d_cpp_native_runtime_probe_invalid")
    else:
        blockers.append("runtime/three_ds_recomp_oot3d_cpp_native_runtime_probe_missing")

    reference_comparison_passed = (
        trace_compare is not None
        and trace_compare.get("status") == "passed"
        and trace_compare.get("parity_passed") is True
        and trace_compare.get("missing_reference") is False
    )
    final_blockers = [
        "dedicated_runtime/three_ds_recomp_engine_host_missing",
        "engine_hosted_player_update_path_missing",
    ]
    if resource_contract_valid and runtime/three_ds_recomp_resource_probe_valid:
        final_blockers.append("runtime/three_ds_recomp_oot3d_format_loader_implementation_missing")
    elif resource_contract_valid:
        final_blockers.append("runtime/three_ds_recomp_native_resource_file_probe_missing")
    else:
        final_blockers.append("runtime/three_ds_recomp_native_resource_loader_contracts_missing")
    if not reference_comparison_passed:
        if trace_compare is not None and trace_compare.get("status") == "failed":
            final_blockers.append("trace_comparison_gate_failed")
        else:
            final_blockers.append("oot3d_reference_trace_missing")
        final_blockers.append("cannot_claim_native_oot3d_movement_parity")
    if not trace_comparison_gate_available:
        final_blockers.append("trace_comparison_gate_unavailable")
    remaining_for_native_demo = [
        "dedicated_shipwright_runtime/three_ds_recomp_native_demo_launch_path",
        "engine_hosted_player_update_path",
        "oot3d_reference_input_trace",
        "movement_collision_animation_parity_gate",
    ]
    if runtime/three_ds_recomp_runtime_probe_valid:
        remaining_for_native_demo.insert(1, "promote_cpp_runtime_probe_to_runtime/three_ds_recomp_window_host")
    else:
        remaining_for_native_demo.insert(1, "runtime/three_ds_recomp_oot3d_cpp_native_runtime_probe")
    if resource_contract_valid and runtime/three_ds_recomp_resource_probe_valid:
        remaining_for_native_demo.insert(1, "runtime/three_ds_recomp_oot3d_format_loader_implementation")
    elif resource_contract_valid:
        remaining_for_native_demo.insert(1, "runtime/three_ds_recomp_oot3d_native_resource_probe")
    else:
        remaining_for_native_demo.insert(1, "runtime/three_ds_recomp_oot3d_native_resource_loader_contracts")
    if not trace_comparison_gate_available:
        remaining_for_native_demo.append("trace_comparison_thresholds")
    readiness = {
        "format": READINESS_FORMAT,
        "status": "incomplete",
        "native_demo_ready": False,
        "manifest": str(manifest_path),
        "policy": {
            "target_engine": "dedicated_shipwright_runtime/three_ds_recomp_fork",
            "runtime_asset_policy": "oot3d_native_or_directly_offline_derived_only",
            "n64_usage_policy": "behavior_baseline_trace_and_delta_reference_only",
            "runtime_n64_asset_substitution_allowed": False,
        },
        "proven_now": proven_now,
        "native_host_probe": {
            "path": str(native_host_probe_path) if native_host_probe_path != Path(".") else "",
            "status": native_host_probe.get("status") if native_host_probe is not None else "missing",
            "host_class": native_host_probe.get("host_class") if native_host_probe is not None else None,
            "current_host_runtime": native_host_probe.get("current_host_runtime") if native_host_probe is not None else None,
        },
        "trace_comparison": {
            "path": str(trace_compare_path) if trace_compare_path != Path(".") else "",
            "status": trace_compare.get("status") if trace_compare is not None else "missing",
            "gate_available": trace_comparison_gate_available,
            "parity_passed": trace_compare.get("parity_passed") if trace_compare is not None else False,
        },
        "native_resource_contract": {
            "path": str(resource_contract_path) if resource_contract_path != Path(".") else "",
            "status": resource_contract.get("status") if resource_contract is not None else "missing",
            "resource_count": resource_contract.get("resource_count") if resource_contract is not None else 0,
        },
        "runtime/three_ds_recomp_native_resource_probe": {
            "path": str(runtime/three_ds_recomp_resource_probe_path) if runtime/three_ds_recomp_resource_probe_path != Path(".") else "",
            "status": runtime/three_ds_recomp_resource_probe.get("status") if runtime/three_ds_recomp_resource_probe is not None else "missing",
            "entry_count": runtime/three_ds_recomp_resource_probe.get("entry_count") if runtime/three_ds_recomp_resource_probe is not None else 0,
            "probe_runtime": runtime/three_ds_recomp_resource_probe.get("probe_runtime") if runtime/three_ds_recomp_resource_probe is not None else None,
            "native_source_parser": native_source_parser_summary,
        },
        "runtime/three_ds_recomp_native_runtime_probe": {
            "path": str(runtime/three_ds_recomp_runtime_probe_path) if runtime/three_ds_recomp_runtime_probe_path != Path(".") else "",
            "status": runtime/three_ds_recomp_runtime_probe.get("status") if runtime/three_ds_recomp_runtime_probe is not None else "missing",
            "trace_frame_count": runtime/three_ds_recomp_runtime_probe.get("trace_frame_count") if runtime/three_ds_recomp_runtime_probe is not None else 0,
            "floor_hit_count": runtime/three_ds_recomp_runtime_probe.get("floor_hit_count") if runtime/three_ds_recomp_runtime_probe is not None else 0,
            "probe_runtime": runtime/three_ds_recomp_runtime_probe.get("probe_runtime") if runtime/three_ds_recomp_runtime_probe is not None else None,
        },
        "runtime/three_ds_recomp_native_seed": {
            "contract_header": str(lus_contract_header),
            "contract_source": str(lus_contract_source),
            "contract_test": str(lus_contract_test),
            "resource_set_api_present": runtime/three_ds_recomp_seed_present,
        },
        "remaining_for_native_demo": remaining_for_native_demo,
        "blocker_count": len(blockers) + len(final_blockers),
        "blockers": blockers + final_blockers,
    }
    output = Path(args.output) if args.output else Path(str(assets.get("native_demo_readiness", manifest_path.with_name("native_demo_readiness.json"))))
    write_json(output, readiness)
    return readiness


def read_glb(path: Path) -> tuple[dict[str, object], bytes]:
    data = path.read_bytes()
    if len(data) < 20:
        raise ValueError(f"{path}: too small for GLB")
    magic, version, _length = struct.unpack_from("<III", data, 0)
    if magic != 0x46546C67 or version != 2:
        raise ValueError(f"{path}: not a glTF 2.0 GLB")
    offset = 12
    json_doc: dict[str, object] | None = None
    bin_chunk = b""
    while offset + 8 <= len(data):
        chunk_len, chunk_type = struct.unpack_from("<II", data, offset)
        offset += 8
        chunk = data[offset : offset + chunk_len]
        offset += chunk_len
        if chunk_type == 0x4E4F534A:
            json_doc = json.loads(chunk.decode("utf-8"))
        elif chunk_type == 0x004E4942:
            bin_chunk = chunk
    if json_doc is None:
        raise ValueError(f"{path}: missing JSON chunk")
    return json_doc, bin_chunk


def accessor_data(gltf: dict[str, object], bin_chunk: bytes, accessor_index: int) -> tuple[list[float | int], int, str]:
    accessors = gltf.get("accessors", [])
    views = gltf.get("bufferViews", [])
    assert isinstance(accessors, list) and isinstance(views, list)
    accessor = accessors[accessor_index]
    assert isinstance(accessor, dict)
    view = views[int(accessor["bufferView"])]
    assert isinstance(view, dict)
    count = int(accessor["count"])
    component_type = int(accessor["componentType"])
    accessor_type = str(accessor["type"])
    components = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[accessor_type]
    byte_offset = int(view.get("byteOffset", 0)) + int(accessor.get("byteOffset", 0))
    stride = int(view.get("byteStride", 0))
    if component_type == 5126:
        fmt, size = "f", 4
    elif component_type == 5125:
        fmt, size = "I", 4
    elif component_type == 5123:
        fmt, size = "H", 2
    else:
        raise ValueError(f"unsupported GLB component type {component_type}")
    stride = stride or size * components
    values: list[float | int] = []
    for i in range(count):
        base = byte_offset + i * stride
        for c in range(components):
            values.append(struct.unpack_from("<" + fmt, bin_chunk, base + c * size)[0])
    return values, components, accessor_type


def glb_triangles(path: Path, limit: int | None = None) -> list[tuple[tuple[float, float, float], ...]]:
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
                (float(positions_raw[i]), float(positions_raw[i + 1]), float(positions_raw[i + 2]))
                for i in range(0, len(positions_raw), 3)
            ]
            indices: list[int]
            if "indices" in primitive:
                idx_raw, _, _ = accessor_data(gltf, bin_chunk, int(primitive["indices"]))
                indices = [int(v) for v in idx_raw]
            else:
                indices = list(range(len(positions)))
            for i in range(0, len(indices) - 2, 3):
                tri = (positions[indices[i]], positions[indices[i + 1]], positions[indices[i + 2]])
                triangles.append(tri)
                if limit is not None and len(triangles) >= limit:
                    return triangles
    return triangles


def render_preview(args: argparse.Namespace) -> dict[str, object]:
    manifest_path = Path(args.manifest)
    manifest = read_json(manifest_path)
    assets = manifest["assets"]
    assert isinstance(assets, dict)
    room_tris = glb_triangles(Path(str(assets["room_glb"])), limit=3500)
    link_tris = glb_triangles(Path(str(assets["link_child_runtime_glb"])), limit=1800)
    collision = parse_collision_xml(Path(str(assets["collision_xml"])))
    movement = manifest.get("movement", {})
    assert isinstance(movement, dict)
    spawn = movement.get("spawn", {"x": 0.0, "y": 0.0, "z": 0.0})
    assert isinstance(spawn, dict)
    player_x = float(spawn.get("x", 0.0))
    player_z = float(spawn.get("z", 0.0))
    player_y = floor_y_at(collision, player_x, player_z, float(spawn.get("y", 0.0)) + 120.0)
    if player_y is None:
        player_y = float(spawn.get("y", 0.0))
    link_scale = 82.0
    link_min_y = min((v[1] * link_scale for tri in link_tris for v in tri), default=0.0)
    width = int(args.width)
    height = int(args.height)
    pixels = bytearray([245, 244, 238] * width * height)

    def transform_room(v: tuple[float, float, float]) -> tuple[float, float, float]:
        return (v[0] * 10000.0, v[1] * 10000.0, v[2] * 10000.0)

    def transform_link(v: tuple[float, float, float]) -> tuple[float, float, float]:
        return (
            player_x + v[0] * link_scale,
            player_y - link_min_y + v[1] * link_scale,
            player_z + v[2] * link_scale,
        )

    all_points = [transform_room(v) for tri in room_tris for v in tri] + [transform_link(v) for tri in link_tris for v in tri]
    projected = [project_iso(p) for p in all_points]
    min_px = min(p[0] for p in projected)
    max_px = max(p[0] for p in projected)
    min_py = min(p[1] for p in projected)
    max_py = max(p[1] for p in projected)
    span_x = max(max_px - min_px, 1e-6)
    span_y = max(max_py - min_py, 1e-6)
    scale = 0.84 * min(width / span_x, height / span_y)

    def to_screen(v: tuple[float, float, float]) -> tuple[int, int]:
        px, py = project_iso(v)
        sx = int((px - min_px) * scale + width * 0.08)
        sy = int((py - min_py) * scale + height * 0.08)
        return sx, sy

    for tri in room_tris:
        draw_tri_wire(pixels, width, height, tuple(to_screen(transform_room(v)) for v in tri), (70, 84, 92))
    for tri in link_tris:
        draw_tri_wire(pixels, width, height, tuple(to_screen(transform_link(v)) for v in tri), (26, 111, 73))

    output = Path(args.output) if args.output else Path(str(assets["preview_ppm"]))
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("wb") as f:
        f.write(f"P6\n{width} {height}\n255\n".encode("ascii"))
        f.write(pixels)
    png_output = Path(str(assets.get("preview_png", output.with_suffix(".png"))))
    write_png_rgb(png_output, width, height, bytes(pixels))
    summary = {
        "format": "oot3d_standalone_demo_render_preview_v1",
        "status": "valid",
        "manifest": str(manifest_path),
        "output": str(output),
        "png_output": str(png_output),
        "width": width,
        "height": height,
        "room_triangle_count": len(room_tris),
        "link_triangle_count": len(link_tris),
    }
    write_json(output.with_suffix(".json"), summary)
    return summary


def write_png_rgb(path: Path, width: int, height: int, rgb: bytes) -> None:
    def chunk(kind: bytes, data: bytes) -> bytes:
        crc = zlib.crc32(kind)
        crc = zlib.crc32(data, crc)
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", crc & 0xFFFFFFFF)

    if len(rgb) != width * height * 3:
        raise ValueError("RGB buffer length does not match image dimensions")
    rows = bytearray()
    stride = width * 3
    for y in range(height):
        rows.append(0)
        rows.extend(rgb[y * stride : (y + 1) * stride])
    payload = b"\x89PNG\r\n\x1a\n"
    payload += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    payload += chunk(b"IDAT", zlib.compress(bytes(rows), level=6))
    payload += chunk(b"IEND", b"")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)


def project_iso(v: tuple[float, float, float]) -> tuple[float, float]:
    x, y, z = v
    return (x - z * 0.55, -y + (x + z) * 0.22)


def draw_tri_wire(
    pixels: bytearray,
    width: int,
    height: int,
    tri: tuple[tuple[int, int], tuple[int, int], tuple[int, int]],
    color: tuple[int, int, int],
) -> None:
    draw_line(pixels, width, height, tri[0], tri[1], color)
    draw_line(pixels, width, height, tri[1], tri[2], color)
    draw_line(pixels, width, height, tri[2], tri[0], color)


def draw_line(
    pixels: bytearray,
    width: int,
    height: int,
    p0: tuple[int, int],
    p1: tuple[int, int],
    color: tuple[int, int, int],
) -> None:
    x0, y0 = p0
    x1, y1 = p1
    dx = abs(x1 - x0)
    dy = -abs(y1 - y0)
    sx = 1 if x0 < x1 else -1
    sy = 1 if y0 < y1 else -1
    err = dx + dy
    while True:
        if 0 <= x0 < width and 0 <= y0 < height:
            idx = (y0 * width + x0) * 3
            pixels[idx : idx + 3] = bytes(color)
        if x0 == x1 and y0 == y1:
            break
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


def run_all(args: argparse.Namespace) -> dict[str, object]:
    manifest = build_demo(args)
    manifest_path = Path(str(manifest["demo_root"])) / "demo_manifest.json"
    sim = simulate_demo(argparse.Namespace(manifest=manifest_path, output=None))
    preview = render_preview(argparse.Namespace(manifest=manifest_path, output=None, width=args.width, height=args.height))
    viewer = run_viewer_self_test(manifest_path)
    resource_contract = build_resource_contract(argparse.Namespace(manifest=manifest_path, output=None))
    readiness = native_readiness(argparse.Namespace(manifest=manifest_path, output=None))
    verify = verify_demo(argparse.Namespace(manifest=manifest_path, output=None))
    return {
        "format": "oot3d_standalone_demo_all_v1",
        "status": "valid"
        if verify["status"] == "valid"
        and sim["status"] == "valid"
        and preview["status"] == "valid"
        and viewer["status"] == "valid"
        else "invalid",
        "manifest": str(manifest_path),
        "verify": verify,
        "simulation": {k: sim[k] for k in ("status", "frame_count", "floor_hit_count", "blocked_step_count")},
        "preview": preview,
        "viewer": viewer,
        "native_resource_contract": {
            "status": resource_contract["status"],
            "resource_count": resource_contract["resource_count"],
            "issue_count": resource_contract["issue_count"],
        },
        "native_demo_readiness": {
            "status": readiness["status"],
            "native_demo_ready": readiness["native_demo_ready"],
            "blocker_count": readiness["blocker_count"],
        },
    }


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Build and verify a small standalone OOT3D demo asset contract.")
    sub = parser.add_subparsers(dest="command", required=True)

    def add_common_build(p: argparse.ArgumentParser) -> None:
        p.add_argument("--repo-root", type=Path, default=repo_root_from_script())
        p.add_argument("--tool-root", type=Path, default=None)
        p.add_argument("--romfs", type=Path, default=DEFAULT_ROMFS)
        p.add_argument("--work-root", type=Path, default=DEFAULT_WORK_ROOT)
        p.add_argument("--frame-step", type=int, default=4)

    build = sub.add_parser("build")
    add_common_build(build)

    verify = sub.add_parser("verify")
    verify.add_argument("--manifest", type=Path, required=True)
    verify.add_argument("--output", type=Path, default=None)

    simulate = sub.add_parser("simulate")
    simulate.add_argument("--manifest", type=Path, required=True)
    simulate.add_argument("--output", type=Path, default=None)

    preview = sub.add_parser("render-preview")
    preview.add_argument("--manifest", type=Path, required=True)
    preview.add_argument("--output", type=Path, default=None)
    preview.add_argument("--width", type=int, default=960)
    preview.add_argument("--height", type=int, default=720)

    resource_contract = sub.add_parser("resource-contract")
    resource_contract.add_argument("--manifest", type=Path, required=True)
    resource_contract.add_argument("--output", type=Path, default=None)

    readiness = sub.add_parser("native-readiness")
    readiness.add_argument("--manifest", type=Path, required=True)
    readiness.add_argument("--output", type=Path, default=None)

    all_cmd = sub.add_parser("all")
    add_common_build(all_cmd)
    all_cmd.add_argument("--width", type=int, default=960)
    all_cmd.add_argument("--height", type=int, default=720)
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = make_parser()
    args = parser.parse_args(argv)
    if args.command == "build":
        result = build_demo(args)
    elif args.command == "verify":
        result = verify_demo(args)
    elif args.command == "simulate":
        result = simulate_demo(args)
    elif args.command == "render-preview":
        result = render_preview(args)
    elif args.command == "resource-contract":
        result = build_resource_contract(args)
    elif args.command == "native-readiness":
        result = native_readiness(args)
    elif args.command == "all":
        result = run_all(args)
    else:
        parser.error(f"unknown command {args.command}")
        return 2
    print(json.dumps(result, indent=2))
    if args.command == "native-readiness":
        return 0
    return 0 if result.get("status") == "valid" or result.get("status") == "built" else 1


if __name__ == "__main__":
    raise SystemExit(main())
