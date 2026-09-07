#include "oot3d_title_intro_playback_render.h"

#include <algorithm>
#include <chrono>
#include <string>

#include "oot3d_demo_math.h"
#include "oot3d_title_intro_actor_render.h"
#include "oot3d_title_intro_effects.h"
#include "oot3d_title_intro_camera.h"
#include "oot3d_title_intro_cue_sampling.h"
#include "oot3d_title_intro_logo_render.h"
#include "oot3d_title_intro_logo_runtime.h"
#include "oot3d_title_intro_opening_frame_runtime.h"
#include "oot3d_title_intro_render_scene.h"
#include "oot3d_title_intro_runtime_environment.h"

namespace {
double PerformanceSecondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

Vec3 TitleIntroCueDirection(const Oot3dTitleIntroActorCueRow& row) {
    Vec3 direction{
        static_cast<double>(row.endX) - static_cast<double>(row.startX),
        0.0,
        static_cast<double>(row.endZ) - static_cast<double>(row.startZ),
    };
    return Dot(direction, direction) > 0.000001 ? Normalize(direction) : Vec3{};
}

Vec3 TitleIntroCueDirection(const Oot3dTitleIntroPlayerActionTransform& transform) {
    Vec3 direction{
        static_cast<double>(transform.endX) - static_cast<double>(transform.startX),
        0.0,
        static_cast<double>(transform.endZ) - static_cast<double>(transform.startZ),
    };
    return Dot(direction, direction) > 0.000001 ? Normalize(direction) : Vec3{};
}

Vec3 TitleIntroTimelineDirection(uint16_t qdbIndex) {
    for (uint32_t i = 0; i < gOot3dTitleIntroActorCueRowCount; ++i) {
        const auto& row = gOot3dTitleIntroActorCueRows[i];
        if (row.qdbIndex != qdbIndex) {
            continue;
        }
        const Vec3 direction = TitleIntroCueDirection(row);
        if (Dot(direction, direction) > 0.000001) {
            return direction;
        }
    }
    return { 0.0, 0.0, 1.0 };
}

void ApplyTitleIntroDiagnosticCamera(TitleIntroPlayback& playback, Camera& camera,
                                     const ThreeDsRecomp::Oot3d::Oot3dDemoBounds& titleActorBounds) {
    if (!playback.LinkCue.Valid && !playback.EponaCue.Valid) {
        return;
    }

    Vec3 target{};
    double extent = 500.0;
    if (titleActorBounds.Valid) {
        target = ToVec3(ThreeDsRecomp::Oot3d::NativeDemoBoundsCenter(titleActorBounds));
        extent = std::max(ThreeDsRecomp::Oot3d::NativeDemoBoundsMaxExtent(titleActorBounds), 250.0);
        target.Y += extent * 0.12;
    } else {
        double count = 0.0;
        if (playback.LinkCue.Valid) {
            target = Add(target, ToVec3(playback.LinkCue.Position));
            count += 1.0;
        }
        if (playback.EponaCue.Valid) {
            target = Add(target, ToVec3(playback.EponaCue.Position));
            count += 1.0;
        }
        target = Scale(target, 1.0 / std::max(1.0, count));
        target.Y += 120.0;
    }

    Vec3 direction = playback.EponaCue.Valid && playback.EponaCue.Row != nullptr
                         ? TitleIntroCueDirection(*playback.EponaCue.Row)
                         : Vec3{};
    if (Dot(direction, direction) <= 0.000001) {
        if (playback.LinkCue.Valid && playback.LinkCue.HasOpeningPlayerMotionTransform) {
            direction = TitleIntroCueDirection(playback.LinkCue.OpeningPlayerMotionTransform);
        } else {
            direction = playback.LinkCue.Valid && playback.LinkCue.Row != nullptr
                            ? TitleIntroCueDirection(*playback.LinkCue.Row)
                            : Vec3{};
        }
    }
    if (Dot(direction, direction) <= 0.000001) {
        direction = TitleIntroTimelineDirection(playback.QdbIndex);
    }
    const Vec3 side = Normalize({ -direction.Z, 0.0, direction.X });
    const double distance = std::clamp(extent * 2.6, 1200.0, 6000.0);
    camera.Target = target;
    camera.Position =
        Add(Add(target, Scale(direction, -distance)), Add(Scale(side, distance * 0.35), { 0.0, distance * 0.35, 0.0 }));
    camera.Up = { 0.0, 1.0, 0.0 };
    camera.FovDegrees = 45.0;
    LookAt(camera, camera.Target);
    camera.NativeCameraActive = false;
    camera.NativeBehavior = "title_intro_diagnostic_camera_from_qdb_actor_cues";
    camera.NativeSet = kNativeCameraSetNone;
    camera.NativeMode = kNativeCameraModeNormal;
    camera.NativeFunction = kNativeCameraSetNone;
    playback.DiagnosticCameraApplied = true;
}


} // namespace

void ApplyTitleIntroPlaybackToRenderScene(
    const Args& args,
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
    TitleIntroPlayback& playback,
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene,
    Camera& camera,
    size_t baseActorVisualCount,
    double aspect,
    const TitleIntroRenderSceneBuilder& buildRenderSceneForMode,
    bool buildActorDiagnostics) {
    if (!playback.Enabled || !playback.Initialized) {
        return;
    }

    auto performanceStart = std::chrono::steady_clock::now();
    const double timelineFrame = TitleIntroTimelineFrame(playback);
    const bool openingFrameRuntimeSampled = SampleTitleIntroOpeningFrameRuntime(playback, scene);
    playback.Performance.OpeningFrameRuntimeSeconds += PerformanceSecondsSince(performanceStart);

    performanceStart = std::chrono::steady_clock::now();
    const float materialAnimationFrame =
        openingFrameRuntimeSampled
            ? static_cast<float>(playback.OpeningFrameRuntimeStep.frame)
            : static_cast<float>(timelineFrame);
    const std::string materialAnimationFrameSource =
        Oot3d_TitleIntroOpeningFrameRuntimeMaterialAnimationFrameSource(
            openingFrameRuntimeSampled ? 1u : 0u);
    const auto runtimeEnvironment = TitleIntroRuntimeEnvironmentInput(playback);
    const bool rebuildRuntimeEnvironment =
        TitleIntroRuntimeEnvironmentNeedsRenderRebuild(
            playback, runtimeEnvironment, materialAnimationFrame);
    if (renderScene.ActorVisuals.size() > baseActorVisualCount) {
        renderScene.ActorVisuals.resize(baseActorVisualCount);
    }
    HideGameplayLinkForTitleIntro(renderScene, false);
    if (rebuildRuntimeEnvironment) {
        if (renderScene.Room.Batches.empty()) {
            renderScene = buildRenderSceneForMode(
                args, scene, RuntimeEnvironmentInputPtr(runtimeEnvironment), materialAnimationFrame);
            if (renderScene.ActorVisuals.size() > baseActorVisualCount) {
                renderScene.ActorVisuals.resize(baseActorVisualCount);
            }
            HideGameplayLinkForTitleIntro(renderScene, false);
        } else {
            ThreeDsRecomp::Oot3d::RefreshOot3dNativeDemoRenderSceneRuntimeEnvironment(
                scene, renderScene, RuntimeEnvironmentInputPtr(runtimeEnvironment), materialAnimationFrame);
        }
    } else {
        ThreeDsRecomp::Oot3d::RefreshOot3dNativeDemoRoomMaterialAnimations(
            scene, renderScene, materialAnimationFrame);
    }
    MarkTitleIntroRuntimeEnvironmentRendered(
        playback, runtimeEnvironment, materialAnimationFrame,
        materialAnimationFrameSource, rebuildRuntimeEnvironment);
    playback.Performance.RuntimeEnvironmentSeconds += PerformanceSecondsSince(performanceStart);

    performanceStart = std::chrono::steady_clock::now();
    playback.LogoRuntime.AlphaState =
        openingFrameRuntimeSampled
            ? TitleIntroLogoAlphaStateFromOpeningFrameRuntime(
                  playback.LogoRuntime,
                  playback.OpeningFrameRuntimeState.logoState,
                  playback.OpeningFrameRuntimeStep.logoStep,
                  static_cast<double>(playback.OpeningFrameRuntimeStep.frame))
            : SimulateTitleIntroLogoAlphaState(playback.LogoRuntime, playback.Frame);
    const TitleIntroCueSample runtimeLinkCue =
        openingFrameRuntimeSampled ? SampleTitleIntroOpeningFrameRuntimeLinkCue(playback) : TitleIntroCueSample{};
    const bool runtimeLinkCueBound = runtimeLinkCue.Valid;
    const TitleIntroCueSample runtimeEponaCue =
        openingFrameRuntimeSampled ? SampleTitleIntroOpeningFrameRuntimePairedMountCue(playback) : TitleIntroCueSample{};
    const bool runtimeEponaCueBound = runtimeEponaCue.Valid;
    playback.LinkCue = runtimeLinkCue;
    playback.EponaCue = runtimeEponaCue;
    playback.LinkCueResolvedFromOpeningFrameRuntime = runtimeLinkCueBound;
    playback.EponaCueResolvedFromOpeningFrameRuntime = runtimeEponaCueBound;
    playback.OpeningFrameRuntimeMountedLinkAttachmentErrorValid = false;
    playback.OpeningFrameRuntimeMountedLinkAttachmentErrorLength = 0.0;
    playback.QdbActorCueFallbackUsedForRender = false;
    playback.AddedActorVisualCount = 0;
    const size_t before = renderScene.ActorVisuals.size();
    const Oot3dTitleIntroOpeningActorMotionSample* linkActorMotionForRender =
        runtimeLinkCueBound && playback.OpeningFrameRuntimeStep.actorMotionActive != 0
            ? &playback.OpeningFrameRuntimeStep.actorMotion
            : nullptr;
    const Oot3dTitleIntroOpeningActorMotionSample* eponaActorMotionForRender =
        runtimeEponaCueBound && playback.OpeningFrameRuntimeStep.actorMotionActive != 0
            ? &playback.OpeningFrameRuntimeStep.actorMotion
            : nullptr;
    const size_t beforeEpona = renderScene.ActorVisuals.size();
    playback.Performance.RuntimeStateSeconds += PerformanceSecondsSince(performanceStart);

    performanceStart = std::chrono::steady_clock::now();
    const TitleIntroActorVisualAppendResult eponaVisual =
        AppendTitleIntroActorVisual(scene, playback.EponaActor, playback.EponaCue, playback.Frame,
                                    eponaActorMotionForRender, true, nullptr,
                                    &playback.HorseDustEmitterProfile,
                                    buildActorDiagnostics, renderScene);
    playback.OpeningFrameRuntimeEponaActorTransformUsedForRender =
        runtimeEponaCueBound && renderScene.ActorVisuals.size() > beforeEpona;
    playback.OpeningFrameRuntimeLinkActorSuppressedByPairedMountDraw = false;
    playback.OpeningFrameRuntimeLinkActorDrawStatus =
        Oot3d_TitleIntroOpeningActorRuntimeMountedLinkDrawNotProcessedStatus();
    TitleIntroMountedLinkContext mountedLinkContext;
    if (eponaVisual.RideActorRootInputValid) {
        mountedLinkContext.RideActorRootInputValid = true;
        mountedLinkContext.RideActorPosition = eponaVisual.RideActorPosition;
        mountedLinkContext.RideActorPoseOffset = eponaVisual.RideActorPoseOffset;
        mountedLinkContext.RideActorRotation = eponaVisual.RideActorRotation;
        mountedLinkContext.RideActorPoseOffsetNodeIndex = eponaVisual.RideActorPoseOffsetNodeIndex;
        mountedLinkContext.RideActorPoseOffsetBoneIndex = eponaVisual.RideActorPoseOffsetBoneIndex;
        mountedLinkContext.Source = Oot3d_TitleIntroOpeningActorRuntimeMountedLinkContextSource();
    }
    playback.Performance.EponaVisualSeconds += PerformanceSecondsSince(performanceStart);

    performanceStart = std::chrono::steady_clock::now();
    const size_t beforeLink = renderScene.ActorVisuals.size();
    const TitleIntroActorVisualAppendResult linkVisual =
        AppendTitleIntroActorVisual(scene, playback.LinkActor, playback.LinkCue, playback.Frame,
                                    linkActorMotionForRender, false, &mountedLinkContext,
                                    nullptr,
                                    buildActorDiagnostics, renderScene);
    playback.OpeningFrameRuntimeMountedLinkAttachmentErrorValid =
        linkVisual.MountedAttachmentErrorValid;
    playback.OpeningFrameRuntimeMountedLinkAttachmentErrorLength =
        linkVisual.MountedAttachmentErrorLength;
    playback.OpeningFrameRuntimeLinkActorDrawStatus =
        Oot3d_TitleIntroOpeningActorRuntimeMountedLinkDrawVisualStatus(
            renderScene.ActorVisuals.size() > beforeLink ? 1u : 0u);
    playback.OpeningFrameRuntimeLinkActorTransformUsedForRender =
        runtimeLinkCueBound && renderScene.ActorVisuals.size() > beforeLink;
    playback.OpeningFrameRuntimeActorMotionUsedForRender =
        playback.OpeningFrameRuntimeLinkActorTransformUsedForRender ||
        playback.OpeningFrameRuntimeEponaActorTransformUsedForRender;
    playback.Performance.LinkVisualSeconds += PerformanceSecondsSince(performanceStart);

    performanceStart = std::chrono::steady_clock::now();
    playback.DiagnosticCameraApplied = false;
    const bool openingFrameRuntimeCameraApplied =
        openingFrameRuntimeSampled && ApplyTitleIntroOpeningFrameRuntimeCamera(playback, camera);
    const bool initialSceneCameraApplied =
        openingFrameRuntimeCameraApplied ? false : ApplyTitleIntroInitialSceneCamera(playback, playback.Frame + 1.0, camera);
    if (!openingFrameRuntimeCameraApplied && !initialSceneCameraApplied &&
        !ApplyTitleIntroNativeCamera(playback, timelineFrame, camera)) {
        ThreeDsRecomp::Oot3d::Oot3dDemoBounds titleActorBounds;
        for (size_t i = before; i < renderScene.ActorVisuals.size(); ++i) {
            if (IsTitleIntroLogoRenderModel(renderScene.ActorVisuals[i])) {
                continue;
            }
            ThreeDsRecomp::Oot3d::NativeDemoExpandBoundsByBounds(
                titleActorBounds, ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelWorldBounds(renderScene.ActorVisuals[i]));
        }
        ApplyTitleIntroDiagnosticCamera(playback, camera, titleActorBounds);
    }
    playback.Performance.CameraSeconds += PerformanceSecondsSince(performanceStart);

    performanceStart = std::chrono::steady_clock::now();
    ThreeDsRecomp::Oot3d::ApplyOot3dNativePicaLightingToActorVisualRange(
        renderScene, before, renderScene.ActorVisuals.size() - before);
    playback.Performance.ActorLightingSeconds += PerformanceSecondsSince(performanceStart);

    UpdateAndAppendTitleIntroNativeEffects(playback, eponaVisual, camera, renderScene);

    performanceStart = std::chrono::steady_clock::now();
    ApplyTitleIntroOpeningFrameRuntimeLogoDraw(playback, renderScene, camera, aspect);
    playback.Performance.LogoDrawSeconds += PerformanceSecondsSince(performanceStart);

    performanceStart = std::chrono::steady_clock::now();
    playback.AddedActorVisualCount = renderScene.ActorVisuals.size() - before;
    RebuildRenderSceneBounds(renderScene, false);
    playback.Performance.BoundsSeconds += PerformanceSecondsSince(performanceStart);
    ++playback.Performance.SampleCount;
}

void SetTitleIntroPlaybackFrame(
    const Args& args,
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
    TitleIntroPlayback& playback,
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene,
    Camera& camera,
    size_t baseActorVisualCount,
    double aspect,
    double frame,
    const TitleIntroRenderSceneBuilder& buildRenderSceneForMode,
    bool buildActorDiagnostics) {
    if (!playback.Enabled || !playback.Initialized) {
        return;
    }
    playback.Frame = std::max(0.0, frame);
    ApplyTitleIntroPlaybackToRenderScene(
        args, scene, playback, renderScene, camera, baseActorVisualCount, aspect,
        buildRenderSceneForMode, buildActorDiagnostics);
}
