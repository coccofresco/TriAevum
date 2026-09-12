import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from audit_release import audit_release
from flatpak_package import ASSETS, stage_flatpak
from test_platform_policy import write_linux_package
from test_release_audit import write_clean_package
from common import atomic_write_json, sha256_file


class FlatpakPackageTests(unittest.TestCase):
    def test_shader_helper_remains_executable_in_flatpak(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            write_linux_package(source)
            relative = "forge/oot3d_native_pica_aot_compiler"
            helper = source / relative
            helper.write_bytes(b"fixture helper")
            manifest = json.loads((source / "release-manifest.json").read_text())
            manifest["files"].append({"path": relative, "role": "shader_preparation_tool",
                "bytes": helper.stat().st_size, "sha256": sha256_file(helper)})
            atomic_write_json(source / "release-manifest.json", manifest)
            original_chmod = Path.chmod
            calls = []
            def chmod(path, mode, **kwargs):
                calls.append((path, mode))
                return original_chmod(path, mode, **kwargs)
            with patch.object(Path, "chmod", chmod):
                stage_flatpak(source, root / "build")
            self.assertIn((root / "build/package" / relative, 0o755), calls)

    def test_stages_only_audited_linux_inventory_without_promoting_qualification(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            write_linux_package(source)
            manifest = stage_flatpak(source, root / "build")
            copied = audit_release(root / "build/package")
            self.assertTrue(copied.ok, copied.errors)
            settings = json.loads(manifest.read_text())
            self.assertFalse(settings["build-options"]["strip"])
            self.assertTrue(settings["build-options"]["no-debuginfo"])
            self.assertTrue(settings["modules"][0]["no-python-timestamp-fix"])
            state = json.loads((root / "build/flatpak-staging.json").read_text())
            self.assertFalse(state["flatpak_qualified"])

    def test_rejects_windows_private_files_and_existing_destination(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            write_clean_package(source)
            with self.assertRaisesRegex(ValueError, "Linux"):
                stage_flatpak(source, root / "build")
            self.assertFalse((root / "build").exists())
            with self.assertRaisesRegex(ValueError, "new directory"):
                stage_flatpak(source, source / "build")
            (source / "personal.cci").write_bytes(b"private")
            with self.assertRaisesRegex(ValueError, "audit"):
                stage_flatpak(source, root / "build")

    def test_copy_failure_removes_only_new_staging(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            write_linux_package(source)
            with patch("flatpak_package.shutil.copyfile", side_effect=OSError("full disk")):
                with self.assertRaisesRegex(OSError, "full disk"):
                    stage_flatpak(source, root / "build")
            self.assertFalse((root / "build").exists())
            self.assertTrue(audit_release(source).ok)

    def test_permissions_are_scoped_and_desktop_actions_share_launcher(self):
        settings = json.loads((ASSETS / "io.github.coccofresco.TriAevum.json").read_text())
        flags = settings["finish-args"]
        self.assertIn("--device=dri", flags)
        self.assertIn("--device=input", flags)
        self.assertFalse(any(flag.startswith("--filesystem=") for flag in flags))
        self.assertNotIn("--device=all", flags)
        desktop = (ASSETS / "io.github.coccofresco.TriAevum.desktop").read_text()
        self.assertIn("Exec=triaevum\n", desktop)
        self.assertIn("Exec=triaevum --forge\n", desktop)
