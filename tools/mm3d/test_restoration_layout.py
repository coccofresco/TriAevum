# SPDX-License-Identifier: GPL-2.0-or-later
"""Tests of the Restoration layout recipe tool. The real-data tests need the
user's EUR dump and the Project Restoration release files and are skipped
without them (TRIAEVUM_MM_TEST_ROMFS, TRIAEVUM_MM_TEST_RESTORATION)."""
import json
import os
from pathlib import Path
import random
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parent))
import restoration_layout as rl


def gar(members, types=("unknown", "mfpk", "mfl", "ctxb")):
    return rl.emit_gar({"hash": "jenkins", "types": list(types), "align": 4, "members": members})


class GarTests(unittest.TestCase):
    def test_round_trip(self):
        members = [("A.mfpk", b"MFPK" + bytes(60)), ("B.mfl", b"MFL " + bytes(12)), ("C.mfl", bytes(32)),
                   ("t.ctxb", b"ctxb" + bytes(28))]
        data = gar(members)
        model = rl.parse_gar(data)
        self.assertEqual(model["members"], members)
        self.assertEqual(model["types"], ["unknown", "mfpk", "mfl", "ctxb"])
        self.assertEqual(rl.emit_gar(model), data)

    def test_non_canonical_layout_is_refused(self):
        data = bytearray(gar([("A.mfl", bytes(16)), ("B.mfl", bytes(16))]))
        data += b"\0" * 4  # trailing padding the canonical emitter never writes
        data[4:8] = len(data).to_bytes(4, "little")
        with self.assertRaisesRegex(ValueError, "canonical"):
            rl.parse_gar(bytes(data))
        with self.assertRaisesRegex(ValueError, "header"):
            rl.parse_gar(b"GAR\x01" + bytes(28))
        with self.assertRaisesRegex(ValueError, "not declared"):
            gar([("A.bin", b"")], types=("unknown",))


class BpsTests(unittest.TestCase):
    def test_hand_vector(self):
        # Header, TargetRead "ab" (action (2-1)<<2|1 = 5 -> varint 0x85),
        # SourceRead 2 (action 4 -> 0x84), trailer CRCs.
        body = b"BPS1" + bytes([0x84, 0x84, 0x80]) + bytes([0x85]) + b"ab" + bytes([0x84])
        source, target = b"xxyy", b"abyy"
        body += zlib.crc32(source).to_bytes(4, "little") + zlib.crc32(target).to_bytes(4, "little")
        patch = body + zlib.crc32(body).to_bytes(4, "little")
        self.assertEqual(rl.bps_apply(patch, source), target)

    def test_encode_apply_vectors(self):
        rng = random.Random(7)
        source = bytes(rng.randrange(256) for _ in range(3000))
        cases = [b"", source, source[:100] + b"inserted" + source[100:], source[500:] + source[:500],
                 b"abcd" * 300, bytes(rng.randrange(256) for _ in range(700))]
        for target in cases:
            patch = rl.bps_encode(source, target)
            self.assertEqual(rl.bps_apply(patch, source), target)
            self.assertEqual(rl.bps_encode(source, target), patch)  # deterministic
        # Moved data is copied, not stored: the patch stays small.
        self.assertLess(len(rl.bps_encode(source, source[500:] + source[:500])), 64)

    def test_crc_and_source_negatives(self):
        source, target = b"0123456789" * 10, b"0123456789" * 9 + b"changed!!!"
        patch = bytearray(rl.bps_encode(source, target))
        with self.assertRaisesRegex(ValueError, "source mismatch"):
            rl.bps_apply(bytes(patch), source[:-1] + b"X")
        corrupt = bytearray(patch)
        corrupt[-5] ^= 1  # the target CRC: the patch checksum catches it first
        with self.assertRaisesRegex(ValueError, "checksum"):
            rl.bps_apply(bytes(corrupt), source)
        with self.assertRaisesRegex(ValueError, "Not a BPS"):
            rl.bps_apply(b"UPS1" + bytes(12), source)


class RecipeTests(unittest.TestCase):
    def synthetic(self):
        play = gar([("Play.mfpk", b"P" * 16), ("Same.mfl", b"S" * 64), ("Mod.mfl", b"M" * 80),
                    ("Moved.mfl", b"V" * 40), ("t.ctxb", b"T" * 32)])
        project = b"MFPJ" + b"\1" * 60
        release = {
            "layout/Joker.Main.Play.gar": gar([("Play.mfpk", b"P" * 16), ("Same.mfl", b"S" * 64),
                                                ("Mod.mfl", b"M" * 40 + b"mod" + b"M" * 37)]),
            "layout/Joker.Main.New.gar": gar([("New.mfpk", b"N" * 16), ("Moved.mfl", b"V" * 20 + b"v" * 20),
                                               ("Own.mfl", b"O" * 24)]),
            "layout/Joker.mfpj": project + b"new archive",
        }
        return play, project, release

    def test_build_and_reconstruct(self):
        play, project, release = self.synthetic()
        recipe, files = rl.build(play, project, release)
        dump = {"layout/Joker.Main.Play.gar": play, "layout/Joker.mfpj": project}
        result = rl.reconstruct(recipe, dump.get, files.__getitem__)
        self.assertEqual(result, release)
        kinds = {m["name"]: next(iter(m["source"])) for o in recipe["outputs"] for m in o.get("members", [])}
        self.assertEqual(kinds, {"Play.mfpk": "dump", "Same.mfl": "dump", "Mod.mfl": "patch",
                                 "New.mfpk": "ship", "Moved.mfl": "patch", "Own.mfl": "ship"})
        # Idempotent: the same inputs give the same recipe and files.
        self.assertEqual(rl.build(play, project, release), (recipe, files))
        self.assertEqual(rl.ship_members(recipe, release),
                         {p: b for p, b in files.items() if "/members/" in p})

    def test_dump_errors_are_clear(self):
        play, project, release = self.synthetic()
        recipe, files = rl.build(play, project, release)
        with self.assertRaisesRegex(ValueError, "has no layout/Joker.Main.Play.gar"):
            rl.reconstruct(recipe, {}.get, files.__getitem__)
        other = gar([("Play.mfpk", b"P" * 16), ("Same.mfl", b"S" * 63 + b"!"), ("Mod.mfl", b"M" * 80),
                     ("Moved.mfl", b"V" * 40), ("t.ctxb", b"T" * 32)])
        with self.assertRaisesRegex(ValueError, "Same.mfl does not match"):
            rl.reconstruct(recipe, {"layout/Joker.Main.Play.gar": other, "layout/Joker.mfpj": project}.get,
                           files.__getitem__)
        missing = gar([("Play.mfpk", b"P" * 16), ("Same.mfl", b"S" * 64)])
        with self.assertRaisesRegex(ValueError, "has no member"):
            rl.reconstruct(recipe, {"layout/Joker.Main.Play.gar": missing, "layout/Joker.mfpj": project}.get,
                           files.__getitem__)
        with self.assertRaisesRegex(ValueError, "differs from the EUR release"):
            rl.reconstruct(recipe, {"layout/Joker.Main.Play.gar": play, "layout/Joker.mfpj": project + b"x"}.get,
                           files.__getitem__)

    def test_committed_recipe_is_consistent(self):
        recipe = json.loads((rl.RECIPE_DIR / rl.RECIPE_NAME).read_text(encoding="utf-8"))
        self.assertEqual(recipe["format"], rl.FORMAT)
        expected = rl.fingerprinted_inputs()
        self.assertEqual({o["path"]: o["sha256"] for o in recipe["outputs"]}, expected)
        for path, digest in recipe["files"].items():
            if "/patches/" in path:
                self.assertEqual(rl.sha256((rl.RECIPE_DIR / path).read_bytes()), digest, path)
            else:
                self.assertTrue(path.startswith("layout/members/"), path)
        shipped = [m["name"] for o in recipe["outputs"] for m in o.get("members", []) if "ship" in m["source"]]
        self.assertEqual(sorted(shipped), sorted([
            "AActionIconBase.mfl", "Hud2.mfl", "HudDpad.mfl", "HudItemIconSet.mfl", "ItemIconBase.mfl",
            "New.mfpk", "hud_patrs01_action_text.ctxb"]))
        # Nothing archive-shaped is committed as package data.
        for path in rl.RECIPE_DIR.rglob("*"):
            self.assertNotIn(path.suffix, (".gar", ".mfpj", ".mfl", ".ctxb"), path)


ROMFS = os.environ.get("TRIAEVUM_MM_TEST_ROMFS")
RESTORATION = os.environ.get("TRIAEVUM_MM_TEST_RESTORATION")


@unittest.skipUnless(ROMFS and RESTORATION, "needs the user's EUR dump and the Restoration release files")
class RealDataTests(unittest.TestCase):
    def test_check_mode_reconstructs_the_fingerprinted_inputs(self):
        self.assertEqual(rl.main(["check", "--romfs", ROMFS, "--restoration", RESTORATION]), 0)

    def test_generate_is_idempotent_against_the_committed_recipe(self):
        with tempfile.TemporaryDirectory() as temporary:
            self.assertEqual(rl.main(["generate", "--romfs", ROMFS, "--restoration", RESTORATION,
                                      "--out", temporary]), 0)
            for path in Path(temporary).rglob("*"):
                if path.is_file():
                    relative = path.relative_to(temporary)
                    self.assertEqual(path.read_bytes(), (rl.RECIPE_DIR / relative).read_bytes(), relative)

    def test_release_archives_round_trip(self):
        for name, data in rl.release_files(Path(RESTORATION)).items():
            if name.endswith(".gar"):
                self.assertEqual(rl.emit_gar(rl.parse_gar(data)), data)

    def test_check_negative_with_a_changed_patch(self):
        with tempfile.TemporaryDirectory() as temporary:
            copy = Path(temporary)
            for path in rl.RECIPE_DIR.rglob("*"):
                if path.is_file():
                    (copy / path.relative_to(rl.RECIPE_DIR)).parent.mkdir(parents=True, exist_ok=True)
                    (copy / path.relative_to(rl.RECIPE_DIR)).write_bytes(path.read_bytes())
            patch = copy / "layout/patches/Main.New.Hud.mfl.bps"
            patch.write_bytes(patch.read_bytes()[:-1] + b"\0")
            with self.assertRaises(SystemExit) as raised:
                rl.main(["check", "--romfs", ROMFS, "--restoration", RESTORATION, "--recipe", str(copy)])
            self.assertIn("changed", str(raised.exception))


if __name__ == "__main__":
    unittest.main()
