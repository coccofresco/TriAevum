"""Static completeness / entry reachability proof (G3): positive and negatives."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from prove_whole_aot_coverage import load_data_facts, prove  # noqa: E402
import tempfile  # noqa: E402

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


def _data_program(tail: bytes) -> tuple[dict, dict, bytes]:
    """0x00 adr r0, 0x10 (add r0, pc, #8); 0x04 bx lr; 0x08.. tail data."""
    head = (0xE28F0008).to_bytes(4, "little") + (0xE12FFF1E).to_bytes(4, "little")
    program = {
        "base": BASE,
        "blocks": [{"id": 0, "pc": BASE, "end_pc": BASE + 8,
                    "successors": [{"site": BASE + 4, "kind": "return"}]}],
        "functions": [{"entry": BASE, "blocks": [0]}],
    }
    return program, {"functions": [{"entry": BASE}]}, head + tail


class DataClassTests(unittest.TestCase):
    def _classes(self, tail: bytes, adr: bool = True) -> dict:
        program, selection, code = _data_program(tail)
        if not adr:
            code = (0xE3A00000).to_bytes(4, "little") + code[4:]  # mov r0, #0
        result = prove(program, selection, code, len(code))
        self.assertEqual(result["residual_bytes"], 0, result["residual_runs"])
        return result["byte_classes"]

    def test_string_fill_nop_addressed_and_island(self) -> None:
        self.assertEqual(self._classes(b"abcd\0\0\0\0", adr=False)["string"], 4)  # the NUL word is padding
        self.assertEqual(self._classes(bytes([0xFF]) * 8, adr=False)["fill"], 8)
        nop = (0xE1A00000).to_bytes(4, "little")
        self.assertEqual(self._classes(nop * 2, adr=False)["padding"], 8)
        # ADR target 0x10 inside the run: addressed.
        self.assertEqual(self._classes(bytes(range(1, 13)))["addressed"], 12)
        # Runs 0x08 and 0x10 around a zero word: ADR names 0x10 (addressed);
        # 0x08 shares the non-code island with it (data_island).
        island = self._classes(bytes.fromhex("01020304") + bytes(4) + bytes.fromhex("05060708"))
        self.assertEqual((island["addressed"], island["data_island"]), (4, 4))

    def test_negative_thumb_return_is_not_a_string(self) -> None:
        # "pG\0\0" is BX lr: code-shaped, so it stays residual.
        program, selection, code = _data_program(bytes.fromhex("70470000"))
        code = (0xE3A00000).to_bytes(4, "little") + code[4:]
        result = prove(program, selection, code, len(code))
        self.assertEqual(result["residual_runs"], ["0x00100008+4"])

    def test_negative_prologue_is_never_data(self) -> None:
        # A Thumb PUSH {r4, lr} inside the island keeps the run residual.
        tail = bytes.fromhex("01020304") + bytes.fromhex("10b500bd")
        program, selection, code = _data_program(tail)
        result = prove(program, selection, code, len(code))
        self.assertEqual(result["residual_runs"], ["0x00100008+8"])

    def test_negative_unreferenced_bytes_stay_residual(self) -> None:
        program, selection, code = _data_program(bytes.fromhex("0102030405060708"))
        program["blocks"][0]["end_pc"] = BASE + 8
        code = (0xE3A00000).to_bytes(4, "little") + code[4:]  # mov r0, #0: no ADR
        result = prove(program, selection, code, len(code))
        self.assertEqual(result["residual_bytes"], 8)

    def _residual(self, tail: bytes) -> list[str]:
        program, selection, code = _data_program(tail)
        return prove(program, selection, code, len(code))["residual_runs"]

    def test_negative_a32_function_tail_is_never_data(self) -> None:
        # ADR names 0x10, but the run is the tail of a real A32 function
        # whose prologue lies elsewhere: ldr r0, [r6, #0xA8]; nop; vpop
        # {d8}; ldmia sp!, {r4-r10, lr} - code, so it stays residual.
        words = (0xE59600A8, 0xE320F000, 0xECBD8B04, 0xE8BD47F0)
        tail = b"".join(word.to_bytes(4, "little") for word in words)
        self.assertEqual(self._residual(tail), ["0x00100008+16"])

    def test_negative_a32_branch_word_is_never_data(self) -> None:
        # A lone AL BL into the image (bl 0x100000) is code, even addressed.
        tail = (0xEBFFFFFC).to_bytes(4, "little") + bytes.fromhex("01020304")
        self.assertIn("0x00100008+8", self._residual(tail))

    def test_negative_a32_straight_line_is_never_data(self) -> None:
        # Six AL-condition words in a row without a terminator.
        tail = b"".join((0xE1A00000 | index << 12).to_bytes(4, "little") for index in range(1, 7))
        self.assertEqual(self._residual(tail), ["0x00100008+24"])

    def test_negative_thumb_branch_line_is_never_data(self) -> None:
        # ldr r2, [r0]; ldr r1, [r2]; bpl; b: straight-line Thumb ending in B.
        tail = bytes.fromhex("0268 1168 01d5 09e0".replace(" ", ""))
        self.assertEqual(self._residual(tail), ["0x00100008+8"])

    def test_reciprocal_table_and_byte_table_stay_data(self) -> None:
        # 0xE38E38E3 (a reciprocal constant) is not an arithmetic AL word, and
        # a byte table holding 0x0000 is not Thumb code even when a 0xE4E0
        # halfword decodes as B: both stay addressed data.
        reciprocal = b"".join(word.to_bytes(4, "little") for word in (0xF0F0F0F0, 0xE38E38E3, 0xD79435E5))
        self.assertEqual(self._classes(reciprocal)["addressed"], 12)
        table = bytes.fromhex("ff000000 00041014 40445054 e0e4f0f4".replace(" ", ""))
        self.assertEqual(self._classes(table)["addressed"], 16)

    def test_guarded_title_data_facts(self) -> None:
        program, selection, code = _data_program(bytes.fromhex("0102030405060708"))
        code = (0xE3A00000).to_bytes(4, "little") + code[4:]
        with tempfile.TemporaryDirectory() as temporary:
            facts = Path(temporary) / "facts.csv"
            facts.write_text("start,size,guard_word,meaning\n"
                             f"0x{BASE + 8:08X},8,0x04030201,table\n", encoding="utf-8")
            loaded = load_data_facts(facts, code, BASE)
            result = prove(program, selection, code, len(code), data_facts=loaded)
            self.assertEqual(result["residual_bytes"], 0)
            self.assertEqual(result["byte_classes"]["title_data"], 8)
            facts.write_text("start,size,guard_word,meaning\n"
                             f"0x{BASE + 8:08X},8,0x04030200,table\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "guard"):
                load_data_facts(facts, code, BASE)


if __name__ == "__main__":
    unittest.main()
