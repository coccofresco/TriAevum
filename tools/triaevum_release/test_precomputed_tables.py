import json
import struct
import tempfile
import unittest
from pathlib import Path

import precomputed_tables as tables
import scene_lights
from test_scene_lights import ORANGE, RGBA8, cmb, cube, room

ROMFS = "ab" * 32


def masks_table(entries, masks):
    data = struct.pack("<4s3I", b"TISP", 1, 3, len(entries))
    for fingerprint, triangles, kind, first in entries:
        data += struct.pack("<QIIQ", fingerprint, triangles, kind, first)
    data += b"".join(struct.pack("<Q", mask) for mask in masks)
    return data + struct.pack("<Q", tables._checksum(data))


def lit_room():
    stand, triangles = cube((0.0, 0.0, 0.0), 40.0)
    return room(cmb([(0, False)], [("mark_lt_01", RGBA8, ORANGE)], [(0, stand, triangles)]))


class PrecomputedTablesTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def write(self, name, data):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def test_mask_table_holds_only_fingerprints_counts_and_masks(self):
        good = masks_table([(1, 2, 0, 0), (5, 0, 1, 2), (9, 1, 0, 2)], [3, 0, 1 << 63])
        self.assertEqual(tables.read_interior_sun_masks(self.write("good.masks", good)),
                         {"entries": 3, "masks": 3, "bound_fallbacks": 1})
        damaged = bytearray(good)
        damaged[16] ^= 1
        for name, data in (("damaged", bytes(damaged)),
                           ("unordered", masks_table([(9, 1, 0, 0), (1, 1, 0, 1)], [0, 0])),
                           ("gap", masks_table([(1, 1, 0, 1)], [0, 0])),
                           ("fallback-masks", masks_table([(1, 1, 1, 0)], [0])),
                           ("criterion", good[:8] + struct.pack("<I", 2) + good[12:])):
            with self.subTest(name), self.assertRaises(ValueError):
                tables.read_interior_sun_masks(self.write(f"{name}.masks", data))

    def test_light_table_holds_only_catalog_emitter_fields(self):
        romfs = self.root / "romfs"
        self.write("romfs/scene/ganon_1_info.zsi", lit_room())
        path = self.write("lights.json", tables.scene_lights_table(romfs, ROMFS))
        table = tables.read_scene_lights_table(path)
        self.assertEqual(len(table["rooms"]), 1)
        emitter = next(iter(table["rooms"].values()))[0]
        for name, change in (("vertices", lambda e: e.update(vertices=[[0, 0, 0]])),
                             ("texels", lambda e: e.update(texture="x" * 65)),
                             ("surface", lambda e: e.update(surfaces=["not-a-hash"])),
                             ("category", lambda e: e.update(category="mesh")),
                             ("position", lambda e: e.update(position=[0, 0]))):
            with self.subTest(name):
                broken = json.loads(json.dumps(table))
                change(next(iter(broken["rooms"].values()))[0])
                with self.assertRaises(ValueError):
                    tables.read_scene_lights_table(self.write("broken.json", json.dumps(broken).encode()))
        self.assertEqual(set(emitter) - tables.EMITTER_KEYS, set())

    def test_release_table_gives_the_locally_computed_catalog(self):
        romfs = self.root / "romfs"
        data = lit_room()
        self.write("romfs/scene/ganon_1_info.zsi", data)
        self.write("romfs/scene/ganon_2_info.zsi", data[:20] + b"\xff" + data[21:])
        local, summary = scene_lights.build_catalog(romfs)
        table = tables.read_scene_lights_table(self.write("lights.json", tables.scene_lights_table(romfs, ROMFS)))
        calls = []
        original = scene_lights.room_emitters
        scene_lights.room_emitters = lambda data: calls.append(data) or original(data)
        try:
            shipped, shipped_summary = scene_lights.build_catalog(romfs, table["rooms"])
            # A room the table does not cover is computed here.
            partial = dict(list(table["rooms"].items())[:1])
            mixed, _ = scene_lights.build_catalog(romfs, partial)
        finally:
            scene_lights.room_emitters = original
        self.assertEqual(shipped, local)
        self.assertEqual(mixed, local)
        self.assertEqual(shipped_summary, {**summary, "precomputed_rooms": 2})
        self.assertEqual(len(calls), 1)

    def test_bind_and_audit_require_catalogued_tables(self):
        masks = self.write("publisher/interior-sun.masks", masks_table([(1, 1, 0, 0)], [7]))
        self.write("publisher/romfs/scene/ganon_1_info.zsi", lit_room())
        lights = self.write("publisher/scene-lights.json",
                            tables.scene_lights_table(self.root / "publisher/romfs", ROMFS))
        catalog = {"titles": [{"inputs": {"romfs": {"sha256": ROMFS}}},
                              {"inputs": {"romfs": {"sha256": "cd" * 32}}}]}
        bound, items = tables.bind_precomputed_tables(catalog, masks, lights)
        self.assertNotIn("precomputed_tables", catalog["titles"][0])
        # Keyed by room content: another revision's identical rooms use them too.
        self.assertEqual(bound["titles"][0]["precomputed_tables"], bound["titles"][1]["precomputed_tables"])
        self.assertEqual(bound["titles"][1]["precomputed_tables"]["source_romfs_sha256"], ROMFS)
        package = self.root / "package"
        declared = {}
        for item in items:
            self.write(f"package/{item['path']}", Path(item["source"]).read_bytes())
            declared[item["path"]] = {"role": item["role"]}
        tables.validate_precomputed_tables(package, bound, declared)
        self.assertEqual(set(tables.title_tables(package, bound["titles"][1])), {"interior_sun_masks", "scene_lights"})
        self.assertEqual(tables.title_tables(package, catalog["titles"][1]), {})
        with self.assertRaisesRegex(ValueError, "Uncatalogued"):
            tables.validate_precomputed_tables(package, catalog, declared)
        with self.assertRaisesRegex(ValueError, "uncatalogued RomFS"):
            tables.validate_precomputed_tables(package, {"titles": bound["titles"][1:]}, declared)
        (package / tables.LIGHTS_PATH).write_bytes(b"{}")
        with self.assertRaises(ValueError):
            tables.validate_precomputed_tables(package, bound, declared)
        with self.assertRaises(ValueError):
            tables.bind_precomputed_tables({"titles": [catalog["titles"][1]]}, masks, lights)


if __name__ == "__main__":
    unittest.main()
