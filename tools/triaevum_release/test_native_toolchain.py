import unittest

from native_toolchain import cxx_driver_arguments
from release_platform import LINUX, WINDOWS


class NativeToolchainTests(unittest.TestCase):
    def test_linux_keeps_cpp_driver_after_resolving_clang_symlink(self):
        self.assertEqual(cxx_driver_arguments(LINUX.target),
                         ("--driver-mode=g++", "--target=" + LINUX.target))

    def test_windows_retains_clang_cl_driver_and_abi(self):
        self.assertEqual(cxx_driver_arguments(WINDOWS.target), ("--target=" + WINDOWS.target,))

    def test_android_cannot_inherit_linux_x64_build_flags(self):
        with self.assertRaisesRegex(ValueError, "Unsupported release target"):
            cxx_driver_arguments("aarch64-linux-android")
