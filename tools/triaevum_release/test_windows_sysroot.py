import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from common import atomic_write_json, sha256_file
from windows_sysroot import build_environment, compiler_sysroot, load_sysroot
from assemble_windows_sysroot import assemble
from whole_aot_object_cache import NativeToolchain, _toolchain_identity, _compile_arguments


class WindowsSysrootTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.files = ("msvc/include/vector", "msvc/lib/x64/libcmt.lib",
                      "sdk/Include/10.0.1.0/ucrt/stdio.h",
                      "sdk/Lib/10.0.1.0/ucrt/x64/libucrt.lib",
                      "sdk/Lib/10.0.1.0/um/x64/kernel32.lib", "clang/include/stddef.h")
        for name in self.files:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"fixture")
        self.write_manifest()

    def write_manifest(self):
        atomic_write_json(self.root / "sysroot.json", {
            "format": "triaevum_windows_sysroot_v1", "sdk_version": "10.0.1.0",
            "msvc_version": "14.44.35207",
            "files": {name: {"bytes": (self.root / name).stat().st_size,
                             "sha256": sha256_file(self.root / name)} for name in self.files},
        })

    def test_identity_changes_with_header_or_library(self):
        original = load_sysroot(self.root)
        (self.root / self.files[0]).write_bytes(b"new-header")
        with self.assertRaisesRegex(ValueError, "missing or changed"):
            load_sysroot(self.root)
        self.write_manifest()
        self.assertNotEqual(original.identity, load_sysroot(self.root).identity)
        self.assertIn(str(self.root / "msvc"), original.arguments())
        self.assertIn("-resource-dir=" + str(self.root / "clang"), original.arguments())
        self.assertIn("-fms-compatibility-version=19.44", original.arguments())

    def test_rejects_extra_files(self):
        (self.root / "injected.h").write_bytes(b"extra")
        with self.assertRaisesRegex(ValueError, "unindexed"):
            load_sysroot(self.root)

    def test_assembly_merges_identical_components_and_verifies_before_publication(self):
        trees = [(self.root / name, name) for name in ("msvc", "sdk", "clang")]
        trees.append((self.root / "msvc", "msvc"))
        output = self.root / "assembled"
        result = assemble(trees, sdk_version="10.0.1.0", msvc_version="14.44.35207", clang_version="22", output=output)
        self.assertEqual(result["files"], len(self.files))
        self.assertEqual(result["identity"], load_sysroot(output).identity)
        with self.assertRaisesRegex(ValueError, "new output"):
            assemble(trees, sdk_version="10.0.1.0", msvc_version="14.44.35207", clang_version="22", output=output)

    def test_assembly_rejects_conflicts_and_incomplete_trees(self):
        other = self.root / "conflict"
        other.mkdir()
        (other / "vector").write_bytes(b"conflicting header")
        trees = [(self.root / "msvc", "msvc"), (other, "msvc/include")]
        with self.assertRaisesRegex(ValueError, "Conflicting"):
            assemble(trees, sdk_version="10.0.1.0", msvc_version="14.44.35207", clang_version="22", output=self.root / "bad")
        self.assertFalse((self.root / "bad").exists())
        with self.assertRaisesRegex(ValueError, "Incomplete"):
            assemble(trees[:1], sdk_version="10.0.1.0", msvc_version="14.44.35207", clang_version="22", output=self.root / "missing")
        self.assertFalse((self.root / "missing").exists())
        self.assertEqual(list(self.root.glob(".sysroot-assembly-*")), [])

    def test_explicit_missing_root_does_not_fall_back_to_host(self):
        compiler = self.root / "clang-cl.exe"
        self.assertIsNone(compiler_sysroot(compiler))
        with self.assertRaises(FileNotFoundError):
            compiler_sysroot(compiler, self.root / "missing")

    def test_build_local_contract_is_bound_to_its_root(self):
        verified = load_sysroot(self.root)
        self.assertIs(compiler_sysroot(self.root / "clang-cl.exe", self.root, verified=verified), verified)
        with self.assertRaisesRegex(ValueError, "does not match"):
            compiler_sysroot(self.root / "clang-cl.exe", self.root / "different", verified=verified)

    def test_removes_developer_flags_case_insensitively(self):
        with patch.dict(os.environ, {"CL": "/Od", "lib": "wrong", "INCLUDE": "wrong",
                                     "CCC_OVERRIDE_OPTIONS": "wrong", "KEEP_ME": "yes"}):
            result = build_environment()
        self.assertNotIn("CL", result)
        self.assertNotIn("lib", result)
        self.assertNotIn("INCLUDE", result)
        self.assertNotIn("CCC_OVERRIDE_OPTIONS", result)
        self.assertEqual(result["KEEP_ME"], "yes")

    def test_object_identity_and_command_use_sysroot(self):
        # Tools stay outside the sysroot's closed inventory.
        with tempfile.TemporaryDirectory() as tools:
            compiler = Path(tools) / "clang-cl.exe"
            archiver = Path(tools) / "llvm-lib.exe"
            compiler.write_bytes(b"compiler")
            archiver.write_bytes(b"archiver")
            toolchain = NativeToolchain(compiler, archiver, sysroot=self.root)
            first = load_sysroot(self.root)
            first_key, _ = _toolchain_identity(toolchain, first)
            args = _compile_arguments(toolchain, Path(tools), Path(tools), Path(tools), first)
            self.assertIn("/vctoolsdir", args)
            self.assertIn(str(self.root / "msvc"), args)
            (self.root / self.files[1]).write_bytes(b"changed-library")
            self.write_manifest()
            second_key, _ = _toolchain_identity(toolchain, load_sysroot(self.root))
            self.assertNotEqual(first_key, second_key)


if __name__ == "__main__":
    unittest.main()
