import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
import game_language
from common import load_json_object


class ForgeLanguageWidgetTests(unittest.TestCase):
    def test_actual_combobox_persists_selection_and_disables_during_install(self):
        import tkinter as tk
        from tkinter import ttk, filedialog, messagebox
        from forge_gui import ForgeWindow
        try:
            root = tk.Tk()
        except tk.TclError as exc:
            self.skipTest(str(exc))
        root.withdraw()
        try:
            with tempfile.TemporaryDirectory() as folder:
                data = Path(folder)
                game_language.install(data, {
                    "format": game_language.FORMAT, "selected": "en", "available": [
                        {"code": "en", "label": "English", "system_id": 1},
                        {"code": "it", "label": "Italiano", "system_id": 4}]})
                with patch("forge_gui.default_gui_data_root", return_value=data):
                    window = ForgeWindow(root, tk, ttk, filedialog, messagebox)
                self.assertEqual(tuple(window.language_combo.cget("values")), ("English", "Italiano"))
                self.assertEqual(str(window.language_combo.cget("state")), "readonly")
                window.language_combo.current(1)
                window.language_combo.event_generate("<<ComboboxSelected>>")
                root.update()
                self.assertEqual(load_json_object(game_language.config_path(data))["selected"], "it")
                window.busy = True
                window._refresh_actions()
                self.assertEqual(str(window.language_combo.cget("state")), "disabled")
        finally:
            root.destroy()
