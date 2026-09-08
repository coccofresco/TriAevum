import copy
from pathlib import Path
import tempfile
import unittest

from common import atomic_write_json, load_json_object
from ctr_rom import ExtractedFile, ExtractedTitleInputs
from forge_gui import match_extracted_recipe
from precompiled_variants import DEFINITIONS, add_verified_variants


class PrecompiledVariantTests(unittest.TestCase):
    def setUp(self):
        self.definitions = load_json_object(DEFINITIONS)
        self.base, self.variant = copy.deepcopy(self.definitions['recipes'])
        self.recipes = {'recipes': [self.base]}
        self.title = {'recipe': self.base['id'], 'inputs': self.base['inputs'],
                      'plugin': {'path': 'titles/existing/triaevum_title_aot.dll'},
                      'sources': ['existing-build-source', 'existing-translated-source'],
                      'abi_version': 2, 'translator_identity_sha256': 'a'*64}
        self.catalog = {'titles': [self.title]}

    def test_one_variant_reuses_title_and_keeps_legacy_recipe(self):
        catalog, recipes = add_verified_variants(self.catalog, self.recipes, self.definitions)
        self.assertEqual(len(catalog['titles']), 2)
        self.assertEqual(len(recipes['recipes']), 2)
        self.assertEqual(recipes['recipes'][0], self.base)
        self.assertEqual(catalog['titles'][0], self.title)
        for field in ('plugin', 'sources', 'abi_version', 'translator_identity_sha256'):
            self.assertEqual(catalog['titles'][1][field], self.title[field])
        self.assertNotIn('input_adapter', recipes['recipes'][1])
        self.assertEqual(len(self.catalog['titles']), 1)
        self.assertEqual(add_verified_variants(catalog, recipes, self.definitions), (catalog, recipes))

    def test_both_catalogue_editions_share_one_exact_input_contract(self):
        self.assertEqual(self.variant['catalogue_records'], ['no-intro-3ds-0004', 'no-intro-3ds-1168'])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'recipes.json'
            atomic_write_json(path, self.definitions)
            inputs = {k: ExtractedFile(Path(k), v['bytes'], v['sha256'])
                      for k, v in self.variant['inputs'].items()}
            extracted = ExtractedTitleInputs('NCSD', 0, 0x0004000000033600, **inputs)
            self.assertEqual(match_extracted_recipe(extracted, path)['id'], self.variant['id'])
            inputs['exheader'] = ExtractedFile(Path('exheader'), 2048, '0'*64)
            with self.assertRaisesRegex(RuntimeError, 'does not match'):
                match_extracted_recipe(ExtractedTitleInputs('NCSD', 0, 0x0004000000033600, **inputs), path)

    def test_changed_code_romfs_process_and_adapter_rejected(self):
        for change in ('code', 'romfs', 'process', 'input_adapter', 'optional_inputs'):
            definitions = copy.deepcopy(self.definitions)
            variant = definitions['recipes'][1]
            if change in ('code', 'romfs'):
                variant['inputs'][change]['sha256'] = '0'*64
            else:
                variant[change] = {}
            with self.subTest(change=change), self.assertRaises(ValueError):
                add_verified_variants(self.catalog, self.recipes, definitions)

    def test_unknown_base_conflict_and_duplicate_identity_rejected(self):
        with self.assertRaisesRegex(ValueError, 'base recipe'):
            add_verified_variants({'titles': []}, self.recipes, self.definitions)
        catalog, recipes = add_verified_variants(self.catalog, self.recipes, self.definitions)
        catalog['titles'][1]['plugin']['path'] = 'wrong.dll'
        with self.assertRaisesRegex(ValueError, 'conflicts'):
            add_verified_variants(catalog, recipes, self.definitions)
        recipes = copy.deepcopy(self.recipes)
        recipes['recipes'].append({**self.variant, 'id': 'unnecessary-duplicate'})
        with self.assertRaisesRegex(ValueError, 'Duplicate input triplet'):
            add_verified_variants(self.catalog, recipes, self.definitions)

    def test_existing_usa_adapter_is_not_rebuilt_or_changed(self):
        usa = {'id': 'usa', 'inputs': {'code': {'sha256': 'f'*64}}, 'input_adapter': {'existing': True}}
        self.recipes['recipes'].append(copy.deepcopy(usa))
        self.catalog['titles'].append({'recipe': 'usa', 'inputs': usa['inputs'], 'input_adapter': usa['input_adapter']})
        catalog, recipes = add_verified_variants(self.catalog, self.recipes, self.definitions)
        self.assertEqual(recipes['recipes'][1], usa)
        self.assertEqual(catalog['titles'][1], self.catalog['titles'][1])


if __name__ == '__main__':
    unittest.main()
