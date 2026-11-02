from pathlib import Path
import tempfile
import unittest

from macos_binary_privacy import contains_host_path


class MacOSBinaryPrivacyTests(unittest.TestCase):
    def test_native_and_wide_strings_across_chunk_boundary(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / "binary"
            for value in ("/Users/synthetic-builder/source.cpp", "/home/synthetic-builder/source.cpp",
                          "C:\\Users\\synthetic-builder\\source.cpp", ".cargo/registry/src/crate/file.rs",
                          ".rustup/toolchains/toolchain/file.rs"):
                for encoding in ("utf-8", "utf-16le", "utf-32be"):
                    binary.write_bytes(b" " * (1024 * 1024 - 3) + value.encode(encoding))
                    self.assertTrue(contains_host_path(binary), (value, encoding))
            binary.write_bytes(b"/src/triaevum/runtime/source.cpp\0/build/triaevum/config.h\0/usr/lib/libSystem.B.dylib")
            self.assertFalse(contains_host_path(binary))


if __name__ == "__main__":
    unittest.main()
