// VFP binary64 forms added for ports of modded titles: VCMP{E}.F64 Dd, #0.0
// and VCVT.F64.U32. Adapted from Ocarina ReTuned's reframed_vfp_zero test.
#include "recomp/a32_vfp_binary64.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

using namespace oot3d::recomp::a32;

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "oot3d_native_a32_vfp_binary64_tests: %s\n", message);
        std::exit(1);
    }
}

GuestState StateWith(std::uint64_t value) {
    GuestState state{};
    state.vfp[14] = static_cast<std::uint32_t>(value);
    state.vfp[15] = static_cast<std::uint32_t>(value >> 32);
    state.fpscr = 1U << 22; // Rounding mode bits must survive the comparison.
    return state;
}

} // namespace

int main() {
    for (const auto raw : {0xEEB57B40U, 0xEEB57BC0U}) {
        Require(VfpBinary64Supported(raw), "VCMP{E}.F64 Dd, #0.0 is supported");
        for (const auto pair : {std::array<std::uint64_t, 2>{0x3ff0000000000000ULL, 0x20000000U},
                                std::array<std::uint64_t, 2>{0xbff0000000000000ULL, 0x80000000U},
                                std::array<std::uint64_t, 2>{0x8000000000000000ULL, 0x60000000U}}) {
            auto state = StateWith(pair[0]);
            const auto result = ExecuteVfpBinary64(raw, 0x1000, state);
            Require(result.kind == ExitKind::Fallthrough && result.pc == 0x1004,
                    "compare with zero falls through");
            Require((state.fpscr & 0xf0000000U) == pair[1], "NZCV of compare with zero");
            Require((state.fpscr & (1U << 22)) != 0, "rounding mode preserved");
        }
    }
    auto quiet = StateWith(0x7ff8000000000000ULL);
    ExecuteVfpBinary64(0xEEB57B40U, 0x1000, quiet);
    Require((quiet.fpscr & 0xf0000001U) == 0x30000000U, "quiet NaN compare is unordered");
    auto signaling = StateWith(0x7ff8000000000000ULL);
    ExecuteVfpBinary64(0xEEB57BC0U, 0x1000, signaling);
    Require((signaling.fpscr & 0xf0000001U) == 0x30000001U, "VCMPE raises invalid on NaN");
    Require(!VfpBinary64Supported(0xEEB57BC1U), "non-zero Vm is not the immediate form");
    for (const auto value : {0U, 1U, 0x80000000U, 0xffffffffU}) {
        GuestState state{};
        state.vfp[15] = value;
        Require(VfpBinary64Supported(0xEEB87B67U), "VCVT.F64.U32 is supported");
        const auto result = ExecuteVfpBinary64(0xEEB87B67U, 0x1000, state);
        Require(result.kind == ExitKind::Fallthrough, "VCVT.F64.U32 falls through");
        const auto bits = std::uint64_t{state.vfp[14]} | (std::uint64_t{state.vfp[15]} << 32);
        Require(std::bit_cast<double>(bits) == static_cast<double>(value),
                "VCVT.F64.U32 converts exactly");
    }
    std::puts("oot3d_native_a32_vfp_binary64_tests passed");
    return 0;
}
