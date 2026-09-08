import tempfile
import unittest
from pathlib import Path

import ctr_rom
from extracted_inputs import stage_directory
from test_ctr_rom import _build_rom


class ExtractedInputTests(unittest.TestCase):
    def fixture(self, root):
        rom = root / 'test.cci'
        _build_rom(rom)
        return ctr_rom.extract_decrypted_rom(rom, root / 'source')

    def test_identical_content_identity_and_source_preserved(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            original = self.fixture(root)
            copied = stage_directory(root / 'source', root / 'staging')
            self.assertEqual(ctr_rom._input_identity(original), ctr_rom._input_identity(copied))
            ctr_rom.publish_extracted_inputs(copied, root / 'published')
            for item in original.by_kind().values():
                self.assertTrue(item.path.is_file())
                self.assertEqual(ctr_rom._digest_file(item.path), item.sha256)

    def test_missing_and_invalid_inputs_rejected_without_output(self):
        for kind, data in [('code', b''), ('exheader', b'bad'), ('romfs', b'encrypted')]:
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                self.fixture(root)
                (root / 'source' / f'{kind}.bin').write_bytes(data)
                with self.assertRaises(ctr_rom.CtrRomError):
                    stage_directory(root / 'source', root / 'staging')
                self.assertFalse((root / 'staging').exists())
                (root / 'source' / f'{kind}.bin').unlink()
                with self.assertRaises(ctr_rom.CtrRomError):
                    stage_directory(root / 'source', root / 'staging')

    def test_existing_output_and_nested_output_preserved(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.fixture(root)
            for output in (root / 'source', root / 'source' / 'nested'):
                with self.assertRaises(ctr_rom.CtrRomError):
                    stage_directory(root / 'source', output)
            (root / 'staging').mkdir()
            (root / 'staging' / 'keep').write_text('keep')
            with self.assertRaises(FileExistsError):
                stage_directory(root / 'source', root / 'staging')
            self.assertEqual((root / 'staging' / 'keep').read_text(), 'keep')
