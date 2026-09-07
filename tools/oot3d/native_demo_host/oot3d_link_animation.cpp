#include "oot3d_link_animation.h"

#include <algorithm>
#include <cstddef>
#include <string>

#include "oot3d_demo_math.h"

namespace {

double NativeWalkFramesPerSecond(const LinkMotionState& motion, const LinkNativeLocomotionConfig& config) {
    const double linearVelocity =
        std::clamp(motion.NativeLinearVelocityUnitsPerTick, 0.0, config.WalkRunThresholdUnitsPerTick);
    const double playbackScale =
        config.WalkAnimationBaseScale + (config.WalkAnimationVelocityScalePerUnit * linearVelocity);
    return config.CharacterFramesPerSecond * std::max(0.0, playbackScale);
}

struct NativeLocomotionAnimationStep {
    double CycleFramesPerSecond = 0.0;
    double EffectiveClipFramesPerSecond = 0.0;
    double WalkRunBlendWeight = 0.0;
    bool WalkRunBlendActive = false;
};

NativeLocomotionAnimationStep NativeLocomotionAnimationStepForMotion(const LinkMotionState& motion,
                                                                     const LinkNativeLocomotionConfig& config) {
    NativeLocomotionAnimationStep step;
    if (!motion.Moving) {
        step.CycleFramesPerSecond = config.CharacterFramesPerSecond;
        step.EffectiveClipFramesPerSecond = config.CharacterFramesPerSecond;
        return step;
    }

    const double linearVelocity = std::max(0.0, motion.NativeLinearVelocityUnitsPerTick);
    double playbackScale =
        config.WalkAnimationBaseScale + (config.WalkAnimationVelocityScalePerUnit * linearVelocity);

    if (motion.LocomotionAnimationClass == "run" && !motion.NativeWallLimitedWalk) {
        const double temp2 = linearVelocity - config.WalkRunThresholdUnitsPerTick;
        const double temp1 = config.WalkRunBlendScalePerUnit * temp2;
        if (temp2 >= 0.0 && temp1 < 1.0) {
            step.WalkRunBlendWeight = std::clamp(temp1, 0.0, 1.0);
            step.WalkRunBlendActive = step.WalkRunBlendWeight > 0.0 && step.WalkRunBlendWeight < 1.0;
        } else if (temp2 >= 0.0) {
            step.WalkRunBlendWeight = 1.0;
            playbackScale = config.RunAnimationBaseScale + (config.RunAnimationVelocityScalePerUnit * temp2);
        }
    }

    step.CycleFramesPerSecond = config.CharacterFramesPerSecond * std::max(0.0, playbackScale);
    step.EffectiveClipFramesPerSecond =
        motion.LocomotionAnimationClass == "run"
            ? step.CycleFramesPerSecond * config.RunFrameScaleFromLocomotionCycle
            : step.CycleFramesPerSecond;
    return step;
}

struct NativeWalkEndSelection {
    bool Available = false;
    size_t ClipIndex = ThreeDsRecomp::Oot3d::kInvalidLinkCsabClipIndex;
    std::string Side;
    double Phase = 0.0;
    double MorphFrames = 0.0;
};

NativeWalkEndSelection SelectNativeWalkEndClip(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                                               const LinkNativeLocomotionConfig& config,
                                               double nativeCycleFrame) {
    NativeWalkEndSelection selection;
    double phase = nativeCycleFrame - config.WalkEndPhaseOffset;
    if (phase < 0.0) {
        phase += config.LocomotionCycleFrameSpan;
    }
    selection.Phase = phase;

    if (phase < config.WalkEndLeftPhaseThreshold) {
        selection.Side = "left";
        selection.ClipIndex = config.Clips.WalkEndLeftClipIndex;
        double morphPhase = config.WalkEndLeftPhaseCenter - phase;
        if (morphPhase < 0.0) {
            morphPhase = config.WalkEndLeftLatePhaseScale * -morphPhase;
        }
        selection.MorphFrames =
            config.WalkEndMorphFrameScale * (morphPhase / config.WalkEndLeftMorphDenominator);
    } else {
        selection.Side = "right";
        selection.ClipIndex = config.Clips.WalkEndRightClipIndex;
        double morphPhase = config.WalkEndRightPhaseCenter - phase;
        if (morphPhase < 0.0) {
            morphPhase = config.WalkEndRightLatePhaseScale * -morphPhase;
        }
        selection.MorphFrames =
            config.WalkEndMorphFrameScale * (morphPhase / config.WalkEndRightMorphDenominator);
    }

    selection.MorphFrames = std::max(0.0, selection.MorphFrames);
    selection.Available = LinkCsabClipIndexValid(scene, selection.ClipIndex);
    return selection;
}

LinkNativeLocomotionState EvaluateLinkNativeLocomotionState(const LinkAnimation& animation,
                                                            const LinkMotionState& motion) {
    if (animation.NativeWalkEndToIdleMorphActive && !motion.HasInput) {
        return LinkNativeLocomotionState::ReturnIdle;
    }
    if (!motion.HasInput &&
        (animation.NativeWalkEndActive ||
         (animation.Moving && motion.NativeLinearVelocityUnitsPerTick <= 0.000001))) {
        return LinkNativeLocomotionState::WalkEnd;
    }
    if (!motion.Moving) {
        return LinkNativeLocomotionState::Idle;
    }
    if (motion.NativeWallLimitedWalk) {
        return LinkNativeLocomotionState::WallLimited;
    }
    if (!animation.Moving && motion.Moving) {
        return LinkNativeLocomotionState::StartMove;
    }
    return LinkNativeLocomotionState::Locomotion;
}

void SelectLinkAnimationClip(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, LinkAnimation& animation,
                             size_t clipIndex, bool moving) {
    if (clipIndex >= scene.LinkCsabClips.size()) {
        clipIndex = scene.LinkStandingClipIndex;
    }
    const bool changed = animation.ClipIndex != clipIndex;
    const auto& clip = scene.LinkCsabClips[clipIndex];
    animation.ClipIndex = clipIndex;
    animation.ClipId = clip.Id;
    animation.CsabName = clip.CsabName;
    animation.FrameCount = clip.Metadata.FrameCount;
    animation.Moving = moving;
    (void)changed;
}

} // namespace

void UpdateLinkMotionLocomotionAnimationClass(LinkMotionState& motion, const LinkInstance& link,
                                              const LinkNativeLocomotionConfig& config) {
    motion.NativeLinearVelocityUnitsPerTick = link.NativeLinearVelocityUnitsPerTick;
    motion.NativeWallSpeedLimitUnitsPerTick = link.NativeWallSpeedLimitUnitsPerTick;
    motion.NativeWallSpeedLimitActive = link.NativeWallSpeedLimitActive;
    motion.NativeWalkRunThresholdUnitsPerTick = config.WalkRunThresholdUnitsPerTick;
    motion.NativeWallLimitedWalk =
        link.NativeWallSpeedLimitActive &&
        link.NativeWallSpeedLimitUnitsPerTick < config.WalkRunThresholdUnitsPerTick;

    if (!motion.Moving) {
        motion.LocomotionAnimationClass = "idle";
    } else if (motion.NativeLinearVelocityUnitsPerTick < config.WalkRunThresholdUnitsPerTick ||
               motion.NativeWallLimitedWalk) {
        motion.LocomotionAnimationClass = "walk";
    } else {
        motion.LocomotionAnimationClass = "run";
    }
}

LinkAnimation InitialLinkAnimation(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                                   const LinkNativeLocomotionConfig& config) {
    LinkAnimation animation;
    animation.ClipIndex = config.Clips.IdleClipIndex;
    const auto& clip = scene.LinkCsabClips.at(animation.ClipIndex);
    animation.ClipId = clip.Id;
    animation.CsabName = clip.CsabName;
    animation.FrameCount = clip.Metadata.FrameCount;
    animation.Frame = 0.0;
    animation.CurrentPose = scene.LinkStandingPose;
    return animation;
}

void AdvanceLinkAnimation(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                          const LinkNativeLocomotionConfig& config, LinkAnimation& animation,
                          const LinkMotionState& motion, double dt) {
    animation.NativeWalkRunBlendWeight = 0.0;
    animation.NativeWalkRunBlendActive = false;
    animation.NativeWalkEndEntryMorphActive = false;
    animation.PoseBlendActive = false;
    animation.PoseBlendFromClipId.clear();
    animation.PoseBlendToClipId.clear();
    animation.PoseBlendWeight = 1.0;

    const double dtTicks = std::max(0.0, dt * config.PlayerTickRate);
    if (motion.HasInput) {
        animation.NativeWalkEndActive = false;
        animation.NativeWalkEndToIdleMorphActive = false;
        animation.NativeWalkEndComplete = false;
        animation.WalkEndEntryPoseValid = false;
        animation.WalkEndToIdlePoseValid = false;
    }

    const LinkNativeLocomotionState controllerState = EvaluateLinkNativeLocomotionState(animation, motion);
    animation.NativeLocomotionControllerActive = true;
    animation.NativeLocomotionState = NativeLocomotionStateName(controllerState);

    if (controllerState == LinkNativeLocomotionState::ReturnIdle) {
        animation.PreviousLocomotionAnimationClass = animation.LocomotionAnimationClass;
        SelectLinkAnimationClip(scene, animation, config.Clips.IdleClipIndex, false);
        animation.LocomotionAnimationClass = "idle";
        animation.NativeWallLimitedWalk = false;
        animation.FramesPerSecond = config.CharacterFramesPerSecond;
        animation.NativeCycleFramesPerSecond = 0.0;
        animation.Frame = 0.0;
        animation.NativeWalkEndToIdleBlendWeight =
            std::min(1.0, animation.NativeWalkEndToIdleBlendWeight + (dtTicks / config.DefaultLoopMorphFrames));
        if (animation.NativeWalkEndToIdleBlendWeight >= 1.0) {
            animation.NativeWalkEndToIdleMorphActive = false;
        }
        return;
    }

    if (controllerState == LinkNativeLocomotionState::WalkEnd && animation.NativeWalkEndActive) {
        SelectLinkAnimationClip(scene, animation, animation.ClipIndex, false);
        animation.Moving = false;
        animation.Frame = std::min<double>(static_cast<double>(animation.FrameCount),
                                           animation.Frame + config.CharacterFramesPerSecond * dt);
        animation.FramesPerSecond = config.CharacterFramesPerSecond;
        animation.NativeCycleFramesPerSecond = 0.0;
        animation.NativeWalkEndEntryBlendWeight =
            animation.NativeWalkEndEntryMorphFrames <= 0.000001
                ? 1.0
                : std::clamp(animation.Frame / animation.NativeWalkEndEntryMorphFrames, 0.0, 1.0);
        animation.NativeWalkEndEntryMorphActive =
            animation.WalkEndEntryPoseValid && animation.NativeWalkEndEntryBlendWeight < 1.0;

        if (animation.Frame >= static_cast<double>(animation.FrameCount)) {
            animation.NativeWalkEndActive = false;
            animation.NativeWalkEndComplete = true;
            animation.NativeWalkEndEntryMorphActive = false;
            animation.WalkEndToIdlePose = ThreeDsRecomp::Oot3d::SampleOot3dNativeDemoLinkPoseFrame(
                scene, animation.ClipIndex, static_cast<float>(animation.Frame));
            animation.WalkEndToIdlePoseValid = animation.WalkEndToIdlePose.Valid;
            animation.NativeWalkEndToIdleMorphActive = animation.WalkEndToIdlePoseValid;
            animation.NativeWalkEndToIdleBlendWeight = 0.0;
            SelectLinkAnimationClip(scene, animation, config.Clips.IdleClipIndex, false);
            animation.LocomotionAnimationClass = "idle";
            animation.NativeLocomotionState = NativeLocomotionStateName(LinkNativeLocomotionState::ReturnIdle);
            animation.Frame = 0.0;
        }
        return;
    }

    if (controllerState == LinkNativeLocomotionState::WalkEnd && !animation.NativeWalkEndActive) {
        const auto selection = SelectNativeWalkEndClip(scene, config, animation.NativeCycleFrame);
        if (selection.Available) {
            animation.PreviousLocomotionAnimationClass = animation.LocomotionAnimationClass;
            animation.NativeWalkEndActive = true;
            animation.NativeWalkEndComplete = false;
            animation.NativeWalkEndToIdleMorphActive = false;
            animation.NativeWalkEndSide = selection.Side;
            animation.NativeWalkEndSelectionPhase = selection.Phase;
            animation.NativeWalkEndEntryMorphFrames = selection.MorphFrames;
            animation.NativeWalkEndEntryBlendWeight = selection.MorphFrames <= 0.000001 ? 1.0 : 0.0;
            animation.WalkEndEntryPose = animation.CurrentPose;
            animation.WalkEndEntryPoseValid = animation.CurrentPose.Valid;
            SelectLinkAnimationClip(scene, animation, selection.ClipIndex, false);
            animation.Moving = false;
            animation.LocomotionAnimationClass = selection.Side == "left" ? "walk_end_left" : "walk_end_right";
            animation.Frame = 0.0;
            animation.FramesPerSecond = config.CharacterFramesPerSecond;
            animation.NativeCycleFramesPerSecond = 0.0;
            animation.NativeWalkEndEntryMorphActive =
                animation.WalkEndEntryPoseValid && animation.NativeWalkEndEntryBlendWeight < 1.0;
            return;
        }
    }

    const bool startingMovement = !animation.Moving && motion.Moving;
    if (startingMovement) {
        animation.StartMorphPose = animation.CurrentPose;
        animation.StartMorphPoseValid = animation.CurrentPose.Valid;
        animation.NativeStartBlendWeight = 0.0;
    }

    size_t clipIndex = config.Clips.IdleClipIndex;
    if (motion.LocomotionAnimationClass == "walk") {
        clipIndex = config.Clips.WalkClipIndex;
    } else if (motion.Moving) {
        clipIndex = config.Clips.RunClipIndex;
    }

    animation.PreviousLocomotionAnimationClass = animation.LocomotionAnimationClass;
    SelectLinkAnimationClip(scene, animation, clipIndex, motion.Moving);
    animation.LocomotionAnimationClass = motion.LocomotionAnimationClass;
    animation.NativeWallLimitedWalk = motion.NativeWallLimitedWalk;
    animation.NativeWalkRunThresholdUnitsPerTick = config.WalkRunThresholdUnitsPerTick;

    if (!motion.Moving) {
        animation.NativeStartBlendWeight = 1.0;
        animation.NativeStartMorphActive = false;
        animation.FramesPerSecond = config.CharacterFramesPerSecond;
        animation.NativeCycleFramesPerSecond = 0.0;
        animation.Frame = WrapAnimationFrame(animation.Frame + animation.FramesPerSecond * dt, animation.FrameCount);
    } else {
        const auto nativeStep = NativeLocomotionAnimationStepForMotion(motion, config);
        animation.NativeCycleFramesPerSecond = nativeStep.CycleFramesPerSecond;
        animation.FramesPerSecond = nativeStep.EffectiveClipFramesPerSecond;
        animation.NativeWalkRunBlendWeight = nativeStep.WalkRunBlendWeight;
        animation.NativeWalkRunBlendActive = nativeStep.WalkRunBlendActive;
        animation.NativeCycleFrame = WrapFrameSpan(animation.NativeCycleFrame + nativeStep.CycleFramesPerSecond * dt,
                                                   config.LocomotionCycleFrameSpan);
        animation.WalkFrame = animation.NativeCycleFrame;
        animation.RunFrame = animation.NativeCycleFrame * config.RunFrameScaleFromLocomotionCycle;
        animation.Frame = motion.LocomotionAnimationClass == "run" ? animation.RunFrame : animation.WalkFrame;
        animation.Frame = WrapAnimationFrame(animation.Frame, animation.FrameCount);

        if (animation.NativeStartBlendWeight < 1.0) {
            animation.NativeStartBlendWeight = std::min(1.0, animation.NativeStartBlendWeight + dtTicks);
        }
        animation.NativeStartMorphActive =
            animation.StartMorphPoseValid && animation.NativeStartBlendWeight > 0.0 &&
            animation.NativeStartBlendWeight < 1.0;
    }
}

ThreeDsRecomp::Oot3d::Matrix4f BlendMatrix(const ThreeDsRecomp::Oot3d::Matrix4f& from, const ThreeDsRecomp::Oot3d::Matrix4f& to,
                                  double weight) {
    ThreeDsRecomp::Oot3d::Matrix4f out;
    const float t = static_cast<float>(std::clamp(weight, 0.0, 1.0));
    const float invT = 1.0f - t;
    for (size_t row = 0; row < 4; ++row) {
        for (size_t col = 0; col < 4; ++col) {
            out.M[row][col] = from.M[row][col] * invT + to.M[row][col] * t;
        }
    }
    return out;
}

ThreeDsRecomp::Oot3d::CsabPose BlendCsabPoses(const ThreeDsRecomp::Oot3d::CsabPose& from, const ThreeDsRecomp::Oot3d::CsabPose& to,
                                     double weight) {
    ThreeDsRecomp::Oot3d::CsabPose out = to;
    out.LocalTransforms.clear();
    out.WorldTransforms.clear();
    const size_t count = std::min(from.WorldTransforms.size(), to.WorldTransforms.size());
    out.WorldTransforms.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        out.WorldTransforms.push_back(BlendMatrix(from.WorldTransforms[i], to.WorldTransforms[i], weight));
    }
    out.SampledChannelValueCount = from.SampledChannelValueCount + to.SampledChannelValueCount;
    out.FiniteChannelValueCount = from.FiniteChannelValueCount + to.FiniteChannelValueCount;
    out.NonF32ChannelBlockCount = from.NonF32ChannelBlockCount + to.NonF32ChannelBlockCount;
    out.AnimatedBoneTransformCount = std::max(from.AnimatedBoneTransformCount, to.AnimatedBoneTransformCount);
    out.Valid = from.Valid && to.Valid && from.WorldTransforms.size() == to.WorldTransforms.size();
    return out;
}
