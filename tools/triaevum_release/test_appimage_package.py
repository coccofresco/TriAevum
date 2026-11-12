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
    def setUp(self):
        # These tests use inventory fixtures, not real ELF binaries. The ELF
        # parser and dependency failures are exercised by test_linux_elf_audit.
        gate = patch("tools.triaevum_release.appimage_package.audit_native_elfs")
        self.elf_audit = gate.start()
        self.addCleanup(gate.stop)

    def test_elf_failure_does_not_create_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_linux_package(root / "package")
            self.elf_audit.side_effect = ValueError("missing bundled dependency libdecor-0.so.0")
            with self.assertRaisesRegex(ValueError, "libdecor"):
                stage_appimage(root / "package", root / "AppDir")
            self.assertFalse((root / "AppDir").exists())

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
            # The desktop entry's icons (branding/README.md): the AppDir icon,
            # .DirIcon and the hicolor tree with the Forge action's icon.
            app_id = "io.github.coccofresco.TriAevum"
            for icon in (f"{app_id}.png", ".DirIcon", f"usr/share/icons/hicolor/512x512/apps/{app_id}.png",
                         f"usr/share/icons/hicolor/64x64/apps/{app_id}.Forge.png"):
                self.assertTrue((appdir / icon).read_bytes().startswith(b"\x89PNG"), icon)
            launcher = (appdir / "AppRun").read_text()
            self.assertIn('--play "$@"', launcher)
            for forbidden in ("SDL_GAMECONTROLLERCONFIG=", "SDL_VIDEODRIVER=", "DISPLAY=", "unset LD_PRELOAD", "flatpak run"):
                self.assertNotIn(forbidden, launcher)

    def test_mm3d_appdir_names_its_product_with_its_icons(self):
        # Owner decisions 2026-11-08: the MM3D product's desktop entry and
        # icons (titles/mm3d); OoT3D's stay the default.
        from tools.triaevum_release import appimage_package
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_linux_package(root / "package")
            appdir = stage_appimage(root / "package", root / "AppDir", title="mm3d")
            app_id = "io.github.coccofresco.TriAevum.MM3D"
            entry = (appdir / f"{app_id}.desktop").read_text(encoding="utf-8")
            self.assertIn("Name=TriAevum \u2014 Moonfall Curse", entry)
            self.assertIn("Exec=AppRun", entry)
            self.assertFalse((appdir / "io.github.coccofresco.TriAevum.desktop").exists())
            for icon in (f"{app_id}.png", ".DirIcon", f"usr/share/icons/hicolor/512x512/apps/{app_id}.png",
                         f"usr/share/icons/hicolor/64x64/apps/{app_id}.Forge.png"):
                self.assertTrue((appdir / icon).read_bytes().startswith(b"\x89PNG"), icon)
            self.assertEqual(sorted(p.name for p in (appdir / "usr/share/icons/hicolor/64x64/apps").iterdir()),
                             [f"{app_id}.Forge.png", f"{app_id}.png"])
            # Negatives: an entry naming the other product, an unknown title.
            wrong = root / "wrong.desktop"
            wrong.write_text(appimage_package.TITLES["oot3d"]["desktop"].read_text(encoding="utf-8"), encoding="utf-8")
            with patch.dict(appimage_package.TITLES["mm3d"], {"desktop": wrong}):
                with self.assertRaisesRegex(ValueError, "does not name its product"):
                    stage_appimage(root / "package", root / "AppDir2", title="mm3d")
            self.assertFalse((root / "AppDir2").exists())
            with self.assertRaisesRegex(ValueError, "Unknown title"):
                stage_appimage(root / "package", root / "AppDir3", title="other")

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

    def test_desktop_actions_match_the_forge_launcher(self):
        # Play and "Prepare or repair game" (UX plan WP-11) reach
        # `TriAevumForge --play [--forge]`, the only options --play accepts.
        apprun = (ASSETS / "AppRun").read_text()
        self.assertIn('exec "$package/TriAevumForge" --play "$@"\n', apprun)
        flatpak = ASSETS.parent / "flatpak"
        self.assertIn('exec /app/lib/triaevum/TriAevumForge --play "$@"\n', (flatpak / "triaevum").read_text())
        for desktop, command in ((ASSETS, "AppRun"), (flatpak, "triaevum")):
            entry = (desktop / "io.github.coccofresco.TriAevum.desktop").read_text(encoding="utf-8")
            with self.subTest(command=command):
                self.assertIn("Actions=Play;Forge;\n", entry)
                self.assertIn(f"[Desktop Action Play]\nName=Play\nExec={command}\n", entry)
                self.assertIn(f"[Desktop Action Forge]\nName=Prepare or repair game\nExec={command} --forge\n", entry)


class SignedAppImageTests(unittest.TestCase):
    def test_signed_package_keeps_its_signature_in_the_payload(self):
        from signed_package_fixture import audit_trusting, write_signed_linux_package
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            keys = write_signed_linux_package(root / "package", root)
            with patch("tools.triaevum_release.appimage_package.audit_native_elfs"), \
                 patch("tools.triaevum_release.appimage_package.audit_release", audit_trusting(keys)):
                appdir = stage_appimage(root / "package", root / "AppDir")
            payload = appdir / "usr/lib/triaevum"
            self.assertEqual((payload / "release-signature.json").read_bytes(),
                             (root / "package/release-signature.json").read_bytes())
            self.assertTrue(audit_release(payload, signing_keys=keys).summary["signed"])
