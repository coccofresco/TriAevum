#include "oot3d_demo_host_playback_frame.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "oot3d_demo_host_camera_controls.h"
#include "oot3d_demo_host_actor_runtime.h"
#include "oot3d_demo_host_link_controls.h"
#include "oot3d_demo_host_player_controller.h"
#include "oot3d_demo_host_view_projection.h"
#include "oot3d_intro_cutscene_playback.h"
#include "oot3d_link_animation.h"
#include "oot3d_link_instance.h"
#include "oot3d_link_movement.h"
#include "oot3d_link_render.h"
#include "oot3d_link_surface_state.h"
#include "oot3d_title_intro_playback_render.h"
#include "oot3d_title_intro_render_scene.h"
#include "oot3d_title_intro_runtime_environment.h"

namespace {

double PerformanceSecondsSince(std::chrono::steady_clock::time_point begin) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
}

void SetActorRuntimeFrameContext(const Args& args, const LinkInstance& link,
                                 const Camera& camera, bool useViewEye) {
    if (!args.ActorRuntime) {
        return;
    }
    const auto player = LinkActorPosition(link);
    Oot3dDemoHostActorFrameContext context;
    context.PlayerActorPositionValid = true;
    context.PlayerActorX = player.X;
    context.PlayerActorY = player.Y;
    context.PlayerActorZ = player.Z;
    context.ViewEyeValid = true;
    context.ViewEyeX = camera.Position.X;
    context.ViewEyeY = camera.Position.Y;
    context.ViewEyeZ = camera.Position.Z;
    context.ViewTargetX = camera.Target.X;
    context.ViewTargetY = camera.Target.Y;
    context.ViewTargetZ = camera.Target.Z;
    context.ViewUpX = camera.Up.X;
    context.ViewUpY = camera.Up.Y;
    context.ViewUpZ = camera.Up.Z;
    context.UseViewEyeForTransitionActors = useViewEye;
    args.ActorRuntime->SetFrameContext(context);
}

} // namespace

bool UpdateWindowDemoPlaybackFrame(
    const Args& args, const Oot3dDemoHostInputState& input,
    Fast::Fast3dWindow& window, const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene, const LinkNativeLocomotionConfig& locomotionConfig,
    const NativeCameraConfig& cameraConfig, size_t baseActorVisualCount, double aspect, double dt,
    std::chrono::steady_clock::time_point now, const LinkInstance& resetLink, LinkInstance& link,
    LinkAnimation& animation, Camera& camera, IntroCutscenePlayback& introCutscene, TitleIntroPlayback& titleIntro,
    LinkMotionState& lastLinkMotion, WindowDemoPlaybackTiming& timing) {
    window.GetMouseStateManager()->StartFrame();
    if (input.Exit) {
        window.Close();
        return false;
    }
    if (titleIntro.Enabled) {
        const auto phaseStart = std::chrono::steady_clock::now();
        lastLinkMotion = {};
        if (!timing.TitleIntroStartTimeResolved) {
            timing.TitleIntroStartTime = now;
            timing.TitleIntroStartTimeResolved = true;
        }
        const double titleIntroElapsedSeconds =
            std::chrono::duration<double>(now - timing.TitleIntroStartTime).count();
        const double titleIntroFrame =
            args.TitleIntroFrameOverride
                ? args.TitleIntroFrame
                : titleIntro.StartFrame + titleIntroElapsedSeconds * titleIntro.FramesPerSecond;
        SetTitleIntroPlaybackFrame(args, scene, titleIntro, renderScene, camera, baseActorVisualCount, aspect,
                                   titleIntroFrame, BuildRenderSceneForTitleIntroPlayback,
                                   args.TitleIntroActorDiagnostics);
        ++timing.PerformanceSampleCount;
        timing.TitleIntroSeconds += PerformanceSecondsSince(phaseStart);
        return true;
    }
    if (introCutscene.Enabled) {
        lastLinkMotion = {};
        if (!timing.IntroCutsceneStartTimeResolved) {
            timing.IntroCutsceneStartTime = now;
            timing.IntroCutsceneStartTimeResolved = true;
        }
        const double elapsedSeconds =
            std::chrono::duration<double>(now - timing.IntroCutsceneStartTime).count();
        const int32_t targetFrame = introCutscene.GenericCutsceneRuntimeRequested
            ? static_cast<int32_t>(std::max(
                  1.0,
                  std::floor(
                      args.CutscenePlayerFrameOverride
                          ? args.CutscenePlayerFrame + 1.0
                          : introCutscene.StartFrame + elapsedSeconds * introCutscene.FramesPerSecond + 1.0)))
            : -1;
        auto phaseStart = std::chrono::steady_clock::now();
        StepIntroCutscenePlayback(introCutscene, camera, targetFrame);
        timing.CameraSeconds += PerformanceSecondsSince(phaseStart);
        if (introCutscene.GenericCutsceneRuntimeRequested &&
            introCutscene.GenericSnapshot.valid != 0) {
            const auto environment =
                CutsceneFrameRuntimeEnvironmentInput(introCutscene.GenericSnapshot);
            ThreeDsRecomp::Oot3d::RefreshOot3dNativeDemoRenderSceneRuntimeEnvironment(
                scene, renderScene, RuntimeEnvironmentInputPtr(environment),
                static_cast<float>(introCutscene.GenericSnapshot.frame));
        }
        phaseStart = std::chrono::steady_clock::now();
        AdvanceLinkAnimation(scene, locomotionConfig, animation, lastLinkMotion, dt);
        ApplyLinkAnimationFrame(scene, locomotionConfig, renderScene, animation, link);
        timing.PlayerAnimationSeconds += PerformanceSecondsSince(phaseStart);
        phaseStart = std::chrono::steady_clock::now();
        ReapplyRenderModeAfterLinkPoseUpdate(args, scene, renderScene);
        UpdateLinkActorShadowState(scene, link, renderScene);
        timing.PlayerPostPoseSeconds += PerformanceSecondsSince(phaseStart);
        if (args.ActorRuntime) {
            phaseStart = std::chrono::steady_clock::now();
            SetActorRuntimeFrameContext(args, link, camera, true);
            args.ActorRuntime->Update(scene, renderScene, dt);
            timing.ActorRuntimeSeconds += PerformanceSecondsSince(phaseStart);
        }
        ++timing.PerformanceSampleCount;
        return true;
    }

    auto phaseStart = std::chrono::steady_clock::now();
    lastLinkMotion = args.PlayerController
                         ? args.PlayerController->UpdateMovement(
                               input, scene, locomotionConfig, camera, link, resetLink, dt)
                         : UpdateLinkInstance(input, scene, locomotionConfig, camera, link, resetLink, dt);
    timing.PlayerMovementSeconds += PerformanceSecondsSince(phaseStart);
    phaseStart = std::chrono::steady_clock::now();
    UpdateCamera(scene, cameraConfig, link, camera, dt);
    timing.CameraSeconds += PerformanceSecondsSince(phaseStart);
    phaseStart = std::chrono::steady_clock::now();
    if (args.PlayerController) {
        args.PlayerController->UpdateAnimation(
            scene, locomotionConfig, renderScene, animation, lastLinkMotion, link, dt);
    } else {
        AdvanceLinkAnimation(scene, locomotionConfig, animation, lastLinkMotion, dt);
        ApplyLinkAnimationFrame(scene, locomotionConfig, renderScene, animation, link);
    }
    timing.PlayerAnimationSeconds += PerformanceSecondsSince(phaseStart);
    phaseStart = std::chrono::steady_clock::now();
    ReapplyRenderModeAfterLinkPoseUpdate(args, scene, renderScene);
    UpdateLinkActorShadowState(scene, link, renderScene);
    timing.PlayerPostPoseSeconds += PerformanceSecondsSince(phaseStart);
    if (args.ActorRuntime) {
        phaseStart = std::chrono::steady_clock::now();
        SetActorRuntimeFrameContext(args, link, camera, false);
        args.ActorRuntime->Update(scene, renderScene, dt);
        timing.ActorRuntimeSeconds += PerformanceSecondsSince(phaseStart);
    }
    ++timing.PerformanceSampleCount;
    return true;
}
