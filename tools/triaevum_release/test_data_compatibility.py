import copy
import json
from pathlib import Path
import struct
import tempfile
import unittest

from common import sha256_file
from ctr_rom import ExtractedFile, ExtractedTitleInputs
from data_compatibility import FORMAT, identity, verify, expected
from romfs_identity import fingerprint
from oot3d_region_assets import normalize_romfs
from test_oot3d_region_assets import romfs_fixture
from forge_gui import match_extracted_recipe
from forge import verify_sources, HashCache, ForgeError
from build_content_families import qualify


class ContentFamilyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.paths = {kind: self.root / f'{kind}.bin' for kind in ('code', 'exheader', 'romfs')}
        self.paths['code'].write_bytes(b'synthetic code and initialized data')
        self.paths['exheader'].write_bytes(bytes(2048))
        source = self.root / 'source.romfs'
        source.write_bytes(romfs_fixture())
        normalize_romfs(source, self.paths['romfs'])
        self.recipe = {'id': 'synthetic', 'inputs': {
            k: {'bytes': p.stat().st_size, 'sha256': sha256_file(p)} for k, p in self.paths.items()}}
        family = identity(self.paths)
        self.recipe['data_compatibility'] = {'format': FORMAT, 'source': family, 'execution': family}

    def extracted(self):
        return ExtractedTitleInputs('NCSD', 0, 0, **{
            k: ExtractedFile(p, p.stat().st_size, sha256_file(p)) for k, p in self.paths.items()})

    def test_signature_and_storage_flags_are_not_process_data(self):
        header = bytearray(self.paths['exheader'].read_bytes())
        header[0xd] = 3
        header[0x400:0x600] = b'S'*512
        self.paths['exheader'].write_bytes(header)
        verify(self.recipe, self.paths, phase='source')
        verified, _ = verify_sources(self.recipe, code_path=self.paths['code'],
                                    exheader_path=self.paths['exheader'], romfs_path=self.paths['romfs'],
                                    cache=HashCache(self.root/'cache.json'))
        self.assertNotEqual(verified['exheader'].sha256, self.recipe['inputs']['exheader']['sha256'])
        header[0x28] = 1
        self.paths['exheader'].write_bytes(header)
        with self.assertRaisesRegex(ValueError, 'exheader'):
            verify(self.recipe, self.paths, phase='source')

    def test_physical_layout_and_ivfc_wrapper_independent(self):
        before = fingerprint(self.paths['romfs'])
        data = bytearray(self.paths['romfs'].read_bytes())
        base = struct.unpack_from('<I', data, 36)[0]
        data[base:base] = bytes(32)
        struct.pack_into('<I', data, 36, base+32)
        self.paths['romfs'].write_bytes(b'IVFC'+bytes(4092)+data)
        self.assertEqual(before, fingerprint(self.paths['romfs']))
        recipe_file = self.root/'recipes.json'
        recipe_file.write_text(json.dumps({'format':'triaevum_supported_revisions_v1', 'recipes':[self.recipe]}))
        self.assertEqual(match_extracted_recipe(self.extracted(), recipe_file), self.recipe)

    def test_payload_changes_rejected(self):
        data = bytearray(self.paths['romfs'].read_bytes())
        data[-1] ^= 1
        self.paths['romfs'].write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'romfs'):
            verify(self.recipe, self.paths, phase='execution')

    def test_code_changes_rejected(self):
        self.paths['code'].write_bytes(b'other code')
        with self.assertRaisesRegex(ValueError, 'code'):
            verify(self.recipe, self.paths, phase='execution')

    def test_lookup_corruption_rejected_even_with_identical_payload(self):
        data = bytearray(self.paths['romfs'].read_bytes())
        offset = struct.unpack_from('<I', data, 4)[0]
        struct.pack_into('<I', data, offset, 0xffffffff)
        self.paths['romfs'].write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'lookup'):
            fingerprint(self.paths['romfs'])

    def test_family_cannot_change_code_binding(self):
        bad = copy.deepcopy(self.recipe)
        bad['data_compatibility']['source']['code']['sha256'] = 'a'*64
        with self.assertRaisesRegex(ValueError, 'compiled code'):
            expected(bad, 'source')

    def test_publisher_requires_original_exact_inputs(self):
        self.assertEqual(qualify(self.recipe, self.root, self.root), self.recipe)
        self.paths['exheader'].write_bytes(b'S'*2048)
        with self.assertRaisesRegex(ValueError, 'Unverified publisher'):
            qualify(self.recipe, self.root, self.root)

    def test_legacy_recipe_stays_strict(self):
        del self.recipe['data_compatibility']
        self.paths['exheader'].write_bytes(b'S'*2048)
        with self.assertRaisesRegex(ForgeError, 'SHA-256 mismatch'):
            verify_sources(self.recipe, code_path=self.paths['code'],
                           exheader_path=self.paths['exheader'], romfs_path=self.paths['romfs'],
                           cache=HashCache(self.root/'cache.json'))
