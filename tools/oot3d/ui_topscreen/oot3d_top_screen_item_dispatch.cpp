#include "oot3d_top_screen_item_dispatch.h"

#include <algorithm>
#include <vector>

namespace Oot3dNativeGame {
namespace {
constexpr uint32_t kItemsPageState = 0x0050672CU;
constexpr uint32_t kActionFlags = 0x0050AF18U;
constexpr uint32_t kSelectionState = 0x0050674CU;
constexpr uint32_t kCursorBase = 0x00506700U;
constexpr std::array<uint32_t, 3> kCursorOffsets{0U, 8U, 0x10U};

TopScreenItemsSelectionPlan SelectionPlan(
    NativeA32Memory& memory, const TopScreenExtendedInputFrame& input) {
    uint32_t pageState = 0;
    if (!memory.Read32(kItemsPageState, &pageState)) return {};
    return ResolveTopScreenItemsSelectionBegin(
        {input.ZrPressed, input.ZlPressed, pageState});
}
} // namespace

std::span<const uint32_t> TopScreenItemDispatchEntries() {
    static const auto entries = [] {
        std::vector<uint32_t> result;
        for (const auto& query : TopScreenVerifiedItemQueryContracts())
            result.push_back(query.OriginalEntry);
        result.insert(result.end(), {kTopScreenSlotItemEntry,
            kTopScreenItemsUpdateEntry, kTopScreenItemsUpdateReturn});
        std::sort(result.begin(), result.end());
        return result;
    }();
    return entries;
}

bool IsTopScreenItemDispatchEntry(uint32_t pc) {
    const auto entries = TopScreenItemDispatchEntries();
    return std::binary_search(entries.begin(), entries.end(), pc);
}

bool ShouldObserveTopScreenItemDispatch(
    uint32_t pc, NativeA32Memory& memory,
    const oot3d::recomp::a32::GuestState& state,
    const TopScreenExtendedInputFrame& input,
    const TopScreenItemDispatchRuntime& runtime) {
    if (pc == kTopScreenItemsUpdateEntry)
        return state.r[14] == kTopScreenItemsUpdateReturn && runtime.PendingSelection == 0;
    if (pc == kTopScreenItemsUpdateReturn)
        return runtime.PendingSelection != 0;
    if (pc == kTopScreenSlotItemEntry)
        return HasTopScreenSlotItemOverrideInput(input) &&
            ResolveTopScreenSlotItemOverrideGuest(
                memory, state.r[0], static_cast<uint8_t>(state.r[1]), input).has_value();
    return HasTopScreenItemQueryOverrideInput(input) &&
        ResolveTopScreenItemQueryGuest(memory, pc, input).has_value();
}

bool ExecuteTopScreenItemDispatch(
    uint32_t pc, NativeA32Memory& memory,
    oot3d::recomp::a32::GuestState& state,
    const TopScreenExtendedInputFrame& input,
    TopScreenItemDispatchRuntime& runtime) {
    if (pc == kTopScreenItemsUpdateEntry) {
        if (state.r[14] != kTopScreenItemsUpdateReturn || runtime.PendingSelection != 0)
            return false;
        ++runtime.SelectionUpdates;
        // The official assignment wrapper has its own held-to-pressed sampler.
        const uint32_t held = uint32_t(input.ZrHeld) | (uint32_t(input.ZlHeld) << 1);
        const uint32_t presses = uint32_t(input.ZrPressed) | (uint32_t(input.ZlPressed) << 1);
        const auto edges = (held & ~runtime.PreviousSelectionButtons) |
                           (presses & ~runtime.PreviousSelectionPressed);
        runtime.PreviousSelectionButtons = held;
        runtime.PreviousSelectionPressed = presses;
        auto selectionInput = input;
        selectionInput.ZrPressed = (edges & 1U) != 0;
        selectionInput.ZlPressed = (edges & 2U) != 0;
        const auto plan = SelectionPlan(memory, selectionInput);
        uint32_t flags = 0;
        if (!plan.Active || !memory.Read32(kActionFlags, &flags) ||
            !memory.Write32(kActionFlags, flags | 0x400U)) return false;
        runtime.PendingSelection = plan.Selection;
        ++runtime.SelectionBegins;
        state.r[15] = pc;
        return true;
    }
    if (pc == kTopScreenItemsUpdateReturn) {
        uint32_t flags = 0, nativeSelection = 0, selection = 0;
        uint16_t cursorY = 0;
        if (runtime.PendingSelection == 0 ||
            !memory.Read32(kActionFlags, &flags) ||
            !memory.Read32(kSelectionState, &nativeSelection) ||
            !memory.IsWritable(kActionFlags, sizeof(flags))) return false;
        const bool complete = CompleteTopScreenItemsSelection(
            runtime.PendingSelection, nativeSelection, &selection, &cursorY);
        // Validate every destination before changing native state. A declined
        // hook must not hand partially modified memory back to compiled code.
        if (complete) {
            if (!memory.IsWritable(kSelectionState, sizeof(selection))) return false;
            for (const auto offset : kCursorOffsets)
                if (!memory.IsWritable(kCursorBase + offset, 4U)) return false;
        }
        memory.Write32(kActionFlags, flags & ~0x400U);
        if (complete) {
            memory.Write32(kSelectionState, selection);
            for (const auto offset : kCursorOffsets) {
                memory.Write16(kCursorBase + offset, 0x010FU);
                memory.Write16(kCursorBase + offset + 2U, cursorY);
            }
            ++runtime.SelectionCompletions;
        }
        runtime.PendingSelection = 0;
        // Resume the original compiled return block once. pc+4 is an internal
        // NOP, not a valid entry in the precompiled module.
        state.r[15] = pc;
        return true;
    }
    if (pc == kTopScreenSlotItemEntry) {
        ++runtime.SlotEntries;
        if (!HasTopScreenSlotItemOverrideInput(input)) return false;
        ++runtime.SlotAttempts;
        const auto resolved = ResolveTopScreenSlotItemOverrideGuest(
            memory, state.r[0], static_cast<uint8_t>(state.r[1]), input);
        if (!resolved.has_value()) {
            ++runtime.SlotNativeFallbacks;
            return false;
        }
        ++runtime.SlotOverrides;
        state.r[0] = *resolved;
        state.r[15] = state.r[14];
        return true;
    }
    for (const auto& query : TopScreenVerifiedItemQueryContracts()) {
        if (query.OriginalEntry != pc) continue;
        ++runtime.QueryEntries;
        if (!HasTopScreenItemQueryOverrideInput(input)) return false;
        const auto index = static_cast<size_t>(query.Query);
        ++runtime.QueryCalls[index];
        const auto resolved = ResolveTopScreenItemQueryGuest(memory, pc, input);
        if (!resolved.has_value()) return false;
        runtime.QueryTrueResults[index] += *resolved ? 1U : 0U;
        state.r[0] = *resolved ? 1U : 0U;
        state.r[15] = state.r[14];
        return true;
    }
    return false;
}
} // namespace Oot3dNativeGame
