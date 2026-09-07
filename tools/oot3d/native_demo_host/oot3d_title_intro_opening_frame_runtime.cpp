#include "oot3d_title_intro_opening_frame_runtime.h"

#include <algorithm>
#include <cmath>

extern "C" {
#include "oot3d/title_intro_opening_logo_runtime.h"
}

int32_t TitleIntroOpeningFrameRuntimeSampleFrame(const TitleIntroPlayback& playback) {
    return static_cast<int32_t>(std::max(1.0, std::floor(playback.Frame)));
}

double TitleIntroTimelineFrame(const TitleIntroPlayback& playback) {
    if (playback.QdbRow == nullptr || playback.QdbRow->endFrame <= 0) {
        return playback.Frame;
    }
    const double span = static_cast<double>(playback.QdbRow->endFrame);
    const double wrapped = std::fmod(playback.Frame, span);
    return wrapped < 0.0 ? wrapped + span : wrapped;
}

namespace {
constexpr double kTitleIntroHorseFloorProbeHeight = 40.0;
constexpr double kTitleIntroHorseFloorSampleDistance = 30.0;
constexpr double kTitleIntroHorseFloorSampleHeight = 60.0;
constexpr double kTitleIntroBinangPerRadian = 32768.0 / 3.14159265358979323846;

int16_t TitleIntroHorseFloorPitch(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
    float x, float y, float z, int16_t yaw) {
    const double yawRadians = static_cast<double>(yaw) *
                              (3.14159265358979323846 / 32768.0);
    const double offsetX = std::sin(yawRadians) * kTitleIntroHorseFloorSampleDistance;
    const double offsetZ = std::cos(yawRadians) * kTitleIntroHorseFloorSampleDistance;
    const double queryY = static_cast<double>(y) + kTitleIntroHorseFloorSampleHeight;
    const auto front = ThreeDsRecomp::Oot3d::Oot3dNativeDemoFloorHitAt(
        scene.Collision, static_cast<double>(x) + offsetX,
        static_cast<double>(z) + offsetZ, queryY);
    const auto back = ThreeDsRecomp::Oot3d::Oot3dNativeDemoFloorHitAt(
        scene.Collision, static_cast<double>(x) - offsetX,
        static_cast<double>(z) - offsetZ, queryY);
    if (!front.has_value() || !back.has_value()) {
        return 0;
    }
    return static_cast<int16_t>(std::lround(
        std::atan2(back->Y - front->Y,
                   kTitleIntroHorseFloorSampleDistance * 2.0) *
        kTitleIntroBinangPerRadian));
}

void ApplyTitleIntroPairedMountFloor(
    TitleIntroPlayback& playback,
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene) {
    auto& step = playback.OpeningFrameRuntimeStep;
    auto& motion = step.actorMotion;
    auto& visual = motion.pairedMountVisualTransform;
    if (step.actorMotionActive == 0 || visual.valid == 0 ||
        Oot3d_TitleIntroOpeningActorRuntimeUsesNativeBgCheckGrounding(
            motion.pairedMountBinding, &motion) == 0u) {
        return;
    }

    auto& runtimeState = playback.OpeningFrameRuntimeState.pairedMountMotionState;
    const auto floor = ThreeDsRecomp::Oot3d::Oot3dNativeDemoFloorHitAt(
        scene.Collision, visual.positionX, visual.positionZ,
        static_cast<double>(runtimeState.positionY) + kTitleIntroHorseFloorProbeHeight);
    if (!floor.has_value()) {
        return;
    }

    const float floorY = static_cast<float>(floor->Y);
    runtimeState.positionY = floorY;
    visual.positionY = floorY;
    visual.rotX = TitleIntroHorseFloorPitch(
        scene, visual.positionX, floorY, visual.positionZ, visual.rotY);
    visual.rotZ = 0;

    if (!playback.GenericCutsceneRuntimeRequested) {
        return;
    }

    playback.GenericCutsceneState.openTitleState.pairedMountMotionState.positionY = floorY;
    playback.GenericCutsceneSnapshot.openTitleStep.actorMotion.pairedMountVisualTransform.positionY = floorY;
    playback.GenericCutsceneSnapshot.openTitleStep.actorMotion.pairedMountVisualTransform.rotX = visual.rotX;
    playback.GenericCutsceneSnapshot.openTitleStep.actorMotion.pairedMountVisualTransform.rotZ = visual.rotZ;
    for (uint16_t i = 0; i < playback.GenericCutsceneSnapshot.actorCount; ++i) {
        auto& actor = playback.GenericCutsceneSnapshot.actors[i];
        if (actor.valid != 0 && actor.actorBindingIndex == visual.actorBindingIndex) {
            actor.positionY = floorY;
            actor.rotX = visual.rotX;
            actor.rotZ = visual.rotZ;
            break;
        }
    }
}
} // namespace

bool SampleTitleIntroOpeningFrameRuntime(
    TitleIntroPlayback& playback,
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene) {
    playback.OpeningFrameRuntimeStepValid = false;
    playback.OpeningFrameRuntimeCameraUsedForRender = false;
    playback.OpeningFrameRuntimeActorMotionUsedForRender = false;
    playback.OpeningFrameRuntimeLinkActorTransformUsedForRender = false;
    playback.OpeningFrameRuntimeEponaActorTransformUsedForRender = false;
    playback.OpeningFrameRuntimeLogoDrawUsedForRender = false;
    playback.OpeningFrameRuntimeLogoDrawResolvedComponentCount = 0;
    playback.OpeningFrameRuntimeLogoDrawVisibleComponentCount = 0;
    playback.OpeningFrameRuntimeLogoDrawSubmittedVisualCount = 0;
    playback.OpeningFrameRuntimeLogoDrawSkippedInvisibleComponentCount = 0;
    playback.OpeningFrameRuntimeLogoDrawMaterialColorOverrideBatchCount = 0;
    playback.OpeningFrameRuntimeLogoDrawRenderStatus =
        Oot3d_TitleIntroOpeningLogoRuntimeDrawRenderNotProcessedStatus();
    playback.OpeningFrameRuntimeLogoInputBound = false;
    playback.OpeningFrameRuntimeLogoInputOrchestrationIndex =
        OOT3D_TITLE_INTRO_OPENING_NO_ORCHESTRATION_INDEX;
    playback.OpeningFrameRuntimeLogoInputCutsceneSourceIndex = kOot3dInvalidSourceIndex;
    playback.OpeningFrameRuntimeLogoInputCutsceneFrame = 0;
    playback.OpeningFrameRuntimeLogoInputEnvFlag3 = 0;
    playback.OpeningFrameRuntimeLogoInputEnvFlag4 = 0;
    playback.OpeningFrameRuntimeLogoInputSourceStatus =
        Oot3d_TitleIntroOpeningFrameRuntimeLogoInputNotSampledStatus();
    playback.OpeningFrameRuntimeLogoInputInitStatus = OOT3D_CUTSCENE_INTRO_RUNTIME_UNINITIALIZED;
    playback.OpeningFrameRuntimeLogoInputStepStatus = OOT3D_CUTSCENE_INTRO_RUNTIME_UNINITIALIZED;
    if (playback.OpeningOrchestrationRow == nullptr ||
        playback.OpeningOrchestrationIndex == OOT3D_TITLE_INTRO_OPENING_NO_ORCHESTRATION_INDEX) {
        playback.OpeningFrameRuntimeInitialized = false;
        playback.OpeningFrameRuntimeState = {};
        playback.OpeningFrameRuntimeStep = {};
        playback.OpeningFrameRuntimeSampledFrame = 0;
        playback.OpeningFrameRuntimeInitStatus =
            OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_MISSING_ORCHESTRATION;
        playback.OpeningFrameRuntimeStepStatus = OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_UNINITIALIZED;
        return false;
    }

    auto initializeRuntime = [&playback]() -> bool {
        playback.OpeningFrameRuntimeState = {};
        playback.OpeningFrameRuntimeStep = {};
        playback.OpeningFrameRuntimeSampledFrame = 0;
        playback.OpeningFrameRuntimeStepStatus =
            OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_UNINITIALIZED;
        if (playback.GenericCutsceneRuntimeRequested) {
            playback.GenericCutsceneState = {};
            playback.GenericCutsceneSnapshot = {};
            playback.GenericCutsceneStepStatus = OOT3D_SCENE_CUTSCENE_FRAME_RUNTIME_UNINITIALIZED;
            playback.GenericCutsceneInitStatus = Oot3d_SceneCutsceneFrameRuntimeInit(
                &playback.GenericCutsceneState,
                playback.GenericCutsceneKey);
            playback.OpeningFrameRuntimeInitStatus = playback.GenericCutsceneState.openTitleInitStatus;
            playback.OpeningFrameRuntimeState = playback.GenericCutsceneState.openTitleState;
        } else {
            playback.OpeningFrameRuntimeInitStatus = Oot3d_TitleIntroOpeningFrameRuntimeInit(
                &playback.OpeningFrameRuntimeState,
                playback.OpeningOrchestrationIndex,
                0);
        }
        playback.OpeningFrameRuntimeInitialized =
            playback.OpeningFrameRuntimeInitStatus == OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_OK &&
            (!playback.GenericCutsceneRuntimeRequested ||
             playback.GenericCutsceneInitStatus == OOT3D_SCENE_CUTSCENE_FRAME_RUNTIME_OK);
        return playback.OpeningFrameRuntimeInitialized;
    };

    if (!playback.OpeningFrameRuntimeInitialized ||
        playback.OpeningFrameRuntimeState.orchestrationIndex != playback.OpeningOrchestrationIndex) {
        if (!initializeRuntime()) {
            return false;
        }
    }

    const int32_t sampleFrame = std::clamp(
        TitleIntroOpeningFrameRuntimeSampleFrame(playback),
        1,
        std::max(1, playback.OpeningFrameRuntimeState.nativeEndFrame));

    if (sampleFrame < playback.OpeningFrameRuntimeSampledFrame) {
        if (!initializeRuntime()) {
            return false;
        }
    }

    while (playback.OpeningFrameRuntimeSampledFrame < sampleFrame) {
        if (playback.GenericCutsceneRuntimeRequested) {
            playback.GenericCutsceneStepStatus = Oot3d_SceneCutsceneFrameRuntimeStep(
                &playback.GenericCutsceneState,
                nullptr,
                &playback.GenericCutsceneSnapshot);
            playback.OpeningFrameRuntimeState = playback.GenericCutsceneState.openTitleState;
            playback.OpeningFrameRuntimeStep = playback.GenericCutsceneSnapshot.openTitleStep;
            playback.OpeningFrameRuntimeStepStatus = playback.GenericCutsceneSnapshot.openTitleStepStatus;
        } else {
            playback.OpeningFrameRuntimeStepStatus = Oot3d_TitleIntroOpeningFrameRuntimeStep(
                &playback.OpeningFrameRuntimeState,
                nullptr,
                &playback.OpeningFrameRuntimeStep);
        }
        if (playback.OpeningFrameRuntimeStepStatus != OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_OK) {
            break;
        }
        ApplyTitleIntroPairedMountFloor(playback, scene);
        const int32_t previousSampledFrame = playback.OpeningFrameRuntimeSampledFrame;
        playback.OpeningFrameRuntimeSampledFrame = playback.OpeningFrameRuntimeStep.frame;
        if (playback.OpeningFrameRuntimeSampledFrame <= previousSampledFrame) {
            playback.OpeningFrameRuntimeSampledFrame = previousSampledFrame + 1;
        }
    }
    playback.OpeningFrameRuntimeLogoInputBound =
        playback.OpeningFrameRuntimeState.logoInputBound != 0;
    playback.OpeningFrameRuntimeLogoInputOrchestrationIndex = playback.OpeningOrchestrationIndex;
    playback.OpeningFrameRuntimeLogoInputCutsceneSourceIndex =
        playback.OpeningFrameRuntimeState.logoInputCutsceneSourceIndex;
    playback.OpeningFrameRuntimeLogoInputCutsceneFrame =
        playback.OpeningFrameRuntimeState.logoInputCutsceneRuntime.frame;
    playback.OpeningFrameRuntimeLogoInputEnvFlag3 =
        playback.OpeningFrameRuntimeState.logoInputEnvFlag3;
    playback.OpeningFrameRuntimeLogoInputEnvFlag4 =
        playback.OpeningFrameRuntimeState.logoInputEnvFlag4;
    playback.OpeningFrameRuntimeLogoInputSourceStatus =
        Oot3d_TitleIntroOpeningFrameRuntimeLogoInputSourceStatus(
            playback.OpeningFrameRuntimeLogoInputBound ? 1u : 0u);
    playback.OpeningFrameRuntimeLogoInputInitStatus =
        playback.OpeningFrameRuntimeState.logoInputInitStatus;
    playback.OpeningFrameRuntimeLogoInputStepStatus =
        playback.OpeningFrameRuntimeState.logoInputStepStatus;
    playback.OpeningFrameRuntimeStepValid =
        playback.OpeningFrameRuntimeStepStatus == OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_OK;
    return playback.OpeningFrameRuntimeStepValid;
}

namespace {
const Oot3dSceneCutsceneActorSnapshot* FindGenericCutsceneActorSnapshot(
    const TitleIntroPlayback& playback,
    const TitleIntroNativeActor& actor) {
    if (!playback.GenericCutsceneRuntimeRequested || playback.GenericCutsceneSnapshot.valid == 0 ||
        actor.OpeningActorBindingRow == nullptr) {
        return nullptr;
    }
    for (uint16_t i = 0; i < playback.GenericCutsceneSnapshot.actorCount; ++i) {
        const auto& snapshot = playback.GenericCutsceneSnapshot.actors[i];
        if (snapshot.valid != 0 &&
            snapshot.actorBindingIndex == actor.OpeningActorBindingRow->actorBindingIndex) {
            return &snapshot;
        }
    }
    return nullptr;
}

TitleIntroCueSample GenericCutsceneActorCueSample(
    const Oot3dSceneCutsceneActorSnapshot* actor,
    bool mountedPlayer) {
    TitleIntroCueSample sample;
    if (actor == nullptr) {
        sample.Status = Oot3d_TitleIntroOpeningActorRuntimeOpeningFrameMotionInactiveStatus();
        return sample;
    }
    sample.Valid = true;
    sample.Active = actor->active != 0;
    sample.GenericActorSnapshot = actor;
    sample.Frame = static_cast<double>(actor->frame);
    sample.Interpolation = static_cast<double>(actor->interpolation);
    sample.Position = { actor->positionX, actor->positionY, actor->positionZ };
    sample.Rotation = { static_cast<double>(actor->rotX), static_cast<double>(actor->rotY),
                        static_cast<double>(actor->rotZ) };
    if (mountedPlayer && actor->attachment.active != 0) {
        sample.HasMountedAttachment = true;
        sample.MountedCopyParentYaw = actor->attachment.copyParentYaw != 0;
        sample.MountedParentActorBindingIndex = actor->attachment.parentActorBindingIndex;
        sample.MountedParentPoseOffsetNodeIndex = actor->attachment.parentPoseOffsetNodeIndex;
        sample.MountedChildRootMotionNodeIndex = actor->attachment.childRootMotionNodeIndex;
        sample.MountedChildRootMotionScale = actor->attachment.childRootMotionScale;
        sample.MountedPlayerActionFunction = actor->attachment.mountedPlayerActionFunction;
        sample.MountedSkeletonNodeOffsetFunction = actor->attachment.skeletonNodeOffsetFunction;
        sample.MountedAttachmentSource = actor->attachment.source != nullptr
                                             ? actor->attachment.source
                                             : "not_mounted";
    }
    sample.Status = mountedPlayer
                        ? Oot3d_TitleIntroOpeningActorRuntimeLinkVisualTransformStatus(
                              sample.Active ? 1u : 0u)
                        : Oot3d_TitleIntroOpeningActorRuntimePairedMountVisualTransformStatus(
                              actor->sourceKind, sample.Active ? 1u : 0u);
    return sample;
}
} // namespace

TitleIntroCueSample SampleTitleIntroOpeningFrameRuntimeLinkCue(const TitleIntroPlayback& playback) {
    if (playback.GenericCutsceneRuntimeRequested) {
        return GenericCutsceneActorCueSample(
            FindGenericCutsceneActorSnapshot(playback, playback.LinkActor), true);
    }
    TitleIntroCueSample sample;
    if (!playback.OpeningFrameRuntimeStepValid) {
        sample.Status = Oot3d_TitleIntroOpeningActorRuntimeOpeningFrameNotSampledStatus();
        return sample;
    }
    if (playback.OpeningFrameRuntimeStep.actorMotionStatus != OOT3D_TITLE_INTRO_OPENING_ACTOR_SAMPLE_OK ||
        playback.OpeningFrameRuntimeStep.actorMotionActive == 0) {
        sample.Frame = static_cast<double>(playback.OpeningFrameRuntimeStep.frame);
        sample.Status = Oot3d_TitleIntroOpeningActorRuntimeOpeningFrameMotionInactiveStatus();
        return sample;
    }

    const auto& actorMotion = playback.OpeningFrameRuntimeStep.actorMotion;
    const auto& visualTransform = actorMotion.linkVisualTransform;
    if (visualTransform.valid == 0 || visualTransform.motionRow == nullptr) {
        sample.Frame = static_cast<double>(actorMotion.frame);
        sample.Status = Oot3d_TitleIntroOpeningActorRuntimeLinkVisualTransformUnresolvedStatus();
        return sample;
    }
    if (Oot3d_TitleIntroOpeningActorRuntimeIsAdultLinkBinding(actorMotion.actorBinding) == 0u) {
        sample.Frame = static_cast<double>(actorMotion.frame);
        sample.Status = Oot3d_TitleIntroOpeningActorRuntimeLinkBindingMismatchStatus();
        return sample;
    }

    sample.Valid = true;
    sample.Active = visualTransform.active != 0;
    sample.OpeningPlayerMotionRow = visualTransform.motionRow;
    sample.OpeningPlayerMotionTransform = actorMotion.playerActionTransform;
    sample.HasOpeningPlayerMotionTransform = true;
    sample.HasMountedAttachment = actorMotion.linkMountAttachment.active != 0u;
    sample.MountedCopyParentYaw = actorMotion.linkMountAttachment.copyParentYaw != 0u;
    sample.MountedParentActorBindingIndex = actorMotion.linkMountAttachment.parentActorBindingIndex;
    sample.MountedParentPoseOffsetNodeIndex =
        actorMotion.linkMountAttachment.parentPoseOffsetNodeIndex;
    sample.MountedChildRootMotionNodeIndex =
        actorMotion.linkMountAttachment.childRootMotionNodeIndex;
    sample.MountedChildRootMotionScale = actorMotion.linkMountAttachment.childRootMotionScale;
    sample.MountedPlayerActionFunction = actorMotion.linkMountAttachment.mountedPlayerActionFunction;
    sample.MountedSkeletonNodeOffsetFunction =
        actorMotion.linkMountAttachment.skeletonNodeOffsetFunction;
    sample.MountedAttachmentSource = actorMotion.linkMountAttachment.source != nullptr
                                       ? actorMotion.linkMountAttachment.source
                                       : "not_mounted";
    sample.Frame = static_cast<double>(visualTransform.frame);
    sample.Interpolation = static_cast<double>(visualTransform.interpolation);
    sample.Position = { static_cast<double>(visualTransform.positionX),
                        static_cast<double>(visualTransform.positionY),
                        static_cast<double>(visualTransform.positionZ) };
    sample.Rotation = { static_cast<double>(visualTransform.rotX),
                        static_cast<double>(visualTransform.rotY),
                        static_cast<double>(visualTransform.rotZ) };
    sample.Status = Oot3d_TitleIntroOpeningActorRuntimeLinkVisualTransformStatus(sample.Active ? 1u : 0u);
    return sample;
}

TitleIntroCueSample SampleTitleIntroOpeningFrameRuntimePairedMountCue(const TitleIntroPlayback& playback) {
    if (playback.GenericCutsceneRuntimeRequested) {
        return GenericCutsceneActorCueSample(
            FindGenericCutsceneActorSnapshot(playback, playback.EponaActor), false);
    }
    TitleIntroCueSample sample;
    if (!playback.OpeningFrameRuntimeStepValid) {
        sample.Status = Oot3d_TitleIntroOpeningActorRuntimeOpeningFrameNotSampledStatus();
        return sample;
    }
    if (playback.OpeningFrameRuntimeStep.actorMotionStatus != OOT3D_TITLE_INTRO_OPENING_ACTOR_SAMPLE_OK ||
        playback.OpeningFrameRuntimeStep.actorMotionActive == 0) {
        sample.Frame = static_cast<double>(playback.OpeningFrameRuntimeStep.frame);
        sample.Status = Oot3d_TitleIntroOpeningActorRuntimeOpeningFrameMotionInactiveStatus();
        return sample;
    }

    const auto& actorMotion = playback.OpeningFrameRuntimeStep.actorMotion;
    const auto& visualTransform = actorMotion.pairedMountVisualTransform;
    if (visualTransform.valid == 0 ||
        (visualTransform.actorCueRow == nullptr && visualTransform.motionRow == nullptr)) {
        sample.Frame = static_cast<double>(actorMotion.frame);
        sample.Status = Oot3d_TitleIntroOpeningActorRuntimePairedMountVisualTransformUnresolvedStatus();
        return sample;
    }
    if (Oot3d_TitleIntroOpeningActorRuntimeIsEponaBinding(actorMotion.pairedMountBinding) == 0u) {
        sample.Frame = static_cast<double>(actorMotion.frame);
        sample.Status = Oot3d_TitleIntroOpeningActorRuntimePairedMountBindingMismatchStatus();
        return sample;
    }

    sample.Valid = true;
    sample.Active = visualTransform.active != 0;
    sample.Row = visualTransform.actorCueRow;
    sample.OpeningPlayerMotionRow = visualTransform.motionRow;
    sample.OpeningPlayerMotionTransform = actorMotion.playerActionTransform;
    sample.HasOpeningPlayerMotionTransform = visualTransform.motionRow != nullptr;
    sample.Frame = static_cast<double>(visualTransform.frame);
    sample.Interpolation = static_cast<double>(visualTransform.interpolation);
    sample.Position = { static_cast<double>(visualTransform.positionX),
                        static_cast<double>(visualTransform.positionY),
                        static_cast<double>(visualTransform.positionZ) };
    sample.Rotation = { static_cast<double>(visualTransform.rotX),
                        static_cast<double>(visualTransform.rotY),
                        static_cast<double>(visualTransform.rotZ) };
    sample.Status = Oot3d_TitleIntroOpeningActorRuntimePairedMountVisualTransformStatus(
        visualTransform.sourceKind,
        sample.Active ? 1u : 0u);
    return sample;
}
