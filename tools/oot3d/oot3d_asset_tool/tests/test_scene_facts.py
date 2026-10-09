"""Small synthetic fixtures only. No extracted title data or game launches."""
from __future__ import annotations

import contextlib
import io
import json
import math
import os
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from oot3d_asset_tool.binary import ParseError
from oot3d_asset_tool.cmb import Mesh, Primitive, Shape, Skeleton, SkeletonBone, Vec3
from oot3d_asset_tool.scene_facts import (CODE_BASE, GLOBAL_ENTRANCES, NORMAL_SCENES, RECORD_SIZE,
    SCENE_COUNT, TAIL_SCENES, SceneFactsSource, build_scene_facts, query_scene_collision,
    room_paths, scene_table, source_key)
from oot3d_asset_tool.zsi import ZsiFile
from oot3d_asset_tool.scene_geometry import (CollisionMesh, RayPolygon, collision_ray,
    entrance_poses, forward, overview_pose, visual_bounds, yaw_degrees)


def command(kind, count=0, arg=0):
    return struct.pack("<II", kind | count << 8, arg)


def actor(actor_id, x=10, y=20, z=30, yaw=16384, params=0x1234):
    return struct.pack("<HhhhhhhH", actor_id, x, y, z, 0, yaw, 0, params)


def synthetic_inputs(root):
    scene_root = root / "romfs/scene"
    scene_root.mkdir(parents=True)
    code = bytearray(TAIL_SCENES-CODE_BASE + (SCENE_COUNT-14)*RECORD_SIZE)
    for scene in range(SCENE_COUNT):
        offset = (NORMAL_SCENES+scene*RECORD_SIZE if scene < 14 else TAIL_SCENES+(scene-14)*RECORD_SIZE)-CODE_BASE
        path = b"rom:/scene/fixture_info.zsi\0"
        code[offset:offset+len(path)] = path
    start = GLOBAL_ENTRANCES-CODE_BASE
    code[start:start+1586*4] = b"\xff" * (1586*4)
    code[start+4:start+8] = bytes((0x55, 0, 0, 0))
    code_bin = root / "exefs/code.bin"
    code_bin.parent.mkdir()
    code_bin.write_bytes(code)
    data = bytearray(0x400)
    data[:4] = b"ZSI\x01"
    commands = [command(4, 1, 0x100), command(6, 1, 0x140), command(0, 1, 0x150),
                command(0x0F, 1, 0x170), command(0x11, 0, 0x010002), command(0x14)]
    data[0x18:0x18+8*len(commands)] = b"".join(commands)
    path = b"rom:/scene/fixture_0_info.zsi\0"
    data[0x110:0x110+len(path)] = path
    data[0x150:0x152] = bytes((0, 0))
    data[0x160:0x170] = actor(0)
    data[0x180:0x19C] = bytes(range(28))
    (scene_root / "fixture_info.zsi").write_bytes(data)
    room = bytearray(0x100)
    room[:4] = b"ZSI\x01"
    room[0x18:0x38] = b"".join([command(0x16), command(1, 2, 0x80), command(0x12), command(0x14)])
    room[0x90:0xA0] = actor(0x5E)
    room[0xA0:0xB0] = actor(0x777, x=40)
    (scene_root / "fixture_0_info.zsi").write_bytes(room)
    return SceneFactsSource(scene_root, code_bin)


class SceneGeometryTests(unittest.TestCase):
    def test_yaw_and_entrance_poses(self):
        self.assertEqual(yaw_degrees(16384), 90)
        self.assertEqual(yaw_degrees(65535), -180/32768)
        self.assertEqual(yaw_degrees(-32768), -180)
        self.assertEqual(forward(0), [0, 0, 1])
        self.assertAlmostEqual(forward(90)[0], 1)
        poses = entrance_poses({"position": [1, 2, 3], "index": 4, "roomId": 2, "yawDegrees": 90}, 3)
        self.assertEqual(poses[0]["position"], [1, 62, 3])
        self.assertEqual(poses[1]["yawDegrees"], -90)
        self.assertEqual(poses[0]["status"], "unverified")
        self.assertEqual(entrance_poses({"position": None}, 0), [])

    def test_hierarchical_visual_transform_and_unused_vertex(self):
        bones = (SkeletonBone(0, -1, Vec3(2, 2, 2), Vec3(0, math.pi/2, 0), Vec3(10, 20, 30)),
                 SkeletonBone(1, 0, Vec3(1, 1, 1), Vec3(0, 0, 0), Vec3(0, 0, 2)))
        shape = Shape(0, 0, 0, (Vec3(0, 0, 0), Vec3(1, 1, 1), Vec3(999, 999, 999)), (), (), (),
                      (Primitive((0, 1, 0), 0, (1,)),))
        model = SimpleNamespace(skeleton=Skeleton(0, 0, bones), shapes=[shape], meshes=[Mesh(0, 0, 0, 0)])
        bounds = visual_bounds([model])
        for actual, expected in zip(bounds["min"], [14, 20, 28]):
            self.assertAlmostEqual(actual, expected)
        for actual, expected in zip(bounds["max"], [16, 22, 30]):
            self.assertAlmostEqual(actual, expected)
        pose = overview_pose(bounds, 0, 0)
        self.assertEqual(pose["status"], "unverified")
        self.assertLess(pose["pitchDegrees"], 0)

    def test_skinned_bounds_unknown_and_bad_hierarchy(self):
        bone = SkeletonBone(0, -1, Vec3(1, 1, 1), Vec3(0, 0, 0), Vec3(0, 0, 0))
        shape = Shape(0, 0, 0, (Vec3(0, 0, 0),), (), (), (), (Primitive((0, 0, 0), 2, (0,)),))
        model = SimpleNamespace(skeleton=Skeleton(0, 0, (bone,)), shapes=[shape], meshes=[Mesh(0, 0, 0, 0)])
        bounds = visual_bounds([model])
        self.assertEqual(bounds["status"], "partial")
        self.assertIsNone(bounds["min"])
        self.assertIsNone(overview_pose(bounds, 0, 0))
        model.skeleton = Skeleton(0, 0, (SkeletonBone(0, 0, bone.scale, bone.rotation, bone.translation),))
        with self.assertRaises(ParseError):
            visual_bounds([model])

    @staticmethod
    def floors():
        vertices = ((-2, 0, -2), (0, 0, 2), (2, 0, -2), (-2, 10, -2), (0, 10, 2), (2, 10, -2))
        return CollisionMesh("synthetic", vertices,
            (RayPolygon((0, 1, 2), 3, (12, 34), 7), RayPolygon((3, 4, 5), 4, (56, 78), 9)))

    def test_stacked_floors_and_origin(self):
        result = collision_ray([self.floors()], 0, 0)
        self.assertEqual(result["origin"], [0, 11, 0])
        self.assertEqual([h["position"][1] for h in result["hits"]], [10, 0])
        self.assertEqual([h["polygonIndex"] for h in result["hits"]], [9, 7])
        self.assertEqual(result["hits"][0]["normal"], [0, 1, 0])
        self.assertEqual(result["hits"][0]["surfaceWords"], (56, 78))
        self.assertEqual(len(collision_ray([self.floors()], 0, 0, 5)["hits"]), 1)

    def test_no_hit_backfaces_edges_and_degenerate(self):
        self.assertEqual(collision_ray([self.floors()], 20, 20)["status"], "no-hit")
        self.assertEqual(collision_ray([], 0, 0)["hits"], [])
        mesh = self.floors()
        mesh = CollisionMesh(mesh.identity, mesh.vertices, (RayPolygon((2, 1, 0)), RayPolygon((0, 0, 0))))
        self.assertEqual(collision_ray([mesh], 0, 0)["hits"][0]["normal"], [0, -1, 0])
        self.assertEqual(len(collision_ray([self.floors()], -2, -2)["hits"]), 2)
        with self.assertRaises(ValueError):
            collision_ray([mesh], float("nan"), 0)

    def test_slope_and_shared_edge_preserve_polygon_identity(self):
        mesh = CollisionMesh("slope", ((0, 0, 0), (0, 0, 2), (2, 2, 0)),
                             (RayPolygon((0, 1, 2), source_index=4), RayPolygon((0, 1, 2), source_index=5)))
        hits = collision_ray([mesh], 0.5, 0.5)["hits"]
        self.assertEqual(len(hits), 2)
        self.assertAlmostEqual(hits[0]["position"][1], 0.5)
        self.assertAlmostEqual(hits[0]["normal"][0], -math.sqrt(0.5))


class SceneFactsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = synthetic_inputs(self.root)

    def test_native_table_and_invalid_image(self):
        self.assertEqual(len(self.source.scenes), 111)
        self.assertEqual(self.source.scenes[0x55]["path"], "fixture_info.zsi")
        with self.assertRaises(ParseError):
            scene_table(b"\0" * 64)

    def test_room_list_is_not_limited_to_a_sample_window(self):
        data = bytearray(16 + 12 * 0x44)
        for i in range(12):
            name = f"rom:/scene/fixture_{i}_info.zsi\0".encode()
            data[16+i*0x44:16+i*0x44+len(name)] = name
        zsi = ZsiFile(Path("synthetic"), bytes(data))
        paths = room_paths(zsi, SimpleNamespace(argument=0, parameter=12))
        self.assertEqual(len(paths), 12)
        self.assertEqual(paths[-1], "fixture_11_info.zsi")

    def test_export_deterministic_and_source_keys(self):
        one = build_scene_facts(self.source, 0x55)
        two = build_scene_facts(self.source, 0x55)
        self.assertEqual(json.dumps(one, sort_keys=True), json.dumps(two, sort_keys=True))
        setup = one["setups"][0]
        self.assertEqual(setup["entrances"][0]["globalEntranceIndices"], [1])
        self.assertEqual(setup["spawns"][0]["roomIds"], [0])
        self.assertEqual(setup["entrances"][0]["yawDegrees"], 90)
        self.assertEqual(setup["sky"]["indoors"], True)
        room = setup["rooms"][0]
        self.assertEqual(room["actors"][0]["sourceKey"], source_key(0x55, 0, 0, 0, "roomActor", 0))
        self.assertEqual(len(room["emitterCandidates"]), 1)
        self.assertEqual(room["emitterCandidates"][0]["confidence"], "medium")
        self.assertEqual(room["actors"][0]["confidence"], "high")
        self.assertIsNone(room["emitterCandidates"][0]["sourceKey"]["emitterSlot"])
        self.assertIsNone(room["actors"][1]["name"])
        self.assertEqual(setup["environment"]["status"], "diagnostic")
        self.assertEqual(len(one["poseCandidates"]), 2)
        self.assertTrue(all(len(f["sha256"]) == 64 for f in one["provenance"]["inputs"]))
        self.assertNotEqual(source_key(1, 0, 0, 0, "roomActor", 0), source_key(1, 1, 0, 0, "roomActor", 0))

    def test_missing_room_and_setup_fail(self):
        with self.assertRaises(ParseError):
            build_scene_facts(self.source, 0x55, 8)
        (self.source.scene_root / "fixture_0_info.zsi").unlink()
        with self.assertRaisesRegex(ParseError, "missing room ZSI"):
            build_scene_facts(self.source, 0x55)

    def test_native_collision_and_shared_header_across_setups(self):
        data = bytearray(0x300)
        data[:4] = b"ZSI\x01"
        setup = b"".join((command(4), command(3, arg=0xF0), command(0x14)))
        data[0x18:0x18+len(setup)*2] = setup*2
        struct.pack_into("<6h5H", data, 0x100, 0, 0, 0, 10, 0, 10, 3, 1, 1, 0, 0)
        struct.pack_into("<5I", data, 0x118, 0x140, 0x160, 0x174, 0x180, 0x180)
        struct.pack_into("<9h", data, 0x140, 0, 0, 0, 0, 0, 10, 10, 0, 0)
        struct.pack_into("<5H3hf", data, 0x160, 0, 0, 1, 2, 0x55DA, 0, 32767, 0, 0)
        (self.source.scene_root / "fixture_info.zsi").write_bytes(data)
        result = query_scene_collision(self.source, 0x55, 2, 2, setup=1)
        self.assertEqual(result["status"], "hit")
        self.assertEqual(result["hits"][0]["position"], [2, 0, 2])
        self.assertEqual(result["hits"][0]["mesh"], "fixture_info.zsi@0x100")
        self.assertEqual(result["hits"][0]["polygonIndex"], 0)
        self.assertEqual(query_scene_collision(self.source, 0x55, 20, 20)["status"], "no-hit")

    def test_unavailable_collision_is_not_no_hit(self):
        with self.assertRaisesRegex(ParseError, "no decoded native collision"):
            query_scene_collision(self.source, 0x55, 0, 0)

    def test_cli_output_and_nonfinite_height(self):
        from oot3d_asset_tool.cli import main
        out = self.root / "facts"
        with contextlib.redirect_stdout(io.StringIO()):
            result = main(["scene-facts", "--scene", "0x55", "--extract-root", str(self.root), "--out", str(out)])
        self.assertEqual(result, 0)
        self.assertEqual(json.loads((out / "scene-0055.facts.json").read_text())["schema"], "triaevum.scene-facts")
        with self.assertRaises(ValueError):
            build_scene_facts(self.source, 0x55, eye_height=float("inf"))

    def test_cli_refuses_repository_output_and_preflights_all_inputs(self):
        from oot3d_asset_tool.cli import main
        common = ["scene-facts", "--extract-root", str(self.root)]
        with contextlib.redirect_stderr(io.StringIO()) as errors:
            with self.assertRaises(SystemExit) as failure:
                main(common + ["--scene", "0x55", "--out", str(Path(__file__).parent)])
        self.assertEqual(failure.exception.code, 2)
        self.assertIn("outside the repository", errors.getvalue())
        (self.source.scene_root / "fixture_0_info.zsi").unlink()
        out = self.root / "must-not-exist"
        with contextlib.redirect_stderr(io.StringIO()) as errors:
            with self.assertRaises(SystemExit) as failure:
                main(common + ["--all", "--out", str(out)])
        self.assertEqual(failure.exception.code, 2)
        self.assertIn("no outputs written", errors.getvalue())
        self.assertFalse(out.exists())

    def test_cli_requires_extract_root_for_both_commands(self):
        from oot3d_asset_tool.cli import main
        commands = [["scene-facts", "--scene", "0x55", "--out", str(self.root / "unused")],
                    ["collision-ray", "--scene", "0x55", "--x", "0", "--z", "0"]]
        with patch.dict(os.environ, {"OOT3D_EXTRACT_ROOT": ""}):
            for command in commands:
                with self.subTest(command=command[0]), contextlib.redirect_stderr(io.StringIO()) as errors:
                    with self.assertRaises(SystemExit) as failure:
                        main(command)
                    self.assertEqual(failure.exception.code, 2)
                    self.assertIn("provide --extract-root or set OOT3D_EXTRACT_ROOT", errors.getvalue())

    def test_cli_extract_root_environment_and_flag_precedence(self):
        from oot3d_asset_tool.cli import main
        command = ["scene-facts", "--scene", "0x55", "--out", str(self.root / "facts")]
        with patch.dict(os.environ, {"OOT3D_EXTRACT_ROOT": str(self.root)}), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(main(command), 0)
        with patch.dict(os.environ, {"OOT3D_EXTRACT_ROOT": str(self.root / "missing")}), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(main(command + ["--extract-root", str(self.root)]), 0)

    def test_all_skips_missing_scenes_and_writes_summary(self):
        from oot3d_asset_tool.cli import main
        # Only one scene table row points at our existing synthetic file.
        code = bytearray(self.source.code)
        for scene, row in self.source.scenes.items():
            if scene == 0x55:
                continue
            offset = row["fileOffset"]
            name = f"rom:/scene/missing_{scene}_info.zsi\0".encode()
            code[offset:offset+0x44] = name.ljust(0x44, b"\0")
        self.source.code_bin.write_bytes(code)
        out = self.root / "facts"
        command = ["scene-facts", "--extract-root", str(self.root), "--all", "--out", str(out)]
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(main(command), 0)
        summary_path = out / "all-scenes-summary.json"
        summary = json.loads(summary_path.read_text())
        self.assertEqual(summary["written"], [{"scene": 0x55, "file": "scene-0055.facts.json"}])
        self.assertEqual(summary["writtenCount"], 1)
        self.assertEqual(summary["skippedCount"], SCENE_COUNT - 1)
        self.assertEqual({item["scene"] for item in summary["skipped"]}, set(range(SCENE_COUNT)) - {0x55})
        self.assertTrue(all(set(item) == {"scene", "reason"} and "missing scene ZSI" in item["reason"]
                            for item in summary["skipped"]))
        first = summary_path.read_bytes()
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(main(command), 0)
        self.assertEqual(first, summary_path.read_bytes())
        # Stale outputs must not turn a run with no available scenes into success.
        (self.source.scene_root / "fixture_info.zsi").unlink()
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(main(command), 1)
        summary = json.loads(summary_path.read_text())
        self.assertEqual(summary["writtenCount"], 0)
        self.assertEqual(summary["skippedCount"], SCENE_COUNT)
        # An explicitly requested missing scene still fails, rather than being skipped.
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as failure:
                main(["scene-facts", "--extract-root", str(self.root), "--scene", "0x55", "--out", str(out)])
        self.assertEqual(failure.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
