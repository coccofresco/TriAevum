#include "oot3d_gameplay_actor.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace oot3d::gameplay {
namespace {

std::int16_t WrapActorS16(std::int32_t value) noexcept {
  return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value));
}

bool QuantizeActorDelta(double value, std::int32_t *result) noexcept {
  if (result == nullptr || !std::isfinite(value) ||
      value < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
      value > static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
    return false;
  }
  *result = static_cast<std::int32_t>(value);
  return true;
}

} // namespace

ActorContextFreezeTimerResult AdvanceActorContextFreezeTimer(
    std::uint8_t value, const TimeContext &time) noexcept {
  if (!time.CrossedLogicalFrame || value == 0U) {
    return {value, false};
  }
  return {static_cast<std::uint8_t>(value - 1U), true};
}

ActorInstanceFreezeTimerResult AdvanceActorInstanceFreezeTimer(
    std::uint16_t value, const TimeContext &time) noexcept {
  if (value == 0U) {
    return {value, false, true};
  }
  if (!time.CrossedLogicalFrame) {
    return {value, false, false};
  }
  const auto next = static_cast<std::uint16_t>(value - 1U);
  return {next, true, next == 0U};
}

ActorEffectTimerResult AdvanceActorEffectTimers(
    std::uint16_t colorFilterTimer, std::int16_t sfxTimer,
    const TimeContext &time) noexcept {
  ActorEffectTimerResult result{colorFilterTimer, sfxTimer, false, false};
  if (!time.CrossedLogicalFrame) {
    return result;
  }
  if (result.ColorFilterTimer != 0U) {
    --result.ColorFilterTimer;
    result.ColorFilterMutated = true;
  }
  if (result.SfxTimer > 0) {
    --result.SfxTimer;
    result.SfxMutated = true;
  }
  return result;
}

ActorBlinkAdvanceResult AdvanceActorBlinkState(
    std::int16_t timer, std::uint16_t sequenceIndex,
    std::uint16_t sequenceLength, const TimeContext &time) noexcept {
  ActorBlinkAdvanceResult result{
      timer, sequenceIndex, ActorBlinkAction::Hold, false, false,
      sequenceLength != 0U};
  if (!result.Valid || !time.CrossedLogicalFrame) {
    return result;
  }

  if (result.Timer != 0) {
    auto timerBits = std::bit_cast<std::uint16_t>(result.Timer);
    timerBits = static_cast<std::uint16_t>(timerBits - 1U);
    result.Timer = std::bit_cast<std::int16_t>(timerBits);
    result.TimerMutated = true;
    if (result.Timer != 0) {
      result.Action = ActorBlinkAction::Continue;
      return result;
    }
  }

  result.SequenceIndex =
      static_cast<std::uint16_t>(result.SequenceIndex + 1U);
  result.SequenceMutated = true;
  if (result.SequenceIndex == sequenceLength) {
    result.SequenceIndex = 0U;
    result.Action = ActorBlinkAction::DispatchRandom;
  } else {
    result.Action = ActorBlinkAction::Continue;
  }
  return result;
}

ActorAuthoredPhaseResult AdvanceActorAuthoredPhase(
    std::uint8_t value, const TimeContext &time) noexcept {
  if (!time.CrossedLogicalFrame) {
    return {value, false};
  }
  return {static_cast<std::uint8_t>(value + 1U), true};
}

bool ShouldDispatchActorAuthoredEvent(
    bool nativeCondition, const TimeContext &time) noexcept {
  return nativeCondition && time.CrossedLogicalFrame;
}

ActorAuthoredRampResult AdvanceActorAuthoredRamp(
    std::int16_t timer, std::uint16_t value, std::int16_t splitThreshold,
    std::uint16_t increaseStep, std::uint16_t decreaseStep,
    std::int16_t minimum, std::int16_t maximum,
    const TimeContext &time) noexcept {
  ActorAuthoredRampResult result{
      timer,
      value,
      std::bit_cast<std::int16_t>(value),
      std::bit_cast<std::int16_t>(value),
      ActorAuthoredRampAction::Hold,
      false,
      false,
      minimum <= maximum,
  };
  if (!result.Valid || timer == 0 || !time.CrossedLogicalFrame) {
    return result;
  }

  auto timerBits = std::bit_cast<std::uint16_t>(timer);
  timerBits = static_cast<std::uint16_t>(timerBits - 1U);
  result.Timer = std::bit_cast<std::int16_t>(timerBits);
  result.Mutated = true;

  std::uint16_t candidateBits = value;
  if (timer >= splitThreshold) {
    candidateBits =
        static_cast<std::uint16_t>(candidateBits + increaseStep);
    result.Action = ActorAuthoredRampAction::Increase;
  } else {
    candidateBits =
        static_cast<std::uint16_t>(candidateBits - decreaseStep);
    result.Action = ActorAuthoredRampAction::Decrease;
  }
  result.ComparisonValue = std::bit_cast<std::int16_t>(candidateBits);
  result.RegisterValue = result.ComparisonValue;

  if (result.Action == ActorAuthoredRampAction::Increase &&
      result.ComparisonValue > maximum) {
    result.RegisterValue = maximum;
    result.StoredValue = std::bit_cast<std::uint16_t>(maximum);
    result.ValueClamped = true;
  } else if (result.Action == ActorAuthoredRampAction::Decrease &&
             result.ComparisonValue < minimum) {
    result.StoredValue = std::bit_cast<std::uint16_t>(minimum);
    result.ValueClamped = true;
  } else {
    result.StoredValue = candidateBits;
  }
  return result;
}

ActorAuthoredCountdownResult AdvanceActorAuthoredCountdownToTransition(
    std::int16_t timer, const TimeContext &time) noexcept {
  if (!time.CrossedLogicalFrame) {
    return {timer, ActorAuthoredCountdownAction::Hold, false};
  }
  if (timer == 0) {
    return {timer, ActorAuthoredCountdownAction::Transition, false};
  }

  const auto timerBits = std::bit_cast<std::uint16_t>(timer);
  const auto decrementedBits =
      static_cast<std::uint16_t>(timerBits - std::uint16_t{1});
  const auto decremented = std::bit_cast<std::int16_t>(decrementedBits);
  return {
      decremented,
      decremented == 0 ? ActorAuthoredCountdownAction::Transition
                       : ActorAuthoredCountdownAction::Decrement,
      true,
  };
}

ActorAngularOscillatorResult AdvanceActorAngularOscillator(
    ActorAngularOscillatorState state, bool grounded,
    std::int16_t acceleration, std::int16_t terminalVelocity,
    float nativeUpdateRate, float updateScale) noexcept {
  ActorAngularOscillatorResult result{
      state,
      state.Velocity,
      false,
      false,
      false,
  };
  if (!std::isfinite(nativeUpdateRate) || !std::isfinite(updateScale) ||
      nativeUpdateRate <= 0.0F || updateScale <= 0.0F) {
    return result;
  }

  const double frameScale =
      static_cast<double>(nativeUpdateRate) *
      static_cast<double>(updateScale);
  const double accelerationValue = static_cast<double>(acceleration);
  double displacementDelta =
      static_cast<double>(state.Velocity) * frameScale +
      0.5 * accelerationValue * frameScale * (frameScale - 1.0);
  const double velocityDelta = accelerationValue * frameScale;
  if (state.Direction == 0U) {
    displacementDelta = -displacementDelta;
  }
  std::int32_t quantizedDisplacementDelta = 0;
  std::int32_t quantizedVelocityDelta = 0;
  if (!std::isfinite(frameScale) || frameScale <= 0.0 ||
      !QuantizeActorDelta(displacementDelta,
                          &quantizedDisplacementDelta) ||
      !QuantizeActorDelta(velocityDelta, &quantizedVelocityDelta)) {
    return result;
  }

  const std::int16_t candidateDisplacement = WrapActorS16(
      static_cast<std::int32_t>(state.Displacement) +
      quantizedDisplacementDelta);
  const std::int16_t candidateVelocity = WrapActorS16(
      static_cast<std::int32_t>(state.Velocity) +
      quantizedVelocityDelta);

  const bool crossedGround =
      grounded &&
      (state.Direction == 0U ? candidateDisplacement >= 0
                             : candidateDisplacement <= 0);
  if (crossedGround) {
    result.State.Displacement = 0;
    result.State.Velocity = 0;
    result.RegisterValue = candidateDisplacement;
    result.GroundReset = true;
  } else {
    result.State.Displacement = candidateDisplacement;
    result.State.Velocity = candidateVelocity;
    result.RegisterValue = candidateVelocity;
    if (candidateVelocity < terminalVelocity) {
      result.State.Velocity = terminalVelocity;
      result.VelocityClamped = true;
    }
  }
  result.Valid = true;
  return result;
}

void ActorUpdatePosition(ActorKinematics &actor, float nativeUpdateRate,
                         float positionUpdateScale) noexcept {
  const float movementScale = nativeUpdateRate * positionUpdateScale;
  actor.Position.X = actor.Position.X + actor.CollisionDisplacement.X +
                     actor.Velocity.X * movementScale;
  actor.Position.Y = actor.Position.Y + actor.CollisionDisplacement.Y +
                     actor.Velocity.Y * movementScale;
  actor.Position.Z = actor.Position.Z + actor.CollisionDisplacement.Z +
                     actor.Velocity.Z * movementScale;
}

void ActorUpdateVelocityXZGravity(ActorKinematics &actor,
                                  float nativeUpdateRate,
                                  float gravityUpdateScale,
                                  const AngleSample &direction) noexcept {
  actor.Velocity.X = direction.Sin * actor.SpeedXZ;
  actor.Velocity.Z = direction.Cos * actor.SpeedXZ;
  actor.Velocity.Y =
      actor.Velocity.Y + actor.Gravity * nativeUpdateRate * gravityUpdateScale;
  actor.Velocity.Y = std::max(actor.Velocity.Y, actor.MinimumVelocityY);
}

void ActorUpdateVelocityXYZ(ActorKinematics &actor,
                            const AngleSample &elevation,
                            const AngleSample &azimuth) noexcept {
  const float horizontalSpeed = elevation.Cos * actor.SpeedXZ;
  actor.Velocity.X = azimuth.Sin * horizontalSpeed;
  actor.Velocity.Y = elevation.Sin * actor.SpeedXZ;
  actor.Velocity.Z = azimuth.Cos * horizontalSpeed;
}

void ActorUpdatePositionWithVelocityFromRotation(
    ActorKinematics &actor, float nativeUpdateRate, float positionUpdateScale,
    const AngleSample &elevation, const AngleSample &azimuth) noexcept {
  ActorUpdateVelocityXYZ(actor, elevation, azimuth);
  ActorUpdatePosition(actor, nativeUpdateRate, positionUpdateScale);
}

void ActorMoveForward(ActorKinematics &actor, float nativeUpdateRate,
                      const ActorMovementConstants &constants,
                      const AngleSample &direction) noexcept {
  ActorUpdateVelocityXZGravity(actor, nativeUpdateRate,
                               constants.GravityUpdateScale, direction);
  ActorUpdatePosition(actor, nativeUpdateRate, constants.PositionUpdateScale);
}

bool ActorHasParent(const ActorLifecycleState &actor) noexcept {
  return actor.HasParent;
}

bool ActorHasNoParent(const ActorLifecycleState &actor) noexcept {
  return !actor.HasParent;
}

void ActorKill(ActorLifecycleState &actor) noexcept {
  actor.UpdateEnabled = false;
  actor.DrawEnabled = false;
  actor.Flags &= ~std::uint32_t{1};
}

void ActorSetUniformScale(Vec3f &scale, float value) noexcept {
  scale = {value, value, value};
}

} // namespace oot3d::gameplay
