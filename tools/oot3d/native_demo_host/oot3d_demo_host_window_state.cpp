#include "oot3d_demo_host_window_state.h"

#include "oot3d_demo_host_view_projection.h"
#include "oot3d_demo_host_actor_runtime.h"
#include "oot3d_intro_cutscene_playback.h"
#include "oot3d_link_animation.h"
#include "oot3d_link_instance.h"
#include "oot3d_link_locomotion_config.h"
#include "oot3d_link_render.h"
#include "oot3d_link_surface_state.h"
#include "oot3d_native_camera_config.h"
#include "oot3d_native_camera_controller.h"
#include "oot3d_title_intro_playback_init.h"
#include "oot3d_title_intro_playback_render.h"
#include "oot3d_title_intro_render_scene.h"

WindowDemoState InitializeWindowDemoState(const Args& args) {
    WindowDemoState state;
    state.Scene = ThreeDsRecomp::Oot3d::LoadOot3dNativeDemoSceneFromManifest(
        args.ManifestPath, ThreeDsRecomp::Oot3d::kDefaultLinkStandingCsabName, args.EntranceIndex,
        ActiveSceneSetupOverrideForArgs(args), ActiveSceneSetupSourceForArgs(args),
        args.NativePlayerClips ? &*args.NativePlayerClips : nullptr);
    state.RenderScene = args.TitleIntroPlayback
                            ? BuildRenderSceneForTitleIntroPlayback(
                                  args, state.Scene, nullptr, static_cast<float>(args.MaterialAnimationFrame))
                            : BuildRenderSceneForMode(args, state.Scene);
    if (args.ActorRuntime) {
        args.ActorRuntime->Initialize(state.Scene, state.RenderScene);
    }
    state.LocomotionConfig = BuildLinkNativeLocomotionConfig(state.Scene);
    state.CameraConfig = BuildNativeCameraConfig(state.Scene);
    state.Link = InitialLinkInstance(state.Scene, state.LocomotionConfig);
    state.ResetLink = state.Link;
    state.Animation = InitialLinkAnimation(state.Scene, state.LocomotionConfig);
    if (!args.TitleIntroPlayback) {
        ApplyLinkAnimationFrame(state.Scene, state.LocomotionConfig, state.RenderScene, state.Animation, state.Link);
        ReapplyRenderModeAfterLinkPoseUpdate(args, state.Scene, state.RenderScene);
        UpdateLinkActorShadowState(state.Scene, state.Link, state.RenderScene);
    }
    state.CameraState = InitialCamera(state.Scene, state.CameraConfig, state.Link);
    state.IntroCutscene = InitialIntroCutscenePlayback(args);
    state.TitleIntro = InitialTitleIntroPlayback(args, state.Scene);
    const size_t originalActorVisualCount = state.RenderScene.ActorVisuals.size();
    state.BaseActorVisualCount = originalActorVisualCount;
    if (state.TitleIntro.Enabled) {
        state.TitleIntro.SuppressedSceneActorVisualCount = 0;
        ApplyTitleIntroPlaybackToRenderScene(
            args, state.Scene, state.TitleIntro, state.RenderScene, state.CameraState, state.BaseActorVisualCount,
            AspectFromDimensions(args.Width, args.Height), BuildRenderSceneForTitleIntroPlayback,
            args.TitleIntroActorDiagnostics);
    }
    return state;
}
