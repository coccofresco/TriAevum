import hashlib
import json
import re
import tempfile
import unittest
import zipfile
from dataclasses import replace
from pathlib import Path

from romfs_mods import ArchiveContract
from ui_texture_pack import (LANGUAGE_FOLDERS, TEXTURE_FOLDER, UI_ATLAS_HD_TEXTURE, VANILLA_UI_HUD,
                             extract_archive, import_extracted, pack_contract, pack_status,
                             prepare_ui_texture_pack, prepared_pack, prepared_path, ui_atlas_hd)

REPOSITORY = Path(__file__).resolve().parents[2]
LANGUAGE_SOURCE = REPOSITORY / "tools/oot3d/native_game_runtime/oot3d_game_language.cpp"
TEXTURES = {
    UI_ATLAS_HD_TEXTURE: b"\x89PNG atlas",
    "UI/tex1_512x256_BB00B25B152582B6_4_mip0.png": b"\x89PNG common",
    "UI/01_US_ENGLISH/tex1_512x256_BB00B25B152582B6_4_mip0.png": b"\x89PNG us english",
    "UI/ITALIAN/tex1_512x256_8FE4BEB04BC6FBFE_4_mip0.png": b"\x89PNG italian",
    "BOSH RUSH/tex1_1024x256_20E7AFFF1BE07C10_12_mip0.png": b"\x89PNG boss",
}


def fixture_pack():
    return replace(VANILLA_UI_HUD, members={
        name: (len(data), hashlib.sha256(data).hexdigest()) for name, data in TEXTURES.items()})


class UiTexturePackTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.data = self.root / "data"
        self.pack = fixture_pack()
        self.archive = self.root / "oot3d_vanilla_ui_hud_32.zip"
        with zipfile.ZipFile(self.archive, "w", compression=zipfile.ZIP_DEFLATED) as output:
            for name, data in TEXTURES.items():
                output.writestr(f"{self.pack.archive_root}/{TEXTURE_FOLDER}/{name}", data)
            output.writestr(f"{self.pack.archive_root}/load/mods/0004000000033500/romfs/message/sys8.qbf",
                            b"not used")
            output.writestr(f"{self.pack.archive_root}/READ ME [INSTRUCTIONS].txt", b"readme")
        data = self.archive.read_bytes()
        self.contract = ArchiveContract(self.pack.archive_kind, self.archive.name, len(data),
                                        hashlib.sha256(data).hexdigest())
        self.recipe = {"optional_inputs": {self.pack.archive_kind: {
            "name": self.contract.name, "bytes": self.contract.bytes, "sha256": self.contract.sha256}}}

    def test_language_folders_and_common_textures_are_sorted_apart(self):
        self.assertEqual(str(prepared_path("UI/08_EU_ITALIAN/a.png")), "languages/08_EU_ITALIAN/a.png")
        self.assertEqual(str(prepared_path("UI/GERMAN/a.png")), "languages/GERMAN/a.png")
        self.assertEqual(str(prepared_path("UI/a.png")), "common/UI/a.png")
        self.assertEqual(str(prepared_path("BOSH RUSH/a.png")), "common/BOSH RUSH/a.png")
        # Every catalogued folder below UI/ is a known language folder.
        for name in VANILLA_UI_HUD.members:
            parts = Path(name).parts
            if len(parts) == 3:
                self.assertIn(parts[1], LANGUAGE_FOLDERS, name)

    def test_runtime_language_mapping_names_only_catalogued_folders(self):
        # The runtime picks folders by the boot language; each must exist in the pack.
        source = LANGUAGE_SOURCE.read_text(encoding="utf-8")
        body = source[source.index("UiTexturePackLanguageFolders(uint8_t"):]
        body = body[:body.index("\n}\n")]
        named = set(re.findall(r'"([0-9A-Z_]+)"', body))
        catalogued = {Path(name).parts[1] for name in VANILLA_UI_HUD.members if len(Path(name).parts) == 3}
        self.assertEqual(catalogued, set(LANGUAGE_FOLDERS))
        self.assertEqual(named - {"09_EU_DUTCH"}, set(LANGUAGE_FOLDERS))

    def test_archive_extracts_only_the_catalogued_textures(self):
        directory = extract_archive(self.archive, self.data, self.contract, self.pack)
        self.assertEqual(prepared_pack(self.data, self.pack), directory)
        self.assertEqual((directory / "languages/01_US_ENGLISH/tex1_512x256_BB00B25B152582B6_4_mip0.png")
                         .read_bytes(), b"\x89PNG us english")
        self.assertEqual((directory / "common/UI/tex1_512x256_BB00B25B152582B6_4_mip0.png").read_bytes(),
                         b"\x89PNG common")
        self.assertEqual(ui_atlas_hd(directory).read_bytes(), b"\x89PNG atlas")
        self.assertFalse(list(directory.rglob("*.qbf")))
        self.assertFalse(list(directory.rglob("*.txt")))
        receipt = json.loads((directory / "source.json").read_text())
        self.assertEqual(receipt["origin"], f"archive:{self.contract.sha256}")
        self.assertFalse(receipt["redistributable"])
        # A changed texture invalidates the prepared pack.
        (directory / "common/UI/tex1_512x256_BB00B25B152582B6_4_mip0.png").write_bytes(b"tampered")
        self.assertIsNone(prepared_pack(self.data, self.pack))

    def test_a_different_archive_is_rejected(self):
        wrong = replace(self.contract, sha256="0" * 64)
        with self.assertRaisesRegex(ValueError, "failed verification"):
            extract_archive(self.archive, self.data, wrong, self.pack)
        changed = replace(self.pack, members={**self.pack.members, UI_ATLAS_HD_TEXTURE: (3, "0" * 64)})
        with self.assertRaisesRegex(ValueError, "differs"):
            extract_archive(self.archive, self.data, self.contract, changed)
        self.assertIsNone(prepared_pack(self.data, self.pack))

    def test_extracted_folder_is_verified_at_any_level(self):
        with zipfile.ZipFile(self.archive) as source:
            source.extractall(self.root / "extracted")
        for folder in (self.root / "extracted", self.root / "extracted" / self.pack.archive_root,
                       self.root / "extracted" / self.pack.archive_root / "load"):
            with self.subTest(str(folder)):
                directory = import_extracted(folder, self.data, self.pack)
                self.assertEqual(json.loads((directory / "source.json").read_text())["origin"],
                                 "extracted_folder")
        texture = self.root / "extracted" / self.pack.archive_root / TEXTURE_FOLDER / UI_ATLAS_HD_TEXTURE
        texture.write_bytes(b"\x89PNG other")
        with self.assertRaisesRegex(ValueError, "differs"):
            import_extracted(self.root / "extracted", self.data, self.pack)
        with self.assertRaisesRegex(ValueError, "does not contain"):
            import_extracted(self.root, self.data, self.pack)

    def test_prepare_uses_a_stored_archive_and_reports_failures(self):
        self.assertEqual(pack_contract(self.recipe, self.pack), self.contract)
        self.assertIsNone(prepare_ui_texture_pack(root=self.root / "package", data_root=self.data,
                                                  recipe={}, pack=self.pack))
        package = self.root / "package"
        package.mkdir()
        self.assertEqual(pack_status(package, self.data, self.contract, self.pack), "missing")
        self.assertIsNone(prepare_ui_texture_pack(root=package, data_root=self.data, recipe=self.recipe,
                                                  pack=self.pack))
        (self.data / "downloads").mkdir(parents=True)
        stored = self.data / "downloads" / self.archive.name
        stored.write_bytes(self.archive.read_bytes())
        self.assertEqual(pack_status(package, self.data, self.contract, self.pack), "archive")
        messages = []
        directory = prepare_ui_texture_pack(root=package, data_root=self.data, recipe=self.recipe,
                                            report=lambda _, text: messages.append(text), pack=self.pack)
        self.assertEqual(directory, prepared_pack(self.data, self.pack))
        self.assertEqual(pack_status(package, self.data, self.contract, self.pack), "ready")
        self.assertTrue(any("from the original archive" in text for text in messages))
        # A corrupt stored archive keeps the native textures and says why.
        (directory / "source.json").unlink()
        stored.write_bytes(b"x" * self.contract.bytes)
        messages.clear()
        self.assertIsNone(prepare_ui_texture_pack(root=package, data_root=self.data, recipe=self.recipe,
                                                  report=lambda _, text: messages.append(text),
                                                  pack=self.pack))
        self.assertTrue(any("native UI textures" in text for text in messages))

    def test_catalogue_matches_the_supported_revisions(self):
        contract = pack_contract(REPOSITORY / "tools/triaevum_release/supported_revisions.json")
        self.assertEqual(contract.name, "oot3d_vanilla_ui_hud_32.zip")
        self.assertEqual(contract.bytes, 451655008)
        self.assertEqual(contract.sha256, "5ff478db312c3f57f54263cd84f4caef15bbb391fd1dda3bdd3c25c4917938a1")
        self.assertEqual(len(VANILLA_UI_HUD.members), 76)
        self.assertIn(UI_ATLAS_HD_TEXTURE, VANILLA_UI_HUD.members)


if __name__ == "__main__":
    unittest.main()
