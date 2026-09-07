import struct
import json
import tempfile
import unittest
from pathlib import Path

from analyze_frame_sequence import analyze, pixel_identity


class FrameSequenceTests(unittest.TestCase):
    def test_pixel_hash_ignores_alpha_and_padding(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            def write(name, depth, pixels):
                header = bytearray(54)
                header[:2] = b"BM"
                struct.pack_into("<I", header, 10, 54)
                struct.pack_into("<I", header, 14, 40)
                struct.pack_into("<iiHHI", header, 18, 1, 1, 1, depth, 0)
                path = root / name
                path.write_bytes(header + pixels)
                return path
            a = write("a.bmp", 24, b"\x01\x02\x03\x00")
            b = write("b.bmp", 32, b"\x01\x02\x03\xff")
            c = write("c.bmp", 24, b"\x01\x02\x04\x00")
            self.assertEqual(pixel_identity(a), pixel_identity(b))
            result = analyze([a, b, c])
            self.assertEqual(result["unique_pixel_images"], 2)
            self.assertEqual(result["identical_adjacent_pairs"], 1)
            for ordinal, path in enumerate((a, c)):
                path.with_name(path.name + ".json").write_text(json.dumps({
                    "format": "triaevum_framebuffer_sample_v1", "width": 1, "height": 1,
                    "temporal_sample": {"previous_source": 1, "current_source": 2,
                        "continuity_epoch": 1, "multiplier": 2, "ordinal": ordinal,
                        "alpha": ordinal / 2, "history_reset": False}}))
            temporal = analyze([a, c])["temporal_attribution"]
            self.assertEqual(temporal["complete_intervals"], 1)
            self.assertEqual(temporal["complete_intervals_with_distinct_pixels_per_ordinal"], 1)
            self.assertEqual(analyze([a, b])["temporal_attribution"]["complete_intervals"], 0)
            c.write_bytes(b"BM")
            with self.assertRaises(ValueError):
                pixel_identity(c)
