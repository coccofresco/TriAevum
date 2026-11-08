import struct
import tempfile
import unittest
from pathlib import Path

from fragment_family import HEADER, MAGIC, VERSION, check, empty_pack, main

TRACKED = Path(__file__).resolve().parents[1] / "shaders" / "mm3d_native_fragment.spvpack"


class FragmentFamilyTests(unittest.TestCase):
    def test_empty_pack_is_header_only(self):
        self.assertEqual(empty_pack(), struct.pack("<4I", MAGIC, VERSION, 0, HEADER))
        check(empty_pack())

    def test_tracked_pack_is_the_empty_family(self):
        self.assertEqual(TRACKED.read_bytes(), empty_pack())

    def test_refuses_a_pack_with_entries(self):
        # A copied shared family (non-zero entry count) would be prepared twice.
        data = struct.pack("<4I", MAGIC, VERSION, 1, HEADER + 8) + b"\0" * 8
        with self.assertRaisesRegex(ValueError, "must be empty"):
            check(data)

    def test_refuses_bad_magic_size_and_truncation(self):
        with self.assertRaises(ValueError):
            check(struct.pack("<4I", 0x12345678, VERSION, 0, HEADER))
        with self.assertRaises(ValueError):
            check(struct.pack("<4I", MAGIC, VERSION, 0, HEADER + 4))
        with self.assertRaisesRegex(ValueError, "truncated"):
            check(b"SPVK")

    def test_cli_writes_then_checks(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / "f.spvpack"
            self.assertEqual(main(["--out", str(out)]), 0)
            self.assertEqual(main(["--check", str(out)]), 0)
            self.assertEqual(out.read_bytes(), empty_pack())


if __name__ == "__main__":
    unittest.main()
