"""Execute the whole-AOT lowering of the ARMv6 media/SIMD instructions.

The expected values are the hand-computed results that a32_media_tests.cpp
checks against the pinned interpreter core (a32_core_alu.cpp), on encodings
taken from Majora's Mask 3D's code: the whole-AOT lowering must agree with the
core bit for bit, including the GE and Q flags.
"""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from whole_aot_cpp import Instruction, LoweringError, _emit_non_control

KQ = 1 << 27

# (raw, registers, cpsr in, checked register, expected value, expected GE or None, expected Q or None)
CASES = [
    (0xE6220FF3, {2: 0x807F1000, 3: 0x01812001}, 0, 0, 0x807FF0FF, None, None),  # QSUB8 r0,r2,r3
    (0xE66ECFF2, {14: 0x01010101, 2: 0x00410200}, 0, 12, 0x01000001, None, None),  # UQSUB8
    (0xE6239F17, {3: 0x7FFF0001, 7: 0x0001FFFF}, 0, 9, 0x7FFF0000, None, None),  # QADD16 sb,r3,r7
    (0xE61C3F14, {12: 0x80000001, 4: 0xFFFF0002}, 0, 3, 0x7FFF0003, 0x3, None),  # SADD16 r3,ip,r4
    (0xE6810FB2, {1: 0xAABBCCDD, 2: 0x11223344}, 0x3 << 16, 0, 0x1122CCDD, 0x3, None),  # SEL r0,r1,r2
    (0xE6510F92, {1: 0xFF018010, 2: 0x010180F0}, 0, 0, 0x00020000, 0xB, None),  # UADD8 r0,r1,r2
    (0xE678EF9A, {8: 0xFF030201, 10: 0x010102FF}, 0, 14, 0x80020280, None, None),  # UHADD8 lr,r8,sl
    (0xE6285F36, {8: 0x00107FF0, 6: 0x0020F000}, 0, 5, 0xF0107FD0, None, None),  # QASX r5,r8,r6
    (0xE6244F54, {4: 0x7FFF8000}, 0, 4, 0x7FFFFFFF, None, None),  # QSAX r4,r4,r4
    (0xE6E83F33, {3: 0x0123FF80}, 0, 3, 0x00FF0000, None, True),  # USAT16 r3,#8,r3
    (0xE6AD8F38, {8: 0x4000E000}, 0, 8, 0x1FFFE000, None, True),  # SSAT16 r8,#14,r8
    (0xE7039817, {7: 0x00030002, 8: 0x00050004, 9: 100}, 0, 3, 123, None, False),  # SMLAD r3,r7,r8,sb
    (0xE706F652, {2: 0x00030002, 6: 0x00050004}, 0, 6, 0xFFFFFFF9, None, False),  # SMUSD r6,r2,r6
    (0xE70CF612, {2: 0x00030002, 6: 0x00050004}, 0, 12, 23, None, False),  # SMUAD ip,r2,r6 (MM 0x149DA8)
    (0xE70CF632, {2: 0x00030002, 6: 0x00050004}, 0, 12, 22, None, False),  # SMUADX
    (0xE70CF612, {2: 0x80008000, 6: 0x80008000}, 0, 12, 0x80000000, None, True),  # SMUAD overflow
    (0xE6C6C07C, {6: 0x0001FFFF, 12: 0x00F00010}, 0, 12, 0x00F1000F, None, None),  # UXTAB16 (MM 0x14A270)
    (0xE616AF17, {6: 0x00017FFF, 7: 0x0001FFFF}, 0, 10, 0x00027FFE, 0xF, None),  # SADD16 sl,r6,r7 (MM 0x16E2CC)
]

PRELUDE = r"""
#include <cstdint>
#include <cstdio>
namespace a32 { constexpr uint32_t kFlagC = 1U << 29U; }
struct State {
    uint32_t R[15]{};
    uint32_t Cpsr = 0U;
    void SetPreservedCpsrBits(uint32_t mask, uint32_t value) { Cpsr = (Cpsr & ~mask) | (value & mask); }
    uint32_t PreservedCpsr() const { return Cpsr; }
};
inline void Oot3dAotSetSaturationFlag(State& state, bool saturated) { if (saturated) state.Cpsr |= 1U << 27U; }
inline uint32_t Oot3dAotRor(uint32_t value, uint32_t amount) {
    amount &= 31U; return amount == 0U ? value : (value >> amount) | (value << (32U - amount));
}
"""


def lower(raw: int) -> str:
    return "\n".join(_emit_non_control(Instruction(0x00100000, raw)))


def program(cases) -> str:
    parts = [PRELUDE, "int main() {", "    int failures = 0;"]
    for index, (raw, registers, cpsr, rd, value, ge, q) in enumerate(cases):
        parts.append("    {")
        parts.append("        State state; State& frame = state; (void)frame;")
        parts.append(f"        state.Cpsr = 0x{cpsr:08X}U;")
        for register, content in registers.items():
            parts.append(f"        state.R[{register}] = 0x{content:08X}U;")
        parts.append(lower(raw))
        checks = [f"state.R[{rd}] == 0x{value:08X}U"]
        if ge is not None:
            checks.append(f"((state.Cpsr >> 16U) & 0xFU) == 0x{ge:X}U")
        if q is not None:
            checks.append(f"((state.Cpsr & 0x{KQ:08X}U) != 0U) == {'true' if q else 'false'}")
        parts.append(f"        if (!({' && '.join(checks)})) {{")
        parts.append(f'            std::printf("FAIL case {index} 0x{raw:08X}: r=%08X cpsr=%08X\\n",'
                     f" state.R[{rd}], state.Cpsr); ++failures;")
        parts.append("        }")
        parts.append("    }")
    parts.append("    return failures == 0 ? 0 : 1;")
    parts.append("}")
    return "\n".join(parts)


class WholeAotMediaExecutionTests(unittest.TestCase):
    def test_media_lowering_matches_the_core(self):
        compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
        if not compiler:
            self.skipTest("a host C++ compiler is required")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cpp = root / "media.cpp"
            executable = root / ("media.exe" if os.name == "nt" else "media")
            cpp.write_text(program(CASES), encoding="utf-8")
            subprocess.run([compiler, "-std=c++17", "-O2", str(cpp), "-o", str(executable)],
                           check=True, capture_output=True, text=True, timeout=120)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_every_rejected_mm3d_media_word_lowers(self):
        # The 36 media words, the UXTAB16 pair and the SMUAD of the MM3D r7 rejection log.
        words = [
            0xE6233F16, 0xE6266F19, 0xE6299F1C, 0xE62BAF19, 0xE6255F1A, 0xE6267F19, 0xE6277F19,
            0xE6266F18, 0xE62EAF19, 0xE62EEF19, 0xE62EAF18, 0xE6E83F33, 0xE62ECF19, 0xE62ECF17,
            0xE62E7F19, 0xE6299F1A, 0xE62B8F19, 0xE629BF17, 0xE6288F16, 0xE62BCF19, 0xE629CF17,
            0xE678EF9A, 0xE616AF17, 0xE6220FF3, 0xE6C6C07C, 0xE70CF612,
        ]
        for raw in words:
            with self.subTest(raw=f"0x{raw:08X}"):
                self.assertTrue(lower(raw))

    def test_undefined_and_pc_forms_are_refused(self):
        for raw in (
            0xE6020F13,  # op1 = 0: undefined parallel add/subtract
            0xE6120FB3,  # op2 = 5: undefined
            0xE62F0F13,  # QADD16 with Rn = PC
            0xE6EF8F3F,  # USAT16 with Rm = PC
            0xE70FF612,  # SMUAD with Rd = PC
        ):
            with self.subTest(raw=f"0x{raw:08X}"):
                with self.assertRaises(LoweringError):
                    lower(raw)


if __name__ == "__main__":
    unittest.main()
