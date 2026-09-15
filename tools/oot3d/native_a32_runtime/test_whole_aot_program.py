from __future__ import annotations

import csv
import json
import sys
import tempfile
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parent))
from whole_aot_program import build_program, pinned


BASE = 0x1000


def branch(pc: int, target: int, *, condition: int = 0xE, link: bool = False) -> int:
    displacement = target - (pc + 8)
    if displacement % 4:
        raise ValueError("unaligned branch target")
    return (
        (condition << 28)
        | 0x0A000000
        | (0x01000000 if link else 0)
        | ((displacement >> 2) & 0x00FFFFFF)
    )


class WholeAotProgramTest(unittest.TestCase):
    def test_explicit_link_accumulates_offsets_and_crosses_unrelated_shifted_alu(self):
        words = [0xE28FE010, 0xE28EE008, 0xE0851107, 0xE1A0F006,
                 0, 0, 0, 0, 0xE12FFF1E]
        slots = set(range(BASE, BASE + len(words) * 4, 4))
        def resolve():
            decoder = pinned._Decoder(b"".join(w.to_bytes(4, "little") for w in words), BASE)
            return pinned._explicit_lr_call_return_site(decoder, slots, BASE + 12, set())
        self.assertEqual(resolve(), BASE + 32)
        for clobber in (0xE085E107, 0xE591E000, 0x128EE008, 0xEB000000):
            words[2] = clobber
            self.assertIsNone(resolve())

    def test_plain_predicated_branch_preserves_an_explicit_link_continuation(self):
        words = [0xE28FE018, branch(BASE + 4, BASE + 24, condition=1),
                 0xE12FFF1E, 0, 0, 0, 0xE12FFF1E, 0, 0xE12FFF1E]
        with tempfile.TemporaryDirectory() as temporary:
            inventory, audit = self._inputs(Path(temporary),
                                           [(BASE, 12, "linked_branch"), (BASE + 24, 4, "callee")])
            program = build_program(b"".join(w.to_bytes(4, "little") for w in words),
                                    inventory, audit, base=BASE)
        first = next(block for block in program.blocks if block.pc == BASE)
        self.assertEqual({(e.kind, e.target) for e in first.successors},
                         {("branch", BASE + 24), ("resume", BASE + 32), ("fallthrough", BASE + 8)})
        self.assertIn(BASE + 32, {b.pc for b in program.blocks})
        self.assertEqual(program.unclaimed_blocks, ())

    def test_constant_pc_destination_keeps_conditional_fallthrough(self):
        words = [0xE28F4010, 0xE2844008, 0xE3500000, 0xD1A0F004,
                 0xE12FFF1E, 0, 0, 0, 0xE3A00001, 0xE12FFF1E]
        with tempfile.TemporaryDirectory() as temporary:
            inventory, audit = self._inputs(Path(temporary), [(BASE, 20, "constant_jump")])
            program = build_program(b"".join(w.to_bytes(4, "little") for w in words),
                                    inventory, audit, base=BASE)
        first = next(block for block in program.blocks if block.pc == BASE)
        self.assertEqual({edge.target for edge in first.successors}, {BASE + 16, BASE + 32})
        self.assertIn(BASE + 32, {block.pc for block in program.blocks})
        self.assertEqual(program.unclaimed_blocks, ())
        for clobber in (0xE5914000, 0x13A04000, 0xEB000000):
            words[1] = clobber  # Unknown load, predicated write, or call.
            decoder = pinned._Decoder(b"".join(w.to_bytes(4, "little") for w in words), BASE)
            self.assertEqual(pinned._constant_pc_targets(decoder,
                set(range(BASE, BASE + 16, 4)), set(range(BASE, BASE + len(words)*4, 4))), {})

    def test_relative_table_load_can_precede_link_register_restoration(self):
        words = [0xE28F6018, 0xE7965100, 0xE1A0E008, 0xE085F006,
                 0, 0, 0, 0, 0x10, 0x18, 0xFFFFFFFF, 0,
                 0xE3A00001, 0xE12FFF1E, 0xE3A00002, 0xE12FFF1E]
        with tempfile.TemporaryDirectory() as temporary:
            inventory, audit = self._inputs(Path(temporary), [(BASE, 16, "table_jump")])
            program = build_program(b"".join(w.to_bytes(4, "little") for w in words),
                                    inventory, audit, base=BASE)
        first = next(block for block in program.blocks if block.pc == BASE)
        self.assertEqual({edge.target for edge in first.successors}, {0x1030, 0x1038})
        words[2] = 0xE1A05008  # Replacing the loaded offset invalidates the pattern.
        decoder = pinned._Decoder(b"".join(w.to_bytes(4, "little") for w in words), BASE)
        self.assertEqual(pinned._base_relative_switch_tables(decoder,
            set(range(BASE, BASE + 16, 4)), set(range(BASE, BASE + len(words)*4, 4))), {})

    def test_explicit_link_can_skip_the_instructions_after_computed_call(self):
        words = [0xE28FE010, 0xE1A05000, 0xE1A0F006, 0xFFFFFFFF,
                 0xFFFFFFFF, 0xFFFFFFFF, 0xE3A00001, 0xE12FFF1E]
        with tempfile.TemporaryDirectory() as temporary:
            inventory, audit = self._inputs(Path(temporary), [(BASE, 12, "call")])
            program = build_program(b"".join(w.to_bytes(4, "little") for w in words),
                                    inventory, audit, base=BASE)
        first = next(block for block in program.blocks if block.pc == BASE)
        self.assertIn(("resume", BASE + 24), {(edge.kind, edge.target) for edge in first.successors})
        self.assertNotIn(BASE + 12, {block.pc for block in program.blocks})
        self.assertIn(BASE + 24, {block.pc for block in program.blocks})
        self.assertEqual(program.unclaimed_blocks, ())

    def test_explicit_link_does_not_override_blx_or_cross_lr_clobbers(self):
        words = [0xE28FE010, 0xE12FFF36, 0xE12FFF1E, 0, 0, 0, 0xE12FFF1E]
        with tempfile.TemporaryDirectory() as temporary:
            inventory, audit = self._inputs(Path(temporary), [(BASE, 12, "blx")])
            program = build_program(b"".join(w.to_bytes(4, "little") for w in words),
                                    inventory, audit, base=BASE)
        first = next(block for block in program.blocks if block.pc == BASE)
        self.assertIn(("resume", BASE + 8), {(edge.kind, edge.target) for edge in first.successors})
        for clobber in (0xE791E002, 0xE1A0E086):  # LDR lr,[r1,r2]; LSL lr,r6,#1.
            code = [0xE28FE010, clobber, 0xE1A0F006, 0, 0, 0, 0xE12FFF1E]
            decoder = pinned._Decoder(b"".join(w.to_bytes(4, "little") for w in code), BASE)
            self.assertIsNone(pinned._explicit_lr_call_return_site(
                decoder, set(range(BASE, BASE + len(code) * 4, 4)), BASE + 8, set()))

    def test_relative_switch_crosses_shift_alias_but_not_base_clobber(self):
        words = [0xE12FFF1E, 0xE12FFF1E, 0xFFFFFFF8, 0xFFFFFFFC,
                 0xE24F5010, 0xE1B07B86, 0xE7956008, 0xE085F006]
        slots = set(range(BASE, BASE + len(words) * 4, 4))
        reached = slots - {BASE + 8, BASE + 12}
        def recover():
            decoder = pinned._Decoder(b"".join(w.to_bytes(4, "little") for w in words), BASE)
            return pinned._base_relative_switch_tables(decoder, reached, slots)
        self.assertIn(BASE + 28, recover())
        with tempfile.TemporaryDirectory() as temporary:
            inventory, audit = self._inputs(Path(temporary), [(BASE + 16, 16, "shift_switch")])
            program = build_program(b"".join(w.to_bytes(4, "little") for w in words),
                                    inventory, audit, base=BASE)
        self.assertTrue({BASE, BASE + 4} <= {block.pc for block in program.blocks})
        self.assertTrue({BASE + 8, BASE + 12} <= program.literal_data)
        words[5] = 0xE1B05B86  # LSLS r5,r6,#23 overwrites the ADR base.
        self.assertEqual(recover(), {})

    def test_negative_relative_table_with_intervening_index_arithmetic(self):
        words = [0xE12FFF1E, 0xE12FFF1E, 0xFFFFFFF8, 0xFFFFFFFC,
                 0xE24F5010, 0xE3A08000, 0xE3500000, 0x02888004,
                 0xE7956008, 0xE085F006]
        decoder = pinned._Decoder(b"".join(w.to_bytes(4, "little") for w in words), BASE)
        slots = set(range(BASE, BASE + len(words)*4, 4))
        reached = slots - {BASE+8, BASE+12}
        tables = pinned._base_relative_switch_tables(decoder, reached, slots)
        self.assertEqual(tables[BASE+36], (BASE+8, (BASE+8, BASE+12)))
        words[5] = 0xE3A05000  # Base clobber: must not reuse an earlier ADR.
        decoder = pinned._Decoder(b"".join(w.to_bytes(4, "little") for w in words), BASE)
        self.assertEqual(pinned._base_relative_switch_tables(decoder, reached, slots), {})

    def test_relative_table_requires_an_explicit_base_and_unshifted_pc_add(self):
        words = [0xE1A05001, 0xE7956100, 0xE085F006, 0, 8, 12,
                 0xE12FFF1E, 0xE12FFF1E]
        slots = set(range(BASE, BASE + len(words)*4, 4))
        def recover():
            decoder = pinned._Decoder(b"".join(w.to_bytes(4, "little") for w in words), BASE)
            return pinned._base_relative_switch_tables(decoder, {BASE, BASE+4, BASE+8}, slots)
        self.assertEqual(recover(), {})  # Register-sourced base is not evidence.
        words[0] = 0xE28F5008  # ADR r5, table at 0x1010.
        self.assertIn(BASE+8, recover())
        words[2] = 0xE085F086  # ADD pc,r5,r6,lsl#1 is not this table encoding.
        self.assertEqual(recover(), {})

    def test_base_relative_switch_targets_are_code_not_self_relative_pointers(self):
        words = [0] * 16
        words[:3] = [0xE28F5018, 0xE7956100, 0xE085F006]
        words[8:10] = [0x10, 0x18]  # Both offsets are relative to 0x1020.
        words[10] = 0xFFFFFFFF  # Unaligned target terminates the table.
        words[12:16] = [0xE3A00001, 0xE12FFF1E, 0xE3A00002, 0xE12FFF1E]
        code = b"".join(word.to_bytes(4, "little") for word in words)
        with tempfile.TemporaryDirectory() as temporary:
            inventory, audit = self._inputs(Path(temporary), [(BASE, 12, "switch")])
            program = build_program(code, inventory, audit, base=BASE)
        block = next(block for block in program.blocks if block.pc == BASE)
        self.assertEqual({edge.target for edge in block.successors}, {0x1030, 0x1038})
        self.assertTrue({0x1020, 0x1024} <= program.literal_data)
        self.assertNotIn(0x1028, program.literal_data)
        self.assertEqual(program.unclaimed_blocks, ())

    def test_excluded_switch_case_remains_executable_without_public_abi(self):
        words = [0xE12FFF1E, 0, 0, 0, 0xE3A00006, 0xE12FFF1E]
        code = b"".join(word.to_bytes(4, "little") for word in words)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inventory, audit = self._inputs(root, [(0x1000, 4, "entry"), (0x1010, 8, "case6")])
            audit.write_text("entry,classification\n0x1010,switch_owned_case\n", encoding="utf-8")
            program = build_program(code, inventory, audit, base=BASE)
        functions = {function.entry: function for function in program.functions}
        self.assertEqual(functions[0x1010].origin, "internal_dispatch")
        self.assertTrue(functions[0x1010].closed_static_cfg)
        self.assertEqual(program.unclaimed_blocks, ())

    def _inputs(
        self,
        root: Path,
        rows: list[tuple[int, int, str]],
    ) -> tuple[Path, Path]:
        inventory = root / "inventory.csv"
        with inventory.open("w", newline="", encoding="utf-8") as target:
            writer = csv.DictWriter(target, fieldnames=("entry", "size", "name"))
            writer.writeheader()
            for entry, size, name in rows:
                writer.writerow(
                    {"entry": hex(entry), "size": hex(size), "name": name}
                )
        audit = root / "audit.csv"
        audit.write_text("entry\n", encoding="utf-8")
        return inventory, audit

    def test_recovers_function_cfg_call_and_literal(self) -> None:
        words = [0] * 11
        words[0] = 0xE3500000  # cmp r0, #0
        words[1] = branch(0x1004, 0x1010, condition=0x0)  # beq
        words[2] = branch(0x1008, 0x1020, link=True)
        words[3] = 0xE12FFF1E  # bx lr
        words[4] = 0xE3A00001  # mov r0, #1
        words[5] = 0xE12FFF1E
        words[8] = 0xE59F1000  # ldr r1, [pc]
        words[9] = 0xE12FFF1E
        words[10] = 0x12345678  # literal
        code = b"".join(word.to_bytes(4, "little") for word in words)

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inventory, audit = self._inputs(
                root,
                [(0x1000, 0x18, "caller"), (0x1020, 0x0C, "callee")],
            )
            program = build_program(
                code,
                inventory,
                audit,
                base=BASE,
                executable_size=len(code),
            )

        functions = {function.entry: function for function in program.functions}
        self.assertEqual(functions[0x1000].direct_calls, ((0x1008, 0x1020),))
        self.assertTrue(functions[0x1000].closed_static_cfg)
        self.assertTrue(functions[0x1020].closed_static_cfg)
        self.assertIn(0x1028, program.literal_data)
        self.assertEqual(program.unclaimed_blocks, ())
        manifest = program.to_manifest(include_instructions=True)
        serialized = json.dumps(manifest)
        self.assertNotIn("PackedOp", serialized)
        self.assertIn('"semantic": "control"', serialized)

    def test_marks_loop_and_cross_function_branch_as_tail_call(self) -> None:
        words = [0] * 10
        words[0] = 0xE2500001  # subs r0, r0, #1
        words[1] = branch(0x1004, 0x1000, condition=0x1)  # bne loop
        words[2] = branch(0x1008, 0x1020)
        words[8] = 0xE12FFF1E
        code = b"".join(word.to_bytes(4, "little") for word in words)

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inventory, audit = self._inputs(
                root,
                [(0x1000, 0x0C, "loop"), (0x1020, 0x04, "tail")],
            )
            program = build_program(
                code,
                inventory,
                audit,
                base=BASE,
                executable_size=len(code),
            )

        functions = {function.entry: function for function in program.functions}
        self.assertEqual(functions[0x1000].tail_calls, ((0x1008, 0x1020),))
        self.assertTrue(functions[0x1000].closed_static_cfg)

    def test_conditional_return_preserves_fallthrough_in_function(self) -> None:
        words = [0] * 4
        words[0] = 0xE3500001  # cmp r0, #1
        words[1] = 0xB12FFF1E  # bxlt lr
        words[2] = 0xE3A00002  # mov r0, #2
        words[3] = 0xE12FFF1E  # bx lr
        code = b"".join(word.to_bytes(4, "little") for word in words)

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inventory, audit = self._inputs(
                root, [(0x1000, 0x10, "conditional_return")]
            )
            program = build_program(
                code,
                inventory,
                audit,
                base=BASE,
                executable_size=len(code),
            )

        functions = {function.entry: function for function in program.functions}
        function = functions[0x1000]
        self.assertEqual(len(function.block_ids), 2)
        self.assertTrue(function.closed_static_cfg)
        self.assertNotIn(0x1008, functions)
        self.assertEqual(program.unclaimed_blocks, ())

    def test_complementary_conditional_tail_calls_close_cfg(self) -> None:
        words = [0xFFFFFFFF] * 10
        words[0] = 0xE3500000  # cmp r0,#0
        words[1] = branch(0x1004, 0x1020, condition=0x1)  # bne first
        words[2] = 0x03A01001  # moveq r1,#1; does not change NZCV
        words[3] = branch(0x100C, 0x1024, condition=0x0)  # beq second
        words[8] = 0xE12FFF1E
        words[9] = 0xE12FFF1E
        code = b"".join(word.to_bytes(4, "little") for word in words)

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inventory, audit = self._inputs(
                root,
                [
                    (0x1000, 0x10, "complementary_dispatch"),
                    (0x1020, 0x04, "first_tail"),
                    (0x1024, 0x04, "second_tail"),
                ],
            )
            program = build_program(
                code,
                inventory,
                audit,
                base=BASE,
                executable_size=len(code),
            )

        functions = {function.entry: function for function in program.functions}
        dispatch = functions[0x1000]
        self.assertTrue(dispatch.closed_static_cfg)
        self.assertEqual(
            dispatch.tail_calls,
            ((0x1004, 0x1020), (0x100C, 0x1024)),
        )
        second = next(block for block in program.blocks if block.pc == 0x1008)
        self.assertEqual(len(second.successors), 1)
        self.assertEqual(second.successors[0].kind, "branch")
        self.assertIsNone(second.successors[0].condition)

    def test_promotes_address_taken_code_to_a_function(self) -> None:
        words = [0] * 18
        words[0] = 0xE12FFF1E
        words[8] = 0x1040  # exact code pointer in an unreachable table
        words[16] = 0xE3A00002
        words[17] = 0xE12FFF1E
        code = b"".join(word.to_bytes(4, "little") for word in words)

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inventory, audit = self._inputs(root, [(0x1000, 0x04, "root")])
            program = build_program(
                code,
                inventory,
                audit,
                base=BASE,
                executable_size=len(code),
            )

        functions = {function.entry: function for function in program.functions}
        self.assertIn(0x1040, program.pointer_roots)
        self.assertEqual(functions[0x1040].origin, "address_taken")
        self.assertTrue(functions[0x1040].closed_static_cfg)
        self.assertEqual(program.unclaimed_blocks, ())


if __name__ == "__main__":
    unittest.main()
