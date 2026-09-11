import unittest
from compare_grass_presets import gpu_statistics


class GrassPresetComparisonTests(unittest.TestCase):
    def test_rejects_missing_grass_work(self):
        frames = [{"presentation_vsync": False, "gpu": {
            "frame_ms": 3, "grass_ms": 0, "native_pica_ms": 1}}] * 10
        with self.assertRaises(ValueError):
            gpu_statistics({"frames": frames}, 1)

    def test_excludes_warmup_and_pending_queries(self):
        frames = [{"presentation_vsync": False, "gpu": {
            "frame_ms": n, "grass_ms": n / 2, "native_pica_ms": 1}} for n in range(10)]
        self.assertEqual(gpu_statistics({"frames": frames}, 3)["frame_ms"], 4.5)
        self.assertEqual(gpu_statistics({"frames": frames[3:7]}, 0, 0)["frame_ms"], 4.5)

    def test_rejects_vsync_and_missing_timestamps(self):
        frames = [{"presentation_vsync": True, "gpu": {}}] * 8
        with self.assertRaises(ValueError):
            gpu_statistics({"frames": frames}, 1)
        for frame in frames:
            frame["presentation_vsync"] = False
        with self.assertRaises(ValueError):
            gpu_statistics({"frames": frames}, 1)
