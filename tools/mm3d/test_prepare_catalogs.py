import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / "tools/mm3d"), str(ROOT / "tools/triaevum_release")]
import prepare_catalogs as prepare
import model_catalog
from test_model_catalog import cmb, lzs
from test_water_textures import level3_romfs


class PrepareCatalogTests(unittest.TestCase):
    def test_title_roles_do_not_promote_nonwater_assets(self):
        profile = json.loads((ROOT / "titles/mm3d/catalogs/water.json").read_text())
        for name, role in (("yado_mizu", 1), ("gb_sea_00", 1), ("bh_taki_01", 4),
                           ("gb_uvwater_01", 8), ("taki_sea_01", 1), ("nm_dokutaki_01", 4)):
            self.assertEqual(model_catalog.water_role(name, profile), role, name)
        for name in ("bh_mizukemuri", "taki_tree_01", "taki_kabe_01", "water_bomb01", "dekubaba_water_",
                     "taki_kusa_01", "torch4_water01", "seahorse_00", "seafairyfish_00"):
            self.assertEqual(model_catalog.water_role(name, profile), 0, name)

    def test_manifest_receipts_and_room_catalog_reuse(self):
        profile = json.loads((ROOT / "titles/mm3d/catalogs/water.json").read_text())
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            code, romfs = root / "code.bin", root / "romfs.bin"
            code.write_bytes(b"synthetic, generator is mocked")
            room = b"ZSI\x09" + bytes(12) + cmb([("water", 8, 8, bytes(64))], profile)
            romfs.write_bytes(level3_romfs({"scenes": {"room_info.zsi": lzs(room)}}))
            generator = prepare.scene_generator()
            generator.generate = Mock(return_value={"title": "mm3d", "rooms": [{"emitters": []}], "rejected": []})
            with patch.object(prepare, "scene_generator", return_value=generator):
                one = prepare.prepare(code, romfs, root / "data")
                two = prepare.prepare(code, romfs, root / "data")
                self.assertEqual(one, two)
                self.assertEqual(generator.generate.call_count, 1)
                Path(one["scene_lights"]["path"]).write_bytes(b"tampered")
                self.assertEqual(prepare.prepare(code, romfs, root / "data"), one)
                self.assertEqual(generator.generate.call_count, 2)
                self.assertEqual(one["runtime_arguments"][0], "--water-texture-catalog")
                self.assertEqual(one["runtime_arguments"][2], "--scene-light-catalog")
                code.write_bytes(b"changed synthetic code")
                three = prepare.prepare(code, romfs, root / "data")
                self.assertNotEqual(one["scene_lights"]["path"], three["scene_lights"]["path"])
                self.assertEqual(one["water"], three["water"])

    def test_rejected_rooms_are_not_published(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            code, romfs = root / "code.bin", root / "romfs.bin"
            code.write_bytes(b"synthetic")
            romfs.write_bytes(level3_romfs({}))
            generator = Mock()
            generator.generate.return_value = {"rooms": [], "rejected": [{"reason": "bad room"}]}
            with patch.object(prepare, "scene_generator", return_value=generator):
                with self.assertRaises(ValueError): prepare.prepare(code, romfs, root / "data")
            self.assertFalse((root / "data/catalogs/mm3d/manifest.json").exists())


if __name__ == "__main__": unittest.main()
