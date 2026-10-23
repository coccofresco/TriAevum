#include "oot3d_top_screen_pause_transaction.h"

#include <stdexcept>

namespace Oot3dNativeGame {
namespace {
// Native pause controller state and scene control written by payload
// 0x005C9940, and the original OoT3D services it calls.
constexpr std::uint32_t kPauseControllerState = 0x0050AF68U;
constexpr std::uint32_t kPauseSceneControl = 0x005043E8U;
constexpr std::uint32_t kPauseTouchCounter = 0x0050AF90U;
constexpr std::uint32_t kPauseSuppression = 0x0050AF9CU;
constexpr std::uint32_t kPlayPointer = 0x005043E0U;
constexpr std::uint32_t kMagicState = 0x00588ED8U;
constexpr std::uint32_t kNativeTransitionPredicate = 0x0035B164U;
constexpr std::uint32_t kPauseOpenPredicate = 0x002F43E8U;
constexpr std::uint32_t kPlayOpenPredicate = 0x00374BE8U;
constexpr std::uint32_t kPauseInputSetTouchEnabled = 0x002FD84CU;
constexpr std::uint32_t kPauseSystemMenuOpen = 0x0043ACA8U;
constexpr std::uint32_t kCallStackReservation = 0x20U;
// Host refreshes a request waits for the native UI update.
constexpr std::uint32_t kUndeliveredRefreshLimit = 8U;

std::uint32_t PageState(const TopScreenPauseDrawInputs &inputs, TopScreenPauseClosePage page) {
  switch (page) {
  case TopScreenPauseClosePage::Gear: return inputs.ChildStates[0];
  case TopScreenPauseClosePage::Items: return inputs.ChildStates[1];
  case TopScreenPauseClosePage::DungeonMap: return inputs.ChildStates[2];
  case TopScreenPauseClosePage::SystemMenu: return inputs.ChildStates[3];
  case TopScreenPauseClosePage::None: break;
  }
  return 0U;
}
} // namespace

void TopScreenPauseTransactionRuntime::RequestStartClose() noexcept {
  mRequest = Request::StartClose;
  mRequestAge = 0;
}

void TopScreenPauseTransactionRuntime::RequestOpenLastPage(
    NativeA32TopScreenPage page) noexcept {
  // Never displace a close, or refresh the age of an earlier edge.
  if (Requested() || Pending()) return;
  mRequest = Request::OpenLastPage;
  mRequestAge = 0;
  mOpenPage = page;
}

void TopScreenPauseTransactionRuntime::RequestSelectSave() noexcept {
  // START and B transactions keep precedence, as in 2.1.1's controller.
  if (Requested() || Pending()) return;
  mRequest = Request::SelectSave;
  mRequestAge = 0;
}

void TopScreenPauseTransactionRuntime::RequestSystemOpen(
    const TopScreenPauseSystemOpenPlan &plan) noexcept {
  // A START close requested in the same refresh wins over Save/Quit.
  if (mRequest == Request::StartClose) return;
  mRequest = Request::SystemOpen;
  mRequestAge = 0;
  mSystemOpenPlan = plan;
}

void TopScreenPauseTransactionRuntime::AgeUndelivered() noexcept {
  if (mRequest == Request::None || ++mRequestAge <= kUndeliveredRefreshLimit) return;
  mRequest = Request::None;
  ++mRetired;
}

void TopScreenPauseTransactionRuntime::Reset() noexcept {
  mRequest = Request::None;
  mRequestAge = 0;
  mSystemOpenPlan = {};
  mOpenPage = NativeA32TopScreenPage::None;
  mOpening = false;
  mSelectSave = false;
  mNativeTransitionActive = false;
  mOpenPlay = 0;
  mStepCount = 0;
  mNextStep = 0;
}

bool TopScreenPauseTransactionRuntime::ReadOpenGates(std::uint32_t *play, bool checkBit29) {
  std::uint32_t controller = 0U, suppression = 0U;
  std::uint16_t magic = 0U;
  std::uint8_t type = 0U, gameplayState = 0U;
  if (!mMemory.Read32(kPlayPointer, play) ||
      !mMemory.Read32(kPauseControllerState, &controller) ||
      !mMemory.Read32(kPauseSuppression, &suppression) ||
      !mMemory.Read16(kMagicState, &magic) ||
      (*play != 0U && (!mMemory.Read8(*play + 0x100U, &type) ||
                      !mMemory.Read8(*play + 0x101U, &gameplayState))))
    throw std::runtime_error("TopScreen START open cannot read native gates");
  // 0x005D51A8..51B8, 0x005D5334..533C, 0x005D5720..572C.
  if (*play == 0U || type != 3U || gameplayState != 2U || controller != 2U)
    return false;
  // 0x005D5A60..5A8C: UXTH(magic - 8) <= 1, or native 0x0035B164 != 0.
  return !checkBit29 || (suppression & 0x20000000U) == 0U ||
         static_cast<std::uint16_t>(magic - 8U) <= 1U || mNativeTransitionActive;
}

bool TopScreenPauseTransactionRuntime::Begin() {
  const auto request = mRequest;
  mRequest = Request::None;
  mOpening = false;
  mSelectSave = false;
  mStepCount = 0;
  mNextStep = 0;
  const auto add = [&](bool call, std::uint32_t address, std::uint32_t value0 = 0U,
                       std::uint32_t value1 = 0U) {
    mSteps[mStepCount++] = {call, address, value0, value1};
  };
  if (request == Request::SelectSave) {
    if (!ReadOpenGates(&mOpenPlay, false)) {
      ++mRetired;
      return false;
    }
    mOpening = true;
    mSelectSave = true;
    mNativeTransitionActive = false;
    // The same three predicate calls as the page open keep its step layout;
    // 2.1.1 tests 0x002F43E8() == 0 and 0x00374BE8(play, 3) == 0.
    add(true, kNativeTransitionPredicate);
    add(true, kPauseOpenPredicate);
    add(true, kPlayOpenPredicate, mOpenPlay, 3U);
    // 0x005D5C44..5C70; the host owns the description word (cleared by the caller).
    add(false, kPauseSceneControl, 1U);
    add(true, kPauseInputSetTouchEnabled, 1U, 1U);
    add(false, kPauseControllerState, 0U);
    add(false, kPauseTouchCounter, 0U);
    add(true, kPauseSystemMenuOpen, 0U);
    return true;
  }
  if (request == Request::OpenLastPage) {
    std::uint32_t pageState = 0U;
    switch (mOpenPage) {
    case NativeA32TopScreenPage::Items: pageState = 12U; break;
    case NativeA32TopScreenPage::Gear: pageState = 16U; break;
    case NativeA32TopScreenPage::Map: pageState = 20U; break;
    default: break;
    }
    if (pageState == 0U || !ReadOpenGates(&mOpenPlay, false)) {
      ++mRetired;
      return false;
    }
    mOpening = true;
    mNativeTransitionActive = false;
    // 0x005D5708..5714 supplies the exception to the later bit-29 gate.
    add(true, kNativeTransitionPredicate);
    add(true, kPauseOpenPredicate);
    add(true, kPlayOpenPredicate, mOpenPlay, 3U);
    // Ordinary 0x005D5B0C..5B2C, without the conditional direct-Items branch.
    add(false, kPauseSceneControl, 1U);
    add(false, kPauseControllerState, pageState);
    add(false, kPauseTouchCounter, 0U);
    return true;
  }
  TopScreenPauseDrawInputs inputs;
  if (!ReadTopScreenPauseDrawInputs(mMemory, &inputs))
    throw std::runtime_error("TopScreen pause transaction cannot read native pause state");
  if (request == Request::StartClose) {
    const auto plan = ResolveTopScreenPauseStartClose(inputs, true);
    if (plan.Page == TopScreenPauseClosePage::None || plan.ResetFunction == 0U) {
      ++mRetired;
      return false;
    }
    add(true, plan.ResetFunction);
    add(false, kPauseControllerState, 2U);
    add(false, kPauseSceneControl, 0U);
    add(true, kPauseInputSetTouchEnabled, 0U, 1U);
    ++mStartCloses;
    mLastClosedPage = plan.Page;
    return true;
  }
  if (request == Request::SystemOpen) {
    const auto plan = mSystemOpenPlan;
    if (plan.Page == TopScreenPauseClosePage::None || plan.ResetFunction == 0U ||
        PageState(inputs, plan.Page) == 0U) {
      ++mRetired;
      return false;
    }
    add(true, plan.ResetFunction);
    add(false, kPauseSceneControl, 1U);
    add(true, kPauseInputSetTouchEnabled, 1U, 0U);
    add(false, kPauseControllerState, 0U);
    add(true, kPauseSystemMenuOpen);
    ++mSystemOpens;
    mLastSystemSourcePage = plan.Page;
    return true;
  }
  return false;
}

bool TopScreenPauseTransactionRuntime::Execute(oot3d::recomp::a32::GuestState &state,
                                               oot3d::recomp::a32::ExecutionResult *result,
                                               std::uint32_t *blocksConsumed) {
  // Only the returns of the three predicate calls are gates; a SELECT Save
  // continues through its own native calls afterwards.
  if (Pending() && mOpening && mNextStep <= 3U) {
    if (mNextStep == 1U) mNativeTransitionActive = state.r[0] != 0U;
    std::uint32_t play = 0U;
    const bool rejected = (mNextStep >= 2U && state.r[0] != 0U) ||
        !ReadOpenGates(&play, mNextStep >= 2U && !mSelectSave) || play != mOpenPlay;
    if (rejected || mRequest == Request::StartClose) {
      // No open writes precede the predicate results. A newer START close
      // can take ownership at this same boundary after restoring its caller.
      state = mCallerState;
      mOpening = false;
      mSelectSave = false;
      mStepCount = 0;
      mNextStep = 0;
      ++mRetired;
    }
  }
  if (!Pending()) {
    if (!Requested() || !Begin()) return false;
    if (state.r[13] < kCallStackReservation ||
        !mMemory.IsWritable(state.r[13] - kCallStackReservation, kCallStackReservation))
      throw std::runtime_error("TopScreen pause transaction has no valid caller stack");
    mCallerState = state;
  }
  while (mNextStep < mStepCount) {
    const auto step = mSteps[mNextStep++];
    if (!step.Call) {
      if (!mMemory.Write32(step.Address, step.Value0))
        throw std::runtime_error("TopScreen pause transaction state write failed");
      continue;
    }
    state = mCallerState;
    state.r[0] = step.Value0;
    state.r[1] = step.Value1;
    state.r[13] -= kCallStackReservation;
    state.r[14] = kTopScreenInputUpdateBoundary;
    state.r[15] = step.Address;
    *result = {oot3d::recomp::a32::ExitKind::Branch, step.Address,
               oot3d::recomp::a32::FallbackReason::None, kTopScreenInputUpdateBoundary};
    if (blocksConsumed != nullptr) *blocksConsumed = 1U;
    return true;
  }
  // Every call returned to the boundary: the native UI update continues.
  state = mCallerState;
  if (mOpening) ++(mSelectSave ? mSelectSaves : mLastPageOpens);
  mOpening = false;
  mSelectSave = false;
  mStepCount = 0;
  mNextStep = 0;
  return false;
}

void TopScreenSystemPageRuntime::Prepare(bool redraw) {
  if (Pending())
    throw std::runtime_error("TopScreen System draw crossed a target boundary");
  mRedraw = redraw;
}

void TopScreenSystemPageRuntime::Reset() noexcept {
  mRedraw = false;
  mRestoreChildren = false;
  mPhase = 0U;
  mChildren = {};
  mResume = {};
}

void TopScreenSystemPageRuntime::Cancel() {
  if (mRestoreChildren && !EndTopScreenPauseChildSuppression(mMemory, mChildren))
    throw std::runtime_error("TopScreen System cannot restore its draw state");
  Reset();
}

bool TopScreenSystemPageRuntime::Execute(oot3d::recomp::a32::GuestState &state,
                                         oot3d::recomp::a32::ExecutionResult *result,
                                         std::uint32_t *blocksConsumed) {
  const auto branch = [&](std::uint32_t entry) {
    state.r[14] = kTopScreenSystemPageBoundary;
    state.r[15] = entry;
    *result = {oot3d::recomp::a32::ExitKind::Branch, entry,
               oot3d::recomp::a32::FallbackReason::None, kTopScreenSystemPageBoundary};
    if (blocksConsumed != nullptr) *blocksConsumed = 1U;
    return true;
  };
  if (!Pending()) {
    if (!mRedraw) return false;
    if (state.r[13] < kCallStackReservation ||
        !mMemory.IsWritable(state.r[13] - kCallStackReservation, kCallStackReservation))
      throw std::runtime_error("TopScreen System has no valid caller stack");
    mPhase = 1U;
    // The original BL, with its original arguments and stack, exactly once.
    return branch(0x004228D4U);
  }
  if (mPhase == 1U) {
    mResume = state;
    mResume.r[14] = kTopScreenSystemPageBoundary + 4U;
    mPhase = 2U;
  } else if (mPhase == 2U) {
    if (!BeginTopScreenPauseChildSuppression(
            mMemory, TopScreenPauseChildSuppressionSet::SystemPageRedraw, &mChildren)) {
      Cancel();
      throw std::runtime_error("TopScreen System cannot prepare its draw state");
    }
    mRestoreChildren = true;
    mPhase = 3U;
    state = mResume;
    state.r[13] -= kCallStackReservation;
    // 0x005D32C0 / 0x005D32C4 are the Mod Options affordance and active
    // flag. Host settings live in F1, so take the ordinary 0x005D3C54 path.
    return branch(0x0041EC50U);
  } else if (mPhase == 3U) {
    if (!EndTopScreenPauseChildSuppression(mMemory, mChildren))
      throw std::runtime_error("TopScreen System cannot restore its draw state");
    mRestoreChildren = false;
    mPhase = 4U;
  } else {
    state = mResume;
    mPhase = 0U;
    const auto handled = branch(kTopScreenSystemPageBoundary + 4U);
    state.r[14] = kTopScreenSystemPageBoundary + 4U;
    return handled;
  }
  // 0x005D32B4 / 0x005D3C68: before the draw and after its writer completes.
  state = mResume;
  state.r[13] -= kCallStackReservation;
  state.r[0] = 0U;
  state.r[1] = 0U;
  state.r[2] = 480U;
  state.r[3] = 400U;
  return branch(0x002FEABCU);
}

} // namespace Oot3dNativeGame
