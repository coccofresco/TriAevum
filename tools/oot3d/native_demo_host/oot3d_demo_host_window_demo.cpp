#include "oot3d_demo_host_window_demo.h"

#include <chrono>
#include <cstdint>

#include <nlohmann/json.hpp>

#include "fast/Fast3dWindow.h"
#include "ship/Context.h"
#include "ship/audio/Audio.h"
#include "ship/audio/AudioPlayer.h"
#include "three_ds_recomp/oot3d/Oot3dNativeFast3dRenderer.h"

#include "oot3d_demo_host_context.h"
#include "oot3d_demo_host_actor_runtime.h"
#include "oot3d_demo_host_input.h"
#include "oot3d_demo_host_io.h"
#include "oot3d_demo_host_playback_frame.h"
#include "oot3d_demo_host_render_frame.h"
#include "oot3d_demo_host_screenshot.h"
#include "oot3d_demo_host_window_summary.h"
#include "oot3d_demo_host_window_state.h"
#include "oot3d_demo_host_window_timing.h"
#include "oot3d_link_instance.h"
#include "oot3d_link_runtime_types.h"
#include "oot3d_title_intro_actor_render.h"

namespace {

struct WindowDemoPhaseTiming {
    double PlaybackUpdateSeconds = 0.0;
    double AudioMixSeconds = 0.0;
    double RenderSubmitSeconds = 0.0;
    double RenderFrameStartSeconds = 0.0;
    double RenderSetupSeconds = 0.0;
    double RenderSceneSubmitSeconds = 0.0;
    double PostRenderSeconds = 0.0;
    double PresentSeconds = 0.0;
};

double SecondsBetween(std::chrono::steady_clock::time_point begin,
                      std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<double>(end - begin).count();
}

void PumpNativeAudio(const Args& args) {
    if (!args.ActorRuntime) {
        return;
    }
    auto* context = Ship::Context::GetRawInstance();
    auto audio = context != nullptr ? context->GetAudio() : nullptr;
    auto player = audio != nullptr ? audio->GetAudioPlayer() : nullptr;
    if (!player || !player->IsInitialized()) {
        return;
    }
    for (size_t block = 0;
         block < 4 && player->Buffered() < player->GetDesiredBuffered(); ++block) {
        std::vector<int16_t> samples;
        if (!args.ActorRuntime->MixAudio(
                static_cast<uint32_t>(player->GetSampleRate()),
                static_cast<size_t>(player->GetSampleLength()), samples)) {
            return;
        }
        if (samples.size() != static_cast<size_t>(player->GetSampleLength()) * 2) {
            throw std::runtime_error("native audio consumer returned an invalid stereo block");
        }
        player->Play(reinterpret_cast<const uint8_t*>(samples.data()),
                     samples.size() * sizeof(int16_t));
    }
}

} // namespace

void RunWindowDemo(const Args& args) {
    InitContextForDemo(args);
    auto demo = InitializeWindowDemoState(args);
    auto& window = GetActiveFast3dWindowForDemo();
    window.SetTextureFilter(Fast::FILTER_LINEAR);
    auto& api = GetActiveRenderingApiForDemo(window);

    uint32_t frameCount = 0;
    nlohmann::json lastSubmission;
    nlohmann::json lastAdapterStats;
    nlohmann::json fast3dLifetimeStats;
    LinkMotionState lastLinkMotion;
    FramebufferScreenshotState screenshotState;
    WindowDemoPlaybackTiming playbackTiming;
    WindowDemoTimingState timing;
    WindowDemoPhaseTiming phaseTiming;
    auto loopEndTime = timing.StartTime;
    const auto inputTimeline =
        Oot3dDemoHostInputTimeline::LoadFile(args.InputTimelinePath);
    Oot3dDemoHostInputDiagnostics inputDiagnostics;
    nlohmann::json rendererParityCheckpoints = nlohmann::json::array();

    {
        ThreeDsRecomp::Oot3d::Oot3dNativeFast3dRenderConfig renderConfig;
        renderConfig.EnableAlphaBlend = false;
        ThreeDsRecomp::Oot3d::Oot3dNativeFast3dRenderBackend backend(api, renderConfig);

        while (window.IsRunning()) {
            WindowDemoFrameTiming frameTiming;
            if (!PrepareNextWindowDemoFrame(args, window, timing, frameTiming)) {
                continue;
            }

            const uint32_t width = std::max<uint32_t>(1, window.GetWidth());
            const uint32_t height = std::max<uint32_t>(1, window.GetHeight());
            const double aspect = static_cast<double>(width) / static_cast<double>(height);
            const auto frameInput =
                ResolveOot3dDemoHostInput(window, inputTimeline, frameCount);
            inputDiagnostics.Observe(frameInput);

            const auto playbackUpdateStart = std::chrono::steady_clock::now();
            const bool playbackUpdated =
                UpdateWindowDemoPlaybackFrame(args, frameInput, window, demo.Scene,
                                              demo.RenderScene, demo.LocomotionConfig,
                                              demo.CameraConfig, demo.BaseActorVisualCount, aspect,
                                              frameTiming.DeltaSeconds, frameTiming.Now, demo.ResetLink, demo.Link,
                                              demo.Animation, demo.CameraState, demo.IntroCutscene, demo.TitleIntro,
                                              lastLinkMotion, playbackTiming);
            phaseTiming.PlaybackUpdateSeconds +=
                SecondsBetween(playbackUpdateStart, std::chrono::steady_clock::now());
            if (!playbackUpdated) {
                continue;
            }
            const auto audioMixStart = std::chrono::steady_clock::now();
            PumpNativeAudio(args);
            phaseTiming.AudioMixSeconds +=
                SecondsBetween(audioMixStart, std::chrono::steady_clock::now());

            if (ShouldWriteFramebufferScreenshot(args, frameCount, screenshotState)) {
                const auto actorPosition = LinkActorPosition(demo.Link);
                rendererParityCheckpoints.push_back({
                    { "frame", frameCount },
                    { "input", {
                        { "move_x", frameInput.MoveX },
                        { "move_y", frameInput.MoveY },
                        { "fast", frameInput.Fast },
                        { "reset", frameInput.Reset },
                        { "script_segment_index", frameInput.ScriptSegmentIndex },
                      } },
                    { "link", {
                        { "actor_position", {
                            { "x", actorPosition.X },
                            { "y", actorPosition.Y },
                            { "z", actorPosition.Z },
                          } },
                        { "grounded", demo.Link.Grounded },
                        { "floor_polygon_index", demo.Link.FloorPolygonIndex },
                        { "floor_surface_type", demo.Link.FloorSurfaceType },
                        { "horizontal_collision", demo.Link.HorizontalCollision },
                        { "wall_polygon_index", demo.Link.WallPolygonIndex },
                        { "native_player_action",
                          NativePlayerActionName(demo.Link.NativePlayerAction) },
                        { "linear_velocity_units_per_tick",
                          demo.Link.NativeLinearVelocityUnitsPerTick },
                        { "movement_yaw", demo.Link.MovementYaw },
                        { "shape_yaw", demo.Link.Yaw },
                      } },
                    { "animation", {
                        { "clip_id", demo.Animation.ClipId },
                        { "frame", demo.Animation.Frame },
                        { "locomotion_class",
                          demo.Animation.LocomotionAnimationClass },
                        { "locomotion_state",
                          demo.Animation.NativeLocomotionState },
                      } },
                    { "camera", {
                        { "position", {
                            { "x", demo.CameraState.Position.X },
                            { "y", demo.CameraState.Position.Y },
                            { "z", demo.CameraState.Position.Z },
                          } },
                        { "target", {
                            { "x", demo.CameraState.Target.X },
                            { "y", demo.CameraState.Target.Y },
                            { "z", demo.CameraState.Target.Z },
                          } },
                        { "native_set", demo.CameraState.NativeSet },
                        { "native_function", demo.CameraState.NativeFunction },
                        { "native_collision_active",
                          demo.CameraState.NativeCollisionActive },
                      } },
                });
            }

            const auto renderSubmitStart = std::chrono::steady_clock::now();
            const auto renderFrame = RenderWindowDemoFrame(window, api, renderConfig, backend, demo.RenderScene,
                                                           demo.CameraState, aspect, width, height, false);
            phaseTiming.RenderSubmitSeconds +=
                SecondsBetween(renderSubmitStart, std::chrono::steady_clock::now());
            phaseTiming.RenderFrameStartSeconds += renderFrame.FrameStartSeconds;
            phaseTiming.RenderSetupSeconds += renderFrame.SetupSeconds;
            phaseTiming.RenderSceneSubmitSeconds += renderFrame.SceneSubmitSeconds;

            const auto postRenderStart = std::chrono::steady_clock::now();
            if (args.TitleIntroPlayback) {
                StripSubmittedTitleIntroActorTexturePayloads(demo.TitleIntro);
            }
            if (!renderFrame.Submission.is_null()) {
                lastSubmission = renderFrame.Submission;
            }
            if (!renderFrame.AdapterStats.is_null()) {
                lastAdapterStats = renderFrame.AdapterStats;
            }

            MaybeWriteFramebufferScreenshot(args, api, width, height, frameCount, screenshotState);
            phaseTiming.PostRenderSeconds +=
                SecondsBetween(postRenderStart, std::chrono::steady_clock::now());

            const auto presentStart = std::chrono::steady_clock::now();
            window.EndFrame();
            phaseTiming.PresentSeconds +=
                SecondsBetween(presentStart, std::chrono::steady_clock::now());
            ++frameCount;

            ApplyWindowDemoFrameLimit(args, window, frameCount);
        }

        loopEndTime = std::chrono::steady_clock::now();
        fast3dLifetimeStats = ThreeDsRecomp::Oot3d::Oot3dNativeFast3dRenderStatsToJson(backend.Stats());
    }

    auto summary = BuildWindowDemoSummary(args, demo.Scene, demo.RenderScene, demo.LocomotionConfig, demo.CameraConfig,
                                          frameCount, demo.CameraState, demo.IntroCutscene, demo.TitleIntro, demo.Link,
                                          lastLinkMotion, demo.Animation, lastSubmission, lastAdapterStats,
                                          fast3dLifetimeStats, window, screenshotState);
    const double frameDivisor = frameCount > 0 ? static_cast<double>(frameCount) : 1.0;
    const double playbackDivisor = playbackTiming.PerformanceSampleCount > 0
                                       ? static_cast<double>(playbackTiming.PerformanceSampleCount)
                                       : 1.0;
    const double measuredWorkSeconds = phaseTiming.PlaybackUpdateSeconds +
                                       phaseTiming.AudioMixSeconds +
                                       phaseTiming.RenderSubmitSeconds +
                                       phaseTiming.PostRenderSeconds;
    const double loopWallSeconds = SecondsBetween(timing.StartTime, loopEndTime);
    summary["performance"] = {
        { "measured_frame_count", frameCount },
        { "playback_update_total_seconds", phaseTiming.PlaybackUpdateSeconds },
        { "playback_update_average_ms", phaseTiming.PlaybackUpdateSeconds * 1000.0 / frameDivisor },
        { "audio_mix_total_seconds", phaseTiming.AudioMixSeconds },
        { "audio_mix_average_ms", phaseTiming.AudioMixSeconds * 1000.0 / frameDivisor },
        { "render_submit_total_seconds", phaseTiming.RenderSubmitSeconds },
        { "render_submit_average_ms", phaseTiming.RenderSubmitSeconds * 1000.0 / frameDivisor },
        { "render_submit_breakdown", {
            { "frame_start_average_ms", phaseTiming.RenderFrameStartSeconds * 1000.0 / frameDivisor },
            { "setup_average_ms", phaseTiming.RenderSetupSeconds * 1000.0 / frameDivisor },
            { "scene_submit_average_ms", phaseTiming.RenderSceneSubmitSeconds * 1000.0 / frameDivisor },
          } },
        { "post_render_total_seconds", phaseTiming.PostRenderSeconds },
        { "post_render_average_ms", phaseTiming.PostRenderSeconds * 1000.0 / frameDivisor },
        { "present_total_seconds", phaseTiming.PresentSeconds },
        { "present_average_ms", phaseTiming.PresentSeconds * 1000.0 / frameDivisor },
        { "measured_work_total_seconds", measuredWorkSeconds },
        { "measured_work_average_ms", measuredWorkSeconds * 1000.0 / frameDivisor },
        { "measured_work_fps", measuredWorkSeconds > 0.0
                                   ? frameDivisor / measuredWorkSeconds
                                   : 0.0 },
        { "loop_wall_total_seconds", loopWallSeconds },
        { "loop_wall_average_ms", loopWallSeconds * 1000.0 / frameDivisor },
        { "loop_wall_fps", loopWallSeconds > 0.0
                               ? frameDivisor / loopWallSeconds
                               : 0.0 },
        { "playback_breakdown", {
            { "sample_count", playbackTiming.PerformanceSampleCount },
            { "title_intro_average_ms", playbackTiming.TitleIntroSeconds * 1000.0 / playbackDivisor },
            { "player_movement_average_ms", playbackTiming.PlayerMovementSeconds * 1000.0 / playbackDivisor },
            { "camera_average_ms", playbackTiming.CameraSeconds * 1000.0 / playbackDivisor },
            { "player_animation_average_ms", playbackTiming.PlayerAnimationSeconds * 1000.0 / playbackDivisor },
            { "player_post_pose_average_ms", playbackTiming.PlayerPostPoseSeconds * 1000.0 / playbackDivisor },
            { "actor_runtime_average_ms", playbackTiming.ActorRuntimeSeconds * 1000.0 / playbackDivisor },
          } },
        { "source", "steady_clock_window_demo_phase_accumulators" },
    };
    summary["input_playback"] = {
        { "mode", inputTimeline.Enabled() ? "scripted_timeline" : "window_keyboard" },
        { "timeline_path", inputTimeline.Enabled()
                               ? nlohmann::json(inputTimeline.SourcePath().string())
                               : nlohmann::json(nullptr) },
        { "timeline_segment_count", inputTimeline.SegmentCount() },
        { "sampled_frame_count", inputDiagnostics.SampledFrameCount },
        { "scripted_frame_count", inputDiagnostics.ScriptedFrameCount },
        { "movement_frame_count", inputDiagnostics.MovementFrameCount },
        { "fast_frame_count", inputDiagnostics.FastFrameCount },
        { "reset_frame_count", inputDiagnostics.ResetFrameCount },
        { "last_state", {
            { "move_x", inputDiagnostics.LastState.MoveX },
            { "move_y", inputDiagnostics.LastState.MoveY },
            { "fast", inputDiagnostics.LastState.Fast },
            { "reset", inputDiagnostics.LastState.Reset },
            { "scripted", inputDiagnostics.LastState.Scripted },
            { "script_segment_index", inputDiagnostics.LastState.ScriptSegmentIndex },
          } },
        { "windows_input_injection_used", false },
    };
    summary["renderer_parity_checkpoints"] = std::move(rendererParityCheckpoints);
    WriteJsonFile(args.OutputPath, summary);

    Ship::Context::DestroyInstance();
}
