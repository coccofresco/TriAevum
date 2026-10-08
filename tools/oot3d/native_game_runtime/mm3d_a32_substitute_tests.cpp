#include "mm3d_a32_substitute.h"

#include <cstdlib>
#include <iostream>
#include <map>
#include <string_view>

namespace {

using namespace Oot3dNativeGame;
namespace a32 = oot3d::recomp::a32;

void Require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "mm3d_a32_substitute_tests: " << message << '\n';
        std::exit(1);
    }
}

class WordMemory final : public a32::MemoryBus {
  public:
    std::map<std::uint32_t, std::uint32_t> Words;
    bool Read32(std::uint32_t address, std::uint32_t* value) override {
        const auto found = Words.find(address);
        if (found == Words.end()) {
            return false;
        }
        *value = found->second;
        return true;
    }
    bool Write32(std::uint32_t address, std::uint32_t value) override {
        Words[address] = value;
        return true;
    }
};

constexpr std::uint32_t kPc = 0x005A57B4U;
constexpr std::uint32_t kN = 1U << 31U;
constexpr std::uint32_t kZ = 1U << 30U;
constexpr std::uint32_t kC = 1U << 29U;

// The substitutions dekui makes outside SVC traps, one per form.
void TestDataProcessing() {
    WordMemory memory;
    a32::GuestState state;
    state.r[2] = 0x1234U;
    Require(ExecuteMm3dSubstitute(0xE3A02000U, kPc, state, memory) == kPc + 4U && state.r[2] == 0U,
            "MOV r2, #0");
    state.r[0] = 4U;
    Require(ExecuteMm3dSubstitute(0xE3100006U, kPc, state, memory) == kPc + 4U &&
                (state.cpsr & kZ) == 0U && state.r[0] == 4U,
            "TST r0, #6 with a common bit");
    state.r[0] = 1U;
    (void)ExecuteMm3dSubstitute(0xE3100006U, kPc, state, memory);
    Require((state.cpsr & kZ) != 0U, "TST r0, #6 without a common bit");
    state.r[1] = 5U;
    (void)ExecuteMm3dSubstitute(0xE2416001U, kPc, state, memory);
    Require(state.r[6] == 4U, "SUB r6, r1, #1");
    state.r[0] = 3U;
    (void)ExecuteMm3dSubstitute(0xE2800007U, kPc, state, memory);
    Require(state.r[0] == 10U, "ADD r0, r0, #7");
    state.r[8] = 1U;
    (void)ExecuteMm3dSubstitute(0xE3580001U, kPc, state, memory);
    Require((state.cpsr & (kZ | kC)) == (kZ | kC) && (state.cpsr & kN) == 0U, "CMP r8, #1 equal");
    state.r[8] = 0U;
    (void)ExecuteMm3dSubstitute(0xE3580001U, kPc, state, memory);
    Require((state.cpsr & kN) != 0U && (state.cpsr & (kZ | kC)) == 0U, "CMP r8, #1 below");
    // MOVNE r3, #0 with Z set does nothing.
    state.cpsr = kZ;
    state.r[3] = 9U;
    Require(ExecuteMm3dSubstitute(0x13A03000U, kPc, state, memory) == kPc + 4U && state.r[3] == 9U,
            "a failed condition executed");
}

void TestControlFlow() {
    WordMemory memory;
    a32::GuestState state;
    Require(ExecuteMm3dSubstitute(0xEA000014U, 0x005A2CECU, state, memory) == 0x005A2CECU + 8U + 0x50U,
            "B +0x50");
    state.r[14] = 0x00123456U;
    Require(ExecuteMm3dSubstitute(0xE12FFF1EU, kPc, state, memory) == 0x00123456U, "BX lr");
    state.r[13] = 0x1000U;
    for (std::uint32_t i = 0; i < 6U; ++i) {
        memory.Words[0x1000U + 4U * i] = 0x40U + i;
    }
    Require(ExecuteMm3dSubstitute(0xE8BD81F0U, kPc, state, memory) == 0x45U && state.r[4] == 0x40U &&
                state.r[8] == 0x44U && state.r[13] == 0x1018U,
            "LDMIA sp!, {r4-r8, pc}");
    Require(!ExecuteMm3dSubstitute(0xE1A04000U, kPc, state, memory).has_value(),
            "a register-operand form was executed");
}

} // namespace

int main() {
    TestDataProcessing();
    TestControlFlow();
    std::cout << "mm3d_a32_substitute_tests: ok\n";
    return 0;
}
