import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import build_forge_binary
import forge
import forge_gui
import precompiled_titles
from common import atomic_write_json, sha256_file
from release_platform import WINDOWS, LINUX, catalog_platform, for_target


class ReleasePlatformTests(unittest.TestCase):
    def test_legacy_windows_and_explicit_linux(self):
        self.assertEqual(catalog_platform({}), WINDOWS)
        self.assertEqual(catalog_platform({"target": LINUX.target}), LINUX)
        with self.assertRaises(ValueError):
            for_target("aarch64-unknown-linux-gnu")

    def test_linux_gui_paths(self):
        with patch("forge_gui.host_platform", return_value=LINUX), patch("forge.host_platform", return_value=LINUX):
            self.assertEqual(forge_gui.runtime_path().name, "TriAevum")
            self.assertEqual(forge.default_runtime_plugin_path().name, "triaevum_title_aot.so")

    def test_linux_catalog_and_cross_platform_rejection(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            def record(name):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(name.encode())
                return {"path": name, "bytes": path.stat().st_size, "sha256": sha256_file(path)}
            recipe = {"id": "fixture", "inputs": {"code": {"bytes": 1, "sha256": "1" * 64}}}
            catalog = {"format": precompiled_titles.FORMAT, "install_model": precompiled_titles.MODEL,
                       "target": LINUX.target, "runtime": record(LINUX.runtime),
                       "native_module": record(LINUX.native_module), "titles": [{
                           "recipe": "fixture", "inputs": recipe["inputs"], "abi_version": 2,
                           "target": LINUX.target, "translator_identity_sha256": "a" * 64,
                           "plugin": record("titles/fixture/" + LINUX.title_module)}]}
            atomic_write_json(root / precompiled_titles.CATALOG, catalog)
            with patch("precompiled_titles.host_platform", return_value=LINUX):
                self.assertEqual(precompiled_titles.select_title(root, recipe)["target"], LINUX.target)
                corrupted = copy.deepcopy(catalog)
                corrupted["titles"][0]["target"] = WINDOWS.target
                with self.assertRaisesRegex(ValueError, "revision/ABI"):
                    precompiled_titles.select_title(root, recipe, catalog=corrupted)
                (root / LINUX.native_module).write_bytes(b"damaged")
                with self.assertRaisesRegex(ValueError, "integrity"):
                    precompiled_titles.select_title(root, recipe)
            with patch("precompiled_titles.host_platform", return_value=WINDOWS):
                with self.assertRaisesRegex(ValueError, "not this host"):
                    precompiled_titles.select_title(root, recipe)

    def test_build_linux_directory_bundle_and_windows_single_file(self):
        for system, expected in (("linux", "TriAevumForge/TriAevumForge"), ("win32", "TriAevumForge.exe")):
            with self.subTest(system=system), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                report = {"status": "inventory", "recipes": ["fixture"],
                          "direct_aot_tools": {"whole_aot_builder_available": True}}
                with patch.object(build_forge_binary.sys, "platform", system), \
                     patch.object(build_forge_binary, "required_data", return_value=()), \
                     patch.object(build_forge_binary.subprocess, "run") as run:
                    run.return_value.stdout = json.dumps(report)
                    binary = build_forge_binary.build(root / "out", root / "work", python=Path("python"))
                    self.assertEqual(binary.relative_to(root / "out").as_posix(), expected)
                    command = run.call_args_list[0].args[0]
                    self.assertIn("--onedir" if system == "linux" else "--onefile", command)
                    if system == "linux":
                        self.assertNotIn("--runtime-tmpdir", command)


if __name__ == "__main__":
    unittest.main()
