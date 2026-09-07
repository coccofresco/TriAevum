#pragma once

#include <cstdint>

namespace oot3d::gameplay {

struct AnimationChangeState {
  std::int32_t AnimationIndex = 0;
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

struct AnimationChangeRequest {
  std::int32_t AnimationIndex = 0;
  float PlaySpeed = 0.0f;
  float StartFrame = 0.0f;
  float EndFrame = 0.0f;
  float MorphFrames = 0.0f;
  std::int32_t AnimationFrameCount = 0;
  std::int8_t MorphTaper = 0;
  std::uint8_t AnimationMode = 0;
};

struct AnimationChangeConstants {
  float Zero = 0.0f;
  float One = 0.0f;
};

struct AnimationOnFrameConstants {
  float Half = 0.0f;
  float Zero = 0.0f;
  float QuantizeScale = 0.0f;
  float QuantizeInverse = 0.0f;
};

struct AnimationFrameCrossingConstants {
  float Zero = 0.0f;
};

enum class AnimationChangePoseEffect : std::uint8_t {
  SampleJointPose,
  SampleMorphPose,
  CopyJointPoseToMorph,
};

enum class LinkAnimationChangePoseEffect : std::uint8_t {
  QueueJointPose,
  QueueMorphPose,
  CopyJointPoseToMorph,
};

struct AnimationChangePlan {
  AnimationChangePoseEffect PoseEffect =
      AnimationChangePoseEffect::SampleJointPose;
  float SampleFrame = 0.0f;
  bool Supported = false;
};

struct LinkAnimationChangePlan {
  LinkAnimationChangePoseEffect PoseEffect =
      LinkAnimationChangePoseEffect::QueueJointPose;
  float SampleFrame = 0.0f;
  bool Supported = false;
};

std::uint8_t ResolveSkelAnimeUpdateMode(std::uint8_t animationMode) noexcept;

AnimationChangePlan
ApplyAnimationChange(AnimationChangeState &state,
                     const AnimationChangeRequest &request,
                     const AnimationChangeConstants &constants) noexcept;

LinkAnimationChangePlan
ApplyLinkAnimationChange(AnimationChangeState &state,
                         const AnimationChangeRequest &request,
                         const AnimationChangeConstants &constants) noexcept;

bool IsAnimationOnFrame(float currentFrame, float playSpeed,
                        float animationLength, float frame,
                        float nativeUpdateRate,
                        const AnimationOnFrameConstants &constants);
bool IsAnimationFrameCrossed(float currentFrame, float playSpeed,
                             float animationLength, float frame,
                             float updateScale,
                             const AnimationFrameCrossingConstants &constants);

} // namespace oot3d::gameplay
