import copy
import unittest

from tools.oot3d.native_a32_runtime.summarize_pica_shader_campaign import summarize


def fixture():
    return {
        "format": "oot3d_pica_effective_shader_inventory_v1",
        "shaders": [{"stage": stage, "source_id": source} for stage, source in
                    (("vertex", "v"), ("fragment", "f"), ("nri_fragment", "n"))],
        "native_pipeline_recipes": [{"native_pipeline_id": p, "vertex_source_id": "v",
            "fragment_source_id": "f", "nri_fragment_source_id": "n"} for p in ("p", "q")],
        "capture_import": {"draws": 4, "complete_draws": 4, "failures": 0, "frames": [
            {"scenario": "one", "draws": 2, "failures": 0, "native_pipeline_ids": ["p"]},
            {"scenario": "two", "draws": 2, "failures": 0, "native_pipeline_ids": ["p", "q"]}]}}


class CampaignTests(unittest.TestCase):
    def test_counts_overlap_and_compact_cover(self):
        source = fixture()
        original = copy.deepcopy(source)
        report = summarize(source, {"shaders": source["shaders"][:1]})
        self.assertEqual(source, original)
        self.assertEqual(report["counts"]["new_shader_modules"], 2)
        self.assertEqual(report["counts"]["native_pipelines"], 2)
        self.assertEqual(report["compact_native_pipeline_cover"],
                         [{"scenario_id": "two", "new_native_pipeline_count": 2}])
        self.assertEqual(report["scenario_ids"], ["two"])
        self.assertEqual(report["uncovered_native_pipeline_ids"], [])
        self.assertFalse(report["game_coverage_proven"])

    def test_missing_recipe_and_missing_shader_are_not_coverage(self):
        for field in ("native_pipeline_recipes", "shaders"):
            source = fixture()
            source[field].pop()
            with self.subTest(field=field), self.assertRaises(ValueError):
                summarize(source)


if __name__ == "__main__":
    unittest.main()
