import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from room_catalog import prepare, compact
from scene_lights import prepare_scene_lights
from test_water_textures import level3_romfs


class RoomCatalogTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.room = b"synthetic complete room"
        self.romfs = self.root / "romfs.bin"
        self.romfs.write_bytes(level3_romfs({"rooms": {"test_0_info.zsi": self.room}}))
        catalog = {"schema": "triaevum.scene-lights", "version": 1, "title": "fixture", "rejected": [], "rooms": [
            {"source": "rooms/test_0_info.zsi", "sourceSha256": hashlib.sha256(self.room).hexdigest(),
             "scene": 1, "room": 0, "fingerprint": "1234567890abcdef", "emitters": [
                 {"category": "lamp", "position": [-1851.8479614257812, 1, 2], "radius": 10,
                  "gain": 1, "colour": [1.0, 0.5, 0.1], "surfaces": ["1234567890abcdef"], "independentEmission": True}]}]}
        self.recipe = {"inputs": {"romfs": {"sha256": hashlib.sha256(self.romfs.read_bytes()).hexdigest()}},
                       "room_catalog": {"version": 1, "title": "fixture", "directory": "rooms",
                                        "generator_sha256": "a" * 64, "catalog_json": compact(catalog).decode()}}

    def run_prepare(self):
        return prepare_scene_lights(data_root=self.root / "data", recipe=self.recipe, romfs=self.romfs)

    def test_install_entrypoint_preserves_bytes_reuses_and_repairs(self):
        p = self.run_prepare()
        expected = self.recipe["room_catalog"]["catalog_json"].encode()
        self.assertEqual(p.read_bytes(), expected)
        stamp = p.stat().st_mtime_ns
        self.assertEqual(self.run_prepare(), p)
        self.assertEqual(p.stat().st_mtime_ns, stamp)
        p.write_bytes(b"broken")
        self.run_prepare()
        self.assertEqual(p.read_bytes(), expected)
        p.with_name("receipt.json").write_bytes(b"{")
        self.run_prepare()
        self.assertEqual(p.read_bytes(), expected)
        self.recipe["room_catalog"]["generator_sha256"] = "b" * 64
        self.assertNotEqual(self.run_prepare(), p)

    def test_cached_catalog_cannot_hide_changed_actual_input(self):
        self.run_prepare()
        self.romfs.write_bytes(level3_romfs({"rooms": {"test_0_info.zsi": b"changed"}}))
        with self.assertRaises(ValueError): self.run_prepare()
        self.recipe["inputs"]["romfs"]["sha256"] = hashlib.sha256(self.romfs.read_bytes()).hexdigest()
        with self.assertRaises(ValueError): self.run_prepare()

    def test_uncovered_and_missing_rooms_fail_closed(self):
        for files in ({}, {"rooms": {"unknown_1_info.zsi": self.room}},
                      {"rooms": {"test_0_info.zsi": self.room, "unknown_1_info.zsi": self.room}}):
            self.romfs.write_bytes(level3_romfs(files))
            self.recipe["inputs"]["romfs"]["sha256"] = hashlib.sha256(self.romfs.read_bytes()).hexdigest()
            with self.assertRaises(ValueError): self.run_prepare()

    def test_hashed_exclusions_are_verified_not_silently_skipped(self):
        extra = b"not in the authoritative native scene table"
        self.romfs.write_bytes(level3_romfs({"rooms": {"test_0_info.zsi": self.room, "unused_0_info.zsi": extra}}))
        self.recipe["inputs"]["romfs"]["sha256"] = hashlib.sha256(self.romfs.read_bytes()).hexdigest()
        self.recipe["room_catalog"]["excluded_rooms"] = [{"source": "rooms/unused_0_info.zsi",
            "sourceSha256": hashlib.sha256(extra).hexdigest(), "reason": "not-referenced-by-native-scene-table"}]
        self.run_prepare()
        self.recipe["room_catalog"]["excluded_rooms"][0]["sourceSha256"] = "c" * 64
        with self.assertRaises(ValueError): self.run_prepare()

    def test_bad_namespace_duplicate_and_emitter_rejected(self):
        original = copy.deepcopy(self.recipe)
        self.recipe["room_catalog"]["title"] = "foreign"
        with self.assertRaises(ValueError): self.run_prepare()
        for mutation in (lambda c: c["rooms"].append(c["rooms"][0]),
                         lambda c: c["rooms"][0]["emitters"][0].update(radius=-1),
                         lambda c: c["rooms"][0]["emitters"][0].update(category="guess"),
                         lambda c: c["rooms"][0]["emitters"][0].update(independentEmission=1)):
            self.recipe = copy.deepcopy(original)
            catalog = json.loads(self.recipe["room_catalog"]["catalog_json"])
            mutation(catalog)
            self.recipe["room_catalog"]["catalog_json"] = compact(catalog).decode()
            with self.assertRaises(ValueError): self.run_prepare()


if __name__ == "__main__": unittest.main()
