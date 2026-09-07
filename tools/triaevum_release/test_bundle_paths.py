from __future__ import annotations

import sys
import unittest
from pathlib import Path
from unittest.mock import patch

from bundle_paths import distribution_path, distribution_root


class BundlePathTests(unittest.TestCase):
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
