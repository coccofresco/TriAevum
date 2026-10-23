"""A new installation starts from the Enhanced preset plus the title's grass sources."""
import json
import os
from pathlib import Path
import subprocess
import unittest

SOURCE = Path(__file__).resolve().parents[1] / "oot3d/native_game_runtime"


class ProductGraphicsDefaultsTests(unittest.TestCase):
    def setUp(self):
        literal = (SOURCE / "triaevum_product_graphics.inc").read_text()
        self.defaults = json.loads(literal.removeprefix('R"json(').strip().removesuffix(')json"'))

    def test_title_snapshot_holds_only_the_grass_source_rules(self):
        # Every other default is the shared renderer's Enhanced preset.
        self.assertEqual(self.defaults, {"Grass": {"Sources": self.defaults["Grass"]["Sources"]}})
        self.assertEqual([source["Target"]["Rgba8Hash"] for source in self.defaults["Grass"]["Sources"]],
                         ["be15aff93dfdcd88", "2321986eb9820c29", "4b8941fd174516b0",
                          "0a29e93a3b0742b3", "bd769b9ce136d73a", "d13528cd4896c851",
                          "98d219f9acce7e31", "cc0f3ad3756aa28b", "7a41e65008d2ea50",
                          "abc341818083dec7", "a580b32083237f97", "887ffba709e21b96",
                          "fa7cfd29f3dcdaba", "c62b9d278e8bd285", "a35e3fe6f83ca9c2",
                          "d19ab95b18fb3495", "a475bf5c5bbd4ff8", "4e80b26cfb20ea87",
                          "72471d7a6540e0cc", "c09d75caf61cc19c", "62d5ae1053b31e2e",
                          "7e5075b79d5c5dcd", "86f3d2415641fc84", "14dd64ec10e5bbd6",
                          "8bbe4b35b0af27a9"])
        source = (SOURCE / "triaevum_product_info.cpp").read_text()
        self.assertIn("Preset(GraphicsPreset::Enhanced)", source)
        self.assertIn('document["GrassSavedPreset"] = document["Grass"]', source)

    def test_topscreen_installer_template_preserves_current_profile(self):
        root = SOURCE.parents[2]
        config = json.loads((root / "config/topscreen_ui.example.json").read_text())
        self.assertEqual(config["hud_scale"], 0.8)
        self.assertEqual((config["hud_margin_x"], config["hud_margin_y"]), (4, 1))
        self.assertEqual(config["hud_layout"], "normal")
        self.assertTrue(config["minimap_visible"])
        self.assertFalse(config["free_camera_enabled"])
        self.assertEqual(config["dpad_child"], ["view", "ocarina", "item_zr", "item_zl"])

    @unittest.skipUnless(os.environ.get("TRIAEVUM_PRODUCT_TEST_EXE"), "requires a built runtime")
    def test_compiled_product_exports_enhanced_with_the_grass_sources(self):
        result = subprocess.run([os.environ["TRIAEVUM_PRODUCT_TEST_EXE"], "--product-info"],
                                capture_output=True, text=True, check=True, timeout=10)
        graphics = json.loads(result.stdout)["default_config"]["Graphics"]
        self.assertEqual(graphics["Preset"], "Enhanced")
        self.assertEqual(graphics["FrameRate"], {"Mode": "Interpolated2x"})
        self.assertAlmostEqual(graphics["Camera"]["FovMultiplier"], 1.1, places=6)
        self.assertEqual(graphics["Effects"]["SurfaceLighting"], "PerPixelPBR")
        self.assertEqual(graphics["Effects"]["DirectionalShadows"]["Mode"], "VirtualShadowMaps")
        self.assertEqual(graphics["Effects"]["PointShadows"]["Mode"], "CubeMaps")
        self.assertEqual(graphics["Effects"]["AO"]["Mode"], "MXAO")
        self.assertEqual(graphics["Effects"]["GlobalIllumination"]["Mode"], "Off")
        self.assertEqual(graphics["Effects"]["Reflections"]["Mode"], "Off")
        self.assertEqual(graphics["Effects"]["Toon"]["Mode"], "Off")
        self.assertEqual(graphics["Grass"]["Quality"], "Custom")
        self.assertEqual(len(graphics["Grass"]["Sources"]), 25)
        self.assertEqual(graphics["GrassSavedPreset"], graphics["Grass"])
        self.assertEqual((graphics["Output"]["Width"], graphics["Output"]["Height"]), (1280, 720))


if __name__ == "__main__":
    unittest.main()
