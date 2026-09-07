#include "oot3d_gameplay_player.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace oot3d::gameplay {

namespace {

constexpr std::uint32_t kItemInHand = 0x8U;
constexpr std::uint32_t kHostileLockOn = 0x10U;
constexpr std::uint32_t kSwimmingState = 0x80U;
constexpr std::uint32_t kIdleRuntimeState = 0x200U;
constexpr std::uint32_t kIdleOverride = 0x400U;
constexpr std::uint32_t kHeightOffsetState = 0x00800000U;
constexpr std::uint32_t kDeepWaterState = 0x400U;
constexpr std::int32_t kAlternateSwimAnimation = 0x34;
constexpr double kDynamicWallClimbPromptNativeTimeUnits = 26.0;
constexpr double kHighLedgeClimbNativeTimeUnits = 18.0;
constexpr double kLowLedgeJumpNativeTimeUnits = 8.0;

} // namespace

std::int32_t
PlayerGetExplosiveHeld(const PlayerEquipmentState &player) noexcept {
  const std::int32_t explosive =
      static_cast<std::int32_t>(player.HeldItemAction) - 0x12;
  return explosive >= 0 && explosive <= 1 ? explosive : -1;
}

bool PlayerHoldsHookshot(const PlayerEquipmentState &player) noexcept {
  return player.HeldItemAction == 0x10 || player.HeldItemAction == 0x11;
}

bool PlayerHoldsTwoHandedWeapon(
    const PlayerEquipmentState &player) noexcept {
  const std::uint32_t action = static_cast<std::uint32_t>(
      static_cast<std::int32_t>(player.HeldItemAction) - 5);
  return action < 3U;
}

bool PlayerHasFreeHookshotHand(const PlayerEquipmentState &player) noexcept {
  return PlayerHoldsHookshot(player) && !player.HasHeldActor;
}

std::uint32_t
PlayerItemInHandMask(const PlayerEquipmentState &player) noexcept {
  return player.StateFlags & kItemInHand;
}

void PlayerUpdateUnderwaterTimer(
    std::uint16_t &underwaterTimer, bool submergedBeyondSurfaceReference,
    const TimeContext &time) noexcept {
  if (!submergedBeyondSurfaceReference) {
    underwaterTimer = 0U;
    return;
  }
  if (time.CrossedLogicalFrame &&
      underwaterTimer < kPlayerUnderwaterTimerSaturationLogicalFrames) {
    ++underwaterTimer;
  }
}

void PlayerUpdateMeleeActionTiming(
    PlayerMeleeActionTimingState &state,
    const TimeContext &time) noexcept {
  if (state.Timer == 0) {
    state.ComboState = 0U;
    return;
  }
  if (!time.CrossedLogicalFrame) {
    return;
  }
  if (state.Timer < 0) {
    ++state.Timer;
  } else {
    --state.Timer;
  }
}

void PlayerAdvanceMeleeWeaponTipComboState(
    std::uint8_t &comboState,
    const TimeContext &time) noexcept {
  if (comboState >= 3U && time.CrossedLogicalFrame) {
    comboState = static_cast<std::uint8_t>(comboState + 1U);
  }
}

void PlayerUpdateDamageRunTimer(
    std::uint8_t &damageRunTimer,
    const TimeContext &time) noexcept {
  if (damageRunTimer != 0U && time.CrossedLogicalFrame) {
    --damageRunTimer;
  }
}

void PlayerUpdateInvincibilityTimer(
    std::int8_t &invincibilityTimer, bool holdPositiveTimer,
    const TimeContext &time) noexcept {
  if (!time.CrossedLogicalFrame || invincibilityTimer == 0 ||
      (invincibilityTimer > 0 && holdPositiveTimer)) {
    return;
  }
  if (invincibilityTimer < 0) {
    ++invincibilityTimer;
  } else {
    --invincibilityTimer;
  }
}

void PlayerAdvanceDamageFlickerAnimationCounter(
    std::uint8_t &counter, std::uint8_t nativeAdvance,
    const TimeContext &time) noexcept {
  if (time.CrossedLogicalFrame) {
    counter = static_cast<std::uint8_t>(counter + nativeAdvance);
  }
}

bool PlayerAdvanceRespawnDamageState(
    std::int8_t &respawnDamageState,
    const TimeContext &time) noexcept {
  if (respawnDamageState >= 0 || !time.CrossedLogicalFrame) {
    return false;
  }
  respawnDamageState =
      static_cast<std::int8_t>(respawnDamageState + 1);
  return respawnDamageState == 0;
}

bool PlayerAdvanceRandomTurnTimer(
    std::int16_t &randomTurnTimer,
    const TimeContext &time) noexcept {
  if (!time.CrossedLogicalFrame) {
    return false;
  }
  if (randomTurnTimer != 0) {
    std::uint16_t encoded = std::bit_cast<std::uint16_t>(randomTurnTimer);
    encoded = static_cast<std::uint16_t>(encoded - 1U);
    randomTurnTimer = std::bit_cast<std::int16_t>(encoded);
  }
  return randomTurnTimer == 0;
}

void PlayerAdvanceAttentionPersistenceCounter(
    std::uint8_t &counter,
    const TimeContext &time) noexcept {
  if (!time.CrossedLogicalFrame) {
    return;
  }
  const std::uint8_t wrapped = static_cast<std::uint8_t>(counter + 1U);
  counter = wrapped > 0xFEU ? 0xFEU : wrapped;
}

void PlayerUpdateCommonCountdowns(
    PlayerCommonCountdownState &state,
    const TimeContext &time) noexcept {
  TickDownIfNonzero(state.ItemActionCooldownTimer, time);
  TickDownIfNonzero(state.TextboxButtonCooldownTimer, time);
  TickDownIfNonzero(state.CollisionSfxCooldownTimer, time);
  if (state.FairyReviveGraceTimer != 0U) {
    --state.FairyReviveGraceTimer;
  }
}

void PlayerAdvanceFishingItemStateTowardReady(
    std::int16_t &itemActionStateOrBurnTimer,
    std::int8_t heldItemAction,
    const TimeContext &time) noexcept {
  if (heldItemAction == kPlayerHeldItemActionFishingPole &&
      itemActionStateOrBurnTimer < 0 && time.CrossedLogicalFrame) {
    ++itemActionStateOrBurnTimer;
  }
}

void PlayerUpdateFloorTypeTimer(
    PlayerFloorTypeTimerState &state, std::uint8_t currentFloorType,
    const TimeContext &time) noexcept {
  if (state.PreviousFloorType != currentFloorType) {
    state.PreviousFloorType = currentFloorType;
    state.Timer = 0;
    return;
  }
  if (time.CrossedLogicalFrame) {
    state.Timer = static_cast<std::uint8_t>(state.Timer + 1U);
  }
}

void PlayerUpdateLedgeContactTimer(
    PlayerLedgeContactTimerState &state, std::uint8_t currentType,
    bool resetTimer, bool advanceTimer, const TimeContext &time) noexcept {
  if (state.Type != currentType) {
    state.Type = currentType;
    state.ElapsedNativeTimeUnits = 0.0;
    return;
  }
  if (resetTimer) {
    state.ElapsedNativeTimeUnits = 0.0;
    return;
  }
  if (!advanceTimer || !std::isfinite(time.NativeUpdateRate) ||
      time.NativeUpdateRate <= 0.0F) {
    return;
  }
  state.ElapsedNativeTimeUnits = std::min(
      kPlayerLedgeContactSaturationNativeTimeUnits,
      state.ElapsedNativeTimeUnits +
          static_cast<double>(time.NativeUpdateRate));
}

std::uint8_t PlayerProjectLedgeContactTimerToWire(
    const PlayerLedgeContactTimerState &state,
    const TimeContext &time) noexcept {
  if (!std::isfinite(state.ElapsedNativeTimeUnits) ||
      state.ElapsedNativeTimeUnits <= 0.0 ||
      !std::isfinite(time.NativeUpdateRate) ||
      time.NativeUpdateRate <= 0.0F) {
    return 0;
  }
  const double simulationTicks =
      std::floor(state.ElapsedNativeTimeUnits /
                     static_cast<double>(time.NativeUpdateRate) +
                 std::numeric_limits<double>::epsilon() * 8.0);
  return static_cast<std::uint8_t>(std::min(
      simulationTicks,
      static_cast<double>(std::numeric_limits<std::uint8_t>::max())));
}

double PlayerLedgeGateNativeTimeUnits(PlayerLedgeGate gate) noexcept {
  switch (gate) {
  case PlayerLedgeGate::DynamicWallClimbPrompt:
    return kDynamicWallClimbPromptNativeTimeUnits;
  case PlayerLedgeGate::HighLedgeClimb:
    return kHighLedgeClimbNativeTimeUnits;
  case PlayerLedgeGate::LowLedgeJump:
    return kLowLedgeJumpNativeTimeUnits;
  }
  return std::numeric_limits<double>::infinity();
}

bool PlayerLedgeGateReached(double elapsedNativeTimeUnits,
                            PlayerLedgeGate gate) noexcept {
  return std::isfinite(elapsedNativeTimeUnits) &&
         elapsedNativeTimeUnits >= PlayerLedgeGateNativeTimeUnits(gate);
}

void PlayerSetRuntimeFlag200(std::uint32_t &runtimeFlags,
                             bool enabled) noexcept {
  if (enabled) {
    runtimeFlags |= kIdleRuntimeState;
  } else {
    runtimeFlags &= ~kIdleRuntimeState;
  }
}

void PlayerSetCutsceneAction(PlayerCutsceneActionState &state,
                             GuestAddress action, std::uint8_t mode,
                             bool haltActors) noexcept {
  state.Mode = mode;
  state.Action = action;
  state.HaltActors = haltActors ? 1U : 0U;
}

bool PlayerInCutsceneMode(
    const PlayerCutsceneModeState &state,
    const PlayerCutsceneModeConstants &constants) noexcept {
  if ((state.StateFlags & constants.BlockingStateMask) != 0U ||
      state.CutsceneAction != 0U || state.PlayState5C2D == 0x14U) {
    return true;
  }
  if ((state.StateFlags & 1U) != 0U ||
      (state.StateFlagsByte & 0x80U) != 0U) {
    return true;
  }

  const std::int32_t itemRange =
      static_cast<std::int32_t>(state.ItemAction) - 0x15;
  if (state.GlobalState80 != 0U && itemRange >= 0 && itemRange <= 5) {
    return true;
  }
  return state.State1749 == 4U;
}

float PlayerGetHeight(const PlayerHeightState &state,
                      const PlayerHeightConstants &constants) noexcept {
  const float flagOffset = (state.StateFlags & kHeightOffsetState) != 0U
                               ? constants.RaisedFlagOffset
                               : constants.DefaultFlagOffset;
  const float base = state.AlternateBaseHeight ? constants.AlternateBase
                                                : constants.DefaultBase;
  return flagOffset + base;
}

std::int32_t PlayerSelectIdleAnimation(
    const PlayerIdleAnimationState &state, std::int32_t normalAnimation,
    std::int32_t alternateAnimation) noexcept {
  const bool alternate =
      (state.RuntimeFlags & kIdleRuntimeState) != 0U ||
      (state.BackgroundState == 1U && state.GlobalTransitionState > 0x50 &&
       (state.RuntimeFlags & kIdleOverride) == 0U);
  return alternate ? alternateAnimation : normalAnimation;
}

bool PlayerUpdateHostileLockOn(PlayerLockOnState &state,
                               float zero) noexcept {
  if (state.HasFocusActor && (state.FocusActorFlags & 0x5U) == 0x5U) {
    state.StateFlags |= kHostileLockOn;
    return true;
  }

  if ((state.StateFlags & kHostileLockOn) != 0U) {
    state.StateFlags &= ~kHostileLockOn;
    if (state.Speed == zero) {
      state.LockOnYaw = state.ShapeYaw;
    }
  }
  return false;
}

void PlayerUpdateSwimVerticalVelocity(
    PlayerSwimVerticalState &state,
    const PlayerSwimVerticalConstants &constants) noexcept {
  float surfaceReference = state.SurfaceReference;
  if (state.VelocityY < constants.Zero) {
    surfaceReference += constants.DescendingSurfaceOffset;
  }

  float acceleration = 0.0f;
  float terminalVelocity = constants.DefaultTerminalVelocity;
  if (state.DepthInWater < surfaceReference) {
    float damping = constants.Zero;
    if (state.VelocityY > constants.Zero) {
      damping = state.VelocityY * constants.PositiveVelocityDamping;
    }
    acceleration = constants.SurfaceAcceleration - damping;
  } else {
    const float sinkThreshold =
        state.AnimationIndex == kAlternateSwimAnimation
            ? constants.AlternateSinkThreshold
            : constants.DefaultSinkThreshold;
    const bool swimmingState = (state.StateFlags & kSwimmingState) != 0U;
    const bool useSinkAcceleration =
        !swimmingState && state.MovementState == 1U &&
        state.VelocityY >= sinkThreshold;

    if (useSinkAcceleration) {
      acceleration = constants.SinkAcceleration;
    } else {
      terminalVelocity = constants.RisingTerminalVelocity;
      acceleration = constants.RisingAcceleration;
      if (state.VelocityY < constants.Zero) {
        acceleration += state.VelocityY * constants.SinkAcceleration;
      }
    }

    if (std::bit_cast<std::int32_t>(state.DepthInWater) >
        std::bit_cast<std::int32_t>(constants.DeepWaterThreshold)) {
      state.SecondaryStateFlags |= kDeepWaterState;
    }
  }

  state.VelocityY += acceleration;
  if (constants.Zero <
      (state.VelocityY - terminalVelocity) * acceleration) {
    state.VelocityY = terminalVelocity;
  }
  state.Gravity = constants.Zero;
}

} // namespace oot3d::gameplay
