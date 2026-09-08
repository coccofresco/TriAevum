import copy
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from common import atomic_write_json, sha256_file
from precompiled_titles import CATALOG, FORMAT, MODEL, checked_file, load_catalog, select_title
from forge_gui import InstallRequest, install_private_title


class PrecompiledTitleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        def artifact(name):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(name.encode())
            return {"path": name, "bytes": path.stat().st_size, "sha256": sha256_file(path)}
        self.recipe = {"id": "test", "inputs": {
            kind: {"sha256": str(i) * 64, "bytes": i} for i, kind in enumerate(("code", "exheader", "romfs"), 1)}}
        self.catalog = {"format": FORMAT, "install_model": MODEL,
                        "runtime": artifact("TriAevum.exe"), "native_module": artifact("forge/oot3d_game_module.dll"),
                        "titles": [{"recipe": "test", "inputs": self.recipe["inputs"], "abi_version": 2,
                                    "target": "x86_64-pc-windows-msvc", "translator_identity_sha256": "a" * 64,
                                    "plugin": artifact("titles/test/contract.dll")}]}
        atomic_write_json(self.root / CATALOG, self.catalog)

    def test_selection_and_relocation(self):
        self.assertEqual(select_title(self.root, self.recipe)["recipe"], "test")
        moved = self.root / "relocated"
        import shutil
        source = self.root / "copy"
        source.mkdir()
        for name in ("TriAevum.exe", "forge", "titles", "recipes"):
            item = self.root / name
            if item.is_dir():
                shutil.copytree(item, source / name)
            else:
                shutil.copy2(item, source / name)
        source.rename(moved)
        select_title(moved, self.recipe)

    def test_corruption_runtime_and_revision_rejected(self):
        for record in (self.catalog["runtime"], self.catalog["native_module"], self.catalog["titles"][0]["plugin"]):
            path = self.root / record["path"]
            original = path.read_bytes()
            path.write_bytes(original + b"changed")
            with self.assertRaisesRegex(ValueError, "integrity"):
                select_title(self.root, self.recipe)
            path.write_bytes(original)
        recipe = copy.deepcopy(self.recipe)
        recipe["inputs"]["code"]["sha256"] = "b" * 64
        with self.assertRaisesRegex(ValueError, "revision/ABI"):
            select_title(self.root, recipe)

    def test_missing_catalog_never_falls_back_to_compilation(self):
        (self.root / CATALOG).unlink()
        with patch("forge_gui.runtime_path", return_value=self.root / "TriAevum.exe"), \
                patch("forge_gui.forge.query_product"), \
                patch("forge_gui.forge.probe_toolchain") as probe, \
                patch("forge_gui.forge.build_private_title") as build, \
                patch("forge_gui.ctr_rom.extract_decrypted_rom") as extract:
            with self.assertRaises(OSError):
                install_private_title(InstallRequest(self.root / "rom.cci", self.root / "data"))
            probe.assert_not_called()
            build.assert_not_called()
            extract.assert_not_called()

    def test_unbound_content_family_cannot_bypass_catalog(self):
        self.recipe['data_compatibility'] = {'format': 'unbound'}
        with self.assertRaisesRegex(ValueError, 'content family differs'):
            select_title(self.root, self.recipe)

    def test_duplicate_and_path_escape_rejected(self):
        self.catalog["titles"] *= 2
        atomic_write_json(self.root / CATALOG, self.catalog)
        with self.assertRaisesRegex(ValueError, "duplicate"):
            load_catalog(self.root)
        with self.assertRaises(ValueError):
            checked_file(self.root, {"path": "../outside.dll"})


if __name__ == "__main__":
    unittest.main()
