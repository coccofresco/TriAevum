import hashlib
import io
import struct
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

from topscreen_assets import acquire_archive, prepare_topscreen_assets
from tools.oot3d.decomp_support.scripts.build_topscreen_texture_override_pack import (
    LANGUAGES, TEXTURES, PROFILE_TEXTURES, build_texture_pack,
)


class TopScreenAssetsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.data = self.root / "data"
        self.archive_bytes = b"official archive fixture"
        self.contract = {"sha256": hashlib.sha256(self.archive_bytes).hexdigest(),
                         "bytes": len(self.archive_bytes), "url": "https://example.test/archive"}
        self.recipe = {"inputs": {"romfs": {"sha256": "a" * 64}},
                       "optional_inputs": {"topscreen_2_1_1_archive": self.contract}}

    def test_local_import_then_verified_cache_without_archive_or_network(self):
        archive = self.root / "topscreen211.zip"
        archive.write_bytes(self.archive_bytes)
        with patch("topscreen_assets.build_texture_pack", return_value=b"O3TU fixture") as build, \
                patch("topscreen_assets.urllib.request.urlopen") as network:
            pack = prepare_topscreen_assets(root=self.root, data_root=self.data,
                                           recipe=self.recipe, romfs=self.root / "romfs.bin")
            build.assert_called_once_with(archive=archive, original_romfs_image=self.root / "romfs.bin")
            archive.unlink()
            self.assertEqual(prepare_topscreen_assets(root=self.root, data_root=self.data,
                             recipe=self.recipe, romfs=self.root / "romfs.bin"), pack)
            self.assertEqual(build.call_count, 1)
            network.assert_not_called()

    def test_corrupt_local_archive_rejected_before_import(self):
        (self.root / "topscreen211.zip").write_bytes(b"bad")
        with patch("topscreen_assets.build_texture_pack") as build:
            with self.assertRaisesRegex(ValueError, "failed verification"):
                prepare_topscreen_assets(root=self.root, data_root=self.data,
                                        recipe=self.recipe, romfs=self.root / "romfs.bin")
            build.assert_not_called()

    def test_download_verified_atomically_and_oversize_rejected(self):
        for payload, valid in ((self.archive_bytes + b"extra", False), (self.archive_bytes, True)):
            response = io.BytesIO(payload)
            response.geturl = lambda: "https://example.test/archive"
            with patch("topscreen_assets.urllib.request.urlopen", return_value=response):
                if valid:
                    path = acquire_archive(self.root, self.data, self.contract, lambda *_: None)
                    self.assertEqual(path.read_bytes(), self.archive_bytes)
                else:
                    with self.assertRaisesRegex(ValueError, "size or time limit"):
                        acquire_archive(self.root, self.data, self.contract, lambda *_: None)
                    self.assertFalse((self.data / "downloads/topscreen211.zip").exists())
            self.assertFalse(list(self.data.rglob("*.partial")))

    def test_other_title_does_not_acquire_oot3d_assets(self):
        with patch("topscreen_assets.urllib.request.urlopen") as network:
            self.assertIsNone(prepare_topscreen_assets(root=self.root, data_root=self.data,
                              recipe={}, romfs=self.root / "other.bin"))
            network.assert_not_called()

    def test_builder_imports_only_native_textures_not_executable_mod(self):
        original = self.root / "romfs"
        archive = self.root / "fixture.zip"
        header = bytearray(0x48)
        header[:4] = b"ctxb"
        struct.pack_into("<HHHH", header, 0x2c, 8, 8, 0x6752, 0x1401)
        with zipfile.ZipFile(archive, "w") as output:
            for language in LANGUAGES:
                for name in TEXTURES:
                    relative = f"menu/{language}/{name}"
                    path = original / relative
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(header + bytes(256))
                    output.writestr("TopScreenMod/romfs/" + relative, header + bytes([1]) * 256)
            for name, _ in PROFILE_TEXTURES:
                output.writestr("TopScreenMod/romfs/menu/" + name, header + bytes([2]) * 256)
            output.writestr("TopScreenMod/code.ips", b"DO NOT IMPORT EXECUTABLE PATCH")
            output.writestr("../../escape", b"not extracted")
        pack = build_texture_pack(archive=archive, original_romfs=original)
        self.assertEqual(struct.unpack_from("<4sIII", pack), (b"O3TU", 2, 1, 2))
        self.assertNotIn(b"DO NOT IMPORT", pack)
        self.assertFalse((self.root / "escape").exists())


if __name__ == "__main__":
    unittest.main()
