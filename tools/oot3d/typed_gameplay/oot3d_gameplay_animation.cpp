#include "oot3d_gameplay_animation.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace oot3d::gameplay {

std::uint8_t ResolveSkelAnimeUpdateMode(std::uint8_t animationMode) noexcept {
  if (animationMode < 2U) {
    return 4U;
  }
  if (animationMode < 4U) {
    return 6U;
  }
  return 5U;
}

AnimationChangePlan
ApplyAnimationChange(AnimationChangeState &state,
                     const AnimationChangeRequest &request,
                     const AnimationChangeConstants &constants) noexcept {
  AnimationChangePlan plan;
  if (!std::isfinite(state.CurrentFrame) || !std::isfinite(request.PlaySpeed) ||
      !std::isfinite(request.StartFrame) || !std::isfinite(request.EndFrame) ||
      !std::isfinite(request.MorphFrames) || !std::isfinite(constants.Zero) ||
      !std::isfinite(constants.One) || constants.Zero != 0.0f ||
      constants.One == 0.0f) {
    return plan;
  }

  state.AnimationMode = request.AnimationMode;
  plan.SampleFrame = request.StartFrame;
  const bool changesPose = state.AnimationIndex != request.AnimationIndex ||
                           state.CurrentFrame != request.StartFrame;
  if (request.MorphFrames != constants.Zero && changesPose) {
    state.MorphWeight = constants.One;
    if (request.MorphFrames < constants.Zero) {
      state.UpdateMode = ResolveSkelAnimeUpdateMode(request.AnimationMode);
      state.MorphRate = constants.One / -request.MorphFrames;
      plan.PoseEffect = AnimationChangePoseEffect::CopyJointPoseToMorph;
    } else {
      state.UpdateMode = request.MorphTaper == 0 ? 7U : 8U;
      if (request.MorphTaper != 0) {
        state.MorphTaper = request.MorphTaper;
      }
      state.MorphRate = constants.One / request.MorphFrames;
      plan.PoseEffect = AnimationChangePoseEffect::SampleMorphPose;
    }
  } else {
    state.UpdateMode = ResolveSkelAnimeUpdateMode(request.AnimationMode);
    state.MorphWeight = constants.Zero;
    plan.PoseEffect = AnimationChangePoseEffect::SampleJointPose;
  }

  state.AnimationIndex = request.AnimationIndex;
  state.StartFrame = request.StartFrame;
  state.EndFrame = request.EndFrame;
  state.AnimationLength =
      static_cast<float>(request.AnimationFrameCount) + constants.One;
  if (request.AnimationMode < 4U) {
    state.CurrentFrame = request.StartFrame;
    if (request.AnimationMode < 2U) {
      state.EndFrame = state.AnimationLength - constants.One;
    }
  } else {
    state.CurrentFrame = constants.Zero;
  }
  state.PlaySpeed = request.PlaySpeed;
  plan.Supported =
      std::isfinite(state.MorphRate) && std::isfinite(state.AnimationLength);
  return plan;
}

LinkAnimationChangePlan
ApplyLinkAnimationChange(AnimationChangeState &state,
                         const AnimationChangeRequest &request,
                         const AnimationChangeConstants &constants) noexcept {
  LinkAnimationChangePlan plan;
  if (!std::isfinite(state.CurrentFrame) || !std::isfinite(request.PlaySpeed) ||
      !std::isfinite(request.StartFrame) || !std::isfinite(request.EndFrame) ||
      !std::isfinite(request.MorphFrames) || !std::isfinite(constants.Zero) ||
      !std::isfinite(constants.One) || constants.Zero != 0.0f ||
      constants.One == 0.0f) {
    return plan;
  }

  state.AnimationMode = request.AnimationMode;
  plan.SampleFrame = request.StartFrame;
  const bool changesPose = state.AnimationIndex != request.AnimationIndex ||
                           state.CurrentFrame != request.StartFrame;
  if (request.MorphFrames != constants.Zero && changesPose) {
    state.MorphWeight = constants.One;
    state.MorphRate = constants.One / std::abs(request.MorphFrames);
    if (request.MorphFrames < constants.Zero) {
      state.UpdateMode = request.AnimationMode < 2U ? 1U : 2U;
      plan.PoseEffect = LinkAnimationChangePoseEffect::CopyJointPoseToMorph;
    } else {
      state.UpdateMode = 3U;
      plan.PoseEffect = LinkAnimationChangePoseEffect::QueueMorphPose;
    }
  } else {
    state.UpdateMode = request.AnimationMode < 2U ? 1U : 2U;
    state.MorphWeight = constants.Zero;
    plan.PoseEffect = LinkAnimationChangePoseEffect::QueueJointPose;
  }

  state.AnimationIndex = request.AnimationIndex;
  state.CurrentFrame = request.StartFrame;
  state.StartFrame = request.StartFrame;
  state.EndFrame = request.EndFrame;
  state.AnimationLength = static_cast<float>(request.AnimationFrameCount + 1);
  state.PlaySpeed = request.PlaySpeed;
  plan.Supported =
      std::isfinite(state.MorphRate) && std::isfinite(state.AnimationLength);
  return plan;
}

bool IsAnimationOnFrame(float currentFrame, float playSpeed,
                        float animationLength, float frame,
                        float nativeUpdateRate,
                        const AnimationOnFrameConstants &constants) {
  if (!std::isfinite(currentFrame) || !std::isfinite(playSpeed) ||
      !std::isfinite(animationLength) || !std::isfinite(frame) ||
      !std::isfinite(nativeUpdateRate) || !std::isfinite(constants.Half) ||
      !std::isfinite(constants.Zero) ||
      !std::isfinite(constants.QuantizeScale) ||
      !std::isfinite(constants.QuantizeInverse) || nativeUpdateRate <= 0.0f ||
      constants.Zero != 0.0f || constants.QuantizeScale <= 0.0f ||
      constants.QuantizeInverse <= 0.0f) {
    throw std::invalid_argument("invalid OOT3D on-frame parameters");
  }

  const float rateScale = nativeUpdateRate * constants.Half;
  const float delta = playSpeed * rateScale;
  float previousFrame = currentFrame - delta;
  if (previousFrame < constants.Zero) {
    previousFrame = animationLength + previousFrame;
  } else if (animationLength <= previousFrame) {
    previousFrame -= animationLength;
  }
  if (frame == constants.Zero && delta > constants.Zero) {
    frame = animationLength;
  }

  const auto quantize = [&](float value) {
    const float scaled = value * constants.QuantizeScale;
    if (scaled < static_cast<float>(std::numeric_limits<std::int32_t>::min()) ||
        scaled > static_cast<float>(std::numeric_limits<std::int32_t>::max())) {
      throw std::invalid_argument("OOT3D on-frame quantization overflow");
    }
    return static_cast<float>(static_cast<std::int32_t>(scaled)) *
           constants.QuantizeInverse;
  };
  const float difference = quantize((previousFrame + delta) - frame);
  const float quantizedDelta = quantize(delta);
  return difference * quantizedDelta >= constants.Zero &&
         (difference - quantizedDelta) * quantizedDelta < constants.Zero;
}

bool IsAnimationFrameCrossed(float currentFrame, float playSpeed,
                             float animationLength, float frame,
                             float updateScale,
                             const AnimationFrameCrossingConstants &constants) {
  if (!std::isfinite(currentFrame) || !std::isfinite(playSpeed) ||
      !std::isfinite(animationLength) || !std::isfinite(frame) ||
      !std::isfinite(updateScale) || !std::isfinite(constants.Zero) ||
      constants.Zero != 0.0f) {
    throw std::invalid_argument("invalid OOT3D frame-crossing parameters");
  }

  const float delta = playSpeed * updateScale;
  float previousFrame = currentFrame - delta;
  if (previousFrame < constants.Zero) {
    previousFrame = animationLength + previousFrame;
  } else if (animationLength <= previousFrame) {
    previousFrame -= animationLength;
  }
  if (frame == constants.Zero && delta > constants.Zero) {
    frame = animationLength;
  }

  const float difference = (previousFrame + delta) - frame;
  return difference * delta >= constants.Zero &&
         (difference - delta) * delta < constants.Zero;
}

} // namespace oot3d::gameplay
