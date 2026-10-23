import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from tools.triaevum_release.linux_elf_audit import (
    Elf, parse_readelf, dependency_errors, audit_native_elfs,
)

class LinuxElfTests(unittest.TestCase):
    def test_version_needs_are_not_provider_definitions(self):
        elf = parse_readelf(
            " 0x1 (NEEDED) Shared library: [libstdc++.so.6]\n"
            " 0x2 (RUNPATH) Library runpath: [$ORIGIN/lib]",
            "Version definition section '.gnu.version_d' contains 1 entry:\n"
            " Name: LOCAL_1\n"
            "Version needs section '.gnu.version_r' contains 1 entry:\n"
            " File: libstdc++.so.6 Cnt: 2\n"
            " Name: CXXABI_1.3.15 Flags: none\n"
            " Name: GLIBCXX_3.4.33 Flags: none\n")
        self.assertEqual(elf.needed, {"libstdc++.so.6"})
        self.assertEqual(elf.definitions, {"LOCAL_1"})
        self.assertEqual(elf.runpath, ("$ORIGIN/lib",))
        self.assertEqual(elf.requirements["libstdc++.so.6"], {"CXXABI_1.3.15", "GLIBCXX_3.4.33"})

    def test_missing_cpp_runtime_is_rejected(self):
        objects = {"lib/libfmt.so.10": Elf({"libstdc++.so.6"}, {"libstdc++.so.6": {"CXXABI_1.3.15"}})}
        self.assertIn("missing bundled dependency libstdc++.so.6", dependency_errors(objects)[0])

    def test_old_bundled_cpp_runtime_is_rejected(self):
        objects = {
            "lib/libspdlog.so.1.15": Elf({"libstdc++.so.6"}, {"libstdc++.so.6": {"CXXABI_1.3.15"}}),
            "lib/libstdc++.so.6": Elf(definitions={"CXXABI_1.3.14"}),
        }
        self.assertIn("lacks CXXABI_1.3.15", dependency_errors(objects)[0])
        objects["lib/libstdc++.so.6"].definitions.add("CXXABI_1.3.15")
        self.assertEqual(dependency_errors(objects), [])

    def test_libdecor_is_required_even_for_x11(self):
        objects = {"lib/libSDL3.so.0": Elf({"libdecor-0.so.0"})}
        self.assertIn("libdecor-0.so.0", dependency_errors(objects)[0])
        objects["lib/libdecor-0.so.0"] = Elf({"libwayland-client.so.0"})
        self.assertEqual(dependency_errors(objects), [])

    def test_transitive_closure_and_gcc_pair(self):
        objects = {"lib/libstdc++.so.6": Elf({"libgcc_s.so.1", "libc.so.6"})}
        self.assertIn("libgcc_s.so.1", dependency_errors(objects)[0])
        objects["lib/libgcc_s.so.1"] = Elf({"libc.so.6"}, {"libc.so.6": {"GLIBC_2.34"}})
        self.assertEqual(dependency_errors(objects), [])

    def test_newer_glibc_and_private_abi_are_rejected(self):
        for version in ("GLIBC_2.41", "GLIBC_PRIVATE"):
            self.assertTrue(dependency_errors({"lib/libstdc++.so.6": Elf(
                {"libc.so.6"}, {"libc.so.6": {version}})}))

    def test_compat_cxx_directory_provides_cpp_runtime(self):
        requirement = {"libstdc++.so.6": {"CXXABI_1.3.15"}}
        objects = {"TriAevum": Elf({"libstdc++.so.6"}, requirement, runpath=("$ORIGIN/lib",)),
                   "forge/module.so": Elf({"libstdc++.so.6"}, requirement, runpath=("$ORIGIN/../lib",)),
                   "lib/compat-cxx/libstdc++.so.6": Elf({"libgcc_s.so.1"}, definitions={"CXXABI_1.3.14"}),
                   "lib/compat-cxx/libgcc_s.so.1": Elf()}
        self.assertEqual(len(dependency_errors(objects)), 2)
        objects["lib/compat-cxx/libstdc++.so.6"].definitions.add("CXXABI_1.3.15")
        self.assertEqual(dependency_errors(objects), [])

    def test_adjacent_helper_requires_origin_runpath(self):
        objects = {"forge/helper": Elf({"libshaderc.so.1"}), "forge/libshaderc.so.1": Elf()}
        self.assertTrue(dependency_errors(objects))
        objects["forge/helper"].runpath = ("$ORIGIN",)
        self.assertEqual(dependency_errors(objects), [])

    def test_rpath_and_runpath_loader_order(self):
        requirement = {"libstdc++.so.6": {"CXXABI_1.3.15"}}
        objects = {
            "forge/helper": Elf({"libstdc++.so.6"}, requirement, rpath=("$ORIGIN",)),
            "forge/libstdc++.so.6": Elf(),
            "lib/libstdc++.so.6": Elf(definitions={"CXXABI_1.3.15"}),
        }
        self.assertTrue(dependency_errors(objects))
        objects["forge/helper"].runpath = ("$ORIGIN",)
        self.assertEqual(dependency_errors(objects), [])

    def test_absolute_or_escaping_paths_are_rejected(self):
        for location in ("/usr/lib/build-machine", "$ORIGIN/..", ""):
            self.assertTrue(dependency_errors({"TriAevum": Elf(runpath=(location,))}))

    def test_actual_elf_magic_scans_native_libraries(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "lib").mkdir()
            (root / "TriAevum").write_bytes(b"\x7fELFfixture")
            (root / "lib/libSDL3.so.0").write_bytes(b"\x7fELFfixture")
            with patch("tools.triaevum_release.linux_elf_audit.inspect_elf",
                       side_effect=[Elf(), Elf({"libdecor-0.so.0"})]):
                with self.assertRaisesRegex(ValueError, "libdecor"):
                    audit_native_elfs(root)

    def test_native_executable_must_be_elf(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "TriAevum").write_bytes(b"not ELF")
            with self.assertRaisesRegex(ValueError, "not an ELF"):
                audit_native_elfs(root)
