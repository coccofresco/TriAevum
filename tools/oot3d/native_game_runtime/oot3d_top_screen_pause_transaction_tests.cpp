#include "oot3d_top_screen_pause_transaction.h"

#include <cstdlib>
#include <iostream>

namespace {
void Require(bool condition, const char *message) {
  if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

constexpr std::uint32_t kItemsState = 0x0050672CU;
constexpr std::uint32_t kControllerState = 0x0050AF68U;
constexpr std::uint32_t kSceneControl = 0x005043E8U;
constexpr std::uint32_t kItemsReset = 0x002EB628U;
constexpr std::uint32_t kSetTouchEnabled = 0x002FD84CU;
constexpr std::uint32_t kSystemMenuOpen = 0x0043ACA8U;

oot3d::recomp::a32::GuestState CallerState() {
  oot3d::recomp::a32::GuestState state;
  state.r[0] = 123;
  state.r[4] = 456;
  state.r[13] = 0x005FF000;
  state.r[14] = 0x00400000;
  state.r[15] = Oot3dNativeGame::kTopScreenInputUpdateBoundary;
  state.vfp[0] = 789;
  state.cpsr = 0xA0000010;
  return state;
}

bool Same(const oot3d::recomp::a32::GuestState &a, const oot3d::recomp::a32::GuestState &b) {
  for (std::size_t index = 0; index < 16U; ++index)
    if (a.r[index] != b.r[index]) return false;
  return a.vfp[0] == b.vfp[0] && a.cpsr == b.cpsr;
}

// One guest call returns to the boundary, as the native callee's `bx lr` does.
bool CallsAndReturns(Oot3dNativeGame::TopScreenPauseTransactionRuntime &runtime,
                     oot3d::recomp::a32::GuestState &state, std::uint32_t entry,
                     std::uint32_t r0, std::uint32_t r1) {
  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocks = 0;
  if (!runtime.Execute(state, &result, &blocks) || !runtime.Pending()) return false;
  const bool ok = state.r[15] == entry && result.pc == entry && state.r[0] == r0 &&
                  state.r[1] == r1 && state.r[13] == 0x005FF000 - 0x20 &&
                  state.r[14] == Oot3dNativeGame::kTopScreenInputUpdateBoundary && blocks == 1;
  state.r[15] = Oot3dNativeGame::kTopScreenInputUpdateBoundary;
  state.r[0] = 0xDEAD;
  return ok;
}

void TestOpenLastPage() {
  using namespace Oot3dNativeGame;
  NativeA32Memory memory;
  std::string error;
  Require(memory.MapRegion({"start-open-test", 0x00300000, 0x00300000, true, false, {}}, &error),
          "cannot map START-open fixture");
  TopScreenPauseTransactionRuntime runtime(memory);
  constexpr std::uint32_t play = 0x00590000U;
  constexpr std::uint32_t transitionPredicate = 0x0035B164U;
  constexpr std::uint32_t pausePredicate = 0x002F43E8U;
  constexpr std::uint32_t playPredicate = 0x00374BE8U;
  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocks = 0U, value = 0U;
  auto state = CallerState();
  const auto seed = [&] {
    runtime.Reset();
    state = CallerState();
    Require(memory.Write32(0x005043E0U, play) && memory.Write8(play + 0x100U, 3U) &&
                memory.Write8(play + 0x101U, 2U) && memory.Write32(kControllerState, 2U) &&
                memory.Write32(0x0050AF9CU, 0U) && memory.Write16(0x00588ED8U, 0U) &&
                memory.Write32(kSceneControl, 7U) && memory.Write32(0x0050AF90U, 19U) &&
                memory.Write32(kItemsState, 0U),
            "cannot seed START-open gates");
  };
  const auto noWrites = [&] {
    return memory.Read32(kSceneControl, &value) && value == 7U &&
           memory.Read32(0x0050AF90U, &value) && value == 19U;
  };
  const auto call = [&](std::uint32_t entry, std::uint32_t returned) {
    Require(CallsAndReturns(runtime, state, entry, entry == playPredicate ? play : 0U,
                            entry == playPredicate ? 3U : 0U),
            "START-open called the wrong guest predicate or ABI");
    Require(noWrites() && memory.Read32(kControllerState, &value) && value == 2U,
            "START-open wrote state before all native gates returned");
    state.r[0] = returned;
    state.vfp[0] = 0xBADU;
    state.cpsr = 0x60000010U;
  };
  const auto rejected = [&] {
    const auto retired = runtime.Retired();
    Require(!runtime.Execute(state, &result, &blocks) && !runtime.NeedsExecution() &&
                Same(state, CallerState()) && noWrites() && runtime.Retired() == retired + 1U,
            "failed START-open gate wrote state, retained work or lost caller registers");
  };
  const auto finish = [&](std::uint32_t pageState) {
    const auto opens = runtime.LastPageOpens();
    Require(!runtime.Execute(state, &result, &blocks) && !runtime.NeedsExecution() &&
                Same(state, CallerState()) && runtime.LastPageOpens() == opens + 1U &&
                memory.Read32(kSceneControl, &value) && value == 1U &&
                memory.Read32(kControllerState, &value) && value == pageState &&
                memory.Read32(0x0050AF90U, &value) && value == 0U,
            "START-open did not complete the ordinary payload writes and restore its caller");
    Require(!runtime.Execute(state, &result, &blocks) && runtime.LastPageOpens() == opens + 1U,
            "one START edge opened twice");
  };
  for (const auto page : {NativeA32TopScreenPage::Items, NativeA32TopScreenPage::Gear,
                          NativeA32TopScreenPage::Map}) {
    seed();
    runtime.RequestOpenLastPage(page);
    Require(runtime.NeedsExecution() && !runtime.Pending(),
            "queued open did not participate in the savestate barrier");
    call(transitionPredicate, 0U);
    call(pausePredicate, 0U);
    call(playPredicate, 0U);
    finish(page == NativeA32TopScreenPage::Items ? 12U :
           page == NativeA32TopScreenPage::Gear ? 16U : 20U);
  }

  // Every mutable gate is read at delivery, not at the host request.
  for (int gate = 0; gate < 5; ++gate) {
    seed();
    runtime.RequestOpenLastPage(NativeA32TopScreenPage::Items);
    switch (gate) {
    case 0: Require(memory.Write32(0x005043E0U, 0U), "clear play"); break;
    case 1: Require(memory.Write8(play + 0x100U, 7U), "change scene type"); break;
    case 2: Require(memory.Write8(play + 0x101U, 1U), "change gameplay state"); break;
    case 3: Require(memory.Write32(kControllerState, 12U), "change touch state"); break;
    case 4: runtime.Reset(); runtime.RequestOpenLastPage(NativeA32TopScreenPage::None); break;
    }
    rejected();
  }
  for (const bool failPlay : {false, true}) {
    seed();
    runtime.RequestOpenLastPage(NativeA32TopScreenPage::Items);
    call(transitionPredicate, 0U);
    call(pausePredicate, failPlay ? 0U : 1U);
    if (failPlay) call(playPredicate, 0x80000000U);
    rejected();
  }

  // Bit 29 has two exceptions; all other bits and signed-halfword wrapping
  // must not accidentally change the condition.
  for (const std::uint16_t magic : {0U, 7U, 8U, 9U, 10U, 0xFFFFU}) {
    for (const bool nativeTransition : {false, true}) {
      seed();
      runtime.RequestOpenLastPage(NativeA32TopScreenPage::Items);
      Require(memory.Write32(0x0050AF9CU, 0xFFFFFFFFU) &&
                  memory.Write16(0x00588ED8U, magic), "seed bit-29 gate");
      call(transitionPredicate, nativeTransition ? 1U : 0U);
      call(pausePredicate, 0U);
      if (magic == 8U || magic == 9U || nativeTransition) {
        call(playPredicate, 0U);
        finish(12U);
      } else {
        rejected();
      }
    }
  }
  seed();
  Require(memory.Write32(0x0050AF9CU, 0xDFFFFFFFU), "seed unrelated mask bits");
  runtime.RequestOpenLastPage(NativeA32TopScreenPage::Items);
  call(transitionPredicate, 0U);
  call(pausePredicate, 0U);
  call(playPredicate, 0U);
  finish(12U);

  // A predicate return may coincide with changed native ownership. No stale
  // play argument or previously accepted suppression state can commit writes.
  for (int gate = 0; gate < 5; ++gate) {
    seed();
    runtime.RequestOpenLastPage(NativeA32TopScreenPage::Items);
    call(transitionPredicate, 0U);
    call(pausePredicate, 0U);
    call(playPredicate, 0U);
    switch (gate) {
    case 0: Require(memory.Write8(play + 0x101U, 1U), "change gameplay in flight"); break;
    case 1: Require(memory.Write32(kControllerState, 16U), "change touch in flight"); break;
    case 2: Require(memory.Write32(0x0050AF9CU, 0x20000000U), "change bit 29 in flight"); break;
    case 3: Require(memory.Write8(play + 0x100U, 7U), "change scene type in flight"); break;
    case 4:
      Require(memory.Write32(0x005043E0U, play + 0x4000U) &&
                  memory.Write8(play + 0x4100U, 3U) && memory.Write8(play + 0x4101U, 2U),
              "replace play in flight");
      break;
    }
    rejected();
  }

  seed();
  runtime.RequestOpenLastPage(NativeA32TopScreenPage::Items);
  for (int refresh = 0; refresh < 8; ++refresh) {
    runtime.AgeUndelivered();
    runtime.RequestOpenLastPage(NativeA32TopScreenPage::Gear);
  }
  Require(runtime.Requested(), "START-open expired too soon");
  runtime.AgeUndelivered();
  Require(!runtime.NeedsExecution() && !runtime.Execute(state, &result, &blocks) && noWrites(),
          "repeated requests renewed an expired START edge");

  for (const bool closeFirst : {false, true}) {
    seed();
    Require(memory.Write32(kItemsState, 2U), "seed close precedence");
    if (closeFirst) runtime.RequestStartClose();
    runtime.RequestOpenLastPage(NativeA32TopScreenPage::Items);
    if (!closeFirst) runtime.RequestStartClose();
    Require(CallsAndReturns(runtime, state, kItemsReset, 0U, 0U) &&
                CallsAndReturns(runtime, state, kSetTouchEnabled, 0U, 1U) &&
                !runtime.Execute(state, &result, &blocks) && Same(state, CallerState()),
            "START-open displaced a queued START close");
  }
  seed();
  runtime.RequestOpenLastPage(NativeA32TopScreenPage::Items);
  call(transitionPredicate, 0U);
  Require(memory.Write32(kItemsState, 2U), "seed newly active close page");
  runtime.RequestStartClose();
  Require(CallsAndReturns(runtime, state, kItemsReset, 0U, 0U) && noWrites() &&
              CallsAndReturns(runtime, state, kSetTouchEnabled, 0U, 1U) &&
              !runtime.Execute(state, &result, &blocks) && Same(state, CallerState()),
          "START close did not preempt an in-flight open at the boundary");

  // State load discards both a queued edge and each predicate continuation.
  for (int calls = 0; calls <= 3; ++calls) {
    seed();
    runtime.RequestOpenLastPage(NativeA32TopScreenPage::Map);
    if (calls >= 1) call(transitionPredicate, 0U);
    if (calls >= 2) call(pausePredicate, 0U);
    if (calls >= 3) call(playPredicate, 0U);
    Require(runtime.NeedsExecution(), "open escaped the savestate continuation barrier");
    Require(memory.Write32(kControllerState, 43U), "simulate loaded state");
    runtime.Reset();
    state = CallerState();
    state.r[0] = 42U;
    const auto loaded = state;
    Require(!runtime.NeedsExecution() && !runtime.Execute(state, &result, &blocks) &&
                Same(state, loaded) && noWrites() &&
                memory.Read32(kControllerState, &value) && value == 43U,
            "state load retained an open or restored old registers/memory");
  }
}
} // namespace

int main() {
  using namespace Oot3dNativeGame;
  TestOpenLastPage();
  NativeA32Memory memory;
  std::string error;
  Require(memory.MapRegion({"pause-transaction-test", 0x00300000, 0x00300000, true, false, {}}, &error),
          "cannot map pause fixture");
  TopScreenPauseTransactionRuntime runtime(memory);
  oot3d::recomp::a32::ExecutionResult result;
  std::uint32_t blocks = 0;

  auto state = CallerState();
  Require(!runtime.Execute(state, &result, &blocks) && Same(state, CallerState()),
          "an idle runtime entered the guest");

  // START on the open Items page: the original reset, the controller and scene
  // writes, then the touch service, and the native update resumes unchanged.
  Require(memory.Write32(kItemsState, 2) && memory.Write32(kControllerState, 0) &&
              memory.Write32(kSceneControl, 7),
          "cannot seed an open Items page");
  runtime.RequestStartClose();
  Require(runtime.NeedsExecution() && !runtime.Pending(), "START close was not queued");
  Require(CallsAndReturns(runtime, state, kItemsReset, 0, 0), "START did not call the Items reset");
  std::uint32_t value = 0;
  Require(memory.Read32(kControllerState, &value) && value == 0,
          "pause state written before the page reset returned");
  Require(CallsAndReturns(runtime, state, kSetTouchEnabled, 0, 1),
          "START did not restore pause touch ownership");
  Require(memory.Read32(kControllerState, &value) && value == 2 &&
              memory.Read32(kSceneControl, &value) && value == 0,
          "START did not write the payload close state");
  Require(!runtime.Execute(state, &result, &blocks) && !runtime.NeedsExecution() &&
              Same(state, CallerState()),
          "the native update did not resume with its own registers");
  Require(runtime.StartCloses() == 1 && runtime.LastClosedPage() == TopScreenPauseClosePage::Items,
          "START close was not recorded");

  // A page that closed before delivery is not reset again.
  Require(memory.Write32(kItemsState, 0), "cannot close the fixture page");
  runtime.RequestStartClose();
  Require(!runtime.Execute(state, &result, &blocks) && !runtime.NeedsExecution() &&
              runtime.Retired() == 1 && Same(state, CallerState()),
          "a START close without a page reached the guest");

  // A request the native update never reaches expires.
  runtime.RequestStartClose();
  for (int refresh = 0; refresh < 8; ++refresh) runtime.AgeUndelivered();
  Require(runtime.Requested(), "a request expired before the native update could run");
  runtime.AgeUndelivered();
  Require(!runtime.Requested() && runtime.Retired() == 2, "an undelivered request survived");

  // B to Save/Quit from a closing Items page, in payload order.
  Require(memory.Write32(kItemsState, 0xE) && memory.Write32(kControllerState, 2),
          "cannot seed a closing Items page");
  runtime.RequestSystemOpen({TopScreenPauseClosePage::Items, kItemsReset});
  Require(CallsAndReturns(runtime, state, kItemsReset, 0, 0), "Save/Quit did not reset Items");
  Require(CallsAndReturns(runtime, state, kSetTouchEnabled, 1, 0),
          "Save/Quit did not hand touch to the system menu");
  Require(memory.Read32(kSceneControl, &value) && value == 1 &&
              memory.Read32(kControllerState, &value) && value == 2,
          "Save/Quit wrote out of payload order");
  Require(CallsAndReturns(runtime, state, kSystemMenuOpen, 0, 0), "Save/Quit did not open the menu");
  Require(memory.Read32(kControllerState, &value) && value == 0,
          "Save/Quit did not clear the pause controller");
  Require(!runtime.Execute(state, &result, &blocks) && Same(state, CallerState()) &&
              runtime.SystemOpens() == 1 &&
              runtime.LastSystemSourcePage() == TopScreenPauseClosePage::Items,
          "Save/Quit did not complete");

  // START wins over Save/Quit requested in the same refresh.
  Require(memory.Write32(kItemsState, 2), "cannot reopen the fixture page");
  runtime.RequestStartClose();
  runtime.RequestSystemOpen({TopScreenPauseClosePage::Items, kItemsReset});
  Require(CallsAndReturns(runtime, state, kItemsReset, 0, 0) &&
              CallsAndReturns(runtime, state, kSetTouchEnabled, 0, 1) &&
              !runtime.Execute(state, &result, &blocks) && runtime.StartCloses() == 2 &&
              runtime.SystemOpens() == 1,
          "Save/Quit replaced a START close");

  // A savestate load discards an in-flight transaction.
  runtime.RequestStartClose();
  Require(CallsAndReturns(runtime, state, kItemsReset, 0, 0), "cannot start a transaction");
  runtime.Reset();
  Require(!runtime.NeedsExecution(), "reset kept an in-flight transaction");
  // The draw transaction is independent of input/reset delivery. Its native
  // call, both viewports, suppression scope and BL continuation are explicit.
  TopScreenSystemPageRuntime system(memory);
  auto draw = CallerState();
  draw.r[15] = kTopScreenSystemPageBoundary;
  Require(!system.Execute(draw, &result, &blocks), "inactive System intercepted the native call");
  Require(memory.Write32(0x0050A530U, 2U) && memory.Write32(0x005043F4U, 9U) &&
              memory.Write32(kControllerState, 7U), "cannot seed System draw");
  system.Prepare(true);
  const auto originalDraw = draw;
  Require(system.Execute(draw, &result, &blocks) && result.pc == 0x004228D4U &&
              draw.r[0] == originalDraw.r[0] && draw.r[13] == originalDraw.r[13] &&
              draw.r[14] == kTopScreenSystemPageBoundary,
          "System did not preserve the original native BL");
  draw.r[15] = kTopScreenSystemPageBoundary;
  draw.r[0] = 0xABCDU;
  draw.vfp[0] = 0x1234U;
  draw.cpsr = 0x60000010U;
  auto resumedDraw = draw;
  resumedDraw.r[14] = resumedDraw.r[15] = kTopScreenSystemPageBoundary + 4U;
  const auto viewport = [&]() {
    return result.pc == 0x002FEABCU && draw.r[0] == 0U && draw.r[1] == 0U &&
           draw.r[2] == 480U && draw.r[3] == 400U &&
           draw.r[13] == originalDraw.r[13] - 0x20U;
  };
  Require(system.Execute(draw, &result, &blocks) && viewport() && !system.Drawing(),
          "System did not install the full viewport before binding its writer");
  Require(memory.Read32(0x005043F4U, &value) && value == 9U,
          "System changed the backdrop flag before its redraw");
  draw.r[15] = kTopScreenSystemPageBoundary;
  Require(system.Execute(draw, &result, &blocks) && result.pc == 0x0041EC50U && system.Drawing(),
          "System did not call the native pause draw in the view pass");
  Require(memory.Read32(0x005043F4U, &value) && value == 1U &&
              memory.Read32(kControllerState, &value) && value == 0U &&
              memory.Read32(0x0050A530U, &value) && value == 2U,
          "System draw has the wrong backdrop/touch/page state");
  draw.r[15] = kTopScreenSystemPageBoundary;
  Require(system.Execute(draw, &result, &blocks) && viewport() && !system.Drawing() &&
              memory.Read32(0x005043F4U, &value) && value == 9U &&
              memory.Read32(kControllerState, &value) && value == 7U,
          "System failed to restore its native draw state");
  draw.r[15] = kTopScreenSystemPageBoundary;
  Require(system.Execute(draw, &result, &blocks) && !system.Pending() &&
              result.pc == kTopScreenSystemPageBoundary + 4U && Same(draw, resumedDraw),
          "System did not resume the original BL result exactly once");
  draw = originalDraw;
  system.Prepare(true);
  for (int call = 0; call < 3; ++call) {
    Require(system.Execute(draw, &result, &blocks), "cannot start cancellable System draw");
    draw.r[15] = kTopScreenSystemPageBoundary;
  }
  system.Cancel();
  Require(!system.Pending() && !system.Redraw() &&
              memory.Read32(0x005043F4U, &value) && value == 9U &&
              memory.Read32(kControllerState, &value) && value == 7U,
          "cancel did not unwind System child overrides");
  system.Prepare(true);
  draw = originalDraw;
  for (int call = 0; call < 3; ++call) {
    Require(system.Execute(draw, &result, &blocks), "cannot start System before a state load");
    draw.r[15] = kTopScreenSystemPageBoundary;
  }
  Require(memory.Write32(0x005043F4U, 42U) && memory.Write32(kControllerState, 43U),
          "cannot simulate loaded guest memory");
  system.Reset();
  Require(!system.Pending() && !system.Redraw() &&
              memory.Read32(0x005043F4U, &value) && value == 42U &&
              memory.Read32(kControllerState, &value) && value == 43U,
          "state load retained System continuation or overwrote loaded guest state");
  std::cout << "pause transaction tests passed\n";
  return 0;
}
