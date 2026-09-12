from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from whole_aot_cpp import (
    HEADER_NAME,
    PROGRAM_FORMAT,
    SELECTION_FORMAT,
    SOURCE_NAME,
    Instruction,
    LoweringError,
    _emit_vfp,
    generate,
)


def words(*values: int) -> bytes:
    return b"".join(value.to_bytes(4, "little") for value in values)


class WholeAotCppTests(unittest.TestCase):
    def write_inputs(
        self, root: Path, code: bytes, *, direct_calls: list[dict] | None = None
    ) -> tuple[Path, Path, Path]:
        code_path = root / "code.bin"
        program_path = root / "program.json"
        selection_path = root / "selection.json"
        code_path.write_bytes(code)
        program_path.write_text(
            json.dumps(
                {
                    "format": PROGRAM_FORMAT,
                    "base": 0x1000,
                    "code_sha256": hashlib.sha256(code).hexdigest(),
                    "blocks": [
                        {
                            "id": 0,
                            "pc": 0x1000,
                            "end_pc": 0x1000 + len(code),
                            "successors": [
                                {"site": 0x1000 + len(code) - 4, "kind": "return"}
                            ],
                        }
                    ],
                    "functions": [
                        {
                            "entry": 0x1000,
                            "name": "SyntheticFunction",
                            "blocks": [0],
                            "closed_static_cfg": True,
                            "direct_calls": direct_calls or [],
                            "tail_calls": [],
                            "indirect_sites": [],
                        }
                    ],
                }
            ),
            encoding="utf-8",
        )
        selection_path.write_text(
            json.dumps(
                {
                    "format": SELECTION_FORMAT,
                    "functions": [{"entry": 0x1000}],
                }
            ),
            encoding="utf-8",
        )
        return program_path, selection_path, code_path

    def test_emits_direct_cfg_and_named_registers(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = self.write_inputs(
                root,
                words(0xE3A00001, 0xE2800002, 0xE12FFF1E),
            )
            output = root / "output"
            manifest = generate(*inputs, output)
            source = (output / SOURCE_NAME).read_text(encoding="utf-8")
            self.assertEqual(manifest["functions"][0]["instructions"], 3)
            self.assertIn("r0 = 0x00000001U", source)
            self.assertIn(
                "r0 = r0 + operand", source
            )
            self.assertIn(
                "return Oot3dAotReturned(r14)", source
            )
            self.assertNotIn("PackedOp", source)
            self.assertNotIn("ExecuteBlock", source)
            self.assertNotIn("ExecuteVfpScalar", source)
            self.assertIn("case Oot3dWholeAotFlowKind::Unsupported:", source)
            self.assertIn("a32::ExitKind::Unsupported, flow.Pc", source)
            self.assertIn("a32::FallbackReason::Unsupported", source)
            self.assertTrue((output / HEADER_NAME).is_file())

    def test_consumes_per_flag_liveness_in_promoted_state(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = self.write_inputs(
                root,
                words(0xE3500000, 0xE3510000, 0xE12FFF1E),
            )
            output = root / "output"
            generate(*inputs, output)
            source = (output / SOURCE_NAME).read_text(encoding="utf-8")
            self.assertIn(
                "Oot3dAotSetSubtractFlags(flags, r0, 0x00000000U, "
                "static_cast<Oot3dAotFlagMask>(0x0U));",
                source,
            )
            self.assertIn(
                "Oot3dAotSetSubtractFlags(flags, r1, 0x00000000U, "
                "static_cast<Oot3dAotFlagMask>(0xFU));",
                source,
            )
            self.assertNotIn("frame.Guest.cpsr", source)

    def test_emits_nonvirtual_typed_memory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = self.write_inputs(
                root,
                words(0xE5901000, 0xE5801004, 0xE12FFF1E),
            )
            output = root / "output"
            generate(*inputs, output)
            source = (output / SOURCE_NAME).read_text(encoding="utf-8")
            self.assertIn("ReadFast<uint32_t>", source)
            self.assertIn("WriteFast<uint32_t>", source)
            self.assertNotIn("memory.Read32", source)

    def test_emits_logical_shifter_carry_and_nop(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = self.write_inputs(
                root,
                words(
                    0xE320F000,  # NOP
                    0xE1310082,  # TEQ r1, r2, LSL #1
                    0xE1810002,  # ORR r0, r1, r2
                    0xE12FFF1E,  # BX LR
                ),
            )
            output = root / "output"
            generate(*inputs, output)
            source = (output / SOURCE_NAME).read_text(encoding="utf-8")
            self.assertIn("Oot3dAotShiftImmediate", source)
            self.assertIn("Oot3dAotSetLogicalFlags", source)
            self.assertIn(
                "r0 = r1 | r2",
                source,
            )
            self.assertNotIn("0xE320F000", source)

    def test_non_flag_register_shift_preserves_zero_amount_semantics(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = self.write_inputs(
                root,
                words(
                    0xE1A0E03B,  # MOV lr, r11, LSR r0
                    0xE12FFF1E,  # BX lr
                ),
            )
            output = root / "output"
            generate(*inputs, output)
            source = (output / SOURCE_NAME).read_text(encoding="utf-8")
            self.assertIn(
                "Oot3dAotShiftRegister(r11, 1U, r0, false).Value", source
            )
            self.assertNotIn("Oot3dAotLsr(r11, r0 & 0xFFU)", source)

    def test_emits_count_leading_zeros(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = self.write_inputs(
                root,
                words(0xE16FCF11, 0xE12FFF1E),  # CLZ r12, r1; BX LR
            )
            output = root / "output"
            generate(*inputs, output)
            source = (output / SOURCE_NAME).read_text(encoding="utf-8")
            self.assertIn("uint32_t value = r1", source)
            self.assertIn("while (count < 32U", source)
            self.assertIn("r12 = count", source)
            self.assertNotIn("Oot3dAotSetAddFlags", source)

    def test_emits_wide_immediates_and_saturating_arithmetic(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = self.write_inputs(
                root,
                words(
                    0xE3010234,  # MOVW r0, #0x1234
                    0xE3450678,  # MOVT r0, #0x5678
                    0xE1020051,  # QADD r0, r1, r2
                    0xE12FFF1E,
                ),
            )
            output = root / "output"
            generate(*inputs, output)
            source = (output / SOURCE_NAME).read_text(encoding="utf-8")
            self.assertIn("r0 = 0x00001234U", source)
            self.assertIn("0x56780000U", source)
            self.assertIn("Oot3dAotSaturateSigned32", source)
            self.assertIn("Oot3dAotSetSaturationFlag", source)

    def test_emits_special_signed_multiply_forms(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = self.write_inputs(
                root,
                words(
                    0xE1003281,  # SMLABB r0, r1, r2, r3
                    0xE1443281,  # SMLALBB r3, r4, r1, r2
                    0xE0456392,  # UMAAL r6, r5, r2, r3
                    0xE12FFF1E,
                ),
            )
            output = root / "output"
            generate(*inputs, output)
            source = (output / SOURCE_NAME).read_text(encoding="utf-8")
            self.assertIn("const int64_t value", source)
            self.assertIn("const int64_t product", source)
            self.assertIn("uint64_t value", source)

    def test_emits_media_reversal_packing_and_saturation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = self.write_inputs(
                root,
                words(
                    0xE6BF2F32,  # REV r2, r2
                    0xE6800012,  # PKHBT r0, r0, r2
                    0xE6E80010,  # USAT r0, #8, r0
                    0xE6BF0070,  # SXTH r0, r0
                    0xE12FFF1E,
                ),
            )
            output = root / "output"
            generate(*inputs, output)
            source = (output / SOURCE_NAME).read_text(encoding="utf-8")
            self.assertIn("source & 0x000000FFU", source)
            self.assertIn("shifted.Value", source)
            self.assertIn("const int64_t clamped", source)
            self.assertIn("static_cast<int16_t>(source)", source)

    def test_emits_exact_vfp_float_to_signed_conversion(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = self.write_inputs(
                root,
                words(0xEEBD0AC0, 0xE12FFF1E),
            )
            output = root / "output"
            generate(*inputs, output)
            source = (output / SOURCE_NAME).read_text(encoding="utf-8")
            self.assertIn("VfpBinary32ToSigned", source)

    def test_emits_exact_vfp_square_root(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = self.write_inputs(
                root,
                words(0xEEB10AC0, 0xE12FFF1E),
            )
            output = root / "output"
            generate(*inputs, output)
            source = (output / SOURCE_NAME).read_text(encoding="utf-8")
            self.assertIn("VfpBinary32SquareRoot", source)

    def test_emits_vfp_multiple_load_and_store(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = self.write_inputs(
                root,
                words(
                    0xEC920A0C,  # VLDMIA r2, {s0-s11}
                    0xECA00A0C,  # VSTMIA r0!, {s0-s11}
                    0xE12FFF1E,
                ),
            )
            output = root / "output"
            generate(*inputs, output)
            source = (output / SOURCE_NAME).read_text(encoding="utf-8")
            self.assertIn(
                "const uint32_t firstAddress = r2", source
            )
            self.assertIn("frame.Guest.vfp[0] = value0", source)
            self.assertIn("frame.Guest.vfp[11] = value11", source)
            self.assertIn("firstAddress + 44U, frame.Guest.vfp[11]", source)
            self.assertIn(
                "r0 = r0 + 48U", source
            )

    def test_vfp_double_memory_transfers_preserve_both_words(self) -> None:
        for register in range(16):
            for add in (False, True):
                with self.subTest(register=register, add=add):
                    raw = 0xED120B03 | (register << 12) | (int(add) << 23)
                    load = "\n".join(_emit_vfp(Instruction(0x1000, raw)))
                    store = "\n".join(_emit_vfp(Instruction(0x1004, raw & ~(1 << 20))))
                    self.assertIn("ReadFast<uint64_t>", load)
                    self.assertIn(f"frame.Guest.vfp[{register * 2}] = static_cast<uint32_t>(value)", load)
                    self.assertIn(f"frame.Guest.vfp[{register * 2 + 1}] = static_cast<uint32_t>(value >> 32U)", load)
                    self.assertIn("WriteFast<uint64_t>", store)
                    self.assertIn(f"static_cast<uint64_t>(frame.Guest.vfp[{register * 2 + 1}]) << 32U", store)
                    self.assertIn(f"{'+' if add else '-'} 12U", load)

    def test_vfp_double_literal_and_conditional_memory(self) -> None:
        source = "\n".join(_emit_vfp(Instruction(0x1000, 0x1D9F0B03)))
        self.assertIn("0x00001008U + 12U", source)
        self.assertIn("ReadFast<uint64_t>", source)
        self.assertIn("if (!(state.Flags.Z()))", source)

    def test_vfp_single_memory_retains_odd_lanes(self) -> None:
        source = "\n".join(_emit_vfp(Instruction(0x1000, 0xEDD2FA03)))
        self.assertIn("ReadFast<uint32_t>", source)
        self.assertIn("frame.Guest.vfp[31] = value", source)
        self.assertNotIn("uint64_t", source)

    def test_vfp_double_memory_rejects_unmodelled_registers_and_pc_store(self) -> None:
        for raw in (0xEDD20B03, 0xEDC20B03, 0xED8F0B03):
            with self.subTest(raw=hex(raw)), self.assertRaises(LoweringError):
                _emit_vfp(Instruction(0x1000, raw))

    def test_emits_vfp_decrement_before_double_store(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = self.write_inputs(
                root,
                words(
                    0xED2D8B06,  # VPUSH {d8-d10}
                    0xECBD8B06,  # VPOP {d8-d10}
                    0xE12FFF1E,
                ),
            )
            output = root / "output"
            generate(*inputs, output)
            source = (output / SOURCE_NAME).read_text(encoding="utf-8")
            self.assertIn("r13 -= 24U", source)
            self.assertIn("frame.Guest.vfp[16]", source)
            self.assertIn("frame.Guest.vfp[21]", source)
            self.assertIn("r13 += 24U", source)

    def test_emits_doubleword_load_and_store(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = self.write_inputs(
                root,
                words(
                    0xE1CD84D0,  # LDRD r8, r9, [sp, #0x40]
                    0xE1CD20F0,  # STRD r2, r3, [sp]
                    0xE12FFF1E,
                ),
            )
            output = root / "output"
            generate(*inputs, output)
            source = (output / SOURCE_NAME).read_text(encoding="utf-8")
            self.assertIn("ReadFast<uint64_t>", source)
            self.assertIn(
                "r8 = static_cast<uint32_t>(value)", source
            )
            self.assertIn(
                "r9 = static_cast<uint32_t>(value >> 32U)",
                source,
            )
            self.assertIn("WriteFast<uint64_t>", source)

    def test_emits_signed_long_multiply_accumulate(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = self.write_inputs(
                root,
                words(0xE0E1C193, 0xE12FFF1E),  # SMLAL r12, r1, r3, r1
            )
            output = root / "output"
            generate(*inputs, output)
            source = (output / SOURCE_NAME).read_text(encoding="utf-8")
            self.assertIn("const int64_t product", source)
            self.assertIn(
                "r12 = static_cast<uint32_t>(value)", source
            )
            self.assertIn(
                "r1 = static_cast<uint32_t>(value >> 32U)",
                source,
            )

    def test_emits_thread_pointer_barrier_and_clear_exclusive(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = self.write_inputs(
                root,
                words(
                    0xEE1D0F70,  # MRC TPIDRURW, r0
                    0xEE075F9A,  # legacy DSB
                    0xF57FF01F,  # CLREX
                    0xE12FFF1E,
                ),
            )
            output = root / "output"
            generate(*inputs, output)
            source = (output / SOURCE_NAME).read_text(encoding="utf-8")
            self.assertIn(
                "r0 = frame.Guest.thread_pointer", source
            )
            self.assertIn("std::atomic_thread_fence", source)
            self.assertIn("frame.Guest.exclusive_valid = false", source)

    def test_emits_word_exclusive_load_and_store(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = self.write_inputs(
                root,
                words(
                    0xE1940F9F,  # LDREX r0, [r4]
                    0xE1842F90,  # STREX r2, r0, [r4]
                    0xE12FFF1E,
                ),
            )
            output = root / "output"
            generate(*inputs, output)
            source = (output / SOURCE_NAME).read_text(encoding="utf-8")
            self.assertIn("context.Memory.LoadExclusive", source)
            self.assertIn("context.Memory.StoreExclusive", source)
            self.assertIn("r2 = success ? 0U : 1U", source)

    def test_emits_parallel_unsigned_saturating_subtract(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = self.write_inputs(
                root,
                words(0xE66ECFF2, 0xE12FFF1E),  # UQSUB8 r12, lr, r2
            )
            output = root / "output"
            generate(*inputs, output)
            source = (output / SOURCE_NAME).read_text(encoding="utf-8")
            self.assertIn("const unsigned a = (left >> shift) & 0xFFU", source)
            self.assertIn("value |= (a > b ? a - b : 0U) << shift", source)
            self.assertIn("r12 = value", source)

    def test_emits_direct_host_call_between_selected_functions(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            code = words(
                0xEB000000,  # BL 0x1008
                0xE12FFF1E,  # BX LR
                0xE3A00007,  # MOV R0, #7
                0xE12FFF1E,  # BX LR
            )
            code_path = root / "code.bin"
            program_path = root / "program.json"
            selection_path = root / "selection.json"
            code_path.write_bytes(code)
            program_path.write_text(
                json.dumps(
                    {
                        "format": PROGRAM_FORMAT,
                        "base": 0x1000,
                        "code_sha256": hashlib.sha256(code).hexdigest(),
                        "blocks": [
                            {
                                "id": 0,
                                "pc": 0x1000,
                                "end_pc": 0x1004,
                                "successors": [
                                    {"site": 0x1000, "kind": "call", "target": 0x1008},
                                    {"site": 0x1000, "kind": "fallthrough", "target": 0x1004},
                                ],
                            },
                            {
                                "id": 1,
                                "pc": 0x1004,
                                "end_pc": 0x1008,
                                "successors": [{"site": 0x1004, "kind": "return"}],
                            },
                            {
                                "id": 2,
                                "pc": 0x1008,
                                "end_pc": 0x1010,
                                "successors": [{"site": 0x100C, "kind": "return"}],
                            },
                        ],
                        "functions": [
                            {
                                "entry": 0x1000,
                                "name": "Caller",
                                "blocks": [0, 1],
                                "closed_static_cfg": True,
                                "direct_calls": [{"site": 0x1000, "target": 0x1008}],
                                "tail_calls": [],
                                "indirect_sites": [],
                            },
                            {
                                "entry": 0x1008,
                                "name": "Callee",
                                "blocks": [2],
                                "closed_static_cfg": True,
                                "direct_calls": [],
                                "tail_calls": [],
                                "indirect_sites": [],
                            },
                        ],
                    }
                ),
                encoding="utf-8",
            )
            selection_path.write_text(
                json.dumps(
                    {
                        "format": SELECTION_FORMAT,
                        "functions": [{"entry": 0x1000}, {"entry": 0x1008}],
                    }
                ),
                encoding="utf-8",
            )
            output = root / "output"
            generate(program_path, selection_path, code_path, output)
            source = (output / SOURCE_NAME).read_text(encoding="utf-8")
            self.assertIn(
                "Execute_Callee_00001008(frame, context, state, 0x00001008U)", source
            )
            self.assertIn("r14 = 0x00001004U", source)
            self.assertIn("uint32_t& r14 = state.R[14];", source)
            self.assertIn("const auto commitState = []() noexcept", source)
            self.assertNotIn("state.R[14] = r14;", source)
            self.assertIn("commitState();", source)
            self.assertIn("reloadState();", source)
            self.assertIn("stateCommit.Dismiss();", source)
            self.assertNotIn("ExecuteOot3dWholeAotFunction(0x", source)

            sharded_output = root / "sharded_output"
            sharded_manifest = generate(
                program_path,
                selection_path,
                code_path,
                sharded_output,
                shard_count=2,
            )
            registry = (sharded_output / SOURCE_NAME).read_text(encoding="utf-8")
            shards = sorted(sharded_output.glob("oot3d_whole_aot_shard_*.cpp"))
            shard_source = "\n".join(
                path.read_text(encoding="utf-8") for path in shards
            )
            self.assertEqual(sharded_manifest["shard_count"], 2)
            self.assertEqual(len(shards), 2)
            self.assertNotIn("block_00001000:", registry)
            self.assertIn("block_00001000:", shard_source)
            self.assertIn(
                "Execute_Callee_00001008(frame, context, state, 0x00001008U)",
                shard_source,
            )
            self.assertIn("Execute_Caller_00001000", shard_source)
            self.assertIn("Execute_Callee_00001008", shard_source)
            self.assertIn("Oot3dAotEnterBlock(context, frame", shard_source)
            self.assertIn("Oot3dAotBlockLimit(context", shard_source)
            self.assertIn("case Oot3dWholeAotFlowKind::BlockLimit:", registry)
            self.assertIn("Oot3dWholeAotExecutionScope executionScope", registry)
            self.assertIn("flow.Pc != stopPc", registry)
            self.assertIn("FindDispatchEntry(flow.Pc)", registry)
            self.assertIn("kDispatchPageOffsets", registry)
            self.assertIn("pc >> kDispatchPageShift", registry)
            self.assertIn(
                "catch (const Oot3dWholeAotObservableExit& exit)", registry
            )
            self.assertIn(
                "Oot3dWholeAotTakeObservableExitSnapshot(", registry
            )
            self.assertIn(
                "if (restoreObservableExitState) state = observableExitState;",
                registry,
            )
            self.assertIn("flow = Oot3dAotBranch(exit.Pc);", registry)

            balanced_output = root / "balanced_output"
            balanced_manifest = generate(
                program_path,
                selection_path,
                code_path,
                balanced_output,
                shard_count=2,
                shard_strategy="balanced",
            )
            self.assertEqual(balanced_manifest["shard_strategy"], "balanced")
            affinity_output = root / "affinity_output"
            affinity_manifest = generate(
                program_path,
                selection_path,
                code_path,
                affinity_output,
                shard_count=2,
                shard_strategy="affinity",
            )
            self.assertEqual(affinity_manifest["shard_strategy"], "affinity")
            self.assertEqual(
                sum(shard["functions"] for shard in affinity_manifest["shards"]),
                2,
            )
            balanced_shards = {
                path.name: path.read_bytes()
                for path in balanced_output.glob("oot3d_whole_aot_shard_*.cpp")
            }
            incremental_manifest = generate(
                program_path,
                selection_path,
                code_path,
                balanced_output,
                shard_count=2,
                shard_strategy="incremental",
            )
            self.assertEqual(
                incremental_manifest["shard_strategy"], "incremental"
            )
            self.assertEqual(
                balanced_shards,
                {
                    path.name: path.read_bytes()
                    for path in balanced_output.glob(
                        "oot3d_whole_aot_shard_*.cpp"
                    )
                },
            )
            self.assertTrue(
                all(
                    "entries" in shard
                    for shard in incremental_manifest["shards"]
                )
            )

            selection = json.loads(selection_path.read_text(encoding="utf-8"))
            selection["functions"] = selection["functions"][:-1]
            selection["external_functions"] = [
                {
                    "entry": 0x1008,
                    "name": "Callee",
                    "reason": "typed test boundary",
                }
            ]
            selection_path.write_text(
                json.dumps(selection), encoding="utf-8"
            )
            caller_shard_before = next(
                index
                for index, shard in enumerate(incremental_manifest["shards"])
                if 0x1000 in shard["entries"]
            )
            removal_manifest = generate(
                program_path,
                selection_path,
                code_path,
                balanced_output,
                shard_count=2,
                shard_strategy="incremental",
            )
            caller_shard_after = next(
                index
                for index, shard in enumerate(removal_manifest["shards"])
                if 0x1000 in shard["entries"]
            )
            self.assertEqual(caller_shard_before, caller_shard_after)

    def test_external_call_continuation_is_a_dispatch_entry(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            code = words(0xEB000000, 0xE12FFF1E, 0xE12FFF1E)
            code_path = root / "code.bin"
            program_path = root / "program.json"
            selection_path = root / "selection.json"
            code_path.write_bytes(code)
            program_path.write_text(
                json.dumps(
                    {
                        "format": PROGRAM_FORMAT,
                        "base": 0x1000,
                        "code_sha256": hashlib.sha256(code).hexdigest(),
                        "blocks": [
                            {
                                "id": 0,
                                "pc": 0x1000,
                                "end_pc": 0x1004,
                                "successors": [
                                    {"site": 0x1000, "kind": "call", "target": 0x1008},
                                    {"site": 0x1000, "kind": "fallthrough", "target": 0x1004},
                                ],
                            },
                            {
                                "id": 1,
                                "pc": 0x1004,
                                "end_pc": 0x1008,
                                "successors": [{"site": 0x1004, "kind": "return"}],
                            },
                        ],
                        "functions": [
                            {
                                "entry": 0x1000,
                                "name": "Caller",
                                "blocks": [0, 1],
                                "closed_static_cfg": True,
                                "direct_calls": [{"site": 0x1000, "target": 0x1008}],
                                "tail_calls": [],
                                "indirect_sites": [],
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )
            selection_path.write_text(
                json.dumps(
                    {
                        "format": SELECTION_FORMAT,
                        "functions": [{"entry": 0x1000}],
                        "external_functions": [{"entry": 0x1008}],
                    }
                ),
                encoding="utf-8",
            )
            output = root / "output"
            manifest = generate(program_path, selection_path, code_path, output)
            source = (output / SOURCE_NAME).read_text(encoding="utf-8")
            self.assertEqual(manifest["functions"][0]["resume_entries"], [0x1004])
            self.assertIn("case 0x00001004U:", source)
            self.assertIn("goto block_00001004;", source)
            self.assertIn("Oot3dAotCallExternal(context, 0x00001008U", source)
            self.assertIn("std::array<uint32_t, 2>", source)

    def test_indirect_call_exits_and_reenters_aot(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            code = words(0xE12FFF33, 0xE12FFF1E)
            code_path = root / "code.bin"
            program_path = root / "program.json"
            selection_path = root / "selection.json"
            code_path.write_bytes(code)
            program_path.write_text(
                json.dumps(
                    {
                        "format": PROGRAM_FORMAT,
                        "base": 0x1000,
                        "code_sha256": hashlib.sha256(code).hexdigest(),
                        "blocks": [
                            {
                                "id": 0,
                                "pc": 0x1000,
                                "end_pc": 0x1004,
                                "successors": [
                                    {"site": 0x1000, "kind": "indirect_call"},
                                    {"site": 0x1000, "kind": "resume", "target": 0x1004},
                                ],
                            },
                            {
                                "id": 1,
                                "pc": 0x1004,
                                "end_pc": 0x1008,
                                "successors": [{"site": 0x1004, "kind": "return"}],
                            },
                        ],
                        "functions": [
                            {
                                "entry": 0x1000,
                                "name": "IndirectCaller",
                                "blocks": [0, 1],
                                "closed_static_cfg": True,
                                "direct_calls": [],
                                "tail_calls": [],
                                "indirect_sites": [
                                    {"site": 0x1000, "kind": "indirect_call"}
                                ],
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )
            selection_path.write_text(
                json.dumps(
                    {"format": SELECTION_FORMAT, "functions": [{"entry": 0x1000}]}
                ),
                encoding="utf-8",
            )
            output = root / "output"
            manifest = generate(program_path, selection_path, code_path, output)
            source = (output / SOURCE_NAME).read_text(encoding="utf-8")
            self.assertEqual(manifest["functions"][0]["resume_entries"], [0x1004])
            self.assertIn("r14 = 0x00001004U", source)
            self.assertIn("ExecuteOot3dWholeAotIndirect", source)
            self.assertIn("r3, frame, context, state", source)
            self.assertIn("case 0x00001004U:", source)

    def test_computed_add_pc_call_uses_existing_link_register(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            code = words(0xE088F003, 0xE12FFF1E)  # ADD pc,r8,r3; BX lr
            code_path = root / "code.bin"
            program_path = root / "program.json"
            selection_path = root / "selection.json"
            code_path.write_bytes(code)
            program_path.write_text(
                json.dumps(
                    {
                        "format": PROGRAM_FORMAT,
                        "base": 0x1000,
                        "code_sha256": hashlib.sha256(code).hexdigest(),
                        "blocks": [
                            {
                                "id": 0,
                                "pc": 0x1000,
                                "end_pc": 0x1004,
                                "successors": [
                                    {"site": 0x1000, "kind": "indirect_call"},
                                    {"site": 0x1000, "kind": "resume", "target": 0x1004},
                                ],
                            },
                            {
                                "id": 1,
                                "pc": 0x1004,
                                "end_pc": 0x1008,
                                "successors": [{"site": 0x1004, "kind": "return"}],
                            },
                        ],
                        "functions": [
                            {
                                "entry": 0x1000,
                                "name": "ComputedCaller",
                                "blocks": [0, 1],
                                "closed_static_cfg": True,
                                "direct_calls": [],
                                "tail_calls": [],
                                "indirect_sites": [
                                    {"site": 0x1000, "kind": "indirect_call"}
                                ],
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )
            selection_path.write_text(
                json.dumps(
                    {"format": SELECTION_FORMAT, "functions": [{"entry": 0x1000}]}
                ),
                encoding="utf-8",
            )
            output = root / "output"
            generate(program_path, selection_path, code_path, output)
            source = (output / SOURCE_NAME).read_text(encoding="utf-8")
            self.assertIn("(r8 + r3), frame, context, state", source)
            self.assertNotIn("r14 = 0x00001004U", source)

    def test_emits_program_status_vmsr_and_binary64_semantics(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = self.write_inputs(
                root,
                words(
                    0xE10F2000,  # MRS r2,CPSR
                    0xE128F002,  # MSR CPSR_f,r2
                    0xEEE10A10,  # VMSR FPSCR,r0
                    0xEC410B10,  # VMOV d0,r0,r1
                    0xEEB03B40,  # VMOV.F64 d3,d0
                    0xEEB72AC2,  # VCVT.F64.F32 d2,s4
                    0xE12FFF1E,
                ),
            )
            output = root / "output"
            generate(*inputs, output)
            source = (output / SOURCE_NAME).read_text(encoding="utf-8")
            self.assertIn("Materialize(state.PreservedCpsr())", source)
            self.assertIn("SetPreservedCpsrBits(0xFF000000U, value)", source)
            self.assertIn("fpscr = r0", source)
            self.assertIn("frame.Guest.vfp[1] = r1", source)
            self.assertIn("frame.Guest.vfp[6] = low", source)
            self.assertIn("a32::ExecuteVfpBinary64", source)

    def test_terminal_prefetch_is_not_a_pc_load(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            code = words(0xF5D1F040, 0xE12FFF1E)  # PLD [r1,#64]; BX lr
            code_path = root / "code.bin"
            program_path = root / "program.json"
            selection_path = root / "selection.json"
            code_path.write_bytes(code)
            program_path.write_text(
                json.dumps(
                    {
                        "format": PROGRAM_FORMAT,
                        "base": 0x1000,
                        "code_sha256": hashlib.sha256(code).hexdigest(),
                        "blocks": [
                            {
                                "id": 0,
                                "pc": 0x1000,
                                "end_pc": 0x1004,
                                "successors": [
                                    {"site": 0x1000, "kind": "fallthrough", "target": 0x1004}
                                ],
                            },
                            {
                                "id": 1,
                                "pc": 0x1004,
                                "end_pc": 0x1008,
                                "successors": [{"site": 0x1004, "kind": "return"}],
                            },
                        ],
                        "functions": [
                            {
                                "entry": 0x1000,
                                "name": "PrefetchThenReturn",
                                "blocks": [0, 1],
                                "closed_static_cfg": True,
                                "direct_calls": [],
                                "tail_calls": [],
                                "indirect_sites": [],
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )
            selection_path.write_text(
                json.dumps(
                    {"format": SELECTION_FORMAT, "functions": [{"entry": 0x1000}]}
                ),
                encoding="utf-8",
            )
            output = root / "output"
            manifest = generate(program_path, selection_path, code_path, output)
            source = (output / SOURCE_NAME).read_text(encoding="utf-8")
            self.assertNotIn("ReadFast<uint32_t>", source)
            self.assertIn("goto block_00001004", source)
            self.assertEqual(manifest["functions"][0]["dispatch_entries"], [0x1004])
            self.assertIn("std::array<uint32_t, 2>", source)

    def test_shared_resume_alias_keeps_both_callable_bodies(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            code = words(0xEF000022, 0xE12FFF1E, 0xEF000022)
            code_path = root / "code.bin"
            program_path = root / "program.json"
            selection_path = root / "selection.json"
            code_path.write_bytes(code)
            shared_resume = {
                "id": 1,
                "pc": 0x1004,
                "end_pc": 0x1008,
                "successors": [{"site": 0x1004, "kind": "return"}],
            }
            program_path.write_text(
                json.dumps(
                    {
                        "format": PROGRAM_FORMAT,
                        "base": 0x1000,
                        "code_sha256": hashlib.sha256(code).hexdigest(),
                        "blocks": [
                            {
                                "id": 0,
                                "pc": 0x1000,
                                "end_pc": 0x1004,
                                "successors": [
                                    {"site": 0x1000, "kind": "svc", "value": 0x22},
                                    {"site": 0x1000, "kind": "resume", "target": 0x1004},
                                ],
                            },
                            shared_resume,
                            {
                                "id": 2,
                                "pc": 0x1008,
                                "end_pc": 0x100C,
                                "successors": [
                                    {"site": 0x1008, "kind": "svc", "value": 0x22},
                                    {"site": 0x1008, "kind": "resume", "target": 0x1004},
                                ],
                            },
                        ],
                        "functions": [
                            {
                                "entry": 0x1000,
                                "name": "FirstCaller",
                                "blocks": [0, 1],
                                "closed_static_cfg": True,
                                "direct_calls": [],
                                "tail_calls": [],
                                "indirect_sites": [],
                            },
                            {
                                "entry": 0x1008,
                                "name": "SecondCaller",
                                "blocks": [2, 1],
                                "closed_static_cfg": True,
                                "direct_calls": [],
                                "tail_calls": [],
                                "indirect_sites": [],
                            },
                        ],
                    }
                ),
                encoding="utf-8",
            )
            selection_path.write_text(
                json.dumps(
                    {
                        "format": SELECTION_FORMAT,
                        "functions": [{"entry": 0x1000}, {"entry": 0x1008}],
                    }
                ),
                encoding="utf-8",
            )

            output = root / "output"
            manifest = generate(program_path, selection_path, code_path, output)
            source = (output / SOURCE_NAME).read_text(encoding="utf-8")

            self.assertEqual(len(manifest["functions"]), 2)
            self.assertEqual(
                [item["resume_entries"] for item in manifest["functions"]],
                [[0x1004], [0x1004]],
            )
            self.assertIn("std::array<uint32_t, 3>", source)
            self.assertIn("case 0x00001000U:", source)
            self.assertIn("case 0x00001008U:", source)

    def test_pc_load_is_an_indirect_branch(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = self.write_inputs(root, words(0xE49DF004))
            output = root / "output"
            generate(*inputs, output)
            source = (output / SOURCE_NAME).read_text(encoding="utf-8")
            self.assertIn("ReadFast<uint32_t>", source)
            self.assertIn("r13 = updatedAddress", source)
            self.assertIn("switch (value)", source)
            self.assertIn("goto block_00001000", source)
            self.assertIn("return Oot3dAotBranch(value)", source)

    def test_svc_exits_to_runtime_and_reenters_at_resume(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            code = words(0xEF000022, 0xE12FFF1E)
            code_path = root / "code.bin"
            program_path = root / "program.json"
            selection_path = root / "selection.json"
            code_path.write_bytes(code)
            program_path.write_text(
                json.dumps(
                    {
                        "format": PROGRAM_FORMAT,
                        "base": 0x1000,
                        "code_sha256": hashlib.sha256(code).hexdigest(),
                        "blocks": [
                            {
                                "id": 0,
                                "pc": 0x1000,
                                "end_pc": 0x1004,
                                "successors": [
                                    {"site": 0x1000, "kind": "svc", "value": 0x22},
                                    {"site": 0x1000, "kind": "resume", "target": 0x1004},
                                ],
                            },
                            {
                                "id": 1,
                                "pc": 0x1004,
                                "end_pc": 0x1008,
                                "successors": [{"site": 0x1004, "kind": "return"}],
                            },
                        ],
                        "functions": [
                            {
                                "entry": 0x1000,
                                "name": "SvcCaller",
                                "blocks": [0, 1],
                                "closed_static_cfg": True,
                                "direct_calls": [],
                                "tail_calls": [],
                                "indirect_sites": [],
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )
            selection_path.write_text(
                json.dumps(
                    {"format": SELECTION_FORMAT, "functions": [{"entry": 0x1000}]}
                ),
                encoding="utf-8",
            )
            output = root / "output"
            manifest = generate(program_path, selection_path, code_path, output)
            source = (output / SOURCE_NAME).read_text(encoding="utf-8")
            self.assertEqual(manifest["functions"][0]["resume_entries"], [0x1004])
            self.assertIn(
                "Oot3dAotSvc(context, 0x00001000U, 0x000022U)", source
            )
            self.assertIn("case Oot3dWholeAotFlowKind::Svc:", source)
            self.assertIn("a32::ExitKind::Svc, flow.Pc", source)
            self.assertIn("case 0x00001004U:", source)

    def test_rejects_unselected_direct_call_target(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inputs = self.write_inputs(
                root,
                words(0xEB000000, 0xE12FFF1E),
                direct_calls=[{"site": 0x1000, "target": 0x1008}],
            )
            with self.assertRaisesRegex(ValueError, "unselected function"):
                generate(*inputs, root / "output")


if __name__ == "__main__":
    unittest.main()
