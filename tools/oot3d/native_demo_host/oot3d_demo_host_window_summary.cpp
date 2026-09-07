#include "oot3d_demo_host_window_summary.h"

#include <algorithm>

#include <nlohmann/json.hpp>

#include "oot3d_demo_host_diagnostics.h"
#include "oot3d_demo_host_actor_runtime.h"
#include "oot3d_demo_host_summary.h"
#include "oot3d_intro_cutscene_runtime_types.h"
#include "oot3d_link_animation.h"
#include "oot3d_link_instance.h"
#include "oot3d_link_locomotion_config.h"
#include "oot3d_link_runtime_types.h"
#include "oot3d_title_intro_diagnostics.h"
#include "oot3d_title_intro_runtime_types.h"
#include "oot3d_title_intro_visibility_diagnostics.h"

nlohmann::json BuildWindowDemoSummary(
    const Args& args, const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene, const LinkNativeLocomotionConfig& locomotionConfig,
    const NativeCameraConfig& cameraConfig, uint32_t frameCount, const Camera& camera,
    const IntroCutscenePlayback& introCutscene, const TitleIntroPlayback& titleIntro, const LinkInstance& link,
    const LinkMotionState& lastLinkMotion, const LinkAnimation& animation, const nlohmann::json& lastSubmission,
    const nlohmann::json& lastAdapterStats, const nlohmann::json& fast3dLifetimeStats, Fast::Fast3dWindow& window,
    const FramebufferScreenshotState& screenshotState) {
    auto summary = BaseSummary(args, scene, renderScene, locomotionConfig, cameraConfig);
    summary["self_test"] = false;
    summary["frame_count"] = frameCount;
    summary["frame_timing"] = {
        { "mode", args.FixedDeltaSeconds > 0.0 ? "fixed" : "wall_clock" },
        { "fixed_delta_seconds", args.FixedDeltaSeconds > 0.0
                                       ? nlohmann::json(args.FixedDeltaSeconds)
                                       : nlohmann::json(nullptr) },
    };
    summary["camera"] = CameraToJson(camera);
    summary["intro_cutscene_runtime"] = IntroCutscenePlaybackToJson(introCutscene);
    summary["title_intro_runtime"] = TitleIntroPlaybackToJson(titleIntro);
    summary["cutscene_player"] = {
        { "requested", args.CutscenePlayerRequest },
        { "scene_id", args.CutscenePlayerSceneId },
        { "setup_index", args.CutscenePlayerSetupIndex },
        { "cutscene_source_index", args.CutscenePlayerSourceIndex },
        { "orchestration_index",
          args.CutscenePlayerOrchestrationIndex == UINT16_MAX
              ? nlohmann::json(nullptr)
              : nlohmann::json(args.CutscenePlayerOrchestrationIndex) },
        { "fixed_frame", args.CutscenePlayerFrameOverride
                              ? nlohmann::json(args.CutscenePlayerFrame)
                              : nlohmann::json(nullptr) },
        { "adapter",
          args.CutscenePlayerRequest
              ? Oot3d_SceneCutsceneFrameAdapterKindName(
                    static_cast<Oot3dSceneCutsceneFrameAdapterKind>(args.CutscenePlayerAdapterKind))
              : "legacy_demo_host" },
        { "generic_frame_runtime_used",
          (titleIntro.GenericCutsceneRuntimeRequested && titleIntro.GenericCutsceneSnapshot.valid != 0) ||
              (introCutscene.GenericCutsceneRuntimeRequested && introCutscene.GenericSnapshot.valid != 0) },
        { "runtime_init_status",
          Oot3d_SceneCutsceneFrameRuntimeStatusName(
              titleIntro.GenericCutsceneRuntimeRequested
                  ? titleIntro.GenericCutsceneInitStatus
                  : introCutscene.GenericInitStatus) },
        { "runtime_step_status",
          Oot3d_SceneCutsceneFrameRuntimeStatusName(
              titleIntro.GenericCutsceneRuntimeRequested
                  ? titleIntro.GenericCutsceneStepStatus
                  : introCutscene.GenericStepStatus) },
    };
    if (args.TitleIntroPlayback) {
        summary["title_intro_visibility"] =
            TitleIntroVisibilityDiagnosticsToJson(
                renderScene, camera, std::max<uint32_t>(1, window.GetWidth()),
                std::max<uint32_t>(1, window.GetHeight()));
    }
    summary["link_instance"] = LinkInstanceToJson(scene, link, locomotionConfig);
    summary["link_motion"] = LinkMotionToJson(lastLinkMotion);
    summary["link_animation"] = LinkAnimationToJson(scene, animation, locomotionConfig);
    summary["actor_runtime"] = args.ActorRuntime ? args.ActorRuntime->Diagnostics()
                                                   : nlohmann::json{
                                                         { "enabled", false },
                                                         { "runtime_id", args.ActorRuntimeId },
                                                         { "status", args.ActorRuntimeStatus },
                                                     };
    summary["last_engine_renderer_submission"] = lastSubmission;
    summary["last_fast3d_adapter"] = lastAdapterStats;
    summary["fast3d_adapter_lifetime"] = fast3dLifetimeStats;
    summary["persistent_fast3d_backend"] = true;
    summary["window_backend_name"] = window.GetWindowBackendName();
    summary["framebuffer_screenshot"] = {
        { "path", args.ScreenshotPath.empty() ? nlohmann::json(nullptr) : nlohmann::json(args.ScreenshotPath.string()) },
        { "sequence", args.ScreenshotSequence },
        { "start_frame", args.ScreenshotStartFrame },
        { "sequence_interval", args.ScreenshotSequenceInterval },
        { "written", screenshotState.Written },
        { "written_count", screenshotState.WrittenCount },
        { "width", screenshotState.Width },
        { "height", screenshotState.Height },
        { "source", "fast3d_rendering_api_read_framebuffer_to_cpu" },
        { "depends_on_windows_foreground", false },
    };
    return summary;
}
