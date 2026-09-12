import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from build_forge_binary import configured_tk_data


class TkBundleTests(unittest.TestCase):
    def test_default_uses_standard_discovery(self):
        with patch.dict(os.environ, {}, clear=True):
            self.assertEqual(configured_tk_data(), ())

    def test_external_data_uses_runtime_hook_locations(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, marker in (("tcl", "init.tcl"), ("tk", "tk.tcl")):
                (root / name).mkdir()
                (root / name / marker).write_text("# test data")
            with patch.dict(os.environ, {"TCL_LIBRARY": str(root / "tcl"),
                                        "TK_LIBRARY": str(root / "tk")}, clear=True):
                self.assertEqual(configured_tk_data(), (
                    ((root / "tcl").resolve(), "_tcl_data"),
                    ((root / "tk").resolve(), "_tk_data")))

    def test_invalid_override_fails_before_build(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.dict(os.environ, {"TK_LIBRARY": directory}, clear=True):
                with self.assertRaisesRegex(ValueError, "tk.tcl"):
                    configured_tk_data()


if __name__ == "__main__":
    unittest.main()
