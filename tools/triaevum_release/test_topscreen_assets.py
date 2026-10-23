import json
import struct
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

from topscreen_assets import (BUNDLED_DIRECTORY, BUNDLED_FONT_DIRECTORY, HD_FONT, LINK_ANIMATIONS, TITLE,
                              UI_ATLAS, checked_ui_atlas_hd, prepare_topscreen_assets,
                              topscreen_link_animations, topscreen_ui_atlas_hd)
from tools.oot3d.decomp_support.scripts.build_topscreen_texture_override_pack import (
    build_texture_pack,
)

REPOSITORY = Path(__file__).resolve().parents[2]
SHIPPED = REPOSITORY / "runtime/triaevum_resources/topscreen/singlescreen-3.0"
SHIPPED_FONT = REPOSITORY / "runtime/triaevum_resources/topscreen/singlescreen-3.1" / HD_FONT


class TopScreenAssetsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.data = self.root / "data"
        bundled = self.root / BUNDLED_DIRECTORY
        bundled.mkdir(parents=True)
        for name in (UI_ATLAS, LINK_ANIMATIONS):
            (bundled / name).write_bytes((SHIPPED / name).read_bytes())
        (self.root / BUNDLED_FONT_DIRECTORY).mkdir(parents=True)
        (self.root / BUNDLED_FONT_DIRECTORY / HD_FONT).write_bytes(SHIPPED_FONT.read_bytes())
        self.recipe = {"title": TITLE, "inputs": {"romfs": {"sha256": "a" * 64}}}

    def prepare(self, ui_atlas_hd=None, report=lambda *_: None):
        return prepare_topscreen_assets(root=self.root, data_root=self.data, recipe=self.recipe,
                                        romfs=self.root / "romfs.bin", ui_atlas_hd=ui_atlas_hd,
                                        report=report)

    @staticmethod
    def fake_font_pack(*, font, romfs, output):
        output.write_bytes(b"validated font fixture")
        return {"fonts": [{"romfs_path": "message/eu/ltn16.qbf"}]}

    @staticmethod
    def png_header(width, height):
        return (b"\x89PNG\r\n\x1a\n" + struct.pack(">I", 13) + b"IHDR" +
                struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0) + b"fixture")

    def hd_atlas(self, width=4096, height=2048):
        path = self.root / "pack" / "tex1_512x256_1FAF9B5B2ADF6038_13_mip0.png"
        path.parent.mkdir(exist_ok=True)
        path.write_bytes(self.png_header(width, height))
        return path

    def test_shipped_assets_and_font_install_without_network(self):
        with patch("topscreen_assets.prepare_font_pack", side_effect=self.fake_font_pack) as fonts, \
                patch("verified_download.urllib.request.urlopen") as network:
            pack = self.prepare()
            network.assert_not_called()
        # The HD font ships in the package and is always prepared.
        self.assertEqual(fonts.call_args.kwargs["font"], self.root / BUNDLED_FONT_DIRECTORY / HD_FONT)
        payload = pack.read_bytes()
        self.assertEqual(struct.unpack_from("<4sIII", payload), (b"O3TU", 2, 0, 1))
        self.assertIn(b"oot3d/topscreen/3.0/ui_atlas", payload)
        self.assertEqual(topscreen_link_animations(pack).read_bytes(),
                         (SHIPPED / LINK_ANIMATIONS).read_bytes())
        self.assertTrue((pack.parent / "font_coverage.zip").is_file())
        # The HD UI atlas needs the user's HD UI texture pack.
        self.assertIsNone(topscreen_ui_atlas_hd(pack))
        receipt = json.loads((pack.parent / "import.json").read_text())
        self.assertIsNone(receipt["source"]["ui_atlas_hd_sha256"])
        with patch("topscreen_assets.build_texture_pack") as build, \
                patch("topscreen_assets.prepare_font_pack") as again:
            self.assertEqual(self.prepare(), pack)
            build.assert_not_called()
            again.assert_not_called()

    def test_modified_or_missing_shipped_asset_is_a_package_error(self):
        for relative in (BUNDLED_DIRECTORY / UI_ATLAS, BUNDLED_FONT_DIRECTORY / HD_FONT):
            original = (self.root / relative).read_bytes()
            with self.subTest(str(relative)):
                (self.root / relative).write_bytes(b"tampered")
                with self.assertRaisesRegex(ValueError, "package is incomplete or modified"):
                    self.prepare()
                (self.root / relative).unlink()
                with self.assertRaisesRegex(ValueError, relative.name):
                    self.prepare()
            (self.root / relative).write_bytes(original)

    def test_tampered_installed_animations_are_reinstalled(self):
        with patch("topscreen_assets.prepare_font_pack", side_effect=self.fake_font_pack):
            pack = self.prepare()
            topscreen_link_animations(pack).write_bytes(b"ZAR\x01 tampered")
            self.assertEqual(self.prepare(), pack)
        self.assertEqual(topscreen_link_animations(pack).read_bytes(),
                         (SHIPPED / LINK_ANIMATIONS).read_bytes())

    def test_texture_pack_atlas_becomes_the_hd_ui_atlas(self):
        messages = []
        with patch("topscreen_assets.prepare_font_pack", side_effect=self.fake_font_pack):
            native = self.prepare()
            pack = self.prepare(ui_atlas_hd=self.hd_atlas(), report=lambda _, text: messages.append(text))
        self.assertNotEqual(native, pack)
        self.assertEqual(topscreen_ui_atlas_hd(pack).read_bytes(), self.png_header(4096, 2048))
        self.assertTrue(any("HD UI texture pack" in text for text in messages))
        # A changed atlas in the pack is installed again.
        with patch("topscreen_assets.prepare_font_pack", side_effect=self.fake_font_pack):
            changed = self.prepare(ui_atlas_hd=self.hd_atlas(2048, 1024))
        self.assertEqual(topscreen_ui_atlas_hd(changed).read_bytes(), self.png_header(2048, 1024))

    def test_corrupt_font_and_new_import_version_are_reprepared(self):
        with patch("topscreen_assets.build_texture_pack", return_value=b"O3TU fixture") as build, \
                patch("topscreen_assets.prepare_font_pack", side_effect=self.fake_font_pack) as fonts:
            pack = self.prepare()
            (pack.parent / "font_coverage.zip").write_bytes(b"corrupt")
            self.prepare()
            with patch("topscreen_assets.FONT_IMPORT_VERSION", 99):
                self.prepare()
            self.assertEqual(build.call_count, 1)
            self.assertEqual(fonts.call_count, 3)
            receipt = json.loads((pack.parent / "fonts_import.json").read_text())
            self.assertEqual(receipt["source"]["font_import_version"], 99)
            self.assertTrue(receipt["runtime_enabled"])

    def test_font_failure_is_not_cached_as_success_and_can_be_retried(self):
        with patch("topscreen_assets.prepare_font_pack", side_effect=ValueError("invalid QBF")):
            with self.assertRaisesRegex(ValueError, "invalid QBF"):
                self.prepare()
        self.assertFalse(list(self.data.rglob("fonts_import.json")))
        with patch("topscreen_assets.prepare_font_pack", side_effect=self.fake_font_pack):
            self.prepare()

    def test_hd_ui_atlas_must_be_a_two_to_one_png(self):
        for name, atlas in (("square", self.png_header(2048, 2048)), ("odd", self.png_header(4000, 2000)),
                            ("text", b"not a png at all, padded to the IHDR length")):
            path = self.root / f"{name}.png"
            path.write_bytes(atlas)
            with self.subTest(name), self.assertRaises(ValueError):
                checked_ui_atlas_hd(path)
        path = self.root / "native.png"
        path.write_bytes(self.png_header(512, 256))
        self.assertEqual(checked_ui_atlas_hd(path), self.png_header(512, 256))

    def test_other_title_does_not_install_oot3d_assets(self):
        self.assertIsNone(prepare_topscreen_assets(root=self.root, data_root=self.data,
                                                   recipe={"title": "Another title"},
                                                   romfs=self.root / "other.bin"))
        self.assertFalse(self.data.exists())

    def test_builder_imports_only_the_singlescreen_atlas(self):
        header = bytearray(0x48)
        header[:4] = b"ctxb"
        struct.pack_into("<HHHH", header, 0x2c, 8, 8, 0x6752, 0x1401)
        singlescreen = self.root / "singlescreen.zip"
        prefix = "SingleScreen_3.0/EUR/Emulator/load/mods/0004000000033600/romfs/"
        with zipfile.ZipFile(singlescreen, "w") as output:
            output.writestr(prefix + "menu/custom_ui01.ctxb", header + bytes([3]) * 256)
            output.writestr("SingleScreen_3.0/EUR/Emulator/load/mods/0004000000033600/code.ips",
                            b"DO NOT IMPORT EXECUTABLE PATCH")
            output.writestr("../../escape", b"not extracted")
        pack = build_texture_pack(singlescreen_archive=singlescreen)
        self.assertEqual(struct.unpack_from("<4sIII", pack), (b"O3TU", 2, 0, 1))
        self.assertIn(b"oot3d/topscreen/3.0/ui_atlas", pack)
        self.assertNotIn(b"DO NOT IMPORT", pack)
        self.assertFalse((self.root / "escape").exists())

        # Forge's install and Switch staging: the shipped 3.0 atlas alone.
        pack = build_texture_pack(singlescreen_ui_atlas=bytes(header + bytes([3]) * 256))
        self.assertEqual(struct.unpack_from("<4sIII", pack), (b"O3TU", 2, 0, 1))
        self.assertIn(b"oot3d/topscreen/3.0/ui_atlas", pack)
        self.assertNotIn(b"oot3d/topscreen/2.1.1/", pack)
        with self.assertRaisesRegex(ValueError, "exactly one SingleScreen 3.0 source"):
            build_texture_pack()
        with self.assertRaisesRegex(ValueError, "exactly one SingleScreen 3.0 source"):
            build_texture_pack(singlescreen_archive=singlescreen, singlescreen_ui_atlas=b"x")

if __name__ == "__main__":
    unittest.main()
