"""Execute computed-call continuations with an explicit, nonadjacent LR."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from whole_aot_cpp import Block, Instruction, _emit_terminal


class WholeAotLinkExecutionTests(unittest.TestCase):
    def test_predicate_return_and_unwind(self):
        compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
        if not compiler:
            self.skipTest("a host C++ compiler is required")
        block = Block(0, 0x1000, 0x1004, (Instruction(0x1000, 0x1088F003),),
                      ({"kind": "indirect_call"}, {"kind": "resume", "target": 0x1008}))
        body = "\n".join(_emit_terminal(block, {0x1000, 0x1004, 0x1008}, {}, {}, frozenset()))
        source = r"""
#include <cassert>
#include <cstdint>
enum class Oot3dWholeAotFlowKind { Returned, Faulted };
struct Oot3dWholeAotFlow { Oot3dWholeAotFlowKind Kind; uint32_t Pc; };
struct State { uint32_t R[16]{}; struct { bool Zero; bool Z() const { return Zero; } } Flags{}; };
struct Frame {};
struct Context {
    struct { unsigned IndirectCalls = 0; } Stats;
    Oot3dWholeAotFlow Reply{Oot3dWholeAotFlowKind::Returned, 0x1008};
    uint32_t Target = 0;
    bool Dismissed = false;
};
Oot3dWholeAotFlow ExecuteOot3dWholeAotIndirect(uint32_t target, Frame&, Context& c, State&) {
    c.Target = target; return c.Reply;
}
Oot3dWholeAotFlow Run(Context& context, State& state) {
    Frame frame;
    auto commitState = [] {};
    auto reloadState = [] {};
    struct Commit { Context& C; void Dismiss() { C.Dismissed = true; } } stateCommit{context};
""" + body + r"""
block_00001004: return {Oot3dWholeAotFlowKind::Returned, 0x1004};
block_00001008: return {Oot3dWholeAotFlowKind::Returned, 0x1008};
}
int main() {
    for (bool skip : {false, true}) for (bool fault : {false, true}) {
        Context c; State s; s.Flags.Zero = skip;
        s.R[8] = 0x2000; s.R[3] = 0x20; s.R[14] = 0x1008;
        if (fault) c.Reply = {Oot3dWholeAotFlowKind::Faulted, 0x3000};
        auto result = Run(c, s);
        assert(result.Pc == (skip ? 0x1004U : fault ? 0x3000U : 0x1008U));
        assert(c.Stats.IndirectCalls == unsigned(!skip));
        assert(c.Target == (skip ? 0U : 0x2020U));
        assert(c.Dismissed == (!skip && fault));
        assert(s.R[14] == 0x1008);
    }
}
"""
        source = "#include <initializer_list>\n" + source
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cpp = root / "link.cpp"
            executable = root / ("link.exe" if os.name == "nt" else "link")
            cpp.write_text(source, encoding="utf-8")
            subprocess.run([compiler, "-std=c++17", "-O2", str(cpp), "-o", str(executable)],
                           check=True, capture_output=True, text=True, timeout=60)
            subprocess.run([str(executable)], check=True, capture_output=True, timeout=15)


if __name__ == "__main__":
    unittest.main()
