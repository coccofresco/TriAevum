#include "oot3d_demo_host_self_test_playback.h"

#include <cstdint>
#include <stdexcept>
#include <string>

#include "oot3d_demo_host_diagnostics.h"
#include "oot3d_demo_host_io.h"
#include "oot3d_demo_host_summary.h"
#include "oot3d_demo_host_view_projection.h"
#include "oot3d_intro_cutscene_playback.h"
#include "oot3d_native_camera_controller.h"
#include "oot3d_title_intro_diagnostics.h"
#include "oot3d_title_intro_playback_init.h"
#include "oot3d_title_intro_playback_render.h"
#include "oot3d_title_intro_render_guard.h"
#include "oot3d_title_intro_render_scene.h"

extern "C" {
#include "oot3d/scene_cutscene_intro_runtime.h"
}

bool TryRunPlaybackSelfTest(const Args& args, const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                            ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene,
                            const LinkNativeLocomotionConfig& locomotionConfig,
                            const NativeCameraConfig& cameraConfig, const LinkInstance& link) {
    if (args.TitleIntroPlayback) {
        Camera camera = InitialCamera(scene, cameraConfig, link);
        TitleIntroPlayback titleIntro = InitialTitleIntroPlayback(args, scene);
        titleIntro.SuppressedSceneActorVisualCount = renderScene.ActorVisuals.size();
        const size_t baseActorVisualCount = 0;
        ApplyTitleIntroPlaybackToRenderScene(
            args, scene, titleIntro, renderScene, camera, baseActorVisualCount,
            AspectFromDimensions(args.Width, args.Height), BuildRenderSceneForTitleIntroPlayback);
        BuildAndMaterializeCurrentFrameNativeViewProjection(
            renderScene, camera, AspectFromDimensions(args.Width, args.Height));

        auto summary = BaseSummary(args, scene, renderScene, locomotionConfig, cameraConfig);
        summary["self_test"] = true;
        summary["camera"] = CameraToJson(camera);
        summary["title_intro_runtime"] = TitleIntroPlaybackToJson(titleIntro);
        summary["link_instance"] = LinkInstanceToJson(scene, link, locomotionConfig);
        LinkMotionState noMotion;
        summary["link_motion"] = LinkMotionToJson(noMotion);
        summary["persistent_fast3d_backend"] = false;
        const auto renderGuard = BuildTitleIntroRenderGuard(renderScene, titleIntro);
        summary["title_intro_render_guard"] = renderGuard;
        WriteJsonFile(args.OutputPath, summary);
        if (!renderGuard.value("passed", false)) {
            throw std::runtime_error("title intro render guard failed; see title_intro_render_guard in " +
                                     args.OutputPath.string());
        }
        return true;
    }
    if (args.IntroCutscenePlayback) {
        Camera camera = InitialCamera(scene, cameraConfig, link);
        IntroCutscenePlayback introCutscene = InitialIntroCutscenePlayback(args);
        const uint32_t stepCount = args.FrameLimit > 0 ? args.FrameLimit : 1;
        for (uint32_t stepIndex = 0; stepIndex < stepCount; ++stepIndex) {
            StepIntroCutscenePlayback(introCutscene, camera);
            if (introCutscene.LastStatus == OOT3D_CUTSCENE_INTRO_RUNTIME_COMPLETE) {
                break;
            }
        }

        auto summary = BaseSummary(args, scene, renderScene, locomotionConfig, cameraConfig);
        summary["self_test"] = true;
        summary["camera"] = CameraToJson(camera);
        summary["intro_cutscene_runtime"] = IntroCutscenePlaybackToJson(introCutscene);
        summary["link_instance"] = LinkInstanceToJson(scene, link, locomotionConfig);
        LinkMotionState noMotion;
        summary["link_motion"] = LinkMotionToJson(noMotion);
        summary["persistent_fast3d_backend"] = false;
        WriteJsonFile(args.OutputPath, summary);
        return true;
    }
    return false;
}
