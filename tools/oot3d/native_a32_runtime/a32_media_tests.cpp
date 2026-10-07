// ARMv6 SIMD media instructions of the pinned A32 core, on encodings taken
// from Majora's Mask 3D's code and hand-computed results.
#include "recomp/a32_core.h"

#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <string>
#include <utility>

namespace {

using namespace oot3d::recomp::a32;

constexpr std::uint32_t kGeShift = 16U;
constexpr std::uint32_t kQ = 1U << 27U;

class NoMemory final : public MemoryBus {
public:
    bool Read32(std::uint32_t, std::uint32_t*) override { return false; }
    bool Write32(std::uint32_t, std::uint32_t) override { return false; }
};

int gFailures = 0;

// Runs `raw` with the given registers; returns the state afterwards.
GuestState Run(std::uint32_t raw,
               std::initializer_list<std::pair<unsigned, std::uint32_t>> registers,
               std::uint32_t cpsr = 0U) {
    GuestState state{};
    state.cpsr = cpsr;
    for (const auto& [index, value] : registers) {
        state.r[index] = value;
    }
    NoMemory memory;
    const auto result = ExecuteCore(raw, 0x00100000U, state, memory);
    if (result.kind != ExitKind::Fallthrough) {
        std::cerr << "FAIL: 0x" << std::hex << raw << " not executed\n" << std::dec;
        ++gFailures;
    }
    return state;
}

void Expect(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++gFailures;
    }
}

std::uint32_t Ge(const GuestState& state) { return (state.cpsr >> kGeShift) & 0xFU; }

}  // namespace

int main() {
    auto s = Run(0xE6220FF3U, {{2, 0x807F1000U}, {3, 0x01812001U}});  // QSUB8 r0,r2,r3
    Expect(s.r[0] == 0x807FF0FFU, "QSUB8 saturates per byte");

    s = Run(0xE66ECFF2U, {{14, 0x01010101U}, {2, 0x00410200U}});  // UQSUB8 ip,lr,r2
    Expect(s.r[12] == 0x01000001U, "UQSUB8 unchanged");

    s = Run(0xE6239F17U, {{3, 0x7FFF0001U}, {7, 0x0001FFFFU}});  // QADD16 sb,r3,r7
    Expect(s.r[9] == 0x7FFF0000U, "QADD16 saturates per halfword");

    s = Run(0xE61C3F14U, {{12, 0x80000001U}, {4, 0xFFFF0002U}});  // SADD16 r3,ip,r4
    Expect(s.r[3] == 0x7FFF0003U && Ge(s) == 0x3U, "SADD16 result and GE");
    s = Run(0xE6810FB2U, {{1, 0xAABBCCDDU}, {2, 0x11223344U}}, s.cpsr);  // SEL r0,r1,r2
    Expect(s.r[0] == 0x1122CCDDU, "SEL picks bytes by GE");

    s = Run(0xE6510F92U, {{1, 0xFF018010U}, {2, 0x010180F0U}});  // UADD8 r0,r1,r2
    Expect(s.r[0] == 0x00020000U && Ge(s) == 0xBU, "UADD8 result and carry GE");

    s = Run(0xE678EF9AU, {{8, 0xFF030201U}, {10, 0x010102FFU}});  // UHADD8 lr,r8,sl
    Expect(s.r[14] == 0x80020280U, "UHADD8 halves without overflow");

    s = Run(0xE6285F36U, {{8, 0x00107FF0U}, {6, 0x0020F000U}});  // QASX r5,r8,r6
    Expect(s.r[5] == 0xF0107FD0U, "QASX exchanges lanes");

    s = Run(0xE6244F54U, {{4, 0x7FFF8000U}});  // QSAX r4,r4,r4
    Expect(s.r[4] == 0x7FFFFFFFU, "QSAX saturates the subtracted lane");

    s = Run(0xE6E83F33U, {{3, 0x0123FF80U}});  // USAT16 r3,#8,r3
    Expect(s.r[3] == 0x00FF0000U && (s.cpsr & kQ) != 0U, "USAT16 clamps and sets Q");

    s = Run(0xE6AD8F38U, {{8, 0x4000E000U}});  // SSAT16 r8,#14,r8
    Expect(s.r[8] == 0x1FFFE000U && (s.cpsr & kQ) != 0U, "SSAT16 clamps and sets Q");

    s = Run(0xE7039817U, {{7, 0x00030002U}, {8, 0x00050004U}, {9, 100U}});  // SMLAD r3,r7,r8,sb
    Expect(s.r[3] == 123U, "SMLAD");

    s = Run(0xE706F652U, {{2, 0x00030002U}, {6, 0x00050004U}});  // SMUSD r6,r2,r6
    Expect(s.r[6] == 0xFFFFFFF9U, "SMUSD");

    s = Run(0xE70CF612U, {{2, 0x00030002U}, {6, 0x00050004U}});  // SMUAD ip,r2,r6
    Expect(s.r[12] == 23U && (s.cpsr & kQ) == 0U, "SMUAD");

    s = Run(0xE70CF632U, {{2, 0x00030002U}, {6, 0x00050004U}});  // SMUADX ip,r2,r6
    Expect(s.r[12] == 22U, "SMUADX exchanges halves");

    s = Run(0xE70CF612U, {{2, 0x80008000U}, {6, 0x80008000U}});  // SMUAD overflow
    Expect(s.r[12] == 0x80000000U && (s.cpsr & kQ) != 0U, "SMUAD overflow sets Q");

    s = Run(0xE6C6C07CU, {{6, 0x0001FFFFU}, {12, 0x00F00010U}});  // UXTAB16 ip,r6,ip
    Expect(s.r[12] == 0x00F1000FU, "UXTAB16 adds per halfword");

    if (gFailures != 0) {
        std::cerr << gFailures << " media check(s) failed\n";
        return 1;
    }
    std::cout << "a32 media tests passed\n";
    return 0;
}
