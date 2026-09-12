import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import zipfile

from common import atomic_write_json
from precompiled_title_layout import artifact
from precompiled_titles import FORMAT, MODEL, CATALOG
from release_platform import LINUX, WINDOWS
from linux_precompiled_catalog import create_catalog, require_elf


def elf(path, kind=3, machine=62):
    path.parent.mkdir(parents=True, exist_ok=True)
    header = bytearray(64)
    header[:6] = b"\x7fELF\x02\x01"
    struct.pack_into("<HH", header, 16, kind, machine)
    path.write_bytes(header)


class LinuxCatalogTests(unittest.TestCase):
    def test_rejects_pe_and_wrong_architecture(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "wrong.so"
            path.write_bytes(b"MZ" + bytes(64))
            with self.assertRaisesRegex(ValueError, "ELF64"):
                require_elf(path, shared=True)
            elf(path, machine=183)
            with self.assertRaisesRegex(ValueError, "x86-64"):
                require_elf(path, shared=True)
            elf(path, kind=2)
            with self.assertRaises(ValueError):
                require_elf(path, shared=True)
            require_elf(path, shared=False)

    def test_binds_linux_catalog_without_changing_rom_contracts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            reference = root / "reference"
            reference.mkdir()
            source = reference / "source/titles/fixture-translated.zip"
            source.parent.mkdir(parents=True)
            build = {"plugin_sha256": "1" * 64, "code_sha256": "2" * 64,
                     "translator_identity_sha256": "3" * 64, "program_sha256": "4" * 64}
            with zipfile.ZipFile(source, "w") as archive:
                archive.writestr("TITLE_SOURCE_MANIFEST.json", json.dumps({
                    "format": "triaevum_translated_title_source_v1", "build": build,
                    "files": {"function.cpp": "5" * 64}}))
            title = {"recipe": "fixture", "inputs": {"code": {"sha256": "2" * 64}},
                     "abi_version": 2, "target": WINDOWS.target,
                     "plugin": {"path": "titles/fixture/triaevum_title_aot.dll", "sha256": "1" * 64},
                     "translator_identity_sha256": "3" * 64,
                     "sources": [artifact(source, "source/titles/fixture-translated.zip")],
                     "data_compatibility": {"fixture": "preserved"}, "input_adapter": {"fixture": "preserved"}}
            catalog = {"format": FORMAT, "install_model": MODEL,
                       "runtime": {"path": WINDOWS.runtime}, "native_module": {"path": WINDOWS.native_module},
                       "titles": [title]}
            atomic_write_json(reference / CATALOG, catalog)
            installation = root / "installation"
            for name in (LINUX.runtime, LINUX.native_module, "titles/fixture/" + LINUX.title_module):
                elf(installation / name)
            plugin = installation / "titles/fixture" / LINUX.title_module
            with patch("linux_precompiled_catalog.host_platform", return_value=LINUX), \
                 patch("linux_precompiled_catalog.query_product") as query:
                result = create_catalog(reference, installation, plugin, source_commit="6" * 40)
            query.assert_called_once_with(installation / LINUX.runtime, plugin=plugin)
            self.assertEqual(result["target"], LINUX.target)
            for key in ("inputs", "data_compatibility", "input_adapter", "sources"):
                self.assertEqual(result["titles"][0][key], title[key])
            self.assertEqual(result["titles"][0]["plugin"]["path"], "titles/fixture/" + LINUX.title_module)
            self.assertEqual(json.loads((reference / CATALOG).read_text()), catalog)
            # Cross-platform promotion must not retain a Windows shader tool.
            catalog["titles"][0]["renderer_shader_preparation"] = {
                "format": "triaevum_renderer_shader_compiler_v1",
                "compiler": {"path": "forge/windows.exe"}}
            atomic_write_json(reference / CATALOG, catalog)
            with patch("linux_precompiled_catalog.host_platform", return_value=LINUX), \
                 patch("linux_precompiled_catalog.query_product") as query:
                with self.assertRaisesRegex(ValueError, "own staged renderer"):
                    create_catalog(reference, installation, plugin, source_commit="6" * 40)
                query.assert_not_called()
                helper = installation / "forge/oot3d_native_pica_aot_compiler"
                elf(helper, kind=2)
                rebound = create_catalog(reference, installation, plugin, source_commit="6" * 40,
                                         shader_compiler=helper)
                self.assertEqual(rebound["titles"][0]["renderer_shader_preparation"]["compiler"]["path"],
                                 "forge/oot3d_native_pica_aot_compiler")
            del catalog["titles"][0]["renderer_shader_preparation"]
            atomic_write_json(reference / CATALOG, catalog)
            before = (installation / CATALOG).read_bytes()
            with patch("linux_precompiled_catalog.host_platform", return_value=LINUX), \
                 patch("linux_precompiled_catalog.query_product", side_effect=ValueError("ABI failed")):
                with self.assertRaisesRegex(ValueError, "ABI failed"):
                    create_catalog(reference, installation, plugin, source_commit="6" * 40)
            self.assertEqual((installation / CATALOG).read_bytes(), before)
            with zipfile.ZipFile(source, "w") as archive:
                archive.writestr("TITLE_SOURCE_MANIFEST.json", json.dumps({
                    "format": "triaevum_translated_title_source_v1", "build": build, "files": {}}))
            catalog["titles"][0]["sources"] = [artifact(source, "source/titles/fixture-translated.zip")]
            atomic_write_json(reference / CATALOG, catalog)
            with patch("linux_precompiled_catalog.host_platform", return_value=LINUX), \
                 patch("linux_precompiled_catalog.query_product") as query:
                with self.assertRaisesRegex(ValueError, "source inventory"):
                    create_catalog(reference, installation, plugin, source_commit="6" * 40)
            query.assert_not_called()
            self.assertEqual((installation / CATALOG).read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
