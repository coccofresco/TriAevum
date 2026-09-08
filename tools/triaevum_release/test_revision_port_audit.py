from __future__ import annotations

import struct
import unittest

from revision_port_audit import compare_windows, text_layout


class RevisionPortAuditTests(unittest.TestCase):
    def test_identical_window(self):
        data = bytes(range(64))
        result = compare_windows(data, data, [0x1000], reference_base=0x1000, target_base=0x1000)
        self.assertEqual(result[0]["status"], "unique_same_address")

    def test_relocated_window(self):
        data = bytes(range(64))
        result = compare_windows(data, b"\xff" * 16 + data, [0x1000], reference_base=0x1000, target_base=0x1000)
        self.assertEqual(result[0]["status"], "unique_relocated")
        self.assertEqual(result[0]["delta"], 16)

    def test_repeated_window_is_ambiguous_even_at_original_address(self):
        data = bytes(range(32))
        result = compare_windows(data, data + data, [0x1000], reference_base=0x1000, target_base=0x1000)
        self.assertEqual(result[0]["status"], "ambiguous")
        self.assertTrue(result[0]["same_address_bytes_equal"])
        self.assertNotIn("delta", result[0])

    def test_changed_window_is_not_relocated_by_neighbour(self):
        data = bytes(range(64))
        result = compare_windows(data, b"\xff" + data[1:], [0x1000, 0x1020], reference_base=0x1000, target_base=0x1000)
        self.assertEqual(result[0]["status"], "changed_or_missing")
        self.assertEqual(result[1]["status"], "unique_same_address")

    def test_unaligned_and_outside_addresses_are_rejected(self):
        data = bytes(range(64))
        result = compare_windows(data, data, [0xFFF, 0x1001, 0x1030], reference_base=0x1000, target_base=0x1000)
        self.assertTrue(all(r["status"] == "outside_text_window" for r in result))

    def test_short_signature_rejected(self):
        with self.assertRaises(ValueError):
            compare_windows(b"test", b"test", [0], reference_base=0, target_base=0, window=4)

    def test_exheader_bounds(self):
        header = bytearray(32)
        struct.pack_into("<III", header, 0x10, 0x100000, 1, 0x800)
        self.assertEqual(text_layout(bytes(4096), header), (0x100000, 0x800))
        with self.assertRaises(ValueError):
            text_layout(bytes(2048), header)


if __name__ == "__main__":
    unittest.main()
