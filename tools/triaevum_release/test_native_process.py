import os
import sys
import ctypes
import subprocess
import tempfile
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

    def test_frozen_windows_removes_only_bundle_path_entries(self):
        environment = {"PATH": r"C:\\Windows;I:\\Forge\\_internal;I:\\Forge\\_internal\\bin;I:\\Forge\\_internal-other"}
        with patch.dict(os.environ, environment, clear=True), \
             patch.object(native_process.sys, "platform", "win32"), \
             patch.object(native_process.sys, "frozen", True, create=True), \
             patch.object(native_process.sys, "_MEIPASS", r"I:\\Forge\\_internal", create=True):
            child = native_process.native_process_environment()
            self.assertEqual(child["PATH"], r"C:\\Windows;I:\\Forge\\_internal-other")
            self.assertEqual(dict(os.environ), environment)

    def test_native_run_captures_checks_and_times_out(self):
        result = native_process.run_native([sys.executable, "-c", "print('native')"],
                                           capture_output=True, text=True, timeout=5, check=True)
        self.assertEqual(result.stdout.strip(), "native")
        with self.assertRaises(subprocess.CalledProcessError):
            native_process.run_native([sys.executable, "-c", "raise SystemExit(9)"], check=True)
        with self.assertRaises(subprocess.TimeoutExpired):
            native_process.run_native([sys.executable, "-c", "import time; time.sleep(10)"], timeout=0.05)

    @unittest.skipUnless(sys.platform == "win32", "Windows loader integration")
    def test_real_windows_loader_scope_restored_on_success_and_failure(self):
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.SetDllDirectoryW.argtypes = [ctypes.c_wchar_p]
        kernel.GetDllDirectoryW.argtypes = [ctypes.c_uint32, ctypes.c_wchar_p]
        def directory():
            buffer = ctypes.create_unicode_buffer(32768)
            kernel.GetDllDirectoryW(len(buffer), buffer)
            return buffer.value
        original = directory()
        try:
            with tempfile.TemporaryDirectory() as bundle, \
                 patch.object(native_process.sys, "frozen", True, create=True):
                self.assertTrue(kernel.SetDllDirectoryW(bundle))
                with native_process._native_library_search():
                    self.assertEqual(directory(), "")
                self.assertEqual(directory(), bundle)
                with self.assertRaises(OSError):
                    native_process.popen_native([os.path.join(bundle, "missing.exe")])
                self.assertEqual(directory(), bundle)
                result = native_process.run_native([sys.executable, "-c", "print('ok')"],
                                                   capture_output=True, text=True, timeout=5)
                self.assertEqual(result.stdout.strip(), "ok")
                self.assertEqual(directory(), bundle)
        finally:
            kernel.SetDllDirectoryW(original or None)
