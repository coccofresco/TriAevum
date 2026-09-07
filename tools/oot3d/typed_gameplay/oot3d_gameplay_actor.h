#pragma once

#include "oot3d_gameplay_math.h"
#include "oot3d_gameplay_time.h"
#include "oot3d_gameplay_types.h"

namespace oot3d::gameplay {

struct ActorMovementConstants {
  float GravityUpdateScale = 0.0f;
  float PositionUpdateScale = 0.0f;
};

struct ActorKinematics {
  Vec3f Position;
  Vec3s Rotation;
  Vec3f Velocity;
  float SpeedXZ = 0.0f;
  float Gravity = 0.0f;
  float MinimumVelocityY = 0.0f;
  Vec3f CollisionDisplacement;
};

struct ActorLifecycleState {
  std::uint32_t Flags = 0;
  bool HasParent = false;
  bool UpdateEnabled = false;
  bool DrawEnabled = false;
};

struct ActorContextFreezeTimerResult {
  std::uint8_t Value = 0;
  bool Mutated = false;
};

struct ActorInstanceFreezeTimerResult {
  std::uint16_t Value = 0;
  bool Mutated = false;
  bool PassesTimerGate = false;
};

struct ActorEffectTimerResult {
  std::uint16_t ColorFilterTimer = 0;
  std::int16_t SfxTimer = 0;
  bool ColorFilterMutated = false;
  bool SfxMutated = false;
};

enum class ActorBlinkAction : std::uint8_t {
  Hold,
  Continue,
  DispatchRandom,
};

struct ActorBlinkAdvanceResult {
  std::int16_t Timer = 0;
  std::uint16_t SequenceIndex = 0;
  ActorBlinkAction Action = ActorBlinkAction::Hold;
  bool TimerMutated = false;
  bool SequenceMutated = false;
  bool Valid = false;
};

struct ActorAuthoredPhaseResult {
  std::uint8_t Value = 0;
  bool Mutated = false;
};

enum class ActorAuthoredRampAction : std::uint8_t {
  Hold,
  Increase,
  Decrease,
};

struct ActorAuthoredRampResult {
  std::int16_t Timer = 0;
  std::uint16_t StoredValue = 0;
  std::int16_t ComparisonValue = 0;
  std::int16_t RegisterValue = 0;
  ActorAuthoredRampAction Action = ActorAuthoredRampAction::Hold;
  bool Mutated = false;
  bool ValueClamped = false;
  bool Valid = false;
};

enum class ActorAuthoredCountdownAction : std::uint8_t {
  Hold,
  Decrement,
  Transition,
};

struct ActorAuthoredCountdownResult {
  std::int16_t Timer = 0;
  ActorAuthoredCountdownAction Action = ActorAuthoredCountdownAction::Hold;
  bool TimerMutated = false;
};

struct ActorAngularOscillatorState {
  std::int16_t Displacement = 0;
  std::int16_t Velocity = 0;
  std::uint8_t Direction = 0;
};

struct ActorAngularOscillatorResult {
  ActorAngularOscillatorState State;
  std::int16_t RegisterValue = 0;
  bool GroundReset = false;
  bool VelocityClamped = false;
  bool Valid = false;
};

// Actor_UpdateAll owns this global nonzero u8 countdown. Actor traversal
// remains per simulation tick while the field advances in authored frames.
ActorContextFreezeTimerResult AdvanceActorContextFreezeTimer(
    std::uint8_t value, const TimeContext &time) noexcept;

// A positive per-instance freeze timer suppresses the Actor update callback.
// The callback becomes eligible on the same logical frame that reaches zero.
ActorInstanceFreezeTimerResult AdvanceActorInstanceFreezeTimer(
    std::uint16_t value, const TimeContext &time) noexcept;

// These two Actor-owned visual/audio timers are evaluated immediately before
// the instance update callback and retain their native unsigned/signed gates.
ActorEffectTimerResult AdvanceActorEffectTimers(
    std::uint16_t colorFilterTimer, std::int16_t sfxTimer,
    const TimeContext &time) noexcept;

// Face state is discrete even when skeleton and tracking are evaluated every
// simulation tick. Reaching zero advances the sequence in the same authored
// frame; wrapping requests one native random timer refresh.
ActorBlinkAdvanceResult AdvanceActorBlinkState(
    std::int16_t timer, std::uint16_t sequenceIndex,
    std::uint16_t sequenceLength, const TimeContext &time) noexcept;

// Byte-sized actor phases remain authoritative on the authored timeline.
// Continuous consumers can still converge on every simulation substep.
ActorAuthoredPhaseResult AdvanceActorAuthoredPhase(
    std::uint8_t value, const TimeContext &time) noexcept;

// Events driven by an authored phase may dispatch at most once per logical
// frame even though their actor callback is evaluated on every substep.
bool ShouldDispatchActorAuthoredEvent(
    bool nativeCondition, const TimeContext &time) noexcept;

// Advances a code-authored signed timer and its clamped halfword ramp as one
// discrete owner. ComparisonValue and RegisterValue preserve conditional ARM
// store behavior for ABI bridges.
ActorAuthoredRampResult AdvanceActorAuthoredRamp(
    std::int16_t timer, std::uint16_t value, std::int16_t splitThreshold,
    std::uint16_t increaseStep, std::uint16_t decreaseStep,
    std::int16_t minimum, std::int16_t maximum,
    const TimeContext &time) noexcept;

// Advances a signed authored countdown and requests its state transition on
// either an existing zero or the same logical frame as a 1 -> 0 crossing.
ActorAuthoredCountdownResult AdvanceActorAuthoredCountdownToTransition(
    std::int16_t timer, const TimeContext &time) noexcept;

// Resamples the native `x += v; v += a` signed-halfword map without changing
// its full authored-frame endpoint. Ground crossings and bounce consumers
// remain eligible on every simulation substep.
ActorAngularOscillatorResult AdvanceActorAngularOscillator(
    ActorAngularOscillatorState state, bool grounded,
    std::int16_t acceleration, std::int16_t terminalVelocity,
    float nativeUpdateRate, float updateScale) noexcept;

void ActorUpdatePosition(ActorKinematics &actor, float nativeUpdateRate,
                         float positionUpdateScale) noexcept;
void ActorUpdateVelocityXZGravity(ActorKinematics &actor,
                                  float nativeUpdateRate,
                                  float gravityUpdateScale,
                                  const AngleSample &direction) noexcept;
void ActorUpdateVelocityXYZ(ActorKinematics &actor,
                            const AngleSample &elevation,
                            const AngleSample &azimuth) noexcept;
void ActorUpdatePositionWithVelocityFromRotation(
    ActorKinematics &actor, float nativeUpdateRate, float positionUpdateScale,
    const AngleSample &elevation, const AngleSample &azimuth) noexcept;
void ActorMoveForward(ActorKinematics &actor, float nativeUpdateRate,
                      const ActorMovementConstants &constants,
                      const AngleSample &direction) noexcept;
bool ActorHasParent(const ActorLifecycleState &actor) noexcept;
bool ActorHasNoParent(const ActorLifecycleState &actor) noexcept;
void ActorKill(ActorLifecycleState &actor) noexcept;
void ActorSetUniformScale(Vec3f &scale, float value) noexcept;

} // namespace oot3d::gameplay
