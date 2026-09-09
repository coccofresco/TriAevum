import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

import forge
import forge_gui
from host_layout import FLATPAK_ID, for_package
from tools.triaevum_release import desktop_launcher


class HostLayoutTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.package = self.root / "read only app"
        self.xdg = self.root / "private user data"
        self.env = {"FLATPAK_ID": FLATPAK_ID, "XDG_DATA_HOME": str(self.xdg)}

    def test_windows_is_portable_even_with_flatpak_variables(self):
        layout = for_package(self.package, environ=self.env, platform="win32")
        self.assertEqual(layout.activation, self.package)
        self.assertEqual(layout.data, self.package / "data")
        self.assertFalse(layout.use_file_portal)

    def test_linux_outside_application_is_still_portable(self):
        for app_id in ("", "org.freedesktop.Sdk"):
            env = dict(self.env, FLATPAK_ID=app_id)
            self.assertEqual(for_package(self.package, environ=env, platform="linux").activation,
                             self.package)

    def test_flatpak_defaults_are_writable_and_shared(self):
        with (patch.object(sys, "platform", "linux"),
              patch("release_platform.platform.machine", return_value="x86_64"),
              patch.object(sys, "frozen", True, create=True),
              patch.object(sys, "executable", str(self.package / "TriAevumForge")),
              patch.dict(os.environ, self.env)):
            activation = self.xdg / "TriAevum"
            self.assertEqual(forge.default_runtime_launch_profile_path(), activation / "TriAevum.launch.json")
            self.assertEqual(forge.default_output_root(), activation / "data/titles")
            self.assertEqual(forge_gui.default_gui_data_root(), activation / "data")
            self.assertEqual(forge_gui.runtime_path(), self.package / "TriAevum")
            self.assertEqual(forge.default_runtime_plugin_path(), activation / "triaevum_title_aot.so")
        self.assertFalse(self.xdg.exists())

    def test_invalid_flatpak_root_fails_instead_of_writing_to_app(self):
        for root in ("", "relative", str(self.package)):
            with self.subTest(root=root), self.assertRaises(ValueError):
                for_package(self.package, platform="linux", environ=dict(self.env, XDG_DATA_HOME=root))

    def test_flatpak_launch_lock_lives_in_activation_root(self):
        with (patch("forge_gui.runtime_path", return_value=self.package / "TriAevum"),
              patch("forge_gui.for_package", return_value=for_package(
                  self.package, environ=self.env, platform="linux")),
              patch("forge_gui._launch_runtime_locked", return_value="child")):
            self.assertEqual(forge_gui.launch_runtime(), "child")
        self.assertFalse(self.package.exists())
        self.assertTrue((self.xdg / "TriAevum").is_dir())


class DesktopLauncherTests(unittest.TestCase):
    def test_first_run_and_explicit_forge_open_gui(self):
        with (patch.object(desktop_launcher.forge_gui, "load_active_title", return_value=None),
              patch.object(desktop_launcher.forge_gui, "main", return_value=0) as gui,
              patch.object(desktop_launcher.forge_gui, "launch_runtime") as launch):
            self.assertEqual(desktop_launcher.main([]), 0)
            self.assertEqual(desktop_launcher.main(["--forge"]), 0)
            self.assertEqual(gui.call_count, 2)
            launch.assert_not_called()

    def test_prepared_installation_plays_and_preserves_exit_code(self):
        with (patch.object(desktop_launcher.forge_gui, "load_active_title", return_value=object()),
              patch.object(desktop_launcher.forge_gui, "main") as gui,
              patch.object(desktop_launcher.forge_gui, "launch_runtime") as launch):
            launch.return_value.wait.return_value = 7
            self.assertEqual(desktop_launcher.main([]), 7)
            gui.assert_not_called()

    def test_invalid_installation_opens_repair_with_visible_error(self):
        with (patch.object(desktop_launcher.forge_gui, "load_active_title", return_value=object()),
              patch.object(desktop_launcher.forge_gui, "main", return_value=0) as gui,
              patch.object(desktop_launcher.forge_gui, "launch_runtime", side_effect=OSError("changed module"))):
            self.assertEqual(desktop_launcher.main([]), 0)
            gui.assert_called_once_with(startup_error="changed module")
