#include "oot3d_top_screen_visions_runtime.h"

#include <stdexcept>

namespace Oot3dNativeGame {
namespace {

constexpr std::uint32_t kGlViewport = 0x002FEABCU;
constexpr std::uint32_t kPauseUiDraw = 0x0041EC50U;
constexpr std::uint32_t kQueueSixDraw = 0x0041AFACU;
constexpr std::uint32_t kPlaySound = 0x0037547CU;
// The native lower handlers 3.1 keeps a copy of (0x00609BCC) and calls.
constexpr std::uint32_t kLowerTable = 0x00534C94U;
// 3.1 0x005D5D50: the native lower handler of submodes 3 and 4.
constexpr std::uint32_t kLowerBackdropDraw = 0x003243A4U;
constexpr std::uint32_t kHeldButtons = 0x005655A0U;
constexpr std::uint32_t kButtonR = 0x100U;
constexpr std::uint32_t kButtonL = 0x200U;
constexpr std::uint32_t kPageSound = 0x010004BFU;
// 0x005CB848 clears these around its PauseUi_Draw: the Gear, touch, Items,
// Map and System page states and the backdrop skip.
constexpr std::array<std::uint32_t, 6> kPauseWords{
    0x00504484U, 0x0050AF68U, 0x0050672CU,
    0x00506CE8U, 0x0050A530U, 0x005043F4U};

} // namespace

void TopScreenVisionsRuntime::Reset() noexcept {
  mStep = 0U;
  mSubmode = 0U;
  mPreviousButtons = 0U;
  mUpperDrawn = false;
}

std::uint32_t TopScreenVisionsRuntime::Read32(std::uint32_t address) const {
  std::uint32_t value = 0U;
  if (!mMemory.Read32(address, &value))
    throw std::runtime_error("SingleScreen Visions cannot read native state");
  return value;
}

void TopScreenVisionsRuntime::Write32(std::uint32_t address, std::uint32_t value) {
  if (!mMemory.WriteHost<std::uint32_t>(address, value))
    throw std::runtime_error("SingleScreen Visions cannot write native state");
}

bool TopScreenVisionsRuntime::Call(std::uint32_t site, std::uint32_t entry,
                                   const std::array<std::uint32_t, 4> &arguments,
                                   oot3d::recomp::a32::GuestState &state,
                                   oot3d::recomp::a32::ExecutionResult *result,
                                   std::uint32_t *blocksConsumed) {
  for (std::size_t index = 0; index < arguments.size(); ++index)
    state.r[index] = arguments[index];
  state.r[14] = site;
  state.r[15] = entry;
  *result = {oot3d::recomp::a32::ExitKind::Branch, entry,
             oot3d::recomp::a32::FallbackReason::None, site};
  if (blocksConsumed != nullptr) *blocksConsumed = 1U;
  return true;
}

bool TopScreenVisionsRuntime::Execute(std::uint32_t pc,
                                      oot3d::recomp::a32::GuestState &state,
                                      oot3d::recomp::a32::ExecutionResult *result,
                                      std::uint32_t *blocksConsumed) {
  if (pc != kTopScreenVisionsUpperDispatch && pc != kTopScreenVisionsLowerDispatch)
    return false;
  if (mStep == 0U) {
    // 0x005D5698 / 0x005D5C18 replace only submodes 0..2.
    const auto submode = Read32(state.r[0] + 0x104U);
    if (submode >= 3U) return false;
    mCaller = state;
    mSubmode = submode;
  }
  return pc == kTopScreenVisionsUpperDispatch
             ? ExecuteUpper(state, result, blocksConsumed)
             : ExecuteLower(state, result, blocksConsumed);
}

bool TopScreenVisionsRuntime::ExecuteUpper(oot3d::recomp::a32::GuestState &state,
                                           oot3d::recomp::a32::ExecutionResult *result,
                                           std::uint32_t *blocksConsumed) {
  constexpr auto site = kTopScreenVisionsUpperDispatch;
  switch (mStep) {
  case 0U:
    mStep = 1U;
    return Call(site, kGlViewport, {0U, 40U, 480U, 320U}, state, result, blocksConsumed);
  case 1U:
    // 0x005CB848: PauseUi_Draw with the six words cleared, in the
    // registers the viewport call left.
    for (std::size_t index = 0; index < kPauseWords.size(); ++index) {
      mPauseWords[index] = Read32(kPauseWords[index]);
      Write32(kPauseWords[index], 0U);
    }
    mStep = 2U;
    return Call(site, kPauseUiDraw, {state.r[0], state.r[1], state.r[2], state.r[3]},
                state, result, blocksConsumed);
  case 2U:
    for (std::size_t index = 0; index < kPauseWords.size(); ++index)
      Write32(kPauseWords[index], mPauseWords[index]);
    mStep = 3U;
    return Call(site, Read32(kLowerTable + mSubmode * 4U),
                {mCaller.r[0], mCaller.r[1], mCaller.r[2], mCaller.r[3]}, state,
                result, blocksConsumed);
  case 3U:
    mStep = 4U;
    return Call(site, kGlViewport, {0U, 0U, 480U, 400U}, state, result, blocksConsumed);
  default:
    // 0x005D56F4: the edges (0x005D52E8), then a tail call of the queue-6
    // draw that returns to the dispatcher's caller.
    mStep = 0U;
    mUpperDrawn = true;
    state = mCaller;
    state.r[0] = mControllerArgument;
    state.r[15] = kQueueSixDraw;
    *result = {oot3d::recomp::a32::ExitKind::Branch, kQueueSixDraw,
               oot3d::recomp::a32::FallbackReason::None, site};
    if (blocksConsumed != nullptr) *blocksConsumed = 1U;
    return true;
  }
}

std::uint32_t TopScreenVisionsRuntime::PageList() {
  const auto held = Read32(kHeldButtons);
  const auto pressed = held & ~mPreviousButtons;
  mPreviousButtons = held;
  const auto play = mCaller.r[0];
  if (Read32(play + 0x104U) >= 2U || Read32(play + 0x118U) != 0U ||
      Read32(play + 0x11CU) != 0U)
    return 0U;
  const auto top = static_cast<std::int32_t>(Read32(play + 0x130U));
  const auto count = static_cast<std::int32_t>(Read32(play + 0x17CU));
  const auto cursor = static_cast<std::int32_t>(Read32(play + 0x12CU));
  std::int32_t slide = 0;
  if (top > 2 && (pressed & kButtonL) != 0U) {
    Write32(play + 0x134U, static_cast<std::uint32_t>(top - 3));
    Write32(play + 0x12CU, static_cast<std::uint32_t>(cursor - 3));
    slide = 5;
  } else if ((pressed & kButtonR) != 0U && count > top + 3) {
    const auto next = cursor + 3 < count ? cursor + 3 : count - 1;
    Write32(play + 0x134U, static_cast<std::uint32_t>(top + 3));
    Write32(play + 0x12CU, static_cast<std::uint32_t>(next));
    slide = -5;
  } else {
    return 0U;
  }
  Write32(play + 0x118U, static_cast<std::uint32_t>(slide));
  Write32(play + 0x11CU, static_cast<std::uint32_t>(slide));
  return kPageSound;
}

bool TopScreenVisionsRuntime::ExecuteLower(oot3d::recomp::a32::GuestState &state,
                                           oot3d::recomp::a32::ExecutionResult *result,
                                           std::uint32_t *blocksConsumed) {
  constexpr auto site = kTopScreenVisionsLowerDispatch;
  // 0x005D5C18 also draws 3.1's lower backdrop (0x005D5970) over the native
  // one; the lower screen is not presented.
  if (mStep == 0U) {
    mStep = 1U;
    return Call(site, kLowerBackdropDraw,
                {mCaller.r[0], mCaller.r[1], mCaller.r[2], mCaller.r[3]}, state,
                result, blocksConsumed);
  }
  if (mStep == 1U) {
    mStep = 2U;
    if (const auto sound = PageList(); sound != 0U) {
      // Native sound arguments: position null, bank 4, the shared volume,
      // frequency and reverb globals (two of them on the stack).
      state = mCaller;
      state.r[13] = (mCaller.r[13] - 8U) & ~7U;
      Write32(state.r[13], 0x0054AC20U);
      Write32(state.r[13] + 4U, 0x0054AC24U);
      return Call(site, kPlaySound, {sound, 0U, 4U, 0x0054AC20U}, state, result,
                  blocksConsumed);
    }
  }
  mStep = 0U;
  state = mCaller;
  state.r[15] = mCaller.r[14];
  *result = {oot3d::recomp::a32::ExitKind::Branch, mCaller.r[14],
             oot3d::recomp::a32::FallbackReason::None, site};
  if (blocksConsumed != nullptr) *blocksConsumed = 1U;
  return true;
}

} // namespace Oot3dNativeGame
