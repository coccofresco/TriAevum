#include "a32_core.h"

#include <atomic>

#include "a32_core_internal.h"

namespace oot3d::recomp::a32 {
namespace {

constexpr std::uint32_t kSystemFormMask = 0x0FFF0FFFU;
constexpr std::uint32_t kMrcTpidrurw = 0x0E1D0F70U;
constexpr std::uint32_t kLegacyDsb = 0x0E070F9AU;
constexpr std::uint32_t kLegacyDmb = 0x0E070FBAU;
constexpr std::uint32_t kMrsApsrMask = 0x0FBF0FFFU;
constexpr std::uint32_t kMrsApsr = 0x010F0000U;
constexpr std::uint32_t kMsrApsrNzcvqMask = 0x0FFFFFF0U;
constexpr std::uint32_t kMsrApsrNzcvq = 0x0128F000U;

ExecutionResult ExecuteObservedSystem(
    std::uint32_t raw,
    std::uint32_t pc,
    GuestState& state) noexcept {
    if ((raw & kMrsApsrMask) == kMrsApsr) {
        const std::uint8_t rd =
            static_cast<std::uint8_t>((raw >> 12U) & 0xFU);
        if (rd == 15U) {
            return {ExitKind::Unsupported, pc, FallbackReason::Core, raw};
        }
        state.r[rd] = state.cpsr;
        state.r[15] = pc + 4U;
        return {ExitKind::Fallthrough, pc + 4U, FallbackReason::None, 0U};
    }
    if ((raw & kMsrApsrNzcvqMask) == kMsrApsrNzcvq) {
        const std::uint8_t rm = static_cast<std::uint8_t>(raw & 0xFU);
        state.cpsr =
            (state.cpsr & 0x00FFFFFFU) | (state.r[rm] & 0xFF000000U);
        state.r[15] = pc + 4U;
        return {ExitKind::Fallthrough, pc + 4U, FallbackReason::None, 0U};
    }

    const std::uint8_t rt = static_cast<std::uint8_t>((raw >> 12U) & 0xFU);
    if ((raw >> 28U) == 0xFU || rt == 15U) {
        return {ExitKind::Unsupported, pc, FallbackReason::Core, raw};
    }

    switch (raw & kSystemFormMask) {
    case kMrcTpidrurw:
        state.r[rt] = state.thread_pointer;
        state.r[15] = pc + 4U;
        return {ExitKind::Fallthrough, pc + 4U, FallbackReason::None, 0U};

    case kLegacyDsb:
    case kLegacyDmb:
        // Guest memory transactions are synchronous.  A sequentially
        // consistent host fence conservatively preserves the ordering
        // promised by both legacy CP15 barrier encodings.
        std::atomic_thread_fence(std::memory_order_seq_cst);
        state.r[15] = pc + 4U;
        return {ExitKind::Fallthrough, pc + 4U, FallbackReason::None, 0U};

    default:
        return {ExitKind::Unsupported, pc, FallbackReason::Core, raw};
    }
}

}  // namespace

ExecutionResult internal::ExecuteCoreSystem(
    std::uint32_t raw,
    std::uint32_t pc,
    GuestState& state) noexcept {
    return ExecuteObservedSystem(raw, pc, state);
}

ExecutionResult ExecuteCore(
    std::uint32_t raw,
    std::uint32_t pc,
    GuestState& state,
    MemoryBus& memory) {
    ExecutionResult result = internal::ExecuteCoreSystem(raw, pc, state);
    if (result.kind != ExitKind::Unsupported) {
        return result;
    }
    result = internal::ExecuteCoreAlu(raw, pc, state, memory);
    if (result.kind != ExitKind::Unsupported) {
        return result;
    }
    return internal::ExecuteCoreMemory(raw, pc, state, memory);
}

}  // namespace oot3d::recomp::a32
