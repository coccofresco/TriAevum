#!/usr/bin/env python3
"""Checks of MM3D's developer warp catalog generator.

The regeneration check needs a 2ship2harkinian checkout: set
TRIAEVUM_2S2H_ROOT to it (the commit recorded in the catalog header's SHA-256).

usage: python -I test_generate_mm3d_dev_warp_catalog.py
"""
from __future__ import annotations

import os
import re
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import generate_mm3d_dev_warp_catalog as generator  # noqa: E402

CATALOG = HERE.parent / "adapter" / "mm3d_dev_warp_catalog.inc"
ROW = re.compile(r'\{"([^"]*)", "[^"]*", "[^"]*", 0x([0-9A-F]{4}), 0x([0-9A-F]{2}), (\d+), -1\}')
SCENE = re.compile(r'\{"([^"]*)", "[^"]*", "[^"]*", (\d+), (\d+), false\}')
ZONE = re.compile(r'\{"([a-z0-9-]+)", "([^"]*)", "([^"]*)", "([^"]*)", 0x([0-9A-F]{4}), \{([^}]*)\}, (true|false)\}')


class Clock(unittest.TestCase):
    def test_clock_values(self):
        self.assertEqual(generator.clock("0000"), 0x0000)
        self.assertEqual(generator.clock("1200"), 0x8000)
        self.assertEqual(generator.clock("1800"), 0xC000)
        self.assertEqual(generator.clock("0300"), 0x2000)
        # Morning is staged one past the 06:00 boundary, as 2S2H's map select.
        self.assertEqual(generator.clock("0600"), 0x4001)
        self.assertEqual(generator.clock("2200"), 0xEAAB)

    def test_slug(self):
        self.assertEqual(generator.slug("Odolwa's Lair"), "odolwas-lair")
        self.assertEqual(generator.slug("South Clock Town (Clock Tower door)"), "south-clock-town-clock-tower-door")


class CommittedCatalog(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = CATALOG.read_text(encoding="utf-8")
        cls.rows = [(label, int(e, 16), int(s, 16), int(p)) for label, e, s, p in ROW.findall(cls.text)]
        cls.scenes = [(name, int(first), int(count)) for name, first, count in SCENE.findall(cls.text)]
        cls.zones = ZONE.findall(cls.text)

    def test_rows_are_layer_zero_encodings(self):
        self.assertEqual(len(self.rows), 490)
        seen = set()
        for label, entrance, scene, spawn in self.rows:
            self.assertEqual(entrance & 0xF, 0, label)
            self.assertLess(entrance >> 9, 112, label)
            self.assertLessEqual(scene, 0x74, label)
            self.assertTrue(label.startswith(f"{(entrance >> 4) & 31}: "), label)
            self.assertNotIn(entrance, seen)
            seen.add(entrance)

    def test_known_rows(self):
        by_entrance = {entrance: (label, scene, spawn) for label, entrance, scene, spawn in self.rows}
        # Live-proven on MM3D 2026-10-09: North (0x6E) and East (0x6C) Clock Town.
        self.assertEqual(by_entrance[0xD600][1], 0x6E)
        self.assertEqual(by_entrance[0xD200][1], 0x6C)
        self.assertEqual(by_entrance[0xD800], ("0: from Clock Tower Interior", 0x6F, 0))
        self.assertEqual(by_entrance[0xD210][0], "1: from South Clock Town (to lower)")
        self.assertEqual(by_entrance[0x54A0][1], 0x73)  # MM3D's telescope scene
        self.assertEqual(by_entrance[0xDC00][1], 0x71)
        self.assertEqual(by_entrance[0xDE00][1], 0x72)
        self.assertEqual(by_entrance[0xD8B0][0], "11: MM3D spawn")

    def test_scene_slices_cover_rows(self):
        self.assertEqual(len(self.scenes), 104)
        covered = set()
        for name, first, count in self.scenes:
            self.assertGreater(count, 0, name)
            self.assertLessEqual(first + count, len(self.rows), name)
            group = self.rows[first][1] >> 9
            for label, entrance, _, _ in self.rows[first:first + count]:
                self.assertEqual(entrance >> 9, group, f"{name}: {label}")
            covered.update(range(first, first + count))
        self.assertEqual(covered, set(range(len(self.rows))))
        self.assertEqual(self.scenes[0][0], "South Clock Town")

    def test_zones_name_catalog_rows(self):
        entrances = {entrance for _, entrance, _, _ in self.rows}
        ids = set()
        self.assertGreaterEqual(len(self.zones), 60)
        for zone_id, category, name, _, entrance, preset, _ in self.zones:
            self.assertIn(int(entrance, 16), entrances, name)
            self.assertNotIn(zone_id, ids)
            ids.add(zone_id)
            day, time, form, unused = (int(v, 0) for v in preset.split(","))
            self.assertIn(day, (-1, 1, 2, 3), name)
            self.assertTrue(time == -1 or 0 <= time <= 0xFFFF, name)
            self.assertIn(form, (-1, 0, 1, 2, 3, 4), name)
            self.assertEqual(unused, -1, name)
            # Fierce Deity is never a preset: the game allows it only in boss rooms.
            self.assertNotEqual(form, 0, name)
            # A day is always staged with a time (the runtime refuses a day alone).
            self.assertTrue(day == -1 or time != -1, name)

    def test_proven_zones_are_an_explicit_list(self):
        # Proof is per zone id with a live receipt, never derived from ZONES:
        # a zone added to ZONES stays unverified until its id is listed.
        self.assertIsInstance(generator.PROVEN_ZONES, frozenset)
        self.assertEqual(len(generator.PROVEN_ZONES), 69)
        ids = {generator.slug(zone[1]) for zone in generator.ZONES}
        self.assertLessEqual(generator.PROVEN_ZONES, ids)
        proven = {zone_id: flag == "true" for zone_id, *_, flag in self.zones}
        for zone_id, flag in proven.items():
            self.assertEqual(flag, zone_id in generator.PROVEN_ZONES, zone_id)

    def test_spawns_are_never_named_after_their_own_scene(self):
        scene_of_row = {}
        for name, first, count in self.scenes:
            for label, entrance, scene, _ in self.rows[first:first + count]:
                scene_of_row.setdefault(entrance, set()).add(name)
        for label, entrance, _, _ in self.rows:
            neighbour = re.search(r"from (.+?)(?: \(| / |, |$)", label)
            if neighbour:
                self.assertNotIn(neighbour.group(1), scene_of_row.get(entrance, ()), label)

    def test_refusals_and_own_clock(self):
        refused = {int(e, 16) for e in re.findall(r"\{0x([0-9A-F]{4}), \"", self.text.split("kMm3dDevRefused")[1]
                                                  .split("};")[0])}
        own = {int(e, 16) for e in re.findall(r"\{0x([0-9A-F]{4}), \"", self.text.split("kMm3dDevOwnClock")[1]
                                              .split("};")[0])}
        self.assertIn(0x5040, refused)  # Deku Palace spawn 4 arrives in Deku Shrine
        self.assertEqual(own, {0x1C00, 0x1C10, 0x1C20, 0x1C40, 0xC800, 0xC810})
        self.assertFalse(own & refused)


@unittest.skipUnless(os.environ.get("TRIAEVUM_2S2H_ROOT"), "TRIAEVUM_2S2H_ROOT is not set")
class Regeneration(unittest.TestCase):
    def test_committed_catalog_regenerates_byte_identical(self):
        text, summary = generator.build(Path(os.environ["TRIAEVUM_2S2H_ROOT"]))
        self.assertEqual(summary["entrances"], 490)
        self.assertEqual(CATALOG.read_bytes().decode("utf-8"), text)


if __name__ == "__main__":
    unittest.main()
