"""The llvm-mingw Windows release inputs (XBUILD Q2): layout, runtime libraries,
explicit DLSS inputs and the gnullvm Forge flags. The MSVC route is unchanged."""
import argparse
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import prepare_release  # noqa: E402
import rust_forge_build  # noqa: E402
from release_platform import WINDOWS, WINDOWS_GNU  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
EXAMPLE = ROOT / "tools/triaevum_release/runtime_release_layout.example.json"


class WindowsGnuLayoutTests(unittest.TestCase):
    def test_layout_has_root_runtime_and_licences_and_no_msvc_crt(self):
        files = prepare_release.mingw_layout(json.loads(EXAMPLE.read_text(encoding="utf-8"))["files"])
        paths = {item["path"]: item for item in files}
        for name in ("libc++.dll", "libunwind.dll", "shaderc_shared.dll"):
            self.assertEqual(paths[name]["role"], "root_runtime_library")
        for name in ("runtime/shaderc_shared.dll", "runtime/msvcp140.dll", "runtime/vcruntime140.dll",
                     "runtime/vcruntime140_1.dll", "LICENSES/Microsoft-Visual-Cpp-Runtime.md"):
            self.assertNotIn(name, paths)
        policy = json.loads((ROOT / "tools/triaevum_release/release_policy.json").read_text(encoding="utf-8"))
        required = policy["platforms"][WINDOWS_GNU.target]["required_paths"]
        for path in ("libc++.dll", "libunwind.dll", "shaderc_shared.dll",
                     "LICENSES/LLVM-Apache-2.0-WITH-LLVM-exception.txt", "LICENSES/mingw-w64.txt"):
            self.assertIn(path, required)
            self.assertIn(path, paths)
        for source in ("LICENSES/LLVM-Apache-2.0-WITH-LLVM-exception.txt", "LICENSES/mingw-w64.txt"):
            self.assertTrue((ROOT / source).is_file(), source)

    def test_msvc_layout_is_untouched_by_the_gnu_helpers(self):
        files = json.loads(EXAMPLE.read_text(encoding="utf-8"))["files"]
        self.assertIn("runtime/msvcp140.dll", {item["path"] for item in files})

    def test_mingw_runtime_libraries_are_required(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "libc++.dll").write_bytes(b"MZ")
            with self.assertRaisesRegex(ValueError, "libunwind.dll"):
                prepare_release.mingw_runtime_artifacts(root)
            (root / "libunwind.dll").write_bytes(b"MZ")
            self.assertEqual(set(prepare_release.mingw_runtime_artifacts(root)), {"libc++.dll", "libunwind.dll"})

    def test_explicit_dlss_inputs_together_or_not_at_all(self):
        none = argparse.Namespace(dlss_module=None, dlss_runtime=None, dlss_license=None)
        self.assertEqual(prepare_release.explicit_dlss_targets(none), {})
        partial = argparse.Namespace(dlss_module=Path("a.dll"), dlss_runtime=None, dlss_license=None)
        with self.assertRaisesRegex(ValueError, "together"):
            prepare_release.explicit_dlss_targets(partial)
        full = argparse.Namespace(dlss_module=Path("m.dll"), dlss_runtime=Path("r.dll"), dlss_license=Path("l.txt"))
        self.assertEqual(set(prepare_release.explicit_dlss_targets(full)), {"dlss", "dlss_runtime", "dlss_license"})

    def test_target_and_runtime_options_must_match(self):
        base = dict(title="oot3d", recipe=None, restoration_resources=None, exe_runner=None)
        with self.assertRaisesRegex(ValueError, "--mingw-runtime-dir"):
            prepare_release.prepare(argparse.Namespace(**base, target=WINDOWS_GNU.target,
                                                       vc_redist_dir=Path("x"), mingw_runtime_dir=None))
        with self.assertRaisesRegex(ValueError, "--vc-redist-dir"):
            prepare_release.prepare(argparse.Namespace(**base, target=WINDOWS.target,
                                                       vc_redist_dir=None, mingw_runtime_dir=Path("x")))


class GnullvmForgeTests(unittest.TestCase):
    def test_cargo_target_and_flags(self):
        self.assertEqual(rust_forge_build.cargo_target(WINDOWS_GNU.target), "x86_64-pc-windows-gnullvm")
        self.assertEqual(rust_forge_build.cargo_target(WINDOWS.target), WINDOWS.target)
        flags = rust_forge_build.rustflags(WINDOWS_GNU.target)
        self.assertIn("target-feature=+crt-static", flags)
        self.assertFalse(any("PDBALTPATH" in flag for flag in flags))
        self.assertEqual(rust_forge_build.BINARIES[WINDOWS_GNU.target], rust_forge_build.BINARIES[WINDOWS.target])
        self.assertIn("-C", rust_forge_build.rustflags(WINDOWS.target))
        self.assertTrue(any("PDBALTPATH" in flag for flag in rust_forge_build.rustflags(WINDOWS.target)))


if __name__ == "__main__":
    unittest.main()
