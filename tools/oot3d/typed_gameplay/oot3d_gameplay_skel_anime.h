#pragma once

#include <cstdint>

namespace oot3d::gameplay {

struct SkelAnimePlaybackState {
  float MorphWeight = 0.0f;
  float MorphRate = 0.0f;
  float CurrentFrame = 0.0f;
  float PlaySpeed = 0.0f;
  float StartFrame = 0.0f;
  float EndFrame = 0.0f;
  float AnimationLength = 0.0f;
  std::int8_t MorphTaper = 0;
  std::uint8_t AnimationMode = 0;
  std::uint8_t UpdateMode = 0;
};

struct SkelAnimeUpdateConstants {
  float LegacyUpdateScale = 0.0f;
  float DirectUpdateScale = 0.0f;
  float TaperAngleScale = 0.0f;
  float TaperUpdateScale = 0.0f;
  float TaperZero = 0.0f;
  float TaperOne = 0.0f;
  float Zero = 0.0f;
  float One = 0.0f;
};

enum class SkelAnimePoseEffect : std::uint8_t {
  None,
  LegacySample,
  LegacyBlend,
  DirectSample,
  DirectTerminalSample,
  DirectLinearBlend,
  DirectTaperedBlend,
};

struct SkelAnimeUpdatePlan {
  SkelAnimePoseEffect PoseEffect = SkelAnimePoseEffect::None;
  float PreviousMorphWeight = 0.0f;
  bool Supported = false;
  bool Complete = false;
  bool WritesMorphWeight = false;
  bool WritesCurrentFrame = false;
  bool WritesUpdateMode = false;
};

struct SkelAnimeMorphStep {
  bool Supported = false;
  bool Active = false;
  bool BlendPose = false;
  bool WritesMorphWeight = false;
};

SkelAnimeUpdatePlan
AdvanceSkelAnimePlayback(SkelAnimePlaybackState &state, float nativeUpdateRate,
                         const SkelAnimeUpdateConstants &constants) noexcept;

SkelAnimeMorphStep AdvanceSkelAnimeMorphWeight(float &morphWeight,
                                               float morphRate,
                                               float nativeUpdateRate,
                                               float updateScale,
                                               float zero) noexcept;

float ResolveLinearMorphBlend(float previousMorphWeight,
                              float currentMorphWeight, float one) noexcept;

std::int16_t ResolveTaperAngle(float morphWeight, float angleScale) noexcept;

float ResolveTaperedMorphBlend(float previousCurveValue,
                               float currentCurveValue, float zero,
                               float one) noexcept;

float ResolveTaperedMorphRatio(float previousCurveValue,
                               float currentCurveValue, float zero) noexcept;

} // namespace oot3d::gameplay
