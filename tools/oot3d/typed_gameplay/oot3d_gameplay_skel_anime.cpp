#include "oot3d_gameplay_skel_anime.h"

#include <cmath>
#include <limits>

namespace oot3d::gameplay {
namespace {

bool IsFinite(const SkelAnimePlaybackState &state,
              const SkelAnimeUpdateConstants &constants,
              float nativeUpdateRate) noexcept {
  return std::isfinite(state.MorphWeight) && std::isfinite(state.MorphRate) &&
         std::isfinite(state.CurrentFrame) && std::isfinite(state.PlaySpeed) &&
         std::isfinite(state.StartFrame) && std::isfinite(state.EndFrame) &&
         std::isfinite(state.AnimationLength) &&
         std::isfinite(nativeUpdateRate) && nativeUpdateRate > 0.0f &&
         std::isfinite(constants.LegacyUpdateScale) &&
         std::isfinite(constants.DirectUpdateScale) &&
         std::isfinite(constants.TaperAngleScale) &&
         std::isfinite(constants.TaperUpdateScale) &&
         std::isfinite(constants.TaperZero) &&
         std::isfinite(constants.TaperOne) && std::isfinite(constants.Zero) &&
         std::isfinite(constants.One);
}

float ScaledUpdate(float nativeUpdateRate, float scale) noexcept {
  return nativeUpdateRate * scale;
}

std::uint8_t ResolveDirectUpdateMode(std::uint8_t animationMode) noexcept {
  if (animationMode < 2U) {
    return 4U;
  }
  return animationMode < 4U ? 6U : 5U;
}

void AdvanceLoopFrame(SkelAnimePlaybackState &state, float updateScale,
                      float zero) noexcept {
  state.CurrentFrame += state.PlaySpeed * updateScale;
  if (state.CurrentFrame < zero) {
    state.CurrentFrame += state.AnimationLength;
  } else if (state.CurrentFrame >= state.AnimationLength) {
    state.CurrentFrame -= state.AnimationLength;
  }
}

void AdvancePartialLoopFrame(SkelAnimePlaybackState &state,
                             float updateScale) noexcept {
  state.CurrentFrame += state.PlaySpeed * updateScale;
  if (state.CurrentFrame < state.StartFrame) {
    state.CurrentFrame =
        (state.CurrentFrame - state.StartFrame) + state.EndFrame;
  } else if (state.CurrentFrame >= state.EndFrame) {
    state.CurrentFrame =
        (state.CurrentFrame - state.EndFrame) + state.StartFrame;
  }
}

void AdvanceOnceFrame(SkelAnimePlaybackState &state, float updateScale,
                      float zero) noexcept {
  state.CurrentFrame += state.PlaySpeed * updateScale;
  if ((state.CurrentFrame - state.EndFrame) * state.PlaySpeed > zero) {
    state.CurrentFrame = state.EndFrame;
    return;
  }
  if (state.CurrentFrame < zero) {
    state.CurrentFrame += state.AnimationLength;
  } else if (state.CurrentFrame >= state.AnimationLength) {
    state.CurrentFrame -= state.AnimationLength;
  }
}

void AdvanceMorph(SkelAnimePlaybackState &state, float updateScale,
                  float zero) noexcept {
  state.MorphWeight -= state.MorphRate * updateScale;
  if (state.MorphWeight <= zero) {
    state.MorphWeight = zero;
  }
}

} // namespace

SkelAnimeUpdatePlan
AdvanceSkelAnimePlayback(SkelAnimePlaybackState &state, float nativeUpdateRate,
                         const SkelAnimeUpdateConstants &constants) noexcept {
  SkelAnimeUpdatePlan plan;
  if (!IsFinite(state, constants, nativeUpdateRate)) {
    return plan;
  }

  plan.Supported = true;
  const float legacyUpdate =
      ScaledUpdate(nativeUpdateRate, constants.LegacyUpdateScale);
  const float directUpdate =
      ScaledUpdate(nativeUpdateRate, constants.DirectUpdateScale);
  const float taperUpdate =
      ScaledUpdate(nativeUpdateRate, constants.TaperUpdateScale);

  switch (state.UpdateMode) {
  case 0U:
    break;
  case 1U:
    AdvanceLoopFrame(state, legacyUpdate, constants.Zero);
    plan.PoseEffect = SkelAnimePoseEffect::LegacySample;
    plan.WritesCurrentFrame = true;
    break;
  case 2U:
    plan.PoseEffect = SkelAnimePoseEffect::LegacySample;
    if (state.CurrentFrame == state.EndFrame) {
      plan.Complete = true;
      break;
    }
    AdvanceOnceFrame(state, legacyUpdate, constants.Zero);
    plan.WritesCurrentFrame = true;
    break;
  case 3U:
    plan.PreviousMorphWeight = state.MorphWeight;
    AdvanceMorph(state, legacyUpdate, constants.Zero);
    plan.PoseEffect = SkelAnimePoseEffect::LegacyBlend;
    plan.WritesMorphWeight = true;
    if (state.MorphWeight <= constants.Zero) {
      state.UpdateMode = state.AnimationMode < 2U ? 1U : 2U;
      plan.WritesUpdateMode = true;
    }
    break;
  case 4U:
    AdvanceLoopFrame(state, directUpdate, constants.Zero);
    plan.PoseEffect = SkelAnimePoseEffect::DirectSample;
    plan.WritesCurrentFrame = true;
    break;
  case 5U:
    AdvancePartialLoopFrame(state, directUpdate);
    plan.PoseEffect = SkelAnimePoseEffect::DirectSample;
    plan.WritesCurrentFrame = true;
    break;
  case 6U:
    if (state.CurrentFrame == state.EndFrame) {
      plan.PoseEffect = SkelAnimePoseEffect::DirectTerminalSample;
      plan.Complete = true;
      break;
    }
    AdvanceOnceFrame(state, directUpdate, constants.Zero);
    plan.PoseEffect = SkelAnimePoseEffect::DirectSample;
    plan.WritesCurrentFrame = true;
    break;
  case 7U:
    plan.PreviousMorphWeight = state.MorphWeight;
    AdvanceMorph(state, directUpdate, constants.Zero);
    plan.PoseEffect = SkelAnimePoseEffect::DirectLinearBlend;
    plan.WritesMorphWeight = true;
    if (state.MorphWeight <= constants.Zero) {
      state.UpdateMode = ResolveDirectUpdateMode(state.AnimationMode);
      plan.WritesUpdateMode = true;
    }
    break;
  case 8U:
    plan.PreviousMorphWeight = state.MorphWeight;
    AdvanceMorph(state, taperUpdate, constants.TaperZero);
    plan.PoseEffect = SkelAnimePoseEffect::DirectTaperedBlend;
    plan.WritesMorphWeight = true;
    if (state.MorphWeight <= constants.TaperZero) {
      state.UpdateMode = ResolveDirectUpdateMode(state.AnimationMode);
      plan.WritesUpdateMode = true;
    }
    break;
  default:
    break;
  }
  return plan;
}

SkelAnimeMorphStep AdvanceSkelAnimeMorphWeight(float &morphWeight,
                                               float morphRate,
                                               float nativeUpdateRate,
                                               float updateScale,
                                               float zero) noexcept {
  SkelAnimeMorphStep step;
  if (!std::isfinite(morphWeight) || !std::isfinite(morphRate) ||
      !std::isfinite(nativeUpdateRate) || nativeUpdateRate <= 0.0f ||
      !std::isfinite(updateScale) || !std::isfinite(zero)) {
    return step;
  }
  step.Supported = true;
  if (morphWeight == zero) {
    return step;
  }

  step.Active = true;
  step.WritesMorphWeight = true;
  morphWeight -= morphRate * ScaledUpdate(nativeUpdateRate, updateScale);
  if (morphWeight > zero) {
    step.BlendPose = true;
  } else {
    morphWeight = zero;
  }
  return step;
}

float ResolveLinearMorphBlend(float previousMorphWeight,
                              float currentMorphWeight, float one) noexcept {
  return one - currentMorphWeight / previousMorphWeight;
}

std::int16_t ResolveTaperAngle(float morphWeight, float angleScale) noexcept {
  const float scaled = morphWeight * angleScale;
  const double widened = static_cast<double>(scaled);
  if (!std::isfinite(scaled) ||
      widened > static_cast<double>(std::numeric_limits<std::int32_t>::max()) ||
      widened < static_cast<double>(std::numeric_limits<std::int32_t>::min())) {
    return 0;
  }
  return static_cast<std::int16_t>(static_cast<std::int32_t>(scaled));
}

float ResolveTaperedMorphBlend(float previousCurveValue,
                               float currentCurveValue, float zero,
                               float one) noexcept {
  return one -
         ResolveTaperedMorphRatio(previousCurveValue, currentCurveValue, zero);
}

float ResolveTaperedMorphRatio(float previousCurveValue,
                               float currentCurveValue, float zero) noexcept {
  return currentCurveValue != zero ? currentCurveValue / previousCurveValue
                                   : zero;
}

} // namespace oot3d::gameplay
