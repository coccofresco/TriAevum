import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from audit_release import DEFAULT_POLICY, audit_release
from common import atomic_write_json
from package_release import package_release
from platform_policy import resolve_policy
from release_platform import WINDOWS, LINUX
from readiness import evaluate_readiness
import test_release_audit as fixture


def linux_files():
    policy = json.loads(DEFAULT_POLICY.read_text())
    common = policy["allowed_roles"]
    files = {name: record for name, record in fixture.FILES.items()
             if record[0] in common and not name.startswith("LICENSES/Microsoft-")}
    files.update({
        "TriAevum": ("runtime_executable", b"ELF runtime fixture"),
        "TriAevumForge": ("forge_executable", b"ELF forge fixture"),
        "forge/oot3d_game_module.so": ("forge_runtime_module", b"ELF module fixture"),
        "triaevum_title_aot.so": ("runtime_library", b"ELF bootstrap fixture"),
        "_internal/base_library.zip": ("forge_frozen_resource", b"Python library fixture"),
    })
    return files


def write_linux_package(root, files=None):
    with patch.object(fixture, "FILES", linux_files() if files is None else files):
        fixture.write_clean_package(root)
    path = root / "release-manifest.json"
    manifest = json.loads(path.read_text())
    manifest["release"]["target"] = LINUX.target
    atomic_write_json(path, manifest)


class PlatformPolicyTests(unittest.TestCase):
    def test_sdk_shaderc_is_explicit_and_current_python_notice_is_required(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            files = linux_files()
            files['forge/libshaderc.so.1'] = ('forge_runtime_module', b'ELF shaderc fixture')
            write_linux_package(root, files)
            result = audit_release(root)
            self.assertTrue(result.ok, result.errors)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            files = linux_files()
            files['forge/libunreviewed.so.1'] = ('forge_runtime_module', b'unreviewed')
            del files['LICENSES/Python-3.13.txt']
            write_linux_package(root, files)
            errors = audit_release(root).errors
            self.assertTrue(any('libunreviewed.so.1' in error for error in errors))
            self.assertIn('required release file is missing: LICENSES/Python-3.13.txt', errors)

    def test_windows_qualification_cannot_approve_linux_release(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "readiness.json"
            document = {"format": "triaevum_release_readiness_v1", "gates": [
                {"id": "fixture", "required": True, "status": "complete"}]}
            atomic_write_json(path, document)
            self.assertTrue(evaluate_readiness(path).ready)
            self.assertFalse(evaluate_readiness(path, target=LINUX.target).ready)
            document["target"] = LINUX.target
            atomic_write_json(path, document)
            self.assertTrue(evaluate_readiness(path, target=LINUX.target).ready)
            self.assertFalse(evaluate_readiness(path).ready)

    def test_common_rules_are_immutable_and_platform_roles_disjoint(self):
        original = json.loads(DEFAULT_POLICY.read_text())
        saved = copy.deepcopy(original)
        windows = resolve_policy(original, WINDOWS.target)
        linux = resolve_policy(original, LINUX.target)
        self.assertEqual(original, saved)
        self.assertEqual(windows["allowed_roles"]["runtime_executable"], [WINDOWS.runtime])
        self.assertEqual(linux["allowed_roles"]["runtime_executable"], [LINUX.runtime])
        self.assertNotIn("msvcp140.dll", linux["required_paths"])
        for policy in (windows, linux):
            self.assertIn("AGENTS.md", policy["forbidden_basenames"])
            self.assertIn(".cci", policy["forbidden_extensions"])
            self.assertEqual(policy["source_archive_required_manifests"],
                             original["source_archive_required_manifests"])
        with self.assertRaises(ValueError):
            resolve_policy(original, "aarch64-linux-android")

    def test_linux_audit_is_host_independent(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_linux_package(root)
            with patch("precompiled_titles.host_platform", side_effect=AssertionError("audit cannot query host")):
                result = audit_release(root)
            self.assertTrue(result.ok, result.errors)
            self.assertEqual(result.summary["target"], LINUX.target)

    def test_linux_rejects_windows_binaries_and_host_loader_even_if_declared(self):
        for name, role in (("TriAevum.exe", "runtime_executable"),
                           ("_internal/foreign.dll", "forge_frozen_resource"),
                           ("lib/libc.so.6", "runtime_library"),
                           ("lib/libvulkan.so.1", "runtime_library")):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                files = linux_files()
                files[name] = role, b"unwanted binary"
                write_linux_package(root, files)
                result = audit_release(root)
                self.assertFalse(result.ok)
                self.assertTrue(any(name in error for error in result.errors), result.errors)

    def test_linux_requires_bootstrap_and_python_payload(self):
        for name in ("triaevum_title_aot.so", "_internal/base_library.zip"):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                files = linux_files()
                del files[name]
                write_linux_package(root, files)
                result = audit_release(root)
                self.assertIn(f"required release file is missing: {name}", result.errors)

    def test_package_queries_explicit_platform_runtime(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            inputs = root / "inputs"
            inputs.mkdir()
            write_linux_package(inputs)
            manifest = json.loads((inputs / "release-manifest.json").read_text())
            layout = root / "layout.json"
            atomic_write_json(layout, {
                "format": "triaevum_public_release_layout_v1", "target": LINUX.target,
                "files": [{**item, "source": str(inputs / item["path"])} for item in manifest["files"]],
            })
            with patch("package_release.query_product", return_value={"product": {"private_title_loaded": True}}) as query:
                with self.assertRaisesRegex(ValueError, "private title plugin"):
                    package_release(layout, root / "out", source_root=inputs,
                                    version="test", source_commit="0" * 40,
                                    enforce_readiness=False)
                self.assertEqual(query.call_args.args[0].name, LINUX.runtime)
            result = package_release(layout, root / "out", source_root=inputs,
                                     version="test", source_commit="0" * 40,
                                     enforce_readiness=False, verify_runtime=False)
            self.assertEqual(result["status"], "packaged")
            self.assertEqual(json.loads((root / "out/release-manifest.json").read_text())["release"]["target"], LINUX.target)


if __name__ == "__main__":
    unittest.main()
