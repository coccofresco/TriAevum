"""Thumb decoding and lowering for the whole-AOT program (G3).

Exercises the real decoder (whole_aot_thumb.decode/walk/discover) and the
real C++ lowering path (whole_aot_cpp._load_thumb_instructions and
_render_function).  Rejected encodings are recorded negatives.
"""

from __future__ import annotations

import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

import whole_aot_cpp as cpp  # noqa: E402
import whole_aot_thumb as thumb  # noqa: E402

try:
    from capstone import CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB, Cs
except ImportError:  # pragma: no cover - the inventory venv provides capstone
    Cs = None

BASE = 0x00100000


def _halfwords(*values: int) -> bytes:
    return b"".join(value.to_bytes(2, "little") for value in values)


def _canonical(mnemonic: str, operands: str) -> tuple[str, str]:
    text = operands.replace(" ", "")
    text = re.sub(r"#(-?\d+),#(\d+)",
                  lambda m: "#0x%x" % (((int(m.group(1)) >> int(m.group(2))) |
                                        (int(m.group(1)) << (32 - int(m.group(2))))) & 0xFFFFFFFF),
                  text)
    text = re.sub(r"#(-?0x[0-9a-f]+|-?[0-9]+)", lambda m: "#%d" % int(m.group(1), 0), text)
    parts = text.split(",") if text else []
    if mnemonic in {"stmdb", "push"}:
        return "push", text.replace("sp!,", "")
    if mnemonic in {"ldm", "pop"}:
        return "pop", text.replace("sp!,", "")
    two_operand = {"adds", "subs", "ands", "eors", "lsls", "lsrs", "asrs", "adcs",
                   "sbcs", "rors", "orrs", "bics", "add", "sub", "muls"}
    if mnemonic in two_operand and len(parts) == 2 and "[" not in text:
        parts = [parts[0], parts[0], parts[1]]
    if mnemonic == "muls" and len(parts) == 3 and parts[0] == parts[2]:
        parts = [parts[0], parts[1], parts[0]]
    if mnemonic in {"add", "adds"} and len(parts) == 3 and "#" not in text:
        parts = [parts[0], *sorted(parts[1:])]
    return mnemonic.replace("negs", "rsbs"), ",".join(parts)


class DecoderTests(unittest.TestCase):
    @unittest.skipIf(Cs is None, "capstone unavailable")
    def test_every_accepted_t16_proxy_matches_capstone(self) -> None:
        thumb_cs = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
        arm_cs = Cs(CS_ARCH_ARM, CS_MODE_ARM)
        accepted = mismatched = 0
        examples = []
        for half in range(0x10000):
            if half >> 11 in (0x1D, 0x1E, 0x1F):
                continue
            code = _halfwords(half, 0xF800)
            try:
                item = thumb.decode(code, BASE, BASE)
            except thumb.ThumbDecodeError:
                continue
            if item.kind != "a32":
                continue
            accepted += 1
            t16 = next(thumb_cs.disasm(code[:2], BASE))
            a32 = next(arm_cs.disasm(item.raw.to_bytes(4, "little"), BASE))
            if _canonical(t16.mnemonic, t16.op_str) != _canonical(a32.mnemonic, a32.op_str):
                mismatched += 1
                examples.append((hex(half), t16.mnemonic, t16.op_str, a32.mnemonic, a32.op_str))
        self.assertGreater(accepted, 40000)
        self.assertEqual(mismatched, 0, examples[:10])

    def test_literal_load_is_rebased_on_the_odd_pc(self) -> None:
        # 0x100002: LDR r1, [pc, #8] -> Align(0x100006, 4) + 8 = 0x10000C.
        code = _halfwords(0x46C0, 0x4902, 0x46C0, 0x46C0, 0x46C0, 0x46C0, 0, 0)
        item = thumb.decode(code, BASE, BASE + 2)
        self.assertEqual(item.kind, "ldr_literal")
        self.assertEqual(item.literal, 0x0010000C)
        offset = item.raw & 0xFFF
        sign = 1 if item.raw & (1 << 23) else -1
        self.assertEqual(item.pc + 8 + sign * offset, 0x0010000C)

    def test_bl_and_blx_pairs(self) -> None:
        # BL +0 from 0x100000 -> 0x100004 (Thumb), BLX -> Align(0x100008,4)+0.
        code = _halfwords(0xF000, 0xF800, 0xF000, 0xE800)
        bl = thumb.decode(code, BASE, BASE)
        blx = thumb.decode(code, BASE, BASE + 4)
        self.assertEqual((bl.kind, bl.target, bl.size), ("bl", 0x00100005, 4))
        self.assertEqual((blx.kind, blx.target), ("blx_imm", 0x00100008))
        self.assertEqual(bl.next_pc, 0x00100005)

    def test_rejects_forms_outside_the_accepted_set(self) -> None:
        rejected = {
            "IT block": _halfwords(0xBF08, 0),
            "SVC": _halfwords(0xDF00, 0),
            "UDF": _halfwords(0xDE00, 0),
            "SXTH": _halfwords(0xB200, 0),
            "REV": _halfwords(0xBA00, 0),
            "BKPT": _halfwords(0xBE00, 0),
            "Thumb-2 LDR.W": _halfwords(0xF8D0, 0x1000),
            "unpaired BL prefix": _halfwords(0xF000, 0x2000),
            "BX pc": _halfwords(0x4778, 0),
            "MOV pc, r0": _halfwords(0x4687, 0),
            "ADD pc, r0": _halfwords(0x4487, 0),
            "MOV r0, pc": _halfwords(0x4678, 0),
            "CPS": _halfwords(0xB662, 0),
        }
        for name, code in rejected.items():
            with self.subTest(name):
                with self.assertRaises(thumb.ThumbDecodeError):
                    thumb.decode(code, BASE, BASE)


class FlowTests(unittest.TestCase):
    def test_walk_blocks_calls_and_noreturn_tail(self) -> None:
        code = _halfwords(
            0xB510,          # 0x00 push {r4, lr}
            0x2800,          # 0x02 cmp r0, #0
            0xD001,          # 0x04 beq 0x0A
            0xF000, 0xF801,  # 0x06 bl 0x0C
            0xBD10,          # 0x0A pop {r4, pc}
            0x4770,          # 0x0C bx lr
            0xF000, 0xE800,  # 0x0E blx Align(0x12,4)=0x10 (A32); link 0x12 outside range
        )
        flow = thumb.walk(code, BASE, ((BASE, BASE + 0x12),))
        self.assertIn(BASE + 0x0C + 1, flow.entries)
        self.assertEqual(flow.arm_call_targets, frozenset())
        starts = [pc for pc, _ in thumb.make_blocks(flow)]
        self.assertEqual(starts, [BASE + 1, BASE + 7, BASE + 0xB, BASE + 0xD])
        tail = thumb.walk(code, BASE, ((BASE + 0x0E, BASE + 0x12),))
        self.assertEqual(tail.noreturn_calls, frozenset({BASE + 0x0F}))
        self.assertEqual(tail.arm_call_targets, frozenset({BASE + 0x10}))

    def test_walk_rejects_flow_leaving_the_ranges(self) -> None:
        code = _halfwords(0x2000, 0x2101, 0x4770)
        with self.assertRaises(thumb.ThumbDecodeError):
            thumb.walk(code, BASE, ((BASE, BASE + 2),))

    def test_walk_rejects_a_literal_that_is_also_code(self) -> None:
        code = _halfwords(0x4800, 0x4770, 0x4770, 0)  # ldr r0,[pc,#0] -> 0x100004
        flow = thumb.walk(code, BASE, ((BASE, BASE + 4),))
        self.assertEqual(flow.literal_words, frozenset({BASE + 4}))
        with self.assertRaises(thumb.ThumbDecodeError):
            thumb.walk(code, BASE, ((BASE, BASE + 8),), (BASE + 4,))

    def test_discover_covers_pointer_entries_and_residue(self) -> None:
        code = bytearray(_halfwords(0x4770, 0x4770, 0x2000, 0x4770, 0, 0))
        code[8:12] = (BASE + 2 + 1).to_bytes(4, "little")
        found = thumb.discover(bytes(code), BASE, ((BASE, BASE + 8),))
        self.assertEqual(found.origins[BASE + 1], "range")
        self.assertEqual(found.origins[BASE + 3], "pointer")
        self.assertEqual(found.origins[BASE + 5], "residue")
        self.assertEqual(found.data_runs, ())


class LoweringTests(unittest.TestCase):
    def _function(self, code: bytes, record_tamper=None):
        flow = thumb.walk(code, BASE, ((BASE, BASE + len(code)),))
        blocks = []
        calls = []
        for index, (pc, items) in enumerate(thumb.make_blocks(flow)):
            records = [item.to_dict() for item in items]
            if record_tamper and index == 0:
                record_tamper(records)
            record = {"pc": pc, "end_pc": (items[-1].address + items[-1].size) | 1,
                      "state": "thumb", "thumb_instructions": records}
            instructions = cpp._load_thumb_instructions(record, code, BASE)
            successors = tuple(
                {"site": items[-1].pc, "kind": kind,
                 **({"target": target} if target is not None else {}),
                 **({"condition": cond} if cond else {})}
                for kind, target, cond in flow.edges(items[-1]))
            for edge in successors:
                if edge["kind"] == "direct_call":
                    calls.append((items[-1].pc, edge["target"]))
            blocks.append(cpp.Block(index, pc, record["end_pc"], instructions, successors))
        return cpp.Function(BASE + 1, "thumb_test", tuple(blocks), tuple(calls), (),
                            tuple(sorted(b.pc for b in blocks[1:])))

    def test_render_links_returns_and_literals(self) -> None:
        code = _halfwords(
            0xB510,          # 0x00 push {r4, lr}
            0x4903,          # 0x02 ldr r1, [pc, #12] -> 0x100010
            0xF000, 0xF802,  # 0x04 bl 0x10000C (Thumb callee in the range)
            0x4479,          # 0x08 add r1, pc  (r1 += 0x10000C)
            0xBD10,          # 0x0A pop {r4, pc}
            0x4770,          # 0x0C bx lr
            0x0000,          # 0x0E padding
            0x1234, 0x5678,  # 0x10 literal word
        )
        function = self._function(code)
        lines = "\n".join(cpp._render_function(
            function, {function.entry: "Self", 0x0010000D: "Callee"}, frozenset()))
        self.assertRegex(lines, r"(state\.R\[14\]|r14) = 0x00100009U;")
        self.assertIn("Execute_Callee(frame, context, state, 0x0010000DU)", lines)
        self.assertIn("callFlow.Pc != 0x00100009U", lines)
        # LDR literal: odd pc 0x100003 + 8 + 5 = Align(0x100006, 4) + 12.
        self.assertIn("0x0010000BU + 0x5U", lines)
        self.assertIn("+ 0x0010000CU;", lines)
        self.assertIn("return Oot3dAotReturned(value15);", lines)
        self.assertIn("case 0x00100009U:", lines)

    def test_tampered_record_is_refused(self) -> None:
        code = _halfwords(0x2001, 0x4770)

        def tamper(records):
            records[0]["raw"] ^= 1
        with self.assertRaises(ValueError):
            self._function(code, tamper)


if __name__ == "__main__":
    unittest.main()
