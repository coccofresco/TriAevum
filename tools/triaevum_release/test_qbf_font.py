import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import zipfile

from tools.oot3d.decomp_support.scripts.qbf_font import parse_qbf, pair_font_coverage
from tools.oot3d.decomp_support.scripts.prepare_qbf_font_pack import FONT_SOURCES, prepare_font_pack


def font_bytes(size=16, records=((32, 0, b"\x06\x04\0\0"), (42, 1, b"\x05\x06\0\0"))):
    glyphs = max(item[1] for item in records) + 1
    header = struct.pack("<4sHHHHBBBB", b"QBF1", len(records), glyphs, 42, 0, 4, size, size, 2)
    directory = b"".join(struct.pack("<HH", code, glyph) + metrics for code, glyph, metrics in records)
    pixels = b"".join(bytes([0x1f + index]) * (size * size // 2) for index in range(glyphs))
    return header + directory + pixels


class QbfFontTests(unittest.TestCase):
    def test_native_dimensions_metrics_and_nibble_order(self):
        font = parse_qbf(font_bytes())
        self.assertEqual((font.width, font.height, font.fallback_code), (16, 16, 42))
        self.assertEqual(font.characters[0].metrics, b"\x06\x04\0\0")
        self.assertEqual(font.glyph_coverage(0), bytes([17, 255]) * 128)

    def test_negative_and_outside_glyph_rejected(self):
        font = parse_qbf(font_bytes())
        for index in (-1, 2):
            with self.assertRaises(ValueError):
                font.glyph_coverage(index)

    def test_truncations_and_extra_data_rejected(self):
        data = font_bytes()
        for size in (0, 4, 15, 16, 25, len(data) - 1):
            with self.subTest(size=size), self.assertRaises(ValueError):
                parse_qbf(data[:size])
        with self.assertRaises(ValueError):
            parse_qbf(data + b"\0")

    def test_invalid_fields_rejected(self):
        for offset, value in ((0, 0), (4, 255), (6, 0), (8, 99), (12, 8),
                              (13, 0), (14, 15), (15, 4), (18, 255), (24, 32)):
            data = bytearray(font_bytes())
            data[offset] = value
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                parse_qbf(bytes(data))

    def test_pair_keeps_native_metrics_and_uses_codes_not_indices(self):
        native = parse_qbf(font_bytes())
        hd = parse_qbf(font_bytes(64, ((32, 1, b"\xff" * 4), (42, 0, b"\xff" * 4))))
        result = pair_font_coverage(native, hd)
        self.assertEqual(result["density"], 4)
        self.assertEqual(result["replacement_metric_differences"], 2)
        self.assertEqual(result["characters"][0], {"code": 32, "native_glyph": 0,
                         "coverage_glyph": 1, "native_metrics_hex": "06040000"})

    def test_incomplete_character_coverage_rejected(self):
        with self.assertRaisesRegex(ValueError, "missing native character"):
            pair_font_coverage(parse_qbf(font_bytes()),
                               parse_qbf(font_bytes(64, ((42, 0, b"\0" * 4),))))

    def test_fractional_and_reduced_density_rejected(self):
        for size in (8, 24):
            with self.subTest(size=size), self.assertRaises(ValueError):
                pair_font_coverage(parse_qbf(font_bytes()), parse_qbf(font_bytes(size)))

    def test_ambiguous_pointer_alias_rejected(self):
        native = parse_qbf(font_bytes(16, ((32, 0, b"\0" * 4), (42, 0, b"\0" * 4))))
        with self.assertRaisesRegex(ValueError, "alias"):
            pair_font_coverage(native, parse_qbf(font_bytes(64)))

    def test_import_is_reproducible_regional_and_never_contains_patch(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive, output = root / "mod.zip", root / "font.zip"
            with zipfile.ZipFile(archive, "w") as source:
                source.writestr(FONT_SOURCES[0][1], font_bytes(64))
                source.writestr("code.ips", b"DO NOT IMPORT")
            def read_font(_, path):
                if path == FONT_SOURCES[0][0]:
                    return font_bytes()
                raise FileNotFoundError(path)
            with patch("tools.oot3d.decomp_support.scripts.prepare_qbf_font_pack.read_level3_romfs_file",
                       side_effect=read_font):
                manifest = prepare_font_pack(archive=archive, romfs=root / "romfs", output=output)
                first = output.read_bytes()
                prepare_font_pack(archive=archive, romfs=root / "romfs", output=output)
            self.assertEqual(first, output.read_bytes())
            self.assertFalse(manifest["runtime_enabled"])
            self.assertEqual(len(manifest["fonts"]), 1)
            with zipfile.ZipFile(output) as prepared:
                self.assertEqual(len(prepared.namelist()), 2)
                self.assertEqual(json.loads(prepared.read("manifest.json")), manifest)
            self.assertNotIn(b"DO NOT IMPORT", first)

    def test_invalid_import_does_not_replace_existing_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive, output = root / "mod.zip", root / "font.zip"
            output.write_bytes(b"existing")
            with zipfile.ZipFile(archive, "w") as source:
                source.writestr(FONT_SOURCES[0][1], b"broken")
            with patch("tools.oot3d.decomp_support.scripts.prepare_qbf_font_pack.read_level3_romfs_file",
                       return_value=font_bytes()), self.assertRaises(ValueError):
                prepare_font_pack(archive=archive, romfs=root / "romfs", output=output)
            self.assertEqual(output.read_bytes(), b"existing")
            self.assertFalse(output.with_suffix(".zip.partial").exists())


if __name__ == "__main__":
    unittest.main()
