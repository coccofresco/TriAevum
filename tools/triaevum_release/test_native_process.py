import os
import unittest
from unittest.mock import patch

import native_process


class NativeProcessTests(unittest.TestCase):
    def test_frozen_linux_restores_original_without_mutating_parent(self):
        for original in (None, "", "/steam/runtime/lib"):
            with self.subTest(original=original):
                environment = {"LD_LIBRARY_PATH": "/forge/_internal", "DISPLAY": ":0"}
                if original is not None:
                    environment["LD_LIBRARY_PATH_ORIG"] = original
                with patch.dict(os.environ, environment, clear=True), \
                     patch.object(native_process.sys, "platform", "linux"), \
                     patch.object(native_process.sys, "frozen", True, create=True):
                    child = native_process.native_process_environment()
                    self.assertEqual(dict(os.environ), environment)
                self.assertEqual(child.get("LD_LIBRARY_PATH"), original or None)
                self.assertNotIn("LD_LIBRARY_PATH_ORIG", child)
                self.assertEqual(child["DISPLAY"], ":0")

    def test_source_and_windows_environments_unchanged(self):
        for platform, frozen in (("linux", False), ("win32", True)):
            with self.subTest(platform=platform), \
                 patch.object(native_process.sys, "platform", platform), \
                 patch.object(native_process.sys, "frozen", frozen, create=True):
                self.assertEqual(native_process.native_process_environment(), dict(os.environ))
