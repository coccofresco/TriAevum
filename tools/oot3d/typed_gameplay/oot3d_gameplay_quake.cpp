#include "oot3d_gameplay_quake.h"

#include <bit>

namespace oot3d::gameplay {
namespace {

std::int16_t DecrementWrapping(std::int16_t value) noexcept {
  const auto bits = std::bit_cast<std::uint16_t>(value);
  return std::bit_cast<std::int16_t>(
      static_cast<std::uint16_t>(bits - std::uint16_t{1}));
}

std::int16_t MultiplyWrapping(std::int16_t left,
                              std::int16_t right) noexcept {
  const std::uint32_t product =
      static_cast<std::uint32_t>(std::bit_cast<std::uint16_t>(left)) *
      static_cast<std::uint32_t>(std::bit_cast<std::uint16_t>(right));
  return std::bit_cast<std::int16_t>(
      static_cast<std::uint16_t>(product));
}

} // namespace

CameraQuakeSignalPlan ResolveCameraQuakeSignal(
    const CameraQuakeRequestWire &request, const TimeContext &time) noexcept {
  CameraQuakeSignalPlan plan;
  plan.Callback = static_cast<CameraQuakeCallback>(request.CallbackIndex);
  plan.CountdownBefore = request.Countdown;
  plan.CountdownAfter = request.Countdown;
  plan.ReturnValue = request.Countdown;

  switch (plan.Callback) {
  case CameraQuakeCallback::SineRandom:
    plan.NeedsSine = true;
    plan.RandomSampleCount = 1;
    break;
  case CameraQuakeCallback::Random:
    plan.RandomSampleCount = 2;
    break;
  case CameraQuakeCallback::SineFade:
    plan.NeedsSine = true;
    break;
  case CameraQuakeCallback::RandomFade:
    plan.RandomSampleCount = 2;
    break;
  case CameraQuakeCallback::Sine:
    plan.NeedsSine = true;
    break;
  case CameraQuakeCallback::PerpetualSineRandom: {
    plan.Supported = true;
    plan.Evaluate = true;
    plan.NeedsSine = true;
    plan.RandomSampleCount = 1;
    if (time.CrossedLogicalFrame) {
      plan.CountdownAfter = DecrementWrapping(request.Countdown);
      plan.CountdownMutated = true;
    }
    const auto phase =
        static_cast<std::int16_t>(
            (std::bit_cast<std::uint16_t>(plan.CountdownAfter) & 0xFU) + 500U);
    plan.SineAngle = MultiplyWrapping(request.Speed, phase);
    plan.ReturnValue = 1;
    return plan;
  }
  case CameraQuakeCallback::None:
  default:
    return plan;
  }

  plan.Supported = true;
  if (request.Countdown <= 0) {
    return plan;
  }

  plan.Evaluate = true;
  if (plan.NeedsSine) {
    plan.SineAngle = MultiplyWrapping(request.Speed, request.Countdown);
  }
  if (plan.Callback == CameraQuakeCallback::SineFade ||
      plan.Callback == CameraQuakeCallback::RandomFade) {
    plan.Envelope =
        static_cast<float>(request.Countdown) /
        static_cast<float>(request.InitialCountdown);
  }
  if (time.CrossedLogicalFrame) {
    plan.CountdownAfter = DecrementWrapping(request.Countdown);
    plan.CountdownMutated = true;
  }
  plan.ReturnValue = plan.CountdownAfter;
  return plan;
}

CameraQuakeFactors ResolveCameraQuakeFactors(
    const CameraQuakeSignalPlan &plan, float sine,
    std::span<const float> randomSamples) noexcept {
  CameraQuakeFactors factors;
  if (!plan.Supported || !plan.Evaluate ||
      randomSamples.size() < plan.RandomSampleCount) {
    return factors;
  }

  switch (plan.Callback) {
  case CameraQuakeCallback::SineRandom:
  case CameraQuakeCallback::PerpetualSineRandom:
    factors.Primary = sine;
    factors.Secondary = sine * randomSamples[0];
    break;
  case CameraQuakeCallback::Random:
    factors.Primary = randomSamples[0];
    factors.Secondary = randomSamples[0] * randomSamples[1];
    break;
  case CameraQuakeCallback::SineFade:
    factors.Primary = sine * plan.Envelope;
    factors.Secondary = factors.Primary;
    break;
  case CameraQuakeCallback::RandomFade:
    factors.Primary = randomSamples[0] * plan.Envelope;
    factors.Secondary = factors.Primary * randomSamples[1];
    break;
  case CameraQuakeCallback::Sine:
    factors.Primary = sine;
    factors.Secondary = sine;
    break;
  case CameraQuakeCallback::None:
  default:
    return factors;
  }
  factors.Supported = true;
  return factors;
}

CameraQuakeRandomStep AdvanceCameraQuakeRandom(
    std::uint32_t seed) noexcept {
  CameraQuakeRandomStep step;
  step.Seed = seed * 0x0019660DU + 0x3C6EF35FU;
  step.ScratchBits = 0x3F800000U | (step.Seed >> 9U);
  step.Value = std::bit_cast<float>(step.ScratchBits) - 1.0F;
  return step;
}

} // namespace oot3d::gameplay
