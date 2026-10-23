#include "oot3d_top_screen_gameplay_runtime.h"
#include "oot3d_native_trigonometry.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace Oot3dNativeGame {
namespace {
// FUN_00360190 (r0 skeleton, r1 animation, r2 end animation, r3 mode,
// s0 speed, s1 start frame, s2 end frame, s3 morph frames).
constexpr std::uint32_t kAnimationChange = 0x00360190U;
constexpr std::uint32_t kPlayerSkeleton = 0x254U;
constexpr std::uint32_t kPlayerAnimationAge = 0x1B3U;
constexpr std::uint32_t kLandingAnimationTable = 0x198U;
constexpr std::uint32_t kLandingAnimationStack = 0x30U;
constexpr std::uint32_t kLandingTimer = 0x38U;
// The floor web (3.1 0x005F4DA0): its "Link above" byte, Link's action, the
// landing actions it accepts natively and the roll action per age (3.1
// literal 0x005F4DE8, the table the landing roll's animations come from).
constexpr std::uint32_t kPlayStatePlayerFromWeb = 0xACU; // r8 = play + 0x2000
constexpr std::uint32_t kWebLinkAbove = 0x2C9U;
constexpr std::uint32_t kWebLandingAction = 0x6BU;
constexpr std::uint32_t kWebLandingActionHeavy = 0x95U;
constexpr std::uint32_t kLandingRollActionTable = 0x0053A790U;

// 3.0 0x005D8BA8 inputs.
constexpr std::uint32_t kPlayStatePointer = 0x005043D4U + 0x0CU;
constexpr std::uint32_t kSaveContext = 0x00587958U;
constexpr std::uint32_t kSaveLinkAge = kSaveContext + 0x04U;      // 0 adult, 1 child
constexpr std::uint32_t kSaveDashEnabled = kSaveContext + 0x44U;  // s16
constexpr std::uint32_t kPlayStateMode = 0x100U;                  // 3 = gameplay
constexpr std::uint32_t kPlayStateSubMode = 0x101U;               // 2 = running
constexpr std::uint32_t kPlayStateFrameFlags = 0x14U;
constexpr std::uint32_t kPlayStatePlayer = 0x20ACU;
constexpr std::uint32_t kDashBlockedQuery = 0x0037577CU; // FUN_0037577c(play)
constexpr std::uint32_t kPlayerRollAction = 0x284U;
constexpr std::uint32_t kRollActionTable = 0x0014E9F0U * 4U; // indexed by +0x1B3
constexpr std::uint32_t kPlayerStateFlags = 0x1710U;
constexpr std::uint32_t kDashCancelFlags = 0x08A02000U;
constexpr std::uint32_t kPlayerGroundFlags = 0x90U; // bit 0: on ground
constexpr std::uint32_t kPlayerHeavy = 0x1A7U;
constexpr std::uint32_t kPlayerArchive = 0x258U;
constexpr std::uint32_t kLinkArchiveAnimations = 0x246U;
constexpr std::uint32_t kArchiveGroup = 0x20U;
constexpr std::uint32_t kArchiveGroupTable = 0x0CU;
constexpr std::uint32_t kArchiveAnimationTable = 0x50U;
// Entries earlier builds patched, in their mask order: the dash set, then the
// unported N64 landing roll (0x08A, 0x08B, 0x168).
constexpr std::array<std::uint16_t, 13> kLegacyPatchedEntries{
    0x06CU, 0x06DU, 0x09AU, 0x09BU, 0x0C4U, 0x127U, 0x128U,
    0x16DU, 0x175U, 0x176U, 0x08AU, 0x08BU, 0x168U};
constexpr std::uint32_t kDashIdleObject = 0x0051B2F4U;
constexpr std::uint32_t kDashIdleWord = 0x60U;
constexpr std::uint16_t kIdleWordNormal = 0x1B2U;
constexpr std::uint16_t kIdleWordDash = 0x1C9U;
// 3.0 literal pool 0x005D9848..0x005D9860.
constexpr float kRunAdult = 1.425F, kRunChild = 1.35F, kRunHeavy = 1.25F, kRunAirborne = 1.18F;
constexpr float kRollNoAnimation = 1.425F, kRollChild = 1.0F, kRollAdult = 0.8F,
                kRollHeavy = 0.685F;

// Native continuations of the dash sites.
constexpr std::uint32_t kTurnSpeedNear = 0x00251B80U;
constexpr std::uint32_t kTurnSpeedFar = 0x00251B64U;
constexpr std::uint32_t kMoveSpeedReturn = 0x00251D4CU;
constexpr std::uint32_t kMoveSpeedCall = 0x0035FB14U; // Actor_UpdateVelocityXZGravity
constexpr std::uint32_t kRollSpeedReturn = 0x002BE8BCU;
constexpr std::uint32_t kRollSpeedCall = 0x002DD8B0U;
constexpr std::uint32_t kDashRunSkip = 0x00129680U;

// 3.1 dash cross-fade (0x005D11A8, literals 0x005D14EC..0x005D151C).
constexpr std::uint32_t kPlayStateFrames = 0x5BF4U;
constexpr std::uint32_t kPlayStateAnimationContext = 0x3410U;
constexpr std::uint32_t kAnimationContextEntries = 0x3414U;
constexpr std::uint32_t kAnimationContextLimit = 0x28U;
constexpr std::uint32_t kPlayerLimbCount = 0x2C8U;
constexpr std::uint32_t kPlayerJointTable = 0x2CCU;
constexpr std::uint32_t kPlayerFrameTable = 0x27CU;
constexpr std::uint32_t kPlayerAnimationFlags = 0x2D5U;
constexpr std::uint32_t kPlayerRunFrame = 0x2254U;
constexpr std::uint32_t kBlendLimbLimit = 0x19U;
constexpr std::uint32_t kDashFrameRateObject = 0x0051B2F4U; // (s16)+0x110 update rate
constexpr std::uint32_t kDashFrameRate = 0x110U;
constexpr std::uint32_t kAnimationContextAdd = 0x00324154U;   // AnimationContext_AddEntry
constexpr std::uint32_t kAnimationDecodeFrame = 0x002BD9ECU;  // AnimationEntry_DecodeFrame
constexpr std::uint32_t kAnimationInterpolate = 0x002C3814U;  // AnimationEntry_InitInterpAll
constexpr float kBlendRateScale = 0.5F, kBlendStep = 1.0F / 6.0F, kBlendMinSpeed = 1.0e-4F,
                kBlendMinRatio = 0.5F, kBlendMaxRatio = 2.0F, kBlendRunFrames = 1.0F / 29.0F;


// Aiming Reticle (3.0 0x005D8C60..0x005D9844).
constexpr std::uint32_t kPlayStateReticleBusy = 0x2134U;
constexpr std::uint32_t kPlayStateCollision = 0xA98U;
constexpr std::uint32_t kPlayerItemAction = 0x1A9U;       // 8..11 bow, 15 slingshot
constexpr std::uint32_t kPlayerAimBlocked = 0x00800000U;  // +0x1710
// The nocked projectile (heldActor), an En_Arrow (actor id 0x16).
constexpr std::uint32_t kPlayerHeldActor = 0x1224U;
constexpr std::uint32_t kPlayerPitch = 0x48U;
constexpr std::uint32_t kPlayerYaw = 0x4AU;
constexpr std::int16_t kActorEnArrow = 0x16;
constexpr std::uint32_t kArrowPosition = 0x28U;
constexpr std::uint32_t kArrowYaw = 0x36U;
constexpr std::uint32_t kLineTest = 0x00369F9CU;  // BgCheck_EntityLineTest1
constexpr std::uint32_t kZarGetCmab = 0x00372F0CU;
constexpr std::uint32_t kMaterialAnimationInit = 0x00372D94U;
constexpr std::uint32_t kReticleScratch = 0x220U;
constexpr std::uint32_t kReticleHit = 0x2104U;
constexpr std::uint32_t kReticleSize = 0x213CU;
constexpr std::uint32_t kReticleColor = 0x2144U;
constexpr std::uint32_t kReticleDotFlag = 0x2146U;
constexpr std::uint32_t kReticleAlpha = 0x2149U;
constexpr std::uint32_t kReticleMaterials = 0x21ACU; // 12 slots, second set at +0x34
constexpr std::uint32_t kReticleDot = 0x2218U;
constexpr std::uint32_t kReticleDotState = 0x221CU;
constexpr std::uint32_t kReticleDotScale = 0x2224U;
constexpr std::uint32_t kReticleDotAnimation = 0x29U;
// FUN_0047b144 0x0047B814..0x0047B8B8, then `bl 0x003721E0`.
constexpr std::uint32_t kReticleScaleGlobal = 0x0050C8E8U;
constexpr std::uint32_t kReticleScaleCall = 0x003721E0U;
constexpr std::uint32_t kReticleScaleReturn = 0x0047B8BCU;

// Sorted: the dispatcher's hook lists keep their order with binary_search.
constexpr std::array<std::uint32_t, 11> kHookPcs{
    kTopScreenDashRunSite,       kTopScreenLandingRollWebSite, kTopScreenDashStateEntry,
    kTopScreenDashTurnSpeedSite,
    kTopScreenDashMoveSpeedSite, kTopScreenDashRollSpeedSite, kTopScreenLinkCsabQuery,
    kTopScreenQuickPutAwayDelay, kTopScreenReticleEntry,      kTopScreenReticleScaleBlock,
    kTopScreenLandingRollSite};

void Branch(oot3d::recomp::a32::GuestState &state, std::uint32_t pc, std::uint32_t target,
            oot3d::recomp::a32::ExecutionResult *result, std::uint32_t *blocksConsumed) {
  state.r[15] = target;
  *result = {oot3d::recomp::a32::ExitKind::Branch, target,
             oot3d::recomp::a32::FallbackReason::None, pc};
  if (blocksConsumed != nullptr) *blocksConsumed = 1U;
}

float Float(std::uint32_t bits) noexcept { return std::bit_cast<float>(bits); }
std::uint32_t Bits(float value) noexcept { return std::bit_cast<std::uint32_t>(value); }

std::uint32_t SubtractFlags(std::uint32_t left, std::uint32_t right) noexcept {
  const std::uint32_t value = left - right;
  std::uint32_t flags = value & 0x80000000U;
  if (value == 0U) flags |= 0x40000000U;
  if (left >= right) flags |= 0x20000000U;
  if (((left ^ right) & (left ^ value) & 0x80000000U) != 0U) flags |= 0x10000000U;
  return flags;
}

// |sxth(a - b)| as 0x005D8CF8..0x005D8D0C computes it (-0x8000 stays negative).
std::int32_t AngleDistance(std::int32_t a, std::int32_t b) noexcept {
  std::int32_t value = static_cast<std::int16_t>(static_cast<std::uint16_t>(a - b));
  if (value < 0) value = static_cast<std::int16_t>(static_cast<std::uint16_t>(b - a));
  return value;
}
} // namespace

std::span<const std::uint32_t> TopScreenGameplayRuntime::HookPcs() noexcept {
  static_assert(std::is_sorted(kHookPcs.begin(), kHookPcs.end()));
  return kHookPcs;
}

bool TopScreenGameplayRuntime::ShouldObserve(std::uint32_t pc) const noexcept {
  switch (pc) {
  case kTopScreenQuickPutAwayDelay:
    return mConfig.QuickPutAway;
  case kTopScreenLandingRollSite:
    return mConfig.LandingRollFix || mLandingPending;
  case kTopScreenLandingRollWebSite:
    return mConfig.LandingRollFix;
  case kTopScreenDashStateEntry:
    return mConfig.RollDash || mRestorePending ||
           mDashQueryPending || mIdleShifted;
  case kTopScreenDashTurnSpeedSite:
  case kTopScreenDashMoveSpeedSite:
    return mConfig.RollDash && mRunScale != 1.0F;
  case kTopScreenDashRollSpeedSite:
    // The cross-fade watches every call to see the dash answer switch.
    return mConfig.RollDash || mRollStep != RollStep::None;
  case kTopScreenDashRunSite:
    return mConfig.RollDash && mInRoll;
  case kTopScreenLinkCsabQuery:
    return mAnswerDash;
  case kTopScreenReticleEntry:
    return mConfig.AimingReticle || mReticleStep != ReticleStep::None;
  case kTopScreenReticleScaleBlock:
    return mConfig.AimingReticle && mReticleScale != 1.0F;
  default:
    return false;
  }
}

void TopScreenGameplayRuntime::Reset() noexcept {
  mLandingPending = false;
  mLandingCaller = {};
  mDashQueryPending = false;
  mDashCaller = {};
  mInRoll = false;
  mAirborneFrames = 0;
  mRunScale = 1.0F;
  mRollScale = 1.0F;
  mBlendFrame = mBlendArchive = mBlendIndex = 0U;
  mBlendDash = mBlendFromDash = false;
  mBlendWeight = mBlendSpeed = 0.0F;
  mBlendRatio = 1.0F;
  mBlendRegionFailed = false;
  mRollStep = RollStep::None;
  mRollCaller = {};
  mRollSource = mRollEntry = 0U;
  mReticleStep = ReticleStep::None;
  mReticleCaller = {};
  mReticleStableFrames = 0;
  mReticleScale = 1.0F;
  // The loaded memory decides whether the region and Link's archive entries
  // are ours; check both at the next update.
  mRegionReady = false;
  mRestorePending = true;
  mLinkArchive = 0U;
  mAnswerDash = false;
}

std::uint32_t TopScreenGameplayRuntime::Read32(std::uint32_t address) const {
  std::uint32_t value = 0U;
  if (!const_cast<NativeA32Memory &>(mMemory).Read32(address, &value))
    throw std::runtime_error("TopScreen gameplay cannot read guest memory");
  return value;
}

bool TopScreenGameplayRuntime::Execute(std::uint32_t pc, oot3d::recomp::a32::GuestState &state,
                                       oot3d::recomp::a32::ExecutionResult *result,
                                       std::uint32_t *blocksConsumed) {
  // Developer candidate dispatch reaches every hook; disabled ones stay native.
  if (!ShouldObserve(pc)) return false;
  switch (pc) {
  case kTopScreenQuickPutAwayDelay:
    if (!mConfig.QuickPutAway) return false;
    state.r[0] = 1U;
    ++mQuickPutAways;
    Branch(state, pc, kTopScreenQuickPutAwayContinue, result, blocksConsumed);
    return true;
  case kTopScreenLandingRollSite:
    return LandingRoll(state, result, blocksConsumed);
  case kTopScreenLandingRollWebSite:
    return LandingRollWeb(state, result, blocksConsumed);
  case kTopScreenDashStateEntry:
    return DashStateEntry(state, result, blocksConsumed);
  case kTopScreenDashRollSpeedSite:
    return DashRollSite(state, result, blocksConsumed);
  case kTopScreenDashTurnSpeedSite:
  case kTopScreenDashMoveSpeedSite:
  case kTopScreenDashRunSite:
    return DashSpeedSite(pc, state, result, blocksConsumed);
  case kTopScreenLinkCsabQuery:
    return LinkCsabQuery(state, result, blocksConsumed);
  case kTopScreenReticleEntry:
    return ReticleEntry(state, result, blocksConsumed);
  case kTopScreenReticleScaleBlock:
    return ReticleScaleBlock(state, result, blocksConsumed);
  default:
    return false;
  }
}

bool TopScreenGameplayRuntime::LandingRoll(oot3d::recomp::a32::GuestState &state,
                                           oot3d::recomp::a32::ExecutionResult *result,
                                           std::uint32_t *blocksConsumed) {
  if (mLandingPending) {
    // The animation change returned: the landing ends as the native block
    // does, with the caller's registers.
    state = mLandingCaller;
    mLandingPending = false;
    ++mLandingRollReturns;
    state.r[0] = 0xFFFFFFFFU;
    if (!mMemory.Write16(state.r[6] + kLandingTimer, 0xFFFFU))
      throw std::runtime_error("TopScreen landing roll cannot end the landing");
    Branch(state, kTopScreenLandingRollSite, kTopScreenLandingRollContinue, result,
           blocksConsumed);
    return true;
  }
  if (!mConfig.LandingRollFix) return false;
  // 0x005F48FC..0x005F4924: the age's landing animation from the table at
  // r11 + 0x198 ends the animation the caller's frame holds at sp + 0x30.
  std::uint8_t age = 0U;
  std::uint32_t animation = 0U, endAnimation = 0U;
  if (!mMemory.Read8(state.r[4] + kPlayerAnimationAge, &age) ||
      !mMemory.Read32(state.r[13] + kLandingAnimationStack, &animation) ||
      !mMemory.Read32(state.r[11] + age * 4U + kLandingAnimationTable, &endAnimation))
    throw std::runtime_error("TopScreen landing roll cannot read the landing animation");
  mLandingCaller = state;
  mLandingPending = true;
  state.r[0] = state.r[4] + kPlayerSkeleton;
  state.r[1] = animation;
  state.r[2] = endAnimation;
  state.r[3] = 2U;
  state.vfp[0] = Bits(1.0F);
  state.vfp[1] = 0U;
  state.vfp[2] = 0U;
  state.vfp[3] = Bits(8.0F);
  state.r[14] = kTopScreenLandingRollSite;
  ++mLandingRolls;
  Branch(state, kTopScreenLandingRollSite, kAnimationChange, result, blocksConsumed);
  return true;
}

bool TopScreenGameplayRuntime::LandingRollWeb(oot3d::recomp::a32::GuestState &state,
                                              oot3d::recomp::a32::ExecutionResult *result,
                                              std::uint32_t *blocksConsumed) {
  if (!mConfig.LandingRollFix) return false;
  // 3.1 0x005F4DA0: Link is above the web, in the air, and in the roll the
  // landing roll plays for his age. Any other action keeps the native block.
  const auto player = Read32(state.r[8] + kPlayStatePlayerFromWeb);
  const auto action = Read32(player + kPlayerRollAction);
  if (action == kWebLandingAction || action == kWebLandingActionHeavy) return false;
  std::uint8_t linkAbove = 0U, age = 0U;
  std::uint16_t ground = 0U;
  if (!mMemory.Read8(state.r[4] + kWebLinkAbove, &linkAbove) ||
      !mMemory.Read16(player + kPlayerGroundFlags, &ground) ||
      !mMemory.Read8(player + kPlayerAnimationAge, &age))
    throw std::runtime_error("TopScreen landing roll cannot read the floor web");
  if (linkAbove != 1U || (ground & 1U) != 0U ||
      action != Read32(kLandingRollActionTable + age * 4U))
    return false;
  // The native block's registers and flags at its landing continuation.
  state.r[0] = player;
  state.r[1] = action;
  state.cpsr = (state.cpsr & 0x0FFFFFFFU) | SubtractFlags(action, kWebLandingActionHeavy);
  ++mLandingRollWebBreaks;
  Branch(state, kTopScreenLandingRollWebSite, kTopScreenLandingRollWebBreak, result,
         blocksConsumed);
  return true;
}

bool TopScreenGameplayRuntime::DashStateEntry(oot3d::recomp::a32::GuestState &state,
                                              oot3d::recomp::a32::ExecutionResult *result,
                                              std::uint32_t *blocksConsumed) {
  if (mDashQueryPending) {
    // FUN_0037577c returned; Player_Update now runs natively.
    const bool blocked = state.r[0] != 0U;
    state = mDashCaller;
    mDashQueryPending = false;
    ++mDashQueries;
    UpdateDashState(!blocked);
    return false;
  }
  // 0x005D9420..0x005D947C: the cheap preconditions before the native query.
  const std::uint32_t play = Read32(kPlayStatePointer);
  bool eligible = mConfig.RollDash && play != 0U;
  if (eligible) {
    std::uint8_t mode = 0, subMode = 0;
    std::uint16_t dashEnabled = 0;
    eligible = mMemory.Read8(play + kPlayStateMode, &mode) && mode == 3U &&
               mMemory.Read8(play + kPlayStateSubMode, &subMode) && subMode == 2U &&
               Read32(play + kPlayStatePlayer) != 0U &&
               mMemory.Read16(kSaveDashEnabled, &dashEnabled) && dashEnabled != 0U &&
               (Read32(play + kPlayStateFrameFlags) & 1U) != 0U;
  }
  if (!eligible) {
    UpdateDashState(false);
    return false;
  }
  mDashCaller = state;
  mDashQueryPending = true;
  state.r[0] = play;
  state.r[14] = kTopScreenDashStateEntry;
  Branch(state, kTopScreenDashStateEntry, kDashBlockedQuery, result, blocksConsumed);
  return true;
}

void TopScreenGameplayRuntime::UpdateDashState(bool eligible) {
  // 0x005D8BC8..0x005D95C0, without the payload's heap allocation.
  mRunScale = 1.0F;
  mRollScale = 1.0F;
  bool dash = false;
  std::uint32_t archive = 0U, age = 0U;
  const std::uint32_t play = Read32(kPlayStatePointer);
  std::uint32_t player = 0U;
  std::uint8_t mode = 0;
  if (play != 0U && mMemory.Read8(play + kPlayStateMode, &mode) && mode == 3U)
    player = Read32(play + kPlayStatePlayer);
  if (player != 0U) {
    if (mConfig.RollDash) EnsureLinkExtRegion();
    archive = Read32(player + kPlayerArchive);
    const std::uint32_t group = archive != 0U ? Read32(archive + kArchiveGroup) : 0xFFFFFFFFU;
    if (group == 0xFFFFFFFFU ||
        Read32(Read32(archive + kArchiveGroupTable) + group * 0x10U) != kLinkArchiveAnimations)
      archive = 0U;
    else
      age = Read32(kSaveLinkAge) != 0U ? 1U : 0U;
  }

  if (!eligible) {
    mInRoll = false;
  } else {
    std::uint8_t animationAge = 0;
    if (!mMemory.Read8(player + kPlayerAnimationAge, &animationAge))
      throw std::runtime_error("TopScreen roll dash cannot read the player");
    const bool rolling = animationAge < 6U && Read32(player + kPlayerRollAction) ==
                                                  Read32(kRollActionTable + animationAge * 4U);
    if (rolling) mInRoll = true;
    if (mInRoll && (Read32(player + kPlayerStateFlags) & kDashCancelFlags) != 0U) {
      mInRoll = false;
    } else if (mInRoll) {
      std::uint8_t heavy = 0;
      mMemory.Read8(player + kPlayerHeavy, &heavy);
      const bool child = Read32(kSaveLinkAge) != 0U;
      dash = archive != 0U && mRegionReady && mLinkExt.has_value();
      mRollScale = !dash ? kRollNoAnimation : child ? kRollChild
                                            : heavy == 1U ? kRollHeavy
                                                          : kRollAdult;
      if (!rolling) {
        std::uint16_t ground = 0;
        mMemory.Read16(player + kPlayerGroundFlags, &ground);
        bool airborne = false;
        if ((ground & 1U) != 0U) {
          mAirborneFrames = 0U;
        } else if (mAirborneFrames == 0xFFU) {
          airborne = true;
        } else {
          ++mAirborneFrames;
          airborne = mAirborneFrames > 2U;
        }
        mRunScale = airborne ? kRunAirborne : child ? kRunChild
                                            : heavy == 1U ? kRunHeavy
                                                          : kRunAdult;
      }
    }
  }
  if (mInRoll) ++mDashFrames;

  // 0x005D95A0..0x005D95C0: the dash swaps the idle word while it lasts.
  const std::uint32_t idleObject = Read32(kDashIdleObject);
  if (idleObject != 0U) {
    std::uint16_t word = 0;
    mMemory.Read16(idleObject + kDashIdleWord, &word);
    if (mInRoll) {
      if (word == kIdleWordNormal) {
        mMemory.Write16(idleObject + kDashIdleWord, kIdleWordDash);
        mIdleShifted = true;
      }
    } else if (mIdleShifted) {
      if (word == kIdleWordDash) mMemory.Write16(idleObject + kDashIdleWord, kIdleWordNormal);
      mIdleShifted = false;
    }
  }
  if (mRestorePending) RestoreLinkArchive(archive);
  const bool available = archive != 0U && mRegionReady && mLinkExt.has_value();
  mLinkArchive = available ? archive : 0U;
  mLinkAge = age;
  mAnswerDash = available && dash;
}

bool TopScreenGameplayRuntime::EnsureLinkExtRegion() {
  if (!mLinkExtAttempted) {
    mLinkExtAttempted = true;
    if (mLinkExtPath.empty()) {
      mLinkExtError = "zelda_link_ext.zar is not installed";
    } else {
      std::ifstream file(mLinkExtPath, std::ios::binary);
      const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                            std::istreambuf_iterator<char>());
      TopScreenLinkExtImage image;
      if (!file && bytes.empty())
        mLinkExtError = "cannot read " + mLinkExtPath.string();
      else if (BuildTopScreenLinkExtImage(bytes, kTopScreenLinkExtBase, &image, &mLinkExtError))
        mLinkExt = std::move(image);
    }
    if (!mLinkExtError.empty())
      std::cerr << "TopScreen Link animations unavailable: " << mLinkExtError << '\n';
  }
  if (!mLinkExt.has_value()) return false;
  if (mRegionReady) return true;
  const auto &bytes = mLinkExt->Bytes;
  if (!mMemory.IsMapped(kTopScreenLinkExtBase, bytes.size())) {
    if (mMemory.IsMapped(kTopScreenLinkExtBase, 1U) ||
        !mMemory.MapRegion({"topscreen_link_ext", kTopScreenLinkExtBase, bytes.size(), true,
                            false, bytes},
                           &mLinkExtError)) {
      std::cerr << "TopScreen Link animations cannot map their region: " << mLinkExtError
                << '\n';
      mLinkExt.reset();
      return false;
    }
  } else if (Read32(kTopScreenLinkExtBase) != TopScreenLinkExtImage::kMagic ||
             Read32(kTopScreenLinkExtBase + 4U) != mLinkExt->ContentHash) {
    if (!mMemory.WriteBytes(kTopScreenLinkExtBase, bytes))
      throw std::runtime_error("TopScreen Link animations cannot refresh their region");
  }
  mRegionReady = true;
  return true;
}

void TopScreenGameplayRuntime::RestoreLinkArchive(std::uint32_t archive) {
  if (!mMemory.IsMapped(kTopScreenLinkExtBase, TopScreenLinkExtImage::kData) ||
      Read32(kTopScreenLinkExtBase) != TopScreenLinkExtImage::kMagic) {
    mRestorePending = false;
    return;
  }
  const std::uint32_t base = kTopScreenLinkExtBase;
  const std::uint32_t mask = Read32(base + TopScreenLinkExtImage::kPatchedMask);
  if (mask == 0U) {
    mRestorePending = false;
    return;
  }
  if (archive == 0U) return;
  if (Read32(base + TopScreenLinkExtImage::kPatchedArchive) == archive) {
    const std::uint32_t table = Read32(archive + kArchiveAnimationTable);
    for (std::size_t i = 0; i < kLegacyPatchedEntries.size(); ++i) {
      if ((mask & (1U << i)) == 0U) continue;
      const std::uint32_t entry = table + kLegacyPatchedEntries[i] * 4U;
      const std::uint32_t current = Read32(entry);
      if (current >= base + TopScreenLinkExtImage::kSlots &&
          current < base + TopScreenLinkExtImage::kData) {
        mMemory.Write32(entry, Read32(base + TopScreenLinkExtImage::kOriginals + i * 4U));
        ++mLinkExtEntryRestores;
      }
    }
  }
  // A different archive replaced the patched one, entries and all.
  mMemory.Write32(base + TopScreenLinkExtImage::kPatchedMask, 0U);
  mRestorePending = false;
}

bool TopScreenGameplayRuntime::LinkCsabQuery(oot3d::recomp::a32::GuestState &state,
                                             oot3d::recomp::a32::ExecutionResult *result,
                                             std::uint32_t *blocksConsumed) {
  // 0x005D10A8: while dashing, Link's archive answers a dash animation with
  // its slot, which holds the CSAB pointer as a native entry does.
  const std::uint32_t archive = state.r[0];
  const std::uint32_t index = state.r[1];
  if (archive != mLinkArchive) return false;
  std::size_t i = 0;
  while (i < kTopScreenLinkExtAnimations.size() &&
         kTopScreenLinkExtAnimations[i].NativeIndex != index)
    ++i;
  if (i == kTopScreenLinkExtAnimations.size()) return false;
  if (!mAnswerDash || !mLinkExt->Present[mLinkAge][i]) return false;
  // The archive may have died since the update: answer only Link's group.
  const std::uint32_t group = Read32(archive + kArchiveGroup);
  if (group == 0xFFFFFFFFU ||
      Read32(Read32(archive + kArchiveGroupTable) + group * 0x10U) != kLinkArchiveAnimations)
    return false;
  const std::uint32_t slot = TopScreenLinkExtImage::SlotAddress(kTopScreenLinkExtBase, mLinkAge, i);
  state.r[0] = slot;
  state.r[3] = slot;
  ++mLinkExtAnswers;
  Branch(state, kTopScreenLinkCsabQuery, state.r[14], result, blocksConsumed);
  return true;
}

bool TopScreenGameplayRuntime::DashSpeedSite(std::uint32_t pc,
                                             oot3d::recomp::a32::GuestState &state,
                                             oot3d::recomp::a32::ExecutionResult *result,
                                             std::uint32_t *blocksConsumed) {
  if (!mConfig.RollDash) return false;
  switch (pc) {
  case kTopScreenDashTurnSpeedSite: {
    if (mRunScale == 1.0F) return false;
    // ldrsh r9, [r7, #0x20]; ldrh r0, [r4, #0x36]; vldr s23, [r6, #0x21C]
    // (x run scale); sub; sxth; abs; cmp r0, #0x6000; ble 0x00251B80.
    std::uint16_t target = 0, facing = 0;
    if (!mMemory.Read16(state.r[7] + 0x20U, &target) ||
        !mMemory.Read16(state.r[4] + 0x36U, &facing))
      throw std::runtime_error("TopScreen roll dash cannot read the turn");
    state.r[9] = static_cast<std::uint32_t>(static_cast<std::int32_t>(
        std::bit_cast<std::int16_t>(target)));
    state.vfp[23] = Bits(Float(Read32(state.r[6] + 0x21CU)) * mRunScale);
    std::int32_t delta = static_cast<std::int16_t>(
        static_cast<std::uint16_t>(static_cast<std::uint32_t>(facing) - state.r[9]));
    if (delta < 0) delta = -delta;
    state.r[0] = static_cast<std::uint32_t>(delta);
    const std::uint32_t difference = state.r[0] - 0x6000U;
    std::uint32_t flags = 0U;
    if ((difference & 0x80000000U) != 0U) flags |= 0x80000000U;
    if (difference == 0U) flags |= 0x40000000U;
    if (state.r[0] >= 0x6000U) flags |= 0x20000000U;
    state.cpsr = (state.cpsr & 0x0FFFFFFFU) | flags;
    ++mDashSpeedApplications;
    Branch(state, pc, delta <= 0x6000 ? kTurnSpeedNear : kTurnSpeedFar, result,
           blocksConsumed);
    return true;
  }
  case kTopScreenDashMoveSpeedSite: {
    if (mRunScale == 1.0F) return false;
    // vldr s0, [r6, #0x21C] (x run scale); vstr s0, [r4, #0x6C];
    // ldrh r0, [r7, #0x20]; strh r0, [r4, #0x36]; mov r0, r4; bl 0x0035FB14.
    const float speed = Float(Read32(state.r[6] + 0x21CU)) * mRunScale;
    std::uint16_t target = 0;
    if (!mMemory.Write32(state.r[4] + 0x6CU, Bits(speed)) ||
        !mMemory.Read16(state.r[7] + 0x20U, &target) ||
        !mMemory.Write16(state.r[4] + 0x36U, target))
      throw std::runtime_error("TopScreen roll dash cannot move the player");
    state.vfp[0] = Bits(speed);
    state.r[0] = state.r[4];
    state.r[14] = kMoveSpeedReturn;
    ++mDashSpeedApplications;
    Branch(state, pc, kMoveSpeedCall, result, blocksConsumed);
    return true;
  }
  case kTopScreenDashRunSite:
    // The dash keeps the run going: skip the run-end flag (+0x714 0x100000).
    if (!mInRoll) return false;
    Branch(state, pc, kDashRunSkip, result, blocksConsumed);
    return true;
  default:
    return false;
  }
}

bool TopScreenGameplayRuntime::DashRollSite(oot3d::recomp::a32::GuestState &state,
                                            oot3d::recomp::a32::ExecutionResult *result,
                                            std::uint32_t *blocksConsumed) {
  if (mRollStep != RollStep::None) return DashRollStep(state, result, blocksConsumed);
  if (!mConfig.RollDash) return false;
  // The block `mov r1, r9; mov r0, r4; bl 0x002DD8B0`: r4 the player, r9 the
  // PlayState, r8 the animation index, s0 the speed. 3.1 0x005D11A8 scales the
  // speed, then cross-fades when the dash answer for this animation switches.
  const std::uint32_t player = state.r[4];
  const std::uint32_t play = state.r[9];
  const std::uint32_t index = state.r[8];
  float speed = Float(state.vfp[0]) * mRollScale;
  if (mRollScale != 1.0F) ++mDashSpeedApplications;
  const std::uint32_t archive = Read32(player + kPlayerArchive);
  bool fade = false;
  if (Read32(play + kPlayStatePlayer) == player && archive != 0U) {
    const std::uint32_t frame = Read32(play + kPlayStateFrames);
    // 0x005CA8A8: the current group's animation count.
    const std::uint32_t group = Read32(archive + kArchiveGroup);
    const std::uint32_t count = group == 0xFFFFFFFFU
        ? 0U : Read32(Read32(archive + kArchiveGroupTable) + group * 0x10U);
    const std::uint32_t nativeEntry =
        index < count ? Read32(Read32(archive + kArchiveAnimationTable) + index * 4U) : 0U;
    // 0x005D1224..0x005D12B8: with Link's animations ready, this dash
    // animation's slot, and whether the query answers it now.
    std::uint32_t dashEntry = 0U;
    bool dash = false;
    if (archive == mLinkArchive && mLinkArchive != 0U && mLinkExt.has_value() &&
        count == kLinkArchiveAnimations) {
      for (std::size_t i = 0; i < kTopScreenLinkExtAnimations.size(); ++i) {
        if (kTopScreenLinkExtAnimations[i].NativeIndex != index) continue;
        if (mLinkExt->Present[mLinkAge][i]) {
          dashEntry = TopScreenLinkExtImage::SlotAddress(kTopScreenLinkExtBase, mLinkAge, i);
          dash = mAnswerDash;
        }
        break;
      }
    }
    // 0x005D12BC..0x005D1438: the same animation on consecutive frames whose
    // answer switched starts a fade from the other animation.
    if (mBlendFrame + 1U == frame && mBlendArchive == archive && mBlendIndex == index) {
      if (mBlendDash != dash) {
        mBlendWeight = 1.0F - mBlendWeight;
        mBlendFromDash = mBlendDash;
        mBlendRatio = speed <= kBlendMinSpeed
            ? 1.0F : std::clamp(mBlendSpeed / speed, kBlendMinRatio, kBlendMaxRatio);
        ++mDashBlends;
      }
    } else {
      mBlendWeight = 0.0F;
    }
    mBlendArchive = archive;
    mBlendIndex = index;
    mBlendFrame = frame;
    mBlendDash = dash;
    if (mBlendWeight > 0.0F) {
      // 0x005D1318..0x005D1458: one sixth per update at 30 Hz.
      float rate = 1.0F;
      if (const std::uint32_t object = Read32(kDashFrameRateObject); object != 0U) {
        std::uint16_t updateRate = 0U;
        if (!mMemory.Read16(object + kDashFrameRate, &updateRate))
          throw std::runtime_error("TopScreen roll dash cannot read the update rate");
        rate = static_cast<float>(std::bit_cast<std::int16_t>(updateRate)) * kBlendRateScale;
      }
      const float weight = mBlendWeight - rate * kBlendStep;
      if (weight > 0.0F) {
        speed *= 1.0F + (mBlendRatio - 1.0F) * weight;
        mBlendWeight = weight;
      } else {
        mBlendWeight = 0.0F;
      }
    }
    mBlendSpeed = speed;
    fade = mBlendWeight > 0.0F;
    mRollSource = mBlendFromDash ? dashEntry : nativeEntry;
  }
  mRollCaller = state;
  state.r[0] = player;
  state.r[1] = play;
  state.vfp[0] = Bits(speed);
  // While fading, the animation call returns here to fade the pose.
  state.r[14] = fade ? kTopScreenDashRollSpeedSite : kRollSpeedReturn;
  mRollStep = fade ? RollStep::Animate : RollStep::None;
  Branch(state, kTopScreenDashRollSpeedSite, kRollSpeedCall, result, blocksConsumed);
  return true;
}

bool TopScreenGameplayRuntime::DashRollStep(oot3d::recomp::a32::GuestState &state,
                                            oot3d::recomp::a32::ExecutionResult *result,
                                            std::uint32_t *blocksConsumed) {
  const auto &caller = mRollCaller;
  const std::uint32_t player = caller.r[4];
  const std::uint32_t play = caller.r[9];
  const std::uint32_t stack = caller.r[13] - (mRollStep == RollStep::Decode ? 0x20U : 0U);
  if (state.r[13] != stack || state.thread_pointer != caller.thread_pointer)
    throw std::runtime_error("TopScreen roll dash re-entry caller does not match");
  const auto call = [&](RollStep next, std::uint32_t target, std::uint32_t returnPc) {
    mRollStep = next;
    state.r[14] = returnPc;
    Branch(state, kTopScreenDashRollSpeedSite, target, result, blocksConsumed);
    return true;
  };
  std::uint8_t limbs = 0U;
  if (!mMemory.Read8(player + kPlayerLimbCount, &limbs))
    throw std::runtime_error("TopScreen roll dash cannot read Link's skeleton");
  switch (mRollStep) {
  case RollStep::Animate:
    // 0x005D136C..0x005D13C0: fade the pose only from a loaded animation, a
    // skeleton the buffer holds and an animation context with room.
    if (mRollSource == 0U || Read32(mRollSource) == 0U || limbs > kBlendLimbLimit ||
        Read32(play + kAnimationContextEntries) > kAnimationContextLimit ||
        !EnsureDashBlendRegion()) {
      mBlendWeight = 0.0F;
      mRollStep = RollStep::None;
      Branch(state, kTopScreenDashRollSpeedSite, kRollSpeedReturn, result, blocksConsumed);
      return true;
    }
    state.r[0] = play + kPlayStateAnimationContext;
    state.r[1] = player + kPlayerSkeleton;
    return call(RollStep::DecodeEntry, kAnimationContextAdd, kTopScreenDashRollSpeedSite);
  case RollStep::DecodeEntry: {
    // 0x005D145C..0x005D14B8: the faded-from animation's frame at the run's
    // position, decoded into the buffer.
    mRollEntry = state.r[0];
    const std::uint32_t csab = Read32(mRollSource);
    const std::int32_t frames =
        std::bit_cast<std::int32_t>(Read32(csab + Read32(csab + 0x14U) + 0x10U));
    const float frame = (static_cast<float>(frames) + 1.0F) * kBlendRunFrames *
                        Float(Read32(player + kPlayerRunFrame));
    std::uint8_t flags = 0U;
    const std::uint32_t arguments = caller.r[13] - 0x20U;
    if (!mMemory.Read8(player + kPlayerAnimationFlags, &flags) ||
        !mMemory.Write32(arguments, kTopScreenDashBlendBase) ||
        !mMemory.Write32(arguments + 4U, flags) || !mMemory.Write32(arguments + 8U, 0U))
      throw std::runtime_error("TopScreen roll dash cannot pass the frame decode");
    state.r[13] = arguments;
    state.r[0] = mRollEntry;
    state.r[1] = Read32(player + kPlayerFrameTable);
    state.r[2] = mRollSource;
    state.r[3] = limbs;
    state.vfp[0] = Bits(frame);
    return call(RollStep::Decode, kAnimationDecodeFrame, kTopScreenDashRollSpeedSite);
  }
  case RollStep::Decode:
    state.r[13] = caller.r[13];
    state.r[0] = play + kPlayStateAnimationContext;
    state.r[1] = player + kPlayerSkeleton;
    return call(RollStep::InterpEntry, kAnimationContextAdd, kTopScreenDashRollSpeedSite);
  case RollStep::InterpEntry:
    // 0x005D14CC..0x005D14E8: blend Link's joints toward it by the weight;
    // the call returns to the native continuation.
    state.r[1] = limbs;
    state.r[2] = Read32(player + kPlayerJointTable);
    state.r[3] = kTopScreenDashBlendBase;
    state.vfp[0] = Bits(mBlendWeight);
    ++mDashBlendPoses;
    return call(RollStep::None, kAnimationInterpolate, kRollSpeedReturn);
  case RollStep::None:
    break;
  }
  return false;
}

bool TopScreenGameplayRuntime::EnsureDashBlendRegion() {
  if (mMemory.IsWritable(kTopScreenDashBlendBase, kTopScreenDashBlendSize) &&
      !mMemory.IsExecutable(kTopScreenDashBlendBase, kTopScreenDashBlendSize))
    return true;
  if (mBlendRegionFailed) return false;
  std::string error;
  if (mMemory.MapRegion({"topscreen_dash_blend", kTopScreenDashBlendBase, kTopScreenDashBlendSize,
                         true, false, {}},
                        &error))
    return true;
  std::cerr << "TopScreen roll dash cannot map its blend buffer: " << error << '\n';
  mBlendRegionFailed = true;
  return false;
}

bool TopScreenGameplayRuntime::ReticleEntry(oot3d::recomp::a32::GuestState &state,
                                            oot3d::recomp::a32::ExecutionResult *result,
                                            std::uint32_t *blocksConsumed) {
  const std::uint32_t play = Read32(kPlayStatePointer);
  switch (mReticleStep) {
  case ReticleStep::LineTest: {
    // BgCheck_EntityLineTest1 returned; FUN_0047b144 then runs natively.
    const bool hit = state.r[0] != 0U;
    state = mReticleCaller;
    mReticleStep = ReticleStep::None;
    if (!hit) return false;
    ++mReticleHits;
    const std::uint32_t frame = (state.r[13] - kReticleScratch) & ~7U;
    std::array<float, 3> point{};
    for (std::uint32_t axis = 0; axis < 3U; ++axis) {
      const std::uint32_t word = Read32(frame + 0x188U + axis * 4U);
      mMemory.Write32(play + kReticleHit + axis * 4U, word);
      point[axis] = Float(word);
    }
    // 0x005D96E4..0x005D9844: distance x (1/700) sizes the dot.
    const float dx = point[0] - mReticleOrigin[0];
    const float dy = point[1] - mReticleOrigin[1];
    const float dz = point[2] - mReticleOrigin[2];
    float sum = dy * dy;
    const float xx = dx * dx;
    sum = sum + xx;
    const float zz = dz * dz;
    sum = sum + zz;
    const float distance = std::sqrt(sum) * Float(0x3ABB3EE7U);
    float dotScale = 0.25F;
    mReticleScale = 0.25F;
    if (!(distance < 0.25F)) {
      dotScale = !(distance > 3.0F) ? distance : 3.0F;
      mReticleScale = !(distance > 1.6F) ? distance : Float(0x3FCCCCCDU);
    }
    mMemory.Write32(play + kReticleDotScale, Bits(dotScale));
    const std::uint32_t dot = Read32(play + kReticleDot);
    if (dot == 0U || Read32(play + kReticleDotState) == 2U) {
      ReticleFinish(play);
      return false;
    }
    mReticleCaller = state;
    mReticleStep = ReticleStep::DotAnimation;
    state.r[0] = dot;
    state.r[1] = kReticleDotAnimation;
    state.r[14] = kTopScreenReticleEntry;
    Branch(state, kTopScreenReticleEntry, kZarGetCmab, result, blocksConsumed);
    return true;
  }
  case ReticleStep::DotAnimation: {
    // ZAR_GetCMABByIndex(dot, 0x29): the hookshot dot's colour animation.
    const std::uint32_t handle = state.r[0];
    state = mReticleCaller;
    mReticleStep = ReticleStep::None;
    const std::uint32_t animation = handle != 0U ? Read32(handle) : 0U;
    if (animation == 0U) {
      ReticleFinish(play);
      return false;
    }
    for (const std::uint32_t row : {0x6CU, 0x7CU, 0x8CU}) {
      mMemory.Write32(animation + row, Bits(1.0F));
      mMemory.Write32(animation + row + 0x40U, 0x3D4CCCCDU);
      mMemory.Write32(animation + row + 0x80U, 0x3D4CCCCDU);
    }
    mReticleDot = handle;
    mReticleMaterial = 0U;
    return ReticleNextMaterial(state, result, blocksConsumed);
  }
  case ReticleStep::DotMaterial:
    state = mReticleCaller;
    mReticleStep = ReticleStep::None;
    return ReticleNextMaterial(state, result, blocksConsumed);
  case ReticleStep::None:
    break;
  }

  // 0x005D8BC8: the scale is 1 unless this frame's aim sets it.
  mReticleScale = 1.0F;
  ++mReticleCalls;
  if (!mConfig.AimingReticle || play == 0U) return false;
  std::uint8_t mode = 0, subMode = 0, itemAction = 0;
  if (!mMemory.Read8(play + kPlayStateMode, &mode) || mode != 3U ||
      !mMemory.Read8(play + kPlayStateSubMode, &subMode) || subMode != 2U ||
      Read32(play + kPlayStateReticleBusy) != 0U)
    return false;
  const std::uint32_t player = Read32(play + kPlayStatePlayer);
  if (player == 0U || !mMemory.Read8(player + kPlayerItemAction, &itemAction) ||
      (itemAction != 0xFU && static_cast<std::uint8_t>(itemAction - 8U) > 3U) ||
      (Read32(player + kPlayerStateFlags) & kPlayerAimBlocked) != 0U)
    return false;
  const std::uint32_t arrow = Read32(player + kPlayerHeldActor);
  std::uint16_t actorId = 0, arrowYaw = 0, playerYaw = 0, playerPitch = 0;
  if (arrow == 0U || !mMemory.Read16(arrow, &actorId) ||
      static_cast<std::int16_t>(actorId) != kActorEnArrow ||
      !mMemory.Read16(player + kPlayerYaw, &playerYaw) ||
      !mMemory.Read16(arrow + kArrowYaw, &arrowYaw) ||
      !mMemory.Read16(player + kPlayerPitch, &playerPitch))
    return false;
  std::int32_t yaw = static_cast<std::int16_t>(arrowYaw);
  if (AngleDistance(yaw, static_cast<std::int16_t>(playerYaw)) >= 0x1800)
    yaw = static_cast<std::int16_t>(playerYaw);
  // 0x005D8D14..0x005D95FC: four steady aim frames before the first cast.
  if (mReticleArrow == arrow &&
      mReticleLastCall == static_cast<std::uint8_t>(mReticleCalls - 1U)) {
    if (AngleDistance(yaw, static_cast<std::int16_t>(mReticleYaw)) >= 0x300)
      mReticleStableFrames = 0U;
    else if (mReticleStableFrames <= 0xF9U)
      ++mReticleStableFrames;
  } else {
    mReticleArrow = arrow;
    mReticleStableFrames = 0U;
  }
  mReticleYaw = static_cast<std::uint16_t>(yaw);
  mReticleLastCall = mReticleCalls;
  if (mReticleStableFrames <= 3U) return false;

  // 0x005D960C..0x005D96C0: a 6000-unit line from the eye along the aim.
  const std::uint32_t frame = (state.r[13] - kReticleScratch) & ~7U;
  if (state.r[13] < kReticleScratch || !mMemory.IsWritable(frame, kReticleScratch))
    throw std::runtime_error("TopScreen aiming reticle has no caller stack");
  const std::int32_t pitch = static_cast<std::int16_t>(
      static_cast<std::uint16_t>(static_cast<std::int16_t>(playerPitch) - 0x82));
  const float horizontal = NativeTrigonometry(mMemory, pitch, true);
  for (std::uint32_t axis = 0; axis < 3U; ++axis)
    mReticleOrigin[axis] = Float(Read32(arrow + kArrowPosition + axis * 4U));
  constexpr float kReach = 6000.0F;
  std::array<float, 3> end{};
  float along = NativeTrigonometry(mMemory, yaw, false) * horizontal;
  float offset = along * kReach;
  end[0] = mReticleOrigin[0] + offset;
  offset = NativeTrigonometry(mMemory, pitch, false) * kReach;
  end[1] = mReticleOrigin[1] - offset;
  along = NativeTrigonometry(mMemory, yaw, true) * horizontal;
  offset = along * kReach;
  end[2] = mReticleOrigin[2] + offset;
  const std::array<std::uint32_t, 6> arguments{frame + 0x44U, 1U, 1U, 1U, 0U, frame + 0x48U};
  for (std::uint32_t index = 0; index < arguments.size(); ++index)
    mMemory.Write32(frame + index * 4U, arguments[index]);
  for (std::uint32_t axis = 0; axis < 3U; ++axis) {
    mMemory.Write32(frame + 0x88U + axis * 4U, Bits(mReticleOrigin[axis]));
    mMemory.Write32(frame + 0x108U + axis * 4U, Bits(end[axis]));
  }
  mReticleCaller = state;
  mReticleStep = ReticleStep::LineTest;
  ++mReticleCasts;
  state.r[0] = play + kPlayStateCollision;
  state.r[1] = frame + 0x88U;
  state.r[2] = frame + 0x108U;
  state.r[3] = frame + 0x188U;
  state.r[13] = frame;
  state.r[14] = kTopScreenReticleEntry;
  Branch(state, kTopScreenReticleEntry, kLineTest, result, blocksConsumed);
  return true;
}

bool TopScreenGameplayRuntime::ReticleNextMaterial(oot3d::recomp::a32::GuestState &state,
                                                   oot3d::recomp::a32::ExecutionResult *result,
                                                   std::uint32_t *blocksConsumed) {
  // 0x005D97A8..0x005D97E8: attach the animation to both slot sets.
  const std::uint32_t play = Read32(kPlayStatePointer);
  while (mReticleMaterial < 24U) {
    const std::uint32_t index = mReticleMaterial++;
    const std::uint32_t slot =
        play + kReticleMaterials + (index / 2U) * 4U + ((index & 1U) != 0U ? 0x34U : 0U);
    const std::uint32_t material = Read32(slot);
    if (material == 0U) continue;
    mReticleCaller = state;
    mReticleStep = ReticleStep::DotMaterial;
    state.r[0] = Read32(material + 0xCU);
    state.r[1] = mReticleDot;
    state.r[14] = kTopScreenReticleEntry;
    Branch(state, kTopScreenReticleEntry, kMaterialAnimationInit, result, blocksConsumed);
    return true;
  }
  mMemory.Write8(play + kReticleDotFlag, 2U);
  mMemory.Write32(play + kReticleDotState, 2U);
  ReticleFinish(play);
  return false;
}

void TopScreenGameplayRuntime::ReticleFinish(std::uint32_t play) {
  // 0x005D97EC..0x005D9810: dot size 90, colour 0x177, pulsing alpha.
  mMemory.Write32(play + kReticleSize, 0x42F00000U);
  mMemory.Write16(play + kReticleColor, 0x177U);
  mReticlePulse = static_cast<std::uint8_t>(mReticlePulse + 2U);
  mMemory.Write8(play + kReticleAlpha, static_cast<std::uint8_t>(mReticlePulse & 0x7FU));
}

bool TopScreenGameplayRuntime::ReticleScaleBlock(oot3d::recomp::a32::GuestState &state,
                                                 oot3d::recomp::a32::ExecutionResult *result,
                                                 std::uint32_t *blocksConsumed) {
  if (!mConfig.AimingReticle || mReticleScale == 1.0F) return false;
  // FUN_0047b144 0x0047B814..0x0047B8B8 with 3.0's `x 0x0060D2BC` after
  // 0x0047B83C; the block's call to 0x003721E0 returns to a block start.
  float s0 = Float(Read32(state.r[6] + 0x12CU)) * Float(Read32(state.r[4] + 0x50U));
  state.r[7] = state.r[6] + state.r[5] * 4U;
  state.r[1] = state.r[13] + 0x90U;
  state.r[0] = Bits(s0);
  const std::uint32_t flags = SubtractFlags(state.r[0], 0x3F800000U);
  state.cpsr = (state.cpsr & 0x0FFFFFFFU) | flags;
  const bool negative = (flags & 0x80000000U) != 0U, overflow = (flags & 0x10000000U) != 0U;
  if (negative != overflow) s0 = Float(state.vfp[20]);
  float s1 = Float(Read32(kReticleScaleGlobal + 0x38U));
  s0 = s1 * s0;
  s0 = s0 * mReticleScale;
  for (const std::uint32_t offset : {0x90U, 0xA0U, 0xB0U, 0x94U, 0xA4U, 0xB4U, 0x98U, 0xA8U}) {
    s1 = Float(Read32(state.r[13] + offset));
    const float scaled = s1 * s0;
    mMemory.Write32(state.r[13] + offset, Bits(scaled));
    s1 = scaled;
  }
  s1 = Float(Read32(state.r[13] + 0xB8U));
  s0 = s1 * s0;
  mMemory.Write32(state.r[13] + 0xB8U, Bits(s0));
  state.vfp[0] = Bits(s0);
  state.vfp[1] = Bits(s1);
  const std::uint32_t model = Read32(state.r[7] + 0xB4U);
  mMemory.Write8(model + 0xACU, static_cast<std::uint8_t>(state.r[10]));
  state.r[0] = Read32(state.r[7] + 0xB4U);
  state.r[14] = kReticleScaleReturn;
  Branch(state, kTopScreenReticleScaleBlock, kReticleScaleCall, result, blocksConsumed);
  return true;
}

} // namespace Oot3dNativeGame
