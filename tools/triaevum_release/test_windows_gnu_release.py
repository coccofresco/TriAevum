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
        for name in ("runtime/msvcp140.dll", "runtime/vcruntime140.dll",
                     "runtime/vcruntime140_1.dll", "LICENSES/Microsoft-Visual-Cpp-Runtime.md"):
            self.assertNotIn(name, paths)
        # Windows searches the executable's own directory, never its parent.
        for name, role in (("runtime/libc++.dll", "runtime_library"), ("runtime/libunwind.dll", "runtime_library"),
                           ("runtime/shaderc_shared.dll", "runtime_library"),
                           ("forge/libc++.dll", "forge_runtime_module"), ("forge/libunwind.dll", "forge_runtime_module")):
            self.assertEqual(paths[name]["role"], role)
            self.assertEqual(paths[name]["source"], "build-release/" + name.split("/")[1])
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


class WindowsGnuTargetTests(unittest.TestCase):
    """The gnu layout names its target, so packaging and the audit apply the
    gnu platform policy; the MSVC layout and policy stay as they were."""

    def example(self):
        return json.loads(EXAMPLE.read_text(encoding="utf-8"))

    def test_gnu_layout_names_its_target_and_msvc_layout_is_returned_unchanged(self):
        msvc = self.example()
        self.assertIs(prepare_release.windows_layout(msvc, WINDOWS.target), msvc)
        self.assertEqual(msvc, self.example())
        self.assertNotIn("target", msvc)
        gnu = prepare_release.windows_layout(self.example(), WINDOWS_GNU.target)
        self.assertEqual(gnu["target"], WINDOWS_GNU.target)
        self.assertEqual(gnu["files"], prepare_release.mingw_layout(self.example()["files"]))

    def test_policy_roles_and_required_files_per_target(self):
        from platform_policy import resolve_policy
        policy = json.loads((ROOT / "tools/triaevum_release/release_policy.json").read_text(encoding="utf-8"))
        gnu = resolve_policy(policy, WINDOWS_GNU.target, "rust")
        self.assertEqual(gnu["allowed_roles"]["root_runtime_library"],
                         ["libc++.dll", "libunwind.dll", "shaderc_shared.dll"])
        for path in ("libc++.dll", "libunwind.dll", "shaderc_shared.dll", "runtime/TriAevum.exe",
                     "LICENSES/LLVM-Apache-2.0-WITH-LLVM-exception.txt", "LICENSES/mingw-w64.txt"):
            self.assertIn(path, gnu["required_paths"])
        for name in ("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"):
            self.assertNotIn(f"runtime/{name}", gnu["required_paths"])
            self.assertIn(name, gnu["forbidden_basenames"])
        self.assertNotIn("LICENSES/Microsoft-Visual-Cpp-Runtime.md", gnu["required_paths"])
        msvc = resolve_policy(policy, WINDOWS.target, "rust")
        self.assertNotIn("root_runtime_library", msvc["allowed_roles"])
        for path in ("runtime/msvcp140.dll", "runtime/vcruntime140.dll", "runtime/vcruntime140_1.dll",
                     "LICENSES/Microsoft-Visual-Cpp-Runtime.md"):
            self.assertIn(path, msvc["required_paths"])
        self.assertNotIn("vcruntime140.dll", msvc.get("forbidden_basenames", []))

    def test_rust_forge_receipt_must_name_the_release_target(self):
        import audit_release
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            declared = {"docs/forge-build.json": {"role": "forge_build_receipt"}}

            def errors(receipt_target, release_target):
                (root / "docs").mkdir(exist_ok=True)
                (root / "docs/forge-build.json").write_text(json.dumps(
                    {"format": audit_release.RUST_FORGE_RECEIPT_FORMAT, "target": receipt_target,
                     "binaries": []}), encoding="utf-8")
                return list(audit_release._rust_forge_errors(root, declared, {}, release_target, set()))
            mismatch = "Rust Forge build receipt target differs from the release target"
            self.assertNotIn(mismatch, errors(WINDOWS_GNU.target, WINDOWS_GNU.target))
            self.assertIn(mismatch, errors(WINDOWS.target, WINDOWS_GNU.target))
            self.assertIn(mismatch, errors(WINDOWS_GNU.target, WINDOWS.target))

    def test_catalog_target_follows_the_title_build(self):
        from release_platform import catalog_platform
        self.assertEqual(catalog_platform({"target": WINDOWS_GNU.target}), WINDOWS_GNU)
        self.assertEqual(catalog_platform({}), WINDOWS)
        self.assertNotEqual(catalog_platform({"target": WINDOWS.target}), WINDOWS_GNU)


class WindowsGnuPackageAuditTests(unittest.TestCase):
    """A whole gnu package through audit_release (the MSVC fixture of
    test_release_audit, turned into the llvm-mingw layout)."""

    def gnu_package(self, root: Path, extra: dict | None = None, release_target=WINDOWS_GNU.target):
        from unittest.mock import patch
        import test_release_audit as fixture
        files = {path: value for path, value in fixture.FILES.items()
                 if path not in ("runtime/msvcp140.dll", "runtime/vcruntime140.dll", "runtime/vcruntime140_1.dll",
                                 "LICENSES/Microsoft-Visual-Cpp-Runtime.md")}
        files.update({
            "libc++.dll": ("root_runtime_library", b"MZ libc++ fixture"),
            "libunwind.dll": ("root_runtime_library", b"MZ libunwind fixture"),
            "shaderc_shared.dll": ("root_runtime_library", b"MZ shaderc fixture"),
            "runtime/libc++.dll": ("runtime_library", b"MZ libc++ fixture"),
            "runtime/libunwind.dll": ("runtime_library", b"MZ libunwind fixture"),
            "runtime/shaderc_shared.dll": ("runtime_library", b"MZ shaderc fixture"),
            "forge/libc++.dll": ("forge_runtime_module", b"MZ libc++ fixture"),
            "forge/libunwind.dll": ("forge_runtime_module", b"MZ libunwind fixture"),
            "LICENSES/LLVM-Apache-2.0-WITH-LLVM-exception.txt": ("license", b"LLVM licence fixture\n"),
            "LICENSES/mingw-w64.txt": ("license", b"mingw-w64 licence fixture\n"),
        })
        files.update(extra or {})
        with patch.dict(fixture.FILES, files, clear=True):
            fixture.write_clean_package(root)
        manifest = root / fixture.MANIFEST
        data = json.loads(manifest.read_text(encoding="utf-8"))
        if release_target is not None:
            data["release"]["target"] = release_target
        manifest.write_text(json.dumps(data), encoding="utf-8")

    def audit(self, **kwargs):
        from audit_release import audit_release
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.gnu_package(root, **kwargs)
            return audit_release(root)

    def test_gnu_package_passes_with_root_runtime_libraries(self):
        result = self.audit()
        self.assertTrue(result.ok, result.errors)

    def test_gnu_package_with_a_visual_cpp_runtime_dll_is_refused(self):
        result = self.audit(extra={"runtime/vcruntime140.dll": ("runtime_library", b"MZ runtime fixture")})
        self.assertFalse(result.ok)
        self.assertTrue(any("vcruntime140.dll" in error for error in result.errors), result.errors)

    def test_gnu_layout_audited_as_msvc_is_refused(self):
        # What the package_release audit reported before the layout named its target.
        result = self.audit(release_target=None)
        self.assertFalse(result.ok)
        self.assertIn("unknown release role for libc++.dll: root_runtime_library", result.errors)
        self.assertIn("required release file is missing: runtime/vcruntime140.dll", result.errors)


class PeImportTests(unittest.TestCase):
    """pe_dependencies: every non-system import beside the PE (or in its hosts)."""

    def check(self, files, config=None):
        from pe_dependencies import pe_import_errors
        from test_vulkan_imports import pe_import_fixture
        policy = json.loads((ROOT / "tools/triaevum_release/release_policy.json").read_text(encoding="utf-8"))
        config = config or policy["platforms"][WINDOWS_GNU.target]["pe_imports"]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); declared = {}
            for relative, (role, imports) in files.items():
                (root / relative).parent.mkdir(parents=True, exist_ok=True)
                (root / relative).write_bytes(pe_import_fixture({name: ["f"] for name in imports}) if imports is not None
                                              else b"MZ library")
                declared[relative] = {"role": role}
            return pe_import_errors(root, declared, config)

    RUNTIME = {"runtime/TriAevum.exe": ("runtime_executable", ["KERNEL32.dll", "libc++.dll", "vulkan-1.dll",
                                                               "api-ms-win-crt-runtime-l1-1-0.dll"])}

    def test_import_only_in_the_parent_directory_is_refused(self):
        errors = self.check({**self.RUNTIME, "libc++.dll": ("root_runtime_library", None)})
        self.assertEqual(len(errors), 1)
        self.assertIn("runtime/TriAevum.exe imports libc++.dll", errors[0])

    def test_import_beside_the_executable_passes(self):
        self.assertEqual(self.check({**self.RUNTIME, "runtime/libc++.dll": ("runtime_library", None)}), [])

    def test_title_module_resolves_from_every_host_directory(self):
        plugin = {"titles/mm3d/triaevum_title_aot.dll": ("precompiled_title", ["KERNEL32.dll", "libc++.dll"])}
        self.assertEqual(self.check({**plugin, "libc++.dll": ("root_runtime_library", None),
                                     "runtime/libc++.dll": ("runtime_library", None)}), [])
        self.assertEqual(len(self.check({**plugin, "libc++.dll": ("root_runtime_library", None)})), 1)

    def test_executables_get_no_host_fallback(self):
        exe = {"titles/mm3d/tool.exe": ("precompiled_title", ["libc++.dll"]),
               "runtime/libc++.dll": ("runtime_library", None), "libc++.dll": ("root_runtime_library", None)}
        self.assertEqual(len(self.check(exe)), 1)

    def test_optional_vulkan_runtime_may_miss_only_the_visual_cpp_runtime(self):
        fsr = {"runtime/amd_fidelityfx_vk.dll": ("optional_vulkan_runtime",
                                                 ["KERNEL32.dll", "MSVCP140.dll", "VCRUNTIME140.dll", "vulkan-1.dll"])}
        self.assertEqual(self.check(fsr), [])
        other = {"runtime/triaevum_immerse.dll": ("separately_licensed_module", ["MSVCP140.dll"])}
        self.assertEqual(len(self.check(other)), 1)


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
