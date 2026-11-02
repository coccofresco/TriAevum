import copy
import json
import contextlib
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import build_forge_binary
import forge
import forge_gui
import precompiled_titles
from common import atomic_write_json, sha256_file
from release_platform import (WINDOWS, LINUX, MACOS, catalog_platform, for_target, host_platform,
                              installed_runtime_path, place_installed_runtime)


class ReleasePlatformTests(unittest.TestCase):
    def test_macos_target_and_host_are_explicit(self):
        self.assertEqual(catalog_platform({"target": MACOS.target}), MACOS)
        with patch("release_platform.sys.platform", "darwin"), \
             patch("release_platform.platform.machine", return_value="arm64"):
            self.assertEqual(host_platform(), MACOS)
        with patch("release_platform.sys.platform", "linux"), \
             patch("release_platform.platform.machine", return_value="aarch64"):
            with self.assertRaises(ValueError):
                host_platform()

    def test_macos_activation_never_mutates_the_signed_package(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / MACOS.runtime).write_bytes(b"signed runtime")
            stale = root / ".fixture.1.stale"
            stale.write_bytes(b"sealed file")
            self.assertEqual(place_installed_runtime(root, MACOS), root / MACOS.runtime)
            self.assertEqual(stale.read_bytes(), b"sealed file")

    def test_developer_title_command_selects_host_toolchain(self):
        for platform in (WINDOWS, LINUX):
            with self.subTest(target=platform.target), \
                 patch("forge.host_platform", return_value=platform), \
                 patch("forge.build_private_title", return_value={"status": "fixture"}) as build, \
                 contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(forge.main(["build-title", "--prepared-directory", "fixture"]), 0)
                self.assertEqual(build.call_args.kwargs["target_triple"], platform.target)
                self.assertEqual(build.call_args.kwargs["compiler"].name, platform.compiler)
                self.assertEqual(build.call_args.kwargs["archiver"].name, platform.archiver)

    def test_installing_places_the_windows_runtime_in_the_root(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "runtime").mkdir()
            (root / WINDOWS.runtime).write_bytes(b"new runtime")
            (root / "runtime/SDL2.dll").write_bytes(b"new library")
            # A previous package left its runtime and DLLs in the root.
            (root / "TriAevum.exe").write_bytes(b"old runtime")
            (root / "SDL2.dll").write_bytes(b"old library")
            (root / "unrelated.dll").write_bytes(b"user file")
            self.assertEqual(installed_runtime_path(root, WINDOWS), root / WINDOWS.runtime)
            self.assertEqual(place_installed_runtime(root, WINDOWS), root / "TriAevum.exe")
            self.assertEqual((root / "TriAevum.exe").read_bytes(), b"new runtime")
            self.assertFalse((root / WINDOWS.runtime).exists())
            self.assertFalse((root / "SDL2.dll").exists())
            self.assertTrue((root / "runtime/SDL2.dll").is_file())
            self.assertTrue((root / "unrelated.dll").is_file())
            self.assertEqual(installed_runtime_path(root, WINDOWS), root / "TriAevum.exe")
            self.assertEqual(place_installed_runtime(root, WINDOWS), root / "TriAevum.exe")
            (root / LINUX.runtime).write_bytes(b"linux runtime")
            self.assertEqual(place_installed_runtime(root, LINUX), root / LINUX.runtime)
            self.assertTrue((root / LINUX.runtime).is_file())

    def test_a_loaded_root_library_is_renamed_aside(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "runtime").mkdir()
            (root / WINDOWS.runtime).write_bytes(b"new runtime")
            (root / "runtime/vcruntime140.dll").write_bytes(b"new crt")
            # Forge has loaded the CRT a previous package left in the root.
            (root / "vcruntime140.dll").write_bytes(b"old crt")
            (root / ".SDL2.dll.1.stale").write_bytes(b"released stale library")
            unlink = Path.unlink

            def loaded(path, *args, **kwargs):
                if path.name == "vcruntime140.dll" and path.parent == root:
                    raise PermissionError(5, "Access is denied", str(path))
                return unlink(path, *args, **kwargs)

            with patch.object(Path, "unlink", loaded):
                self.assertEqual(place_installed_runtime(root, WINDOWS), root / "TriAevum.exe")
            self.assertFalse((root / "vcruntime140.dll").exists())
            self.assertFalse((root / ".SDL2.dll.1.stale").exists())
            self.assertEqual([path.read_bytes() for path in root.glob(".vcruntime140.dll.*.stale")],
                             [b"old crt"])
            self.assertEqual((root / "TriAevum.exe").read_bytes(), b"new runtime")
            # Once released, the next run removes the renamed library.
            self.assertEqual(place_installed_runtime(root, WINDOWS), root / "TriAevum.exe")
            self.assertEqual(list(root.glob(".*.stale")), [])

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
            with patch("precompiled_titles.host_platform", side_effect=AssertionError("metadata validation queried host")):
                self.assertEqual(precompiled_titles.validate_title(root, recipe)["target"], LINUX.target)
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
