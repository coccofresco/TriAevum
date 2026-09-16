import tempfile
from pathlib import Path
import unittest

from sample_aot_windows import classify_symbol, read_map


class SymbolTests(unittest.TestCase):
    def test_map_uses_image_base_and_coalesces_icf_aliases(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "test.map"
            path.write_text("Preferred load address is 0000000140000000\n"
                            " 0001:00000010 first 0000000140001010 f a.obj\n"
                            " 0001:00000010 alias 0000000140001010 f b.obj\n"
                            " 0001:00000020 second 0000000140001020 f c.obj\n")
            self.assertEqual(read_map(path), [(0x1010, "first"), (0x1020, "second")])

    def test_missing_base_is_rejected(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "bad.map"
            path.write_text("not a map")
            with self.assertRaises(ValueError):
                read_map(path)

    def test_shared_helper_not_attributed_to_icf_owner(self):
        self.assertEqual(classify_symbol("Oot3dAotEnterBlock@Execute_FUN_1234"), "block_entry_helper")

    def test_categories(self):
        for symbol, expected in (
            ("??$ReadFast@I", "memory_read_helper"),
            ("??$WriteFast@I", "memory_write_helper"),
            ("?RoundFinite@", "scalar_float_helper"),
            ("ExecuteOot3dWholeAotIndirect", "dispatch"),
            ("Execute_Mesh", "translated_body_including_inlined_helpers"),
            (None, "other_or_unresolved"),
        ):
            with self.subTest(symbol=symbol):
                self.assertEqual(classify_symbol(symbol), expected)


if __name__ == "__main__":
    unittest.main()
