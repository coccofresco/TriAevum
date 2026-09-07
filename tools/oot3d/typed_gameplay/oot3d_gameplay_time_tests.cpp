#include "oot3d_gameplay_actor.h"
#include "oot3d_gameplay_camera.h"
#include "oot3d_gameplay_camera_animation.h"
#include "oot3d_gameplay_cutscene.h"
#include "oot3d_gameplay_quake.h"
#include "oot3d_gameplay_time.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>

namespace {

void Expect(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void ExpectNear(double actual, double expected, const char* message) {
  if (std::abs(actual - expected) > 1.0e-12) {
    throw std::runtime_error(message);
  }
}

void TestActorUpdateAllTiming(
    const oot3d::gameplay::TimeContext& intermediate,
    const oot3d::gameplay::TimeContext& logical) {
  auto contextFreeze =
      oot3d::gameplay::AdvanceActorContextFreezeTimer(3U, intermediate);
  Expect(contextFreeze.Value == 3U && !contextFreeze.Mutated,
         "Actor context freeze advanced on an intermediate substep");
  contextFreeze =
      oot3d::gameplay::AdvanceActorContextFreezeTimer(3U, logical);
  Expect(contextFreeze.Value == 2U && contextFreeze.Mutated,
         "Actor context freeze did not advance on a logical frame");
  contextFreeze =
      oot3d::gameplay::AdvanceActorContextFreezeTimer(0U, logical);
  Expect(contextFreeze.Value == 0U && !contextFreeze.Mutated,
         "Actor context freeze underflowed");

  auto instanceFreeze =
      oot3d::gameplay::AdvanceActorInstanceFreezeTimer(1U, intermediate);
  Expect(instanceFreeze.Value == 1U && !instanceFreeze.Mutated &&
             !instanceFreeze.PassesTimerGate,
         "Actor instance freeze released on an intermediate substep");
  instanceFreeze =
      oot3d::gameplay::AdvanceActorInstanceFreezeTimer(2U, logical);
  Expect(instanceFreeze.Value == 1U && instanceFreeze.Mutated &&
             !instanceFreeze.PassesTimerGate,
         "Actor instance freeze released before reaching zero");
  instanceFreeze =
      oot3d::gameplay::AdvanceActorInstanceFreezeTimer(1U, logical);
  Expect(instanceFreeze.Value == 0U && instanceFreeze.Mutated &&
             instanceFreeze.PassesTimerGate,
         "Actor instance freeze did not release at zero");
  instanceFreeze =
      oot3d::gameplay::AdvanceActorInstanceFreezeTimer(0U, intermediate);
  Expect(instanceFreeze.Value == 0U && !instanceFreeze.Mutated &&
             instanceFreeze.PassesTimerGate,
         "unfrozen Actor failed its timer gate");

  auto effectTimers =
      oot3d::gameplay::AdvanceActorEffectTimers(4U, 3, intermediate);
  Expect(effectTimers.ColorFilterTimer == 4U &&
             effectTimers.SfxTimer == 3 &&
             !effectTimers.ColorFilterMutated && !effectTimers.SfxMutated,
         "Actor effect timers advanced on an intermediate substep");
  effectTimers =
      oot3d::gameplay::AdvanceActorEffectTimers(4U, 3, logical);
  Expect(effectTimers.ColorFilterTimer == 3U &&
             effectTimers.SfxTimer == 2 &&
             effectTimers.ColorFilterMutated && effectTimers.SfxMutated,
         "Actor effect timers did not advance on a logical frame");
  effectTimers =
      oot3d::gameplay::AdvanceActorEffectTimers(0U, -1, logical);
  Expect(effectTimers.ColorFilterTimer == 0U &&
             effectTimers.SfxTimer == -1 &&
             !effectTimers.ColorFilterMutated && !effectTimers.SfxMutated,
         "Actor effect timer terminal states changed");
}

void TestActorBlinkTiming(
    const oot3d::gameplay::TimeContext& intermediate,
    const oot3d::gameplay::TimeContext& logical) {
  using oot3d::gameplay::ActorBlinkAction;

  auto blink =
      oot3d::gameplay::AdvanceActorBlinkState(3, 0U, 4U, intermediate);
  Expect(blink.Valid && blink.Timer == 3 && blink.SequenceIndex == 0U &&
             blink.Action == ActorBlinkAction::Hold &&
             !blink.TimerMutated && !blink.SequenceMutated,
         "Actor blink advanced on an intermediate substep");

  blink = oot3d::gameplay::AdvanceActorBlinkState(3, 0U, 4U, logical);
  Expect(blink.Timer == 2 && blink.SequenceIndex == 0U &&
             blink.Action == ActorBlinkAction::Continue &&
             blink.TimerMutated && !blink.SequenceMutated,
         "Actor blink timer did not advance on a logical frame");

  blink = oot3d::gameplay::AdvanceActorBlinkState(1, 0U, 4U, logical);
  Expect(blink.Timer == 0 && blink.SequenceIndex == 1U &&
             blink.Action == ActorBlinkAction::Continue &&
             blink.TimerMutated && blink.SequenceMutated,
         "Actor blink zero crossing did not advance the sequence");

  blink = oot3d::gameplay::AdvanceActorBlinkState(0, 3U, 4U, logical);
  Expect(blink.Timer == 0 && blink.SequenceIndex == 0U &&
             blink.Action == ActorBlinkAction::DispatchRandom &&
             !blink.TimerMutated && blink.SequenceMutated,
         "Actor blink sequence did not request one random refresh");

  blink = oot3d::gameplay::AdvanceActorBlinkState(-1, 2U, 4U, logical);
  Expect(blink.Timer == -2 && blink.SequenceIndex == 2U &&
             blink.Action == ActorBlinkAction::Continue &&
             blink.TimerMutated && !blink.SequenceMutated,
         "Actor blink signed nonzero timer semantics changed");

  blink = oot3d::gameplay::AdvanceActorBlinkState(
      std::numeric_limits<std::int16_t>::min(), 2U, 4U, logical);
  Expect(blink.Timer == std::numeric_limits<std::int16_t>::max() &&
             blink.SequenceIndex == 2U &&
             blink.Action == ActorBlinkAction::Continue,
         "Actor blink timer did not retain native s16 wrap");

  blink = oot3d::gameplay::AdvanceActorBlinkState(0, 0U, 0U, logical);
  Expect(!blink.Valid && blink.Action == ActorBlinkAction::Hold &&
             !blink.TimerMutated && !blink.SequenceMutated,
         "Actor blink accepted an empty sequence");
}

void TestActorAuthoredPhaseTiming(
    const oot3d::gameplay::TimeContext& intermediate,
    const oot3d::gameplay::TimeContext& logical) {
  auto phase =
      oot3d::gameplay::AdvanceActorAuthoredPhase(0xFEU, intermediate);
  Expect(phase.Value == 0xFEU && !phase.Mutated,
         "Actor authored phase advanced on an intermediate substep");

  phase = oot3d::gameplay::AdvanceActorAuthoredPhase(0xFEU, logical);
  Expect(phase.Value == 0xFFU && phase.Mutated,
         "Actor authored phase did not advance on a logical frame");

  phase = oot3d::gameplay::AdvanceActorAuthoredPhase(0xFFU, logical);
  Expect(phase.Value == 0U && phase.Mutated,
         "Actor authored phase did not retain native u8 wrap");

  Expect(!oot3d::gameplay::ShouldDispatchActorAuthoredEvent(
             true, intermediate) &&
             oot3d::gameplay::ShouldDispatchActorAuthoredEvent(true,
                                                                logical) &&
             !oot3d::gameplay::ShouldDispatchActorAuthoredEvent(false,
                                                                 logical),
         "Actor authored event gate changed its native condition or cadence");
}

void TestSignedWrappingCountdown(
    const oot3d::gameplay::TimeContext& intermediate,
    const oot3d::gameplay::TimeContext& logical) {
  std::int16_t value = 2;
  auto mutation =
      oot3d::gameplay::TickDownWrappingIfNonzero(value, intermediate);
  Expect(!mutation.Mutated && mutation.Previous == 2 &&
             mutation.Current == 2 && value == 2,
         "Signed countdown advanced on an intermediate substep");

  mutation = oot3d::gameplay::TickDownWrappingIfNonzero(value, logical);
  Expect(mutation.Mutated && !mutation.ReachedZero &&
             mutation.Previous == 2 && mutation.Current == 1 && value == 1,
         "Signed countdown did not advance on a logical frame");

  mutation = oot3d::gameplay::TickDownWrappingIfNonzero(value, logical);
  Expect(mutation.Mutated && mutation.ReachedZero &&
             mutation.Previous == 1 && mutation.Current == 0 && value == 0,
         "Signed countdown did not report its zero crossing");

  mutation = oot3d::gameplay::TickDownWrappingIfNonzero(value, logical);
  Expect(!mutation.Mutated && value == 0,
         "Signed countdown changed its zero sentinel");

  value = std::numeric_limits<std::int16_t>::min();
  mutation = oot3d::gameplay::TickDownWrappingIfNonzero(value, logical);
  Expect(mutation.Mutated &&
             value == std::numeric_limits<std::int16_t>::max(),
         "Signed countdown did not retain native fixed-width wrap");
}

void TestActorAuthoredRampTiming(
    const oot3d::gameplay::TimeContext& intermediate,
    const oot3d::gameplay::TimeContext& logical) {
  using oot3d::gameplay::ActorAuthoredRampAction;
  const auto advance = [&](std::int16_t timer, std::uint16_t value,
                           const oot3d::gameplay::TimeContext& time) {
    return oot3d::gameplay::AdvanceActorAuthoredRamp(
        timer, value, 8, 0xFFU, 0x41U, 0, 0xFF, time);
  };

  auto result = advance(8, 0, intermediate);
  Expect(result.Valid && !result.Mutated &&
             result.Action == ActorAuthoredRampAction::Hold &&
             result.Timer == 8 && result.StoredValue == 0,
         "Actor authored ramp advanced on an intermediate substep");

  result = advance(8, 0, logical);
  Expect(result.Mutated &&
             result.Action == ActorAuthoredRampAction::Increase &&
             result.Timer == 7 && result.StoredValue == 0xFFU &&
             result.ComparisonValue == 0xFF &&
             result.RegisterValue == 0xFF && !result.ValueClamped,
         "Actor authored ramp increase mismatch");

  result = advance(8, 10, logical);
  Expect(result.Mutated &&
             result.Action == ActorAuthoredRampAction::Increase &&
             result.StoredValue == 0xFFU &&
             result.ComparisonValue == 265 &&
             result.RegisterValue == 0xFF && result.ValueClamped,
         "Actor authored ramp upper clamp mismatch");

  result = advance(7, 0xFFU, logical);
  Expect(result.Mutated &&
             result.Action == ActorAuthoredRampAction::Decrease &&
             result.Timer == 6 && result.StoredValue == 190U &&
             result.ComparisonValue == 190 &&
             result.RegisterValue == 190 && !result.ValueClamped,
         "Actor authored ramp decrease mismatch");

  result = advance(1, 0, logical);
  Expect(result.Mutated &&
             result.Action == ActorAuthoredRampAction::Decrease &&
             result.Timer == 0 && result.StoredValue == 0U &&
             result.ComparisonValue == -65 &&
             result.RegisterValue == -65 && result.ValueClamped,
         "Actor authored ramp lower clamp mismatch");

  result = advance(0, 123U, logical);
  Expect(result.Valid && !result.Mutated &&
             result.Action == ActorAuthoredRampAction::Hold &&
             result.Timer == 0 && result.StoredValue == 123U,
         "Actor authored ramp changed its inactive state");
}

void TestActorAuthoredCountdownTransitionTiming(
    const oot3d::gameplay::TimeContext& intermediate,
    const oot3d::gameplay::TimeContext& logical) {
  using oot3d::gameplay::ActorAuthoredCountdownAction;
  const auto advance = [&](std::int16_t timer,
                           const oot3d::gameplay::TimeContext& time) {
    return oot3d::gameplay::AdvanceActorAuthoredCountdownToTransition(
        timer, time);
  };

  auto result = advance(2, intermediate);
  Expect(result.Timer == 2 && !result.TimerMutated &&
             result.Action == ActorAuthoredCountdownAction::Hold,
         "Actor authored countdown advanced on an intermediate substep");

  result = advance(2, logical);
  Expect(result.Timer == 1 && result.TimerMutated &&
             result.Action == ActorAuthoredCountdownAction::Decrement,
         "Actor authored countdown decrement mismatch");

  result = advance(1, logical);
  Expect(result.Timer == 0 && result.TimerMutated &&
             result.Action == ActorAuthoredCountdownAction::Transition,
         "Actor authored countdown zero-crossing transition mismatch");

  result = advance(0, intermediate);
  Expect(result.Timer == 0 && !result.TimerMutated &&
             result.Action == ActorAuthoredCountdownAction::Hold,
         "Actor authored zero transition was not deferred");

  result = advance(0, logical);
  Expect(result.Timer == 0 && !result.TimerMutated &&
             result.Action == ActorAuthoredCountdownAction::Transition,
         "Actor authored existing-zero transition mismatch");

  result = advance(std::numeric_limits<std::int16_t>::min(), logical);
  Expect(result.Timer == std::numeric_limits<std::int16_t>::max() &&
             result.TimerMutated &&
             result.Action == ActorAuthoredCountdownAction::Decrement,
         "Actor authored countdown did not retain signed halfword wrap");
}

void TestActorAngularOscillatorTiming() {
  using oot3d::gameplay::ActorAngularOscillatorState;
  constexpr std::int16_t kAcceleration = -0xC00;
  constexpr std::int16_t kTerminalVelocity = -0x1200;
  constexpr float kUpdateScale = 0.5F;

  const ActorAngularOscillatorState initial{
      -10000,
      0x1800,
      1U,
  };
  const auto native = oot3d::gameplay::AdvanceActorAngularOscillator(
      initial, false, kAcceleration, kTerminalVelocity, 2.0F, kUpdateScale);
  Expect(native.Valid && !native.GroundReset && !native.VelocityClamped &&
             native.State.Displacement == -3856 &&
             native.State.Velocity == 0xC00 &&
             native.RegisterValue == 0xC00,
         "Actor angular oscillator native-step mismatch");

  const auto firstHalf = oot3d::gameplay::AdvanceActorAngularOscillator(
      initial, false, kAcceleration, kTerminalVelocity, 1.0F, kUpdateScale);
  const auto secondHalf = oot3d::gameplay::AdvanceActorAngularOscillator(
      firstHalf.State, false, kAcceleration, kTerminalVelocity, 1.0F,
      kUpdateScale);
  Expect(firstHalf.Valid && secondHalf.Valid &&
             firstHalf.State.Displacement == -6544 &&
             firstHalf.State.Velocity == 0x1200 &&
             secondHalf.State.Displacement == native.State.Displacement &&
             secondHalf.State.Velocity == native.State.Velocity,
         "Actor angular oscillator half steps changed the native endpoint");

  auto result = oot3d::gameplay::AdvanceActorAngularOscillator(
      {10000, 0x1800, 0U}, false, kAcceleration, kTerminalVelocity, 2.0F,
      kUpdateScale);
  Expect(result.Valid && result.State.Displacement == 3856 &&
             result.State.Velocity == 0xC00,
         "Actor angular oscillator reverse direction mismatch");

  result = oot3d::gameplay::AdvanceActorAngularOscillator(
      {-1000, 500, 1U}, true, kAcceleration, kTerminalVelocity, 2.0F,
      kUpdateScale);
  Expect(result.Valid && result.GroundReset &&
             result.State.Displacement == 0 && result.State.Velocity == 0 &&
             result.RegisterValue == -500,
         "Actor angular oscillator forward ground reset mismatch");

  result = oot3d::gameplay::AdvanceActorAngularOscillator(
      {1000, 500, 0U}, true, kAcceleration, kTerminalVelocity, 2.0F,
      kUpdateScale);
  Expect(result.Valid && result.GroundReset &&
             result.State.Displacement == 0 && result.State.Velocity == 0 &&
             result.RegisterValue == 500,
         "Actor angular oscillator reverse ground reset mismatch");

  result = oot3d::gameplay::AdvanceActorAngularOscillator(
      {-10000, -3000, 1U}, false, kAcceleration, kTerminalVelocity, 2.0F,
      kUpdateScale);
  Expect(result.Valid && !result.GroundReset && result.VelocityClamped &&
             result.State.Velocity == kTerminalVelocity &&
             result.RegisterValue == -6072,
         "Actor angular oscillator terminal clamp mismatch");

  result = oot3d::gameplay::AdvanceActorAngularOscillator(
      initial, false, kAcceleration, kTerminalVelocity, 0.0F, kUpdateScale);
  Expect(!result.Valid && result.State.Displacement == initial.Displacement &&
             result.State.Velocity == initial.Velocity,
         "Actor angular oscillator accepted an invalid rate");
}

void TestCameraCurveSampling(
    const oot3d::gameplay::TimeContext& firstHalf) {
  using oot3d::gameplay::CameraCurveSampleStatus;
  using oot3d::gameplay::CameraCurveType;
  using oot3d::gameplay::CameraCurveView;
  using oot3d::gameplay::CameraHermiteKeyframeWire;
  using oot3d::gameplay::CameraLinearKeyframeWire;

  const std::array linearPoints{
      CameraLinearKeyframeWire{10, 2.0F},
      CameraLinearKeyframeWire{12, 6.0F},
  };
  CameraCurveView linear{
      .Type = CameraCurveType::Linear,
      .PointCount = static_cast<std::int32_t>(linearPoints.size()),
      .LoopEndFrame = 12,
      .PointBytes = std::as_bytes(std::span(linearPoints)),
  };
  float value = 0.0F;
  Expect(oot3d::gameplay::SampleCameraCurve(linear, 9.0F, false, &value) ==
                 CameraCurveSampleStatus::Ok &&
             std::abs(value - 2.0F) < 1.0e-6F,
         "camera linear pre-roll mismatch");
  Expect(oot3d::gameplay::SampleCameraCurve(linear, 11.0F, false, &value) ==
                 CameraCurveSampleStatus::Ok &&
             std::abs(value - 4.0F) < 1.0e-6F,
         "camera linear interpolation mismatch");
  Expect(oot3d::gameplay::SampleCameraCurve(linear, 13.0F, false, &value) ==
                 CameraCurveSampleStatus::Ok &&
             std::abs(value - 6.0F) < 1.0e-6F,
         "camera linear terminal hold mismatch");

  const std::array hermitePoints{
      CameraHermiteKeyframeWire{10, 0.0F, 0.0F, 0.0F},
      CameraHermiteKeyframeWire{12, 10.0F, 0.0F, 0.0F},
  };
  CameraCurveView hermite{
      .Type = CameraCurveType::Hermite,
      .PointCount = static_cast<std::int32_t>(hermitePoints.size()),
      .LoopEndFrame = 12,
      .PointBytes = std::as_bytes(std::span(hermitePoints)),
  };
  Expect(oot3d::gameplay::SampleCameraCurve(hermite, 9.0F, false, &value) ==
                 CameraCurveSampleStatus::Ok &&
             value == 0.0F,
         "camera Hermite pre-roll mismatch");
  Expect(oot3d::gameplay::SampleCameraCurve(hermite, 11.0F, false, &value) ==
                 CameraCurveSampleStatus::Ok &&
             std::abs(value - 5.0F) < 1.0e-6F,
         "camera Hermite interpolation mismatch");

  CameraCurveView step = linear;
  step.Type = CameraCurveType::Step;
  Expect(oot3d::gameplay::SampleCameraCurve(step, 11.0F, false, &value) ==
                 CameraCurveSampleStatus::Ok &&
             std::abs(value - 2.0F) < 1.0e-6F,
         "camera step hold mismatch");
  ExpectNear(oot3d::gameplay::ResolveCameraAnimationContinuousFrame(
                 20, firstHalf),
             20.5, "camera animation continuous frame mismatch");
}

void TestCameraQuakeSignals(
    const oot3d::gameplay::TimeContext& firstHalf,
    const oot3d::gameplay::TimeContext& secondHalf) {
  using oot3d::gameplay::CameraQuakeCallback;
  using oot3d::gameplay::CameraQuakeRequestWire;

  CameraQuakeRequestWire request;
  request.RequestId = 17;
  request.InitialCountdown = 4;
  request.CallbackIndex =
      static_cast<std::uint8_t>(CameraQuakeCallback::SineRandom);
  request.Speed = 0x1000;
  request.Countdown = 4;

  auto plan =
      oot3d::gameplay::ResolveCameraQuakeSignal(request, firstHalf);
  Expect(plan.Supported && plan.Evaluate && plan.NeedsSine &&
             !plan.CountdownMutated && plan.CountdownAfter == 4 &&
             plan.ReturnValue == 4 && plan.SineAngle == 0x4000 &&
             plan.RandomSampleCount == 1,
         "camera quake intermediate sine/random plan mismatch");
  const std::array oneRandom{0.25F};
  auto factors = oot3d::gameplay::ResolveCameraQuakeFactors(
      plan, 0.5F, oneRandom);
  Expect(factors.Supported && std::abs(factors.Primary - 0.5F) < 1.0e-6F &&
             std::abs(factors.Secondary - 0.125F) < 1.0e-6F,
         "camera quake sine/random factors mismatch");

  plan = oot3d::gameplay::ResolveCameraQuakeSignal(request, secondHalf);
  Expect(plan.CountdownMutated && plan.CountdownAfter == 3 &&
             plan.ReturnValue == 3 && plan.SineAngle == 0x4000,
         "camera quake logical countdown mismatch");

  request.CallbackIndex =
      static_cast<std::uint8_t>(CameraQuakeCallback::RandomFade);
  request.Countdown = 2;
  plan = oot3d::gameplay::ResolveCameraQuakeSignal(request, firstHalf);
  const std::array twoRandom{0.25F, 0.5F};
  factors = oot3d::gameplay::ResolveCameraQuakeFactors(
      plan, 0.0F, twoRandom);
  Expect(plan.RandomSampleCount == 2 &&
             std::abs(plan.Envelope - 0.5F) < 1.0e-6F &&
             factors.Supported &&
             std::abs(factors.Primary - 0.125F) < 1.0e-6F &&
             std::abs(factors.Secondary - 0.0625F) < 1.0e-6F,
         "camera quake random fade factors mismatch");

  request.CallbackIndex =
      static_cast<std::uint8_t>(CameraQuakeCallback::PerpetualSineRandom);
  request.Speed = 1;
  request.Countdown = 0;
  plan = oot3d::gameplay::ResolveCameraQuakeSignal(request, firstHalf);
  Expect(plan.Evaluate && !plan.CountdownMutated &&
             plan.CountdownAfter == 0 && plan.SineAngle == 500 &&
             plan.ReturnValue == 1,
         "camera perpetual quake intermediate plan mismatch");
  plan = oot3d::gameplay::ResolveCameraQuakeSignal(request, secondHalf);
  Expect(plan.CountdownMutated && plan.CountdownAfter == -1 &&
             plan.SineAngle == 515 && plan.ReturnValue == 1,
         "camera perpetual quake logical plan mismatch");

  request.CallbackIndex = 0xFFU;
  plan = oot3d::gameplay::ResolveCameraQuakeSignal(request, secondHalf);
  Expect(!plan.Supported && !plan.Evaluate,
         "unsupported camera quake callback was accepted");

  const auto random = oot3d::gameplay::AdvanceCameraQuakeRandom(0U);
  Expect(random.Seed == 0x3C6EF35FU &&
             random.ScratchBits ==
                 (0x3F800000U | (random.Seed >> 9U)) &&
             random.Value >= 0.0F && random.Value < 1.0F,
         "camera quake LCG step mismatch");
}

} // namespace

int main() {
  const auto native30 = oot3d::gameplay::ResolveTimeStep(30U);
  ExpectNear(native30.Seconds, 1.0 / 30.0, "30 Hz seconds mismatch");
  ExpectNear(native30.NativeUpdateRate, 2.0,
             "30 Hz native update-rate mismatch");

  const auto enhanced60 = oot3d::gameplay::ResolveTimeStep(60U);
  ExpectNear(enhanced60.Seconds, 1.0 / 60.0, "60 Hz seconds mismatch");
  ExpectNear(enhanced60.NativeUpdateRate, 1.0,
             "60 Hz native update-rate mismatch");

  oot3d::gameplay::TimeContext firstHalf;
  firstHalf.SimulationTick = 1U;
  firstHalf.DeltaSeconds = 1.0 / 60.0;
  firstHalf.NativeUpdateRate = 1.0F;
  firstHalf.PreviousLogicalFrame = 0.0;
  firstHalf.CurrentLogicalFrame = 0.5;
  firstHalf.LogicalFrameIndex = 0U;
  firstHalf.CrossedLogicalFrame = false;

  std::uint8_t countdown = 2U;
  auto mutation =
      oot3d::gameplay::TickDownIfNonzero(countdown, firstHalf);
  Expect(!mutation.Mutated && countdown == 2U,
         "timer advanced on an intermediate 60 Hz substep");

  auto secondHalf = firstHalf;
  secondHalf.SimulationTick = 2U;
  secondHalf.PreviousLogicalFrame = 0.5;
  secondHalf.CurrentLogicalFrame = 1.0;
  secondHalf.LogicalFrameIndex = 1U;
  secondHalf.CrossedLogicalFrame = true;
  TestActorUpdateAllTiming(firstHalf, secondHalf);
  TestActorBlinkTiming(firstHalf, secondHalf);
  TestActorAuthoredPhaseTiming(firstHalf, secondHalf);
  TestSignedWrappingCountdown(firstHalf, secondHalf);
  TestActorAuthoredRampTiming(firstHalf, secondHalf);
  TestActorAuthoredCountdownTransitionTiming(firstHalf, secondHalf);
  TestActorAngularOscillatorTiming();
  mutation = oot3d::gameplay::TickDownIfNonzero(countdown, secondHalf);
  Expect(mutation.Mutated && !mutation.ReachedZero &&
             mutation.Previous == 2U && mutation.Current == 1U &&
             countdown == 1U,
         "timer did not advance on a logical-frame crossing");

  secondHalf.SimulationTick = 4U;
  secondHalf.PreviousLogicalFrame = 1.5;
  secondHalf.CurrentLogicalFrame = 2.0;
  secondHalf.LogicalFrameIndex = 2U;
  mutation = oot3d::gameplay::TickDownIfNonzero(countdown, secondHalf);
  Expect(mutation.Mutated && mutation.ReachedZero && countdown == 0U,
         "timer zero crossing mismatch");
  mutation = oot3d::gameplay::TickDownIfNonzero(countdown, secondHalf);
  Expect(!mutation.Mutated && countdown == 0U,
         "zero timer underflowed");

  std::uint16_t counter = 3U;
  auto increment =
      oot3d::gameplay::TickUpToLimit(counter, std::uint16_t{4U}, firstHalf);
  Expect(!increment.Mutated && counter == 3U,
         "counter advanced on an intermediate substep");
  increment =
      oot3d::gameplay::TickUpToLimit(counter, std::uint16_t{4U}, secondHalf);
  Expect(increment.Mutated && counter == 4U,
         "counter did not advance at a logical-frame boundary");
  increment =
      oot3d::gameplay::TickUpToLimit(counter, std::uint16_t{4U}, secondHalf);
  Expect(!increment.Mutated && counter == 4U,
         "counter exceeded its native saturation limit");

  auto cameraFloorMiss =
      oot3d::gameplay::AdvanceCameraFloorMissCounter(199U, firstHalf);
  Expect(!cameraFloorMiss.Mutated && cameraFloorMiss.Value == 199U,
         "camera floor-miss counter advanced on an intermediate substep");
  cameraFloorMiss =
      oot3d::gameplay::AdvanceCameraFloorMissCounter(199U, secondHalf);
  Expect(cameraFloorMiss.Mutated && cameraFloorMiss.Value == 200U,
         "camera floor-miss counter did not advance on a logical frame");
  cameraFloorMiss = oot3d::gameplay::AdvanceCameraFloorMissCounter(
      std::numeric_limits<std::uint32_t>::max(), secondHalf);
  Expect(cameraFloorMiss.Mutated && cameraFloorMiss.Value == 0U,
         "camera floor-miss counter did not preserve native u32 wrap");

  auto cameraInterfaceDelay =
      oot3d::gameplay::AdvanceCameraInterfaceDelay(3U, firstHalf);
  Expect(!cameraInterfaceDelay.Mutated && cameraInterfaceDelay.Value == 3U,
         "camera interface delay changed on an intermediate substep");
  cameraInterfaceDelay =
      oot3d::gameplay::AdvanceCameraInterfaceDelay(3U, secondHalf);
  Expect(cameraInterfaceDelay.Mutated && cameraInterfaceDelay.Value == 2U,
         "camera interface delay did not advance on a logical frame");
  cameraInterfaceDelay =
      oot3d::gameplay::AdvanceCameraInterfaceDelay(0U, secondHalf);
  Expect(!cameraInterfaceDelay.Mutated && cameraInterfaceDelay.Value == 0U,
         "camera interface delay underflowed");

  auto cameraModeCountdown =
      oot3d::gameplay::AdvanceCameraModeFrameCountdown(15, firstHalf);
  Expect(!cameraModeCountdown.Mutated && cameraModeCountdown.Value == 15,
         "camera mode countdown advanced on an intermediate substep");
  cameraModeCountdown =
      oot3d::gameplay::AdvanceCameraModeFrameCountdown(15, secondHalf);
  Expect(cameraModeCountdown.Mutated && cameraModeCountdown.Value == 14,
         "camera mode countdown did not advance on a logical frame");
  cameraModeCountdown =
      oot3d::gameplay::AdvanceCameraModeFrameCountdown(0, secondHalf);
  Expect(cameraModeCountdown.Mutated && cameraModeCountdown.Value == -1,
         "camera mode countdown did not preserve native zero underflow");
  cameraModeCountdown = oot3d::gameplay::AdvanceCameraModeFrameCountdown(
      std::numeric_limits<std::int16_t>::min(), secondHalf);
  Expect(cameraModeCountdown.Mutated &&
             cameraModeCountdown.Value ==
                 std::numeric_limits<std::int16_t>::max(),
         "camera mode countdown did not preserve native s16 wrap");

  auto special5Timer =
      oot3d::gameplay::AdvanceCameraSpecial5Timer(3, firstHalf);
  Expect(special5Timer.Value == 3 &&
             special5Timer.Action ==
                 oot3d::gameplay::CameraSpecial5TimerAction::Hold,
         "Camera_Special5 timer advanced on an intermediate substep");
  special5Timer =
      oot3d::gameplay::AdvanceCameraSpecial5Timer(3, secondHalf);
  Expect(special5Timer.Value == 2 &&
             special5Timer.Action ==
                 oot3d::gameplay::CameraSpecial5TimerAction::Decrement,
         "Camera_Special5 positive timer did not advance");
  special5Timer =
      oot3d::gameplay::AdvanceCameraSpecial5Timer(0, firstHalf);
  Expect(special5Timer.Value == 0 &&
             special5Timer.Action ==
                 oot3d::gameplay::CameraSpecial5TimerAction::Hold,
         "Camera_Special5 zero transition was not deferred");
  special5Timer =
      oot3d::gameplay::AdvanceCameraSpecial5Timer(0, secondHalf);
  Expect(special5Timer.Value == 0 &&
             special5Timer.Action ==
                 oot3d::gameplay::CameraSpecial5TimerAction::ZeroTransition,
         "Camera_Special5 zero transition was not released");
  special5Timer =
      oot3d::gameplay::AdvanceCameraSpecial5Timer(-1, firstHalf);
  Expect(special5Timer.Value == -1 &&
             special5Timer.Action ==
                 oot3d::gameplay::CameraSpecial5TimerAction::Terminal,
         "Camera_Special5 negative sentinel was not terminal");

  auto waterDistortionTimer =
      oot3d::gameplay::AdvanceCameraWaterDistortionTimer(80, firstHalf);
  Expect(!waterDistortionTimer.Mutated &&
             waterDistortionTimer.Value == 80,
         "water distortion timer changed on an intermediate substep");
  waterDistortionTimer =
      oot3d::gameplay::AdvanceCameraWaterDistortionTimer(80, secondHalf);
  Expect(waterDistortionTimer.Mutated &&
             waterDistortionTimer.Value == 79,
         "water distortion timer did not advance on a logical frame");
  waterDistortionTimer =
      oot3d::gameplay::AdvanceCameraWaterDistortionTimer(0, secondHalf);
  Expect(!waterDistortionTimer.Mutated &&
             waterDistortionTimer.Value == 0,
         "water distortion timer crossed its native terminal value");
  ExpectNear(oot3d::gameplay::SampleCameraWaterDistortionTimer(80, firstHalf),
             79.5, "water distortion fractional sample mismatch");
  ExpectNear(
      oot3d::gameplay::SampleCameraWaterDistortionTimer(80, secondHalf), 80.0,
      "water distortion logical-boundary sample mismatch");
  TestCameraQuakeSignals(firstHalf, secondHalf);

  std::uint16_t cutsceneFrame = 0U;
  auto cutsceneAdvance =
      oot3d::gameplay::AdvanceCutsceneNormalFrame(cutsceneFrame, firstHalf);
  Expect(!cutsceneAdvance.DispatchDiscreteCommands && cutsceneFrame == 0U,
         "cutscene command stream advanced on an intermediate substep");
  ExpectNear(cutsceneAdvance.Cursor.PreviousFrame, 0.0,
             "cutscene previous subframe mismatch");
  ExpectNear(cutsceneAdvance.Cursor.CurrentFrame, 0.5,
             "cutscene current subframe mismatch");

  cutsceneAdvance =
      oot3d::gameplay::AdvanceCutsceneNormalFrame(cutsceneFrame, secondHalf);
  Expect(cutsceneAdvance.DispatchDiscreteCommands && cutsceneFrame == 1U &&
             cutsceneAdvance.Cursor.CrossedLegacyFrame &&
             cutsceneAdvance.Cursor.LegacyVisibleFrame == 1U,
         "cutscene command stream did not advance on a logical crossing");
  ExpectNear(cutsceneAdvance.Cursor.PreviousFrame, 0.5,
             "cutscene crossing previous frame mismatch");
  ExpectNear(cutsceneAdvance.Cursor.CurrentFrame, 1.0,
             "cutscene crossing current frame mismatch");
  ExpectNear(oot3d::gameplay::ResolveCutsceneContinuousFrame(12U, firstHalf),
             12.5, "cutscene continuous frame sample mismatch");
  ExpectNear(oot3d::gameplay::InterpolateCutsceneCueWeight(10U, 14U, 11.5),
             0.375, "cutscene fractional cue weight mismatch");
  ExpectNear(oot3d::gameplay::InterpolateCutsceneCueWeight(10U, 14U, 16.0),
             1.0, "cutscene cue weight upper clamp mismatch");
  ExpectNear(oot3d::gameplay::InterpolateCutsceneCueWeight(10U, 10U, 10.0),
             1.0, "zero-duration cutscene cue weight mismatch");
  TestCameraCurveSampling(firstHalf);

  auto nativeFrame = secondHalf;
  nativeFrame.PreviousLogicalFrame = 8.0;
  nativeFrame.CurrentLogicalFrame = 9.0;
  cutsceneFrame = 0xFFFFU;
  cutsceneAdvance =
      oot3d::gameplay::AdvanceCutsceneNormalFrame(cutsceneFrame, nativeFrame);
  Expect(cutsceneAdvance.DispatchDiscreteCommands && cutsceneFrame == 0U,
         "cutscene legacy frame did not preserve u16 wrap");
  ExpectNear(cutsceneAdvance.Cursor.CurrentFrame, 65536.0,
             "cutscene unwrapped cursor mismatch");
  return 0;
}
