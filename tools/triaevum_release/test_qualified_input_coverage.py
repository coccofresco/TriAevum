import copy
import unittest
from common import load_json_object
from precompiled_variants import DEFINITIONS, add_verified_variants
from qualified_input_coverage import bind, require_coverage


class QualifiedInputCoverageTests(unittest.TestCase):
    def setUp(self):
        self.recipes = load_json_object(DEFINITIONS)
        base = self.recipes["recipes"][0]
        self.title = {
            "recipe": base["id"], "inputs": base["inputs"], "target": "current-platform",
            "plugin": {"sha256": "new-gameplay-fixes", "path": "new-title"},
            "sources": ["new-translated-source"],
            "renderer_shader_preparation": {"compiler": "new-compiler"},
            "device_pipeline_preparation": {"recipes": "new-pipelines"},
        }
        self.catalog, self.recipes = add_verified_variants(
            {"titles": [self.title]}, self.recipes, self.recipes)

    def test_restore_both_regions_families_and_current_runtime_contracts(self):
        for target in ("x86_64-pc-windows-msvc", "x86_64-unknown-linux-gnu"):
            current = copy.deepcopy(self.catalog)
            for title in current["titles"]:
                title["target"] = target
            catalog, recipes, files = bind(current, self.recipes)
            require_coverage(catalog, recipes)
            self.assertEqual(len(recipes["recipes"]), 3)
            self.assertEqual(len(files), 1)
            self.assertEqual(files[0]["role"], "input_copy_adapter")
            for title in catalog["titles"]:
                self.assertEqual(title["target"], target)
                for field in ("plugin", "sources", "renderer_shader_preparation", "device_pipeline_preparation"):
                    self.assertEqual(title[field], self.title[field])
            self.assertEqual(bind(catalog, recipes), (catalog, recipes, files))

    def test_previous_alpha2_metadata_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "lost qualified input coverage"):
            require_coverage(self.catalog, self.recipes)

    def test_family_loss_is_detected(self):
        catalog, recipes, _ = bind(self.catalog, self.recipes)
        catalog["titles"][-1].pop("data_compatibility")
        with self.assertRaisesRegex(ValueError, "changed qualified input coverage"):
            require_coverage(catalog, recipes)

    def test_canonical_change_cannot_silently_reuse_adapter(self):
        for field in ("inputs", "process"):
            recipes = copy.deepcopy(self.recipes)
            recipes["recipes"][0][field] = {}
            with self.assertRaisesRegex(ValueError, "canonical execution"):
                bind(self.catalog, recipes)
