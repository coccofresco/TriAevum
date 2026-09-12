"""Execute generated VFP memory operations, not just their source spelling."""

from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from whole_aot_cpp import Instruction, _emit_vfp


class WholeAotVfpMemoryExecutionTests(unittest.TestCase):
    def test_generated_transfers(self) -> None:
        compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
        if not compiler:
            self.skipTest("a host C++ compiler is required for generated-code execution")

        functions = []
        cases = []
        for lane in range(32):
            for double in (False, True):
                if double and lane % 2:
                    continue
                for positive in (False, True):
                    for load in (False, True):
                        raw = (0x1D020A03 | (int(positive) << 23) | (int(load) << 20)
                               | ((lane // 2) << 12) | (int(double) << 8))
                        if not double:
                            raw |= (lane % 2) << 22
                        index = len(functions)
                        body = "\n".join(_emit_vfp(Instruction(0x1000, raw)))
                        functions.append(f"int Run{index}(Frame& frame, Context& context, State& state) {{\n{body}\nreturn 0;\n}}")
                        cases.append(f"Check(Run{index}, {lane}, {str(double).lower()}, {str(positive).lower()}, {str(load).lower()});")

        literal = "\n".join(_emit_vfp(Instruction(0x1000, 0xED9FFB03)))
        source = r"""
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>

struct Bus {
    std::array<uint8_t, 8192> Bytes{};
    uint32_t End = Bytes.size();
    unsigned Transfers = 0, Width = 0;
    template<class T> bool ReadFast(uint32_t address, T* value) {
        ++Transfers; Width = sizeof(T);
        if (address > End || sizeof(T) > End - address) return false;
        std::memcpy(value, Bytes.data() + address, sizeof(T)); return true;
    }
    template<class T> bool WriteFast(uint32_t address, T value) {
        ++Transfers; Width = sizeof(T);
        if (address > End || sizeof(T) > End - address) return false;
        std::memcpy(Bytes.data() + address, &value, sizeof(T)); return true;
    }
};
struct Frame { struct { std::array<uint32_t, 32> vfp; } Guest; };
struct Context { Bus Memory; uint32_t FaultPc = 0, FaultAddress = 0; };
struct State {
    uint32_t R[16]{};
    struct { bool Zero = false; bool Z() const { return Zero; } } Flags;
};
int Oot3dAotMemoryFault(Context& c, uint32_t pc, uint32_t address) {
    c.FaultPc = pc; c.FaultAddress = address; return -1;
}
using Run = int (*)(Frame&, Context&, State&);
void Check(Run run, unsigned lane, bool isDouble, bool positive, bool load) {
    constexpr uint32_t address = 100; // 4-byte aligned, deliberately not 8-byte aligned.
    constexpr uint32_t low = 0x89ABCDEF, high = 0x400921FB, sentinel = 0xDEADC0DE;
    for (bool fault : {false, true}) for (bool skip : {false, true}) {
        Frame f; f.Guest.vfp.fill(sentinel);
        Context c; c.Memory.Bytes.fill(0xA5);
        State s; s.R[2] = positive ? address - 12 : address + 12; s.Flags.Zero = skip;
        std::memcpy(c.Memory.Bytes.data() + address, &low, 4);
        std::memcpy(c.Memory.Bytes.data() + address + 4, &high, 4);
        if (!load) { f.Guest.vfp[lane] = low; if (isDouble) f.Guest.vfp[lane + 1] = high; }
        auto before = f.Guest.vfp;
        auto memoryBefore = c.Memory.Bytes;
        if (fault) c.Memory.End = address + (isDouble ? 4 : 0);
        assert(run(f, c, s) == (fault && !skip ? -1 : 0));
        assert(c.Memory.Transfers == (skip ? 0U : 1U));
        if (!skip) assert(c.Memory.Width == (isDouble ? 8U : 4U));
        if (fault || skip) {
            assert(f.Guest.vfp == before); assert(c.Memory.Bytes == memoryBefore);
            if (fault && !skip) { assert(c.FaultPc == 0x1000); assert(c.FaultAddress == address); }
        } else if (load) {
            before[lane] = low; if (isDouble) before[lane + 1] = high;
            assert(f.Guest.vfp == before); assert(c.Memory.Bytes == memoryBefore);
        } else {
            assert(f.Guest.vfp == before); assert(c.Memory.Bytes == memoryBefore);
        }
        assert(s.R[2] == (positive ? address - 12 : address + 12));
    }
}
"""
        source += "\n".join(functions)
        source += f"\nint Literal(Frame& frame, Context& context, State& state) {{\n{literal}\nreturn 0;\n}}\n"
        source += "int main() {\n" + "\n".join(cases)
        source += r"""
    Frame f{}; Context c; State s;
    const uint64_t expected = 0x400921FB89ABCDEFULL;
    assert(c.Memory.WriteFast<uint64_t>(0x1014, expected));
    assert(Literal(f, c, s) == 0);
    assert(f.Guest.vfp[30] == 0x89ABCDEF && f.Guest.vfp[31] == 0x400921FB);
}
"""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cpp = root / "vfp_memory.cpp"
            executable = root / ("vfp_memory.exe" if os.name == "nt" else "vfp_memory")
            cpp.write_text(source, encoding="utf-8")
            subprocess.run([compiler, "-std=c++17", "-O2", str(cpp), "-o", str(executable)],
                           check=True, capture_output=True, text=True, timeout=60)
            subprocess.run([str(executable)], check=True, capture_output=True, timeout=15)


if __name__ == "__main__":
    unittest.main()
