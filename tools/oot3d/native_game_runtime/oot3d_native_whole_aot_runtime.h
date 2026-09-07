#pragma once

#include "recomp/a32_runtime.h"
#include "recomp/a32_vfp_binary64.h"
#include "oot3d_aot_architectural_state.h"
#include "oot3d_native_a32_vfp_ops.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace Oot3dNativeGame {

class NativeA32Memory;

struct Oot3dWholeAotStats {
    uint64_t Calls = 0;
    uint64_t DirectCalls = 0;
    uint64_t IndirectCalls = 0;
    uint64_t ResolvedIndirectCalls = 0;
    uint64_t ExternalCalls = 0;
    uint64_t SvcExits = 0;
    uint64_t BlockLimitExits = 0;
    uint64_t MemoryFaults = 0;
    uint64_t UnsupportedExits = 0;
};

struct Oot3dWholeAotFrame {
    oot3d::recomp::a32::GuestState& Guest;

    explicit Oot3dWholeAotFrame(
        oot3d::recomp::a32::GuestState& state) noexcept
        : Guest(state) {
    }
};

enum class Oot3dWholeAotFlowKind : uint8_t {
    Returned,
    Branch,
    Svc,
    BlockLimit,
    MemoryFault,
    Unsupported,
};

struct Oot3dWholeAotFlow {
    Oot3dWholeAotFlowKind Kind = Oot3dWholeAotFlowKind::Unsupported;
    uint32_t Pc = 0;
    uint32_t Detail = 0;
};

struct Oot3dWholeAotObservableExit final {
    uint32_t Pc = 0;
};

struct Oot3dWholeAotObservableExitSnapshot final {
    oot3d::recomp::a32::GuestState State{};
    uint32_t Pc = 0;
    bool Valid = false;
};

// Block-entry callbacks are sparse (normally a few hundred PCs) but every
// guest basic block has to decide whether one applies.  This small
// no-false-negative filter rejects almost all non-hook PCs before the exact
// sorted lookup.  A positive result remains only a hint; callers must still
// use the exact list to reject hash collisions.
struct Oot3dAotBlockEntryFilter final {
    static constexpr size_t kBitCount = 4096U;
    static constexpr size_t kWordCount = kBitCount / 64U;

    std::array<uint64_t, kWordCount> Words{};

    static constexpr size_t BitIndex(uint32_t pc) noexcept {
        const uint32_t instructionIndex = pc >> 2U;
        const uint32_t hash = instructionIndex * 2654435761U;
        return static_cast<size_t>(hash >> 20U);
    }

    void Insert(uint32_t pc) noexcept {
        const size_t bitIndex = BitIndex(pc);
        Words[bitIndex >> 6U] |= uint64_t{1} << (bitIndex & 63U);
    }

    bool MayContain(uint32_t pc) const noexcept {
        const size_t bitIndex = BitIndex(pc);
        return (Words[bitIndex >> 6U] &
                (uint64_t{1} << (bitIndex & 63U))) != 0U;
    }
};

inline thread_local bool gOot3dWholeAotExecutionActive = false;
inline thread_local Oot3dWholeAotObservableExitSnapshot
    gOot3dWholeAotObservableExitSnapshot{};

class Oot3dWholeAotExecutionScope final {
public:
    Oot3dWholeAotExecutionScope() noexcept
        : mPrevious(gOot3dWholeAotExecutionActive) {
        gOot3dWholeAotExecutionActive = true;
    }

    Oot3dWholeAotExecutionScope(const Oot3dWholeAotExecutionScope&) = delete;
    Oot3dWholeAotExecutionScope& operator=(
        const Oot3dWholeAotExecutionScope&) = delete;

    ~Oot3dWholeAotExecutionScope() noexcept {
        gOot3dWholeAotExecutionActive = mPrevious;
    }

private:
    bool mPrevious = false;
};

inline bool Oot3dWholeAotExecutionActive() noexcept {
    return gOot3dWholeAotExecutionActive;
}

[[noreturn]] inline void Oot3dWholeAotExitAt(uint32_t pc) {
    gOot3dWholeAotObservableExitSnapshot.Valid = false;
    throw Oot3dWholeAotObservableExit{pc};
}

[[noreturn]] inline void Oot3dWholeAotExitAt(
    uint32_t pc, const oot3d::recomp::a32::GuestState& state) {
    gOot3dWholeAotObservableExitSnapshot = {state, pc, true};
    throw Oot3dWholeAotObservableExit{pc};
}

inline bool Oot3dWholeAotTakeObservableExitSnapshot(
    uint32_t pc, oot3d::recomp::a32::GuestState* state) noexcept {
    auto& snapshot = gOot3dWholeAotObservableExitSnapshot;
    const bool available =
        snapshot.Valid && snapshot.Pc == pc && state != nullptr;
    if (available) {
        *state = snapshot.State;
    }
    snapshot.Valid = false;
    return available;
}

using Oot3dWholeAotExternalCall = Oot3dWholeAotFlow (*)(
    uint32_t entry,
    Oot3dWholeAotFrame& frame,
    NativeA32Memory& memory);

struct Oot3dWholeAotContext {
    NativeA32Memory& Memory;
    oot3d::recomp::a32::MemoryBus& CallbackMemory;
    Oot3dWholeAotStats& Stats;
    Oot3dWholeAotExternalCall ExternalCall = nullptr;
    oot3d::recomp::a32::BlockEntryCallback BlockEntry = nullptr;
    void* BlockEntryUser = nullptr;
    // A non-empty filtered list is sorted by the runtime producer.
    const uint32_t* BlockEntryPcs = nullptr;
    size_t BlockEntryPcCount = 0;
    const Oot3dAotBlockEntryFilter* BlockEntryFilter = nullptr;
    bool SkipFirstBlockEntry = false;
    uint32_t BlocksRemaining = 0;
    uint32_t BlocksConsumed = 0;
};

struct Oot3dAotExternalStateAccess final {
    uint16_t InputGprs = 0x7FFFU;
    uint16_t OutputGprs = 0x7FFFU;
    bool InputFlags = true;
    bool OutputFlags = true;
};

// The product's hand-written native helpers have a smaller architectural ABI
// than an arbitrary guest callback. Direct AOT calls pass literal entries, so
// this constexpr lookup folds away at each callsite and avoids round-tripping
// all fifteen promoted GPRs and NZCV for the hottest helpers. Unknown entries
// retain the conservative full-state boundary.
constexpr Oot3dAotExternalStateAccess Oot3dAotExternalStateAccessFor(
    uint32_t entry) noexcept {
    constexpr uint16_t R0 = 1U << 0U;
    constexpr uint16_t R1 = 1U << 1U;
    constexpr uint16_t R2 = 1U << 2U;
    constexpr uint16_t R3 = 1U << 3U;
    constexpr uint16_t Sp = 1U << 13U;
    constexpr uint16_t Lr = 1U << 14U;
    switch (entry) {
    case 0x00307BD8U: // PicaCommandWriterWriteRegisterRange
        return {static_cast<uint16_t>(R0 | R1 | R2 | R3 | Sp | Lr),
                0U, false, false};
    case 0x0036C174U: // Mtx3x4Multiply
        return {static_cast<uint16_t>(R0 | R1 | R2 | Lr),
                static_cast<uint16_t>(R1 | R2), false, false};
    case 0x00372224U: // Mtx3x4CopyIfDistinct
        return {static_cast<uint16_t>(R0 | R1 | Lr),
                0U, false, false};
    default:
        return {};
    }
}

inline bool Oot3dAotConsumeBlock(Oot3dWholeAotContext& context) noexcept {
    if (context.BlocksRemaining == 0U) {
        return false;
    }
    --context.BlocksRemaining;
    ++context.BlocksConsumed;
    return true;
}

inline bool Oot3dAotShouldNotifyBlock(
    const Oot3dWholeAotContext& context, uint32_t pc,
    bool firstBlock) noexcept {
    if (context.BlockEntry == nullptr ||
        (firstBlock && context.SkipFirstBlockEntry)) {
        return false;
    }
    if (context.BlockEntryPcCount == 0U) {
        return true;
    }
    if (context.BlockEntryFilter != nullptr &&
        !context.BlockEntryFilter->MayContain(pc)) {
        return false;
    }
    return std::binary_search(
        context.BlockEntryPcs,
        context.BlockEntryPcs + context.BlockEntryPcCount, pc);
}

inline bool Oot3dAotEnterBlock(
    Oot3dWholeAotContext& context, Oot3dWholeAotFrame& frame,
    uint32_t pc) {
    const bool firstBlock = context.BlocksConsumed == 0U;
    if (!Oot3dAotConsumeBlock(context)) {
        return false;
    }
    if (!Oot3dAotShouldNotifyBlock(context, pc, firstBlock)) {
        return true;
    }
    context.BlockEntry(pc, frame.Guest, context.CallbackMemory,
                       context.BlockEntryUser);
    return true;
}

inline bool Oot3dAotEnterBlock(
    Oot3dWholeAotContext& context, Oot3dWholeAotFrame& frame,
    Oot3dAotArchitecturalState& state, uint32_t pc) {
    const bool firstBlock = context.BlocksConsumed == 0U;
    if (!Oot3dAotConsumeBlock(context)) {
        return false;
    }
    if (!Oot3dAotShouldNotifyBlock(context, pc, firstBlock)) {
        return true;
    }
    state.Flush(0x7FFFU, true);
    context.BlockEntry(pc, frame.Guest, context.CallbackMemory,
                       context.BlockEntryUser);
    state.Reload(0x7FFFU, true);
    return true;
}

template <typename CommitState, typename ReloadState>
inline bool Oot3dAotEnterBlock(
    Oot3dWholeAotContext& context, Oot3dWholeAotFrame& frame,
    Oot3dAotArchitecturalState& state, uint32_t pc,
    CommitState&& commitState, ReloadState&& reloadState) {
    const bool firstBlock = context.BlocksConsumed == 0U;
    if (!Oot3dAotConsumeBlock(context)) {
        return false;
    }
    if (!Oot3dAotShouldNotifyBlock(context, pc, firstBlock)) {
        return true;
    }
    commitState();
    state.Flush(0x7FFFU, true);
    context.BlockEntry(pc, frame.Guest, context.CallbackMemory,
                       context.BlockEntryUser);
    state.Reload(0x7FFFU, true);
    reloadState();
    return true;
}

inline Oot3dWholeAotFlow Oot3dAotCallExternal(
    Oot3dWholeAotContext& context,
    uint32_t entry,
    Oot3dWholeAotFrame& frame) noexcept {
    ++context.Stats.ExternalCalls;
    if (context.ExternalCall == nullptr) {
        return {Oot3dWholeAotFlowKind::Unsupported, entry, 0};
    }
    return context.ExternalCall(entry, frame, context.Memory);
}

inline Oot3dWholeAotFlow Oot3dAotCallExternal(
    Oot3dWholeAotContext& context, uint32_t entry,
    Oot3dWholeAotFrame& frame,
    Oot3dAotArchitecturalState& state) noexcept {
    const auto access = Oot3dAotExternalStateAccessFor(entry);
    state.Flush(access.InputGprs, access.InputFlags);
    const Oot3dWholeAotFlow flow = Oot3dAotCallExternal(context, entry, frame);
    state.Reload(access.OutputGprs, access.OutputFlags);
    return flow;
}

template <typename CommitState, typename ReloadState>
inline Oot3dWholeAotFlow Oot3dAotCallExternal(
    Oot3dWholeAotContext& context, uint32_t entry,
    Oot3dWholeAotFrame& frame, Oot3dAotArchitecturalState& state,
    CommitState&& commitState, ReloadState&& reloadState) noexcept {
    commitState();
    const auto access = Oot3dAotExternalStateAccessFor(entry);
    state.Flush(access.InputGprs, access.InputFlags);
    const Oot3dWholeAotFlow flow = Oot3dAotCallExternal(context, entry, frame);
    state.Reload(access.OutputGprs, access.OutputFlags);
    reloadState();
    return flow;
}

inline Oot3dWholeAotFlow Oot3dAotReturned(uint32_t target) noexcept {
    return {Oot3dWholeAotFlowKind::Returned, target, 0};
}

inline Oot3dWholeAotFlow Oot3dAotBranch(uint32_t target) noexcept {
    return {Oot3dWholeAotFlowKind::Branch, target, 0};
}

inline Oot3dWholeAotFlow Oot3dAotSvc(
    Oot3dWholeAotContext& context, uint32_t pc,
    uint32_t immediate) noexcept {
    ++context.Stats.SvcExits;
    return {Oot3dWholeAotFlowKind::Svc, pc, immediate};
}

inline Oot3dWholeAotFlow Oot3dAotBlockLimit(
    Oot3dWholeAotContext& context, uint32_t pc) noexcept {
    ++context.Stats.BlockLimitExits;
    return {Oot3dWholeAotFlowKind::BlockLimit, pc, 0};
}

inline Oot3dWholeAotFlow Oot3dAotMemoryFault(
    Oot3dWholeAotContext& context, uint32_t pc, uint32_t address) noexcept {
    ++context.Stats.MemoryFaults;
    return {Oot3dWholeAotFlowKind::MemoryFault, pc, address};
}

inline void Oot3dAotSetNz(
    Oot3dWholeAotFrame& frame, uint32_t value) noexcept {
    using namespace oot3d::recomp::a32;
    frame.Guest.cpsr =
        (frame.Guest.cpsr & ~(kFlagN | kFlagZ)) |
        (value & kFlagN) | (value == 0 ? kFlagZ : 0);
}

inline void Oot3dAotSetNz64(
    Oot3dWholeAotFrame& frame, uint64_t value) noexcept {
    using namespace oot3d::recomp::a32;
    frame.Guest.cpsr =
        (frame.Guest.cpsr & ~(kFlagN | kFlagZ)) |
        ((value & 0x8000000000000000ULL) != 0U ? kFlagN : 0U) |
        (value == 0U ? kFlagZ : 0U);
}

inline void Oot3dAotSetLogicalFlags(
    Oot3dWholeAotFrame& frame, uint32_t value, bool carry) noexcept {
    using namespace oot3d::recomp::a32;
    frame.Guest.cpsr =
        (frame.Guest.cpsr & ~(kFlagN | kFlagZ | kFlagC)) |
        (value & kFlagN) | (value == 0 ? kFlagZ : 0) |
        (carry ? kFlagC : 0);
}

inline uint32_t Oot3dAotSaturateSigned32(
    int64_t value, bool& saturated) noexcept {
    constexpr int64_t low = -INT64_C(2147483648);
    constexpr int64_t high = INT64_C(2147483647);
    if (value < low) {
        saturated = true;
        return 0x80000000U;
    }
    if (value > high) {
        saturated = true;
        return 0x7FFFFFFFU;
    }
    return static_cast<uint32_t>(value);
}

inline void Oot3dAotSetSaturationFlag(
    Oot3dWholeAotFrame& frame, bool saturated) noexcept {
    if (saturated) {
        frame.Guest.cpsr |= 1U << 27U;
    }
}

struct Oot3dAotShiftResult {
    uint32_t Value = 0;
    bool Carry = false;
};

inline uint32_t Oot3dAotAsr(uint32_t value, uint32_t amount) noexcept;
inline uint32_t Oot3dAotRor(uint32_t value, uint32_t amount) noexcept;

inline Oot3dAotShiftResult Oot3dAotShiftImmediate(
    uint32_t value, uint32_t type, uint32_t amount,
    bool oldCarry) noexcept {
    switch (type & 3U) {
    case 0U: // LSL
        if (amount == 0U) return {value, oldCarry};
        return {value << amount, ((value >> (32U - amount)) & 1U) != 0U};
    case 1U: // LSR; an encoded zero means 32.
        if (amount == 0U) return {0U, (value & 0x80000000U) != 0U};
        return {value >> amount, ((value >> (amount - 1U)) & 1U) != 0U};
    case 2U: // ASR; an encoded zero means 32.
        if (amount == 0U) {
            const bool sign = (value & 0x80000000U) != 0U;
            return {sign ? 0xFFFFFFFFU : 0U, sign};
        }
        return {Oot3dAotAsr(value, amount),
                ((value >> (amount - 1U)) & 1U) != 0U};
    default: // ROR, with encoded zero selecting RRX.
        if (amount == 0U) {
            return {(oldCarry ? 0x80000000U : 0U) | (value >> 1U),
                    (value & 1U) != 0U};
        }
        const uint32_t rotated = Oot3dAotRor(value, amount);
        return {rotated, (rotated & 0x80000000U) != 0U};
    }
}

inline Oot3dAotShiftResult Oot3dAotShiftRegister(
    uint32_t value, uint32_t type, uint32_t amount,
    bool oldCarry) noexcept {
    amount &= 0xFFU;
    if (amount == 0U) return {value, oldCarry};
    switch (type & 3U) {
    case 0U: // LSL
        if (amount < 32U) {
            return {value << amount,
                    ((value >> (32U - amount)) & 1U) != 0U};
        }
        return {0U, amount == 32U && (value & 1U) != 0U};
    case 1U: // LSR
        if (amount < 32U) {
            return {value >> amount,
                    ((value >> (amount - 1U)) & 1U) != 0U};
        }
        return {0U, amount == 32U && (value & 0x80000000U) != 0U};
    case 2U: { // ASR
        const bool sign = (value & 0x80000000U) != 0U;
        if (amount >= 32U) return {sign ? 0xFFFFFFFFU : 0U, sign};
        return {Oot3dAotAsr(value, amount),
                ((value >> (amount - 1U)) & 1U) != 0U};
    }
    default: { // ROR
        const uint32_t rotation = amount & 31U;
        if (rotation == 0U) return {value, (value & 0x80000000U) != 0U};
        const uint32_t rotated = Oot3dAotRor(value, rotation);
        return {rotated, (rotated & 0x80000000U) != 0U};
    }
    }
}

inline void Oot3dAotSetSubtractFlags(
    Oot3dWholeAotFrame& frame, uint32_t left, uint32_t right) noexcept {
    using namespace oot3d::recomp::a32;
    const uint32_t value = left - right;
    frame.Guest.cpsr =
        (frame.Guest.cpsr & ~(kFlagN | kFlagZ | kFlagC | kFlagV)) |
        (value & kFlagN) | (value == 0 ? kFlagZ : 0) |
        (left >= right ? kFlagC : 0) |
        ((((left ^ right) & (left ^ value)) & kFlagN) != 0 ? kFlagV : 0);
}

inline void Oot3dAotSetAddFlags(
    Oot3dWholeAotFrame& frame, uint32_t left, uint32_t right) noexcept {
    using namespace oot3d::recomp::a32;
    const uint32_t value = left + right;
    const uint64_t wide = static_cast<uint64_t>(left) + right;
    frame.Guest.cpsr =
        (frame.Guest.cpsr & ~(kFlagN | kFlagZ | kFlagC | kFlagV)) |
        (value & kFlagN) | (value == 0 ? kFlagZ : 0) |
        (wide > 0xFFFFFFFFULL ? kFlagC : 0) |
        (((~(left ^ right) & (left ^ value)) & kFlagN) != 0 ? kFlagV : 0);
}

inline uint32_t Oot3dAotAddWithCarry(
    Oot3dWholeAotFrame& frame, uint32_t left, uint32_t right,
    bool carryIn, bool setFlags) noexcept {
    using namespace oot3d::recomp::a32;
    const uint64_t wide = static_cast<uint64_t>(left) + right +
                          (carryIn ? 1ULL : 0ULL);
    const uint32_t value = static_cast<uint32_t>(wide);
    if (setFlags) {
        frame.Guest.cpsr =
            (frame.Guest.cpsr & ~(kFlagN | kFlagZ | kFlagC | kFlagV)) |
            (value & kFlagN) | (value == 0 ? kFlagZ : 0) |
            (wide > 0xFFFFFFFFULL ? kFlagC : 0) |
            (((~(left ^ right) & (left ^ value)) & kFlagN) != 0
                 ? kFlagV
                 : 0);
    }
    return value;
}

inline uint32_t Oot3dAotLsl(uint32_t value, uint32_t amount) noexcept {
    if (amount == 0) {
        return value;
    }
    return amount < 32 ? value << amount : 0;
}

inline uint32_t Oot3dAotLsr(uint32_t value, uint32_t amount) noexcept {
    amount = amount == 0 ? 32 : amount;
    return amount < 32 ? value >> amount : 0;
}

inline uint32_t Oot3dAotAsr(uint32_t value, uint32_t amount) noexcept {
    amount = amount == 0 ? 32 : amount;
    if (amount >= 32) {
        return (value & 0x80000000U) != 0 ? 0xFFFFFFFFU : 0;
    }
    return (value >> amount) |
           ((value & 0x80000000U) != 0
                ? 0xFFFFFFFFU << (32 - amount)
                : 0);
}

inline uint32_t Oot3dAotRor(uint32_t value, uint32_t amount) noexcept {
    amount &= 31;
    return amount == 0 ? value
                       : (value >> amount) | (value << (32 - amount));
}

inline void Oot3dAotCommitVfp(
    uint32_t& destination,
    uint32_t& fpscr,
    oot3d::recomp::a32::VfpBinary32Result result) noexcept {
    destination = result.value;
    fpscr |= result.exception_flags;
}

} // namespace Oot3dNativeGame
