#include "oot3d_top_screen_item_dispatch.h"
#include "oot3d_top_screen_input_cadence.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace {
void Check(bool value, const char* message) {
    if (!value) {
        std::cerr << "TopScreen item dispatch: " << message << '\n';
        std::exit(1);
    }
}
} // namespace

void RunTopScreenItemDispatchTests() {
    using namespace Oot3dNativeGame;
    {
        TopScreenInputCadence clock;
        clock.ObserveGuest({.XHeld = true});
        clock.ObserveGuest({});
        auto snapshot = clock.Advance();
        Check(snapshot.XPressed && !snapshot.XHeld, "short refresh tap lost or made held");
        Check(!clock.Advance().XPressed, "short refresh tap repeated");
        clock.ObserveGuest({.DpadRightHeld = true});
        clock.ObserveGuest({});
        snapshot = clock.Advance();
        Check(snapshot.DpadRightPressed && !snapshot.DpadRightHeld,
              "short song browser tap lost or made held");
        Check(!clock.Advance().DpadRightPressed, "song browser tap repeated");
        for (unsigned skippedRefreshes : {0U, 1U, 2U, 5U}) {
            clock.Reset();
            clock.ObserveGuest({.ZrHeld = true, .ZlHeld = true});
            for (unsigned i = 0; i < skippedRefreshes; ++i)
                clock.ObserveGuest({.ZrHeld = true, .ZlHeld = true});
            snapshot = clock.Advance();
            Check(snapshot.ZrPressed && snapshot.ZlPressed && snapshot.ZrHeld,
                  "refresh phase lost an extended edge");
            Check(!clock.Advance().ZrPressed, "held input repeated edge");
        }
        clock.ObserveGuest({});
        clock.ObserveGuest({.ZrHeld = true});
        clock.Reset();
        Check(!clock.Advance().ZrPressed, "load reset retained edge");
        clock.ObserveGuest({.ZrHeld = true});
        snapshot = clock.Advance();
        clock.ObserveGuest({});
        Check(snapshot.ZrPressed && snapshot.ZrHeld,
              "consumer snapshot changed on a refresh without a native update");
        Check(!clock.Advance().ZrPressed, "release repeated edge");
        clock.ObserveGuest({.ZrHeld = true});
        Check(clock.Advance().ZrPressed, "release/repress lost edge");
        clock.ObserveGuest({.DpadLeftHeld = true, .DpadRightHeld = true,
                            .RestorationLayout = true});
        snapshot = clock.Advance();
        Check(snapshot.RestorationLayout && snapshot.DpadLeftHeld &&
              snapshot.DpadRightHeld && !snapshot.ZrPressed,
              "compatibility/chord fields changed at cadence boundary");
    }
    using oot3d::recomp::a32::GuestState;
    NativeA32Memory memory;
    Check(memory.MapRegion({"ui", 0x00500000U, 0x90000U, true, false, {}}), "map UI");
    constexpr uint32_t global = 0x10000000U;
    Check(memory.MapRegion({"global", global, 0x10000U, true, false, {}}), "map global");
    Check(memory.Write8(0x005879FCU, 0x45U) && memory.Write8(0x005879FDU, 0x46U),
          "seed slots");
    TopScreenItemDispatchRuntime runtime;
    TopScreenExtendedInputFrame input;
    GuestState state{};
    state.r[14] = 0x123400U;
    const auto entries = TopScreenItemDispatchEntries();
    Check(entries.size() == 7 && std::is_sorted(entries.begin(), entries.end()) &&
          std::adjacent_find(entries.begin(), entries.end()) == entries.end(), "registry");
    Check(!IsTopScreenItemDispatchEntry(0x1234U), "unknown entry registered");

    for (const auto pc : entries) {
        const auto before = state;
        const auto generation = memory.WriteGeneration();
        Check(!ShouldObserveTopScreenItemDispatch(pc, memory, state, input, runtime),
              "inactive input exits compiled code");
        Check(!ExecuteTopScreenItemDispatch(pc, memory, state, input, runtime),
              "inactive input replaced native code");
        Check(state.r == before.r && state.vfp == before.vfp && state.cpsr == before.cpsr &&
              memory.WriteGeneration() == generation, "declined hook changed guest state");
    }
    input.ZrPressed = input.ZlPressed = input.ZrHeld = input.ZlHeld = true;
    for (const auto& query : TopScreenVerifiedItemQueryContracts()) {
        Check(IsTopScreenItemDispatchEntry(query.OriginalEntry), "query missing from registry");
        Check(ShouldObserveTopScreenItemDispatch(query.OriginalEntry, memory, state, input, runtime),
              "active query not observed");
        const auto generation = memory.WriteGeneration();
        for (int repeat = 0; repeat < 2; ++repeat) {
            Check(ExecuteTopScreenItemDispatch(query.OriginalEntry, memory, state, input, runtime) &&
                  state.r[0] == 1 && state.r[15] == state.r[14], "query branch/result");
        }
        Check(memory.WriteGeneration() == generation, "query mutated memory");
        Check(memory.Write32(0x0050AF9CU, query.SuppressionMask), "seed suppression");
        Check(ExecuteTopScreenItemDispatch(query.OriginalEntry, memory, state, input, runtime) &&
              state.r[0] == 0, "native suppression ignored");
        Check(memory.Write32(0x0050AF34U + query.NativeFieldOffset, 1), "seed native result");
        Check(ExecuteTopScreenItemDispatch(query.OriginalEntry, memory, state, input, runtime) &&
              state.r[0] == 1, "native result lost under suppression");
        Check(memory.Write32(0x0050AF34U + query.NativeFieldOffset, 0) &&
              memory.Write32(0x0050AF9CU, 0), "reset native result");
    }
    Check(runtime.QueryTrace.empty(), "disabled diagnostic recorded queries");
    runtime.TraceEnabled = true;
    const auto traceEntry = TopScreenVerifiedItemQueryContracts()[0].OriginalEntry;
    for (unsigned i = 0; i < 140; ++i)
        ExecuteTopScreenItemDispatch(traceEntry, memory, state, input, runtime);
    Check(runtime.QueryTrace.size() == 128 && runtime.QueryTrace.front().ReturnPc == state.r[14] &&
          runtime.QueryTrace.front().Result && !runtime.QueryTrace.front().NativeResult,
          "query diagnostic lost caller/result or exceeded its bound");
    runtime.TraceEnabled = false;
    input = {.ZrHeld = true, .ZlHeld = true};
    for (const auto& query : TopScreenVerifiedItemQueryContracts()) {
        Check(ExecuteTopScreenItemDispatch(query.OriginalEntry, memory, state, input, runtime),
              "held query declined");
        const bool held = query.Query == TopScreenItemQuery::ItemIHeld ||
                          query.Query == TopScreenItemQuery::ItemIIHeld;
        Check(state.r[0] == (held ? 1U : 0U), "pressed state leaked into next tick");
    }
    input = {.DpadLeftHeld = true, .DpadRightHeld = true};
    for (uint32_t slot = 2; slot <= 4; ++slot) {
        state.r[0] = global;
        state.r[1] = slot;
        const bool eligible = slot >= 3;
        Check(ShouldObserveTopScreenItemDispatch(kTopScreenSlotItemEntry, memory, state, input, runtime)
                  == eligible, "slot observation mismatch");
        Check(ExecuteTopScreenItemDispatch(kTopScreenSlotItemEntry, memory, state, input, runtime)
                  == eligible, "slot dispatch mismatch");
        Check(state.r[0] == (eligible ? 0x45U + slot - 3U : global), "slot identity");
    }
    state.r[0] = global;
    state.r[1] = 3;
    Check(memory.Write8(global + 0x5C75U, 1), "set special state");
    Check(!ShouldObserveTopScreenItemDispatch(kTopScreenSlotItemEntry, memory, state, input, runtime) &&
          !ExecuteTopScreenItemDispatch(kTopScreenSlotItemEntry, memory, state, input, runtime) &&
          state.r[0] == global, "special state did not stay native");

    for (const bool secondSlot : {false, true}) {
        input = {.ZrPressed = !secondSlot, .ZlPressed = secondSlot,
                 .ZrHeld = !secondSlot, .ZlHeld = secondSlot};
        const auto assignmentPress = input;
        Check(memory.Write32(0x0050672CU, 1), "inactive page");
        state.r[14] = kTopScreenItemsUpdateReturn;
        Check(!ExecuteTopScreenItemDispatch(kTopScreenItemsUpdateEntry, memory, state, input, runtime),
              "assignment allowed during page transition");
        ExecuteTopScreenItemDispatch(kTopScreenItemsUpdateEntry, memory, state, {}, runtime);
        Check(memory.Write32(0x0050672CU, 2) && memory.Write32(0x0050AF18U, 0x20U), "active page");
        Check(ShouldObserveTopScreenItemDispatch(kTopScreenItemsUpdateEntry, memory, state, input, runtime) &&
              ExecuteTopScreenItemDispatch(kTopScreenItemsUpdateEntry, memory, state, input, runtime),
              "assignment begin");
        Check(state.r[15] == 0x002EC3E4U && state.r[14] == kTopScreenItemsUpdateReturn,
              "assignment did not call native Items update");
        Check(!ExecuteTopScreenItemDispatch(kTopScreenItemsUpdateEntry, memory, state, input, runtime),
              "assignment entry retrapped before native update");
        uint32_t value = 0;
        Check(memory.Read32(0x0050AF18U, &value) && value == 0x420U, "assignment touch bit");
        Check(memory.Write32(0x0050674CU, 0xBU), "native selection result");
        // Return processing must not depend on the button still being held.
        input = {};
        Check(ShouldObserveTopScreenItemDispatch(kTopScreenItemsUpdateReturn, memory, state, input, runtime) &&
              ExecuteTopScreenItemDispatch(kTopScreenItemsUpdateReturn, memory, state, input, runtime),
              "assignment continuation lost after release");
        Check(state.r[15] == kTopScreenItemsUpdateReturn && runtime.PendingSelection == 0,
              "assignment continuation branch");
        Check(memory.Read32(0x0050AF18U, &value) && value == 0x20U, "assignment bit cleanup");
        Check(memory.Read32(0x0050674CU, &value) && value == (secondSlot ? 0x17U : 5U), "assignment slot");
        for (uint32_t offset : {0U, 8U, 0x10U}) {
            uint16_t x = 0, y = 0;
            Check(memory.Read16(0x00506700U + offset, &x) &&
                  memory.Read16(0x00506702U + offset, &y) && x == 0x10FU &&
                  y == (secondSlot ? 0xBFU : 7U), "assignment cursor");
        }
        Check(!ShouldObserveTopScreenItemDispatch(kTopScreenItemsUpdateReturn, memory, state, input, runtime),
              "assignment completion repeated");
        Check(!ExecuteTopScreenItemDispatch(kTopScreenItemsUpdateEntry, memory, state,
                  assignmentPress, runtime), "same update repeated assignment");
        Check(!ExecuteTopScreenItemDispatch(kTopScreenItemsUpdateEntry, memory, state,
                  {.ZrHeld = !secondSlot, .ZlHeld = secondSlot}, runtime),
              "held trigger repeated assignment on the next update");
        ExecuteTopScreenItemDispatch(kTopScreenItemsUpdateEntry, memory, state, {}, runtime);
    }
    Check(runtime.SelectionBegins == 2 && runtime.SelectionCompletions == 2, "assignment counts");
    runtime.PendingSelection = 5;
    Check(memory.Write32(0x0050674CU, 0), "native rejected assignment");
    Check(ExecuteTopScreenItemDispatch(kTopScreenItemsUpdateReturn, memory, state, {}, runtime) &&
          runtime.PendingSelection == 0 && runtime.SelectionCompletions == 2,
          "native rejected selection was forced into a slot");

    NativeA32Memory incomplete;
    Check(incomplete.MapRegion({"flags", 0x0050AF18U, 4U, true, false, {}}), "flags region");
    Check(incomplete.MapRegion({"selection", 0x0050674CU, 4U, true, false, {}}), "selection region");
    Check(incomplete.Write32(0x0050AF18U, 0x420U) && incomplete.Write32(0x0050674CU, 0xBU), "incomplete fixture");
    runtime.PendingSelection = 5;
    const auto before = state;
    const auto generation = incomplete.WriteGeneration();
    Check(!ExecuteTopScreenItemDispatch(kTopScreenItemsUpdateReturn, incomplete, state, {}, runtime) &&
          incomplete.WriteGeneration() == generation && state.r == before.r && runtime.PendingSelection == 5,
          "failed cursor validation partially changed native memory");
    for (const auto& query : TopScreenVerifiedItemQueryContracts()) {
        Check(!ShouldObserveTopScreenItemDispatch(query.OriginalEntry, incomplete, state,
                  {.ZrHeld = true}, runtime) &&
              !ExecuteTopScreenItemDispatch(query.OriginalEntry, incomplete, state,
                  {.ZrHeld = true}, runtime), "unmapped query did not decline");
    }
    runtime.PendingSelection = 0; // Same reset as quickload: counters are session diagnostics.
    Check(!ShouldObserveTopScreenItemDispatch(kTopScreenItemsUpdateReturn, memory, state, {}, runtime),
          "loadstate retained pending assignment");
    std::cout << "TopScreen item dispatch contracts passed\n";
}
