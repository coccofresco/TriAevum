from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_true_aot import (
    HEADER_NAME,
    MANIFEST_NAME,
    SOURCE_NAME,
    generate_true_aot,
)


class GenerateTrueAotTest(unittest.TestCase):
    def _manifest(
        self, path: Path, end_pc: int, entry_points: list[int] | None = None
    ) -> None:
        block = {
            "pc": 0x1000,
            "end_pc": end_pc,
            "symbol": "test.clear_loop",
        }
        if entry_points is not None:
            block["entry_points"] = entry_points
        path.write_text(
            json.dumps(
                {
                    "format": "oot3d_true_aot_blocks_v1",
                    "blocks": [block],
                }
            ),
            encoding="utf-8",
        )

    def test_emits_direct_cfg_region(self) -> None:
        words = (
            0xE4C05001,
            0xE598204C,
            0xE0822001,
            0xE1520000,
            0x8AFFFFFA,
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            code = root / "code.bin"
            manifest = root / "blocks.json"
            output = root / "generated"
            code.write_bytes(b"".join(word.to_bytes(4, "little") for word in words))
            self._manifest(manifest, 0x1014)

            generate_true_aot(code, manifest, output, base=0x1000)

            source = (output / SOURCE_NAME).read_text(encoding="utf-8")
            self.assertIn("goto instruction_00001000", source)
            self.assertIn("memory.Write8", source)
            self.assertIn("memory.Read32", source)
            self.assertNotIn("PackedOp", source)
            self.assertNotIn("Decode", source)
            self.assertTrue((output / HEADER_NAME).is_file())
            generated_manifest = json.loads(
                (output / MANIFEST_NAME).read_text(encoding="utf-8")
            )
            self.assertEqual(generated_manifest["regions"][0]["instruction_count"], 5)

    def test_emits_multi_block_region_with_multiple_entries(self) -> None:
        words = (
            0xE3500000,
            0x0A000001,
            0xE0811101,
            0xEA000000,
            0xE2611000,
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            code = root / "code.bin"
            manifest = root / "blocks.json"
            output = root / "generated"
            code.write_bytes(b"".join(word.to_bytes(4, "little") for word in words))
            self._manifest(manifest, 0x1014, [0x1000, 0x1010])

            generate_true_aot(code, manifest, output, base=0x1000)

            source = (output / SOURCE_NAME).read_text(encoding="utf-8")
            self.assertIn("case 0x00001000U: goto instruction_00001000", source)
            self.assertIn("case 0x00001010U: goto instruction_00001010", source)
            self.assertIn("goto instruction_00001010", source)
            self.assertIn("ShiftLsl(state.r[1], 2U)", source)
            self.assertIn("0x00000000U - state.r[1]", source)
            generated_manifest = json.loads(
                (output / MANIFEST_NAME).read_text(encoding="utf-8")
            )
            self.assertEqual(
                generated_manifest["regions"][0]["entry_points"],
                [0x1000, 0x1010],
            )

    def test_rejects_unsupported_instruction(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            code = root / "code.bin"
            manifest = root / "blocks.json"
            code.write_bytes((0xEF000000).to_bytes(4, "little"))
            self._manifest(manifest, 0x1004)
            with self.assertRaisesRegex(ValueError, "unsupported"):
                generate_true_aot(code, manifest, root / "generated", base=0x1000)

    def test_emits_exact_vfp_curve_scan_operations(self) -> None:
        words = (
            0xEEF00A40,
            0xEE016A10,
            0xEEB81AC1,
            0xEEB41AE0,
            0xEEF1FA10,
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            code = root / "code.bin"
            manifest = root / "blocks.json"
            output = root / "generated"
            code.write_bytes(b"".join(word.to_bytes(4, "little") for word in words))
            self._manifest(manifest, 0x1014)

            generate_true_aot(code, manifest, output, base=0x1000)

            source = (output / SOURCE_NAME).read_text(encoding="utf-8")
            self.assertIn("state.vfp[1] = state.vfp[0]", source)
            self.assertIn("state.vfp[2] = state.r[6]", source)
            self.assertIn("VfpBinary32FromSigned", source)
            self.assertIn("VfpBinary32Compare", source)
            self.assertIn("state.cpsr = (state.cpsr & ~0xF0000000U)", source)

    def test_emits_immediate_signed_halfword_load(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            code = root / "code.bin"
            manifest = root / "blocks.json"
            output = root / "generated"
            code.write_bytes((0xE1D160F0).to_bytes(4, "little"))
            self._manifest(manifest, 0x1004)

            generate_true_aot(code, manifest, output, base=0x1000)

            source = (output / SOURCE_NAME).read_text(encoding="utf-8")
            self.assertIn("memory.Read16", source)
            self.assertIn("static_cast<int16_t>(value)", source)

    def test_emits_function_prologue_literal_and_halfword_operations(self) -> None:
        words = (
            0xE92D0030,
            0xE3C1200F,
            0xE59F3000,
            0xE1D140B2,
            0xE1C050B2,
            0xE1D450F0,
            0xE8BD0030,
            0xE12FFF1E,
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            code = root / "code.bin"
            manifest = root / "blocks.json"
            output = root / "generated"
            code.write_bytes(b"".join(word.to_bytes(4, "little") for word in words))
            self._manifest(manifest, 0x1020)

            generate_true_aot(code, manifest, output, base=0x1000)

            source = (output / SOURCE_NAME).read_text(encoding="utf-8")
            self.assertIn("state.r[1] & ~(0x0000000FU)", source)
            self.assertIn("0x00001010U + 0x0U", source)
            self.assertIn("memory.Read16", source)
            self.assertIn("memory.Write16", source)
            self.assertIn("firstAddress = state.r[13] - 8U", source)
            self.assertIn("state.r[13] = state.r[13] + 8U", source)
            self.assertIn("BranchTo(state.r[14]", source)

    def test_emits_register_memory_and_vfp_memory(self) -> None:
        words = (
            0xE7903109,
            0xED970A06,
            0xED8D0A01,
            0xE12FFF1E,
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            code = root / "code.bin"
            manifest = root / "blocks.json"
            output = root / "generated"
            code.write_bytes(b"".join(word.to_bytes(4, "little") for word in words))
            self._manifest(manifest, 0x1010)

            generate_true_aot(code, manifest, output, base=0x1000)

            source = (output / SOURCE_NAME).read_text(encoding="utf-8")
            self.assertIn("ShiftLsl(state.r[9], 2U)", source)
            self.assertIn("state.vfp[0] = value", source)
            self.assertIn("memory.Write32(address, state.vfp[0])", source)

            code.write_bytes((0xEB000000).to_bytes(4, "little"))
            self._manifest(manifest, 0x1004)
            with self.assertRaisesRegex(ValueError, "whole-program AOT"):
                generate_true_aot(code, manifest, output, base=0x1000)


if __name__ == "__main__":
    unittest.main()
