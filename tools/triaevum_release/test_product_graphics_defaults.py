"""The title-owned default snapshot must not change generic Authentic settings."""
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

    def test_requested_presentation_and_complete_style(self):
        self.assertEqual(self.defaults["FrameRate"]["Mode"], "Interpolated2x")
        self.assertEqual(self.defaults["Camera"]["FovMultiplier"], 1.1)
        toon = self.defaults["Effects"]["Toon"]
        self.assertEqual(toon["Mode"], "PicaMaterial")
        self.assertAlmostEqual(toon["LightBandLevels"][0], 0.168, places=5)
        self.assertTrue(toon["OutlineEnabled"])
        self.assertAlmostEqual(toon["OutlineWidth"], 0.95, places=5)
        self.assertTrue(toon["CustomLightBands"])
        self.assertEqual(len(toon["LightBandLevels"]), 6)
        self.assertEqual(len(toon["LightBandThresholds"]), 5)

    def test_all_grass_sections_and_six_masks_are_preserved(self):
        grass = self.defaults["Grass"]
        self.assertEqual(grass["Quality"], "Custom")
        for section in ("Appearance", "Budget", "Generation", "LinkInteraction", "Performance", "Wind"):
            self.assertTrue(grass[section])
        self.assertEqual(grass["Performance"]["SegmentLodStartDistance"], 501)
        self.assertEqual(grass["Performance"]["SegmentLodEndDistance"], 1000)
        self.assertEqual(grass["Performance"]["LodReferenceDistance"], 1668)
        self.assertAlmostEqual(grass["Performance"]["LodStartFraction"], 1)
        self.assertAlmostEqual(grass["Performance"]["LodEndFraction"], 1)
        self.assertEqual(grass["Performance"]["DensityFadeFraction"], 1)
        self.assertAlmostEqual(grass["Performance"]["DrawFadeFraction"], 0.15)
        self.assertAlmostEqual(grass["Performance"]["TuftTransitionFraction"], 0.34)
        self.assertAlmostEqual(grass["Performance"]["FarTuftDensity"], 0.1)
        self.assertEqual(grass["Performance"]["FarTuftSpread"], 4)
        self.assertEqual(grass["Performance"]["SegmentLodSoftness"], 0.75)
        self.assertAlmostEqual(grass["Performance"]["FarDensity"], 0.62)
        self.assertAlmostEqual(grass["Appearance"]["TextureColorInfluence"], 0.75)
        self.assertAlmostEqual(grass["Appearance"]["TextureRootBrightness"], 1.01, places=5)
        self.assertAlmostEqual(grass["Appearance"]["TextureTipBrightness"], 1.74, places=5)
        self.assertFalse(grass["Appearance"]["ToonRimEnabled"])
        self.assertEqual(grass["Appearance"]["ToonRimFadeStart"], 0)
        self.assertEqual(grass["Appearance"]["ToonRimFadeEnd"], 901)
        self.assertEqual(grass["Budget"]["DrawDistance"], 50000)
        self.assertEqual(grass["Appearance"]["BladeSegments"], 5)
        self.assertEqual(grass["Generation"]["MinimumSpacing"], 0)
        interaction = grass["LinkInteraction"]
        self.assertAlmostEqual(interaction["ColliderHeightMultiplier"], 1.69, places=5)
        self.assertAlmostEqual(interaction["ColliderRadiusMultiplier"], 1.19, places=5)
        self.assertAlmostEqual(interaction["CollisionPush"], 1.66, places=5)
        self.assertAlmostEqual(interaction["VelocityResponse"], 0.87, places=5)
        self.assertEqual(interaction["VerticalMargin"], 69)
        self.assertEqual(grass["Generation"]["IndividualRandomness"], 1)
        self.assertEqual(grass["Generation"]["InstancesPerSquareMeter"], 1024)
        self.assertEqual(grass["Budget"]["MaxInstancesPerRoom"], 500000)
        self.assertEqual([source["Target"]["Rgba8Hash"] for source in grass["Sources"]],
                         ["be15aff93dfdcd88", "2321986eb9820c29", "4b8941fd174516b0",
                          "0a29e93a3b0742b3", "bd769b9ce136d73a", "d13528cd4896c851"])
        self.assertTrue(grass["Performance"]["FarTuftsEnabled"])
        self.assertEqual(grass["Performance"]["FarTuftBladeCount"], 3)

    def test_topscreen_installer_template_preserves_current_profile(self):
        root = SOURCE.parents[2]
        config = json.loads((root / "config/topscreen_ui.example.json").read_text())
        self.assertEqual(config["hud_scale"], 0.8)
        self.assertEqual((config["hud_margin_x"], config["hud_margin_y"]), (4, 1))
        self.assertEqual(config["hud_layout"], "normal")
        self.assertTrue(config["minimap_visible"])
        self.assertFalse(config["free_camera_enabled"])
        self.assertEqual(config["dpad_child"], ["view", "ocarina", "item_zr", "item_zl"])

    def test_snapshot_does_not_capture_machine_paths_or_unrequested_effects(self):
        self.assertEqual(set(self.defaults),
                         {"Preset", "SchemaVersion", "FrameRate", "Camera", "Effects", "Grass"})
        self.assertEqual(set(self.defaults["Effects"]), {"Toon"})
        source = (SOURCE / "triaevum_product_info.cpp").read_text()
        self.assertIn('document["GrassSavedPreset"] = document["Grass"]', source)

    @unittest.skipUnless(os.environ.get("TRIAEVUM_PRODUCT_TEST_EXE"), "requires a built runtime")
    def test_compiled_product_exports_the_snapshot(self):
        result = subprocess.run([os.environ["TRIAEVUM_PRODUCT_TEST_EXE"], "--product-info"],
                                capture_output=True, text=True, check=True, timeout=10)
        graphics = json.loads(result.stdout)["default_config"]["Graphics"]
        self.assertEqual(graphics["Grass"], self.defaults["Grass"])
        self.assertEqual(graphics["GrassSavedPreset"], graphics["Grass"])
        self.assertEqual(graphics["Effects"]["Toon"], self.defaults["Effects"]["Toon"])
        self.assertEqual(graphics["FrameRate"], self.defaults["FrameRate"])
        self.assertAlmostEqual(graphics["Camera"]["FovMultiplier"], 1.1, places=6)
        self.assertEqual(graphics["Effects"]["Reflections"]["Mode"], "Off")


if __name__ == "__main__":
    unittest.main()
