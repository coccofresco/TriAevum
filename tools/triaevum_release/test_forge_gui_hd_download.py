import io
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import ui_texture_pack


class FakeWorkerProcess:
    """The install worker subprocess: one complete event, exit 0."""

    def __init__(self, *_args, **_kwargs):
        self.stdout = io.StringIO(json.dumps({"forge_worker_event": "complete",
                                              "payload": {"status": "ready"}}) + "\n")

    def wait(self):
        return 0

    def poll(self):
        return 0

    def kill(self):
        pass


class ForgeHdDownloadTests(unittest.TestCase):
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
        return root, window

    def _install(self, window, downloads):
        def fake_download(data_root, pack, contract, progress=lambda *_: None, **_kwargs):
            self.assertIs(pack, ui_texture_pack.VANILLA_UI_HUD)
            downloads.append(data_root)
            progress(1, 1)
            return data_root / "downloads" / contract.name

        class Process(FakeWorkerProcess):
            pass

        with (patch("forge_gui.romfs_mods.download_archive", side_effect=fake_download),
              patch("forge_gui.subprocess.Popen", Process),
              patch("forge_gui.installation_path", return_value=Path(".")),
              patch.object(window.messagebox, "showinfo"),
              patch.object(window.messagebox, "showerror")):
            window.rom.set("game.cci")
            window._install()
            window.install_thread.join(timeout=30)
            window._poll_events()

    def test_checkbox_is_on_by_default_and_downloads_before_the_install(self):
        with tempfile.TemporaryDirectory() as folder:
            data = Path(folder)
            root, window = self._window(data)
            try:
                self.assertTrue(window.hd_auto_download.get())
                self.assertIn("downloaded when you prepare", window.hd_status.get())
                downloads = []
                self._install(window, downloads)
                self.assertEqual(downloads, [data])
                self.assertFalse(window.busy)
            finally:
                root.destroy()

    def test_unchecked_or_stored_archive_skips_the_download(self):
        with tempfile.TemporaryDirectory() as folder:
            data = Path(folder)
            root, window = self._window(data)
            try:
                window.hd_auto_download.set(False)
                self.assertIn("Not installed", window.hd_status.get())
                downloads = []
                self._install(window, downloads)
                self.assertEqual(downloads, [])
                window.hd_auto_download.set(True)
                with patch("forge_gui.ui_texture_pack.pack_status", return_value="archive"):
                    self._install(window, downloads)
                self.assertEqual(downloads, [])
            finally:
                root.destroy()

    def test_download_failure_keeps_installing_with_native_textures(self):
        with tempfile.TemporaryDirectory() as folder:
            data = Path(folder)
            root, window = self._window(data)
            try:
                with (patch("forge_gui.romfs_mods.download_archive",
                            side_effect=ValueError("offline")),
                      patch("forge_gui.subprocess.Popen", FakeWorkerProcess),
                      patch("forge_gui.installation_path", return_value=Path(".")),
                      patch.object(window.messagebox, "showinfo") as info,
                      patch.object(window.messagebox, "showerror") as error):
                    window.rom.set("game.cci")
                    window._install()
                    window.install_thread.join(timeout=30)
                    window._poll_events()
                self.assertIn("download failed: offline", window.hd_status.get())
                self.assertIn("native textures", window.hd_status.get())
                info.assert_called_once()
                error.assert_not_called()
                self.assertFalse(window.busy)
            finally:
                root.destroy()


if __name__ == "__main__":
    unittest.main()
