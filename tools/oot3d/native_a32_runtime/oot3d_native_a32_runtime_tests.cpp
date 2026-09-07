#include "recomp/a32_runtime.h"
#include "oot3d_aot_architectural_state.h"
#include "oot3d_native_a32_vfp_ops.h"
#include "../native_game_runtime/oot3d_native_whole_aot_runtime.h"

#include <array>
#include <cstdint>
#include <iostream>

namespace {

class WordMemory final : public oot3d::recomp::a32::MemoryBus {
  public:
    bool Read32(std::uint32_t address, std::uint32_t* value) override {
        if ((address & 3U) != 0 || address / 4U >= mWords.size()) {
            return false;
        }
        *value = mWords[address / 4U];
        return true;
    }

    bool Write32(std::uint32_t address, std::uint32_t value) override {
        if ((address & 3U) != 0 || address / 4U >= mWords.size()) {
            return false;
        }
        mWords[address / 4U] = value;
        return true;
    }

  private:
    std::array<std::uint32_t, 16> mWords{};
};

oot3d::recomp::a32::ExecutionResult ObserveFallbackPc(
    oot3d::recomp::a32::FallbackReason,
    std::uint32_t pc,
    const oot3d::recomp::a32::PackedOp&,
    oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus&,
    void* user) {
    *static_cast<bool*>(user) = state.r[15] == pc;
    return {oot3d::recomp::a32::ExitKind::Fallthrough, pc + 4U,
            oot3d::recomp::a32::FallbackReason::None, 0U};
}

bool ExecuteNativeProbe(
    std::uint32_t pc,
    oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus&,
    oot3d::recomp::a32::ExecutionResult* result,
    void*) {
    if (pc != 0x00103100U || result == nullptr) {
        return false;
    }
    state.r[0] = 0xC0DEF00DU;
    *result = {oot3d::recomp::a32::ExitKind::Branch, state.r[14],
               oot3d::recomp::a32::FallbackReason::None, pc};
    return true;
}

bool ExecuteRegistrylessNativeProbe(
    std::uint32_t pc,
    oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus&,
    oot3d::recomp::a32::ExecutionResult* result,
    std::uint32_t blockBudget,
    std::uint32_t* blocksConsumed,
    void* user) {
    if (pc != 0x00104000U || result == nullptr || blockBudget != 3U) {
        return false;
    }
    *static_cast<bool*>(user) = true;
    state.r[0] = 0xA07A07U;
    *blocksConsumed = 2U;
    *result = {oot3d::recomp::a32::ExitKind::Svc, pc,
               oot3d::recomp::a32::FallbackReason::None, 0x42U};
    return true;
}

} // namespace

int main() {
    {
        Oot3dNativeGame::Oot3dAotBlockEntryFilter filter;
        constexpr std::array<std::uint32_t, 4> hookPcs{
            0x00100000U, 0x00123454U, 0x00567898U, 0x00ABCDF0U};
        for (const std::uint32_t pc : hookPcs) {
            filter.Insert(pc);
        }
        for (const std::uint32_t pc : hookPcs) {
            if (!filter.MayContain(pc)) {
                std::cerr << "AOT block-entry filter produced a false negative\n";
                return 1;
            }
        }
    }

    {
        using Oot3dNativeGame::Oot3dAotExternalStateAccessFor;
        constexpr auto pica =
            Oot3dAotExternalStateAccessFor(0x00307BD8U);
        constexpr auto multiply =
            Oot3dAotExternalStateAccessFor(0x0036C174U);
        constexpr auto copy =
            Oot3dAotExternalStateAccessFor(0x00372224U);
        constexpr auto conservative =
            Oot3dAotExternalStateAccessFor(0x00100000U);
        if (pica.InputGprs != 0x600FU || pica.OutputGprs != 0U ||
            pica.InputFlags || pica.OutputFlags ||
            multiply.InputGprs != 0x4007U ||
            multiply.OutputGprs != 0x0006U || multiply.InputFlags ||
            multiply.OutputFlags || copy.InputGprs != 0x4003U ||
            copy.OutputGprs != 0U || copy.InputFlags || copy.OutputFlags ||
            conservative.InputGprs != 0x7FFFU ||
            conservative.OutputGprs != 0x7FFFU ||
            !conservative.InputFlags || !conservative.OutputFlags) {
            std::cerr << "whole-AOT external state mask contract changed\n";
            return 1;
        }
    }

    bool dismissedScopeCommitted = false;
    {
        Oot3dNativeGame::Oot3dAotScopeExit dismissedScope(
            [&dismissedScopeCommitted]() noexcept {
                dismissedScopeCommitted = true;
            });
        dismissedScope.Dismiss();
    }
    if (dismissedScopeCommitted) {
        std::cerr << "dismissed AOT scope committed stale state\n";
        return 1;
    }

    oot3d::recomp::a32::GuestState observableState{};
    observableState.r[0] = 0x51A7E123U;
    try {
        Oot3dNativeGame::Oot3dWholeAotExitAt(
            0x00123450U, observableState);
    } catch (const Oot3dNativeGame::Oot3dWholeAotObservableExit& exit) {
        oot3d::recomp::a32::GuestState restoredState{};
        if (exit.Pc != 0x00123450U ||
            !Oot3dNativeGame::Oot3dWholeAotTakeObservableExitSnapshot(
                exit.Pc, &restoredState) ||
            restoredState.r[0] != observableState.r[0]) {
            std::cerr << "AOT observable exit lost materialized guest state\n";
            return 1;
        }
    }

    using namespace oot3d::recomp::a32;
    constexpr PackedOp ops[] = {
        {0xE3A0002AU, EncodeMetadata(Opcode::MovImm, Condition::Al, Immediate)},
        {0xE2800001U, EncodeMetadata(Opcode::Add, Condition::Al, Immediate)},
    };
    const Block block{0x00100000U, ops, 2U};
    GuestState state{};
    WordMemory memory;
    {
        using namespace Oot3dNativeGame;
        GuestState promotedGuest{};
        promotedGuest.r[0] = 7U;
        promotedGuest.cpsr = kFlagC | (1U << 27U);
        Oot3dAotArchitecturalState promoted(promotedGuest);
        promoted.R[0] = 9U;
        promoted.Flags.SetAdd(
            0x7FFFFFFFU, 1U,
            Oot3dAotFlagMask::N | Oot3dAotFlagMask::V);
        if (promotedGuest.r[0] != 7U ||
            promotedGuest.cpsr != (kFlagC | (1U << 27U))) {
            std::cerr << "promoted state became observable before flush\n";
            return 1;
        }
        promoted.Flush(1U << 0U, true);
        if (promotedGuest.r[0] != 9U ||
            (promotedGuest.cpsr & (kFlagN | kFlagV | kFlagC)) !=
                (kFlagN | kFlagV | kFlagC) ||
            (promotedGuest.cpsr & (1U << 27U)) == 0U) {
            std::cerr << "promoted state flush or partial flags failed\n";
            return 1;
        }
        promotedGuest.r[0] = 11U;
        promotedGuest.cpsr = kFlagZ;
        promoted.Reload(1U << 0U, true);
        if (promoted.R[0] != 11U || !promoted.Flags.Z() ||
            promoted.Flags.N() || promoted.Flags.C() || promoted.Flags.V()) {
            std::cerr << "promoted state observable reload failed\n";
            return 1;
        }

        Oot3dAotLazyFlags shiftFlags(kFlagZ | kFlagC);
        uint32_t nonZeroBitCount = 0U;
        Oot3dAotSetLogicalFlags(
            shiftFlags, 0x7FU & 1U, shiftFlags.C(),
            Oot3dAotFlagMask::Z);
        if (!shiftFlags.Z()) {
            ++nonZeroBitCount;
        }
        Oot3dAotSetSubtractFlags(
            shiftFlags, 1U, 1U,
            Oot3dAotFlagMask::C | Oot3dAotFlagMask::Z);
        if (nonZeroBitCount != 1U || !shiftFlags.Z() || !shiftFlags.C()) {
            std::cerr << "partial flags sequence failed\n";
            return 1;
        }
    }
    const auto result = ExecuteBlock(block, state, memory);
    if (result.kind != ExitKind::Fallthrough || state.r[0] != 43U ||
        state.r[15] != 0x00100008U || !ConditionPassed(Condition::Al, state.cpsr)) {
        std::cerr << "pinned A32 runtime smoke test failed\n";
        return 1;
    }
    constexpr PackedOp classifiedOps[] = {
        {0xE1A04000U, EncodeMetadata(Opcode::CoreAlu, Condition::Al)},
        {0xE5912000U, EncodeMetadata(Opcode::CoreMemory, Condition::Al)},
        {0xEE1D3F70U, EncodeMetadata(Opcode::CoreSystem, Condition::Al)},
    };
    const Block classifiedBlock{0x00101000U, classifiedOps, 3U};
    GuestState classifiedState{};
    classifiedState.r[0] = 77U;
    classifiedState.r[1] = 0U;
    classifiedState.thread_pointer = 0x12345678U;
    memory.Write32(0U, 99U);
    const auto classifiedResult =
        ExecuteBlock(classifiedBlock, classifiedState, memory);
    if (classifiedResult.kind != ExitKind::Fallthrough ||
        classifiedState.r[4] != 77U || classifiedState.r[2] != 99U ||
        classifiedState.r[3] != classifiedState.thread_pointer) {
        std::cerr << "classified A32 core decoder test failed\n";
        return 1;
    }
    constexpr PackedOp mrsOps[] = {
        {0xE10F2000U, EncodeMetadata(Opcode::CoreSystem, Condition::Al)},
    };
    GuestState systemState{};
    systemState.cpsr = 0xA5001234U;
    const auto mrsResult =
        ExecuteBlock({0x00101800U, mrsOps, 1U}, systemState, memory);
    if (mrsResult.kind != ExitKind::Fallthrough ||
        systemState.r[2] != 0xA5001234U) {
        std::cerr << "MRS APSR execution failed\n";
        return 1;
    }
    constexpr PackedOp msrOps[] = {
        {0xE128F002U, EncodeMetadata(Opcode::CoreSystem, Condition::Al)},
    };
    systemState.cpsr = 0x01234567U;
    systemState.r[2] = 0x5AFFFFFFU;
    const auto msrResult =
        ExecuteBlock({0x00101804U, msrOps, 1U}, systemState, memory);
    if (msrResult.kind != ExitKind::Fallthrough ||
        systemState.cpsr != 0x5A234567U) {
        std::cerr << "MSR APSR_nzcvq execution failed\n";
        return 1;
    }
    constexpr PackedOp fallbackOps[] = {
        {0U, EncodeMetadata(Opcode::Unsupported, Condition::Al)},
    };
    const Block fallbackBlock{0x00102000U, fallbackOps, 1U};
    bool fallbackObservedPc = false;
    const auto fallbackResult = ExecuteBlock(
        fallbackBlock, classifiedState, memory, ObserveFallbackPc,
        &fallbackObservedPc);
    if (fallbackResult.kind != ExitKind::Fallthrough ||
        fallbackResult.pc != 0x00102004U || !fallbackObservedPc ||
        classifiedState.r[15] != 0x00102004U) {
        std::cerr << "deferred A32 PC fallback boundary test failed\n";
        return 1;
    }
    static constexpr PackedOp nativeCallOps[] = {
        {0xEB00003EU,
         EncodeMetadata(Opcode::Branch, Condition::Al, Link)},
    };
    static constexpr PackedOp nativeReturnOps[] = {
        {0xEF00002AU, EncodeMetadata(Opcode::Svc, Condition::Al)},
    };
    static constexpr Block nativeCallBlock{
        0x00103000U, nativeCallOps, 1U};
    static constexpr Block nativeReturnBlock{
        0x00103004U, nativeReturnOps, 1U};
    static constexpr Block nativeBlocks[]{
        nativeCallBlock, nativeReturnBlock};
    static constexpr BlockShard nativeShard{
        0x00103000U, 0x00103004U, nativeBlocks, 2U};
    static constexpr Registry nativeRegistry{&nativeShard, 1U, nullptr, 0U};
    constexpr std::uint32_t nativeFunctionPcs[]{0x00103100U};
    GuestState nativeState{};
    const auto nativeResult = Dispatch(
        nativeRegistry, 0x00103000U, nativeState, memory, nullptr, nullptr,
        4U, nullptr, nullptr, nullptr, 0U, &ExecuteNativeProbe, nullptr,
        nativeFunctionPcs, 1U);
    if (nativeResult.kind != ExitKind::Svc || nativeResult.detail != 0x2AU ||
        nativeState.r[0] != 0xC0DEF00DU ||
        nativeState.r[15] != 0x00103004U ||
        nativeState.r[14] != 0x00103004U) {
        std::cerr << "native compiled function dispatch test failed\n";
        return 1;
    }
    static constexpr Registry emptyRegistry{};
    bool registrylessNativeCalled = false;
    GuestState registrylessState{};
    const auto registrylessResult = Dispatch(
        emptyRegistry, 0x00104000U, registrylessState, memory, nullptr,
        nullptr, 3U, nullptr, nullptr, nullptr, 0U, nullptr, nullptr,
        nullptr, 0U, &ExecuteRegistrylessNativeProbe,
        &registrylessNativeCalled);
    if (!registrylessNativeCalled ||
        registrylessResult.kind != ExitKind::Svc ||
        registrylessResult.detail != 0x42U ||
        registrylessState.r[0] != 0xA07A07U ||
        registrylessState.r[15] != 0x00104000U) {
        std::cerr << "registry-less native block dispatch test failed\n";
        return 1;
    }

    const auto expectVfp = [](VfpBinary32Result value,
                              std::uint32_t expected) {
        return value.value == expected && value.exception_flags == 0U;
    };
    if (!expectVfp(VfpBinary32Add(0x3FC00000U, 0x40100000U, 0U),
                   0x40700000U) ||
        !expectVfp(VfpBinary32Subtract(0x40700000U, 0x3FC00000U, 0U),
                   0x40100000U) ||
        !expectVfp(VfpBinary32Multiply(0x3FC00000U, 0x40000000U, 0U),
                   0x40400000U) ||
        !expectVfp(VfpBinary32Divide(0x40400000U, 0x40000000U, 0U),
                   0x3FC00000U) ||
        !expectVfp(VfpBinary32MultiplyAccumulate(
                       0x3F800000U, 0x40000000U, 0x40400000U, 0U),
                   0x40E00000U) ||
        !expectVfp(VfpBinary32MultiplySubtract(
                       0x40E00000U, 0x40000000U, 0x40400000U, 0U),
                   0x3F800000U) ||
        !expectVfp(VfpBinary32NegativeMultiplyAccumulate(
                       0x3F800000U, 0x40000000U, 0x40400000U, 0U),
                   0xC0E00000U) ||
        !expectVfp(VfpBinary32NegativeMultiplySubtract(
                       0x3F800000U, 0x40000000U, 0x40400000U, 0U),
                   0x40A00000U) ||
        !expectVfp(VfpBinary32NegativeMultiply(
                       0x40000000U, 0x40400000U, 0U),
                   0xC0C00000U) ||
        !expectVfp(VfpBinary32SquareRoot(0x40800000U, 0U),
                   0x40000000U) ||
        !expectVfp(VfpBinary32FromUnsigned(3U, 0U), 0x40400000U)) {
        std::cerr << "named binary32 AOT primitive test failed\n";
        return 1;
    }
    std::cout << "pinned A32 runtime smoke test passed\n";
    return 0;
}
