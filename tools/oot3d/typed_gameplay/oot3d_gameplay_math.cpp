#include "oot3d_gameplay_math.h"

#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace oot3d::gameplay {
namespace {

std::int16_t WrapS16(std::int32_t value) noexcept {
  return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value));
}

std::int16_t ScaleSignedStep(std::int32_t step, float nativeUpdateRate,
                             float updateScale, float roundingBias) {
  const float scaled =
      static_cast<float>(step) * nativeUpdateRate * updateScale +
      (step < 1 ? -roundingBias : roundingBias);
  if (!std::isfinite(scaled) ||
      scaled < static_cast<float>(std::numeric_limits<std::int32_t>::min()) ||
      scaled > static_cast<float>(std::numeric_limits<std::int32_t>::max())) {
    throw std::invalid_argument("invalid OOT3D scaled signed step");
  }
  return WrapS16(static_cast<std::int32_t>(scaled));
}

} // namespace

float InterpolateAngleSin(std::uint16_t angle, float interpolationScale,
                          const AngleTableEntry &entry) noexcept {
  const float fraction = static_cast<float>(angle & 0xFFU) * interpolationScale;
  return entry.Sin + fraction * entry.SinDelta;
}

float InterpolateAngleCos(std::uint16_t angle, float interpolationScale,
                          const AngleTableEntry &entry) noexcept {
  const float fraction = static_cast<float>(angle & 0xFFU) * interpolationScale;
  return entry.Cos + fraction * entry.CosDelta;
}

bool StepToF(float &current, float target, float step, float nativeUpdateRate,
             float updateScale, float zero) {
  if (!std::isfinite(current) || !std::isfinite(target) ||
      !std::isfinite(step) || !std::isfinite(nativeUpdateRate) ||
      !std::isfinite(updateScale) || zero != 0.0f || nativeUpdateRate <= 0.0f) {
    throw std::invalid_argument("invalid OOT3D float-step parameters");
  }

  if (step != zero) {
    if (current > target) {
      step = -step;
    }
    const float scaledStep = (nativeUpdateRate * step) * updateScale;
    current += scaledStep;
    if ((current - target) * scaledStep >= zero) {
      current = target;
      return true;
    }
  } else if (current == target) {
    return true;
  }
  return false;
}

float SmoothStepToF(float &current, float target, float fraction,
                    float maximumStep, float minimumStep,
                    float nativeUpdateRate, float updateScale, float epsilon) {
  if (!std::isfinite(current) || !std::isfinite(target) ||
      !std::isfinite(fraction) || !std::isfinite(maximumStep) ||
      !std::isfinite(minimumStep) || !std::isfinite(nativeUpdateRate) ||
      !std::isfinite(updateScale) || !std::isfinite(epsilon) ||
      nativeUpdateRate <= 0.0f || epsilon < 0.0f) {
    throw std::invalid_argument("invalid OOT3D float smooth-step parameters");
  }

  if (current != target) {
    const float scaledMinimumStep =
        (nativeUpdateRate * minimumStep) * updateScale;
    const float scaledMaximumStep =
        (nativeUpdateRate * maximumStep) * updateScale;
    const float scaledFraction = (nativeUpdateRate * fraction) * updateScale;
    const float difference = target - current;
    float step = difference * scaledFraction;
    if (std::fabs(difference) < epsilon) {
      step = difference;
    }

    if (step >= scaledMinimumStep || step <= -scaledMinimumStep) {
      if (step > scaledMaximumStep) {
        step = scaledMaximumStep;
      }
      if (step < -scaledMaximumStep) {
        step = -scaledMaximumStep;
      }
      current += step;
    } else {
      if (step < scaledMinimumStep) {
        current += scaledMinimumStep;
        step = scaledMinimumStep;
        if (current > target) {
          current = target;
        }
      }
      if (step > -scaledMinimumStep) {
        current -= scaledMinimumStep;
        if (current < target) {
          current = target;
        }
      }
    }
  }
  return std::fabs(target - current);
}

void ApproachF(float &current, float target, float fraction, float maximumStep,
               float nativeUpdateRate, float updateScale, float epsilon) {
  if (!std::isfinite(current) || !std::isfinite(target) ||
      !std::isfinite(fraction) || !std::isfinite(maximumStep) ||
      !std::isfinite(nativeUpdateRate) || !std::isfinite(updateScale) ||
      !std::isfinite(epsilon) || nativeUpdateRate <= 0.0f || epsilon < 0.0f) {
    throw std::invalid_argument("invalid OOT3D float approach parameters");
  }
  if (current == target) {
    return;
  }

  const float difference = target - current;
  const float scaledFraction = (nativeUpdateRate * fraction) * updateScale;
  const float scaledMaximumStep =
      (nativeUpdateRate * maximumStep) * updateScale;
  float step = difference * scaledFraction;
  if (std::fabs(difference) < epsilon) {
    step = difference;
  }
  if (step > scaledMaximumStep) {
    step = scaledMaximumStep;
  } else if (step < -scaledMaximumStep) {
    step = -scaledMaximumStep;
  }
  current += step;
}

void ApproachZeroF(float &current, float fraction, float maximumStep,
                   float nativeUpdateRate, float updateScale, float epsilon) {
  if (!std::isfinite(current) || !std::isfinite(fraction) ||
      !std::isfinite(maximumStep) || !std::isfinite(nativeUpdateRate) ||
      !std::isfinite(updateScale) || !std::isfinite(epsilon) ||
      nativeUpdateRate <= 0.0f || epsilon < 0.0f) {
    throw std::invalid_argument("invalid OOT3D zero-approach parameters");
  }

  const float scaledMaximumStep =
      (nativeUpdateRate * maximumStep) * updateScale;
  const float scaledFraction = (nativeUpdateRate * fraction) * updateScale;
  float step = current * scaledFraction;
  if (std::fabs(current) < epsilon) {
    step = current;
  }
  if (step > scaledMaximumStep) {
    step = scaledMaximumStep;
  } else if (step < -scaledMaximumStep) {
    step = -scaledMaximumStep;
  }
  current -= step;
}

bool ScaledStepToS(std::int16_t &current, std::int16_t target,
                   std::int16_t step, float nativeUpdateRate,
                   float updateScale) {
  if (!std::isfinite(nativeUpdateRate) || !std::isfinite(updateScale) ||
      nativeUpdateRate <= 0.0f) {
    throw std::invalid_argument("invalid OOT3D scaled-step parameters");
  }
  if (step != 0) {
    const std::int16_t difference = WrapS16(static_cast<std::int32_t>(current) -
                                            static_cast<std::int32_t>(target));
    if (difference > 0) {
      step = WrapS16(-static_cast<std::int32_t>(step));
    }
    const float scaled =
        static_cast<float>(step) * (nativeUpdateRate * updateScale);
    if (scaled < static_cast<float>(std::numeric_limits<std::int32_t>::min()) ||
        scaled > static_cast<float>(std::numeric_limits<std::int32_t>::max())) {
      throw std::invalid_argument("OOT3D scaled-step overflow");
    }
    const std::int16_t delta = WrapS16(static_cast<std::int32_t>(scaled));
    current = WrapS16(static_cast<std::int32_t>(current) + delta);
    const std::int32_t remaining = WrapS16(static_cast<std::int32_t>(current) -
                                           static_cast<std::int32_t>(target));
    if (remaining * static_cast<std::int32_t>(step) >= 0) {
      current = target;
      return true;
    }
  } else if (current == target) {
    return true;
  }
  return false;
}

bool StepToS(std::int16_t &current, std::int16_t target, std::int16_t step,
             float nativeUpdateRate, float updateScale, float roundingBias) {
  if (!std::isfinite(nativeUpdateRate) || !std::isfinite(updateScale) ||
      !std::isfinite(roundingBias) || nativeUpdateRate <= 0.0f) {
    throw std::invalid_argument("invalid OOT3D signed-step parameters");
  }
  if (step == 0) {
    return current == target;
  }
  if (current > target) {
    step = WrapS16(-static_cast<std::int32_t>(step));
  }

  const float scaled =
      (nativeUpdateRate * static_cast<float>(step)) * updateScale +
      (step > 0 ? roundingBias : -roundingBias);
  if (scaled < static_cast<float>(std::numeric_limits<std::int32_t>::min()) ||
      scaled > static_cast<float>(std::numeric_limits<std::int32_t>::max())) {
    throw std::invalid_argument("OOT3D signed-step overflow");
  }
  const std::int16_t delta = WrapS16(static_cast<std::int32_t>(scaled));
  current = WrapS16(static_cast<std::int32_t>(current) + delta);
  const std::int32_t remaining =
      static_cast<std::int32_t>(current) - static_cast<std::int32_t>(target);
  if (remaining * static_cast<std::int32_t>(delta) >= 0) {
    current = target;
    return true;
  }
  return false;
}

bool StepToAngleS(std::int16_t &current, std::int16_t target, std::int16_t step,
                  float nativeUpdateRate, float updateScale,
                  float roundingBias) {
  if (!std::isfinite(nativeUpdateRate) || !std::isfinite(updateScale) ||
      !std::isfinite(roundingBias) || nativeUpdateRate <= 0.0f) {
    throw std::invalid_argument("invalid OOT3D angle-step parameters");
  }

  std::int32_t difference = static_cast<std::int32_t>(target) - current;
  if (difference < 0) {
    step = WrapS16(-static_cast<std::int32_t>(step));
  }
  if (difference >= 0x8000) {
    step = WrapS16(-static_cast<std::int32_t>(step));
    difference -= 0xFFFF;
  } else if (difference <= -0x8000) {
    step = WrapS16(-static_cast<std::int32_t>(step));
    difference += 0xFFFF;
  }

  if (step == 0) {
    return current == target;
  }
  const float scaled =
      (nativeUpdateRate * static_cast<float>(step)) * updateScale +
      (step > 0 ? roundingBias : -roundingBias);
  if (scaled < static_cast<float>(std::numeric_limits<std::int32_t>::min()) ||
      scaled > static_cast<float>(std::numeric_limits<std::int32_t>::max())) {
    throw std::invalid_argument("OOT3D angle-step overflow");
  }
  const std::int16_t delta = WrapS16(static_cast<std::int32_t>(scaled));
  current = WrapS16(static_cast<std::int32_t>(current) + delta);
  if (difference * static_cast<std::int32_t>(delta) <= 0) {
    current = target;
    return true;
  }
  return false;
}

std::int16_t SmoothStepToSUpdateRate(std::int16_t current, std::int16_t target,
                                     std::int32_t scale,
                                     std::int32_t maximumStep,
                                     float nativeUpdateRate, float updateScale,
                                     float roundingBias) {
  if (scale == 0 || !std::isfinite(nativeUpdateRate) ||
      !std::isfinite(updateScale) || !std::isfinite(roundingBias)) {
    throw std::invalid_argument("invalid OOT3D smooth-step parameters");
  }

  const std::uint16_t differenceBits =
      static_cast<std::uint16_t>(target) - static_cast<std::uint16_t>(current);
  const std::int16_t difference = std::bit_cast<std::int16_t>(differenceBits);
  const std::int16_t divided =
      WrapS16(static_cast<std::int32_t>(difference) / scale);
  const std::int32_t divided32 = divided;

  std::int16_t step = 0;
  if (maximumStep < divided32) {
    step = ScaleSignedStep(maximumStep, nativeUpdateRate, updateScale,
                           roundingBias);
  } else if (divided32 < -static_cast<std::int64_t>(maximumStep)) {
    step = WrapS16(-static_cast<std::int32_t>(ScaleSignedStep(
        maximumStep, nativeUpdateRate, updateScale, roundingBias)));
  } else {
    step =
        ScaleSignedStep(divided32, nativeUpdateRate, updateScale, roundingBias);
  }
  return WrapS16(static_cast<std::int32_t>(current) + step);
}

std::int16_t SmoothStepToS(std::int16_t &current, std::int16_t target,
                           std::int32_t scale, std::int32_t maximumStep,
                           std::int32_t minimumStep, float nativeUpdateRate,
                           float updateScale, float roundingBias) {
  if (scale == 0 || !std::isfinite(nativeUpdateRate) ||
      !std::isfinite(updateScale) || !std::isfinite(roundingBias) ||
      nativeUpdateRate <= 0.0f) {
    throw std::invalid_argument("invalid OOT3D signed smooth-step parameters");
  }

  const std::uint16_t differenceBits =
      static_cast<std::uint16_t>(target) - static_cast<std::uint16_t>(current);
  const std::int16_t difference = std::bit_cast<std::int16_t>(differenceBits);
  if (current == target) {
    return difference;
  }

  const std::int16_t divided =
      WrapS16(static_cast<std::int32_t>(difference) / scale);
  std::int32_t step = divided;
  if (step > minimumStep || static_cast<std::int64_t>(step) <
                                -static_cast<std::int64_t>(minimumStep)) {
    if (step > maximumStep) {
      step = maximumStep;
    }
    if (static_cast<std::int64_t>(step) <
        -static_cast<std::int64_t>(maximumStep)) {
      step = -maximumStep;
    }
    const std::int16_t scaledStep =
        ScaleSignedStep(step, nativeUpdateRate, updateScale, roundingBias);
    current = WrapS16(static_cast<std::int32_t>(current) + scaledStep);
    return difference;
  }

  const std::int16_t scaledMinimum =
      ScaleSignedStep(minimumStep, nativeUpdateRate, updateScale, roundingBias);
  if (difference >= 0) {
    current = WrapS16(static_cast<std::int32_t>(current) + scaledMinimum);
    if (WrapS16(static_cast<std::int32_t>(target) - current) <= 0) {
      current = target;
    }
  } else {
    current = WrapS16(static_cast<std::int32_t>(current) - scaledMinimum);
    if (WrapS16(static_cast<std::int32_t>(target) - current) >= 0) {
      current = target;
    }
  }
  return difference;
}

} // namespace oot3d::gameplay
