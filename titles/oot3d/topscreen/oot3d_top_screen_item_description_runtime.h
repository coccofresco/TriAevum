#pragma once

#include "oot3d_native_a32_memory.h"
#include "oot3d_top_screen_mod_profile.h"

#include <array>
#include <cstdint>

namespace Oot3dNativeGame {

// Return of the native page draw 0x0041982C -> PauseUi_Draw (a NOP), where
// the relocated page has just been drawn to the upper screen.
inline constexpr std::uint32_t kTopScreenItemDescriptionBoundary = 0x00419830U;

// TopScreen 2.1.1 SELECT description (payload word 0x005E3D3C, redraw
// 0x005D3344..0x005D3410): owns whether the description is shown and, after
// each page draw to the upper screen, redraws the native message 0x005C1878
// over the page with the payload's native call sequence. Only native OoT3D
// functions run; the payload's two constant matrices are written to guest
// stack scratch for the uniform uploads.
class TopScreenItemDescriptionRuntime final {
public:
  explicit TopScreenItemDescriptionRuntime(NativeA32Memory &memory) : mMemory(memory) {}

  bool Visible() const noexcept { return mVisible; }
  bool &VisibleState() noexcept { return mVisible; }
  void Hide() noexcept { mVisible = false; }
  // A page has been drawn to the upper screen in this native draw: its
  // return may carry the description.
  void ArmPageDraw() noexcept { mArmed = true; }
  void DisarmPageDraw() noexcept { mArmed = false; }
  bool Pending() const noexcept { return mStepCount != 0U; }
  // The draw return must leave the compiled graph: a redraw is in progress,
  // or this page draw may start one.
  bool NeedsExecution() const noexcept { return Pending() || (mArmed && mVisible); }
  bool Execute(oot3d::recomp::a32::GuestState &state,
               oot3d::recomp::a32::ExecutionResult *result,
               std::uint32_t *blocksConsumed);
  void Reset() noexcept;

  std::uint64_t Toggles() const noexcept { return mToggles; }
  void CountToggle() noexcept { ++mToggles; }
  std::uint64_t Redraws() const noexcept { return mRedraws; }

private:
  struct Call {
    std::uint32_t Entry = 0;
    std::uint32_t R0 = 0, R1 = 0, R2 = 0, R3 = 0;
  };
  NativeA32Memory &mMemory;
  bool mVisible = false;
  bool mArmed = false;
  std::array<Call, 7> mCalls{};
  std::size_t mStepCount = 0;
  std::size_t mNextStep = 0;
  oot3d::recomp::a32::GuestState mCallerState{};
  std::uint64_t mToggles = 0;
  std::uint64_t mRedraws = 0;
};

} // namespace Oot3dNativeGame
