from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools.triaevum_release.appimage_package import stage_appimage, ASSETS
from audit_release import audit_release
from host_layout import for_package
from test_platform_policy import write_linux_package
from test_release_audit import write_clean_package


class AppImageTests(unittest.TestCase):
    def test_mount_relocation_keeps_user_data(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            for mount in (root / "mount one", root / "mount two"):
                layout = for_package(mount / "usr/lib/triaevum", platform="linux",
                    environ={"APPDIR": str(mount), "HOME": str(root / "home")})
                self.assertEqual(layout.activation, root / "home/.local/share/TriAevum")
                self.assertFalse(layout.use_file_portal)

    def test_external_environment_does_not_relocate_other_packages(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            env = {"APPDIR": str(root / "other"), "HOME": str(root)}
            self.assertEqual(for_package(root / "app", platform="linux", environ=env).activation, root / "app")
            self.assertEqual(for_package(root / "other/app", platform="win32", environ=env).activation, root / "other/app")

    def test_invalid_storage_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            for env in ({"APPDIR": str(root)}, {"APPDIR": str(root), "XDG_DATA_HOME": str(root)}):
                with self.assertRaises(ValueError):
                    for_package(root / "usr/lib/triaevum", platform="linux", environ=env)

    def test_stage_preserves_audit_and_shader_helper_permissions(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_linux_package(root / "package")
            appdir = stage_appimage(root / "package", root / "AppDir")
            self.assertTrue(audit_release(appdir / "usr/lib/triaevum").ok)
            self.assertNotIn(b"\r", (appdir / "AppRun").read_bytes())
            launcher = (appdir / "AppRun").read_text()
            self.assertIn('--play "$@"', launcher)
            for forbidden in ("SDL_GAMECONTROLLERCONFIG=", "SDL_VIDEODRIVER=", "DISPLAY=", "unset LD_PRELOAD", "flatpak run"):
                self.assertNotIn(forbidden, launcher)

    def test_rejects_windows_private_data_and_existing_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_clean_package(root / "windows")
            with self.assertRaisesRegex(ValueError, "Linux"):
                stage_appimage(root / "windows", root / "AppDir")
            write_linux_package(root / "linux")
            (root / "linux/personal.cci").write_bytes(b"private")
            with self.assertRaisesRegex(ValueError, "audit"):
                stage_appimage(root / "linux", root / "AppDir")
            with self.assertRaisesRegex(ValueError, "new directory"):
                stage_appimage(root / "linux", root / "linux/nested")

    def test_failure_removes_only_new_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_linux_package(root / "package")
            with patch("tools.triaevum_release.appimage_package.shutil.copyfile", side_effect=OSError("disk full")):
                with self.assertRaises(OSError):
                    stage_appimage(root / "package", root / "AppDir")
            self.assertFalse((root / "AppDir").exists())
            self.assertTrue(audit_release(root / "package").ok)
