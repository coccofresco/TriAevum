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
//
// Roll Dash (OoT3D TopScreen's "Roll dash" row). Semantics: 2Ship
// Enhancements/Player/DashAfterRoll.cpp, mode On: OnPassPlayerInputs keeps
// "dashing" while A is held from a roll (Player_Action_26 with actionVar2 == 0,
// a bonk sets it) into the run (Player_Action_13), ignoring the one Idle frame
// between them; while dashing VB_CONSIDER_BUNNY_HOOD_EQUIPPED is forced in
// Player_Action_13 (2Ship z_player.c:15154, speedTarget *= 1.5), for every form.
// Re-found in EUR/v1.1 by meaning (MAP_6 G7.01d/e), not by address:
//   input pass 0x001F9E90: ldr r3 / ldr r2,=0x007AC22C (Input) / mov r1,r5 /
//     mov r0,r4 (player) / bl Player_UpdateCommon; it follows the A/B clear
//     (bic #0x403) as 2Ship's hook follows the textbox cooldown clear.
//   actionFunc player+0xE14 (Player_SetAction 0x001D32A4 str r8,[r4,#0xE14]);
//   actionVar2 player+0x11E4E (SetAction strh [r7,#0x4E] after strb av1; the
//     roll reads it by pool 0x0020FC1C before its bonk branch).
//   Player_Action_26 (roll) 0x0020F830: the pool of func_80836B3C 0x002201B4.
//   Player_Action_Idle 0x001FC924: func_808369F4's third choice (pool
//     0x001DE0E8, after Player_Action_2 0x001DD828 and Player_Action_3).
//   Player_Action_13 0x00231E00, its Bunny Hood test 0x00231E98:
//     ldrb r0,[r4,#0x20B] (currentMask) ; cmp #4 ; bne ; vmul by 1.5 (pool
//     0x00231FA0 = 0x3FC00000). Dashing, the site continues at 0x00231EA4 (the
//     multiply) exactly as the forced predicate does; otherwise native.
// Input+0x8 is the held buttons; A is bit 0 on the 3DS pad.
#include "../adapter/mm3d_guest_patches.h"
#include <atomic>
#include <cstdint>

namespace Oot3dNativeGame::Mm3dGameplay {

inline constexpr std::uint32_t kQuickPutAwaySite = 0x00218DE8U;
inline constexpr std::uint32_t kQuickPutAwayContinue = 0x00218DF0U;
inline constexpr std::pair<std::uint32_t, std::uint32_t> kQuickPutAwayGuard[] = {
    {0x00218DE0U, 0xE3550013U}, {0x00218DE4U, 0x0A00000BU}, {0x00218DE8U, 0xE3A00014U},
    {0x00218DECU, 0xE5C60E2FU}, {0x00218DF0U, 0xE1A01005U}, {0x00218E18U, 0xE5D60E2FU}};

inline constexpr std::uint32_t kRollDashInputSite = 0x001F9E90U;
inline constexpr std::uint32_t kRollDashSpeedSite = 0x00231E98U;
inline constexpr std::uint32_t kRollDashSpeedApply = 0x00231EA4U;
inline constexpr std::uint32_t kRollDashSpeedNative = 0x00231EB4U;
inline constexpr std::uint32_t kInputHeld = 0x007AC22CU + 0x8U;
inline constexpr std::uint32_t kButtonA = 0x1U;
inline constexpr std::uint32_t kActionFuncOffset = 0xE14U;
inline constexpr std::uint32_t kActionVar2Offset = 0x11E4EU;
inline constexpr std::uint32_t kActionRoll = 0x0020F830U;
inline constexpr std::uint32_t kActionRun = 0x00231E00U;
inline constexpr std::uint32_t kActionIdle = 0x001FC924U;
inline constexpr std::pair<std::uint32_t, std::uint32_t> kRollDashInputGuard[] = {
    {0x001F9C74U, 0xE1A04000U}, {0x001F9E90U, 0xE59F3144U}, {0x001F9E94U, 0xE59F2128U},
    {0x001F9E98U, 0xE1A01005U}, {0x001F9E9CU, 0xE1A00004U}, {0x001F9EA0U, 0xEB002962U},
    {0x001F9FC4U, 0x007AC22CU}, {0x001D32A4U, 0xE5848E14U}, {0x001D344CU, 0xE1C704BEU},
    {0x0020FC1CU, 0x00011E4EU}, {0x002201B4U, 0x0020F830U}, {0x001DE0DCU, 0x001DD828U},
    {0x001DE0E8U, 0x001FC924U}};
inline constexpr std::pair<std::uint32_t, std::uint32_t> kRollDashSpeedGuard[] = {
    {0x00231E00U, 0xE92D40F0U}, {0x00231E08U, 0xE1A04000U}, {0x00231E98U, 0xE5D4020BU},
    {0x00231E9CU, 0xE3500004U}, {0x00231EA0U, 0x1A000003U}, {0x00231EA4U, 0xED9D0A01U},
    {0x00231EACU, 0xEE200A20U}, {0x00231FA0U, 0x3FC00000U}};

// The options a title implements, in the host's gameplay settings store.
struct Options {
  bool QuickPutAway = false;
  bool RollDash = false;
};

class GameplayOptions {
 public:
  void Set(const Options& options) noexcept {
    mQuickPutAway.store(options.QuickPutAway);
    mRollDash.store(options.RollDash);
  }
  [[nodiscard]] Options Get() const noexcept { return {mQuickPutAway.load(), mRollDash.load()}; }
  [[nodiscard]] std::uint64_t QuickPutAways() const noexcept { return mQuickPutAways.load(); }
  [[nodiscard]] bool Dashing() const noexcept { return mDashing.load(); }
  [[nodiscard]] std::uint64_t DashFrames() const noexcept { return mDashFrames.load(); }

  // 2Ship DashAfterRoll's OnPassPlayerInputs, on the guest's own fields.
  void PassPlayerInputs(std::uint32_t player, oot3d::recomp::a32::MemoryBus& memory) {
    if (!mRollDash.load(std::memory_order_relaxed)) {
      mDashing.store(false);
      mPrevAction = 0;
      return;
    }
    std::uint32_t held = 0, action = 0;
    std::uint16_t actionVar2 = 0;
    if (!memory.Read32(kInputHeld, &held) || !memory.Read32(player + kActionFuncOffset, &action) ||
        !memory.Read16(player + kActionVar2Offset, &actionVar2)) {
      mDashing.store(false);
      return;
    }
    const bool rollJustEnded = mPrevAction == kActionRoll && action == kActionIdle;
    bool dashing = mDashing.load(std::memory_order_relaxed);
    if ((held & kButtonA) == 0U) dashing = false;
    else if (action == kActionRoll) dashing = actionVar2 == 0U;
    else if (action != kActionRun && !rollJustEnded) dashing = false;
    mPrevAction = action;
    mDashing.store(dashing);
  }

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

    Mm3dGuestPatchSite input;
    input.Pc = kRollDashInputSite;
    input.Name = "mm3d.roll_dash.input";
    input.Guard.assign(std::begin(kRollDashInputGuard), std::end(kRollDashInputGuard));
    input.Handler = [this](oot3d::recomp::a32::GuestState& state, oot3d::recomp::a32::MemoryBus& memory)
        -> std::optional<std::uint32_t> {
      PassPlayerInputs(state.r[4], memory);
      return std::nullopt;  // observe only: the native input pass runs
    };
    patches.Add(std::move(input));

    Mm3dGuestPatchSite speed;
    speed.Pc = kRollDashSpeedSite;
    speed.Name = "mm3d.roll_dash.speed";
    speed.Guard.assign(std::begin(kRollDashSpeedGuard), std::end(kRollDashSpeedGuard));
    speed.Handler = [this](oot3d::recomp::a32::GuestState&, oot3d::recomp::a32::MemoryBus&)
        -> std::optional<std::uint32_t> {
      if (!mRollDash.load(std::memory_order_relaxed) || !mDashing.load(std::memory_order_relaxed))
        return std::nullopt;
      mDashFrames.fetch_add(1, std::memory_order_relaxed);
      return kRollDashSpeedApply;
    };
    patches.Add(std::move(speed));
    patches.AddTargets({kRollDashSpeedApply, kRollDashSpeedNative});
  }

 private:
  std::atomic<bool> mQuickPutAway{false};
  std::atomic<std::uint64_t> mQuickPutAways{0};
  std::atomic<bool> mRollDash{false};
  std::atomic<bool> mDashing{false};
  std::atomic<std::uint64_t> mDashFrames{0};
  std::uint32_t mPrevAction = 0;  // guest thread only
};

} // namespace Oot3dNativeGame::Mm3dGameplay
