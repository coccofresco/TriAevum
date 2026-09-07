#include "oot3d_native_true_aot_blocks.h"

#if defined(OOT3D_NATIVE_GENERATED_TRUE_AOT)
#include "oot3d_a32_true_aot_generated.h"
#endif

#include <array>

namespace Oot3dNativeGame {
namespace {

#if !defined(OOT3D_NATIVE_GENERATED_TRUE_AOT)
constexpr uint32_t kBgCheckLineTestClearBlock = 0x003247B0U;
constexpr std::array kEntryPoints{kBgCheckLineTestClearBlock};
#endif
Oot3dTrueAotBlockStats gStats;

#if !defined(OOT3D_NATIVE_GENERATED_TRUE_AOT)
void UpdateSubtractFlags(oot3d::recomp::a32::GuestState& state,
                         uint32_t left, uint32_t right) noexcept {
    const uint32_t value = left - right;
    const bool carry = left >= right;
    const bool overflow =
        (((left ^ right) & (left ^ value)) &
         oot3d::recomp::a32::kFlagN) != 0U;
    state.cpsr &= ~(oot3d::recomp::a32::kFlagN |
                    oot3d::recomp::a32::kFlagZ |
                    oot3d::recomp::a32::kFlagC |
                    oot3d::recomp::a32::kFlagV);
    if ((value & oot3d::recomp::a32::kFlagN) != 0U) {
        state.cpsr |= oot3d::recomp::a32::kFlagN;
    }
    if (value == 0U) {
        state.cpsr |= oot3d::recomp::a32::kFlagZ;
    }
    if (carry) {
        state.cpsr |= oot3d::recomp::a32::kFlagC;
    }
    if (overflow) {
        state.cpsr |= oot3d::recomp::a32::kFlagV;
    }
}

bool ExecuteBgCheckLineTestClearBlock(
    oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory,
    oot3d::recomp::a32::ExecutionResult* result) {
    // Direct translation of 0x003247B0..0x003247C0. The self back-edge stays
    // in host code, as it would in a compiled function; no opcode decoder or
    // per-iteration dispatcher is entered.
    for (;;) {
        ++gStats.Iterations;
        const uint32_t writeAddress = state.r[0];
        if (!memory.Write8(writeAddress, static_cast<uint8_t>(state.r[5]))) {
            state.r[15] = 0x003247B0U;
            *result = {oot3d::recomp::a32::ExitKind::MemoryFault,
                       state.r[15],
                       oot3d::recomp::a32::FallbackReason::None,
                       writeAddress};
            ++gStats.MemoryFaults;
            return true;
        }
        state.r[0] = writeAddress + 1U;

        const uint32_t readAddress = state.r[8] + 0x4CU;
        if (!memory.Read32(readAddress, &state.r[2])) {
            state.r[15] = 0x003247B4U;
            *result = {oot3d::recomp::a32::ExitKind::MemoryFault,
                       state.r[15],
                       oot3d::recomp::a32::FallbackReason::None,
                       readAddress};
            ++gStats.MemoryFaults;
            return true;
        }
        state.r[2] += state.r[1];
        UpdateSubtractFlags(state, state.r[2], state.r[0]);
        const bool branch =
            (state.cpsr & oot3d::recomp::a32::kFlagC) != 0U &&
            (state.cpsr & oot3d::recomp::a32::kFlagZ) == 0U;
        if (!branch) {
            state.r[15] = 0x003247C4U;
            *result = {oot3d::recomp::a32::ExitKind::Fallthrough,
                       state.r[15],
                       oot3d::recomp::a32::FallbackReason::None, 0U};
            return true;
        }
    }
}
#endif

} // namespace

std::span<const uint32_t> Oot3dTrueAotBlockEntryPoints() noexcept {
#if defined(OOT3D_NATIVE_GENERATED_TRUE_AOT)
    return oot3d::recomp::GetA32GeneratedTrueAotEntryPoints();
#else
    return kEntryPoints;
#endif
}

void ResetOot3dTrueAotBlockStats() noexcept {
    gStats = {};
}

Oot3dTrueAotBlockStats GetOot3dTrueAotBlockStats() noexcept {
    return gStats;
}

bool ExecuteOot3dTrueAotBlock(
    uint32_t pc,
    oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory,
    oot3d::recomp::a32::ExecutionResult* result,
    void*) {
#if defined(OOT3D_NATIVE_GENERATED_TRUE_AOT)
    oot3d::recomp::GeneratedTrueAotRunStats runStats{};
    if (!oot3d::recomp::ExecuteA32GeneratedTrueAotBlock(
            pc, state, memory, result, &runStats)) {
        return false;
    }
    ++gStats.Calls;
    gStats.Iterations += runStats.Iterations;
    gStats.MemoryFaults += runStats.MemoryFaults;
    return true;
#else
    if (result == nullptr || pc != kBgCheckLineTestClearBlock) {
        return false;
    }
    ++gStats.Calls;
    return ExecuteBgCheckLineTestClearBlock(state, memory, result);
#endif
}

} // namespace Oot3dNativeGame
