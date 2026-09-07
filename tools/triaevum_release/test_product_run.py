import struct
import tempfile
import unittest
from pathlib import Path

from validate_product_run import framebuffer_color_count


class ProductRunTests(unittest.TestCase):
    def test_pixel_check_ignores_alpha_and_rejects_blank_capture(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "frame.bmp"
            header = bytearray(54)
            struct.pack_into("<2sIHHI", header, 0, b"BM", 54 + 256, 0, 0, 54)
            struct.pack_into("<IiiHH", header, 14, 40, 8, 8, 1, 32)
            path.write_bytes(header + b"".join(bytes((0, 0, 0, i)) for i in range(64)))
            self.assertEqual(framebuffer_color_count(path), 1)
            path.write_bytes(header + b"".join(bytes((i, 0, 0, 255)) for i in range(64)))
            self.assertEqual(framebuffer_color_count(path), 64)
            path.write_bytes(header)
            with self.assertRaisesRegex(ValueError, "Truncated"):
                framebuffer_color_count(path)
