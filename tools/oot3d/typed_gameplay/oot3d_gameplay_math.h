#pragma once

#include <cstdint>

namespace oot3d::gameplay {

struct AngleTableEntry {
  float Sin = 0.0f;
  float Cos = 0.0f;
  float SinDelta = 0.0f;
  float CosDelta = 0.0f;
};

static_assert(sizeof(AngleTableEntry) == 0x10);

struct AngleSample {
  float Sin = 0.0f;
  float Cos = 0.0f;
};

float InterpolateAngleSin(std::uint16_t angle, float interpolationScale,
                          const AngleTableEntry &entry) noexcept;
float InterpolateAngleCos(std::uint16_t angle, float interpolationScale,
                          const AngleTableEntry &entry) noexcept;

bool StepToF(float &current, float target, float step, float nativeUpdateRate,
             float updateScale, float zero);

float SmoothStepToF(float &current, float target, float fraction,
                    float maximumStep, float minimumStep,
                    float nativeUpdateRate, float updateScale, float epsilon);

void ApproachF(float &current, float target, float fraction, float maximumStep,
               float nativeUpdateRate, float updateScale, float epsilon);

void ApproachZeroF(float &current, float fraction, float maximumStep,
                   float nativeUpdateRate, float updateScale, float epsilon);

bool ScaledStepToS(std::int16_t &current, std::int16_t target,
                   std::int16_t step, float nativeUpdateRate,
                   float updateScale);

bool StepToS(std::int16_t &current, std::int16_t target, std::int16_t step,
             float nativeUpdateRate, float updateScale, float roundingBias);

bool StepToAngleS(std::int16_t &current, std::int16_t target, std::int16_t step,
                  float nativeUpdateRate, float updateScale,
                  float roundingBias);

std::int16_t SmoothStepToSUpdateRate(std::int16_t current, std::int16_t target,
                                     std::int32_t scale,
                                     std::int32_t maximumStep,
                                     float nativeUpdateRate, float updateScale,
                                     float roundingBias);

std::int16_t SmoothStepToS(std::int16_t &current, std::int16_t target,
                           std::int32_t scale, std::int32_t maximumStep,
                           std::int32_t minimumStep, float nativeUpdateRate,
                           float updateScale, float roundingBias);

} // namespace oot3d::gameplay
