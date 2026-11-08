"""Tests for the MM3D whole-AOT inventory prune (stale producer rows)."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from whole_aot_inventory import prune  # noqa: E402

BASE = 0x1000


def image(words: list[int]) -> bytes:
    return b"".join(word.to_bytes(4, "little") for word in words)


def row(entry: int, size: int, name: str) -> dict[str, str]:
    return {"entry": f"0x{entry:08X}", "size": str(size), "name": name}


class PruneTest(unittest.TestCase):
    def setUp(self) -> None:
        words = [0xE3A00000] * 16
        words[4] = 0x00001020  # 0x1010: slot naming 0x1020, inside its function
        words[5] = 0x00001020
        words[8] = 0xE3A00001  # 0x1020: real code (second entry)
        self.code = image(words)
        self.rows = [
            row(0x1000, 0x40, "owner"),
            row(0x1010, 0x30, "ctr_interior_entry_00001010"),
            row(0x1020, 0x20, "ctr_interior_entry_00001020"),
            row(0x1008, 0x38, "mm3d_guest_patch_00001008"),
            row(0x100C, 0x34, "mm3d_guest_patch_0000100C"),
        ]

    def test_drops_stale_guest_patch_and_address_table_slot(self) -> None:
        kept, dropped = prune(self.code, self.rows, {0x1008}, BASE)
        self.assertEqual([r["name"] for r in kept],
                         ["owner", "ctr_interior_entry_00001020",
                          "mm3d_guest_patch_00001008"])
        self.assertEqual([entry for entry, _ in dropped], [0x1010, 0x100C])

    def test_negative_current_boundary_and_code_entry_are_kept(self) -> None:
        # Every guest patch row is a current boundary and the interior word is
        # code: nothing is dropped.
        words = list(int.from_bytes(self.code[i:i + 4], "little")
                     for i in range(0, len(self.code), 4))
        words[4] = 0xE3A00002
        kept, dropped = prune(image(words), self.rows, {0x1008, 0x100C}, BASE)
        self.assertEqual(dropped, [])
        self.assertEqual(len(kept), len(self.rows))


if __name__ == "__main__":
    unittest.main()
