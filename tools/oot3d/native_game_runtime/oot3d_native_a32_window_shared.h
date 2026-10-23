#pragma once

// Shared declarations of the A32 window host (oot3d_native_a32_window.cpp) and its topic files:
// title addresses, the dispatch and projection state, and the functions the window calls.

#include "oot3d_native_a32_window.h"
#include "oot3d_dev_free_camera.h"
#include "three_ds_sdl_controller.h"
#include "oot3d_prebackend_budget.h"
#include "oot3d_cpu_phase_probe.h"
#include "fast/renderer/frame_time_distribution.h"
#include "fast/renderer/slow_frame_samples.h"
#include "fast/renderer3ds/pica_program_preparation.h"
#include "fast/renderer3ds/slow_step_trace.h"
#include "oot3d_native_preparation_screen.h"
#include "oot3d_game_language_panel.h"
#include "oot3d_graphics_settings_schedule.h"
#include "oot3d_scene_environment.h"
#if defined(TRIAEVUM_DEVELOPER_TOOLS)
#include "native_a32_read_probe.h"
#endif
#ifdef OOT3D_WHOLE_AOT_PRODUCT_MODE
#include "oot3d_native_crash_diagnostics.h"
#endif

#include "oot3d_a32_provenance.h"
#include "oot3d_demo_host_context.h"
#include "oot3d_demo_host_io.h"
#include "oot3d_demo_host_screenshot.h"
#include "oot3d_demo_host_window_timing.h"
#include "oot3d_legacy_ui_adapters.h"
#include "oot3d_n64_ui_renderer.h"
#include "oot3d_native_a32_ctr_host.h"
#include "oot3d_native_a32_dsp_hle.h"
#include "oot3d_native_a32_dsp_memory_adapter.h"
#include "oot3d_native_a32_process_image.h"
#include "oot3d_native_a32_registry.h"
#include "oot3d_native_a32_savestate.h"
#include "oot3d_native_a32_scene_view.h"
#include "oot3d_native_actor_interactions.h"
#include "oot3d_native_a32_actor_visibility.h"
#include "fast/oot3d/grass_interaction_bridge.h"
#include "oot3d_native_a32_timing_probe.h"
#include "oot3d_native_compiled_functions.h"
#include "oot3d_native_controls_settings_panel.h"
#include "oot3d_native_frame_rate.h"
#include "oot3d_native_game_bootstrap.h"
#include "oot3d_native_mass_aot.h"
#include "oot3d_native_pcm_diagnostics.h"
#include "oot3d_native_presentation_pacer.h"
#include "oot3d_native_pica_frontend.h"
#include "oot3d_native_pica_composition.h"
#include "mm3d_guest_patches.h"
#include "mm3d_restoration.h"
#include "mm3d_single_screen.h"
#include "mm3d_pica_composition.h"
#include "mm3d_title_contracts.h"
#include "oot3d_native_pica_presentation_scheduler.h"
#include "oot3d_native_render_command_stream.h"
#include "oot3d_native_pica_semantic_trace.h"
#include "oot3d_native_pica_submission.h"
#include "oot3d_native_pica_visual_frame.h"
#include "oot3d_native_pica_visual_savestate.h"
#include "oot3d_native_pica_vulkan_bridge.h"
#include "oot3d_native_pica_vulkan_plan.h"
#include "oot3d_native_player_temporal_bridge.h"
#include "oot3d_native_scenario_bootstrap.h"
#include "oot3d_native_temporal_event_ledger.h"
#include "oot3d_native_true_aot_blocks.h"
#include "oot3d_native_ui_lifecycle_bridge.h"
#include "oot3d_native_ui_texture_provider.h"
#include "oot3d_top_screen_ocarina_text_runtime.h"
#include "oot3d_top_screen_pause_transaction.h"
#include "oot3d_top_screen_item_description_runtime.h"
#include "oot3d_top_screen_wide_items_runtime.h"
#include "oot3d_top_screen_gameplay_runtime.h"
#include "oot3d_object_bank_extension.h"
#include "oot3d_top_screen_rain_adapter.h"
#include "oot3d_native_whole_aot_runtime.h"
#ifdef OOT3D_NATIVE_DIRECT_AOT_PLUGIN
#include "oot3d_native_direct_aot.h"
#endif
#include "oot3d_top_screen_camera_guest.h"
#include "oot3d_top_screen_controls.h"
#include "oot3d_top_screen_frontend_composition.h"
#include "oot3d_top_screen_gameplay_actions.h"
#include "oot3d_top_screen_gameplay_action_consumer.h"
#include "oot3d_top_screen_item_guest.h"
#include "oot3d_top_screen_item_dispatch.h"
#include "oot3d_top_screen_input_cadence.h"
#include "oot3d_top_screen_items_hint.h"
#include "oot3d_top_screen_settings_panel.h"
#include "oot3d_top_screen_texture_overrides.h"
#include "oot3d_native_hd_font_runtime.h"
#include "oot3d_top_screen_texture_runtime.h"
#include "oot3d_typed_gameplay_bridge.h"

#include "fast/Fast3dGui.h"
#include "fast/Fast3dWindow.h"
#include "fast/oot3d/graphics_settings_persistence.h"
#include "fast/oot3d/graphics_settings_runtime.h"
#include "fast/oot3d/graphics_settings_window.h"
#include "fast/oot3d/perspective_fov_policy.h"
#include "fast/oot3d/presentation_pacing_policy.h"
#include "fast/oot3d/title_render_backend.h"
#include "ship/Context.h"
#include "ship/audio/Audio.h"
#include "ship/audio/AudioPlayer.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/controller/controldevice/controller/mapping/keyboard/KeyboardScancodes.h"
#include "ship/controller/physicaldevice/ConnectedPhysicalDeviceManager.h"
#include "ship/controller/physicaldevice/GlobalSDLDeviceSettings.h"
#include "ship/window/gui/Gui.h"

#include <SDL2/SDL.h>
#include <imgui.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif


namespace Oot3dA32Window {

inline constexpr uint64_t kCtrArm11TicksPerSecond = 268111856ULL;
inline constexpr uint64_t kGuestDisplayRefreshRate =
    Oot3dNativeGame::kOot3dNativeTimeUnitsPerSecond;
inline constexpr uint32_t kMinimumHostAudioPrebufferMilliseconds = 100U;
inline constexpr uint64_t kA32RuntimeSampleDenominator = 64ULL;
inline constexpr uint64_t kNativeDependencyRenderTargetNamespace = 1ULL;
inline constexpr uint64_t kVisualInterpolationRenderTargetNamespace = 2ULL;
inline constexpr size_t kMaximumPicaDrainPassesPerRefresh = 64U;
inline constexpr uint32_t kMtx4x4BuildFrustum = 0x002FDE9CU;
inline constexpr uint32_t kMtx4x4BuildOrthographicProjectionRotated = 0x00300AA8U;
inline constexpr uint32_t kEnMagInit = 0x0018CBB8U;
inline constexpr uint32_t kEnMagDraw = 0x001DA4F4U;
inline constexpr uint32_t kEnMagUpdate = 0x001DA9F8U;
inline constexpr uint32_t kTopScreenTitleLogoFadeHoldPrefix = 0x001DADCCU;
inline constexpr uint32_t kPauseUiUpdate = 0x0041E968U;
inline constexpr uint32_t kPauseUiDraw = 0x0041EC50U;
inline constexpr uint32_t kPauseUiDrawReturn = 0x00419830U;
inline constexpr uint32_t kPauseAlternateViewportDrawCall = 0x00419850U;
inline constexpr uint32_t kPauseAlternateViewportNativeDraw = 0x0041D1A8U;
inline constexpr uint32_t kPauseConditionalDrawCall = 0x00419874U;
inline constexpr uint32_t kPauseConditionalNativeDraw = 0x0041C500U;
inline constexpr uint32_t kPauseRoutedCommandCallA = 0x0041952CU;
inline constexpr uint32_t kPauseRoutedCommandCallB = 0x004198B0U;
inline constexpr uint32_t kPauseRoutedCommandNative = 0x00300240U;
inline constexpr uint32_t kPauseOverlayViewportDrawCall = 0x00419838U;
inline constexpr uint32_t kPauseOverlayNativeDraw = 0x0041F308U;
inline constexpr uint32_t kPauseAlternateRendererUpdateCall = 0x00300538U;
inline constexpr uint32_t kPauseAlternateRendererNativeUpdate = 0x0042A278U;
inline constexpr uint32_t kPauseSceneSevenDrawCall = 0x00300520U;
inline constexpr uint32_t kPauseSceneSevenNativeDraw = 0x004228E4U;
inline constexpr uint32_t kPauseSceneFiveDrawCall = 0x0030052CU;
inline constexpr uint32_t kPauseSceneFiveNativeDraw = 0x0042CB54U;
inline constexpr uint32_t kPauseSceneViewportDrawCall = 0x00300550U;
inline constexpr uint32_t kPauseSceneViewportNativeDraw = 0x0042CBCCU;
inline constexpr uint32_t kPauseTouchCoordinateUpdateCall = 0x0041E984U;
inline constexpr uint32_t kPauseTouchCoordinateNativeUpdate = 0x004289DCU;
inline constexpr uint32_t kPauseRendererVisibilityCall = 0x00300544U;
inline constexpr uint32_t kPauseRendererVisibilityNativeDraw = 0x00427A3CU;
inline constexpr uint32_t kPauseControllerDrawCall = 0x0041988CU;
inline constexpr uint32_t kPauseControllerNativeDraw = 0x0041AFACU;
inline constexpr uint32_t kPauseControllerRendererDraw = 0x002F1280U;
inline constexpr uint32_t kTopScreenPauseState = 0x0050AF68U;
inline constexpr uint32_t kTopScreenPauseSceneControl = 0x005043E8U;
inline constexpr uint32_t kTopScreenDirectCallReturn = 0x60000000U;
inline constexpr uint32_t kAudioPlaySoundGeneral = 0x0037547CU;
inline constexpr uint32_t kTopScreenAimCycleSound = 0x0100048EU;
inline constexpr uint32_t kTopScreenAimCycleSoundPosition = 0x0054AC20U;
inline constexpr uint32_t kTopScreenAimCycleSoundScale = 0x0054AC24U;
inline constexpr uint32_t kTopScreenOcarinaEligibility = 0x002EFFF8U;
inline constexpr uint32_t kGlViewportEntry = 0x002FEABCU;
inline constexpr uint32_t kPauseIconBuild = 0x002E6CD4U;
inline constexpr uint32_t kPauseTouchButtonsDrawCall = 0x0042F5CCU;
inline constexpr uint32_t kPauseTouchButtonsTouchBranch = 0x0042EC2CU;
inline constexpr uint32_t kPauseTouchButtonsNoTouchExit = 0x0042F294U;
// IPS 0x0042E644: `bl PauseInput_GetHeldButtons` -> `mov r0, #0`.
inline constexpr uint32_t kPauseTouchButtonsHeldDownQuery = 0x0042E644U;
inline constexpr uint32_t kPauseTouchButtonsHeldDownTest = 0x0042E648U;
inline constexpr uint32_t kPauseLowerScreenBlock = 0x0041DDC8U;
inline constexpr uint32_t kPauseLowerScreenBlockEnd = 0x0041DEC8U;
inline constexpr uint32_t kPauseDungeonMapCursorInput = 0x00442F38U;
// PauseUi_Update `bne` past the pause scene dim; 2.1.1 and 3.0 make it a `b`.
inline constexpr uint32_t kPauseSceneDimBranch = 0x0041EB40U;
inline constexpr uint32_t kPauseSceneDimSkip = 0x0041EC10U;
inline constexpr uint32_t kPauseUiNativeUpdateCall = 0x0041EAE0U;
inline constexpr uint32_t kPauseProjectionPrepareCall = 0x0041EB28U;
inline constexpr uint32_t kPauseProjectionPrepare = 0x002FBC50U;
inline constexpr uint32_t kPauseUiNativeUpdate = 0x0042DDA8U;
inline constexpr uint32_t kTopScreenCameraNormal1Scalar = 0x0023A930U;
inline constexpr uint32_t kTopScreenCameraNormal1DefaultPath = 0x0023A94CU;
inline constexpr uint32_t kTopScreenCameraNormal1AlternatePath = 0x0023A974U;
inline constexpr uint32_t kTopScreenCameraFovScalar = 0x00478EB8U;
inline constexpr uint32_t kTopScreenCameraUpdateEntry = 0x002D84C4U;
inline constexpr uint32_t kTopScreenCameraUpdatePatchSite = 0x002D84C8U;
inline constexpr uint32_t kTopScreenGameplayCompositionPrefix = 0x002E278CU;
inline constexpr uint32_t kTopScreenGameplayCompositionBranch = 0x002E27C0U;
inline constexpr uint32_t kFileSelectUpdate = 0x0042F9A8U;
inline constexpr uint32_t kFileSelectInit = 0x0046B114U;
inline constexpr uint32_t kGameStateUpdate =
    Oot3dNativeGame::kOot3dGameStateUpdateEntry;
inline constexpr uint32_t kFileChooseInit = 0x00450B60U;
inline constexpr uint32_t kPlayTransitionUpdate = 0x002E2E60U;
inline constexpr uint32_t kFileSelectActivate = 0x002E7AE0U;
inline constexpr uint32_t kFileSelectState = 0x00504FA0U;
inline constexpr uint32_t kNativeGameMode = 0x00588E3CU;
inline constexpr uint32_t kBlockingQueuePop = 0x002CE040U;
inline constexpr uint32_t kBlockingQueuePush = 0x002D2EF4U;
inline constexpr uint32_t kRendererCommandQueueProcessPending = 0x002DBCBCU;
inline constexpr uint32_t kBlockingQueueTryPush = 0x00490D60U;
inline constexpr uint32_t kTaskQueueTakeFirstFromPriority = 0x002D33CCU;
inline constexpr uint32_t kTaskQueueEnqueuePriorityNode = 0x0030AB9CU;
inline constexpr uint32_t kTaskQueueWorkerDrainPriorityNodes = 0x004667B8U;
inline constexpr uint32_t kNngxP3dCallback = 0x00415AE4U;
inline constexpr uint32_t kNngxCommonInterruptHandler = 0x003025C0U;
inline constexpr uint32_t kNngxGlobalState = 0x005A6F30U;
inline constexpr uint32_t kNngxCmdlistCompletionDispatch = 0x003FD420U;
inline constexpr uint32_t kNngxCmdlistOwnerState = 0x0054CC34U;
inline constexpr uint32_t kRendererGlobalCallbackAfterLookup = 0x0016CB78U;
inline constexpr uint32_t kRendererCommandQueue = 0x005B93A8U;
inline constexpr uint32_t kInputGlobalState = 0x0056559CU;
inline constexpr uint32_t kInputHeldOffset = 0x04U;
inline constexpr uint32_t kInputPressedOffset = 0x08U;
inline constexpr uint32_t kInputReleasedOffset = 0x0CU;
inline constexpr uint32_t kInputRawCurrentOffset = 0xC4U;
inline constexpr uint32_t kInputRawPreviousOffset = 0xC8U;
inline constexpr size_t kMaximumA32BlockTraceRecords = 65'536U;

namespace CpuProbe = Oot3dNativeGame::CpuPhaseProbe;

struct NativeFramePhaseTiming {
  double GuestSeconds = 0.0;
  double GuestInsidePicaSubmitSeconds = 0.0;
  double GuestClockSeconds = 0.0;
  double FrameStartSeconds = 0.0;
  double HostFrameStartSeconds = 0.0;
  double InputPollSeconds = 0.0;
  double RendererFrameStartSeconds = 0.0;
  double DspMixSeconds = 0.0;
  double AudioOutputSeconds = 0.0;
  double PicaSubmitSeconds = 0.0;
  double PicaPlanSeconds = 0.0;
  double PicaBackendSeconds = 0.0;
  double PicaDiagnosticsSeconds = 0.0;
  double VisualPresentationSeconds = 0.0;
  double VisualReplaySeconds = 0.0;
  double PresentSeconds = 0.0;
  double PacingWaitSeconds = 0.0;
  std::array<double, 15> Values() const {
    return {GuestSeconds, FrameStartSeconds, HostFrameStartSeconds, InputPollSeconds,
      RendererFrameStartSeconds, DspMixSeconds, AudioOutputSeconds, PicaSubmitSeconds,
      PicaPlanSeconds, PicaBackendSeconds, PicaDiagnosticsSeconds,
      VisualPresentationSeconds, VisualReplaySeconds, PresentSeconds, PacingWaitSeconds};
  }
};

struct NativeAudioOutputDiagnostics {
  void Observe(uint32_t hostFrame, int32_t before, int32_t after,
               uint64_t requestedFrames, int32_t desiredBuffered) {
    const auto now = std::chrono::steady_clock::now();
    if (HasSubmitTime) {
      const double gapSeconds =
          std::chrono::duration<double>(now - LastSubmitTime).count();
      MaximumSubmitGapSeconds = std::max(MaximumSubmitGapSeconds, gapSeconds);
      if (gapSeconds >= 0.05 && SubmitGapEvents.size() < 64U) {
        SubmitGapEvents.push_back({
            {"host_frame", hostFrame},
            {"gap_seconds", gapSeconds},
            {"buffered_before", before},
        });
      }
    }
    LastSubmitTime = now;
    HasSubmitTime = true;
    ++SubmitCalls;
    RequestedFrames += requestedFrames;
    if (!HasBufferObservation) {
      MinimumBufferedBefore = before;
      MaximumBufferedBefore = before;
      MinimumBufferedAfter = after;
      MaximumBufferedAfter = after;
      HasBufferObservation = true;
    } else {
      MinimumBufferedBefore = std::min(MinimumBufferedBefore, before);
      MaximumBufferedBefore = std::max(MaximumBufferedBefore, before);
      MinimumBufferedAfter = std::min(MinimumBufferedAfter, after);
      MaximumBufferedAfter = std::max(MaximumBufferedAfter, after);
    }
    const bool wasPrimed = Primed;
    if (after >= desiredBuffered) {
      Primed = true;
    }
    if (wasPrimed && before == 0) {
      ++StarvationObservations;
      StarvationEvents.push_back({
          {"host_frame", hostFrame},
          {"buffered_before", before},
          {"buffered_after", after},
          {"requested_frames", requestedFrames},
      });
    }
    const uint64_t growth =
        after > before ? static_cast<uint64_t>(after - before) : 0U;
    if (growth < requestedFrames) {
      EstimatedShortfallFrames += requestedFrames - growth;
      ++ShortGrowthSubmissions;
    }
  }

  bool HasBufferObservation = false;
  bool HasSubmitTime = false;
  bool Primed = false;
  std::chrono::steady_clock::time_point LastSubmitTime{};
  double MaximumSubmitGapSeconds = 0.0;
  uint64_t SubmitCalls = 0;
  uint64_t RequestedFrames = 0;
  uint64_t ShortGrowthSubmissions = 0;
  uint64_t EstimatedShortfallFrames = 0;
  uint64_t StarvationObservations = 0;
  int32_t MinimumBufferedBefore = 0;
  int32_t MaximumBufferedBefore = 0;
  int32_t MinimumBufferedAfter = 0;
  int32_t MaximumBufferedAfter = 0;
  nlohmann::json StarvationEvents = nlohmann::json::array();
  nlohmann::json SubmitGapEvents = nlohmann::json::array();
};

struct NativeInputConsumerDiagnostics {
  uint64_t Samples = 0;
  uint64_t ReadFailures = 0;
  uint64_t HeldFrames = 0;
  uint64_t PressedFrames = 0;
  uint64_t ReleasedFrames = 0;
  uint32_t LastHeld = 0;
  uint32_t LastPressed = 0;
  uint32_t LastReleased = 0;
  uint32_t LastRawCurrent = 0;
  uint32_t LastRawPrevious = 0;
  bool HasPrevious = false;
  nlohmann::json Transitions = nlohmann::json::array();

  void Observe(Oot3dNativeGame::NativeA32Memory &memory, uint32_t hostFrame) {
    uint32_t held = 0;
    uint32_t pressed = 0;
    uint32_t released = 0;
    uint32_t rawCurrent = 0;
    uint32_t rawPrevious = 0;
    if (!memory.Read32(kInputGlobalState + kInputHeldOffset, &held) ||
        !memory.Read32(kInputGlobalState + kInputPressedOffset, &pressed) ||
        !memory.Read32(kInputGlobalState + kInputReleasedOffset, &released) ||
        !memory.Read32(kInputGlobalState + kInputRawCurrentOffset,
                       &rawCurrent) ||
        !memory.Read32(kInputGlobalState + kInputRawPreviousOffset,
                       &rawPrevious)) {
      ++ReadFailures;
      return;
    }
    ++Samples;
    HeldFrames += held != 0U ? 1U : 0U;
    PressedFrames += pressed != 0U ? 1U : 0U;
    ReleasedFrames += released != 0U ? 1U : 0U;
    const bool changed = !HasPrevious || held != LastHeld ||
                         pressed != LastPressed || released != LastReleased ||
                         rawCurrent != LastRawCurrent ||
                         rawPrevious != LastRawPrevious;
    if (changed && Transitions.size() < 64U) {
      Transitions.push_back({
          {"host_frame", hostFrame},
          {"held", held},
          {"pressed", pressed},
          {"released", released},
          {"raw_current", rawCurrent},
          {"raw_previous", rawPrevious},
      });
    }
    LastHeld = held;
    LastPressed = pressed;
    LastReleased = released;
    LastRawCurrent = rawCurrent;
    LastRawPrevious = rawPrevious;
    HasPrevious = true;
  }
};

struct NativeSceneProjectionState {
  struct CallsiteStats {
    uint64_t Calls = 0;
    float FirstLeft = 0.0F;
    float FirstRight = 0.0F;
    // The latest perspective frustum this callsite received, before the
    // presentation policy widens it (left, right, bottom, top, near, far).
    std::array<float, 6> LastFrustum{};
  };

  Fast::Oot3d::ScenePresentationPolicy Presentation{};
  float FovMultiplier = 1.0F;
  // The title's perspective frustum builder (s0..s5 = left, right, bottom,
  // top, near, far), hooked at entry; OoT3D's by default.
  uint32_t PerspectiveFrustumEntry = kMtx4x4BuildFrustum;
  // Whether each frustum it builds is published as the scene view. A title
  // whose frusta also serve other views holds the latest one until its
  // composition tracker reaches the opaque world pass, and publishes that.
  bool FrustumIsSceneView = true;
  struct PendingFrustum {
    uint32_t Pc = 0, ReturnAddress = 0;
    float Left = 0, Right = 0, Bottom = 0, Top = 0, NearPlane = 0, FarPlane = 0;
  };
  std::optional<PendingFrustum> PendingSceneFrustum;
  uint64_t FrustumCalls = 0;
  uint64_t OrthographicCalls = 0;
  std::map<std::pair<uint32_t, uint32_t>, CallsiteStats> Callsites;
};

struct NativeWidescreenProjectionState {
  struct A32BlockTraceRecord {
    uint32_t HostFrame = 0;
    uint32_t Pc = 0;
    uint64_t MemoryWriteGeneration = 0;
    uint64_t MemoryWriteFingerprint = 0;
    uint64_t FastReadMismatchCount = 0;
    uint32_t LastFastReadMismatchAddress = 0;
    uint64_t LastFastReadMismatchValue = 0;
    uint64_t LastCheckedReadMismatchValue = 0;
    std::array<uint32_t, 16> Registers{};
    uint32_t Cpsr = 0;
    uint32_t Fpscr = 0;
    uint32_t ThreadPointer = 0;
    std::array<uint32_t, 32> Vfp{};
    uint32_t ExclusiveAddress = 0;
    uint64_t ExclusiveToken = 0;
    uint8_t ExclusiveSize = 0;
    bool ExclusiveValid = false;
  };

  NativeSceneProjectionState Projection;
  Oot3dNativeGame::NativeActorVisibilityState ActorVisibility;
  bool CollectExtendedDiagnostics = false;
  uint32_t CurrentHostFrame = 0;
  uint32_t EnMagInitCalls = 0;
  uint32_t EnMagUpdateCalls = 0;
  uint32_t EnMagDrawCalls = 0;
  nlohmann::json EnMagEvents = nlohmann::json::array();
  std::map<std::string, uint64_t> UiLifecycleCallCounts;
  nlohmann::json UiLifecycleEvents = nlohmann::json::array();
  nlohmann::json GameStateEvents = nlohmann::json::array();
  uint64_t GameStateUpdateCalls = 0;
  uint32_t LastGameStateAddress = 0;
  uint32_t LastGameStateMain = 0;
  uint32_t LastNextGameStateInit = 0;
  uint32_t LastNativeGameMode = 0;
  uint8_t LastGameStateRunning = 0;
  uint8_t LastTransitionTrigger = 0;
  uint8_t LastTransitionType = 0;
  uint8_t LastTransitionState = 0;
  bool HasGameStateSnapshot = false;
  nlohmann::json RendererQueueEvents = nlohmann::json::array();
  std::map<std::string, uint64_t> RendererQueueCallCounts;
  bool ProfileA32Blocks = false;
  uint64_t BlockEntries = 0;
  uint32_t BlockSampleState = 0x6D2B79F5U;
  std::unordered_map<uint32_t, uint64_t> BlockSamples;
  bool TraceA32Blocks = false;
  bool BlockTraceTruncated = false;
  // Adapter bring-up: TRIAEVUM_A32_CALL_TRACE=PATH,FIRST_FRAME,LAST_FRAME,WORD
  // (hex WORD: a guest word sampled at each record, such as the title's
  // command-list cursor) writes one record per observed block entry in that
  // host-frame range: frame, pc, lr, sp, r0..r3, thread pointer, the word
  // (ten little-endian u32). Needs --trace-a32-blocks, which observes every
  // block entry.
  std::FILE *CallTrace = nullptr;
  uint32_t CallTraceFirstFrame = 0;
  uint32_t CallTraceLastFrame = 0;
  uint32_t CallTraceWordAddress = 0;
  // Adapter bring-up: TRIAEVUM_A32_PC_DUMP=PC,FRAME,DIR,ADDR:SIZE[;ADDR:SIZE...]
  // (hex PC/ADDR/SIZE) writes, once, the guest registers and the listed
  // guest ranges at the first entry of block PC from host frame FRAME on:
  // DIR/registers.txt and DIR/<ADDR>.bin. Memory mid-frame, at a native draw
  // boundary, which a savestate at a frame boundary cannot show.
  uint32_t PcDumpAddress = 0;
  uint32_t PcDumpFrame = 0;
  std::string PcDumpDirectory;
  std::vector<std::pair<uint32_t, uint32_t>> PcDumpRanges;
  bool PcDumpDone = true;
  uint64_t BlockTraceRecordsObserved = 0;
#if defined(TRIAEVUM_DEVELOPER_TOOLS)
  // TRIAEVUM_A32_PROBE=SPEC.json,OUT.jsonl: read probes at listed code
  // addresses (native_a32_read_probe.h), for adapter bring-up.
  Oot3dNativeGame::A32ReadProbeSet *ReadProbes = nullptr;
  std::FILE *ReadProbeOut = nullptr;
#endif
  Oot3dNativeGame::NativeA32Memory *TraceMemory = nullptr;
  Oot3dNativeGame::NativeHdFontRuntime *HdFonts = nullptr;
  std::deque<A32BlockTraceRecord> BlockTrace;
  Oot3dNativeGame::Oot3dNativeUiLifecycleBridge *UiLifecycleBridge = nullptr;
  Oot3dNativeGame::TopScreenOcarinaTextRuntime *OcarinaText = nullptr;
  Oot3dNativeGame::TopScreenPauseTransactionRuntime *PauseTransaction = nullptr;
  Oot3dNativeGame::TopScreenSystemPageRuntime *SystemPage = nullptr;
  Oot3dNativeGame::TopScreenItemDescriptionRuntime *ItemDescription = nullptr;
  const Oot3dNativeGame::TopScreenWideItemsRuntime *WideItems = nullptr;
  Oot3dNativeGame::Oot3dPicaCompositionDomain *PicaCompositionDomain =
      nullptr;
  Oot3dNativeGame::Oot3dNativePicaCompositionTracker
      *PicaCompositionTracker = nullptr;
  Oot3dNativeGame::Mm3dNativePicaCompositionTracker
      *Mm3dCompositionTracker = nullptr;
  const Oot3dNativeGame::Mm3dGuestPatches *Mm3dGuestPatches = nullptr;
  Oot3dNativeGame::TopScreenTextureOverrideRuntime *TopScreenTextureOverrides =
      nullptr;
  bool TopScreenUiProfile = false;
  Oot3dNativeGame::TopScreenUiConfig TopScreenConfig;
  Oot3dNativeGame::TopScreenExtendedInputFrame *TopScreenInput = nullptr;
  Oot3dNativeGame::TopScreenInputCadence *TopScreenInputClock = nullptr;
  const Oot3dNativeGame::TopScreenItemDispatchRuntime *TopScreenItems = nullptr;
  const Oot3dNativeGame::TopScreenGameplayRuntime *TopScreenGameplay = nullptr;
  // Extended draw distance object slots: their hooks leave compiled code
  // only while the extension is active (ObjectBankExtension::ShouldObserve).
  const Oot3dNativeGame::ObjectBankExtension *ObjectBank = nullptr;
  // SingleScreen 3.0 rain: observed only while its option or a weather draw
  // is active (TopScreenRainAdapter::ShouldObserve).
  const Oot3dNativeGame::TopScreenRainAdapter *Rain = nullptr;
  std::span<const uint32_t> WholeAotObservableExitPcs;
  // Set to the pc of each observable exit this policy raises (see
  // NativeCandidateDispatchState::ObservedExitPc).
  std::optional<uint32_t> *ObservedExitPc = nullptr;
  std::optional<Oot3dNativeGame::TopScreenPauseTargetPlan>
      PendingTopScreenPauseTarget;
  uint32_t PendingTopScreenPauseRenderer = 0U;
  uint64_t TopScreenPauseTargetObservations = 0U;
  uint64_t TopScreenPauseTargetCommands = 0U;
  uint64_t TopScreenPauseTargetRewrites = 0U;
  uint64_t TopScreenPauseChildSuppressions = 0U;
  uint64_t TopScreenPauseChildNativeDraws = 0U;
  bool HasTopScreenPauseDrawInputs = false;
  bool LastTopScreenPauseRouteActive = false;
  bool LastTopScreenPauseChildrenSuppressed = false;
  Oot3dNativeGame::TopScreenPauseDrawInputs LastTopScreenPauseDrawInputs{};
  Oot3dNativeGame::TopScreenPausePageRedrawState TopScreenPausePageRedraw;
  Oot3dNativeGame::TopScreenTouchCoordinateRouteState
      *TopScreenTouchCoordinateRoute = nullptr;
  bool TopScreenPausePageRedrawActive = false;
  uint64_t TopScreenPausePageCompositionCalls = 0U;
  uint64_t TopScreenPausePageRedraws = 0U;
  uint64_t TopScreenPausePageRedrawSkips = 0U;
  uint64_t TopScreenPauseProjectionCalls = 0U;
  uint64_t TopScreenPauseProjectionFailures = 0U;
  nlohmann::json TopScreenPauseProjectionEvents = nlohmann::json::array();
  Oot3dNativeGame::TopScreenPauseRouteState *TopScreenPauseTargetRoute =
      nullptr;
  Oot3dNativeGame::TopScreenPauseDrawRoutingState TopScreenPauseDrawRouting;
  std::optional<Oot3dNativeGame::TopScreenPauseChildState>
      PendingTopScreenPauseChildRestore;
  Oot3dNativeGame::NativeFrameRatePolicy *FrameRatePolicy = nullptr;
  Oot3dNativeGame::NativeA32SceneViewProbe *SceneViewProbe = nullptr;
  // The developer free camera owns Camera_Update while it is active.
  const Oot3dNativeGame::DevFreeCamera *DevFreeCamera = nullptr;
  Oot3dNativeGame::NativeA32PlayerTimingProbe *PlayerTimingProbe = nullptr;
  Oot3dNativeGame::NativeTemporalEventLedger *TemporalEventLedger = nullptr;
  Oot3dNativeGame::NativeA32PlayerTemporalBridge *PlayerTemporalBridge =
      nullptr;
  nlohmann::json PlayerTimingEvents = nlohmann::json::array();
  bool PlayerTimingEventsTruncated = false;
};

struct NativeCandidateDispatchState {
  oot3d::recomp::a32::BlockEntryCallback BlockEntry = nullptr;
  void *BlockEntryUser = nullptr;
  std::span<const uint32_t> BlockEntryPcs;
  Oot3dNativeGame::Oot3dMassAotPcFilter MassAotBlockEntryFilter;
  Oot3dNativeGame::Oot3dMassAotPcFilter MassAotObservableExitFilter;
  Oot3dNativeGame::NativeA32Memory *Memory = nullptr;
  Oot3dNativeGame::Oot3dTypedGameplayContext TypedGameplayContext;
  bool TypedGameplay = false;
  bool MassAot = false;
  bool CompiledFunctions = false;
  bool WholeAot = false;
  bool ManualCompiledFunctions = true;
  bool TrueAotBlocks = false;
  bool ProfileRuntime = false;
  bool TopScreenUiProfile = false;
  bool TopScreenPauseAotBarrier = false;
  bool TopScreenExtendedCameraActive = false;
  Oot3dNativeGame::TopScreenFreeCameraGuestRuntime TopScreenCamera;
  // Developer free camera (inactive unless F1 > Dev Tools turns it on).
  std::shared_ptr<Oot3dNativeGame::DevFreeCamera> DevFreeCamera =
      std::make_shared<Oot3dNativeGame::DevFreeCamera>();
  bool DevFreeCameraReported = false;
  Oot3dNativeGame::TopScreenCameraFovOwnership TopScreenCameraFov;
  Oot3dNativeGame::NativeFreeCameraInputAccumulator TopScreenCameraInput;
  Oot3dNativeGame::TopScreenExtendedInputFrame TopScreenInput;
  Oot3dNativeGame::TopScreenInputCadence TopScreenInputClock;
  // The pc whose block policy raised the last observable exit: the product
  // dispatcher resumes it without running that policy a second time.
  std::optional<uint32_t> ObservedExitPc;
  // The projection policy's page redraw to the upper screen.
  const bool *TopScreenPausePageRedrawActive = nullptr;
  uint32_t PreviousTopScreenButtons = 0U;
  Oot3dNativeGame::NativeA32PolledButtonLatch StartButtonLatch;
  Oot3dNativeGame::TopScreenStartRoutingState TopScreenStartRouting;
  Oot3dNativeGame::TopScreenVersionChoiceRevealState TopScreenVersionChoiceReveal;
  Oot3dNativeGame::TopScreenPauseSystemOpenState TopScreenPauseSystemOpen;
  Oot3dNativeGame::TopScreenPauseTransactionRuntime *PauseTransaction = nullptr;
  Oot3dNativeGame::TopScreenSystemPageRuntime *SystemPage = nullptr;
  Oot3dNativeGame::TopScreenItemDescriptionRuntime *ItemDescription = nullptr;
  Oot3dNativeGame::TopScreenWideItemsRuntime *WideItems = nullptr;
  Oot3dNativeGame::TopScreenGameplayRuntime *Gameplay = nullptr;
  // Extended draw distance object slots, in every UI profile, ahead of the
  // TopScreen source port.
  Oot3dNativeGame::ObjectBankExtension *ObjectBank = nullptr;
  Oot3dNativeGame::TopScreenRainAdapter *Rain = nullptr;
  // Sorted TopScreen entries the product hook owns (product builds only).
  std::vector<uint32_t> ProductTopScreenBlocks;
  uint64_t TopScreenAimProjectileCycleAttempts = 0U;
  uint64_t TopScreenAimProjectileCycleUpdates = 0U;
  uint64_t TopScreenAimProjectileCycleSoundCalls = 0U;
  uint64_t TopScreenGameplayDpadAttempts = 0U;
  uint64_t TopScreenNaviViewActivations = 0U;
  uint64_t TopScreenOcarinaQueries = 0U;
  uint64_t TopScreenOcarinaActivations = 0U;
  Oot3dNativeGame::TopScreenGameplayActionConsumerStats
      TopScreenGameplayActions;
  Oot3dNativeGame::TopScreenGameplayActionConsumerRuntime
      TopScreenGameplayActionRuntime;
  uint8_t TopScreenViewportDrawPhase = 0U;
  uint32_t TopScreenViewportDrawArgument = 0U;
  uint8_t TopScreenOverlayViewportDrawPhase = 0U;
  uint8_t TopScreenTouchCoordinateUpdatePhase = 0U;
  std::optional<Oot3dNativeGame::TopScreenAlternateRendererState>
      TopScreenAlternateRendererRestore;
  Oot3dNativeGame::TopScreenPauseRouteState TopScreenPauseRoute;
  Oot3dNativeGame::TopScreenPauseRouteState TopScreenAotPauseRoute;
  Oot3dNativeGame::TopScreenTouchCoordinateRouteState
      TopScreenTouchCoordinateRoute;
  Oot3dNativeGame::TopScreenRendererVisibilityRouteState
      TopScreenRendererVisibilityRoute;
  uint32_t TopScreenGameOverDrawPhase = 0U;
  std::array<uint32_t, 5> TopScreenGameOverDrawRegisters{};
  Oot3dNativeGame::TopScreenPauseChildState TopScreenGameOverChildRestore{};
  Oot3dNativeGame::TopScreenPauseControllerState TopScreenPauseController;
  uint8_t TopScreenPauseControllerDrawPhase = 0U;
  std::array<uint32_t, 2> TopScreenPauseControllerVisibleRenderers{};
  Oot3dNativeGame::TopScreenItemDispatchRuntime TopScreenItems;
  uint32_t SampleState = 0xA32C0DE1U;
  uint64_t Calls = 0;
  uint64_t Samples = 0;
  uint64_t TypedGameplaySamples = 0;
  uint64_t TypedGameplaySampleNanoseconds = 0;
  uint64_t CompiledSamples = 0;
  uint64_t CompiledSampleNanoseconds = 0;
  uint64_t TrueAotSamples = 0;
  uint64_t TrueAotSampleNanoseconds = 0;
  uint64_t UnhandledSamples = 0;
  uint64_t UnhandledSampleNanoseconds = 0;
  uint64_t TopScreenSourcePortCalls = 0;
  uint64_t TopScreenTitleLogoFadeHoldCalls = 0;
  uint64_t TopScreenLowerCompositionSkips = 0;
  uint64_t TopScreenCameraNormal1Calls = 0;
  uint64_t TopScreenCameraUpdateCalls = 0;
  uint64_t TopScreenCameraActiveCalls = 0;
  Oot3dNativeGame::Oot3dNativeUiLifecycleBridge *UiLifecycleBridge = nullptr;
  Oot3dNativeGame::TopScreenOcarinaTextRuntime *OcarinaText = nullptr;
  Oot3dNativeGame::Oot3dPicaCompositionDomain *PicaCompositionDomain =
      nullptr;
  Oot3dNativeGame::TopScreenPauseProjectionState *TopScreenPauseProjection =
      nullptr;
  const Oot3dNativeGame::TopScreenUiConfig *TopScreenConfig = nullptr;
};

struct NativeControlPollingState {
  int32_t ControllerInstance = -1;
  std::array<bool, Oot3dNativeGame::kNativeControlActionCount>
      PhysicalActions{};
  std::array<bool, Oot3dNativeGame::kNativeControlActionCount>
      PendingPressedActions{};
  int64_t PendingMouseDeltaX = 0;
  int64_t PendingMouseDeltaY = 0;
  double PendingInputSeconds = 0.0;
  bool GameplayMouseOwned = false;
  uint64_t MouseEligiblePolls = 0;
  uint64_t MouseReleasedPolls = 0;
  uint64_t MouseHostUiPolls = 0;
  uint64_t MouseNativeUiPolls = 0;
  uint64_t MouseCaptureTransitions = 0;
  uint64_t MouseMovementPolls = 0;
  Oot3dNativeGame::NativeRightStickProfileState RightStickProfile;
  ThreeDsRecomp::Input::VirtualMotionState VirtualMotion;
  uint32_t PreviousControllerButtons = 0;
  uint32_t PendingControllerButtons = 0;
  // The developer free camera's own physical sources, apart from the game's
  // bindings (the right mouse button is the game's R): mouse motion since it
  // last read it, the right mouse button, and the pad's sticks (-1..1, dead
  // zone applied; Y up) and shoulders.
  int64_t DevMouseDeltaX = 0;
  int64_t DevMouseDeltaY = 0;
  bool DevMouseRightHeld = false;
  std::array<float, 2> DevPadLeftStick{};
  std::array<float, 2> DevPadRightStick{};
  bool DevPadLeftShoulder = false;
  bool DevPadRightShoulder = false;
};

bool ExecuteProductTopScreenHook(uint32_t pc, oot3d::recomp::a32::GuestState &state,
                                   oot3d::recomp::a32::MemoryBus &,
                                   oot3d::recomp::a32::ExecutionResult *result,
                                   uint32_t blockBudget, uint32_t *blocksConsumed, void *user);

bool ExecuteNativeCandidate(uint32_t pc, oot3d::recomp::a32::GuestState &state,
                            oot3d::recomp::a32::MemoryBus &memory,
                            oot3d::recomp::a32::ExecutionResult *result,
                            uint32_t blockBudget, uint32_t *blocksConsumed,
                            void *user);

void ApplyNativeWidescreenProjectionPolicy(
    uint32_t pc, oot3d::recomp::a32::GuestState &state,
    oot3d::recomp::a32::MemoryBus &memory, void *user);

Oot3dNativeGame::NativeA32InputFrame
PollNativeA32Input(Fast::Fast3dWindow &window,
                   bool nativeFrontendTouchEnabled,
                   bool hostGuiVisible,
                   bool topScreenUiProfile,
                   bool topScreenFreeCameraEnabled,
                   Oot3dNativeGame::NativeControlConfigRuntime &controls,
                   const Oot3dNativeGame::NativeAimProfileTransform
                       &aimTransform,
                   double samplePeriodSeconds,
                   bool guestRefreshWillConsume,
                   NativeControlPollingState &pollingState);

Oot3dNativeGame::NativeA32InputFrame ResolveNativeA32GuestInput(
    const Oot3dNativeGame::NativeA32InputFrame &physical,
    const Oot3dNativeGame::NativeA32InputTimeline &timeline,
    uint32_t guestFrameIndex, uint32_t runFrameIndex,
    bool nativeFrontendTouchEnabled,
    bool topScreenStartRoutingEligible, bool topScreenUiProfile,
    Oot3dNativeGame::NativeA32PolledButtonLatch &startButtonLatch,
    Oot3dNativeGame::TopScreenStartRoutingState &startRoutingState);

bool ResolveNativeA32TouchInputEnabled(
    Oot3dNativeGame::NativeA32Memory &memory,
    const Oot3dNativeGame::Oot3dNativeUiLifecycleBridge &uiLifecycleBridge,
    bool topScreenUiProfile);

} // namespace Oot3dA32Window
