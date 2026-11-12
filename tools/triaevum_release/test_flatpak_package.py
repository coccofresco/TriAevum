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
            # The manifest installs the desktop entry's icons from icons/hicolor.
            for name in ("io.github.coccofresco.TriAevum", "io.github.coccofresco.TriAevum.Forge"):
                icon = root / f"build/icons/hicolor/256x256/apps/{name}.png"
                self.assertTrue(icon.read_bytes().startswith(b"\x89PNG"), icon)

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

    def test_mm3d_flatpak_rejects_the_pyinstaller_forge(self):
        # Negative: MM3D has only the title-mm3d Rust Forge; a PyInstaller
        # (OoT3D) package is refused and nothing stays staged.
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_linux_package(root / "source")
            with self.assertRaisesRegex(ValueError, "requires the title's Rust Forge"):
                stage_flatpak(root / "source", root / "build", title="mm3d")
            self.assertFalse((root / "build").exists())

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
        # Flatpak before 1.15.6 silently drops --device=input, which leaves
        # SDL without /dev/input; pads also need hidraw and USB nodes (#71).
        self.assertIn("--device=all", flags)
        self.assertNotIn("--device=input", flags)
        self.assertFalse(any(flag.startswith("--filesystem=") for flag in flags))
        desktop = (ASSETS / "io.github.coccofresco.TriAevum.desktop").read_text()
        self.assertIn("Exec=triaevum\n", desktop)
        self.assertIn("Exec=triaevum --forge\n", desktop)


class SignedFlatpakTests(unittest.TestCase):
    def test_signed_package_keeps_its_signature_in_the_build_inputs(self):
        from signed_package_fixture import audit_trusting, write_signed_linux_package
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            keys = write_signed_linux_package(root / "source", root)
            with patch("flatpak_package.audit_release", audit_trusting(keys)):
                stage_flatpak(root / "source", root / "inputs")
            copied = root / "inputs/package/release-signature.json"
            self.assertEqual(copied.read_bytes(), (root / "source/release-signature.json").read_bytes())

    def test_unsigned_package_has_no_signature_in_the_build_inputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_linux_package(root / "source")
            stage_flatpak(root / "source", root / "inputs")
            self.assertFalse((root / "inputs/package/release-signature.json").exists())
