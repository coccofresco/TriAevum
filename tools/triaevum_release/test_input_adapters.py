import copy
from pathlib import Path
import tempfile
import unittest

from common import atomic_write_json, sha256_file
from ctr_rom import ExtractedFile, ExtractedTitleInputs, publish_extracted_inputs
from forge_gui import match_extracted_recipe
from input_adapters import FORMAT, RECEIPT, adapt_extracted_inputs, validate_adapter
from input_copy_adapter import build_program, identity
from oot3d_region_assets import ALGORITHM, normalize_romfs
from precompiled_title_layout import artifact
from test_oot3d_region_assets import romfs_fixture


class InputAdapterTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.source = self.root / 'original'
        self.source.mkdir()
        self.canonical = b'89abcdef01234567'
        for kind, data in (('code', b'0123456789abcdef'), ('exheader', bytes(64)), ('romfs', romfs_fixture())):
            (self.source / f'{kind}.bin').write_bytes(data)
        files = {kind: ExtractedFile(self.source/f'{kind}.bin', (self.source/f'{kind}.bin').stat().st_size,
                                    sha256_file(self.source/f'{kind}.bin')) for kind in ('code', 'exheader', 'romfs')}
        self.extracted = ExtractedTitleInputs('NCSD', 0, 123, **files)
        contracts = {kind: {'bytes': item.bytes, 'sha256': item.sha256} for kind, item in files.items()}
        normalize_romfs(self.source/'romfs.bin', self.root/'expected.romfs')
        inputs = {'code': identity(self.canonical), 'exheader': contracts['exheader'],
                  'romfs': identity((self.root/'expected.romfs').read_bytes())}
        program = build_program((self.source/'code.bin').read_bytes(), self.canonical)
        self.program_path = self.root / 'recipes/adapters/copies.json'
        atomic_write_json(self.program_path, program)
        self.recipe = {'id': 'fixture-adapted', 'inputs': inputs, 'input_adapter': {
            'format': FORMAT, 'source_inputs': contracts, 'resource_algorithm': ALGORITHM,
            'code_copies': artifact(self.program_path, 'recipes/adapters/copies.json')}}
        self.output = self.root/'normalized'

    def test_no_adapter_preserves_existing_pipeline(self):
        actual, receipt = adapt_extracted_inputs(self.extracted, {'id': 'native'}, root=self.root, output=self.output)
        self.assertIs(actual, self.extracted)
        self.assertIsNone(receipt)
        self.assertFalse(self.output.exists())

    def test_import_matches_source_not_execution_identity(self):
        recipes = self.root/'recipes.json'
        atomic_write_json(recipes, {'format': 'triaevum_supported_revisions_v1', 'recipes': [self.recipe]})
        self.assertEqual(match_extracted_recipe(self.extracted, recipes), self.recipe)
        normalized, _ = adapt_extracted_inputs(self.extracted, self.recipe, root=self.root, output=self.output)
        with self.assertRaisesRegex(RuntimeError, 'does not match'):
            match_extracted_recipe(normalized, recipes)

    def test_verified_output_published_with_original_receipt_and_no_compilation(self):
        normalized, receipt = adapt_extracted_inputs(self.extracted, self.recipe, root=self.root, output=self.output)
        self.assertEqual(normalized.code.path.read_bytes(), self.canonical)
        self.assertEqual(normalized.exheader.path.read_bytes(), self.extracted.exheader.path.read_bytes())
        self.assertEqual(self.extracted.code.path.read_bytes(), b'0123456789abcdef')
        self.assertEqual(receipt['source_inputs'], self.recipe['input_adapter']['source_inputs'])
        self.assertEqual(receipt['execution_inputs'], self.recipe['inputs'])
        self.assertEqual(receipt['objects_compiled'], 0)
        published = publish_extracted_inputs(normalized, self.root/'sources')
        self.assertTrue((published.code.path.parent / RECEIPT).is_file())
        normalized, _ = adapt_extracted_inputs(self.extracted, self.recipe, root=self.root, output=self.output)
        reused = publish_extracted_inputs(normalized, self.root/'sources')
        self.assertEqual(reused, published)

    def test_corrupt_source_and_wrong_output_rejected_before_activation(self):
        self.extracted.code.path.write_bytes(bytes(16))
        with self.assertRaisesRegex(ValueError, 'source identity'):
            adapt_extracted_inputs(self.extracted, self.recipe, root=self.root, output=self.output)
        self.assertFalse(self.output.exists())
        self.extracted.code.path.write_bytes(b'0123456789abcdef')
        recipe = copy.deepcopy(self.recipe)
        recipe['inputs']['romfs']['sha256'] = '0'*64
        with self.assertRaisesRegex(ValueError, 'Normalized input identity'):
            adapt_extracted_inputs(self.extracted, recipe, root=self.root, output=self.output)
        self.assertFalse(self.output.exists())
        self.assertTrue(self.source.is_dir())

    def test_recipe_requires_matching_copy_identity_and_no_literal_payload(self):
        recipe = copy.deepcopy(self.recipe)
        recipe['inputs']['code']['sha256'] = '0'*64
        with self.assertRaisesRegex(ValueError, 'revision contracts'):
            validate_adapter(self.root, recipe)
        program = build_program(b'0123456789abcdef', self.canonical)
        program['literal'] = 'not allowed'
        atomic_write_json(self.program_path, program)
        recipe = copy.deepcopy(self.recipe)
        recipe['input_adapter']['code_copies'] = artifact(self.program_path, 'recipes/adapters/copies.json')
        with self.assertRaisesRegex(ValueError, 'COPY-only'):
            validate_adapter(self.root, recipe)

    def test_corrupt_copy_artifact_and_existing_output_rejected(self):
        self.output.mkdir()
        with self.assertRaises(FileExistsError):
            adapt_extracted_inputs(self.extracted, self.recipe, root=self.root, output=self.output)
        self.assertTrue(self.output.is_dir())
        self.program_path.write_text('{}')
        with self.assertRaisesRegex(ValueError, 'integrity validation'):
            validate_adapter(self.root, self.recipe)


if __name__ == '__main__':
    unittest.main()
