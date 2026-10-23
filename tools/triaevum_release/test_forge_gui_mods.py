import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import romfs_mods
from test_forge_gui_hd_download import FakeWorkerProcess


class ForgeOptionalModTests(unittest.TestCase):
    def _window(self, data):
        import tkinter as tk
        from tkinter import ttk, filedialog, messagebox
        from forge_gui import ForgeWindow
        try:
            root = tk.Tk()
        except tk.TclError as exc:
            self.skipTest(str(exc))
        root.withdraw()
        with patch("forge_gui.default_gui_data_root", return_value=data):
            window = ForgeWindow(root, tk, ttk, filedialog, messagebox)
        window.hd_auto_download.set(False)  # Isolate the mod download.
        return root, window

    def _install(self, window, download):
        with (patch("forge_gui.romfs_mods.download_archive", side_effect=download),
              patch("forge_gui.subprocess.Popen", FakeWorkerProcess),
              patch("forge_gui.installation_path", return_value=Path(".")),
              patch.object(window.messagebox, "showinfo") as info,
              patch.object(window.messagebox, "showerror") as error):
            window.rom.set("game.cci")
            window._install()
            window.install_thread.join(timeout=30)
            window._poll_events()
        return info, error

    def test_off_by_default_and_the_checkbox_is_the_persisted_setting(self):
        with tempfile.TemporaryDirectory() as folder:
            data = Path(folder)
            root, window = self._window(data)
            try:
                self.assertFalse(window.mod_enabled.get())
                self.assertIn("native models", window.mod_status.get())
                downloads = []
                self._install(window, lambda *args, **kwargs: downloads.append(args))
                self.assertEqual(downloads, [])
                window.mod_enabled.set(True)
                self.assertEqual(romfs_mods.enabled_mods(data), ("vanilla_plus_link",))
                self.assertIn("downloaded when you prepare", window.mod_status.get())
                window.mod_enabled.set(False)
                self.assertEqual(romfs_mods.enabled_mods(data), ())
            finally:
                root.destroy()

    def test_enabled_mod_is_downloaded_before_the_install(self):
        with tempfile.TemporaryDirectory() as folder:
            data = Path(folder)
            romfs_mods.set_mod_enabled(data, "vanilla_plus_link", True)
            root, window = self._window(data)
            try:
                self.assertTrue(window.mod_enabled.get())
                downloads = []

                def download(data_root, mod, contract, **kwargs):
                    downloads.append((data_root, mod.mod_id, contract.name))
                    kwargs["progress"](1, 1)
                    return data_root / "downloads" / contract.name

                info, error = self._install(window, download)
                self.assertEqual(downloads, [(data, "vanilla_plus_link", "link_remodel_vanilla_plus_1_9.rar")])
                info.assert_called_once()
                error.assert_not_called()
                self.assertFalse(window.busy)
            finally:
                root.destroy()

    def test_download_failure_keeps_installing_with_native_models(self):
        with tempfile.TemporaryDirectory() as folder:
            data = Path(folder)
            romfs_mods.set_mod_enabled(data, "vanilla_plus_link", True)
            root, window = self._window(data)
            try:
                info, error = self._install(window, ValueError("offline"))
                self.assertIn("Mod download failed: offline", window.mod_status.get())
                self.assertIn("native models", window.mod_status.get())
                info.assert_called_once()
                error.assert_not_called()
            finally:
                root.destroy()

    def test_malformed_settings_are_reported_not_replaced(self):
        with tempfile.TemporaryDirectory() as folder:
            data = Path(folder)
            romfs_mods.settings_path(data).parent.mkdir(parents=True)
            romfs_mods.settings_path(data).write_text("{}", encoding="utf-8")
            root, window = self._window(data)
            try:
                self.assertIn("Cannot read the mod settings", window.mod_status.get())
                self.assertEqual(str(window.mod_check.cget("state")), "disabled")
                self.assertEqual(romfs_mods.settings_path(data).read_text(encoding="utf-8"), "{}")
            finally:
                root.destroy()


if __name__ == "__main__":
    unittest.main()
