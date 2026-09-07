from __future__ import annotations

import csv
import json
import sys
import tempfile
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parent))
from whole_aot_program import build_program


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
