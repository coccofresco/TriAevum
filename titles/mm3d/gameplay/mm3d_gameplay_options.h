#pragma once
// MM3D gameplay options with the same meaning as OoT3D's F1/Esc Gameplay rows
// (titles/oot3d/topscreen/oot3d_top_screen_gameplay_runtime.h). Neither
// mm3d-dekui nor Project Restoration implements these (checked: dekui
// src/mm3d/*.h, Restoration source/rst/*), so this file is their only MM3D
// owner; the value comes from the host's one gameplay settings store.
//
// Quick put away. Semantics: 2Ship Enhancements/Player/InstantPutaway.cpp,
// VB_RESET_PUTAWAY_TIMER in Player_UpdateInterface (z_player.c): when the A
// action is not PUTAWAY the 20-update cooldown is NOT re-armed, so an item can
// be put away at once. Identity: mm3d-decomp v1.0 fn_0x00218644 (doAction
// 0x13 == PUTAWAY, cooldown byte player+0xE2F reset to 0x14, else decremented
// with doAction 0x0A); re-found in EUR/v1.1 by these words, not by address:
//   0x00218DE0 cmp r5,#19 ; 0x00218DE4 beq 0x00218E18 (PUTAWAY: count down)
//   0x00218DE8 mov r0,#20 ; 0x00218DEC strb r0,[r6,#0xE2F] (re-arm cooldown)
//   0x00218DF0 mov r1,r5  (Interface_SetAButtonDoAction argument)
//   0x00218E18 ldrb r0,[r6,#0xE2F] (the cooldown is read on the PUTAWAY path)
// Enabled, the site at 0x00218DE8 continues at 0x00218DF0: the cooldown store
// is skipped exactly as 2Ship skips it. Off, the original instruction runs.
#include "../adapter/mm3d_guest_patches.h"
#include <atomic>
#include <cstdint>

namespace Oot3dNativeGame::Mm3dGameplay {

inline constexpr std::uint32_t kQuickPutAwaySite = 0x00218DE8U;
inline constexpr std::uint32_t kQuickPutAwayContinue = 0x00218DF0U;
inline constexpr std::pair<std::uint32_t, std::uint32_t> kQuickPutAwayGuard[] = {
    {0x00218DE0U, 0xE3550013U}, {0x00218DE4U, 0x0A00000BU}, {0x00218DE8U, 0xE3A00014U},
    {0x00218DECU, 0xE5C60E2FU}, {0x00218DF0U, 0xE1A01005U}, {0x00218E18U, 0xE5D60E2FU}};

// The options a title implements, in the host's gameplay settings store.
struct Options {
  bool QuickPutAway = false;
};

class GameplayOptions {
 public:
  void Set(const Options& options) noexcept { mQuickPutAway.store(options.QuickPutAway); }
  [[nodiscard]] Options Get() const noexcept { return {mQuickPutAway.load()}; }
  [[nodiscard]] std::uint64_t QuickPutAways() const noexcept { return mQuickPutAways.load(); }

  void Register(Mm3dGuestPatches& patches) {
    Mm3dGuestPatchSite site;
    site.Pc = kQuickPutAwaySite;
    site.Name = "mm3d.quick_put_away";
    site.Guard.assign(std::begin(kQuickPutAwayGuard), std::end(kQuickPutAwayGuard));
    site.Handler = [this](oot3d::recomp::a32::GuestState&, oot3d::recomp::a32::MemoryBus&)
        -> std::optional<std::uint32_t> {
      if (!mQuickPutAway.load(std::memory_order_relaxed)) return std::nullopt;
      mQuickPutAways.fetch_add(1, std::memory_order_relaxed);
      return kQuickPutAwayContinue;
    };
    patches.Add(std::move(site));
    patches.AddTargets({kQuickPutAwayContinue});
  }

 private:
  std::atomic<bool> mQuickPutAway{false};
  std::atomic<std::uint64_t> mQuickPutAways{0};
};

} // namespace Oot3dNativeGame::Mm3dGameplay
