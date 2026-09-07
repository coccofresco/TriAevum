from __future__ import annotations

import unittest

from whole_aot_cpp import Block, Function, Instruction
from whole_aot_optimization_ir import (
    Flag,
    analyze_function,
    instruction_effects,
    register_names,
)


def block(
    pc: int, words: tuple[int, ...], successors: tuple[dict[str, object], ...]
) -> Block:
    return Block(
        0,
        pc,
        pc + 4 * len(words),
        tuple(Instruction(pc + 4 * index, word) for index, word in enumerate(words)),
        successors,
    )


class WholeAotOptimizationIrTests(unittest.TestCase):
    def test_condition_and_data_processing_effects_are_per_flag(self) -> None:
        move_flags = instruction_effects(0xE3B00001)  # MOVS r0, #1
        self.assertEqual(move_flags.gpr_writes, 1 << 0)
        self.assertEqual(move_flags.flag_reads, Flag.C)
        self.assertEqual(move_flags.flag_writes, Flag.NZC)

        add_carry = instruction_effects(0xE0B21003)  # ADCS r1, r2, r3
        self.assertEqual(register_names(add_carry.gpr_reads), ("r2", "r3"))
        self.assertEqual(add_carry.gpr_writes, 1 << 1)
        self.assertEqual(add_carry.flag_reads, Flag.C)
        self.assertEqual(add_carry.flag_writes, Flag.NZCV)

        branch_equal = instruction_effects(0x0A000000)
        self.assertEqual(branch_equal.flag_reads, Flag.Z)
        self.assertTrue(branch_equal.predicated)

        vfp_add = instruction_effects(0xEE300A01)
        self.assertEqual(vfp_add.flag_writes, Flag.NONE)
        self.assertFalse(vfp_add.barrier)

    def test_status_and_prefetch_effects_match_architecture(self) -> None:
        mrs = instruction_effects(0xE10F2000)
        self.assertEqual(register_names(mrs.gpr_writes), ("r2",))
        self.assertEqual(mrs.flag_writes, Flag.NONE)

        msr = instruction_effects(0xE128F002)
        self.assertEqual(register_names(msr.gpr_reads), ("r2",))
        self.assertEqual(msr.flag_writes, Flag.NZCV)

        vmsr = instruction_effects(0xEEE10A10)
        self.assertEqual(register_names(vmsr.gpr_reads), ("r0",))
        self.assertEqual(vmsr.gpr_writes, 0)

        core_pair_to_double = instruction_effects(0xEC410B10)
        self.assertEqual(
            register_names(core_pair_to_double.gpr_reads), ("r0", "r1")
        )

        prefetch = instruction_effects(0xF5D1F040)
        self.assertEqual(prefetch.gpr_reads, 0)
        self.assertEqual(prefetch.gpr_writes, 0)

    def test_dead_flag_analysis_removes_overwritten_producer(self) -> None:
        item = block(
            0x1000,
            (
                0xE3500000,  # CMP r0, #0
                0xE3510000,  # CMP r1, #0; overwrites all NZCV
                0xE12FFF1E,  # BX lr; architectural exit observes flags
            ),
            ({"kind": "return", "site": 0x1008},),
        )
        plan = analyze_function(Function(0x1000, "DeadFlags", (item,), (), ()))
        first, second, _ = plan.blocks[0].instructions
        self.assertEqual(first.dead_flag_writes, Flag.NZCV)
        self.assertEqual(first.required_flag_writes, Flag.NONE)
        self.assertEqual(second.required_flag_writes, Flag.NZCV)
        self.assertEqual(plan.dead_flag_write_count, 1)

    def test_predicated_flag_writer_does_not_kill_previous_value(self) -> None:
        item = block(
            0x2000,
            (
                0xE3500000,  # CMP r0, #0
                0x13510000,  # CMPNE r1, #0
                0xE12FFF1E,
            ),
            ({"kind": "return", "site": 0x2008},),
        )
        plan = analyze_function(Function(0x2000, "Predicated", (item,), (), ()))
        first, second, _ = plan.blocks[0].instructions
        self.assertEqual(first.required_flag_writes, Flag.NZCV)
        self.assertEqual(second.effects.flag_reads, Flag.Z)

    def test_scc_region_and_dirty_exit_are_general_cfg_properties(self) -> None:
        first = block(
            0x3000,
            (0xE2800001, 0xEA000000),  # ADD r0, r0, #1; branch
            ({"kind": "branch", "target": 0x3008},),
        )
        second = block(
            0x3008,
            (0xE3510000, 0x1AFFFFFC),  # CMP r1, #0; BNE 0x3000
            (
                {"kind": "branch", "target": 0x3000},
                {"kind": "return"},
            ),
        )
        plan = analyze_function(Function(0x3000, "Loop", (first, second), (), ()))
        loops = [region for region in plan.regions if region.is_loop]
        self.assertEqual(len(loops), 1)
        self.assertEqual(loops[0].blocks, (0x3000, 0x3008))
        self.assertTrue(plan.blocks[1].observable_exit)
        self.assertEqual(register_names(plan.blocks[1].dirty_gprs_out), ("r0",))
        self.assertEqual(plan.blocks[1].dirty_flags_out, Flag.NZCV)


if __name__ == "__main__":
    unittest.main()
