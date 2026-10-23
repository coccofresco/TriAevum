#pragma once

#include "oot3d_native_a32_memory.h"
#include "oot3d_top_screen_config.h"
#include "oot3d_top_screen_link_ext.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace Oot3dNativeGame {

// SingleScreen 3.0 gameplay options at EUR block entries. Each policy issues
// only native OoT3D calls or register/memory changes the 3.0 payload makes;
// no payload code runs. A disabled option never leaves compiled execution.

// FUN_003c45f4 0x003C49CC `mov r0, #0x1E`, the 30-update wait before an item
// can be put away, then `b 0x003C4B00`. 3.0 (0x005F399C) loads 1 instead.
inline constexpr std::uint32_t kTopScreenQuickPutAwayDelay = 0x003C49CCU;
inline constexpr std::uint32_t kTopScreenQuickPutAwayContinue = 0x003C4B00U;
// FUN_004bba4c 0x004BBE10 `mvn r0, #0; strh r0, [r6, #0x38]; b 0x004BBCFC`,
// the end of a landing. 3.0 (0x005F48EC) first replays the landing as a roll.
inline constexpr std::uint32_t kTopScreenLandingRollSite = 0x004BBE10U;
inline constexpr std::uint32_t kTopScreenLandingRollContinue = 0x004BBCFCU;
// BgYdanSp_FloorWebIdle 0x0019AB08 `ldr r0, [r8, #0xAC]; ldr r1, [r0, #0x284];
// cmp r1, #0x6B; cmpne r1, #0x95; bne 0x0019AD1C`: the Deku Tree floor web
// gives way only under a landing action. With the landing roll, SingleScreen
// 3.1 (0x0019AB18 -> 0x005F4DA0) also lets the roll's action break it.
inline constexpr std::uint32_t kTopScreenLandingRollWebSite = 0x0019AB08U;
inline constexpr std::uint32_t kTopScreenLandingRollWebBreak = 0x0019AB1CU;

// Roll Dash. 3.0 computes the dash state once per frame
// (0x005D8BA8) and the player update consumes it; here it is computed at the
// next Player_Update entry, which sees the same player fields.
inline constexpr std::uint32_t kTopScreenDashStateEntry = 0x001E1B54U; // Player_Update
// FUN_00129474's run continuation `ldr r0, [r7, #0x714]` (3.0 0x005F40AC).
inline constexpr std::uint32_t kTopScreenDashRunSite = 0x0012966CU;
// oot3d_player_action_turn_in_place 0x00251B48 / 0x00251D34 speed loads
// (3.0 0x005F4088 / 0x005F4054) and FUN_002be660's roll animation call
// 0x002BE8B8 (3.0 0x005F4070); hooked at their block entries.
inline constexpr std::uint32_t kTopScreenDashTurnSpeedSite = 0x00251B40U;
inline constexpr std::uint32_t kTopScreenDashMoveSpeedSite = 0x00251D34U;
inline constexpr std::uint32_t kTopScreenDashRollSpeedSite = 0x002BE8B0U;
// Host-reserved guest region for zelda_link_ext.zar, outside every CTR
// mapping: the image ends below 0x00600000 and the heap starts at 0x08000000.
inline constexpr std::uint32_t kTopScreenLinkExtBase = 0x06000000U;
// SingleScreen 3.1 cross-fades Link between the dash and the normal run
// (0x002BE8B8 -> 0x005D11A8): the previous animation's frame is decoded into
// a joint buffer the native animation context reads later in the frame.
// One host-reserved page, mapped on first use; 25 joints of 16 bytes.
inline constexpr std::uint32_t kTopScreenDashBlendBase = 0x06210000U;
inline constexpr std::uint32_t kTopScreenDashBlendSize = 0x1000U;
// ZAR_GetCSABByIndex (r0 archive, r1 index), the leaf 3.0 redirects
// (0x0034807C -> 0x005D10A8). Link's archive table stays native: the archive
// destructor (0x002F7188) frees every table entry through the allocator.
inline constexpr std::uint32_t kTopScreenLinkCsabQuery = 0x0034807CU;

// Aiming Reticle. 3.0 runs 0x005D8BA8's tail at FUN_0047b144's entry (the
// hookshot reticle draw): bow and slingshot first-person aim cast a 6000-unit
// line and drive the native hookshot dot; the dot scale is applied in the
// block 0x0047B814 (3.0 0x0047B83C -> 0x005F3FA4).
inline constexpr std::uint32_t kTopScreenReticleEntry = 0x0047B144U;
inline constexpr std::uint32_t kTopScreenReticleScaleBlock = 0x0047B814U;

class TopScreenGameplayRuntime final {
public:
  // `linkAnimations`: the zelda_link_ext.zar Forge imported from the user's
  // SingleScreen 3.0 archive; empty when it is not installed.
  TopScreenGameplayRuntime(NativeA32Memory &memory, std::filesystem::path linkAnimations = {})
      : mMemory(memory), mLinkExtPath(std::move(linkAnimations)) {}

  void SetConfig(const TopScreenUiConfig &config) noexcept { mConfig = config; }
  // Block entries owned by this runtime (developer and product hook lists).
  static std::span<const std::uint32_t> HookPcs() noexcept;
  // Whether compiled execution must leave at `pc` for an enabled option or a
  // native call in flight.
  [[nodiscard]] bool ShouldObserve(std::uint32_t pc) const noexcept;
  bool Execute(std::uint32_t pc, oot3d::recomp::a32::GuestState &state,
               oot3d::recomp::a32::ExecutionResult *result,
               std::uint32_t *blocksConsumed);
  void Reset() noexcept;

  std::uint64_t QuickPutAways() const noexcept { return mQuickPutAways; }
  std::uint64_t LandingRolls() const noexcept { return mLandingRolls; }
  std::uint64_t LandingRollReturns() const noexcept { return mLandingRollReturns; }
  std::uint64_t LandingRollWebBreaks() const noexcept { return mLandingRollWebBreaks; }
  std::uint64_t DashQueries() const noexcept { return mDashQueries; }
  std::uint64_t DashFrames() const noexcept { return mDashFrames; }
  std::uint64_t LinkExtAnswers() const noexcept { return mLinkExtAnswers; }
  std::uint64_t LinkExtEntryRestores() const noexcept { return mLinkExtEntryRestores; }
  std::uint64_t DashSpeedApplications() const noexcept { return mDashSpeedApplications; }
  std::uint64_t DashBlends() const noexcept { return mDashBlends; }
  std::uint64_t DashBlendPoses() const noexcept { return mDashBlendPoses; }
  std::uint64_t ReticleCasts() const noexcept { return mReticleCasts; }
  std::uint64_t ReticleHits() const noexcept { return mReticleHits; }
  // Empty when the animations are available or not requested.
  const std::string &LinkExtError() const noexcept { return mLinkExtError; }

private:
  bool LandingRoll(oot3d::recomp::a32::GuestState &state,
                   oot3d::recomp::a32::ExecutionResult *result,
                   std::uint32_t *blocksConsumed);
  bool LandingRollWeb(oot3d::recomp::a32::GuestState &state,
                      oot3d::recomp::a32::ExecutionResult *result,
                      std::uint32_t *blocksConsumed);
  bool DashStateEntry(oot3d::recomp::a32::GuestState &state,
                      oot3d::recomp::a32::ExecutionResult *result,
                      std::uint32_t *blocksConsumed);
  bool DashSpeedSite(std::uint32_t pc, oot3d::recomp::a32::GuestState &state,
                     oot3d::recomp::a32::ExecutionResult *result,
                     std::uint32_t *blocksConsumed);
  // FUN_002be660's animation call with 3.1's roll scale and cross-fade.
  bool DashRollSite(oot3d::recomp::a32::GuestState &state,
                    oot3d::recomp::a32::ExecutionResult *result,
                    std::uint32_t *blocksConsumed);
  bool DashRollStep(oot3d::recomp::a32::GuestState &state,
                    oot3d::recomp::a32::ExecutionResult *result,
                    std::uint32_t *blocksConsumed);
  bool EnsureDashBlendRegion();
  bool ReticleEntry(oot3d::recomp::a32::GuestState &state,
                    oot3d::recomp::a32::ExecutionResult *result,
                    std::uint32_t *blocksConsumed);
  // Issues the next MaterialAnimation_Init of the dot, or finishes.
  bool ReticleNextMaterial(oot3d::recomp::a32::GuestState &state,
                           oot3d::recomp::a32::ExecutionResult *result,
                           std::uint32_t *blocksConsumed);
  void ReticleFinish(std::uint32_t play);
  bool ReticleScaleBlock(oot3d::recomp::a32::GuestState &state,
                         oot3d::recomp::a32::ExecutionResult *result,
                         std::uint32_t *blocksConsumed);
  // `eligible`: the 3.0 dash preconditions held, FUN_0037577c(play) included.
  void UpdateDashState(bool eligible);
  bool LinkCsabQuery(oot3d::recomp::a32::GuestState &state,
                     oot3d::recomp::a32::ExecutionResult *result,
                     std::uint32_t *blocksConsumed);
  bool EnsureLinkExtRegion();
  // Earlier builds pointed Link's archive entries at the slots; a state
  // saved by them gets the native entries back.
  void RestoreLinkArchive(std::uint32_t archive);
  std::uint32_t Read32(std::uint32_t address) const;

  NativeA32Memory &mMemory;
  TopScreenUiConfig mConfig{};
  // The landing roll's native call returns to its site with this state.
  bool mLandingPending = false;
  oot3d::recomp::a32::GuestState mLandingCaller{};
  // FUN_0037577c returns to Player_Update's entry with this state.
  bool mDashQueryPending = false;
  oot3d::recomp::a32::GuestState mDashCaller{};
  // 3.0 0x0060FCB8 (rolling or dashing), 0x0060FCB9 (airborne frames),
  // 0x0060FCBB (dash idle word shifted).
  bool mInRoll = false;
  std::uint8_t mAirborneFrames = 0;
  bool mIdleShifted = false;
  // 3.0 0x0060D2C0 (run) and 0x0060D2C4 (roll) speed scales.
  float mRunScale = 1.0F;
  float mRollScale = 1.0F;
  // 3.1 cross-fade (0x0060F4D4..0x0060F4E8, ratio 0x0060D8BC): the last call's frame, archive,
  // animation index and dash answer, the animation faded from, the weight,
  // the last speed and the speed ratio at the switch.
  std::uint32_t mBlendFrame = 0;
  std::uint32_t mBlendArchive = 0;
  std::uint32_t mBlendIndex = 0;
  bool mBlendDash = false;
  bool mBlendFromDash = false;
  float mBlendWeight = 0.0F;
  float mBlendSpeed = 0.0F;
  float mBlendRatio = 1.0F;
  bool mBlendRegionFailed = false;
  // The animation call and the pose fade's native calls return to the site.
  enum class RollStep : std::uint8_t { None, Animate, DecodeEntry, Decode, InterpEntry };
  RollStep mRollStep = RollStep::None;
  oot3d::recomp::a32::GuestState mRollCaller{};
  std::uint32_t mRollSource = 0; // the faded-from animation entry
  std::uint32_t mRollEntry = 0;  // the animation context entry
  // zelda_link_ext.zar image; the region is verified again after a load.
  std::filesystem::path mLinkExtPath;
  bool mLinkExtAttempted = false;
  std::optional<TopScreenLinkExtImage> mLinkExt;
  std::string mLinkExtError;
  bool mRegionReady = false;
  bool mRestorePending = false;
  // This frame's Link archive; the query answers with slots while dashing.
  std::uint32_t mLinkArchive = 0;
  std::uint32_t mLinkAge = 0;
  bool mAnswerDash = false;
  // Aiming Reticle: 3.0 0x0060FCB0 (call counter), 0x0060D2C8 (last counter
  // that reached the aim), 0x0060FCBC (held arrow), 0x0060FCC0 (stable frames),
  // 0x0060FCC2 (last yaw), 0x0060FCC4 (pulse) and 0x0060D2BC (dot scale).
  enum class ReticleStep : std::uint8_t { None, LineTest, DotAnimation, DotMaterial };
  ReticleStep mReticleStep = ReticleStep::None;
  oot3d::recomp::a32::GuestState mReticleCaller{};
  std::uint8_t mReticleCalls = 0;
  std::uint8_t mReticleLastCall = 0;
  std::uint32_t mReticleArrow = 0;
  std::uint8_t mReticleStableFrames = 0;
  std::uint16_t mReticleYaw = 0;
  std::uint8_t mReticlePulse = 0;
  float mReticleScale = 1.0F;
  std::array<float, 3> mReticleOrigin{};
  std::uint32_t mReticleDot = 0;
  std::uint32_t mReticleMaterial = 0; // next of 24 attachment slots
  std::uint64_t mReticleCasts = 0;
  std::uint64_t mReticleHits = 0;
  std::uint64_t mQuickPutAways = 0;
  std::uint64_t mLandingRolls = 0;
  std::uint64_t mLandingRollReturns = 0;
  std::uint64_t mLandingRollWebBreaks = 0;
  std::uint64_t mDashQueries = 0;
  std::uint64_t mDashFrames = 0;
  std::uint64_t mLinkExtAnswers = 0;
  std::uint64_t mLinkExtEntryRestores = 0;
  std::uint64_t mDashSpeedApplications = 0;
  std::uint64_t mDashBlends = 0;
  std::uint64_t mDashBlendPoses = 0;
};

} // namespace Oot3dNativeGame
