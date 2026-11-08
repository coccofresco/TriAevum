"""Static completeness / entry reachability proof (G3): positive and negatives."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from prove_whole_aot_coverage import prove  # noqa: E402

BASE = 0x00100000


def _image() -> bytes:
    words = [
        0xE59F0000,  # 0x00 ldr r0, [pc, #0] -> literal 0x08
        0xFA000000,  # 0x04 blx 0x0C (Thumb)
        0x12345678,  # 0x08 literal
    ]
    code = b"".join(word.to_bytes(4, "little") for word in words)
    return code + bytes.fromhex("7047") + b"\x00\x00"  # 0x0C bx lr; 0x0E pad


def _program() -> dict:
    return {
        "base": BASE,
        "blocks": [
            {"id": 0, "pc": BASE, "end_pc": BASE + 8, "successors": [
                {"site": BASE + 4, "kind": "thumb_call", "target": BASE + 0x0D},
                {"site": BASE + 4, "kind": "resume", "target": BASE + 8}]},
            {"id": 1, "pc": BASE + 0x0D, "end_pc": BASE + 0x0F, "state": "thumb",
             "thumb_instructions": [{"pc": BASE + 0x0D, "size": 2}],
             "successors": [{"site": BASE + 0x0D, "kind": "return"}]},
        ],
        "functions": [
            {"entry": BASE, "blocks": [0]},
            {"entry": BASE + 0x0D, "blocks": [1]},
        ],
    }


class ProofTests(unittest.TestCase):
    def test_complete_image_passes_except_resume_into_data(self) -> None:
        selection = {"functions": [{"entry": BASE}, {"entry": BASE + 0x0D}]}
        result = prove(_program(), selection, _image(), len(_image()))
        self.assertEqual(result["residual_bytes"], 0)
        self.assertEqual(result["byte_classes"]["literal"], 4)
        self.assertEqual(result["byte_classes"]["code"], 10)
        # The resume edge targets 0x08, a literal: reachability must flag it.
        self.assertEqual(result["reachability"]["failure_count"], 1)

    def test_unselected_thumb_function_is_residual_and_unreachable(self) -> None:
        selection = {"functions": [{"entry": BASE}]}
        result = prove(_program(), selection, _image(), len(_image()))
        self.assertGreater(result["residual_bytes"], 0)
        failures = " ".join(result["reachability"]["failures"])
        self.assertIn("untranslated 0x0010000D", failures)

    def test_unknown_selection_is_refused(self) -> None:
        with self.assertRaises(ValueError):
            prove(_program(), {"functions": [{"entry": BASE + 0x40}]}, _image(), 0x10)


if __name__ == "__main__":
    unittest.main()
