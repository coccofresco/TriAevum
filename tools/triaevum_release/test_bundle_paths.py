from __future__ import annotations

import sys
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from bundle_paths import distribution_path, distribution_root, installation_root
import forge
import forge_gui


class BundlePathTests(unittest.TestCase):
    def test_frozen_gui_and_cli_share_portable_defaults_after_move(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            for package in (root / "original", root / "another folder" / "moved"):
                with (
                    patch.object(sys, "frozen", True, create=True),
                    patch.object(sys, "executable", str(package / "TriAevumForge.exe")),
                    patch.object(sys, "_MEIPASS", str(root / "unrelated-unpack"), create=True),
                    patch.dict(os.environ, {"LOCALAPPDATA": str(root / "appdata"),
                                            "HOME": str(root / "home"),
                                            "XDG_DATA_HOME": str(root / "xdg")}),
                ):
                    self.assertEqual(installation_root(), package)
                    self.assertEqual(forge.default_output_root(), package / "data/titles")
                    self.assertEqual(forge_gui.default_gui_data_root(), package / "data")
                    self.assertEqual(forge.default_active_title_state_path(), package / "data/active-title.json")
                    self.assertEqual(forge.default_translation_cache_root(), package / "data/translator-cache")
                    self.assertEqual(forge.default_runtime_launch_profile_path(), package / "TriAevum.launch.json")
            self.assertFalse((root / "appdata").exists())
            self.assertFalse((root / "home").exists())
            self.assertFalse((root / "xdg").exists())

    def test_source_root_is_repository(self) -> None:
        self.assertEqual(
            distribution_root(), Path(__file__).resolve().parents[2]
        )

    def test_frozen_root_uses_meipass(self) -> None:
        marker = Path("I:/isolated/frozen-root")
        with patch.object(sys, "_MEIPASS", str(marker), create=True):
            self.assertEqual(distribution_root(), marker.resolve())
            self.assertEqual(
                distribution_path("tools/recipe.json"),
                marker.resolve() / "tools/recipe.json",
            )


if __name__ == "__main__":
    unittest.main()
