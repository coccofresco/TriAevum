#include "oot3d_native_a32_window.h"
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
#include "oot3d_native_pica_presentation_scheduler.h"
#include "oot3d_native_pica_semantic_trace.h"
#include "oot3d_native_pica_submission.h"
#include "oot3d_native_pica_visual_frame.h"
#include "oot3d_native_pica_visual_savestate.h"
#include "oot3d_native_pica_vulkan_bridge.h"
#include "oot3d_native_pica_vulkan_plan.h"
#include "oot3d_native_player_temporal_bridge.h"
#include "oot3d_native_scenario_bootstrap.h"
#include "oot3d_source_actor_init_context_runtime.h"
#include "oot3d_source_actor_update_all_runtime.h"
#include "oot3d_source_camera_update_runtime.h"
#include "oot3d_source_csab_curve_runtime.h"
#include "oot3d_source_player_update_runtime.h"
#include "oot3d_source_player_update_common_runtime.h"
#include "oot3d_source_cutscene_process_commands_runtime.h"
#include "oot3d_source_cutscene_update_frame_runtime.h"
#include "oot3d_source_gameplay_profile.h"
#include "oot3d_native_temporal_event_ledger.h"
#include "oot3d_native_true_aot_blocks.h"
#include "oot3d_native_ui_lifecycle_bridge.h"
#include "oot3d_native_ui_texture_provider.h"
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
#include "oot3d_top_screen_items_hint.h"
#include "oot3d_top_screen_settings_panel.h"
#include "oot3d_top_screen_texture_overrides.h"
#include "oot3d_top_screen_texture_runtime.h"
#include "oot3d_typed_gameplay_bridge.h"

#include "fast/Fast3dWindow.h"
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
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace {

#if defined(__SWITCH__)
size_t gSwitchBlockHookCount = 0U;
bool gSwitchBlockHooksSorted = false;
bool gSwitchBlockHooksContainProcessEntry = false;
uint64_t gSwitchRuntimeHostFrames = 0U;
uint32_t gSwitchRuntimeGuestFrames = 0U;
std::string gSwitchUiProfile = "unknown";
std::string gSwitchGameplayTiming = "unknown";
std::string gSwitchControlProfile = "unknown";
uint32_t gSwitchPresentationRateHz = 0U;
bool gSwitchVisualInterpolation = false;
bool gSwitchTopScreen211Assets = false;
uint32_t gSwitchWholeAotBlockBudget = 0U;
uint64_t gSwitchWholeAotBlockLimitExits = 0U;
bool gSwitchPicaGeometryCacheEnabled = false;
uint64_t gSwitchPicaGeometryPersistentDraws = 0U;
uint64_t gSwitchPicaGeometryPersistentUploads = 0U;
uint64_t gSwitchPicaGeometryRegistryEntries = 0U;
bool gSwitchPicaGeometryCacheObserved = false;

void WriteSwitchBootStage(const char* stage) {
  std::ofstream stream("sdmc:/switch/oot3dre/boot-status.txt",
                       std::ios::trunc);
  if (!stream) {
    return;
  }
  stream << "status=launching\n"
         << "stage=" << stage << '\n'
         << "whole_aot_functions=12419\n"
         << "host_boundaries=3\n"
         << "residual_a32=0\n"
         << "block_hook_count=" << gSwitchBlockHookCount << '\n'
         << "block_hooks_sorted=" << (gSwitchBlockHooksSorted ? 1 : 0)
         << '\n'
         << "block_hooks_contain_process_entry="
         << (gSwitchBlockHooksContainProcessEntry ? 1 : 0) << '\n'
         << "host_frames=" << gSwitchRuntimeHostFrames << '\n'
         << "guest_frames=" << gSwitchRuntimeGuestFrames << '\n'
         << "ui_profile=" << gSwitchUiProfile << '\n'
         << "gameplay_timing=" << gSwitchGameplayTiming << '\n'
         << "presentation_rate_hz=" << gSwitchPresentationRateHz << '\n'
         << "visual_interpolation=" << (gSwitchVisualInterpolation ? 1 : 0)
         << '\n'
         << "control_profile=" << gSwitchControlProfile << '\n'
         << "topscreen_211_assets=" << (gSwitchTopScreen211Assets ? 1 : 0)
         << '\n'
         << "whole_aot_block_budget=" << gSwitchWholeAotBlockBudget << '\n'
         << "whole_aot_block_limit_exits="
         << gSwitchWholeAotBlockLimitExits << '\n'
         << "pica_geometry_cache_enabled="
         << (gSwitchPicaGeometryCacheEnabled ? 1 : 0) << '\n'
         << "pica_geometry_persistent_draws="
         << gSwitchPicaGeometryPersistentDraws << '\n'
         << "pica_geometry_persistent_uploads="
         << gSwitchPicaGeometryPersistentUploads << '\n'
         << "pica_geometry_registry_entries="
         << gSwitchPicaGeometryRegistryEntries << '\n';
}

#endif

constexpr uint64_t kCtrArm11TicksPerSecond = 268111856ULL;
constexpr uint64_t kGuestDisplayRefreshRate =
    Oot3dNativeGame::kOot3dNativeTimeUnitsPerSecond;
constexpr uint32_t kMinimumHostAudioPrebufferMilliseconds = 100U;
constexpr uint64_t kA32RuntimeSampleDenominator = 64ULL;
constexpr uint64_t kVisualInterpolationRenderTargetNamespace = 1ULL;
constexpr size_t kMaximumPicaDrainPassesPerRefresh = 64U;
constexpr uint32_t kMtx4x4BuildFrustum = 0x002FDE9CU;
constexpr uint32_t kMtx4x4BuildOrthographicProjectionRotated = 0x00300AA8U;
constexpr uint32_t kEnMagInit = 0x0018CBB8U;
constexpr uint32_t kEnMagDraw = 0x001DA4F4U;
constexpr uint32_t kEnMagUpdate = 0x001DA9F8U;
constexpr uint32_t kTopScreenTitleLogoFadeHoldPrefix = 0x001DADCCU;
constexpr uint32_t kPauseUiUpdate = 0x0041E968U;
constexpr uint32_t kPauseUiDraw = 0x0041EC50U;
constexpr uint32_t kPauseUiDrawReturn = 0x00419830U;
constexpr uint32_t kPauseAlternateViewportDrawCall = 0x00419850U;
constexpr uint32_t kPauseAlternateViewportNativeDraw = 0x0041D1A8U;
constexpr uint32_t kPauseConditionalDrawCall = 0x00419874U;
constexpr uint32_t kPauseConditionalNativeDraw = 0x0041C500U;
constexpr uint32_t kPauseRoutedCommandCallA = 0x0041952CU;
constexpr uint32_t kPauseRoutedCommandCallB = 0x004198B0U;
constexpr uint32_t kPauseRoutedCommandNative = 0x00300240U;
constexpr uint32_t kPauseOverlayViewportDrawCall = 0x00419838U;
constexpr uint32_t kPauseOverlayNativeDraw = 0x0041F308U;
constexpr uint32_t kPauseAlternateRendererUpdateCall = 0x00300538U;
constexpr uint32_t kPauseAlternateRendererNativeUpdate = 0x0042A278U;
constexpr uint32_t kPauseSceneSevenDrawCall = 0x00300520U;
constexpr uint32_t kPauseSceneSevenNativeDraw = 0x004228E4U;
constexpr uint32_t kPauseSceneViewportDrawCall = 0x00300550U;
constexpr uint32_t kPauseSceneViewportNativeDraw = 0x0042CBCCU;
constexpr uint32_t kPauseTouchCoordinateUpdateCall = 0x0041E984U;
constexpr uint32_t kPauseTouchCoordinateNativeUpdate = 0x004289DCU;
constexpr uint32_t kPauseRendererVisibilityCall = 0x00300544U;
constexpr uint32_t kPauseRendererVisibilityNativeDraw = 0x00427A3CU;
constexpr uint32_t kPauseControllerDrawCall = 0x0041988CU;
constexpr uint32_t kPauseControllerNativeDraw = 0x0041AFACU;
constexpr uint32_t kPauseControllerRendererDraw = 0x002F1280U;
constexpr uint32_t kPauseInputSetTouchEnabled = 0x002FD84CU;
constexpr uint32_t kPauseSystemMenuOpen = 0x0043ACA8U;
constexpr uint32_t kTopScreenPauseState = 0x0050AF68U;
constexpr uint32_t kTopScreenPauseSceneControl = 0x005043E8U;
constexpr uint32_t kTopScreenDirectCallReturn = 0x60000000U;
constexpr uint32_t kAudioPlaySoundGeneral = 0x0037547CU;
constexpr uint32_t kTopScreenAimCycleSound = 0x0100048EU;
constexpr uint32_t kTopScreenAimCycleSoundPosition = 0x0054AC20U;
constexpr uint32_t kTopScreenAimCycleSoundScale = 0x0054AC24U;
constexpr uint32_t kTopScreenOcarinaEligibility = 0x002EFFF8U;
constexpr uint32_t kGlViewportEntry = 0x002FEABCU;
constexpr uint32_t kPauseIconBuild = 0x002E6CD4U;
constexpr uint32_t kPauseTouchButtonsDrawCall = 0x0042F5CCU;
constexpr uint32_t kPauseTouchButtonsTouchBranch = 0x0042EC2CU;
constexpr uint32_t kPauseTouchButtonsNoTouchExit = 0x0042F294U;
constexpr uint32_t kPauseLowerScreenBlock = 0x0041DDC8U;
constexpr uint32_t kPauseLowerScreenBlockEnd = 0x0041DEC8U;
constexpr uint32_t kPauseDungeonMapCursorInput = 0x00442F38U;
constexpr uint32_t kPauseUiNativeUpdateCall = 0x0041EAE0U;
constexpr uint32_t kPauseProjectionPrepareCall = 0x0041EB28U;
constexpr uint32_t kPauseProjectionPrepare = 0x002FBC50U;
constexpr uint32_t kGlobalActionStateGetSlotItemId = 0x002C3970U;
constexpr uint32_t kPauseItemsNativeUpdateCall = 0x00433B68U;
constexpr uint32_t kPauseItemsNativeUpdateReturn = 0x00433B6CU;
constexpr uint32_t kPauseItemsNativeUpdate = 0x002EC3E4U;
constexpr uint32_t kPauseUiNativeUpdate = 0x0042DDA8U;
constexpr uint32_t kTopScreenCameraNormal1Scalar = 0x0023A930U;
constexpr uint32_t kTopScreenCameraNormal1DefaultPath = 0x0023A94CU;
constexpr uint32_t kTopScreenCameraNormal1AlternatePath = 0x0023A974U;
constexpr uint32_t kTopScreenCameraFovScalar = 0x00478EB8U;
constexpr uint32_t kTopScreenCameraUpdateEntry = 0x002D84C4U;
constexpr uint32_t kTopScreenCameraUpdatePatchSite = 0x002D84C8U;
constexpr uint32_t kTopScreenGameplayCompositionPrefix = 0x002E278CU;
constexpr uint32_t kTopScreenGameplayCompositionBranch = 0x002E27C0U;
constexpr uint32_t kFileSelectUpdate = 0x0042F9A8U;
constexpr uint32_t kFileSelectInit = 0x0046B114U;
constexpr uint32_t kGameStateUpdate =
    Oot3dNativeGame::kOot3dGameStateUpdateEntry;
constexpr uint32_t kFileChooseInit = 0x00450B60U;
constexpr uint32_t kPlayTransitionUpdate = 0x002E2E60U;
constexpr uint32_t kFileSelectActivate = 0x002E7AE0U;
constexpr uint32_t kFileSelectState = 0x00504FA0U;
constexpr uint32_t kNativeGameMode = 0x00588E3CU;
constexpr uint32_t kBlockingQueuePop = 0x002CE040U;
constexpr uint32_t kBlockingQueuePush = 0x002D2EF4U;
constexpr uint32_t kRendererCommandQueueProcessPending = 0x002DBCBCU;
constexpr uint32_t kBlockingQueueTryPush = 0x00490D60U;
constexpr uint32_t kTaskQueueTakeFirstFromPriority = 0x002D33CCU;
constexpr uint32_t kTaskQueueEnqueuePriorityNode = 0x0030AB9CU;
constexpr uint32_t kTaskQueueWorkerDrainPriorityNodes = 0x004667B8U;
constexpr uint32_t kNngxP3dCallback = 0x00415AE4U;
constexpr uint32_t kNngxCommonInterruptHandler = 0x003025C0U;
constexpr uint32_t kNngxGlobalState = 0x005A6F30U;
constexpr uint32_t kNngxCmdlistCompletionDispatch = 0x003FD420U;
constexpr uint32_t kNngxCmdlistOwnerState = 0x0054CC34U;
constexpr uint32_t kRendererGlobalCallbackAfterLookup = 0x0016CB78U;
constexpr uint32_t kRendererCommandQueue = 0x005B93A8U;
constexpr uint32_t kInputGlobalState = 0x0056559CU;
constexpr uint32_t kInputHeldOffset = 0x04U;
constexpr uint32_t kInputPressedOffset = 0x08U;
constexpr uint32_t kInputReleasedOffset = 0x0CU;
constexpr uint32_t kInputRawCurrentOffset = 0xC4U;
constexpr uint32_t kInputRawPreviousOffset = 0xC8U;
constexpr size_t kMaximumA32BlockTraceRecords = 65'536U;

uint64_t HashDiagnosticBytes(std::span<const uint8_t> bytes) noexcept {
  uint64_t hash = 14695981039346656037ULL;
  for (const uint8_t byte : bytes) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  return hash;
}

bool IsNativeFrontendOpaqueTargetInitializationDraw(
    const Oot3dNativeGame::Oot3dPicaDrawSubmission &draw) {
  const auto &state = draw.State;
  const auto &blend = state.OutputMerger.Blend;
  const auto &depth = state.OutputMerger.Depth;
  const bool hasTexture =
      std::any_of(state.Textures.begin(), state.Textures.end(),
                  [](const auto &texture) { return texture.Enabled; });
  // Native UI targets include rotated/padded storage outside the logical
  // viewport, so framebuffer dimensions cannot establish whether this copy
  // quad initializes the frontend canvas. The replacement blend/depth state
  // identifies that operation; its association with CommonBackground00's
  // target AND viewport is checked by the caller, preserving the scene clear.
  return !hasTexture && state.VertexInput.Indexed &&
         state.VertexInput.VertexCount == 4U &&
         state.OutputMerger.ColorWriteMask == 0x0FU &&
         state.OutputMerger.LogicOperation ==
             Oot3dNativeGame::Oot3dPicaLogicOperation::Copy &&
         blend.Enabled &&
         blend.SourceColor == Oot3dNativeGame::Oot3dPicaBlendFactor::One &&
         blend.DestinationColor ==
             Oot3dNativeGame::Oot3dPicaBlendFactor::Zero &&
         blend.SourceAlpha == Oot3dNativeGame::Oot3dPicaBlendFactor::One &&
         blend.DestinationAlpha ==
             Oot3dNativeGame::Oot3dPicaBlendFactor::Zero &&
         depth.TestEnabled && depth.WriteEnabled &&
         depth.Compare == Oot3dNativeGame::Oot3dPicaCompareFunction::Always;
}

struct NativeSceneProjectionState {
  struct CallsiteStats {
    uint64_t Calls = 0;
    float FirstLeft = 0.0F;
    float FirstRight = 0.0F;
  };

  Fast::Oot3d::ScenePresentationPolicy Presentation{};
  float FovMultiplier = 1.0F;
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
  uint64_t BlockTraceRecordsObserved = 0;
  Oot3dNativeGame::NativeA32Memory *TraceMemory = nullptr;
  std::deque<A32BlockTraceRecord> BlockTrace;
  Oot3dNativeGame::Oot3dNativeUiLifecycleBridge *UiLifecycleBridge = nullptr;
  Oot3dNativeGame::Oot3dPicaCompositionDomain *PicaCompositionDomain =
      nullptr;
  Oot3dNativeGame::Oot3dNativePicaCompositionTracker
      *PicaCompositionTracker = nullptr;
  Oot3dNativeGame::TopScreenTextureOverrideRuntime *TopScreenTextureOverrides =
      nullptr;
  bool TopScreenUiProfile = false;
  Oot3dNativeGame::TopScreenUiConfig TopScreenConfig;
  const Oot3dNativeGame::TopScreenExtendedInputFrame *TopScreenInput = nullptr;
  std::span<const uint32_t> WholeAotObservableExitPcs;
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
  Oot3dNativeGame::NativeA32PlayerTimingProbe *PlayerTimingProbe = nullptr;
  Oot3dNativeGame::NativeTemporalEventLedger *TemporalEventLedger = nullptr;
  Oot3dNativeGame::NativeA32PlayerTemporalBridge *PlayerTemporalBridge =
      nullptr;
  nlohmann::json PlayerTimingEvents = nlohmann::json::array();
  bool PlayerTimingEventsTruncated = false;
};

void WriteA32BlockTrace(const std::filesystem::path &path,
                        const NativeWidescreenProjectionState &runtime) {
  const auto parent = path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    throw std::runtime_error("cannot open A32 block trace: " + path.string());
  }
  stream << nlohmann::json({
                               {"format", "oot3d_a32_block_trace_v2"},
                               {"records", runtime.BlockTrace.size()},
                               {"records_observed",
                                runtime.BlockTraceRecordsObserved},
                               {"truncated", runtime.BlockTraceTruncated},
                           })
                .dump()
         << '\n';
  for (const auto &record : runtime.BlockTrace) {
    stream
        << nlohmann::json(
               {
                   {"host_frame", record.HostFrame},
                   {"pc", record.Pc},
                   {"memory_write_generation", record.MemoryWriteGeneration},
                   {"memory_write_fingerprint", record.MemoryWriteFingerprint},
                   {"fast_read_mismatch_count", record.FastReadMismatchCount},
                   {"last_fast_read_mismatch_address",
                    record.LastFastReadMismatchAddress},
                   {"last_fast_read_mismatch_value",
                    record.LastFastReadMismatchValue},
                   {"last_checked_read_mismatch_value",
                    record.LastCheckedReadMismatchValue},
                   {"r", record.Registers},
                   {"cpsr", record.Cpsr},
                   {"fpscr", record.Fpscr},
                   {"thread_pointer", record.ThreadPointer},
                   {"vfp", record.Vfp},
                   {"exclusive_address", record.ExclusiveAddress},
                   {"exclusive_token", record.ExclusiveToken},
                   {"exclusive_size", record.ExclusiveSize},
                   {"exclusive_valid", record.ExclusiveValid},
               })
               .dump()
        << '\n';
  }
  if (!stream) {
    throw std::runtime_error("cannot write A32 block trace: " + path.string());
  }
}

class ScopedA32BlockTraceWriter final {
 public:
  ScopedA32BlockTraceWriter(
      const std::filesystem::path &path,
      const NativeWidescreenProjectionState &runtime)
      : Path(path), Runtime(runtime) {}

  ~ScopedA32BlockTraceWriter() {
    if (Written || Path.empty()) {
      return;
    }
    try {
      Write();
    } catch (const std::exception &exception) {
      std::cerr << "oot3d_native_game: failed to preserve A32 fault trace: "
                << exception.what() << '\n';
    }
  }

  void Write() {
    if (Written || Path.empty()) {
      return;
    }
    WriteA32BlockTrace(Path, Runtime);
    Written = true;
  }

 private:
  std::filesystem::path Path;
  const NativeWidescreenProjectionState &Runtime;
  bool Written = false;
};

double SecondsSince(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
      .count();
}

struct NativeFramePhaseTiming {
  double GuestSeconds = 0.0;
  double FrameStartSeconds = 0.0;
  double DspMixSeconds = 0.0;
  double AudioOutputSeconds = 0.0;
  double PicaSubmitSeconds = 0.0;
  double PicaPlanSeconds = 0.0;
  double PicaBackendSeconds = 0.0;
  double PicaDiagnosticsSeconds = 0.0;
  double VisualPresentationSeconds = 0.0;
  double VisualReplaySeconds = 0.0;
  double PresentSeconds = 0.0;
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
  Oot3dNativeGame::TopScreenCameraFovOwnership TopScreenCameraFov;
  Oot3dNativeGame::NativeFreeCameraInputAccumulator TopScreenCameraInput;
  Oot3dNativeGame::TopScreenExtendedInputFrame TopScreenInput;
  bool PreviousTopScreenZrHeld = false;
  bool PreviousTopScreenZlHeld = false;
  uint32_t PreviousTopScreenButtons = 0U;
  Oot3dNativeGame::NativeA32PolledButtonLatch StartButtonLatch;
  Oot3dNativeGame::TopScreenStartRoutingState TopScreenStartRouting;
  Oot3dNativeGame::TopScreenPauseSystemOpenState TopScreenPauseSystemOpen;
  uint64_t TopScreenPauseStartCloseCalls = 0U;
  uint64_t TopScreenPauseSystemOpenCalls = 0U;
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
  Oot3dNativeGame::TopScreenPauseClosePage TopScreenPauseLastClosedPage =
      Oot3dNativeGame::TopScreenPauseClosePage::None;
  Oot3dNativeGame::TopScreenPauseClosePage TopScreenPauseSystemSourcePage =
      Oot3dNativeGame::TopScreenPauseClosePage::None;
  uint8_t TopScreenViewportDrawPhase = 0U;
  uint32_t TopScreenViewportDrawArgument = 0U;
  uint8_t TopScreenSceneViewportDrawPhase = 0U;
  uint32_t TopScreenSceneViewportDrawArgument = 0U;
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
  Oot3dNativeGame::TopScreenPauseControllerState TopScreenPauseController;
  uint8_t TopScreenPauseControllerDrawPhase = 0U;
  std::array<uint32_t, 2> TopScreenPauseControllerVisibleRenderers{};
  uint32_t PendingTopScreenItemsSelection = 0U;
  uint32_t SampleState = 0xA32C0DE1U;
  uint64_t Calls = 0;
  uint64_t Samples = 0;
  uint64_t SourceActorInitContextSamples = 0;
  uint64_t SourceActorInitContextSampleNanoseconds = 0;
  uint64_t SourceActorUpdateAllSamples = 0;
  uint64_t SourceActorUpdateAllSampleNanoseconds = 0;
  uint64_t SourceCutsceneUpdateFrameSamples = 0;
  uint64_t SourceCutsceneUpdateFrameSampleNanoseconds = 0;
  uint64_t SourceCutsceneProcessCommandsSamples = 0;
  uint64_t SourceCutsceneProcessCommandsSampleNanoseconds = 0;
  uint64_t SourceCameraUpdateSamples = 0;
  uint64_t SourceCameraUpdateSampleNanoseconds = 0;
  uint64_t SourcePlayerUpdateSamples = 0;
  uint64_t SourcePlayerUpdateSampleNanoseconds = 0;
  uint64_t SourcePlayerUpdateCommonSamples = 0;
  uint64_t SourcePlayerUpdateCommonSampleNanoseconds = 0;
  uint64_t SourceCsabCurveSamples = 0;
  uint64_t SourceCsabCurveSampleNanoseconds = 0;
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
  std::array<uint64_t, 4> TopScreenItemQueryCalls{};
  std::array<uint64_t, 4> TopScreenItemQueryTrueResults{};
  uint64_t TopScreenSlotItemAttempts = 0;
  uint64_t TopScreenSlotItemOverrides = 0;
  uint64_t TopScreenItemQueryEntries = 0;
  uint64_t TopScreenSlotItemEntries = 0;
  uint64_t TopScreenZrHeldFrames = 0;
  uint64_t TopScreenZlHeldFrames = 0;
  bool TopScreenZrPressLatched = false;
  bool TopScreenZlPressLatched = false;
  uint32_t TopScreenZrPressLatchFrames = 0U;
  uint32_t TopScreenZlPressLatchFrames = 0U;
  bool PreviousPhysicalZrHeld = false;
  bool PreviousPhysicalZlHeld = false;
  bool TopScreenZrHostEdgePending = false;
  bool TopScreenZlHostEdgePending = false;
  uint64_t TopScreenZrHostEdges = 0;
  uint64_t TopScreenZlHostEdges = 0;
  uint64_t TopScreenZrGuestEdges = 0;
  uint64_t TopScreenZlGuestEdges = 0;
  uint64_t TopScreenSlotItemNativeFallbacks = 0;
  Oot3dNativeGame::Oot3dNativeUiLifecycleBridge *UiLifecycleBridge = nullptr;
  Oot3dNativeGame::Oot3dPicaCompositionDomain *PicaCompositionDomain =
      nullptr;
  Oot3dNativeGame::TopScreenPauseProjectionState *TopScreenPauseProjection =
      nullptr;
  const Oot3dNativeGame::TopScreenUiConfig *TopScreenConfig = nullptr;
  Oot3dNativeGame::SourceActorInitContextRuntime *SourceActorInitContext =
      nullptr;
  Oot3dNativeGame::SourceActorUpdateAllRuntime *SourceActorUpdateAll =
      nullptr;
  Oot3dNativeGame::SourceCutsceneUpdateFrameRuntime
      *SourceCutsceneUpdateFrame = nullptr;
  Oot3dNativeGame::SourceCutsceneProcessCommandsRuntime
      *SourceCutsceneProcessCommands = nullptr;
  Oot3dNativeGame::SourceCameraUpdateRuntime *SourceCameraUpdate = nullptr;
  Oot3dNativeGame::SourcePlayerUpdateRuntime *SourcePlayerUpdate = nullptr;
  Oot3dNativeGame::SourcePlayerUpdateCommonRuntime
      *SourcePlayerUpdateCommon = nullptr;
  Oot3dNativeGame::SourceCsabCurveRuntime *SourceCsabCurve = nullptr;
};

bool ExecuteTopScreenUiSourcePortBlock(
    uint32_t pc, oot3d::recomp::a32::GuestState &state,
    NativeCandidateDispatchState &dispatch,
    oot3d::recomp::a32::ExecutionResult *result, uint32_t *blocksConsumed) {
  auto &memory = *dispatch.Memory;
  uint32_t nextPc = 0U;
  auto branchFromCallsite = [&](uint32_t target, uint32_t returnAddress) {
    state.r[14] = returnAddress;
    state.r[15] = target;
    *result = {oot3d::recomp::a32::ExitKind::Branch, target,
               oot3d::recomp::a32::FallbackReason::None, pc};
    if (blocksConsumed != nullptr)
      *blocksConsumed = 1U;
    return true;
  };
  if (pc == kTopScreenTitleLogoFadeHoldPrefix) {
    Oot3dNativeGame::TopScreenTitleLogoFadeHoldResult hold;
    if (!Oot3dNativeGame::ApplyTopScreenTitleLogoFadeHold(
            memory, state.r[4], state.r[5], state.vfp[16], &hold)) {
      return false;
    }
    ++dispatch.TopScreenTitleLogoFadeHoldCalls;
    state.r[0] = hold.AnimationOwner;
    state.r[1] = hold.Argument1;
    state.r[15] = hold.NextPc;
    *result = {oot3d::recomp::a32::ExitKind::Branch, hold.NextPc,
               oot3d::recomp::a32::FallbackReason::None, pc};
    if (blocksConsumed != nullptr)
      *blocksConsumed = 1U;
    return true;
  }
  if (pc == kPauseConditionalDrawCall) {
    Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
    if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &inputs)) {
      return false;
    }
    if (Oot3dNativeGame::ResolveTopScreenPauseRouteActive(
            inputs, &dispatch.TopScreenPauseRoute)) {
      return branchFromCallsite(kPauseConditionalNativeDraw, pc + 4U);
    }
    return branchFromCallsite(pc + 4U, pc + 4U);
  }
  if (pc == kPauseTouchCoordinateUpdateCall) {
    if (dispatch.TopScreenTouchCoordinateUpdatePhase == 0U) {
      dispatch.TopScreenTouchCoordinateUpdatePhase = 1U;
      return branchFromCallsite(kPauseTouchCoordinateNativeUpdate, pc);
    }
    dispatch.TopScreenTouchCoordinateUpdatePhase = 0U;
    Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
    std::uint32_t pauseCameraControl = 0U;
    std::uint32_t secondaryLayer = 0U;
    if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &inputs) ||
        !memory.Read32(0x00506CE8U, &pauseCameraControl) ||
        !memory.Read32(0x004FDABCU, &secondaryLayer)) {
      return false;
    }
    const bool routeActive = Oot3dNativeGame::ResolveTopScreenPauseRouteActive(
        inputs, &dispatch.TopScreenPauseRoute);
    if (Oot3dNativeGame::ResolveTopScreenTouchCoordinateSuppression(
            inputs, routeActive, pauseCameraControl != 0U, secondaryLayer == 1U,
            &dispatch.TopScreenTouchCoordinateRoute) &&
        !Oot3dNativeGame::ApplyTopScreenTouchCoordinateSuppression(memory)) {
      return false;
    }
    return branchFromCallsite(pc + 4U, pc + 4U);
  }
  if (pc == kPauseRendererVisibilityCall) {
    Oot3dNativeGame::TopScreenPauseDrawInputs globalInputs;
    if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &globalInputs)) {
      return false;
    }
    Oot3dNativeGame::TopScreenRendererVisibilityInputs inputs;
    inputs.ControllerAddress = state.r[0];
    inputs.HasCallScene = state.r[1] != 0U;
    inputs.GlobalSceneRuntimeRoute =
        globalInputs.HasScene && globalInputs.SceneMode == 3U &&
        (globalInputs.RuntimeMode == 1U || globalInputs.RuntimeMode == 2U);
    inputs.RuntimeSceneLatch =
        dispatch.TopScreenTouchCoordinateRoute.RuntimeSceneLatch;
    inputs.NativeRuntimeActive = globalInputs.NativeTransitionActive;
    std::uint16_t nativeFadeGate = 0U;
    if (!memory.Read16(0x0058799CU, &nativeFadeGate))
      return false;
    inputs.NativeFadeGate = static_cast<std::int16_t>(nativeFadeGate);
    if (inputs.HasCallScene &&
        (!memory.Read8(state.r[1] + 0x100U, &inputs.CallSceneMode) ||
         !memory.Read32(state.r[1] + 0x104U, &inputs.CallSceneSubmode) ||
         !memory.Read16(state.r[1] + 0x318CU, &inputs.CallSceneSequence))) {
      return false;
    }
    Oot3dNativeGame::TopScreenRendererVisibilityAction action;
    if (!Oot3dNativeGame::ResolveTopScreenRendererVisibilityRoute(
            memory, inputs, &dispatch.TopScreenRendererVisibilityRoute,
            &action)) {
      return false;
    }
    return branchFromCallsite(
        action == Oot3dNativeGame::TopScreenRendererVisibilityAction::NativeDraw
            ? kPauseRendererVisibilityNativeDraw
            : pc + 4U,
        pc + 4U);
  }
  if (pc == kPauseControllerDrawCall) {
    if (dispatch.TopScreenPauseControllerDrawPhase == 1U) {
      if (!Oot3dNativeGame::PrepareTopScreenPauseAlternateRenderers(
              memory, &dispatch.TopScreenPauseController,
              &dispatch.TopScreenPauseControllerVisibleRenderers)) {
        return false;
      }
      dispatch.TopScreenPauseControllerDrawPhase = 2U;
      state.r[0] = 0U;
      state.r[1] = 40U;
      state.r[2] = 480U;
      state.r[3] = 320U;
      return branchFromCallsite(kGlViewportEntry, pc);
    }
    if (dispatch.TopScreenPauseControllerDrawPhase == 2U) {
      dispatch.TopScreenPauseControllerDrawPhase = 3U;
      const auto renderer =
          dispatch.TopScreenPauseControllerVisibleRenderers[0];
      if (renderer != 0U) {
        state.r[0] = renderer;
        return branchFromCallsite(kPauseControllerRendererDraw, pc);
      }
    }
    if (dispatch.TopScreenPauseControllerDrawPhase == 3U) {
      dispatch.TopScreenPauseControllerDrawPhase = 4U;
      const auto renderer =
          dispatch.TopScreenPauseControllerVisibleRenderers[1];
      if (renderer != 0U) {
        state.r[0] = renderer;
        return branchFromCallsite(kPauseControllerRendererDraw, pc);
      }
    }
    if (dispatch.TopScreenPauseControllerDrawPhase == 4U) {
      dispatch.TopScreenPauseControllerDrawPhase = 5U;
      state.r[0] = 0U;
      state.r[1] = 0U;
      state.r[2] = 480U;
      state.r[3] = 400U;
      return branchFromCallsite(kGlViewportEntry, pc);
    }
    if (dispatch.TopScreenPauseControllerDrawPhase == 5U) {
      dispatch.TopScreenPauseControllerDrawPhase = 0U;
      dispatch.TopScreenPauseControllerVisibleRenderers = {};
      Oot3dNativeGame::TopScreenPauseControllerInputs inputs;
      if (!Oot3dNativeGame::ReadTopScreenPauseControllerInputs(memory,
                                                               &inputs)) {
        return false;
      }
      if (dispatch.UiLifecycleBridge != nullptr) {
        dispatch.UiLifecycleBridge->SetTopScreenPauseEdgePresentation(
            Oot3dNativeGame::BuildTopScreenPauseEdgeGeometry(
                inputs.Pause.HasScene, inputs.Pause.SceneMode,
                inputs.Pause.RuntimeMode, 0U, 0, 1.0F, 1.0F));
      }
      return branchFromCallsite(pc + 4U, pc + 4U);
    }
    Oot3dNativeGame::TopScreenPauseControllerInputs inputs;
    if (!Oot3dNativeGame::ReadTopScreenPauseControllerInputs(memory, &inputs)) {
      return false;
    }
    const bool routeActive = Oot3dNativeGame::ResolveTopScreenPauseRouteActive(
        inputs.Pause, &dispatch.TopScreenPauseRoute);
    Oot3dNativeGame::TopScreenPauseControllerAction action;
    if (!Oot3dNativeGame::ResolveTopScreenPauseController(
            memory, inputs, routeActive, &dispatch.TopScreenPauseController,
            &action)) {
      return false;
    }
    if (action ==
        Oot3dNativeGame::TopScreenPauseControllerAction::SuppressNativeDraw) {
      return branchFromCallsite(pc + 4U, pc + 4U);
    }
    if (action == Oot3dNativeGame::TopScreenPauseControllerAction::
                      NativeDrawThenAlternateOverlay) {
      dispatch.TopScreenPauseControllerDrawPhase = 1U;
      return branchFromCallsite(kPauseControllerNativeDraw, pc);
    }
    return branchFromCallsite(kPauseControllerNativeDraw, pc + 4U);
  }
  if (pc == kPauseRoutedCommandCallA || pc == kPauseRoutedCommandCallB) {
    Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
    if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &inputs)) {
      return false;
    }
    const bool routeActive = Oot3dNativeGame::ResolveTopScreenPauseRouteActive(
        inputs, &dispatch.TopScreenPauseRoute);
    state.r[1] = Oot3dNativeGame::ResolveTopScreenPauseRoutedCommand(
        state.r[1], routeActive);
    return branchFromCallsite(kPauseRoutedCommandNative, pc + 4U);
  }
  if (pc == kPauseOverlayViewportDrawCall) {
    if (dispatch.TopScreenOverlayViewportDrawPhase == 0U) {
      Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
      if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &inputs)) {
        return false;
      }
      if (!Oot3dNativeGame::ResolveTopScreenPauseRouteActive(
              inputs, &dispatch.TopScreenPauseRoute)) {
        if (dispatch.UiLifecycleBridge != nullptr) {
          dispatch.UiLifecycleBridge->SetTopScreenPauseEdgePresentation(
              std::nullopt);
        }
        return branchFromCallsite(kPauseOverlayNativeDraw, pc + 4U);
      }
      dispatch.TopScreenOverlayViewportDrawPhase = 1U;
      state.r[0] = 0U;
      state.r[1] = 40U;
      state.r[2] = 480U;
      state.r[3] = 320U;
      return branchFromCallsite(kGlViewportEntry, pc);
    }
    if (dispatch.TopScreenOverlayViewportDrawPhase == 1U) {
      dispatch.TopScreenOverlayViewportDrawPhase = 2U;
      return branchFromCallsite(kPauseOverlayNativeDraw, pc);
    }
    dispatch.TopScreenOverlayViewportDrawPhase = 0U;
    Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
    if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &inputs)) {
      return false;
    }
    if (!Oot3dNativeGame::UseTopScreenSceneFiveViewport(inputs)) {
      if (dispatch.UiLifecycleBridge != nullptr) {
        dispatch.UiLifecycleBridge->SetTopScreenPauseEdgePresentation(
            std::nullopt);
      }
      return branchFromCallsite(pc + 4U, pc + 4U);
    }
    if (dispatch.UiLifecycleBridge != nullptr) {
      dispatch.UiLifecycleBridge->SetTopScreenPauseEdgePresentation(
          Oot3dNativeGame::BuildTopScreenPauseEdgeGeometry(
              inputs.HasScene, inputs.SceneMode, inputs.RuntimeMode, 0U, 0,
              1.0F, 1.0F));
    }
    state.r[0] = 0U;
    state.r[1] = 0U;
    state.r[2] = 480U;
    state.r[3] = 400U;
    return branchFromCallsite(kGlViewportEntry, pc + 4U);
  }
  if (pc == kPauseSceneSevenDrawCall) {
    Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
    if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &inputs)) {
      return false;
    }
    if (inputs.HasScene && inputs.SceneMode == 7U) {
      return branchFromCallsite(pc + 4U, pc + 4U);
    }
    return branchFromCallsite(kPauseSceneSevenNativeDraw, pc + 4U);
  }
  if (pc == kPauseSceneViewportDrawCall) {
    auto branch = [&](uint32_t target, uint32_t returnAddress) {
      state.r[14] = returnAddress;
      state.r[15] = target;
      *result = {oot3d::recomp::a32::ExitKind::Branch, target,
                 oot3d::recomp::a32::FallbackReason::None, pc};
      if (blocksConsumed != nullptr)
        *blocksConsumed = 1U;
      return true;
    };
    if (dispatch.TopScreenSceneViewportDrawPhase == 0U) {
      Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
      if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &inputs)) {
        return false;
      }
      if (!Oot3dNativeGame::UseTopScreenSceneFiveViewport(inputs)) {
        return branch(kPauseSceneViewportNativeDraw, pc + 4U);
      }
      dispatch.TopScreenSceneViewportDrawArgument = state.r[0];
      dispatch.TopScreenSceneViewportDrawPhase = 1U;
      state.r[0] = 0U;
      state.r[1] = static_cast<uint32_t>(-80);
      state.r[2] = 240U;
      state.r[3] = 480U;
      return branch(kGlViewportEntry, pc);
    }
    if (dispatch.TopScreenSceneViewportDrawPhase == 1U) {
      dispatch.TopScreenSceneViewportDrawPhase = 2U;
      state.r[0] = dispatch.TopScreenSceneViewportDrawArgument;
      return branch(kPauseSceneViewportNativeDraw, pc);
    }
    dispatch.TopScreenSceneViewportDrawPhase = 0U;
    dispatch.TopScreenSceneViewportDrawArgument = 0U;
    state.r[0] = 0U;
    state.r[1] = 0U;
    state.r[2] = 240U;
    state.r[3] = 320U;
    return branch(kGlViewportEntry, pc + 4U);
  }
  if (pc == kPauseAlternateRendererUpdateCall) {
    if (!dispatch.TopScreenAlternateRendererRestore.has_value()) {
      Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
      if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &inputs)) {
        return false;
      }
      if (Oot3dNativeGame::UseTopScreenAlternatePauseViewport(inputs)) {
        Oot3dNativeGame::TopScreenAlternateRendererState saved;
        if (!Oot3dNativeGame::BeginTopScreenAlternateRendererSuppression(
                memory, &saved)) {
          return false;
        }
        dispatch.TopScreenAlternateRendererRestore = saved;
        state.r[14] = pc;
      } else {
        state.r[14] = pc + 4U;
      }
      state.r[15] = kPauseAlternateRendererNativeUpdate;
      *result = {oot3d::recomp::a32::ExitKind::Branch,
                 kPauseAlternateRendererNativeUpdate,
                 oot3d::recomp::a32::FallbackReason::None, pc};
      if (blocksConsumed != nullptr)
        *blocksConsumed = 1U;
      return true;
    }
    if (!Oot3dNativeGame::EndTopScreenAlternateRendererSuppression(
            memory, *dispatch.TopScreenAlternateRendererRestore)) {
      return false;
    }
    dispatch.TopScreenAlternateRendererRestore.reset();
    nextPc = pc + 4U;
    state.r[15] = nextPc;
    *result = {oot3d::recomp::a32::ExitKind::Branch, nextPc,
               oot3d::recomp::a32::FallbackReason::None, pc};
    if (blocksConsumed != nullptr)
      *blocksConsumed = 1U;
    return true;
  }
  if (pc == kPauseAlternateViewportDrawCall) {
    auto branch = [&](uint32_t target, uint32_t returnAddress) {
      state.r[14] = returnAddress;
      state.r[15] = target;
      *result = {oot3d::recomp::a32::ExitKind::Branch, target,
                 oot3d::recomp::a32::FallbackReason::None, pc};
      if (blocksConsumed != nullptr)
        *blocksConsumed = 1U;
      return true;
    };
    if (dispatch.TopScreenViewportDrawPhase == 0U) {
      Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
      if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &inputs)) {
        return false;
      }
      if (!Oot3dNativeGame::UseTopScreenAlternatePauseViewport(inputs)) {
        return branch(kPauseAlternateViewportNativeDraw, pc + 4U);
      }
      dispatch.TopScreenViewportDrawArgument = state.r[0];
      dispatch.TopScreenViewportDrawPhase = 1U;
      state.r[0] = 0U;
      state.r[1] = 40U;
      state.r[2] = 480U;
      state.r[3] = 320U;
      return branch(kGlViewportEntry, pc);
    }
    if (dispatch.TopScreenViewportDrawPhase == 1U) {
      dispatch.TopScreenViewportDrawPhase = 2U;
      state.r[0] = dispatch.TopScreenViewportDrawArgument;
      return branch(kPauseAlternateViewportNativeDraw, pc);
    }
    dispatch.TopScreenViewportDrawPhase = 0U;
    dispatch.TopScreenViewportDrawArgument = 0U;
    state.r[0] = 0U;
    state.r[1] = 0U;
    state.r[2] = 480U;
    state.r[3] = 400U;
    return branch(kGlViewportEntry, pc + 4U);
  }
  if (pc == kTopScreenCameraUpdateEntry ||
      pc == kTopScreenCameraUpdatePatchSite) {
    const bool enteredAtFunctionEntry = pc == kTopScreenCameraUpdateEntry;
    if (enteredAtFunctionEntry &&
        dispatch.TopScreenCamera.Phase ==
            Oot3dNativeGame::TopScreenCameraGuestPhase::Idle) {
      std::uint16_t cameraSetting = 0U;
      std::uint16_t cameraIndex = 0xFFFFU;
      if (!memory.Read16(state.r[1] + 0x18AU, &cameraSetting) ||
          !memory.Read16(state.r[1] + 0x1ACU, &cameraIndex)) {
        return false;
      }
      dispatch.TopScreenCameraFov =
          Oot3dNativeGame::ResolveTopScreenCameraFovOwnership(
              {state.r[0], static_cast<std::int16_t>(cameraSetting),
               static_cast<std::int16_t>(cameraIndex)});
    }
    if (!dispatch.TopScreenCamera.Camera.Enabled) {
      dispatch.TopScreenExtendedCameraActive = false;
      return false;
    }
    const uint32_t cameraStackAddress =
        enteredAtFunctionEntry ? state.r[13] - 36U : state.r[13];
    auto branchToNative = [&](uint32_t target, uint32_t argument0,
                              uint32_t argument1 = 0U,
                              uint32_t argument2 = 0U) {
      state.r[0] = argument0;
      state.r[1] = argument1;
      state.r[2] = argument2;
      // Return through the real function entry: 0x002D84C8 is the IPS patch
      // site but not a materialized AOT block boundary. The staged phase
      // distinguishes this callback from the initial function invocation.
      state.r[14] = kTopScreenCameraUpdateEntry;
      state.r[15] = target;
      *result = {oot3d::recomp::a32::ExitKind::Branch, target,
                 oot3d::recomp::a32::FallbackReason::None, pc};
      if (blocksConsumed != nullptr)
        *blocksConsumed = 1U;
      return true;
    };
    auto finishUpdate = [&]() {
      const uint32_t stack = dispatch.TopScreenCamera.OriginalStackAddress;
      for (uint32_t index = 0U; index < 8U; ++index) {
        if (!memory.Read32(stack + index * 4U, &state.r[4U + index])) {
          return false;
        }
      }
      if (!memory.Read32(stack + 32U, &nextPc))
        return false;
      state.r[13] = stack + 36U;
      state.r[15] = nextPc;
      Oot3dNativeGame::ResetTopScreenFreeCameraGuestUpdate(
          &dispatch.TopScreenCamera);
      dispatch.TopScreenExtendedCameraActive =
          dispatch.TopScreenCamera.Camera.Active;
      *result = {oot3d::recomp::a32::ExitKind::Branch, nextPc,
                 oot3d::recomp::a32::FallbackReason::None, pc};
      if (blocksConsumed != nullptr)
        *blocksConsumed = 1U;
      return true;
    };

    switch (dispatch.TopScreenCamera.Phase) {
    case Oot3dNativeGame::TopScreenCameraGuestPhase::Idle: {
      ++dispatch.TopScreenCameraUpdateCalls;
      bool active = false;
      const auto cameraInput = dispatch.TopScreenCameraInput.Consume();
      if (!Oot3dNativeGame::BeginTopScreenFreeCameraGuestUpdate(
              memory, state.r[0], state.r[1], cameraStackAddress,
              cameraInput.X, cameraInput.Y,
              cameraInput.Kind ==
                  Oot3dNativeGame::NativeFreeCameraInputKind::Relative,
              &dispatch.TopScreenCamera, &active)) {
        return false;
      }
      dispatch.TopScreenExtendedCameraActive = active;
      dispatch.TopScreenCameraActiveCalls += active ? 1U : 0U;
      if (!active) {
        // The payload owns Camera_Update only while free-camera
        // ownership is active. Let the compiled native function run
        // from its original entry so it retains its optimized region
        // and executes the original `mov r6, r0` itself.
        return false;
      }
      if (enteredAtFunctionEntry) {
        // Native 0x002D84C4 is `push {r4-r11, lr}`. The IPS record replaces
        // the following `mov r6, r0`, but AOT callbacks can only observe the
        // function's real basic-block entry. Materialize the prologue only
        // after the typed path has accepted ownership; otherwise returning
        // false executes the complete original function unchanged.
        for (uint32_t index = 0U; index < 8U; ++index) {
          if (!memory.Write32(cameraStackAddress + index * 4U,
                              state.r[4U + index])) {
            return false;
          }
        }
        if (!memory.Write32(cameraStackAddress + 32U, state.r[14])) {
          return false;
        }
        state.r[13] = cameraStackAddress;
      }
      state.r[13] = dispatch.TopScreenCamera.ScratchAddress;
      return branchToNative(Oot3dNativeGame::kTopScreenCameraCheckWater,
                            dispatch.TopScreenCamera.CameraAddress);
    }
    case Oot3dNativeGame::TopScreenCameraGuestPhase::Water:
      if (!Oot3dNativeGame::ApplyTopScreenFreeCameraEnvironment(
              memory, &dispatch.TopScreenCamera))
        return false;
      return branchToNative(Oot3dNativeGame::kTopScreenCameraUpdateInterface,
                            0U);
    case Oot3dNativeGame::TopScreenCameraGuestPhase::Interface:
      if (!Oot3dNativeGame::PrepareTopScreenFreeCameraCollision(
              memory, &dispatch.TopScreenCamera))
        return false;
      if (dispatch.TopScreenCamera.PlayerAddress == 0U) {
        return finishUpdate();
      }
      return branchToNative(Oot3dNativeGame::kTopScreenCameraBgCheckInfo,
                            dispatch.TopScreenCamera.CameraAddress,
                            dispatch.TopScreenCamera.ScratchAddress + 0x20U,
                            dispatch.TopScreenCamera.ScratchAddress + 0x30U);
    case Oot3dNativeGame::TopScreenCameraGuestPhase::Collision:
      if (!Oot3dNativeGame::CommitTopScreenFreeCameraCollision(
              memory, &dispatch.TopScreenCamera))
        return false;
      return branchToNative(Oot3dNativeGame::kTopScreenCameraQuakeUpdate,
                            dispatch.TopScreenCamera.CameraAddress,
                            dispatch.TopScreenCamera.ScratchAddress + 0x60U);
    case Oot3dNativeGame::TopScreenCameraGuestPhase::Quake: {
      const bool quakeActive = state.r[0] != 0U;
      if (!Oot3dNativeGame::ApplyTopScreenFreeCameraQuake(
              memory, &dispatch.TopScreenCamera, quakeActive)) {
        return false;
      }
      std::uint32_t floorPoly = 0U;
      std::uint8_t bgId = 0U;
      if (!memory.Read32(dispatch.TopScreenCamera.PlayerAddress + 0x7CU,
                         &floorPoly) ||
          !memory.Read8(dispatch.TopScreenCamera.PlayerAddress + 0x81U, &bgId))
        return false;
      return branchToNative(Oot3dNativeGame::kTopScreenCameraGetDataId,
                            dispatch.TopScreenCamera.GlobalContextAddress +
                                0xA98U,
                            floorPoly, bgId);
    }
    case Oot3dNativeGame::TopScreenCameraGuestPhase::CameraData:
      if (!Oot3dNativeGame::ApplyTopScreenFreeCameraData(
              memory, &dispatch.TopScreenCamera,
              static_cast<int16_t>(state.r[0])))
        return false;
      return finishUpdate();
    }
  }
  if (pc == kTopScreenCameraFovScalar) {
    if (!dispatch.TopScreenCameraFov.Active ||
        dispatch.TopScreenCameraFov.ValueAddress != state.r[4] + 0x10U ||
        dispatch.TopScreenCamera.Camera.FovPercent == 100U) {
      return false;
    }
    std::uint32_t nativeFovBits = 0U;
    if (!memory.Read32(state.r[4] + 0x10U, &nativeFovBits)) {
      return false;
    }
    const float resolvedFov = Oot3dNativeGame::ResolveTopScreenCameraFovDegrees(
        std::bit_cast<float>(nativeFovBits),
        dispatch.TopScreenCamera.Camera.FovPercent);
    state.vfp[16] = std::bit_cast<std::uint32_t>(resolvedFov);
    nextPc = pc + 4U;
    state.r[15] = nextPc;
    *result = {oot3d::recomp::a32::ExitKind::Branch, nextPc,
               oot3d::recomp::a32::FallbackReason::None, pc};
    if (blocksConsumed != nullptr)
      *blocksConsumed = 1U;
    return true;
  }
  if (pc == kTopScreenCameraNormal1Scalar) {
    if (!dispatch.TopScreenCamera.Camera.N64StyleZoom) {
      return false;
    }
    ++dispatch.TopScreenCameraNormal1Calls;
    const auto scalar = Oot3dNativeGame::ResolveTopScreenCameraNormal1Scalar(
        state.vfp[0], dispatch.TopScreenCamera.Camera.ZoomPercent);
    if (!memory.Write32(state.r[4] + 0x124U, scalar.NativeValueBits) ||
        !memory.Write32(state.r[13] + 0x60U, scalar.ContinuedValueBits)) {
      return false;
    }
    state.vfp[0] = scalar.ContinuedValueBits;
    // The payload returns at 0x0023A938, inside the original basic block.
    // Preserve that continuation through its next real AOT boundary.
    uint16_t modeBits = 0U;
    if (!memory.Read16(state.r[5] + 0x2AU, &modeBits))
      return false;
    const auto mode = static_cast<int32_t>(static_cast<int16_t>(modeBits));
    state.r[0] = static_cast<uint32_t>(mode);
    auto compareZero = [&](uint32_t value) {
      using namespace oot3d::recomp::a32;
      state.cpsr = (state.cpsr & ~(kFlagN | kFlagZ | kFlagC | kFlagV)) |
                   kFlagC | (value & kFlagN) | (value == 0U ? kFlagZ : 0U);
    };
    compareZero(state.r[0]);
    if (mode <= 0) {
      nextPc = kTopScreenCameraNormal1DefaultPath;
    } else {
      compareZero(state.r[9]);
      nextPc = state.r[9] == 0U ? kTopScreenCameraNormal1AlternatePath
                                : kTopScreenCameraNormal1DefaultPath;
    }
    state.r[15] = nextPc;
    *result = {oot3d::recomp::a32::ExitKind::Branch, nextPc,
               oot3d::recomp::a32::FallbackReason::None, pc};
    if (blocksConsumed != nullptr)
      *blocksConsumed = 1U;
    return true;
  }
  if (pc == kPauseItemsNativeUpdateCall) {
    uint32_t pageState = 0U;
    if (!memory.Read32(0x0050672CU, &pageState))
      return false;
    const auto plan = Oot3dNativeGame::ResolveTopScreenItemsSelectionBegin(
        {dispatch.TopScreenInput.ZrPressed, dispatch.TopScreenInput.ZlPressed,
         pageState});
    if (!plan.Active)
      return false;
    uint32_t actionFlags = 0U;
    if (!memory.Read32(0x0050AF18U, &actionFlags) ||
        !memory.Write32(0x0050AF18U, actionFlags | 0x400U)) {
      return false;
    }
    dispatch.PendingTopScreenItemsSelection = plan.Selection;
    state.r[14] = kPauseItemsNativeUpdateReturn;
    nextPc = kPauseItemsNativeUpdate;
    state.r[15] = nextPc;
    *result = {oot3d::recomp::a32::ExitKind::Branch, nextPc,
               oot3d::recomp::a32::FallbackReason::None, pc};
    if (blocksConsumed != nullptr)
      *blocksConsumed = 1U;
    return true;
  }
  if (pc == kPauseItemsNativeUpdateReturn &&
      dispatch.PendingTopScreenItemsSelection != 0U) {
    uint32_t actionFlags = 0U;
    uint32_t selectionState = 0U;
    if (!memory.Read32(0x0050AF18U, &actionFlags) ||
        !memory.Write32(0x0050AF18U, actionFlags & ~0x400U) ||
        !memory.Read32(0x0050674CU, &selectionState)) {
      return false;
    }
    uint32_t selection = 0U;
    uint16_t cursorY = 0U;
    if (Oot3dNativeGame::CompleteTopScreenItemsSelection(
            dispatch.PendingTopScreenItemsSelection, selectionState, &selection,
            &cursorY)) {
      constexpr std::array<uint32_t, 3> kCursorOffsets{0U, 8U, 0x10U};
      if (!memory.Write32(0x0050674CU, selection))
        return false;
      for (const auto offset : kCursorOffsets) {
        if (!memory.Write16(0x00506700U + offset, 0x010FU) ||
            !memory.Write16(0x00506702U + offset, cursorY)) {
          return false;
        }
      }
    }
    dispatch.PendingTopScreenItemsSelection = 0U;
    nextPc = pc + 4U; // the original return instruction is a NOP
    state.r[15] = nextPc;
    *result = {oot3d::recomp::a32::ExitKind::Branch, nextPc,
               oot3d::recomp::a32::FallbackReason::None, pc};
    if (blocksConsumed != nullptr)
      *blocksConsumed = 1U;
    return true;
  }
  if (pc == kGlobalActionStateGetSlotItemId) {
    ++dispatch.TopScreenSlotItemEntries;
    if (!Oot3dNativeGame::HasTopScreenSlotItemOverrideInput(
            dispatch.TopScreenInput)) {
      return false;
    }
    ++dispatch.TopScreenSlotItemAttempts;
    const uint8_t slot = static_cast<uint8_t>(state.r[1]);
    const auto resolved =
        Oot3dNativeGame::ResolveTopScreenSlotItemOverrideGuest(
            memory, state.r[0], slot, dispatch.TopScreenInput);
    if (resolved.has_value()) {
      ++dispatch.TopScreenSlotItemOverrides;
      state.r[0] = *resolved;
      nextPc = state.r[14];
      state.r[15] = nextPc;
      *result = {oot3d::recomp::a32::ExitKind::Branch, nextPc,
                 oot3d::recomp::a32::FallbackReason::None, pc};
      if (blocksConsumed != nullptr)
        *blocksConsumed = 1U;
      return true;
    }
    ++dispatch.TopScreenSlotItemNativeFallbacks;
    return false;
  }
  for (const auto &contract :
       Oot3dNativeGame::TopScreenVerifiedItemQueryContracts()) {
    if (contract.OriginalEntry != pc)
      continue;
    ++dispatch.TopScreenItemQueryEntries;
    if (!Oot3dNativeGame::HasTopScreenItemQueryOverrideInput(
            dispatch.TopScreenInput)) {
      return false;
    }
    const auto index = static_cast<std::size_t>(contract.Query);
    ++dispatch.TopScreenItemQueryCalls[index];
    const auto resolved = Oot3dNativeGame::ResolveTopScreenItemQueryGuest(
        memory, pc, dispatch.TopScreenInput);
    if (!resolved.has_value()) {
      return false;
    }
    dispatch.TopScreenItemQueryTrueResults[index] += *resolved ? 1U : 0U;
    // A press edge lives for one guest frame, but the game polls this getter
    // on its own schedule. Consume the latched edge only once the guest has
    // actually observed it as pressed.
    if (*resolved && contract.Query == Oot3dNativeGame::TopScreenItemQuery::ItemIPressed) {
      dispatch.TopScreenZrPressLatched = false;
      dispatch.TopScreenZrPressLatchFrames = 0U;
    }
    if (*resolved && contract.Query == Oot3dNativeGame::TopScreenItemQuery::ItemIIPressed) {
      dispatch.TopScreenZlPressLatched = false;
      dispatch.TopScreenZlPressLatchFrames = 0U;
    }
    state.r[0] = *resolved ? 1U : 0U;
    nextPc = state.r[14];
    state.r[15] = nextPc;
    *result = {oot3d::recomp::a32::ExitKind::Branch, nextPc,
               oot3d::recomp::a32::FallbackReason::None, pc};
    if (blocksConsumed != nullptr)
      *blocksConsumed = 1U;
    return true;
  }
  if (pc == kTopScreenGameplayCompositionPrefix) {
    uint32_t thresholdBits = 0U;
    uint32_t compositionBits = 0U;
    if (!memory.Read32(0x002E2A6CU, &thresholdBits))
      return false;
    state.vfp[3] = state.vfp[16];
    state.r[8] = state.r[4] + 0x7C00U;
    state.r[1] = thresholdBits;
    if (!memory.Read32(state.r[8] + 0x338U, &compositionBits)) {
      return false;
    }
    state.vfp[0] = compositionBits;
    state.vfp[2] = state.vfp[3];
    state.vfp[1] = state.vfp[3];
    if (!memory.Write32(state.r[13] + 0x1DCU, state.vfp[2]) ||
        !memory.Write32(state.r[13] + 0x1E0U, state.vfp[1]) ||
        !memory.Write32(state.r[13] + 0x1E4U, state.vfp[3]) ||
        !memory.Write32(state.r[13] + 0x1E8U, state.vfp[0])) {
      return false;
    }
    state.r[0] = compositionBits;
    const uint32_t subtraction = state.r[0] - state.r[1];
    using namespace oot3d::recomp::a32;
    state.cpsr = (state.cpsr & ~(kFlagN | kFlagZ | kFlagC | kFlagV)) |
                 (subtraction & kFlagN) | (subtraction == 0U ? kFlagZ : 0U) |
                 (state.r[0] >= state.r[1] ? kFlagC : 0U) |
                 ((((state.r[0] ^ state.r[1]) & (state.r[0] ^ subtraction)) &
                   kFlagN) != 0U
                      ? kFlagV
                      : 0U);
    if (!Oot3dNativeGame::ResolveTopScreenControlFlow(
            kTopScreenGameplayCompositionBranch, &nextPc)) {
      return false;
    }
    state.r[15] = nextPc;
    *result = {oot3d::recomp::a32::ExitKind::Branch, nextPc,
               oot3d::recomp::a32::FallbackReason::None, pc};
    if (blocksConsumed != nullptr)
      *blocksConsumed = 1U;
    return true;
  }
  if (Oot3dNativeGame::ResolveTopScreenControlFlow(pc, &nextPc)) {
    state.r[15] = nextPc;
    *result = {oot3d::recomp::a32::ExitKind::Branch, nextPc,
               oot3d::recomp::a32::FallbackReason::None, pc};
    if (blocksConsumed != nullptr)
      *blocksConsumed = 1U;
    return true;
  }
  uint8_t vfpLane = 0U;
  uint32_t valueBits = 0U;
  if (Oot3dNativeGame::ResolveTopScreenFloatLoad(pc, &vfpLane, &valueBits)) {
    state.vfp[vfpLane] = valueBits;
    nextPc = pc + 4U;
    state.r[15] = nextPc;
    *result = {oot3d::recomp::a32::ExitKind::Branch, nextPc,
               oot3d::recomp::a32::FallbackReason::None, pc};
    if (blocksConsumed != nullptr)
      *blocksConsumed = 1U;
    return true;
  }
  switch (pc) {
  case kPauseUiNativeUpdateCall: {
    uint32_t pauseState = 0U;
    if (!memory.Read32(0x0050AF68U, &pauseState))
      return false;
    if (Oot3dNativeGame::SuppressTopScreenTouchButtonDraw(pauseState)) {
      nextPc = pc + 4U;
    } else {
      state.r[14] = pc + 4U;
      nextPc = kPauseUiNativeUpdate;
    }
    break;
  }
  case kPauseProjectionPrepareCall:
    if (dispatch.TopScreenPauseProjection == nullptr ||
        !Oot3dNativeGame::ApplyTopScreenPauseProjection(
            memory, *dispatch.TopScreenPauseProjection, nullptr,
            dispatch.TopScreenConfig)) {
      return false;
    }
    state.r[14] = pc + 4U;
    nextPc = kPauseProjectionPrepare;
    break;
  case kPauseTouchButtonsDrawCall: {
    if (!Oot3dNativeGame::ApplyTopScreenNativeTouchQuad28Layout(memory)) {
      return false;
    }
    uint32_t pauseState = 0U;
    if (!memory.Read32(0x0050AF68U, &pauseState))
      return false;
    if (Oot3dNativeGame::SuppressTopScreenTouchButtonDraw(pauseState)) {
      nextPc = pc + 4U;
    } else {
      state.r[14] = pc + 4U;
      nextPc = state.r[1];
    }
    break;
  }
  case kPauseTouchButtonsTouchBranch:
    // IPS record 27 changes `tst r0, #8` to `tst r0, #0`; the following
    // native BEQ therefore always takes the no-touch continuation.
    state.cpsr = (state.cpsr & ~((1U << 31U) | (1U << 30U))) | (1U << 30U);
    nextPc = kPauseTouchButtonsNoTouchExit;
    break;
  case kPauseLowerScreenBlock:
    nextPc = kPauseLowerScreenBlockEnd;
    break;
  case kPauseDungeonMapCursorInput:
    nextPc = state.r[14];
    break;
  default:
    return false;
  }
  state.r[15] = nextPc;
  *result = {oot3d::recomp::a32::ExitKind::Branch, nextPc,
             oot3d::recomp::a32::FallbackReason::None, pc};
  if (blocksConsumed != nullptr)
    *blocksConsumed = 1U;
  return true;
}

void ApplyNativeWidescreenProjectionPolicy(
    uint32_t pc, oot3d::recomp::a32::GuestState &state,
    oot3d::recomp::a32::MemoryBus &memory, void *user);

bool ExecuteProductTopScreenCamera(uint32_t pc, oot3d::recomp::a32::GuestState &state,
                                   oot3d::recomp::a32::MemoryBus &,
                                   oot3d::recomp::a32::ExecutionResult *result,
                                   uint32_t blockBudget, uint32_t *blocksConsumed, void *user) {
  auto &dispatch = *static_cast<NativeCandidateDispatchState *>(user);
  // Product opt-in UI mod, not the experimental source/gameplay dispatcher.
  // Camera hooks plus the ZL/ZR item-slot getters; every other PC stays in
  // compiled code.
  const bool itemQueryEntry = std::any_of(
      Oot3dNativeGame::TopScreenVerifiedItemQueryContracts().begin(),
      Oot3dNativeGame::TopScreenVerifiedItemQueryContracts().end(),
      [pc](const Oot3dNativeGame::TopScreenItemQueryContract &contract) {
        return contract.OriginalEntry == pc;
      });
  if (!dispatch.TopScreenUiProfile || dispatch.Memory == nullptr ||
      (pc != kTopScreenCameraUpdateEntry && pc != kTopScreenCameraUpdatePatchSite &&
       pc != kTopScreenCameraNormal1Scalar && !itemQueryEntry &&
       pc != kGlobalActionStateGetSlotItemId)) return false;
  const bool handled = ExecuteTopScreenUiSourcePortBlock(pc, state, dispatch, result, blocksConsumed);
  if (handled) return true;
  // This entry was already observed before returning to the dispatcher. If
  // native ownership wins, resume it once instead of trapping the same entry.
  return Oot3dNativeGame::ExecuteOot3dCompiledFunction(
      pc, state, *dispatch.Memory, result, blockBudget, blocksConsumed,
      dispatch.BlockEntry, dispatch.BlockEntryUser,
      dispatch.BlockEntryPcs.data(), dispatch.BlockEntryPcs.size(), true, true, true);
}

bool ExecuteNativeCandidate(uint32_t pc, oot3d::recomp::a32::GuestState &state,
                            oot3d::recomp::a32::MemoryBus &memory,
                            oot3d::recomp::a32::ExecutionResult *result,
                            uint32_t blockBudget, uint32_t *blocksConsumed,
                            void *user) {
  auto &dispatch = *static_cast<NativeCandidateDispatchState *>(user);
  bool sample = false;
  if (dispatch.ProfileRuntime) {
    ++dispatch.Calls;
    dispatch.SampleState = dispatch.SampleState * 1664525U + 1013904223U;
    sample = (dispatch.SampleState & (kA32RuntimeSampleDenominator - 1U)) == 0U;
  }
  const auto sampleStart = sample ? std::chrono::steady_clock::now()
                                  : std::chrono::steady_clock::time_point{};
  const bool topScreenUi =
      dispatch.TopScreenUiProfile && dispatch.Memory != nullptr &&
      ExecuteTopScreenUiSourcePortBlock(pc, state, dispatch, result,
                                        blocksConsumed);
  if (topScreenUi) {
    ++dispatch.TopScreenSourcePortCalls;
    if (pc == kTopScreenGameplayCompositionPrefix) {
      ++dispatch.TopScreenLowerCompositionSkips;
      if (dispatch.PicaCompositionDomain != nullptr) {
        *dispatch.PicaCompositionDomain =
            Oot3dNativeGame::Oot3dPicaCompositionDomain::Ui;
      }
      if (dispatch.UiLifecycleBridge != nullptr) {
        dispatch.UiLifecycleBridge->ObserveGameplayComposition();
      }
    }
  }
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  constexpr bool sourceActorInitContext = false;
  constexpr bool sourceActorUpdateAll = false;
  constexpr bool sourceCutsceneUpdateFrame = false;
  constexpr bool sourceCutsceneProcessCommands = false;
  constexpr bool sourceCameraUpdate = false;
  constexpr bool sourcePlayerUpdate = false;
  constexpr bool sourcePlayerUpdateCommon = false;
  constexpr bool sourceOwner = false;
  constexpr bool sourceCsabCurve = false;
#else
  const bool sourceActorInitContext =
      !topScreenUi && dispatch.SourceActorInitContext != nullptr &&
      dispatch.SourceActorInitContext->Execute(
          pc, state, memory, result, blocksConsumed);
  const bool sourceActorUpdateAll =
      !topScreenUi && !sourceActorInitContext &&
      dispatch.SourceActorUpdateAll != nullptr &&
      dispatch.SourceActorUpdateAll->Execute(
          pc, state, memory, result, blocksConsumed);
  const bool sourceCutsceneUpdateFrame =
      !topScreenUi && !sourceActorInitContext && !sourceActorUpdateAll &&
      dispatch.SourceCutsceneUpdateFrame != nullptr &&
      dispatch.SourceCutsceneUpdateFrame->Execute(
          pc, state, memory, result, blocksConsumed);
  const bool sourceCutsceneProcessCommands =
      !topScreenUi && !sourceActorInitContext && !sourceActorUpdateAll &&
      !sourceCutsceneUpdateFrame &&
      dispatch.SourceCutsceneProcessCommands != nullptr &&
      dispatch.SourceCutsceneProcessCommands->Execute(
          pc, state, memory, result, blocksConsumed);
  const bool sourceCameraUpdate =
      !topScreenUi && !sourceActorInitContext && !sourceActorUpdateAll &&
      !sourceCutsceneUpdateFrame && !sourceCutsceneProcessCommands &&
      dispatch.SourceCameraUpdate != nullptr &&
      dispatch.SourceCameraUpdate->Execute(
          pc, state, memory, result, blocksConsumed);
  if (sourceCameraUpdate && result != nullptr &&
      result->kind == oot3d::recomp::a32::ExitKind::Unsupported) {
    std::cerr << "oot3d_native_game: source Camera_Update failed: "
              << dispatch.SourceCameraUpdate->LastError() << '\n';
  }
  const bool sourcePlayerUpdate =
      !topScreenUi && !sourceActorInitContext && !sourceActorUpdateAll &&
      !sourceCutsceneUpdateFrame && !sourceCutsceneProcessCommands &&
      !sourceCameraUpdate && dispatch.SourcePlayerUpdate != nullptr &&
      dispatch.SourcePlayerUpdate->Execute(
          pc, state, memory, result, blocksConsumed);
  if (sourcePlayerUpdate && result != nullptr &&
      result->kind == oot3d::recomp::a32::ExitKind::Unsupported) {
    std::cerr << "oot3d_native_game: source Player_Update failed: "
              << dispatch.SourcePlayerUpdate->LastError() << '\n';
  }
  const bool sourcePlayerUpdateCommon =
      !topScreenUi && !sourceActorInitContext && !sourceActorUpdateAll &&
      !sourceCutsceneUpdateFrame && !sourceCutsceneProcessCommands &&
      !sourceCameraUpdate && !sourcePlayerUpdate &&
      dispatch.SourcePlayerUpdateCommon != nullptr &&
      dispatch.SourcePlayerUpdateCommon->Execute(
          pc, state, memory, result, blocksConsumed);
  if (sourcePlayerUpdateCommon && result != nullptr &&
      result->kind == oot3d::recomp::a32::ExitKind::Unsupported) {
    std::cerr
        << "oot3d_native_game: source Player_UpdateCommon failed: "
        << dispatch.SourcePlayerUpdateCommon->LastError() << '\n';
  }
  const bool sourceOwner =
      sourceActorInitContext || sourceActorUpdateAll ||
      sourceCutsceneUpdateFrame || sourceCutsceneProcessCommands ||
      sourceCameraUpdate || sourcePlayerUpdate ||
      sourcePlayerUpdateCommon;
  const bool sourceCsabCurve =
      !topScreenUi && !sourceOwner &&
      dispatch.SourceCsabCurve != nullptr &&
      dispatch.SourceCsabCurve->Execute(
          pc, state, memory, result, blocksConsumed);
#endif
  const bool typedGameplay = !topScreenUi && !sourceOwner &&
                             !sourceCsabCurve &&
                             dispatch.TypedGameplay &&
                             dispatch.Memory != nullptr &&
                             Oot3dNativeGame::ExecuteOot3dTypedGameplay(
                                 pc, state, *dispatch.Memory, result,
                                 dispatch.TypedGameplayContext, blocksConsumed);
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  const bool wholeAotAllowed = dispatch.WholeAot;
#else
  const bool wholeAotAllowed =
      dispatch.WholeAot && !dispatch.TopScreenPauseAotBarrier;
#endif
  const bool compiled =
      !topScreenUi && !sourceOwner && !sourceCsabCurve &&
      !typedGameplay &&
      dispatch.CompiledFunctions &&
      dispatch.Memory != nullptr &&
      Oot3dNativeGame::ExecuteOot3dCompiledFunction(
          pc, state, *dispatch.Memory, result, blockBudget, blocksConsumed,
          dispatch.BlockEntry, dispatch.BlockEntryUser,
          dispatch.BlockEntryPcs.data(), dispatch.BlockEntryPcs.size(), true,
          dispatch.ManualCompiledFunctions, wholeAotAllowed);
  const bool massAot =
      !topScreenUi && !sourceOwner && !sourceCsabCurve &&
      !typedGameplay &&
      !compiled && dispatch.MassAot &&
      dispatch.Memory != nullptr &&
      Oot3dNativeGame::ExecuteOot3dMassAot(
          pc, state, *dispatch.Memory, result, blockBudget, blocksConsumed,
          dispatch.BlockEntry, dispatch.BlockEntryUser,
          dispatch.MassAotBlockEntryFilter,
          dispatch.MassAotObservableExitFilter, true);
  const bool trueAot = !topScreenUi && !sourceOwner &&
                       !sourceCsabCurve &&
                       !typedGameplay && !compiled && !massAot &&
                       dispatch.TrueAotBlocks &&
                       Oot3dNativeGame::ExecuteOot3dTrueAotBlock(
                           pc, state, memory, result, nullptr);
  if (sample) {
    const uint64_t nanoseconds = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - sampleStart)
            .count());
    ++dispatch.Samples;
    if (sourceActorInitContext) {
      ++dispatch.SourceActorInitContextSamples;
      dispatch.SourceActorInitContextSampleNanoseconds += nanoseconds;
    } else if (sourceActorUpdateAll) {
      ++dispatch.SourceActorUpdateAllSamples;
      dispatch.SourceActorUpdateAllSampleNanoseconds += nanoseconds;
    } else if (sourceCutsceneUpdateFrame) {
      ++dispatch.SourceCutsceneUpdateFrameSamples;
      dispatch.SourceCutsceneUpdateFrameSampleNanoseconds += nanoseconds;
    } else if (sourceCutsceneProcessCommands) {
      ++dispatch.SourceCutsceneProcessCommandsSamples;
      dispatch.SourceCutsceneProcessCommandsSampleNanoseconds += nanoseconds;
    } else if (sourceCameraUpdate) {
      ++dispatch.SourceCameraUpdateSamples;
      dispatch.SourceCameraUpdateSampleNanoseconds += nanoseconds;
    } else if (sourcePlayerUpdate) {
      ++dispatch.SourcePlayerUpdateSamples;
      dispatch.SourcePlayerUpdateSampleNanoseconds += nanoseconds;
    } else if (sourcePlayerUpdateCommon) {
      ++dispatch.SourcePlayerUpdateCommonSamples;
      dispatch.SourcePlayerUpdateCommonSampleNanoseconds +=
          nanoseconds;
    } else if (sourceCsabCurve) {
      ++dispatch.SourceCsabCurveSamples;
      dispatch.SourceCsabCurveSampleNanoseconds += nanoseconds;
    } else if (typedGameplay) {
      ++dispatch.TypedGameplaySamples;
      dispatch.TypedGameplaySampleNanoseconds += nanoseconds;
    } else if (massAot || compiled) {
      ++dispatch.CompiledSamples;
      dispatch.CompiledSampleNanoseconds += nanoseconds;
    } else if (trueAot) {
      ++dispatch.TrueAotSamples;
      dispatch.TrueAotSampleNanoseconds += nanoseconds;
    } else {
      ++dispatch.UnhandledSamples;
      dispatch.UnhandledSampleNanoseconds += nanoseconds;
    }
  }
  const bool handled =
      topScreenUi || sourceOwner || sourceCsabCurve || typedGameplay ||
      massAot || compiled || trueAot;
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  if (!handled) {
    if (result != nullptr) {
      *result = {
          oot3d::recomp::a32::ExitKind::Unsupported,
          pc,
          oot3d::recomp::a32::FallbackReason::Unsupported,
          0x57414F54U,
      };
    }
    if (blocksConsumed != nullptr) {
      *blocksConsumed = 1U;
    }
    return true;
  }
#endif
  return handled;
}

void ApplyNativeWidescreenProjectionPolicy(
    uint32_t pc, oot3d::recomp::a32::GuestState &state,
    oot3d::recomp::a32::MemoryBus &memory, void *user) {
  auto &runtime = *static_cast<NativeWidescreenProjectionState *>(user);
  const bool enabledTopScreenCameraExit =
      (pc != kTopScreenCameraUpdateEntry ||
       runtime.TopScreenConfig.FreeCameraEnabled) &&
      (pc != kTopScreenCameraNormal1Scalar ||
       runtime.TopScreenConfig.CameraZoomPercent != 100U);
  const bool itemQueryEntry = std::any_of(
      Oot3dNativeGame::TopScreenVerifiedItemQueryContracts().begin(),
      Oot3dNativeGame::TopScreenVerifiedItemQueryContracts().end(),
      [pc](const Oot3dNativeGame::TopScreenItemQueryContract &contract) {
        return contract.OriginalEntry == pc;
      });
  // Exit to the dispatcher only when the typed replacement will handle the
  // call. A declined hook cannot resume the compiled block in product mode,
  // so an input-only gate would spin on the same entry while ZL/ZR is held.
  const bool enabledTopScreenItemExit =
      runtime.TopScreenInput == nullptr ||
      runtime.TraceMemory == nullptr ||
      (!itemQueryEntry && pc != kGlobalActionStateGetSlotItemId) ||
      (itemQueryEntry &&
       Oot3dNativeGame::HasTopScreenItemQueryOverrideInput(
           *runtime.TopScreenInput) &&
       Oot3dNativeGame::ResolveTopScreenItemQueryGuest(
           *runtime.TraceMemory, pc, *runtime.TopScreenInput)
           .has_value()) ||
      (pc == kGlobalActionStateGetSlotItemId &&
       Oot3dNativeGame::HasTopScreenSlotItemOverrideInput(
           *runtime.TopScreenInput) &&
       Oot3dNativeGame::ResolveTopScreenSlotItemOverrideGuest(
           *runtime.TraceMemory, state.r[0],
           static_cast<uint8_t>(state.r[1]), *runtime.TopScreenInput)
           .has_value());
#if defined(OOT3D_NATIVE_DIRECT_AOT_PLUGIN)
  const bool wholeAotExecutionActive =
      Oot3dNativeGame::Oot3dWholeAotPluginExecutionActive();
#else
  const bool wholeAotExecutionActive =
      Oot3dNativeGame::Oot3dWholeAotExecutionActive();
#endif
  if (wholeAotExecutionActive &&
      enabledTopScreenCameraExit && enabledTopScreenItemExit &&
      std::binary_search(runtime.WholeAotObservableExitPcs.begin(),
                         runtime.WholeAotObservableExitPcs.end(), pc)) {
#if defined(OOT3D_NATIVE_DIRECT_AOT_PLUGIN)
    Oot3dNativeGame::Oot3dWholeAotPluginObservableExit(pc, state);
#else
    Oot3dNativeGame::Oot3dWholeAotExitAt(pc, state);
#endif
  }
  if (runtime.PlayerTemporalBridge != nullptr &&
      runtime.FrameRatePolicy != nullptr) {
    runtime.PlayerTemporalBridge->ObserveBlockEntry(
        pc, state, memory,
        runtime.FrameRatePolicy->GameplayClock().Context());
  }
  if (runtime.PicaCompositionTracker != nullptr) {
    runtime.PicaCompositionTracker->ObserveBlockEntry(pc, state, memory);
  }
  if (runtime.TraceA32Blocks) {
    ++runtime.BlockTraceRecordsObserved;
    if (runtime.BlockTrace.size() == kMaximumA32BlockTraceRecords) {
      runtime.BlockTrace.pop_front();
      runtime.BlockTraceTruncated = true;
    }
    NativeWidescreenProjectionState::A32BlockTraceRecord record;
    record.HostFrame = runtime.CurrentHostFrame;
    record.Pc = pc;
    record.MemoryWriteGeneration =
        runtime.TraceMemory != nullptr
            ? runtime.TraceMemory->WriteGeneration()
            : 0U;
    record.MemoryWriteFingerprint =
        runtime.TraceMemory != nullptr
            ? runtime.TraceMemory->WriteTraceFingerprint()
            : 0U;
    if (runtime.TraceMemory != nullptr) {
      record.FastReadMismatchCount =
          runtime.TraceMemory->FastReadMismatchCount();
      record.LastFastReadMismatchAddress =
          runtime.TraceMemory->LastFastReadMismatchAddress();
      record.LastFastReadMismatchValue =
          runtime.TraceMemory->LastFastReadMismatchValue();
      record.LastCheckedReadMismatchValue =
          runtime.TraceMemory->LastCheckedReadMismatchValue();
    }
    record.Registers = state.r;
    record.Cpsr = state.cpsr;
    record.Fpscr = state.fpscr;
    record.ThreadPointer = state.thread_pointer;
    record.Vfp = state.vfp;
    record.ExclusiveAddress = state.exclusive_address;
    record.ExclusiveToken = state.exclusive_token;
    record.ExclusiveSize = state.exclusive_size;
    record.ExclusiveValid = state.exclusive_valid;
    runtime.BlockTrace.push_back(std::move(record));
  }
  if (runtime.UiLifecycleBridge != nullptr) {
    const auto observation =
        runtime.UiLifecycleBridge->ObserveGuestEntry(pc);
    if (runtime.PicaCompositionDomain != nullptr &&
        Oot3dNativeGame::BeginsNativeGameplayUiPresentation(observation)) {
      *runtime.PicaCompositionDomain =
          Oot3dNativeGame::Oot3dPicaCompositionDomain::Ui;
    }
    if (runtime.TopScreenUiProfile) {
      runtime.UiLifecycleBridge->ApplyTopScreenGuestHook(pc, state.r[0]);
    }
  }
  if (runtime.TopScreenUiProfile && runtime.TraceMemory != nullptr &&
      runtime.UiLifecycleBridge != nullptr && pc == kPauseProjectionPrepare &&
      state.r[14] == kPauseProjectionPrepareCall + 4U) {
    const auto captureProjection = [&]() {
      nlohmann::json snapshot = nlohmann::json::object();
      uint32_t owner = 0U;
      uint32_t extents = 0U;
      uint32_t materialized = 0U;
      uint32_t offset = 0U;
      uint32_t extentA = 0U;
      uint32_t extentB = 0U;
      uint32_t materializedA = 0U;
      uint32_t materializedB = 0U;
      uint32_t offsetX = 0U;
      uint32_t offsetY = 0U;
      const bool rootAvailable =
          runtime.TraceMemory->Read32(0x004FDA84U, &owner);
      const bool ownerAvailable =
          rootAvailable && owner != 0U &&
          runtime.TraceMemory->Read32(owner + 0x0CU, &extents) &&
          runtime.TraceMemory->Read32(owner + 0x10U, &materialized) &&
          runtime.TraceMemory->Read32(owner + 0x1CU, &offset);
      const bool extentsAvailable =
          ownerAvailable && extents != 0U &&
          runtime.TraceMemory->Read32(extents, &extentA) &&
          runtime.TraceMemory->Read32(extents + 0x24U, &extentB);
      const bool materializedAvailable =
          ownerAvailable && materialized != 0U &&
          runtime.TraceMemory->Read32(materialized, &materializedA) &&
          runtime.TraceMemory->Read32(materialized + 0x24U, &materializedB);
      const bool offsetAvailable =
          ownerAvailable && offset != 0U &&
          runtime.TraceMemory->Read32(offset, &offsetX) &&
          runtime.TraceMemory->Read32(offset + 4U, &offsetY);
      snapshot = {
          {"owner", owner},
          {"extents", extents},
          {"materialized", materialized},
          {"offset", offset},
          {"extent_a_bits", extentsAvailable ? extentA : 0U},
          {"extent_b_bits", extentsAvailable ? extentB : 0U},
          {"materialized_a_bits", materializedAvailable ? materializedA : 0U},
          {"materialized_b_bits", materializedAvailable ? materializedB : 0U},
          {"offset_x_bits", offsetAvailable ? offsetX : 0U},
          {"offset_y_bits", offsetAvailable ? offsetY : 0U},
      };
      return snapshot;
    };
    nlohmann::json before = nullptr;
    if (runtime.CollectExtendedDiagnostics &&
        runtime.TopScreenPauseProjectionEvents.size() < 64U) {
      before = captureProjection();
    }
    ++runtime.TopScreenPauseProjectionCalls;
    std::string projectionError;
    const bool applied = Oot3dNativeGame::ApplyTopScreenPauseProjection(
        *runtime.TraceMemory,
        runtime.UiLifecycleBridge->TopScreenPauseProjection(),
        &projectionError, &runtime.TopScreenConfig);
    if (!applied) {
      ++runtime.TopScreenPauseProjectionFailures;
    }
    if (runtime.CollectExtendedDiagnostics &&
        runtime.TopScreenPauseProjectionEvents.size() < 64U) {
      runtime.TopScreenPauseProjectionEvents.push_back(
          {{"host_frame", runtime.CurrentHostFrame},
           {"return_address", state.r[14]},
           {"applied", applied},
           {"error", projectionError},
           {"before", std::move(before)},
           {"after", captureProjection()}});
    }
  }
  constexpr uint32_t kPauseRendererSetTarget = 0x00300588U;
  constexpr uint32_t kGlViewport = 0x002FEABCU;
  constexpr uint32_t kGlBindFramebuffer = 0x00311364U;
  if (runtime.TopScreenUiProfile && runtime.TraceMemory != nullptr) {
    if (pc == kPauseIconBuild) {
      Oot3dNativeGame::TopScreenPauseIconBuild build{state.r[2], state.r[3],
                                                     0U, 0U};
      if (runtime.TraceMemory->IsWritable(state.r[13], 8U) &&
          runtime.TraceMemory->Read32(state.r[13], &build.StackArgument0) &&
          runtime.TraceMemory->Read32(state.r[13] + 4U,
                                      &build.StackArgument1) &&
          Oot3dNativeGame::ResolveTopScreenPauseIconBuild(
              &build, runtime.TopScreenConfig)) {
        state.r[2] = build.Argument2;
        state.r[3] = build.Argument3;
        runtime.TraceMemory->Write32(state.r[13], build.StackArgument0);
        runtime.TraceMemory->Write32(state.r[13] + 4U, build.StackArgument1);
      }
    } else if (pc == kPauseRendererSetTarget) {
      runtime.PendingTopScreenPauseTarget.reset();
      runtime.PendingTopScreenPauseRenderer = 0U;
      uint8_t enabled = 0U;
      uint8_t halfHeight = 0U;
      if (state.r[0] != 0U &&
          runtime.TraceMemory->Read8(state.r[0] + 4U, &enabled) &&
          runtime.TraceMemory->Read8(state.r[0] + 0x75U, &halfHeight)) {
        Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
        bool routeActive = false;
        if (Oot3dNativeGame::ReadTopScreenPauseDrawInputs(*runtime.TraceMemory,
                                                          &inputs) &&
            runtime.TopScreenPauseTargetRoute != nullptr) {
          ++runtime.TopScreenPauseTargetObservations;
          routeActive = Oot3dNativeGame::ResolveTopScreenPauseRouteActive(
              inputs, runtime.TopScreenPauseTargetRoute);
          routeActive = routeActive || runtime.TopScreenPausePageRedrawActive;
          runtime.HasTopScreenPauseDrawInputs = true;
          runtime.LastTopScreenPauseDrawInputs = inputs;
          runtime.LastTopScreenPauseRouteActive = routeActive;
          Oot3dNativeGame::ObserveTopScreenPauseTargetCommand(
              inputs, state.r[1], &runtime.TopScreenPauseDrawRouting);
        }
        const Oot3dNativeGame::TopScreenPauseTargetCommand command{
            enabled != 0U, state.r[1], halfHeight != 0U};
        auto plan =
            runtime.TopScreenPausePageRedrawActive
                ? Oot3dNativeGame::ResolveTopScreenPausePageRedrawTargetCommand(
                      command)
                : Oot3dNativeGame::ResolveTopScreenPauseTargetCommand(
                      command, routeActive);
        const auto frontendPlan =
            Oot3dNativeGame::ResolveTopScreenFrontendTargetCommand(
                command,
                runtime.UiLifecycleBridge != nullptr &&
                    runtime.UiLifecycleBridge->NativeFrontendPresentationActive());
        if (frontendPlan.Handled) {
          plan = frontendPlan;
        }
        if (plan.Handled && plan.BindTopTarget) {
          runtime.PendingTopScreenPauseTarget = plan;
          runtime.PendingTopScreenPauseRenderer = state.r[0];
          ++runtime.TopScreenPauseTargetCommands;
        }
      }
    } else if (pc == kGlBindFramebuffer &&
               runtime.PendingTopScreenPauseTarget.has_value()) {
      uint32_t framebuffer = 0U;
      const auto &plan = *runtime.PendingTopScreenPauseTarget;
      if (runtime.TraceMemory->Read32(runtime.PendingTopScreenPauseRenderer +
                                          plan.FramebufferBindingOffset,
                                      &framebuffer)) {
        state.r[1] = framebuffer;
      }
    } else if (pc == kGlViewport &&
               runtime.PendingTopScreenPauseTarget.has_value()) {
      const auto plan = *runtime.PendingTopScreenPauseTarget;
      state.r[0] = plan.ViewportX;
      state.r[1] = plan.ViewportY;
      state.r[2] = plan.ViewportWidth;
      state.r[3] = plan.ViewportHeight;
      runtime.PendingTopScreenPauseTarget.reset();
      runtime.PendingTopScreenPauseRenderer = 0U;
      ++runtime.TopScreenPauseTargetRewrites;
    } else if (pc == kPauseUiDraw && state.r[14] == kPauseUiDrawReturn &&
               !runtime.PendingTopScreenPauseChildRestore.has_value()) {
      Oot3dNativeGame::TopScreenPauseDrawInputs inputs;
      if (Oot3dNativeGame::ReadTopScreenPauseDrawInputs(*runtime.TraceMemory,
                                                        &inputs)) {
        runtime.HasTopScreenPauseDrawInputs = true;
        runtime.LastTopScreenPauseDrawInputs = inputs;
        const auto action = Oot3dNativeGame::ResolveTopScreenPauseDrawAction(
            inputs, &runtime.TopScreenPauseDrawRouting);
        runtime.LastTopScreenPauseChildrenSuppressed =
            action ==
            Oot3dNativeGame::TopScreenPauseDrawAction::SuppressNativeChildren;
        if (!runtime.LastTopScreenPauseChildrenSuppressed) {
          runtime.TopScreenPausePageRedrawActive = false;
          if (runtime.UiLifecycleBridge != nullptr) {
            runtime.UiLifecycleBridge
                ->SetTopScreenPausePageRedrawEdgePresentation(std::nullopt);
          }
          ++runtime.TopScreenPauseChildNativeDraws;
        }
        if (runtime.LastTopScreenPauseChildrenSuppressed) {
          ++runtime.TopScreenPausePageCompositionCalls;
          Oot3dNativeGame::TopScreenPausePageRedrawInputs pageInputs;
          const bool runtimeSceneLatch =
              runtime.TopScreenTouchCoordinateRoute != nullptr &&
              runtime.TopScreenTouchCoordinateRoute->RuntimeSceneLatch;
          const bool pageRedraw =
              Oot3dNativeGame::ReadTopScreenPausePageRedrawInputs(
                  *runtime.TraceMemory, runtimeSceneLatch, &pageInputs) &&
              Oot3dNativeGame::ResolveTopScreenPausePageRedraw(
                  pageInputs, &runtime.TopScreenPausePageRedraw);
          runtime.TopScreenPausePageRedrawActive = pageRedraw;
          if (runtime.UiLifecycleBridge != nullptr) {
            runtime.UiLifecycleBridge
                ->SetTopScreenPausePageRedrawEdgePresentation(
                    pageRedraw
                        ? std::optional(
                              Oot3dNativeGame::BuildTopScreenPauseEdgeGeometry(
                                  pageInputs.Pause.HasScene,
                                  pageInputs.Pause.SceneMode,
                                  pageInputs.Pause.RuntimeMode, 0U, 0, 1.0F,
                                  1.0F))
                        : std::nullopt);
          }
          const auto suppressionSet =
              pageRedraw
                  ? Oot3dNativeGame::TopScreenPauseChildSuppressionSet::
                        PageRedraw
                  : Oot3dNativeGame::TopScreenPauseChildSuppressionSet::Tail;
          Oot3dNativeGame::TopScreenPauseChildState saved;
          if (Oot3dNativeGame::BeginTopScreenPauseChildSuppression(
                  *runtime.TraceMemory, suppressionSet, &saved)) {
            runtime.PendingTopScreenPauseChildRestore = saved;
            ++runtime.TopScreenPauseChildSuppressions;
            if (pageRedraw) {
              ++runtime.TopScreenPausePageRedraws;
            } else {
              ++runtime.TopScreenPausePageRedrawSkips;
            }
          }
        }
      } else if (runtime.UiLifecycleBridge != nullptr) {
        runtime.UiLifecycleBridge->SetTopScreenPausePageRedrawEdgePresentation(
            std::nullopt);
      }
    } else if (pc == kPauseUiDrawReturn &&
               runtime.PendingTopScreenPauseChildRestore.has_value()) {
      Oot3dNativeGame::EndTopScreenPauseChildSuppression(
          *runtime.TraceMemory, *runtime.PendingTopScreenPauseChildRestore);
      runtime.PendingTopScreenPauseChildRestore.reset();
    }
  }
  if (pc == Oot3dNativeGame::kOot3dFramePacingBeginEntry &&
      runtime.FrameRatePolicy != nullptr) {
    runtime.FrameRatePolicy->BeginFramePacing(state.r[0]);
  }
  if (pc == Oot3dNativeGame::kOot3dPlayerUpdateEntry) {
    if (runtime.PlayerTimingProbe != nullptr) {
      runtime.PlayerTimingProbe->ObserveUpdateEntry(state);
    }
    if (runtime.SceneViewProbe != nullptr) {
      runtime.SceneViewProbe->ObservePlayState(state.r[1]);
    }
    if (runtime.CollectExtendedDiagnostics &&
        runtime.PlayerTimingProbe != nullptr) {
      constexpr size_t kMaximumPlayerTimingEvents = 4096U;
      const auto snapshot = runtime.PlayerTimingProbe->Capture(memory);
      if (snapshot.has_value() &&
          runtime.TemporalEventLedger != nullptr &&
          runtime.FrameRatePolicy != nullptr) {
        runtime.TemporalEventLedger->ObservePlayer(
            runtime.FrameRatePolicy->GameplayClock().Context(), *snapshot);
      }
      if (snapshot.has_value() &&
          runtime.PlayerTimingEvents.size() < kMaximumPlayerTimingEvents) {
        runtime.PlayerTimingEvents.push_back({
            {"host_frame", runtime.CurrentHostFrame},
            {"world_position",
             {snapshot->WorldX, snapshot->WorldY, snapshot->WorldZ}},
            {"velocity",
             {snapshot->VelocityX, snapshot->VelocityY, snapshot->VelocityZ}},
            {"actor_speed", snapshot->ActorSpeed},
            {"player_speed", snapshot->PlayerSpeed},
            {"floor_height", snapshot->FloorHeight},
            {"underwater_timer", snapshot->UnderwaterTimer},
            {"melee_weapon_action_timer",
             snapshot->MeleeWeaponActionTimer},
            {"melee_weapon_combo_state",
             snapshot->MeleeWeaponComboState},
            {"item_action_state_or_burn_timer",
             snapshot->ItemActionStateOrBurnTimer},
            {"ledge_climb_type", snapshot->LedgeClimbType},
            {"ledge_climb_delay_timer",
             snapshot->LedgeClimbDelayTimer},
            {"textbox_button_cooldown_timer",
             snapshot->TextboxButtonCooldownTimer},
            {"damage_flicker_animation_counter",
             snapshot->DamageFlickerAnimationCounter},
            {"damage_run_timer", snapshot->DamageRunTimer},
            {"item_action_cooldown_timer",
             snapshot->ItemActionCooldownTimer},
            {"collision_sfx_cooldown_timer",
             snapshot->CollisionSfxCooldownTimer},
            {"invincibility_timer", snapshot->InvincibilityTimer},
            {"floor_type_timer", snapshot->FloorTypeTimer},
            {"previous_floor_type", snapshot->PreviousFloorType},
            {"respawn_damage_state", snapshot->RespawnDamageState},
            {"random_turn_state", snapshot->RandomTurnState},
            {"random_turn_timer", snapshot->RandomTurnTimer},
            {"attention_persistence_counter",
             snapshot->AttentionPersistenceCounter},
            {"fairy_revive_grace_timer",
             snapshot->FairyReviveGraceTimer},
            {"background_check_flags", snapshot->BackgroundCheckFlags},
            {"action_function", snapshot->ActionFunction},
            {"animation_resource", snapshot->AnimationResource},
            {"animation_frame", snapshot->AnimationFrame},
            {"animation_play_speed", snapshot->AnimationPlaySpeed},
            {"animation_mode", snapshot->AnimationMode},
            {"time_state_address", snapshot->TimeStateAddress},
            {"time_state_update_rate", snapshot->TimeStateUpdateRate},
        });
      } else if (snapshot.has_value()) {
        runtime.PlayerTimingEventsTruncated = true;
      }
    }
  }
  if (pc == kGameStateUpdate && runtime.FrameRatePolicy != nullptr &&
      !runtime.FrameRatePolicy->ApplyNativeTimeScaleAtGameStateUpdate(
          state.r[0], memory)) {
    throw std::runtime_error("native frame-rate policy failed: " +
                             runtime.FrameRatePolicy->LastError());
  }
  if (pc == Oot3dNativeGame::kOot3dFramePacingCommitEntry &&
      runtime.FrameRatePolicy != nullptr &&
      !runtime.FrameRatePolicy->ApplyFramePacingInterval(state.r[4], memory)) {
    throw std::runtime_error("native frame-pacing policy failed: " +
                             runtime.FrameRatePolicy->LastError());
  }
  if (pc == Oot3dNativeGame::kOot3dFramePacingDecisionEntry &&
      runtime.FrameRatePolicy != nullptr &&
      !runtime.FrameRatePolicy->ApplyFramePacingDeadline(state.r[4], memory)) {
    throw std::runtime_error("native frame-pacing deadline policy failed: " +
                             runtime.FrameRatePolicy->LastError());
  }
  if (runtime.ProfileA32Blocks) {
    ++runtime.BlockEntries;
    runtime.BlockSampleState =
        runtime.BlockSampleState * 1664525U + 1013904223U;
    if ((runtime.BlockSampleState & 63U) == 0U) {
      ++runtime.BlockSamples[pc];
    }
  }
  if (runtime.CollectExtendedDiagnostics &&
      (pc == kGameStateUpdate || pc == kFileChooseInit ||
       pc == kPlayTransitionUpdate || pc == kFileSelectActivate)) {
    const auto read32 = [&](uint32_t address) {
      uint32_t value = 0;
      memory.Read32(address, &value);
      return value;
    };
    if (pc == kFileSelectActivate) {
      runtime.GameStateEvents.push_back({
          {"host_frame", runtime.CurrentHostFrame},
          {"function", "FileSelect_Activate"},
          {"argument", state.r[0]},
          {"controller_state_before", read32(kFileSelectState + 0x10U)},
          {"return_address", state.r[14]},
      });
      if (runtime.GameStateEvents.size() > 128U) {
        runtime.GameStateEvents.erase(runtime.GameStateEvents.begin());
      }
      return;
    }
    const uint32_t gameState = state.r[0];
    const auto read8 = [&](uint32_t address) {
      uint8_t value = 0;
      memory.Read8(address, &value);
      return value;
    };
    const uint32_t main = read32(gameState + 0x04U);
    const uint32_t destroy = read32(gameState + 0x08U);
    const uint32_t nextInit = read32(gameState + 0x0CU);
    const uint32_t nextSize = read32(gameState + 0x10U);
    const uint8_t running = read8(gameState + 0x101U);
    const uint32_t nativeGameMode = read32(kNativeGameMode);
    const uint8_t transitionTrigger = read8(gameState + 0x5C2DU);
    const uint8_t transitionType = read8(gameState + 0x5C76U);
    const uint8_t transitionState = read8(gameState + 0x7F12U);
    const bool changed = !runtime.HasGameStateSnapshot ||
                         gameState != runtime.LastGameStateAddress ||
                         main != runtime.LastGameStateMain ||
                         nextInit != runtime.LastNextGameStateInit ||
                         running != runtime.LastGameStateRunning ||
                         nativeGameMode != runtime.LastNativeGameMode ||
                         transitionTrigger != runtime.LastTransitionTrigger ||
                         transitionType != runtime.LastTransitionType ||
                         transitionState != runtime.LastTransitionState;
    if (pc == kFileChooseInit || changed) {
      const char *function = pc == kGameStateUpdate  ? "GameState_Update"
                             : pc == kFileChooseInit ? "FileChoose_Init"
                                                     : "PlayTransition_Update";
      runtime.GameStateEvents.push_back({
          {"host_frame", runtime.CurrentHostFrame},
          {"function", function},
          {"state", gameState},
          {"main", main},
          {"destroy", destroy},
          {"next_init", nextInit},
          {"next_size", nextSize},
          {"running", running},
          {"native_game_mode", nativeGameMode},
          {"transition_trigger", transitionTrigger},
          {"transition_type", transitionType},
          {"transition_state", transitionState},
          {"return_address", state.r[14]},
      });
      if (runtime.GameStateEvents.size() > 128U) {
        runtime.GameStateEvents.erase(runtime.GameStateEvents.begin());
      }
    }
    runtime.GameStateUpdateCalls += pc == kGameStateUpdate ? 1U : 0U;
    runtime.LastGameStateAddress = gameState;
    runtime.LastGameStateMain = main;
    runtime.LastNextGameStateInit = nextInit;
    runtime.LastGameStateRunning = running;
    runtime.LastNativeGameMode = nativeGameMode;
    runtime.LastTransitionTrigger = transitionTrigger;
    runtime.LastTransitionType = transitionType;
    runtime.LastTransitionState = transitionState;
    runtime.HasGameStateSnapshot = true;
  }
  if (runtime.CollectExtendedDiagnostics &&
      (pc == kPauseUiUpdate || pc == kPauseUiDraw || pc == kFileSelectUpdate ||
       pc == kFileSelectInit)) {
    const char *function = pc == kPauseUiUpdate      ? "PauseUi_Update"
                           : pc == kPauseUiDraw      ? "PauseUi_Draw"
                           : pc == kFileSelectUpdate ? "FileSelect_Update"
                                                     : "FileSelect_Init";
    ++runtime.UiLifecycleCallCounts[function];
    runtime.UiLifecycleEvents.push_back({
        {"host_frame", runtime.CurrentHostFrame},
        {"function", function},
        {"argument_0", state.r[0]},
        {"argument_1", state.r[1]},
        {"return_address", state.r[14]},
        {"thread_pointer", state.thread_pointer},
    });
    if (runtime.UiLifecycleEvents.size() > 256U) {
      runtime.UiLifecycleEvents.erase(runtime.UiLifecycleEvents.begin());
    }
    return;
  }
  if (runtime.CollectExtendedDiagnostics &&
      (pc == kBlockingQueuePop || pc == kBlockingQueuePush ||
       pc == kRendererCommandQueueProcessPending ||
       pc == kBlockingQueueTryPush || pc == kTaskQueueTakeFirstFromPriority ||
       pc == kTaskQueueEnqueuePriorityNode ||
       pc == kTaskQueueWorkerDrainPriorityNodes || pc == kNngxP3dCallback ||
       pc == kNngxCommonInterruptHandler ||
       pc == kNngxCmdlistCompletionDispatch ||
       pc == kRendererGlobalCallbackAfterLookup)) {
    const char *function =
        pc == kBlockingQueuePop    ? "BlockingQueue_Pop"
        : pc == kBlockingQueuePush ? "BlockingQueue_Push"
        : pc == kRendererCommandQueueProcessPending
            ? "RendererCommandQueue_ProcessPending"
        : pc == kBlockingQueueTryPush ? "BlockingQueue_TryPush"
        : pc == kTaskQueueTakeFirstFromPriority
            ? "TaskQueue_TakeFirstFromPriority"
        : pc == kTaskQueueEnqueuePriorityNode ? "TaskQueue_EnqueuePriorityNode"
        : pc == kTaskQueueWorkerDrainPriorityNodes
            ? "TaskQueueWorker_DrainPriorityNodes"
        : pc == kNngxP3dCallback            ? "nngx_P3DCallback"
        : pc == kNngxCommonInterruptHandler ? "nngx_CommonInterruptHandler"
        : pc == kNngxCmdlistCompletionDispatch
            ? "nngx_CmdlistCompletionDispatch"
            : "RendererGlobalCallback_AfterLookup";
    nlohmann::json argumentWords = nlohmann::json::array();
    if (pc == kTaskQueueEnqueuePriorityNode) {
      for (uint32_t offset = 0; offset < 0x20U; offset += 4U) {
        uint32_t value = 0;
        if (!memory.Read32(state.r[1] + offset, &value)) {
          break;
        }
        argumentWords.push_back(value);
      }
    }
    uint32_t queue = state.r[0];
    uint32_t argument = state.r[1];
    if (pc == kNngxP3dCallback || pc == kNngxCommonInterruptHandler) {
      uint32_t commandList = 0;
      uint32_t callback = 0;
      memory.Read32(kNngxGlobalState + 0xA0U, &commandList);
      if (commandList != 0U) {
        memory.Read32(commandList + 0x30U, &callback);
        for (uint32_t offset = 0; offset < 0x38U; offset += 4U) {
          uint32_t value = 0;
          if (!memory.Read32(commandList + offset, &value)) {
            break;
          }
          argumentWords.push_back(value);
        }
      }
      queue = commandList;
      argument = callback;
    } else if (pc == kNngxCmdlistCompletionDispatch) {
      uint32_t owner = 0;
      uint32_t vtable = 0;
      uint32_t callback = 0;
      memory.Read32(kNngxCmdlistOwnerState + 0x14U, &owner);
      if (owner != 0U && memory.Read32(owner, &vtable)) {
        memory.Read32(vtable + 0x1CU, &callback);
        for (uint32_t offset = 0; offset < 0x30U; offset += 4U) {
          uint32_t value = 0;
          if (!memory.Read32(owner + offset, &value)) {
            break;
          }
          argumentWords.push_back(value);
        }
      }
      queue = owner;
      argument = callback;
    } else if (pc == kRendererGlobalCallbackAfterLookup) {
      uint32_t callback = 0;
      uint32_t callbackArgument = 0;
      if (state.r[0] != 0U) {
        memory.Read32(state.r[0] + 0x10U, &callback);
        memory.Read32(state.r[0] + 0x14U, &callbackArgument);
      }
      queue = state.r[0];
      argument = callback;
      argumentWords.push_back(callbackArgument);
    }
    runtime.RendererQueueEvents.push_back({
        {"host_frame", runtime.CurrentHostFrame},
        {"function", function},
        {"thread_pointer", state.thread_pointer},
        {"queue", queue},
        {"argument", argument},
        {"return_address", state.r[14]},
        {"argument_words", std::move(argumentWords)},
    });
    ++runtime.RendererQueueCallCounts[function];
    if (runtime.RendererQueueEvents.size() > 512U) {
      runtime.RendererQueueEvents.erase(runtime.RendererQueueEvents.begin());
    }
    return;
  }
  if (pc == kEnMagInit || pc == kEnMagUpdate || pc == kEnMagDraw) {
    uint32_t &callCount = pc == kEnMagInit     ? runtime.EnMagInitCalls
                          : pc == kEnMagUpdate ? runtime.EnMagUpdateCalls
                                               : runtime.EnMagDrawCalls;
    ++callCount;

    const uint32_t actor = state.r[0];
    const auto read16 = [&](uint32_t offset) {
      uint16_t value = 0;
      return memory.Read16(actor + offset, &value) ? value : 0xFFFFU;
    };
    const auto read32 = [&](uint32_t offset) {
      uint32_t value = 0;
      return memory.Read32(actor + offset, &value) ? value : 0xFFFFFFFFU;
    };
    const auto readFloat = [&](uint32_t offset) {
      return std::bit_cast<float>(read32(offset));
    };
    nlohmann::json event{
        {"host_frame", runtime.CurrentHostFrame},
        {"function", pc == kEnMagInit     ? "EnMag_Init"
                     : pc == kEnMagUpdate ? "EnMag_Update"
                                          : "EnMag_Draw"},
        {"call", callCount},
        {"actor", actor},
        {"handle_main", read32(0x1A4U)},
        {"handle_title", read32(0x1A8U)},
        {"handle_copyright", read32(0x1ACU)},
        {"delay_timer", read16(0x1C0U)},
        {"substate", read16(0x1C4U)},
        {"timer", read16(0x1C6U)},
        {"state", read16(0x1C8U)},
        {"title_alpha", readFloat(0x1D0U)},
        {"main_alpha", readFloat(0x1D4U)},
        {"copyright_alpha", readFloat(0x1D8U)},
        {"effect_alpha", readFloat(0x1DCU)},
    };
    if (pc == kEnMagDraw) {
      nlohmann::json queueWords = nlohmann::json::array();
      for (uint32_t offset = 0; offset < 0x80U; offset += 4U) {
        uint32_t value = 0;
        if (!memory.Read32(kRendererCommandQueue + offset, &value)) {
          break;
        }
        queueWords.push_back(value);
      }
      event["renderer_command_queue_address"] = kRendererCommandQueue;
      event["renderer_command_queue_words"] = std::move(queueWords);
    }
    if (runtime.EnMagEvents.size() == 256U) {
      runtime.EnMagEvents.erase(runtime.EnMagEvents.begin());
    }
    runtime.EnMagEvents.push_back(std::move(event));
    return;
  }
  if (pc != kMtx4x4BuildFrustum &&
      pc != kMtx4x4BuildOrthographicProjectionRotated) {
    return;
  }
  auto &projection = runtime.Projection;
  const float left = std::bit_cast<float>(state.vfp[0]);
  const float right = std::bit_cast<float>(state.vfp[1]);
  if (!std::isfinite(left) || !std::isfinite(right) || left == right) {
    return;
  }
  auto &callsite = projection.Callsites[{pc, state.r[14]}];
  if (callsite.Calls == 0) {
    callsite.FirstLeft = left;
    callsite.FirstRight = right;
  }
  ++callsite.Calls;
  if (pc == kMtx4x4BuildOrthographicProjectionRotated) {
    ++projection.OrthographicCalls;
    return;
  }
  const float bottom = std::bit_cast<float>(state.vfp[2]);
  const float top = std::bit_cast<float>(state.vfp[3]);
  const float nearPlane = std::bit_cast<float>(state.vfp[4]);
  const float farPlane = std::bit_cast<float>(state.vfp[5]);
  const auto resolved = Fast::Oot3d::ResolvePerspectiveFov({
      {left, right, bottom, top, nearPlane, farPlane},
      projection.FovMultiplier,
      projection.Presentation,
      true,
  });
  const float resolvedLeft = resolved.Frustum.Left;
  const float resolvedRight = resolved.Frustum.Right;
  if (resolvedLeft != left || resolvedRight != right ||
      resolved.Frustum.Bottom != bottom || resolved.Frustum.Top != top) {
    state.vfp[0] = std::bit_cast<uint32_t>(resolvedLeft);
    state.vfp[1] = std::bit_cast<uint32_t>(resolvedRight);
    state.vfp[2] = std::bit_cast<uint32_t>(resolved.Frustum.Bottom);
    state.vfp[3] = std::bit_cast<uint32_t>(resolved.Frustum.Top);
    ++projection.FrustumCalls;
  }
  if (runtime.SceneViewProbe != nullptr) {
    runtime.SceneViewProbe->ObservePerspective(
        pc, state.r[14], resolvedLeft, resolvedRight,
        resolved.Frustum.Bottom, resolved.Frustum.Top,
        resolved.Frustum.NearPlane, resolved.Frustum.FarPlane);
  }
}

Oot3dNativeGame::NativeA32ProcessRunResult
RunUntilGuestWait(Oot3dNativeGame::NativeA32Process &process,
                  uint32_t wholeAotBlockBudget) {
  // A block-limit exit unwinds the native whole-AOT call chain. The caller
  // resumes it immediately: this is a stack-safety boundary, not an SDL event
  // pump or guest scheduling boundary.
  auto result = process.Run(wholeAotBlockBudget);
  while (result.Kind == Oot3dNativeGame::NativeA32ProcessRunKind::Yielded) {
    result = process.Run(wholeAotBlockBudget);
  }
  return result;
}

void RequireRunnableGuest(
    const Oot3dNativeGame::NativeA32ProcessRunResult &result) {
  if (result.Kind == Oot3dNativeGame::NativeA32ProcessRunKind::Faulted) {
    throw std::runtime_error("native A32 process faulted: " + result.Error);
  }
}

Oot3dNativeGame::NativeGuestClockDeadlineResolution AdvanceGuestClockTo(
    Oot3dNativeGame::NativeA32Process &process,
    Oot3dNativeGame::NativeA32CtrHostServices &hostServices,
    Oot3dNativeGame::NativeA32ProcessRunResult &processResult,
    uint64_t deadline, uint32_t wholeAotBlockBudget) {
  auto resolution = Oot3dNativeGame::ResolveNativeGuestClockDeadline(
      hostServices.SystemTicks(), deadline);
  if (resolution.OvershootTicks != 0U) {
    return resolution;
  }
  while (processResult.Kind !=
         Oot3dNativeGame::NativeA32ProcessRunKind::Terminated) {
    const auto wakeTick = hostServices.NextSleepWakeTick();
    if (!wakeTick.has_value() || *wakeTick > deadline) {
      break;
    }
    const uint64_t currentTick = hostServices.SystemTicks();
    hostServices.AdvanceSystemTicks(
        *wakeTick > currentTick ? *wakeTick - currentTick : 0U);
    processResult = RunUntilGuestWait(process, wholeAotBlockBudget);
    RequireRunnableGuest(processResult);
    resolution = Oot3dNativeGame::ResolveNativeGuestClockDeadline(
        hostServices.SystemTicks(), deadline);
    if (resolution.OvershootTicks != 0U) {
      return resolution;
    }
  }
  resolution = Oot3dNativeGame::ResolveNativeGuestClockDeadline(
      hostServices.SystemTicks(), deadline);
  if (resolution.AdvanceTicks != 0U) {
    hostServices.AdvanceSystemTicks(resolution.AdvanceTicks);
  }
  return resolution;
}

std::vector<Oot3dNativeGame::Oot3dPicaPhysicalMemoryRegion>
BuildPicaMemoryRegions(
    const Oot3dNativeGame::NativeA32ProcessImageManifest &manifest) {
  std::vector<Oot3dNativeGame::Oot3dPicaPhysicalMemoryRegion> regions{
      {0x20000000U, manifest.LinearHeapBaseAddress, manifest.LinearHeapSize}};
  for (const auto &region : manifest.SystemRegions) {
    if (region.Name == "ctr_vram") {
      regions.push_back({0x18000000U, region.Address, region.MappedSize});
      break;
    }
  }
  return regions;
}

struct NativeControlPollingState {
  std::array<bool, Oot3dNativeGame::kNativeControlActionCount>
      PhysicalActions{};
  std::array<bool, Oot3dNativeGame::kNativeControlActionCount>
      PendingPressedActions{};
  int64_t PendingMouseDeltaX = 0;
  int64_t PendingMouseDeltaY = 0;
  double PendingMouseSeconds = 0.0;
  bool GameplayMouseOwned = false;
  Oot3dNativeGame::NativeRightStickProfileState RightStickProfile;
};

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
                   NativeControlPollingState &pollingState) {
  using namespace Oot3dNativeGame;
  const auto config = controls.Snapshot().Config;
  NativeControlHostInputState host;
  host.SamplePeriodSeconds = samplePeriodSeconds;
  const auto &io = ImGui::GetIO();
  const bool keyboardCaptured = io.WantCaptureKeyboard;
  const bool mouseCaptured = hostGuiVisible;

  struct SelectedController {
    int32_t InstanceId = -1;
    SDL_GameController *Controller = nullptr;
    std::string Guid;
  };
  std::optional<SelectedController> selectedController;
  std::vector<NativeControlDeviceDescriptor> deviceDescriptors;
  auto *context = Ship::Context::GetRawInstance();
  auto controlDeck = context != nullptr ? context->GetControlDeck() : nullptr;
  if (controlDeck != nullptr) {
    auto devices = controlDeck->GetConnectedPhysicalDeviceManager();
    if (devices != nullptr) {
      auto connected = devices->GetConnectedSDLGamepadsForPort(0);
      std::vector<std::pair<int32_t, SDL_GameController *>> ordered(
          connected.begin(), connected.end());
      std::sort(ordered.begin(), ordered.end(),
                [](const auto &lhs, const auto &rhs) {
                  return lhs.first < rhs.first;
                });
      for (const auto &[instanceId, controller] : ordered) {
        if (controller == nullptr ||
            SDL_GameControllerGetAttached(controller) != SDL_TRUE) {
          continue;
        }
        SDL_Joystick *joystick = SDL_GameControllerGetJoystick(controller);
        char guidText[33]{};
        SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joystick), guidText,
                                  static_cast<int>(sizeof(guidText)));
        const char *name = SDL_GameControllerName(controller);
        NativeControlDeviceDescriptor descriptor;
        descriptor.InstanceId = instanceId;
        descriptor.Guid = guidText;
        descriptor.Name =
            name != nullptr ? name : descriptor.Guid;
#if SDL_VERSION_ATLEAST(2, 0, 14)
        descriptor.HasGyroscope =
            SDL_GameControllerHasSensor(controller, SDL_SENSOR_GYRO) ==
            SDL_TRUE;
        descriptor.HasAccelerometer =
            SDL_GameControllerHasSensor(controller, SDL_SENSOR_ACCEL) ==
            SDL_TRUE;
#endif
        deviceDescriptors.push_back(descriptor);
        const bool preferred =
            !config.PreferredControllerGuid.empty() &&
            config.PreferredControllerGuid == descriptor.Guid;
        const bool automatic =
            config.PreferredControllerGuid.empty() &&
            !selectedController.has_value();
        if (preferred || automatic) {
          selectedController =
              SelectedController{instanceId, controller, descriptor.Guid};
        }
      }
    }
  }
  controls.ObserveDevices(std::move(deviceDescriptors));

  const int16_t triggerThreshold = static_cast<int16_t>(
      32767 * std::clamp(config.TriggerDeadZonePercent, 0, 95) / 100);
  class LusHostButtonSource final
      : public ThreeDsRecomp::Input::HostButtonSource {
   public:
    LusHostButtonSource(Fast::Fast3dWindow &window,
                        bool keyboardCaptured,
                        bool mouseCaptured,
                        SDL_GameController *controller,
                        int16_t triggerThreshold)
        : mWindow(window), mKeyboardCaptured(keyboardCaptured),
          mMouseCaptured(mouseCaptured), mController(controller),
          mTriggerThreshold(triggerThreshold) {}

    bool IsKeyboardKeyHeld(
        NativeKeyboardKey key) const noexcept override {
      return !mKeyboardCaptured && key != NativeKeyboardKey::None &&
             mWindow.IsKeyDown(static_cast<int32_t>(key));
    }

    bool IsMouseButtonHeld(
        NativeMouseButton button) const noexcept override {
      return !mMouseCaptured && button != NativeMouseButton::None &&
             mWindow.GetMouseState(static_cast<Ship::MouseBtn>(button));
    }

    bool IsGamepadButtonHeld(
        NativeGamepadButton binding) const noexcept override {
      if (mController == nullptr || binding == NativeGamepadButton::None) {
        return false;
      }
      const auto button = [&](SDL_GameControllerButton value) {
        return SDL_GameControllerGetButton(mController, value) != 0;
      };
      switch (binding) {
      case NativeGamepadButton::A:
        return button(SDL_CONTROLLER_BUTTON_A);
      case NativeGamepadButton::B:
        return button(SDL_CONTROLLER_BUTTON_B);
      case NativeGamepadButton::X:
        return button(SDL_CONTROLLER_BUTTON_X);
      case NativeGamepadButton::Y:
        return button(SDL_CONTROLLER_BUTTON_Y);
      case NativeGamepadButton::Back:
        return button(SDL_CONTROLLER_BUTTON_BACK);
      case NativeGamepadButton::Guide:
        return button(SDL_CONTROLLER_BUTTON_GUIDE);
      case NativeGamepadButton::Start:
        return button(SDL_CONTROLLER_BUTTON_START);
      case NativeGamepadButton::LeftStick:
        return button(SDL_CONTROLLER_BUTTON_LEFTSTICK);
      case NativeGamepadButton::RightStick:
        return button(SDL_CONTROLLER_BUTTON_RIGHTSTICK);
      case NativeGamepadButton::LeftShoulder:
        return button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
      case NativeGamepadButton::RightShoulder:
        return button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
      case NativeGamepadButton::DpadUp:
        return button(SDL_CONTROLLER_BUTTON_DPAD_UP);
      case NativeGamepadButton::DpadDown:
        return button(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
      case NativeGamepadButton::DpadLeft:
        return button(SDL_CONTROLLER_BUTTON_DPAD_LEFT);
      case NativeGamepadButton::DpadRight:
        return button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
      case NativeGamepadButton::LeftTrigger:
        return SDL_GameControllerGetAxis(
                   mController, SDL_CONTROLLER_AXIS_TRIGGERLEFT) >
               mTriggerThreshold;
      case NativeGamepadButton::RightTrigger:
        return SDL_GameControllerGetAxis(
                   mController, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) >
               mTriggerThreshold;
      case NativeGamepadButton::None:
        return false;
      }
      return false;
    }

   private:
    Fast::Fast3dWindow &mWindow;
    bool mKeyboardCaptured = false;
    bool mMouseCaptured = false;
    SDL_GameController *mController = nullptr;
    int16_t mTriggerThreshold = 0;
  };
  LusHostButtonSource buttonSource(
      window, keyboardCaptured, mouseCaptured,
      selectedController.has_value() ? selectedController->Controller
                                     : nullptr,
      triggerThreshold);
  const ThreeDsRecomp::Input::HostDeviceEnablement enabledDevices{
      config.KeyboardEnabled,
      config.MouseEnabled,
      config.ControllerEnabled,
  };

  for (size_t index = 0; index < kNativeControlActionCount; ++index) {
    const auto &binding = config.Bindings[index];
    const bool physicalHeld = ThreeDsRecomp::Input::IsHostBindingHeld(
        binding, enabledDevices, buttonSource);
    if (physicalHeld && !pollingState.PhysicalActions[index] &&
        static_cast<NativeControlAction>(index) !=
            NativeControlAction::Start) {
      pollingState.PendingPressedActions[index] = true;
    }
    pollingState.PhysicalActions[index] = physicalHeld;
    host.Actions[index] = physicalHeld;
    if (guestRefreshWillConsume &&
        static_cast<NativeControlAction>(index) !=
            NativeControlAction::Start) {
      host.Actions[index] =
          physicalHeld || pollingState.PendingPressedActions[index];
      pollingState.PendingPressedActions[index] = false;
    }
  }

  if (config.ControllerEnabled && selectedController.has_value()) {
    SDL_GameController *controller = selectedController->Controller;
    const auto axis = [&](SDL_GameControllerAxis value) {
      return SDL_GameControllerGetAxis(controller, value);
    };
    const auto invertedAxis = [&](SDL_GameControllerAxis value) {
      return static_cast<int16_t>(std::clamp(
          -static_cast<int32_t>(axis(value)), -32767, 32767));
    };
    host.LeftStickX = axis(SDL_CONTROLLER_AXIS_LEFTX);
    host.LeftStickY = invertedAxis(SDL_CONTROLLER_AXIS_LEFTY);
    host.RightStickX = axis(SDL_CONTROLLER_AXIS_RIGHTX);
    host.RightStickY = invertedAxis(SDL_CONTROLLER_AXIS_RIGHTY);
#if SDL_VERSION_ATLEAST(2, 0, 14)
    constexpr float kGravityMetersPerSecondSquared = 9.80665F;
    constexpr float kRadiansToDegrees =
        57.2957795130823208768F;
    if (SDL_GameControllerHasSensor(controller, SDL_SENSOR_ACCEL) ==
        SDL_TRUE) {
      if (SDL_GameControllerIsSensorEnabled(
              controller, SDL_SENSOR_ACCEL) != SDL_TRUE) {
        SDL_GameControllerSetSensorEnabled(controller, SDL_SENSOR_ACCEL,
                                           SDL_TRUE);
      }
      std::array<float, 3> sample{};
      if (SDL_GameControllerGetSensorData(
              controller, SDL_SENSOR_ACCEL, sample.data(),
              static_cast<int>(sample.size())) == 0) {
        host.ControllerMotion.Accelerometer = {
            sample[0] / kGravityMetersPerSecondSquared,
            -sample[1] / kGravityMetersPerSecondSquared,
            sample[2] / kGravityMetersPerSecondSquared};
        host.ControllerMotion.AccelerometerValid = true;
      }
    }
    if (SDL_GameControllerHasSensor(controller, SDL_SENSOR_GYRO) ==
        SDL_TRUE) {
      if (SDL_GameControllerIsSensorEnabled(
              controller, SDL_SENSOR_GYRO) != SDL_TRUE) {
        SDL_GameControllerSetSensorEnabled(controller, SDL_SENSOR_GYRO,
                                           SDL_TRUE);
      }
      std::array<float, 3> sample{};
      if (SDL_GameControllerGetSensorData(
              controller, SDL_SENSOR_GYRO, sample.data(),
              static_cast<int>(sample.size())) == 0) {
        host.ControllerMotion.GyroscopeDegreesPerSecond = {
            -sample[0] * kRadiansToDegrees,
            sample[1] * kRadiansToDegrees,
            -sample[2] * kRadiansToDegrees};
        host.ControllerMotion.GyroscopeValid = true;
      }
    }
#endif
  }
  controls.ObserveMotion(host.ControllerMotion);

  const bool sourceUsesMouse =
      config.NativeAimSource == NativeMotionSource::Mouse ||
      config.NativeAimSource == NativeMotionSource::Automatic ||
      (topScreenUiProfile && topScreenFreeCameraEnabled &&
       (config.FreeCameraSource == NativeMotionSource::Mouse ||
        config.FreeCameraSource == NativeMotionSource::Automatic));
  const bool gameplayMouseOwned = ResolveNativeGameplayMouseOwnership(
      nativeFrontendTouchEnabled, hostGuiVisible, config.MouseEnabled,
      sourceUsesMouse);
  bool mouseCaptureChanged =
      gameplayMouseOwned != pollingState.GameplayMouseOwned;
  pollingState.GameplayMouseOwned = gameplayMouseOwned;
  if (gameplayMouseOwned && config.CaptureMouseInGameplay) {
    if (!window.IsMouseCaptured()) {
      window.SetMouseCapture(true);
      mouseCaptureChanged = true;
    }
    window.SetCursorVisibility(false);
  } else if (window.IsMouseCaptured() &&
             (!gameplayMouseOwned || !config.CaptureMouseInGameplay)) {
    window.SetMouseCapture(false);
    mouseCaptureChanged = true;
    window.SetCursorVisibility(true);
  }
  // SDL relative motion is an accumulator. Drain it even while ImGui owns the
  // pointer so menu movement and capture warps cannot leak into gameplay.
  const auto mouseDelta = window.GetMouseDelta();
  if (gameplayMouseOwned && !mouseCaptureChanged) {
    pollingState.PendingMouseDeltaX += mouseDelta.x;
    pollingState.PendingMouseDeltaY += mouseDelta.y;
    pollingState.PendingMouseSeconds +=
        std::clamp(samplePeriodSeconds, 0.0, 0.25);
    if (guestRefreshWillConsume) {
      host.MouseDeltaX = static_cast<int32_t>(std::clamp<int64_t>(
          pollingState.PendingMouseDeltaX, INT32_MIN, INT32_MAX));
      host.MouseDeltaY = static_cast<int32_t>(std::clamp<int64_t>(
          pollingState.PendingMouseDeltaY, INT32_MIN, INT32_MAX));
      host.SamplePeriodSeconds =
          std::max(pollingState.PendingMouseSeconds, 1.0 / 1000.0);
      pollingState.PendingMouseDeltaX = 0;
      pollingState.PendingMouseDeltaY = 0;
      pollingState.PendingMouseSeconds = 0.0;
    }
  } else {
    pollingState.PendingMouseDeltaX = 0;
    pollingState.PendingMouseDeltaY = 0;
    pollingState.PendingMouseSeconds = 0.0;
  }

  auto frame = MapNativeControlInput(config, host, aimTransform,
                                     &pollingState.RightStickProfile,
                                     guestRefreshWillConsume);
  if (nativeFrontendTouchEnabled) {
    if (window.IsMouseCaptured()) {
      window.SetMouseCapture(false);
    }
    window.SetCursorVisibility(true);
    const auto pointer = window.GetMousePos();
    const auto touch = Oot3dNativeGame::MapHostPointerToNativeA32Touch(
        pointer.x, pointer.y, window.GetWidth(), window.GetHeight(),
        window.GetMouseState(Ship::LUS_MOUSE_BTN_LEFT),
        topScreenUiProfile
            ? Oot3dNativeGame::NativeA32TouchPresentation::TopScreen400x240
            : Oot3dNativeGame::NativeA32TouchPresentation::
                  NativeLowerScreen320x240);
    frame.Hid.TouchX = touch.X;
    frame.Hid.TouchY = touch.Y;
    frame.Hid.TouchPressed = touch.Pressed;
  }
  ApplyNativeControlShortcutTouch(host, frame);
  frame.Exit = !keyboardCaptured &&
               window.IsKeyDown(Ship::LUS_KB_ESCAPE);
  return frame;
}

Oot3dNativeGame::NativeA32InputFrame ResolveNativeA32GuestInput(
    const Oot3dNativeGame::NativeA32InputFrame &physical,
    const Oot3dNativeGame::NativeA32InputTimeline &timeline,
    uint32_t guestFrameIndex, uint32_t runFrameIndex,
    bool nativeFrontendTouchEnabled,
    bool topScreenStartRoutingEligible, bool topScreenUiProfile,
    Oot3dNativeGame::NativeA32PolledButtonLatch &startButtonLatch,
    Oot3dNativeGame::TopScreenStartRoutingState &startRoutingState) {
  auto resolved = physical;
  if (timeline.Enabled()) {
    resolved = timeline.Sample(guestFrameIndex, runFrameIndex);
    resolved.Exit = physical.Exit;
  } else {
    const uint32_t start = Oot3dNativeGame::NativeA32HidButtonMask(
        Oot3dNativeGame::NativeA32HidButton::Start);
    const bool physicalStartHeld = (resolved.Hid.Buttons & start) != 0U;
    if (Oot3dNativeGame::ResolveNativeA32PolledButton(
            physicalStartHeld, startButtonLatch)) {
      resolved.Hid.Buttons |= start;
    } else {
      resolved.Hid.Buttons &= ~start;
    }
  }
  Oot3dNativeGame::ApplyTopScreenStartRouting(
      resolved, topScreenUiProfile, topScreenStartRoutingEligible,
      nativeFrontendTouchEnabled, true, startRoutingState);
  return resolved;
}

bool ResolveNativeA32TouchInputEnabled(
    Oot3dNativeGame::NativeA32Memory &memory,
    const Oot3dNativeGame::Oot3dNativeUiLifecycleBridge &uiLifecycleBridge,
    bool topScreenUiProfile) {
  bool nativePausePageActive = false;
  if (topScreenUiProfile) {
    Oot3dNativeGame::TopScreenPauseDrawInputs pauseInputs;
    nativePausePageActive =
        Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &pauseInputs) &&
        pauseInputs.AnyPageGateActive;
  }
  return Oot3dNativeGame::ResolveNativeA32TouchInputEnabled(
      topScreenUiProfile,
      uiLifecycleBridge.NativeTouchPresentationActive(),
      uiLifecycleBridge.NativeFrontendPresentationActive(),
      nativePausePageActive);
}

bool ApplyTopScreenPauseClose(
    Oot3dNativeGame::NativeA32Process &process,
    const Oot3dNativeGame::TopScreenPauseClosePlan &plan, std::string *error) {
  if (plan.Page == Oot3dNativeGame::TopScreenPauseClosePage::None ||
      plan.ResetFunction == 0U) {
    return true;
  }

  uint32_t pauseState = 0U;
  uint32_t sceneControl = 0U;
  auto &memory = process.Memory();
  if (!memory.Read32(kTopScreenPauseState, &pauseState) ||
      !memory.Read32(kTopScreenPauseSceneControl, &sceneControl) ||
      !memory.IsWritable(kTopScreenPauseState, sizeof(uint32_t)) ||
      !memory.IsWritable(kTopScreenPauseSceneControl, sizeof(uint32_t))) {
    if (error != nullptr)
      *error = "cannot preflight native pause state";
    return false;
  }
  static_cast<void>(pauseState);
  static_cast<void>(sceneControl);

  constexpr std::array<uint32_t, 1> kResetArguments{0U};
  if (!process.InvokeFunction(plan.ResetFunction, kResetArguments,
                              kTopScreenDirectCallReturn, error)) {
    if (error != nullptr)
      *error = "page reset failed: " + *error;
    return false;
  }
  if (!memory.Write32(kTopScreenPauseState, 2U) ||
      !memory.Write32(kTopScreenPauseSceneControl, 0U)) {
    if (error != nullptr)
      *error = "native pause close-state write failed";
    return false;
  }
  constexpr std::array<uint32_t, 2> kTouchArguments{0U, 1U};
  if (!process.InvokeFunction(kPauseInputSetTouchEnabled, kTouchArguments,
                              kTopScreenDirectCallReturn, error)) {
    if (error != nullptr)
      *error = "pause input restore failed: " + *error;
    return false;
  }
  return true;
}

bool ApplyTopScreenPauseSystemOpen(
    Oot3dNativeGame::NativeA32Process &process,
    const Oot3dNativeGame::TopScreenPauseSystemOpenPlan &plan,
    std::string *error) {
  if (plan.Page == Oot3dNativeGame::TopScreenPauseClosePage::None ||
      plan.ResetFunction == 0U) {
    return true;
  }

  auto &memory = process.Memory();
  if (!memory.IsWritable(kTopScreenPauseState, sizeof(uint32_t)) ||
      !memory.IsWritable(kTopScreenPauseSceneControl, sizeof(uint32_t))) {
    if (error != nullptr)
      *error = "cannot preflight native system-menu transition state";
    return false;
  }

  constexpr std::array<uint32_t, 1> kResetArguments{0U};
  if (!process.InvokeFunction(plan.ResetFunction, kResetArguments,
                              kTopScreenDirectCallReturn, error)) {
    if (error != nullptr)
      *error = "page reset before system menu failed: " + *error;
    return false;
  }
  if (!memory.Write32(kTopScreenPauseSceneControl, 1U)) {
    if (error != nullptr)
      *error = "native system-menu scene-control write failed";
    return false;
  }
  constexpr std::array<uint32_t, 2> kTouchArguments{1U, 0U};
  if (!process.InvokeFunction(kPauseInputSetTouchEnabled, kTouchArguments,
                              kTopScreenDirectCallReturn, error)) {
    if (error != nullptr)
      *error = "system-menu touch ownership failed: " + *error;
    return false;
  }
  if (!memory.Write32(kTopScreenPauseState, 0U)) {
    if (error != nullptr)
      *error = "native system-menu pause-state write failed";
    return false;
  }
  constexpr std::array<uint32_t, 0> kNoArguments{};
  if (!process.InvokeFunction(kPauseSystemMenuOpen, kNoArguments,
                              kTopScreenDirectCallReturn, error)) {
    if (error != nullptr)
      *error = "native system-menu open failed: " + *error;
    return false;
  }
  return true;
}

} // namespace

void RunOot3dNativeA32Window(const Oot3dNativeGameLaunch &launch) {
  if (launch.Host.Renderer != "opengl" &&
      launch.Host.Renderer != "nri" &&
      launch.Host.Renderer != "vulkan") {
    throw std::runtime_error(
        "the native process requires the OpenGL or NRI/Vulkan backend");
  }

  auto topScreenConfigRuntime =
      std::make_shared<Oot3dNativeGame::TopScreenUiConfigRuntime>(
          launch.TopScreenConfigPath, launch.TopScreenConfig);
  auto controlConfigRuntime =
      std::make_shared<Oot3dNativeGame::NativeControlConfigRuntime>(
          launch.ControlConfigPath, launch.ControlConfig);
  Oot3dNativeGame::TopScreenUiConfig activeTopScreenConfig =
      topScreenConfigRuntime->Snapshot().Config;
#if defined(__SWITCH__)
  gSwitchUiProfile = Oot3dNativeGame::Oot3dUiProfileName(launch.UiProfile);
  gSwitchGameplayTiming =
      Oot3dNativeGame::GameplayTimingModeName(launch.GameplayTiming);
  gSwitchControlProfile = Oot3dNativeGame::NativeControlProfileName(
      controlConfigRuntime->Snapshot().Config.Profile);
  gSwitchPresentationRateHz = launch.PresentationRateHz;
  gSwitchWholeAotBlockBudget = launch.WholeAotBlockBudget;
  gSwitchPicaGeometryCacheEnabled =
      !launch.DisableOpenGlPicaGeometryCache;
#endif

  std::string error;
  const auto manifest = Oot3dNativeGame::LoadNativeA32ProcessImageManifest(
      launch.A32ProcessManifestPath, &error);
  if (!manifest.has_value()) {
    throw std::runtime_error(error);
  }
#if defined(__SWITCH__)
  WriteSwitchBootStage("manifest_loaded");
#endif

  std::optional<Oot3dNativeGame::NativeScenarioCatalog> scenarioCatalog;
  std::optional<Oot3dNativeGame::NativeScenarioBootstrap> scenarioBootstrap;
  if (!launch.ScenarioId.empty()) {
    scenarioCatalog =
        Oot3dNativeGame::LoadNativeScenarioCatalog(launch.ScenarioCatalogPath);
    const auto *recipe = Oot3dNativeGame::FindNativeScenarioRecipe(
        *scenarioCatalog, launch.ScenarioId);
    if (recipe == nullptr) {
      throw std::runtime_error("unknown structural scenario: " +
                               launch.ScenarioId);
    }
    scenarioBootstrap.emplace(scenarioCatalog->Runtime, *recipe);
    std::cout << "oot3d_native_game: structural scenario " << recipe->Id
              << " selects " << recipe->ScenePath << " entrance "
              << recipe->EntranceIndex << '\n';
  }

  Oot3dNativeGame::TopScreenTextureOverridePack topScreenTextureOverridePack;
  const Oot3dNativeGame::TopScreenTextureOverridePack
      *topScreenTextureOverridePackPointer = nullptr;
  if (!launch.TopScreenTextureOverridePackPath.empty()) {
    if (!topScreenTextureOverridePack.Load(
            launch.TopScreenTextureOverridePackPath, &error)) {
      throw std::runtime_error("cannot load TopScreen texture overrides: " +
                               error);
    }
    topScreenTextureOverridePackPointer = &topScreenTextureOverridePack;
#if defined(__SWITCH__)
    gSwitchTopScreen211Assets =
        topScreenTextureOverridePack.FindProfileTexture(
            Oot3dNativeGame::kTopScreen211MenuAtlasSemantic) != nullptr &&
        topScreenTextureOverridePack.FindProfileTexture(
            "oot3d/topscreen/2.1.1/font_atlas") != nullptr;
#endif
  }

  Oot3dNativeGame::Oot3dNativePicaFrontend picaFrontend;
  picaFrontend.SetDiagnosticHistoryEnabled(false);
  Oot3dNativeGame::Oot3dNativePicaCompositionTracker
      picaCompositionTracker;
  Oot3dNativeGame::Oot3dPicaCompositionDomain picaCompositionDomain =
      Oot3dNativeGame::Oot3dPicaCompositionDomain::Unknown;
  Oot3dNativeGame::NativeA32CtrHostConfig hostConfig;
  hostConfig.ResourceLimitValues = manifest->ResourceLimitValues;
  hostConfig.ResourceCurrentValues = manifest->ResourceCurrentValues;
  hostConfig.LinearHeapBaseAddress = manifest->LinearHeapBaseAddress;
  hostConfig.LinearHeapSize = manifest->LinearHeapSize;
  hostConfig.HeapBaseAddress = manifest->HeapBaseAddress;
  hostConfig.HeapSize = manifest->HeapSize;
  hostConfig.RomFsImagePath = manifest->RomFsImagePath;
  hostConfig.RomFsImageOffset = manifest->RomFsImageOffset;
  hostConfig.RomFsImageSize = manifest->RomFsImageSize;
  hostConfig.PicaFrontend = &picaFrontend;
  hostConfig.PicaCompositionDomain = &picaCompositionDomain;
  hostConfig.PicaCompositionProvider = &picaCompositionTracker;
  hostConfig.SaveDataDirectory =
      launch.SaveDataDirectory.empty()
          ? launch.A32ProcessManifestPath.parent_path() /
                "oot3d_native_savedata"
          : launch.SaveDataDirectory;
  const std::filesystem::path saveDataDirectory = hostConfig.SaveDataDirectory;
  const std::filesystem::path quickStatePath =
      launch.QuickStatePath.empty() ? saveDataDirectory / "quick.oot3dsav"
                                    : launch.QuickStatePath;
  const Oot3dNativeGame::NativeA32SavestateCompatibility savestateCompatibility{
      manifest->CodeBinSha256,
      manifest->ProcessName,
      std::string(oot3d::recomp::kA32SourceSnapshotId),
  };
  hostConfig.DetailedSvcDiagnostics = launch.ExtendedDiagnostics;
  hostConfig.ProfileRuntime = launch.ProfileA32Runtime;
  Oot3dNativeGame::NativeA32CtrHostServices hostServices(std::move(hostConfig));
  const auto frameRateContract =
      Oot3dNativeGame::ResolveNativeFrameRateContract(
          launch.GameplayTiming, launch.PresentationRateHz);
#if defined(__SWITCH__)
  gSwitchVisualInterpolation = frameRateContract.VisualInterpolationAllowed;
#endif
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  if (launch.EnableSourceGameplayProfile ||
      launch.EnableSourceActorInitContext ||
      launch.EnableSourceActorUpdateAll ||
      launch.EnableSourceCutsceneUpdateFrame ||
      launch.EnableSourceCutsceneProcessCommands ||
      launch.EnableSourceCameraUpdate ||
      launch.EnableSourcePlayerUpdate ||
      launch.EnableSourcePlayerUpdateCommon ||
      launch.EnableSourceCsabCurves) {
    throw std::runtime_error(
        "source-native options are documentation-only in a whole-AOT "
        "product");
  }
  const Oot3dNativeGame::SourceGameplayOwnerSelection
      sourceGameplayOwners{};
#else
  if (launch.EnableSourceGameplayProfile &&
      frameRateContract.Mode ==
          Oot3dNativeGame::GameplayTimingMode::Enhanced60) {
    throw std::runtime_error(
        "--enable-source-gameplay-profile is Native30-only; "
        "Enhanced60 retains its typed timing owners");
  }
  if (launch.EnableSourceGameplayProfile &&
      (launch.DisableCompiledFunctions ||
       launch.DisableTypedGameplay)) {
    throw std::runtime_error(
        "--enable-source-gameplay-profile requires typed gameplay "
        "for GameState_Update");
  }
  const auto sourceGameplayOwners =
      Oot3dNativeGame::ResolveSourceGameplayOwnerSelection({
          launch.EnableSourceGameplayProfile,
          !launch.DisableCompiledFunctions &&
              !launch.DisableTypedGameplay,
          launch.EnableSourceActorInitContext,
          launch.EnableSourceActorUpdateAll,
          launch.EnableSourceCutsceneUpdateFrame,
          launch.EnableSourceCutsceneProcessCommands,
          launch.EnableSourceCameraUpdate,
          launch.EnableSourcePlayerUpdate,
          launch.EnableSourcePlayerUpdateCommon,
      });
#endif
  const bool enableSourceActorInitContext =
      sourceGameplayOwners.ActorInitContext;
  const bool enableSourceActorUpdateAll =
      sourceGameplayOwners.ActorUpdateAll;
  const bool enableSourceCutsceneUpdateFrame =
      sourceGameplayOwners.CutsceneUpdateFrame;
  const bool enableSourceCutsceneProcessCommands =
      sourceGameplayOwners.CutsceneProcessCommands;
  const bool enableSourceCameraUpdate =
      sourceGameplayOwners.CameraUpdate;
  const bool enableSourcePlayerUpdate =
      sourceGameplayOwners.PlayerUpdate;
  const bool enableSourcePlayerUpdateCommon =
      sourceGameplayOwners.PlayerUpdateCommon;
  const bool enableSourceCsabCurves = launch.EnableSourceCsabCurves;
#if !defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  if (enableSourceCutsceneUpdateFrame &&
      frameRateContract.Mode ==
          Oot3dNativeGame::GameplayTimingMode::Enhanced60) {
    throw std::runtime_error(
        "--enable-source-cutscene-update-frame is Native30-only; "
        "Enhanced60 retains its typed frame-crossing owner");
  }
  if (enableSourceCutsceneProcessCommands &&
      frameRateContract.Mode ==
          Oot3dNativeGame::GameplayTimingMode::Enhanced60) {
    throw std::runtime_error(
        "--enable-source-cutscene-process-commands is Native30-only; "
        "Enhanced60 retains its typed cutscene path");
  }
  if (enableSourceCameraUpdate &&
      frameRateContract.Mode ==
          Oot3dNativeGame::GameplayTimingMode::Enhanced60) {
    throw std::runtime_error(
        "--enable-source-camera-update is Native30-only; "
        "Enhanced60 retains its typed camera timing owner");
  }
  if (enableSourcePlayerUpdate &&
      frameRateContract.Mode ==
          Oot3dNativeGame::GameplayTimingMode::Enhanced60) {
    throw std::runtime_error(
        "--enable-source-player-update is Native30-only; "
        "Enhanced60 retains its typed player timing owner");
  }
  if (enableSourcePlayerUpdateCommon &&
      frameRateContract.Mode ==
          Oot3dNativeGame::GameplayTimingMode::Enhanced60) {
    throw std::runtime_error(
        "--enable-source-player-update-common is Native30-only; "
        "Enhanced60 retains its typed player timing owner");
  }
#endif
  Oot3dNativeGame::NativeFrameRatePolicy frameRatePolicy(frameRateContract);
  Oot3dNativeGame::NativeA32SceneViewProbe sceneViewProbe;
  Oot3dNativeGame::NativeA32PlayerTimingProbe playerTimingProbe;
  Oot3dNativeGame::NativeTemporalEventLedger temporalEventLedger;
  Oot3dNativeGame::NativeA32PlayerTemporalBridge playerTemporalBridge;
  NativeWidescreenProjectionState widescreenProjection;
  widescreenProjection.FrameRatePolicy = &frameRatePolicy;
  widescreenProjection.SceneViewProbe = &sceneViewProbe;
  widescreenProjection.PlayerTimingProbe = &playerTimingProbe;
  widescreenProjection.TemporalEventLedger = &temporalEventLedger;
  if (frameRateContract.Mode ==
      Oot3dNativeGame::GameplayTimingMode::Enhanced60) {
    widescreenProjection.PlayerTemporalBridge = &playerTemporalBridge;
  }
  widescreenProjection.CollectExtendedDiagnostics = launch.ExtendedDiagnostics;
  widescreenProjection.ProfileA32Blocks = launch.ProfileA32Blocks;
  widescreenProjection.TraceA32Blocks = !launch.A32BlockTracePath.empty();
  ScopedA32BlockTraceWriter blockTraceWriter(
      launch.A32BlockTracePath, widescreenProjection);
  std::vector<uint32_t> blockEntryHooks{
      kMtx4x4BuildFrustum,
      kMtx4x4BuildOrthographicProjectionRotated,
      kGameStateUpdate,
      Oot3dNativeGame::kOot3dPlayerUpdateEntry,
      Oot3dNativeGame::kOot3dFramePacingBeginEntry,
      Oot3dNativeGame::kOot3dFramePacingDecisionEntry,
      Oot3dNativeGame::kOot3dFramePacingCommitEntry,
      Oot3dNativeGame::kOot3dMeshCommandPacketSubmitEntry,
  };
  const auto picaCompositionHooks =
      Oot3dNativeGame::Oot3dNativePicaCompositionHookPcs();
  blockEntryHooks.insert(blockEntryHooks.end(),
                         picaCompositionHooks.begin(),
                         picaCompositionHooks.end());
  if (widescreenProjection.PlayerTemporalBridge != nullptr) {
    const auto temporalHooks =
        Oot3dNativeGame::NativeA32PlayerTemporalHookPcs();
    blockEntryHooks.insert(blockEntryHooks.end(), temporalHooks.begin(),
                           temporalHooks.end());
  }
  const auto typedGameplayObservableExitHooks =
      Oot3dNativeGame::Oot3dTypedGameplayObservableExitPoints();
#if !defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  blockEntryHooks.insert(blockEntryHooks.end(),
                         typedGameplayObservableExitHooks.begin(),
                         typedGameplayObservableExitHooks.end());
#endif
  const auto &uiLifecycleEntries =
      Oot3dNativeGame::Oot3dNativeUiLifecycleBridge::GuestEntryPoints();
  blockEntryHooks.insert(blockEntryHooks.end(), uiLifecycleEntries.begin(),
                         uiLifecycleEntries.end());
  if (launch.UiProfile == Oot3dNativeGame::Oot3dUiProfile::TopScreen) {
    const std::array topScreenHooks{
        Oot3dNativeGame::kTopScreenQuestMaterializedHook,
        Oot3dNativeGame::kTopScreenQuestSubmitModelsHook,
        Oot3dNativeGame::kTopScreenQuestDrawHook,
        0x00300588U,
        0x002FEABCU,
        0x00311364U,
        kPauseProjectionPrepare,
        kPauseUiDraw,
        kPauseUiDrawReturn,
        kPauseIconBuild};
    // Texture payloads are patched when the native pause owner has loaded
    // them, before its update can submit HUD or pause draws.
    blockEntryHooks.push_back(kPauseUiUpdate);
    blockEntryHooks.insert(blockEntryHooks.end(), topScreenHooks.begin(),
                           topScreenHooks.end());
    const std::array topScreenSourcePortHooks{
        kPauseTouchButtonsDrawCall,
        kPauseTouchButtonsTouchBranch,
        kPauseLowerScreenBlock,
        kPauseDungeonMapCursorInput,
        kPauseUiNativeUpdateCall,
        kPauseProjectionPrepareCall,
        kGlobalActionStateGetSlotItemId,
        kPauseItemsNativeUpdateCall,
        kPauseItemsNativeUpdateReturn,
        kPauseAlternateViewportDrawCall,
        kPauseConditionalDrawCall,
        kPauseRoutedCommandCallA,
        kPauseRoutedCommandCallB,
        kPauseOverlayViewportDrawCall,
        kPauseAlternateRendererUpdateCall,
        kPauseSceneSevenDrawCall,
        kPauseSceneViewportDrawCall,
        kPauseTouchCoordinateUpdateCall,
        kPauseRendererVisibilityCall,
        kPauseControllerDrawCall,
        kTopScreenCameraNormal1Scalar,
        kTopScreenCameraFovScalar,
        kTopScreenCameraUpdateEntry,
        kTopScreenCameraUpdatePatchSite,
        kTopScreenGameplayCompositionPrefix,
    };
    blockEntryHooks.insert(blockEntryHooks.end(),
                           topScreenSourcePortHooks.begin(),
                           topScreenSourcePortHooks.end());
    for (const auto &contract :
         Oot3dNativeGame::TopScreenVerifiedControlFlowContracts()) {
      blockEntryHooks.push_back(contract.Entry);
    }
    for (const auto &contract :
         Oot3dNativeGame::TopScreenVerifiedFloatLoadContracts()) {
      blockEntryHooks.push_back(contract.Entry);
    }
    for (const auto &contract :
         Oot3dNativeGame::TopScreenVerifiedItemQueryContracts()) {
      blockEntryHooks.push_back(contract.OriginalEntry);
    }
  }
  if (launch.ExtendedDiagnostics) {
    const std::array diagnosticHooks{
        kEnMagInit,
        kEnMagUpdate,
        kEnMagDraw,
        kPauseUiUpdate,
        kPauseUiDraw,
        kFileSelectUpdate,
        kFileSelectInit,
        kFileChooseInit,
        kPlayTransitionUpdate,
        kFileSelectActivate,
        kBlockingQueuePop,
        kBlockingQueuePush,
        kRendererCommandQueueProcessPending,
        kBlockingQueueTryPush,
        kTaskQueueTakeFirstFromPriority,
        kTaskQueueEnqueuePriorityNode,
        kTaskQueueWorkerDrainPriorityNodes,
        kNngxP3dCallback,
        kNngxCommonInterruptHandler,
        kNngxCmdlistCompletionDispatch,
        kRendererGlobalCallbackAfterLookup,
    };
    blockEntryHooks.insert(blockEntryHooks.end(), diagnosticHooks.begin(),
                           diagnosticHooks.end());
  }
  std::sort(blockEntryHooks.begin(), blockEntryHooks.end());
  blockEntryHooks.erase(
      std::unique(blockEntryHooks.begin(), blockEntryHooks.end()),
      blockEntryHooks.end());
#if defined(__SWITCH__)
  gSwitchBlockHookCount = blockEntryHooks.size();
  gSwitchBlockHooksSorted =
      std::is_sorted(blockEntryHooks.begin(), blockEntryHooks.end());
  gSwitchBlockHooksContainProcessEntry = std::binary_search(
      blockEntryHooks.begin(), blockEntryHooks.end(), 0x00100000U);
#endif
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  const bool enableCompiledFunctions = true;
#else
  const bool enableCompiledFunctions = !launch.DisableCompiledFunctions;
#endif
  const bool wholeAotAvailable =
      Oot3dNativeGame::Oot3dWholeAotAvailable();
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  if (!wholeAotAvailable) {
    throw std::runtime_error(
        "whole-AOT product mode requires the audited generated backend");
  }
  const bool enableWholeAot = true;
#else
  const bool enableWholeAot = enableCompiledFunctions &&
                              !launch.DisableWholeAot &&
                              wholeAotAvailable;
#endif
  if (enableCompiledFunctions && !launch.DisableWholeAot &&
      !wholeAotAvailable) {
    std::cerr
        << "Warning: whole-AOT was requested but is not linked. "
           "Guest execution will use substantially slower fallback paths.\n";
  }
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  const bool enableMassAot = false;
#else
  const bool enableMassAot = enableCompiledFunctions &&
                             !launch.DisableMassAot &&
                             Oot3dNativeGame::Oot3dMassAotAvailable();
#endif
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  const bool enableTrueAot = false;
#else
  const bool enableTrueAot =
      enableCompiledFunctions && !launch.DisableTrueAotBlocks;
#endif
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  const bool enableManualCompiledFunctions = true;
#else
  const bool enableManualCompiledFunctions =
      !launch.DisableManualCompiledFunctions;
#endif
  const auto trueAotEntries = Oot3dNativeGame::Oot3dTrueAotBlockEntryPoints();
  const auto compiledFunctionEntries =
      Oot3dNativeGame::Oot3dCompiledFunctionEntryPoints(
          enableWholeAot,
          enableCompiledFunctions && enableManualCompiledFunctions);
  const auto typedGameplayEntries =
      Oot3dNativeGame::Oot3dTypedGameplayEntryPoints();
  std::vector<uint32_t> nativeCandidateEntries;
  if (enableMassAot) {
    const auto massAotEntries = Oot3dNativeGame::Oot3dMassAotBlockEntryPoints();
    nativeCandidateEntries.insert(nativeCandidateEntries.end(),
                                  massAotEntries.begin(), massAotEntries.end());
    Oot3dNativeGame::ResetOot3dMassAotStats();
  }
  if (enableCompiledFunctions) {
    nativeCandidateEntries.insert(nativeCandidateEntries.end(),
                                  typedGameplayEntries.begin(),
                                  typedGameplayEntries.end());
    nativeCandidateEntries.insert(nativeCandidateEntries.end(),
                                  compiledFunctionEntries.begin(),
                                  compiledFunctionEntries.end());
    Oot3dNativeGame::ResetOot3dTypedGameplayStats();
    Oot3dNativeGame::ResetOot3dCompiledFunctionStats();
    Oot3dNativeGame::SetOot3dCompiledFunctionProfilingEnabled(
        launch.ProfileA32Runtime);
  }
  if (enableTrueAot) {
    nativeCandidateEntries.insert(nativeCandidateEntries.end(),
                                  trueAotEntries.begin(), trueAotEntries.end());
    Oot3dNativeGame::ResetOot3dTrueAotBlockStats();
  }
  if (enableSourceActorUpdateAll) {
    nativeCandidateEntries.push_back(
        Oot3dNativeGame::kSourceActorUpdateAllEntry);
  }
  if (enableSourceActorInitContext) {
    nativeCandidateEntries.push_back(
        Oot3dNativeGame::kSourceActorInitContextEntry);
  }
  if (enableSourceCutsceneUpdateFrame) {
    nativeCandidateEntries.push_back(
        Oot3dNativeGame::kSourceCutsceneUpdateFrameEntry);
  }
  if (enableSourceCutsceneProcessCommands) {
    nativeCandidateEntries.push_back(
        Oot3dNativeGame::kSourceCutsceneProcessCommandsEntry);
  }
  if (enableSourceCameraUpdate) {
    nativeCandidateEntries.push_back(
        Oot3dNativeGame::kSourceCameraUpdateEntry);
  }
  if (enableSourcePlayerUpdate) {
    nativeCandidateEntries.push_back(
        Oot3dNativeGame::kSourcePlayerUpdateEntry);
  }
  if (enableSourcePlayerUpdateCommon) {
    nativeCandidateEntries.push_back(
        Oot3dNativeGame::kSourcePlayerUpdateCommonEntry);
  }
  if (enableSourceCsabCurves) {
    nativeCandidateEntries.push_back(
        Oot3dNativeGame::kSourceCsabCurveS16Entry);
    nativeCandidateEntries.push_back(
        Oot3dNativeGame::kSourceCsabCurveF32Entry);
  }
  std::vector<uint32_t> topScreenSourcePortBlocks;
  std::vector<uint32_t> wholeAotObservableExitBlocks;
#if !defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  wholeAotObservableExitBlocks.push_back(
      Oot3dNativeGame::kOot3dMeshCommandPacketSubmitEntry);
  wholeAotObservableExitBlocks.insert(
      wholeAotObservableExitBlocks.end(), picaCompositionHooks.begin(),
      picaCompositionHooks.end());
#endif
  if (enableSourceActorUpdateAll) {
    wholeAotObservableExitBlocks.push_back(
        Oot3dNativeGame::kSourceActorUpdateAllEntry);
  }
  if (enableSourceActorInitContext) {
    wholeAotObservableExitBlocks.push_back(
        Oot3dNativeGame::kSourceActorInitContextEntry);
  }
  if (enableSourceCutsceneUpdateFrame) {
    wholeAotObservableExitBlocks.push_back(
        Oot3dNativeGame::kSourceCutsceneUpdateFrameEntry);
  }
  if (enableSourceCutsceneProcessCommands) {
    wholeAotObservableExitBlocks.push_back(
        Oot3dNativeGame::kSourceCutsceneProcessCommandsEntry);
  }
  if (enableSourceCameraUpdate) {
    wholeAotObservableExitBlocks.push_back(
        Oot3dNativeGame::kSourceCameraUpdateEntry);
  }
  if (enableSourcePlayerUpdate) {
    wholeAotObservableExitBlocks.push_back(
        Oot3dNativeGame::kSourcePlayerUpdateEntry);
  }
  if (enableSourcePlayerUpdateCommon) {
    wholeAotObservableExitBlocks.push_back(
        Oot3dNativeGame::kSourcePlayerUpdateCommonEntry);
  }
  if (enableSourceCsabCurves) {
    wholeAotObservableExitBlocks.push_back(
        Oot3dNativeGame::kSourceCsabCurveS16Entry);
    wholeAotObservableExitBlocks.push_back(
        Oot3dNativeGame::kSourceCsabCurveF32Entry);
  }
#if !defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  if (enableCompiledFunctions && !launch.DisableTypedGameplay) {
    wholeAotObservableExitBlocks.insert(
        wholeAotObservableExitBlocks.end(),
        typedGameplayObservableExitHooks.begin(),
        typedGameplayObservableExitHooks.end());
  }
#endif
  if (launch.UiProfile == Oot3dNativeGame::Oot3dUiProfile::TopScreen) {
    topScreenSourcePortBlocks = {
        kPauseTouchButtonsDrawCall,       kPauseTouchButtonsTouchBranch,
        kPauseLowerScreenBlock,           kPauseDungeonMapCursorInput,
        kPauseUiNativeUpdateCall,         kPauseProjectionPrepareCall,
        kPauseAlternateViewportDrawCall,  kPauseConditionalDrawCall,
        kPauseRoutedCommandCallA,         kPauseRoutedCommandCallB,
        kPauseOverlayViewportDrawCall,    kPauseAlternateRendererUpdateCall,
        kPauseSceneSevenDrawCall,         kPauseSceneViewportDrawCall,
        kPauseTouchCoordinateUpdateCall,  kPauseRendererVisibilityCall,
        kPauseControllerDrawCall,         kTopScreenCameraNormal1Scalar,
        kTopScreenCameraUpdateEntry,      kTopScreenCameraUpdatePatchSite,
        kTopScreenTitleLogoFadeHoldPrefix};
    for (const auto &contract :
         Oot3dNativeGame::TopScreenVerifiedControlFlowContracts()) {
      topScreenSourcePortBlocks.push_back(contract.Entry);
    }
    for (const auto &contract :
         Oot3dNativeGame::TopScreenVerifiedFloatLoadContracts()) {
      topScreenSourcePortBlocks.push_back(contract.Entry);
    }
    for (const auto &contract :
         Oot3dNativeGame::TopScreenVerifiedItemQueryContracts()) {
      topScreenSourcePortBlocks.push_back(contract.OriginalEntry);
    }
    topScreenSourcePortBlocks.push_back(kGlobalActionStateGetSlotItemId);
    topScreenSourcePortBlocks.push_back(kPauseItemsNativeUpdateCall);
    topScreenSourcePortBlocks.push_back(kPauseItemsNativeUpdateReturn);
    topScreenSourcePortBlocks.push_back(kTopScreenGameplayCompositionPrefix);
    // These source ports are reached from inside large AOT regions. Make
    // their native entries observable so execution returns to the dispatcher
    // before applying the typed replacement. Keep this list sorted because
    // the whole-AOT block-entry callback uses binary_search.
    const std::array topScreenObservableExitBlocks{
        kTopScreenCameraNormal1Scalar,
        kTopScreenCameraUpdateEntry,
        kTopScreenGameplayCompositionPrefix,
        kTopScreenTitleLogoFadeHoldPrefix,
    };
#if !defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
    wholeAotObservableExitBlocks.insert(
        wholeAotObservableExitBlocks.end(),
        topScreenObservableExitBlocks.begin(),
        topScreenObservableExitBlocks.end());
#else
    wholeAotObservableExitBlocks.push_back(kTopScreenCameraUpdateEntry);
    wholeAotObservableExitBlocks.push_back(kTopScreenCameraNormal1Scalar);
#endif
    // ZL/ZR item slots. The block-entry gate above exits only when the
    // override resolves, so ordinary play never leaves compiled code here.
    for (const auto &contract :
         Oot3dNativeGame::TopScreenVerifiedItemQueryContracts()) {
      wholeAotObservableExitBlocks.push_back(contract.OriginalEntry);
    }
    wholeAotObservableExitBlocks.push_back(kGlobalActionStateGetSlotItemId);
    nativeCandidateEntries.insert(nativeCandidateEntries.end(),
                                  topScreenSourcePortBlocks.begin(),
                                  topScreenSourcePortBlocks.end());
  }
  std::sort(wholeAotObservableExitBlocks.begin(),
            wholeAotObservableExitBlocks.end());
  wholeAotObservableExitBlocks.erase(
      std::unique(wholeAotObservableExitBlocks.begin(),
                  wholeAotObservableExitBlocks.end()),
      wholeAotObservableExitBlocks.end());
  std::sort(nativeCandidateEntries.begin(), nativeCandidateEntries.end());
  nativeCandidateEntries.erase(
      std::unique(nativeCandidateEntries.begin(), nativeCandidateEntries.end()),
      nativeCandidateEntries.end());
  Oot3dNativeGame::ConfigureOot3dNativeA32Candidates(
      nativeCandidateEntries);
  Oot3dNativeGame::NativeA32Process process(
      Oot3dNativeGame::Oot3dNativeA32Registry(), hostServices);
#if !defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  Oot3dNativeGame::SourceActorInitContextRuntime sourceActorInitContext(
      process);
  Oot3dNativeGame::SourceActorUpdateAllRuntime sourceActorUpdateAll(process);
  Oot3dNativeGame::SourceCutsceneUpdateFrameRuntime
      sourceCutsceneUpdateFrame(process);
  Oot3dNativeGame::SourceCutsceneProcessCommandsRuntime
      sourceCutsceneProcessCommands(process);
  Oot3dNativeGame::SourceCameraUpdateRuntime sourceCameraUpdate(process);
  Oot3dNativeGame::SourcePlayerUpdateRuntime sourcePlayerUpdate(process);
  Oot3dNativeGame::SourcePlayerUpdateCommonRuntime
      sourcePlayerUpdateCommon(process);
  Oot3dNativeGame::SourceCsabCurveRuntime sourceCsabCurve(
      process.Memory());
#endif
  widescreenProjection.TraceMemory = &process.Memory();
  process.Memory().EnableWriteTraceFingerprint(
      widescreenProjection.TraceA32Blocks);
  Oot3dNativeGame::Oot3dNativeUiLifecycleBridge uiLifecycleBridge(
      process.Memory());
  uiLifecycleBridge.SetTopScreenConfig(activeTopScreenConfig);
  widescreenProjection.UiLifecycleBridge = &uiLifecycleBridge;
  widescreenProjection.PicaCompositionDomain = &picaCompositionDomain;
  widescreenProjection.PicaCompositionTracker = &picaCompositionTracker;
  widescreenProjection.TopScreenUiProfile =
      launch.UiProfile == Oot3dNativeGame::Oot3dUiProfile::TopScreen;
  widescreenProjection.TopScreenConfig = activeTopScreenConfig;
  const auto nativeBlockEntryPcs =
      launch.ProfileA32Blocks || widescreenProjection.TraceA32Blocks
          ? std::span<const uint32_t>{}
          : std::span<const uint32_t>{blockEntryHooks};
  NativeCandidateDispatchState nativeCandidateDispatch{
      &ApplyNativeWidescreenProjectionPolicy,
      &widescreenProjection,
      nativeBlockEntryPcs,
      Oot3dNativeGame::BuildOot3dMassAotPcFilter(nativeBlockEntryPcs,
                                                 nativeBlockEntryPcs.empty()),
      Oot3dNativeGame::BuildOot3dMassAotPcFilter(
          wholeAotObservableExitBlocks, false),
      &process.Memory(),
      {static_cast<float>(frameRateContract.NativeUpdateRate),
       &frameRatePolicy.GameplayClock().Context()},
      enableCompiledFunctions && !launch.DisableTypedGameplay,
      enableMassAot,
      enableCompiledFunctions,
      enableWholeAot,
      enableManualCompiledFunctions,
      enableTrueAot,
      launch.ProfileA32Runtime,
  };
  nativeCandidateDispatch.TopScreenUiProfile =
      launch.UiProfile == Oot3dNativeGame::Oot3dUiProfile::TopScreen;
#if !defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  nativeCandidateDispatch.SourceActorInitContext =
      enableSourceActorInitContext ? &sourceActorInitContext : nullptr;
  nativeCandidateDispatch.SourceActorUpdateAll =
      enableSourceActorUpdateAll ? &sourceActorUpdateAll : nullptr;
  nativeCandidateDispatch.SourceCutsceneUpdateFrame =
      enableSourceCutsceneUpdateFrame ? &sourceCutsceneUpdateFrame : nullptr;
  nativeCandidateDispatch.SourceCutsceneProcessCommands =
      enableSourceCutsceneProcessCommands
          ? &sourceCutsceneProcessCommands
          : nullptr;
  nativeCandidateDispatch.SourceCameraUpdate =
      enableSourceCameraUpdate ? &sourceCameraUpdate : nullptr;
  nativeCandidateDispatch.SourcePlayerUpdate =
      enableSourcePlayerUpdate ? &sourcePlayerUpdate : nullptr;
  nativeCandidateDispatch.SourcePlayerUpdateCommon =
      enableSourcePlayerUpdateCommon
          ? &sourcePlayerUpdateCommon
          : nullptr;
  nativeCandidateDispatch.SourceCsabCurve =
      enableSourceCsabCurves ? &sourceCsabCurve : nullptr;
#endif
  widescreenProjection.TopScreenInput =
      &nativeCandidateDispatch.TopScreenInput;
  nativeCandidateDispatch.UiLifecycleBridge = &uiLifecycleBridge;
  nativeCandidateDispatch.PicaCompositionDomain = &picaCompositionDomain;
  nativeCandidateDispatch.TopScreenPauseProjection =
      &uiLifecycleBridge.TopScreenPauseProjection();
  nativeCandidateDispatch.TopScreenConfig = &activeTopScreenConfig;
  Oot3dNativeGame::ApplyTopScreenFreeCameraConfig(
      nativeCandidateDispatch.TopScreenCamera.Camera, activeTopScreenConfig);
  std::uint64_t appliedTopScreenConfigRevision =
      topScreenConfigRuntime->Snapshot().Revision;
  const auto applyTopScreenConfigSnapshot = [&]() {
    const auto snapshot = topScreenConfigRuntime->Snapshot();
    if (snapshot.Revision == appliedTopScreenConfigRevision) {
      return;
    }
    activeTopScreenConfig = snapshot.Config;
    appliedTopScreenConfigRevision = snapshot.Revision;
    uiLifecycleBridge.SetTopScreenConfig(activeTopScreenConfig);
    widescreenProjection.TopScreenConfig = activeTopScreenConfig;
    Oot3dNativeGame::ApplyTopScreenFreeCameraConfig(
        nativeCandidateDispatch.TopScreenCamera.Camera,
        activeTopScreenConfig);
    if (!activeTopScreenConfig.FreeCameraEnabled) {
      nativeCandidateDispatch.TopScreenCameraInput.Reset();
    }
  };
  const auto setTopScreenSessionMinimapVisible = [&](bool visible) {
    activeTopScreenConfig.MinimapVisible = visible;
    uiLifecycleBridge.SetTopScreenConfig(activeTopScreenConfig);
    widescreenProjection.TopScreenConfig = activeTopScreenConfig;
  };
  widescreenProjection.TopScreenPauseTargetRoute =
      &nativeCandidateDispatch.TopScreenPauseRoute;
  widescreenProjection.TopScreenTouchCoordinateRoute =
      &nativeCandidateDispatch.TopScreenTouchCoordinateRoute;
  widescreenProjection.WholeAotObservableExitPcs =
      wholeAotObservableExitBlocks;
  process.SetTimingEnabled(launch.ProfileA32Runtime);
#if !defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
  // Native candidates are experimental reconstruction aids.  The product
  // executes the audited whole-AOT graph and never substitutes source-native
  // or typed gameplay implementations for guest functions.
  if (!nativeCandidateEntries.empty()) {
    process.SetNativeBlockCallback(&ExecuteNativeCandidate,
                                   &nativeCandidateDispatch);
  }
#else
  if (nativeCandidateDispatch.TopScreenUiProfile) {
    process.SetNativeBlockCallback(&ExecuteProductTopScreenCamera,
                                   &nativeCandidateDispatch);
  }
#endif
  if (!Oot3dNativeGame::MountNativeA32ProcessImage(
          process, *manifest, std::filesystem::path{}, &error)) {
    throw std::runtime_error(error);
  }
#if defined(__SWITCH__)
  WriteSwitchBootStage("process_image_mounted");
#endif
  const auto applySelectedUiProfile = [&]() {
    if (launch.UiProfile != Oot3dNativeGame::Oot3dUiProfile::TopScreen) {
      return;
    }
    Oot3dNativeGame::TopScreenLayoutApplyStats layoutStats;
    if (!Oot3dNativeGame::ApplyTopScreenVerifiedLayout(process.Memory(),
                                                       &layoutStats, &error)) {
      throw std::runtime_error(
          "TopScreen source-port layout rejected the process image: " + error);
    }
    Oot3dNativeGame::TopScreenRuntimeGeometryApplyStats geometryStats;
    if (layoutStats.WordsChanged != 0U &&
        !Oot3dNativeGame::RebindTopScreenVerifiedRuntimeGeometry(
            process.Memory(), &geometryStats, &error)) {
      throw std::runtime_error(
          "TopScreen source-port runtime geometry rejected the process "
          "image: " +
          error);
    }
    std::cout << "TopScreen source-port layout: contracts="
              << layoutStats.ContractsChecked
              << " words=" << layoutStats.WordsWritten
              << " changed_words=" << layoutStats.WordsChanged
              << " rebound_streams=" << geometryStats.StreamsRebound
              << " rebound_quads=" << geometryStats.QuadsRebound << '\n';
  };

  const auto picaMemoryRegions = BuildPicaMemoryRegions(*manifest);
  const Oot3dNativeGame::Oot3dPicaPhysicalMemoryView picaMemoryView(
      process.Memory(), picaMemoryRegions);
  std::optional<Oot3dNativeGame::TopScreenTextureOverrideRuntime>
      topScreenTextureOverrideRuntime;
  if (topScreenTextureOverridePackPointer != nullptr) {
    topScreenTextureOverrideRuntime.emplace(topScreenTextureOverridePack,
                                            picaMemoryView);
    widescreenProjection.TopScreenTextureOverrides =
        &*topScreenTextureOverrideRuntime;
  }
  Oot3dNativeGame::Oot3dNativePicaSubmissionQueue submissionQueue(
      picaMemoryView, true);
  if (topScreenTextureOverrideRuntime.has_value()) {
    submissionQueue.SetTexturePayloadTransform(
        [&](const Oot3dNativeGame::Oot3dPicaTextureState &texture,
            std::span<std::uint8_t> payload) {
          topScreenTextureOverrideRuntime->Transform(texture, payload);
        });
  }
  submissionQueue.SetRuntimeProfilingEnabled(launch.ProfileA32Runtime);
  picaFrontend.SetPacketSink(&submissionQueue);

  Args hostArgs = launch.Host;
  hostArgs.AudioSampleRate = Oot3dNativeGame::NativeA32DspHle::NativeSampleRate;
  hostArgs.AudioSampleLength =
      Oot3dNativeGame::NativeA32DspHle::SamplesPerFrame;
  hostArgs.AudioDesiredBuffered = std::max<int32_t>(
      hostArgs.AudioDesiredBuffered,
      static_cast<int32_t>((static_cast<uint64_t>(hostArgs.AudioSampleRate) *
                                kMinimumHostAudioPrebufferMilliseconds +
                            999U) /
                           1000U));
  std::vector<std::shared_ptr<Fast::Oot3d::GraphicsSettingsPanelTab>>
      applicationSettingsTabs;
  applicationSettingsTabs.push_back(
      Oot3dNativeGame::CreateNativeControlsSettingsPanel(
          controlConfigRuntime,
          launch.UiProfile ==
                  Oot3dNativeGame::Oot3dUiProfile::TopScreen
              ? topScreenConfigRuntime
              : nullptr));
  if (launch.UiProfile ==
      Oot3dNativeGame::Oot3dUiProfile::TopScreen) {
    applicationSettingsTabs.push_back(
        Oot3dNativeGame::CreateTopScreenSettingsPanel(
            topScreenConfigRuntime));
  }
  Fast::Oot3d::InstallGraphicsSettingsPanelTabs(
      std::move(applicationSettingsTabs));
  InitContextForDemo(hostArgs);
#if defined(__SWITCH__)
  WriteSwitchBootStage("host_context_initialized");
#endif
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE) && defined(_WIN32)
  std::string crashDiagnosticsError;
  if (!Oot3dNativeGame::InstallWholeAotCrashDiagnostics(
          saveDataDirectory / "crashes", &crashDiagnosticsError)) {
    throw std::runtime_error("whole-AOT crash diagnostics failed: " +
                             crashDiagnosticsError);
  }
#endif
  auto &window = GetActiveFast3dWindowForDemo();
  if (hostArgs.ThroughputBenchmark) {
    window.SetTargetFps(0);
  }
  auto &api = GetActiveRenderingApiForDemo(window);
  api.SetNativePicaGeometryCacheEnabled(
      !launch.DisableOpenGlPicaGeometryCache);
  auto *const titleRenderBackend =
      dynamic_cast<Fast::Oot3d::TitleRenderBackend *>(&api);
  const auto gui =
      Ship::Context::GetRawInstance()->GetWindow()->GetGui();
  if (gui == nullptr) {
    throw std::runtime_error("native PICA host has no renderer GUI");
  }
#if defined(__SWITCH__)
  WriteSwitchBootStage("opengl_renderer_initialized");
#endif
  Oot3dNativeGame::Oot3dNativeA32UiTextureProvider nativeUiTextureProvider(
      picaMemoryView, topScreenTextureOverridePackPointer);
  oot3d::ui::Fast3dUiRenderBackend uiRenderBackend(api);
  oot3d::ui::N64UiFast3dRenderer n64UiRenderer(uiRenderBackend,
                                               &nativeUiTextureProvider);
  std::array<std::vector<oot3d::ui::UiPrimitive>,
             oot3d::ui::kUiSubsystemCount>
      lastTopScreenPrimitivesBySubsystem;
  window.SetTextureFilter(Fast::FILTER_LINEAR);
  const auto inputTimeline = Oot3dNativeGame::NativeA32InputTimeline::LoadFile(
      launch.Host.InputTimelinePath);
#if defined(__SWITCH__)
  WriteSwitchBootStage("ui_runtime_initialized");
#endif

  const auto updateScenePresentation = [&]() {
    widescreenProjection.Projection.Presentation =
        Fast::Oot3d::ResolveScenePresentationPolicy(
            std::max<uint32_t>(1, window.GetWidth()),
            std::max<uint32_t>(1, window.GetHeight()));
    widescreenProjection.Projection.FovMultiplier =
        Fast::Oot3d::GraphicsSettingsRuntime::Instance()
            .Snapshot()
            .FovMultiplier;
  };
  updateScenePresentation();
  process.SetBlockEntryCallback(
      &ApplyNativeWidescreenProjectionPolicy, &widescreenProjection,
      launch.ProfileA32Blocks || widescreenProjection.TraceA32Blocks
          ? std::vector<uint32_t>{}
          : blockEntryHooks);
#if defined(__SWITCH__)
  WriteSwitchBootStage("guest_callbacks_initialized");
#endif

  NativeFramePhaseTiming phaseTiming;
  Oot3dNativeGame::NativeA32InputDiagnostics inputDiagnostics;
  NativeInputConsumerDiagnostics inputConsumerDiagnostics;
  NativeControlPollingState nativeControlPollingState;
  Oot3dNativeGame::Oot3dPicaVulkanShaderSourceCache shaderSourceCache;
  Oot3dNativeGame::Oot3dPicaSemanticTraceWriter picaSemanticTrace(
      launch.PicaSemanticTracePath);
  picaSemanticTrace.BeginSession(
      launch.Host.Renderer,
      Oot3dNativeGame::GameplayTimingModeName(launch.GameplayTiming),
      Oot3dNativeGame::Oot3dUiProfileName(launch.UiProfile));
  Oot3dNativeGame::NativeA32DspHle dspHle(
      Oot3dNativeGame::BuildNativeA32DspPhysicalRegions(*manifest));
  auto *context = Ship::Context::GetRawInstance();
  auto audio = context != nullptr ? context->GetAudio() : nullptr;
  auto audioPlayer = audio != nullptr ? audio->GetAudioPlayer() : nullptr;
#if defined(__SWITCH__)
  WriteSwitchBootStage("audio_runtime_initialized");
#endif
  auto phaseStart = std::chrono::steady_clock::now();
  Oot3dNativeGame::NativeA32ProcessRunResult processResult;
  uint32_t frameCount = 0;
  uint32_t runFrameCount = 0;
  uint64_t presentationFrameCount = 0;
  uint64_t refreshTickRemainder = 0;
  uint64_t nextVblankTick = 0;
  uint64_t lateVblankDeadlineCount = 0;
  uint64_t maximumVblankDeadlineOvershootTicks = 0;
  uint64_t savestateSaveCount = 0;
  uint64_t savestateLoadCount = 0;
  uint64_t savestateSaveBytes = 0;
  uint64_t savestateLoadBytes = 0;
  double savestateSaveSeconds = 0.0;
  double savestateLoadSeconds = 0.0;
  double savestateCaptureSeconds = 0.0;
  double savestateEncodeSeconds = 0.0;
  double savestateWriteSeconds = 0.0;
  double savestateReadSeconds = 0.0;
  double savestateDecodeSeconds = 0.0;
  double savestateRestoreSeconds = 0.0;
  uint64_t legacySavestateClockRestoreCount = 0;
  uint64_t legacySavestatePresentationSchedulerRestoreCount = 0;
  uint64_t legacySavestateFrameRatePolicyRestoreCount = 0;
  uint64_t legacySavestateTopScreenTemporalRestoreCount = 0;
  uint64_t legacySavestatePicaVisualReplayRestoreCount = 0;
  uint64_t legacySavestatePicaTextureCacheRestoreCount = 0;
  uint64_t legacySavestatePicaColorTargetRestoreCount = 0;
  uint64_t legacySavestatePicaPresentationRestoreCount = 0;
  uint64_t savestatePicaTexturesCaptured = 0;
  uint64_t savestatePicaTextureBytesCaptured = 0;
  uint64_t savestatePicaTexturesRestored = 0;
  uint64_t savestatePicaTextureBytesRestored = 0;
  uint64_t savestatePicaColorTargetsCaptured = 0;
  uint64_t savestatePicaColorTargetBytesCaptured = 0;
  uint64_t savestatePicaColorTargetsRestored = 0;
  uint64_t savestatePicaColorTargetBytesRestored = 0;
  uint64_t savestatePicaDisplayImagesCaptured = 0;
  uint64_t savestatePicaDisplayImageBytesCaptured = 0;
  uint64_t savestatePicaDisplayImagesRestored = 0;
  uint64_t savestatePicaDisplayImageBytesRestored = 0;
  uint64_t savestatePicaVisualReplayBytesCaptured = 0;
  uint64_t savestatePicaVisualReplayBytesRestored = 0;
  uint64_t lastSavestateSemanticFingerprint = 0;
  Oot3dNativeGame::NativePresentationScheduler presentationScheduler(
      frameRateContract.SimulationRateHz,
      frameRateContract.Mode ==
              Oot3dNativeGame::GameplayTimingMode::Enhanced60
          ? Oot3dNativeGame::NativeSimulationCatchUpPolicy::
                PreserveBoundedDebt
          : Oot3dNativeGame::NativeSimulationCatchUpPolicy::DropExcess);
  Oot3dNativeGame::NativeVisualSampleCadence visualSampleCadence;
  Oot3dNativeGame::Oot3dPicaPresentationScheduler
      picaPresentationScheduler;
  Oot3dNativeGame::Oot3dPicaVisualContinuityTracker
      visualContinuityTracker;
  Oot3dNativeGame::Oot3dPicaPreparedVisualTransition
      preparedVisualTransition;
  std::optional<Oot3dNativeGame::Oot3dPicaVisualFrame>
      latestVisualFrame;
  std::optional<Oot3dNativeGame::Oot3dPicaVisualFrame>
      previousVisualFrame;
  bool latestVisualTransitionContinuous = false;
  uint64_t visualContinuityEpoch = 1U;
  std::map<uint32_t, Oot3dNativeGame::Oot3dPicaDisplayTransferSubmission>
      displayTransfersByOutput;
  uint64_t lastSubmittedDrawId = 0;
  uint64_t lastSelectedTopTransferCompletionId = 0;
  const auto restoreSavestateTiming =
      [&](const Oot3dNativeGame::NativeA32SavestateRuntimeState &runtime) {
        if (!runtime.GameplayClockAvailable) {
          frameRatePolicy.ResetGameplayClock();
          ++legacySavestateClockRestoreCount;
          std::cerr
              << "oot3d_native_game: legacy savestate has no gameplay clock; "
                 "starting at a quiescent logical-frame boundary\n";
        } else {
          if (runtime.GameplayTiming != frameRateContract.Mode) {
            throw std::runtime_error(
                "savestate gameplay timing mode is " +
                std::string(Oot3dNativeGame::GameplayTimingModeName(
                    runtime.GameplayTiming)) +
                ", requested runtime mode is " +
                Oot3dNativeGame::GameplayTimingModeName(
                    frameRateContract.Mode));
          }
          if (!frameRatePolicy.RestoreGameplayClock(runtime.GameplayClock)) {
            throw std::runtime_error(
                "savestate gameplay clock restore failed: " +
                frameRatePolicy.LastError());
          }
        }
        if (!runtime.PresentationSchedulerAvailable) {
          presentationScheduler.Reset();
          ++legacySavestatePresentationSchedulerRestoreCount;
        } else if (!presentationScheduler.Restore(
                       runtime.PresentationScheduler, &error)) {
          throw std::runtime_error(
              "savestate presentation scheduler restore failed: " + error);
        }
        if (!runtime.FrameRatePolicyTemporalStateAvailable) {
          frameRatePolicy.ResetTemporalState();
          ++legacySavestateFrameRatePolicyRestoreCount;
        } else if (!frameRatePolicy.RestoreTemporalState(
                       runtime.FrameRatePolicyTemporalState, &error)) {
          throw std::runtime_error(
              "savestate frame-rate policy restore failed: " + error);
        }
      };
  const auto restoreSavestateTopScreenTemporalState =
      [&](const Oot3dNativeGame::NativeA32SavestateRuntimeState &runtime) {
        if (launch.UiProfile !=
            Oot3dNativeGame::Oot3dUiProfile::TopScreen) {
          return;
        }
        if (!runtime.TopScreenTemporalStateAvailable ||
            !runtime.TopScreenTemporalState.ProfileActive) {
          ++legacySavestateTopScreenTemporalRestoreCount;
          return;
        }
        const auto &state = runtime.TopScreenTemporalState;
        widescreenProjection.TopScreenPausePageRedrawActive =
            state.PausePageRedrawActive;
        widescreenProjection.TopScreenPausePageRedraw.DelayCalls =
            state.PausePageRedrawDelayCalls;
        widescreenProjection.TopScreenPauseDrawRouting
            .NativeTransitionLatched =
            state.PauseDrawNativeTransitionLatched;
        widescreenProjection.TopScreenPauseDrawRouting
            .SuppressionDelayArmed =
            state.PauseDrawSuppressionDelayArmed;
        widescreenProjection.TopScreenPauseDrawRouting
            .SuppressionDelayCommands =
            state.PauseDrawSuppressionDelayCommands;
        nativeCandidateDispatch.TopScreenPauseRoute.PreviousRuntimeMode =
            state.PauseRoutePreviousRuntimeMode;
        nativeCandidateDispatch.TopScreenPauseRoute.TransitionPhase =
            state.PauseRouteTransitionPhase;
        nativeCandidateDispatch.TopScreenPauseRoute.RemainingCalls =
            state.PauseRouteRemainingCalls;
        nativeCandidateDispatch.TopScreenAotPauseRoute.PreviousRuntimeMode =
            state.AotPauseRoutePreviousRuntimeMode;
        nativeCandidateDispatch.TopScreenAotPauseRoute.TransitionPhase =
            state.AotPauseRouteTransitionPhase;
        nativeCandidateDispatch.TopScreenAotPauseRoute.RemainingCalls =
            state.AotPauseRouteRemainingCalls;
        nativeCandidateDispatch.TopScreenTouchCoordinateRoute
            .RuntimeSceneLatch = state.TouchCoordinateRuntimeSceneLatch;
      };
  const auto restoreSavestatePicaTextureCache =
      [&](const Oot3dNativeGame::NativeA32SavestateRuntimeState &runtime) {
        if (!runtime.PicaTextureCacheAvailable) {
          ++legacySavestatePicaTextureCacheRestoreCount;
          return;
        }
        if (!api.RestorePicaTextureCache(
                runtime.PicaTextureCache, &error)) {
          throw std::runtime_error(
              "savestate native PICA texture restore failed: " + error);
        }
        for (const auto &texture : runtime.PicaTextureCache) {
          savestatePicaTextureBytesRestored +=
              texture.PixelBytes.size();
        }
        savestatePicaTexturesRestored +=
            runtime.PicaTextureCache.size();
      };
  const auto restoreSavestatePicaColorTargets =
      [&](const Oot3dNativeGame::NativeA32SavestateRuntimeState &runtime) {
        if (!runtime.PicaColorTargetsAvailable) {
          ++legacySavestatePicaColorTargetRestoreCount;
          return;
        }
        if (!api.RestorePicaColorTargets(
                runtime.PicaColorTargets, &error)) {
          throw std::runtime_error(
              "savestate native PICA color restore failed: " + error);
        }
        std::vector<uint32_t> restoredAddresses;
        restoredAddresses.reserve(runtime.PicaColorTargets.size());
        for (const auto &target : runtime.PicaColorTargets) {
          restoredAddresses.push_back(target.ColorPhysicalAddress);
          savestatePicaColorTargetBytesRestored +=
              target.ColorRgba8.size();
        }
        submissionQueue.RestoreGpuColorRenderTargetOwnership(
            restoredAddresses);
        savestatePicaColorTargetsRestored += restoredAddresses.size();
      };
  const auto restoreSavestatePicaPresentationState =
      [&](const Oot3dNativeGame::NativeA32SavestateRuntimeState &runtime) {
        if (!runtime.PicaPresentationStateAvailable) {
          ++legacySavestatePicaPresentationRestoreCount;
          return;
        }
        if (!api.RestorePicaPresentationState(
                runtime.PicaPresentationState, &error)) {
          throw std::runtime_error(
              "savestate native PICA presentation restore failed: " +
              error);
        }
        for (const auto &image :
             runtime.PicaPresentationState.DisplayImages) {
          savestatePicaDisplayImageBytesRestored +=
              image.ColorRgba8.size();
        }
        savestatePicaDisplayImagesRestored +=
            runtime.PicaPresentationState.DisplayImages.size();
      };
  const auto restoreSavestatePicaVisualReplayState =
      [&](const Oot3dNativeGame::NativeA32SavestateRuntimeState &runtime) {
        if (!runtime.PicaVisualReplayStateAvailable) {
          picaPresentationScheduler.Reset();
          visualContinuityTracker = {};
          preparedVisualTransition.Reset();
          latestVisualFrame.reset();
          previousVisualFrame.reset();
          latestVisualTransitionContinuous = false;
          visualSampleCadence.Reset();
          displayTransfersByOutput.clear();
          lastSubmittedDrawId = 0U;
          lastSelectedTopTransferCompletionId = 0U;
          ++visualContinuityEpoch;
          ++legacySavestatePicaVisualReplayRestoreCount;
          return;
        }
        Oot3dNativeGame::Oot3dPicaVisualReplayState state;
        if (!Oot3dNativeGame::DecodeOot3dPicaVisualReplayState(
                runtime.PicaVisualReplayState, state, &error) ||
            !picaPresentationScheduler.RestoreState(
                std::move(state.Scheduler)) ||
            !visualContinuityTracker.RestoreState(
                std::move(state.Continuity))) {
          throw std::runtime_error(
              "savestate native PICA visual replay restore failed: " +
              error);
        }
        previousVisualFrame = std::move(state.PreviousFrame);
        latestVisualFrame = std::move(state.LatestFrame);
        latestVisualTransitionContinuous =
            state.LatestTransitionContinuous;
        preparedVisualTransition.Reset();
        visualSampleCadence.Reset();
        if (latestVisualTransitionContinuous &&
            previousVisualFrame.has_value() &&
            latestVisualFrame.has_value()) {
          latestVisualTransitionContinuous =
              preparedVisualTransition.Prepare(*previousVisualFrame,
                                               *latestVisualFrame);
        }
        ++visualContinuityEpoch;
        displayTransfersByOutput =
            std::move(state.DisplayTransfersByOutput);
        lastSelectedTopTransferCompletionId =
            state.LastSelectedTopTransferCompletionId;
        lastSubmittedDrawId = state.LastSubmittedDrawId;
        savestatePicaVisualReplayBytesRestored +=
            runtime.PicaVisualReplayState.size();
      };
  if (!launch.LoadStatePath.empty()) {
#if defined(__SWITCH__)
    WriteSwitchBootStage("initial_savestate_loading");
#endif
    Oot3dNativeGame::NativeA32SavestateRuntimeState restoredRuntime;
    Oot3dNativeGame::NativeA32SavestateIoResult ioResult;
    if (!Oot3dNativeGame::LoadNativeA32State(
            launch.LoadStatePath, savestateCompatibility, restoredRuntime,
            process, hostServices, picaFrontend, submissionQueue, dspHle,
            &ioResult, &error)) {
      throw std::runtime_error("native savestate load failed: " + error);
    }
    Oot3dNativeGame::ResetOot3dTypedGameplayTransientState();
    picaCompositionTracker.Reset();
    frameCount = restoredRuntime.FrameCount;
    refreshTickRemainder = restoredRuntime.RefreshTickRemainder;
    nextVblankTick = restoredRuntime.NextVblankTick;
    processResult.Kind = static_cast<Oot3dNativeGame::NativeA32ProcessRunKind>(
        restoredRuntime.ProcessRunKind);
    restoreSavestateTiming(restoredRuntime);
    applySelectedUiProfile();
    if (!api.ResetPicaState(&error)) {
      throw std::runtime_error(
          "native PICA reset before initial savestate restore failed: " +
          error);
    }
    if (titleRenderBackend != nullptr &&
        !titleRenderBackend->ResetTitleState(&error)) {
      throw std::runtime_error(
          "OOT3D renderer reset before initial savestate restore failed: " +
          error);
    }
    restoreSavestatePicaTextureCache(restoredRuntime);
    restoreSavestatePicaColorTargets(restoredRuntime);
    restoreSavestatePicaPresentationState(restoredRuntime);
    restoreSavestatePicaVisualReplayState(restoredRuntime);
    restoreSavestateTopScreenTemporalState(restoredRuntime);
    ++savestateLoadCount;
    savestateLoadBytes += ioResult.FileBytes;
    savestateLoadSeconds += SecondsSince(phaseStart);
    savestateReadSeconds += ioResult.IoSeconds;
    savestateDecodeSeconds += ioResult.DecodeSeconds;
    savestateRestoreSeconds += ioResult.RestoreSeconds;
    std::cout << "oot3d_native_game: loaded initial state at frame "
              << frameCount << " from " << launch.LoadStatePath.string()
              << '\n';
  } else {
    applySelectedUiProfile();
#if defined(__SWITCH__)
    WriteSwitchBootStage("initial_guest_dispatch");
#endif
    processResult =
        RunUntilGuestWait(process, launch.WholeAotBlockBudget);
#if defined(__SWITCH__)
    WriteSwitchBootStage("initial_guest_wait");
#endif
    phaseTiming.GuestSeconds += SecondsSince(phaseStart);
    RequireRunnableGuest(processResult);
  }
  uint64_t drawCount = 0;
  uint64_t refreshesWithNativeDraws = 0;
  uint64_t displayTransferCount = 0;
  uint64_t recordedCompletionCount = 0;
  uint64_t completedP3dCount = 0;
  uint64_t completedPpfCount = 0;
  uint64_t picaDrainPassCount = 0;
  uint64_t maximumPicaDrainPassesPerRefresh = 0;
  uint64_t selectedTopTransferCount = 0;
  uint64_t nativeBottomFrontendPresentationCount = 0;
  uint64_t nativeMenuBackdropDrawOmissionCount = 0;
  uint64_t nativeMenuClearDrawOmissionCount = 0;
  uint64_t nativeFrontendPresentationActiveCount = 0;
  uint64_t nativeFrontendBottomTransferHitCount = 0;
  uint64_t visualSnapshotCount = 0;
  uint64_t visualTransitionCount = 0;
  uint64_t visualMatchedDrawCount = 0;
  uint64_t visualStrictUniqueMatchedDrawCount = 0;
  uint64_t visualStrictOrdinalMatchedDrawCount = 0;
  uint64_t visualStructuralOrdinalMatchedDrawCount = 0;
  uint64_t visualPipelineOrdinalMatchedDrawCount = 0;
  uint64_t visualChangedContinuousDrawCount = 0;
  uint64_t visualChangedPerInstanceVertexDrawCount = 0;
  uint64_t visualChangedPerVertexDrawCount = 0;
  Oot3dNativeGame::Oot3dPicaVisualInterpolationTiming visualInterpolationTiming;
  uint64_t visualAmbiguousPreviousDrawCount = 0;
  uint64_t visualAmbiguousCurrentDrawCount = 0;
  uint64_t visualUnmatchedPreviousDrawCount = 0;
  uint64_t visualUnmatchedCurrentDrawCount = 0;
  uint64_t visualSamplesPresented = 0;
  uint64_t visualDirectFramesPresented = 0;
  uint64_t visualReusedSnapshotPresentations = 0;
  uint64_t visualDiscontinuityCount = 0;
  uint64_t visualNamespaceResyncCount = 0;
  uint64_t visualSampleDrawCount = 0;
  uint64_t visualInterpolatedDrawCount = 0;
  uint64_t visualInterpolatedPerInstanceVertexDrawCount = 0;
  uint64_t visualInterpolatedPerVertexDrawCount = 0;
  uint64_t vblankCount = 0;
  uint64_t dspFramesMixed = 0;
  uint64_t dspFramesSuppressed = 0;
  uint64_t dspSamplesMixed = 0;
  uint64_t dspNonzeroSamples = 0;
  uint64_t dspPcmFnv1a64 = 14695981039346656037ULL;
  int32_t dspPeakMagnitude = 0;
  nlohmann::json displayTransferEvents = nlohmann::json::array();
  nlohmann::json drawEvents = nlohmann::json::array();
  nlohmann::json visualTransitionEvents = nlohmann::json::array();
  WindowDemoTimingState timing;
  auto presentationPacingPolicy =
      Fast::Oot3d::ResolvePresentationPacingPolicy(
          Fast::Oot3d::GraphicsSettingsRuntime::Instance()
              .Snapshot()
              .FrameRate);
  const auto resolveFrameCompositionPolicy =
      [&](const Fast::Oot3d::PresentationPacingPolicy& pacing) {
        auto policy = pacing.Composition;
        policy.NativeRateHz = frameRateContract.SimulationRateHz;
        if (!frameRateContract.VisualInterpolationAllowed) {
          policy.Interpolation =
              Fast::Oot3d::NativeVisualInterpolationMode::Disabled;
          policy.FixedSampleMultiplier = 1U;
        }
        return policy;
      };
  auto frameCompositionPolicy =
      resolveFrameCompositionPolicy(presentationPacingPolicy);
  visualSampleCadence.Configure(
      frameCompositionPolicy.FixedMultiplier()
          ? frameCompositionPolicy.FixedSampleMultiplier
          : 0U);
  Oot3dNativeGame::NativeRealtimeRefreshPacer realtimePacer(
      hostArgs.FrameLimit == 0U && !hostArgs.ThroughputBenchmark,
      presentationPacingPolicy.TargetRateHz);
  realtimePacer.Configure(presentationPacingPolicy.Enabled &&
                              !hostArgs.ThroughputBenchmark,
                          presentationPacingPolicy.TargetRateHz);
  bool visualInterpolationEnabled =
      Oot3dNativeGame::ShouldUseNativeVisualInterpolation(
          frameRateContract,
          frameCompositionPolicy.InterpolationEnabled());
  auto lastPresentationTime = std::chrono::steady_clock::now();
  NativeAudioOutputDiagnostics audioOutputDiagnostics;
  Oot3dNativeGame::NativePcmContinuityDiagnostics pcmContinuityDiagnostics;
  std::vector<int16_t> pcmDumpSamples;
  FramebufferScreenshotState screenshotState;
  uint64_t lastPublishedSceneViewSerial = 0;
  uint64_t publishedSceneViewCount = 0;
  const auto publishNativeSceneView = [&]() {
    const auto snapshot = sceneViewProbe.Capture(process.Memory());
    if (!snapshot.has_value() ||
        snapshot->Serial == lastPublishedSceneViewSerial) {
      return;
    }
    const Fast::Oot3d::TitleSceneViewSubmission view{
        snapshot->GuestFunction,
        snapshot->GuestReturnAddress,
        snapshot->Left,
        snapshot->Right,
        snapshot->Bottom,
        snapshot->Top,
        snapshot->NearPlane,
        snapshot->FarPlane,
        snapshot->Eye,
        snapshot->At,
        snapshot->CameraAvailable,
    };
    if (titleRenderBackend != nullptr &&
        titleRenderBackend->PublishSceneView(view)) {
      lastPublishedSceneViewSerial = snapshot->Serial;
      ++publishedSceneViewCount;
    }
  };
  picaPresentationScheduler.SetTimingEnabled(launch.ExtendedDiagnostics);
  std::optional<Fast::Oot3d::NativeFrameTemporalSample> capturedTemporalSample;
  const auto executeVisualSample =
      [&](const Oot3dNativeGame::Oot3dPicaVisualFrameViewSample &sample,
          Oot3dNativeGame::Oot3dPicaPresentationExecutionKind kind,
          bool present,
          const Fast::Oot3d::NativeFrameTemporalSample& temporalSample) {
        const auto started = std::chrono::steady_clock::now();
        (void)api.PublishPicaFrameTemporalSample(temporalSample);
        Fast::Oot3d::GrassInteractionBridge::Instance().SelectSample(temporalSample);
        if (!picaPresentationScheduler.Execute(
                api, sample, kVisualInterpolationRenderTargetNamespace, kind,
                present, &error)) {
          throw std::runtime_error(
              "native PICA scheduled presentation failed: " + error);
        }
        const double elapsed = SecondsSince(started);
        if (present) {
          capturedTemporalSample = temporalSample;
        }
        phaseTiming.PicaBackendSeconds += elapsed;
        phaseTiming.VisualReplaySeconds += elapsed;
      };
  Oot3dNativeGame::Oot3dPicaVisualFrameViewSample visualFrameSampleScratch;
  std::vector<Oot3dNativeGame::Oot3dPicaDrawSubmission> drainedDraws;
  bool quickSaveKeyWasDown = false;
  bool quickLoadKeyWasDown = false;
  bool quickSavePending = false;
  bool automatedStateSaved = false;
  std::optional<uint32_t> automatedStateSavedAtRunFrame;
  std::optional<uint32_t> automatedStateSavedAtGuestFrame;

  const auto saveState = [&](const std::filesystem::path &path) {
    const auto started = std::chrono::steady_clock::now();
    Oot3dNativeGame::NativeA32SavestateRuntimeState runtime;
    runtime.FrameCount = frameCount;
    runtime.RefreshTickRemainder = refreshTickRemainder;
    runtime.NextVblankTick = nextVblankTick;
    runtime.ProcessRunKind = static_cast<uint32_t>(processResult.Kind);
    runtime.GameplayClockAvailable = true;
    runtime.GameplayTiming = frameRateContract.Mode;
    runtime.GameplayClock =
        frameRatePolicy.GameplayClock().CaptureState();
    runtime.PresentationSchedulerAvailable = true;
    runtime.PresentationScheduler = presentationScheduler.CaptureState();
    runtime.FrameRatePolicyTemporalStateAvailable = true;
    runtime.FrameRatePolicyTemporalState =
        frameRatePolicy.CaptureTemporalState();
    runtime.TopScreenTemporalStateAvailable = true;
    auto &topScreenState = runtime.TopScreenTemporalState;
    topScreenState.ProfileActive =
        launch.UiProfile == Oot3dNativeGame::Oot3dUiProfile::TopScreen;
    topScreenState.PausePageRedrawActive =
        widescreenProjection.TopScreenPausePageRedrawActive;
    topScreenState.PausePageRedrawDelayCalls =
        widescreenProjection.TopScreenPausePageRedraw.DelayCalls;
    topScreenState.PauseDrawNativeTransitionLatched =
        widescreenProjection.TopScreenPauseDrawRouting
            .NativeTransitionLatched;
    topScreenState.PauseDrawSuppressionDelayArmed =
        widescreenProjection.TopScreenPauseDrawRouting.SuppressionDelayArmed;
    topScreenState.PauseDrawSuppressionDelayCommands =
        widescreenProjection.TopScreenPauseDrawRouting
            .SuppressionDelayCommands;
    topScreenState.PauseRoutePreviousRuntimeMode =
        nativeCandidateDispatch.TopScreenPauseRoute.PreviousRuntimeMode;
    topScreenState.PauseRouteTransitionPhase =
        nativeCandidateDispatch.TopScreenPauseRoute.TransitionPhase;
    topScreenState.PauseRouteRemainingCalls =
        nativeCandidateDispatch.TopScreenPauseRoute.RemainingCalls;
    topScreenState.AotPauseRoutePreviousRuntimeMode =
        nativeCandidateDispatch.TopScreenAotPauseRoute.PreviousRuntimeMode;
    topScreenState.AotPauseRouteTransitionPhase =
        nativeCandidateDispatch.TopScreenAotPauseRoute.TransitionPhase;
    topScreenState.AotPauseRouteRemainingCalls =
        nativeCandidateDispatch.TopScreenAotPauseRoute.RemainingCalls;
    topScreenState.TouchCoordinateRuntimeSceneLatch =
        nativeCandidateDispatch.TopScreenTouchCoordinateRoute
            .RuntimeSceneLatch;
    Oot3dNativeGame::Oot3dPicaVisualReplayState visualReplayState;
    visualReplayState.Scheduler =
        picaPresentationScheduler.CaptureState();
    visualReplayState.Continuity =
        visualContinuityTracker.CaptureState();
    visualReplayState.PreviousFrame = previousVisualFrame;
    visualReplayState.LatestFrame = latestVisualFrame;
    visualReplayState.LatestTransitionContinuous =
        latestVisualTransitionContinuous;
    visualReplayState.DisplayTransfersByOutput =
        displayTransfersByOutput;
    visualReplayState.LastSelectedTopTransferCompletionId =
        lastSelectedTopTransferCompletionId;
    visualReplayState.LastSubmittedDrawId = lastSubmittedDrawId;
    if (!Oot3dNativeGame::EncodeOot3dPicaVisualReplayState(
            visualReplayState, runtime.PicaVisualReplayState, &error)) {
      std::cerr
          << "oot3d_native_game: native PICA visual replay capture failed: "
          << error << '\n';
      return false;
    }
    runtime.PicaVisualReplayStateAvailable = true;
    savestatePicaVisualReplayBytesCaptured +=
        runtime.PicaVisualReplayState.size();
    if (!api.CapturePicaTextureCache(
            runtime.PicaTextureCache, &error)) {
      std::cerr
          << "oot3d_native_game: native PICA texture capture failed: "
          << error << '\n';
      return false;
    }
    runtime.PicaTextureCacheAvailable = true;
    for (const auto &texture : runtime.PicaTextureCache) {
      savestatePicaTextureBytesCaptured += texture.PixelBytes.size();
    }
    savestatePicaTexturesCaptured += runtime.PicaTextureCache.size();
    if (!api.CapturePicaColorTargets(
            runtime.PicaColorTargets, &error)) {
      std::cerr
          << "oot3d_native_game: native PICA color capture failed: "
          << error << '\n';
      return false;
    }
    runtime.PicaColorTargetsAvailable = true;
    for (const auto &target : runtime.PicaColorTargets) {
      savestatePicaColorTargetBytesCaptured +=
          target.ColorRgba8.size();
    }
    savestatePicaColorTargetsCaptured += runtime.PicaColorTargets.size();
    if (!api.CapturePicaPresentationState(
            runtime.PicaPresentationState, &error)) {
      std::cerr
          << "oot3d_native_game: native PICA presentation capture failed: "
          << error << '\n';
      return false;
    }
    runtime.PicaPresentationStateAvailable = true;
    for (const auto &image :
         runtime.PicaPresentationState.DisplayImages) {
      savestatePicaDisplayImageBytesCaptured +=
          image.ColorRgba8.size();
    }
    savestatePicaDisplayImagesCaptured +=
        runtime.PicaPresentationState.DisplayImages.size();
    Oot3dNativeGame::NativeA32SavestateIoResult ioResult;
    if (!Oot3dNativeGame::SaveNativeA32State(
            path, savestateCompatibility, runtime, process, hostServices,
            picaFrontend, submissionQueue, dspHle, &ioResult, &error)) {
      std::cerr << "oot3d_native_game: savestate save failed: " << error
                << '\n';
      return false;
    }
    ++savestateSaveCount;
    savestateSaveBytes += ioResult.FileBytes;
    lastSavestateSemanticFingerprint = ioResult.SemanticFingerprint;
    savestateSaveSeconds += SecondsSince(started);
    savestateCaptureSeconds += ioResult.CaptureSeconds;
    savestateEncodeSeconds += ioResult.EncodeSeconds;
    savestateWriteSeconds += ioResult.IoSeconds;
    std::cout << "oot3d_native_game: saved state at frame " << frameCount
              << " to " << path.string() << '\n';
    return true;
  };
  const auto loadState = [&](const std::filesystem::path &path) {
    const auto started = std::chrono::steady_clock::now();
    Oot3dNativeGame::NativeA32SavestateRuntimeState runtime;
    Oot3dNativeGame::NativeA32SavestateIoResult ioResult;
    if (!Oot3dNativeGame::LoadNativeA32State(
            path, savestateCompatibility, runtime, process, hostServices,
            picaFrontend, submissionQueue, dspHle, &ioResult, &error)) {
      std::cerr << "oot3d_native_game: savestate load failed: " << error
                << '\n';
      return false;
    }
    Oot3dNativeGame::ResetOot3dTypedGameplayTransientState();
    picaCompositionTracker.Reset();
    applySelectedUiProfile();
    if (!api.ResetPicaState(&error)) {
      throw std::runtime_error(
          "native PICA reset after savestate load failed: " + error);
    }
    if (titleRenderBackend != nullptr &&
        !titleRenderBackend->ResetTitleState(&error)) {
      throw std::runtime_error(
          "OOT3D renderer reset after savestate load failed: " + error);
    }
    restoreSavestatePicaTextureCache(runtime);
    restoreSavestatePicaColorTargets(runtime);
    restoreSavestatePicaPresentationState(runtime);
    if (audioPlayer != nullptr && audioPlayer->IsInitialized()) {
      audioPlayer->Flush();
    }
    frameCount = runtime.FrameCount;
    refreshTickRemainder = runtime.RefreshTickRemainder;
    nextVblankTick = runtime.NextVblankTick;
    processResult = {};
    processResult.Kind = static_cast<Oot3dNativeGame::NativeA32ProcessRunKind>(
        runtime.ProcessRunKind);
    restoreSavestateTiming(runtime);
    picaFrontend.SetPacketSink(&submissionQueue);
    uiLifecycleBridge.ResetAfterStateLoad(frameCount);
    uiLifecycleBridge.SetTopScreenConfig(activeTopScreenConfig);
    widescreenProjection.TopScreenPauseDrawRouting = {};
    widescreenProjection.TopScreenPausePageRedraw = {};
    widescreenProjection.TopScreenPausePageRedrawActive = false;
    widescreenProjection.TopScreenPausePageCompositionCalls = 0U;
    widescreenProjection.TopScreenPausePageRedraws = 0U;
    widescreenProjection.TopScreenPausePageRedrawSkips = 0U;
    widescreenProjection.PendingTopScreenPauseChildRestore.reset();
    nativeCandidateDispatch.TopScreenInput = {};
    nativeCandidateDispatch.TopScreenGameplayActionRuntime = {};
    nativeCandidateDispatch.PreviousTopScreenZrHeld = false;
    nativeCandidateDispatch.PreviousTopScreenZlHeld = false;
    nativeCandidateDispatch.TopScreenZrPressLatched = false;
    nativeCandidateDispatch.TopScreenZlPressLatched = false;
    nativeCandidateDispatch.TopScreenZrPressLatchFrames = 0U;
    nativeCandidateDispatch.TopScreenZlPressLatchFrames = 0U;
    nativeCandidateDispatch.PreviousPhysicalZrHeld = false;
    nativeCandidateDispatch.PreviousPhysicalZlHeld = false;
    nativeCandidateDispatch.TopScreenZrHostEdgePending = false;
    nativeCandidateDispatch.TopScreenZlHostEdgePending = false;
    nativeCandidateDispatch.PreviousTopScreenButtons = 0U;
    nativeCandidateDispatch.StartButtonLatch = {};
    nativeCandidateDispatch.TopScreenStartRouting = {};
    nativeCandidateDispatch.TopScreenPauseSystemOpen = {};
    nativeCandidateDispatch.TopScreenViewportDrawPhase = 0U;
    nativeCandidateDispatch.TopScreenViewportDrawArgument = 0U;
    nativeCandidateDispatch.TopScreenSceneViewportDrawPhase = 0U;
    nativeCandidateDispatch.TopScreenSceneViewportDrawArgument = 0U;
    nativeCandidateDispatch.TopScreenOverlayViewportDrawPhase = 0U;
    nativeCandidateDispatch.TopScreenTouchCoordinateUpdatePhase = 0U;
    nativeCandidateDispatch.TopScreenAlternateRendererRestore.reset();
    nativeCandidateDispatch.TopScreenPauseRoute = {};
    nativeCandidateDispatch.TopScreenTouchCoordinateRoute = {};
    nativeCandidateDispatch.TopScreenRendererVisibilityRoute = {};
    nativeCandidateDispatch.TopScreenPauseController = {};
    nativeCandidateDispatch.TopScreenPauseControllerDrawPhase = 0U;
    nativeCandidateDispatch.TopScreenPauseControllerVisibleRenderers = {};
    nativeCandidateDispatch.PendingTopScreenItemsSelection = 0U;
    nativeCandidateDispatch.TopScreenCamera = {};
    nativeCandidateDispatch.TopScreenCameraInput.Reset();
    Oot3dNativeGame::ApplyTopScreenFreeCameraConfig(
        nativeCandidateDispatch.TopScreenCamera.Camera,
        activeTopScreenConfig);
    nativeCandidateDispatch.TopScreenExtendedCameraActive = false;
    restoreSavestateTopScreenTemporalState(runtime);
    sceneViewProbe.Reset();
    Fast::Oot3d::GrassInteractionBridge::Instance().Reset();
    temporalEventLedger.Reset();
    lastPublishedSceneViewSerial = 0;
    n64UiRenderer.Release();
    picaPresentationScheduler.Reset();
    visualContinuityTracker = {};
    preparedVisualTransition.Reset();
    latestVisualFrame.reset();
    previousVisualFrame.reset();
    latestVisualTransitionContinuous = false;
    visualFrameSampleScratch = {};
    displayTransfersByOutput.clear();
    drainedDraws.clear();
    lastSubmittedDrawId = 0;
    lastSelectedTopTransferCompletionId = 0;
    ++visualContinuityEpoch;
    restoreSavestatePicaVisualReplayState(runtime);
    pcmContinuityDiagnostics = {};
    realtimePacer.ResetDeadline();
    lastPresentationTime = std::chrono::steady_clock::now();
    ++savestateLoadCount;
    savestateLoadBytes += ioResult.FileBytes;
    savestateLoadSeconds += SecondsSince(started);
    savestateReadSeconds += ioResult.IoSeconds;
    savestateDecodeSeconds += ioResult.DecodeSeconds;
    savestateRestoreSeconds += ioResult.RestoreSeconds;
    std::cout << "oot3d_native_game: loaded state at frame " << frameCount
              << " from " << path.string() << '\n';
    return true;
  };

  auto benchmarkMeasurementStart = std::chrono::steady_clock::now();
  auto benchmarkMeasurementEnd = benchmarkMeasurementStart;
  uint64_t benchmarkMeasuredFrames = 0U;
  bool benchmarkMeasurementStarted =
      hostArgs.BenchmarkWarmupFrames == 0U;

#if defined(__SWITCH__)
  WriteSwitchBootStage("runtime_loop");
#endif
  while (window.IsRunning() &&
         processResult.Kind !=
             Oot3dNativeGame::NativeA32ProcessRunKind::Terminated) {
    widescreenProjection.CurrentHostFrame =
        static_cast<uint32_t>(presentationFrameCount);
    WindowDemoFrameTiming frameTiming;
    if (!PrepareNextWindowDemoFrame(hostArgs, window, timing, frameTiming)) {
      continue;
    }
    const auto requestedPresentationPolicy =
        Fast::Oot3d::ResolvePresentationPacingPolicy(
            Fast::Oot3d::GraphicsSettingsRuntime::Instance()
                .Snapshot()
                .FrameRate);
    if (Fast::Oot3d::PresentationPacingPolicyChanged(
            presentationPacingPolicy, requestedPresentationPolicy)) {
      const auto requestedCompositionPolicy =
          resolveFrameCompositionPolicy(requestedPresentationPolicy);
      const bool previousInterpolationEnabled = visualInterpolationEnabled;
      const bool requestedInterpolationEnabled =
          Oot3dNativeGame::ShouldUseNativeVisualInterpolation(
              frameRateContract,
              requestedCompositionPolicy.InterpolationEnabled());
      const bool compositionModeChanged =
          frameCompositionPolicy.Interpolation !=
              requestedCompositionPolicy.Interpolation ||
          frameCompositionPolicy.FixedSampleMultiplier !=
              requestedCompositionPolicy.FixedSampleMultiplier;
      presentationPacingPolicy = requestedPresentationPolicy;
      frameCompositionPolicy = requestedCompositionPolicy;
      realtimePacer.Configure(presentationPacingPolicy.Enabled &&
                                  !hostArgs.ThroughputBenchmark,
                              presentationPacingPolicy.TargetRateHz);
      visualSampleCadence.Configure(
          frameCompositionPolicy.FixedMultiplier()
              ? frameCompositionPolicy.FixedSampleMultiplier
              : 0U);
      visualInterpolationEnabled = requestedInterpolationEnabled;
      if (compositionModeChanged) {
        ++visualContinuityEpoch;
      }
      if (previousInterpolationEnabled != visualInterpolationEnabled) {
        preparedVisualTransition.Reset();
        visualContinuityTracker = {};
        previousVisualFrame.reset();
        latestVisualFrame.reset();
        latestVisualTransitionContinuous = false;
        visualSampleCadence.Reset();
      }
    }
    const auto presentationTime = std::chrono::steady_clock::now();
    realtimePacer.ObservePresentation(presentationTime);
    const double presentationElapsedSeconds =
        hostArgs.FixedDeltaSeconds > 0.0
            ? hostArgs.FixedDeltaSeconds
            : std::chrono::duration<double>(presentationTime -
                                            lastPresentationTime)
                  .count();
    lastPresentationTime = presentationTime;
    const auto presentationStep =
        presentationScheduler.Advance(presentationElapsedSeconds);
    const uint32_t guestRefreshesDue = presentationStep.GuestRefreshesDue;
    const float visualInterpolationAlpha = static_cast<float>(
        std::clamp(presentationStep.InterpolationAlpha, 0.0, 1.0));
    applyTopScreenConfigSnapshot();
    uiLifecycleBridge.BeginHostFrame(
        static_cast<uint32_t>(presentationFrameCount));
    if (topScreenTextureOverrideRuntime.has_value()) {
      topScreenTextureOverrideRuntime->Observe(uiLifecycleBridge);
    }

    const uint32_t width = std::max<uint32_t>(1, window.GetWidth());
    const uint32_t height = std::max<uint32_t>(1, window.GetHeight());
    updateScenePresentation();
    phaseStart = std::chrono::steady_clock::now();
    window.GetMouseStateManager()->StartFrame();
    gui->StartDraw();
    window.StartFrame();
    const bool quickSaveKeyDown = window.IsKeyDown(Ship::KbScancode::LUS_KB_F5);
    const bool quickLoadKeyDown = window.IsKeyDown(Ship::KbScancode::LUS_KB_F8);
    quickSavePending =
        quickSavePending || (quickSaveKeyDown && !quickSaveKeyWasDown);
    const bool quickLoadRequested = quickLoadKeyDown && !quickLoadKeyWasDown;
    quickSaveKeyWasDown = quickSaveKeyDown;
    quickLoadKeyWasDown = quickLoadKeyDown;
    bool nativeFrontendTouchEnabled = ResolveNativeA32TouchInputEnabled(
        process.Memory(), uiLifecycleBridge,
        nativeCandidateDispatch.TopScreenUiProfile);
    uint32_t topScreenNativeGameMode = UINT32_MAX;
    const bool topScreenGameplayInputMode =
        process.Memory().Read32(kNativeGameMode, &topScreenNativeGameMode) &&
        Oot3dNativeGame::IsTopScreenGameplayInputMode(topScreenNativeGameMode);
    picaCompositionDomain =
        topScreenGameplayInputMode
            ? Oot3dNativeGame::Oot3dPicaCompositionDomain::Scene
            : Oot3dNativeGame::Oot3dPicaCompositionDomain::Unknown;
    bool topScreenStartRoutingEligible =
        uiLifecycleBridge.NativeGameplayPresentationActive() &&
        topScreenGameplayInputMode;
    Oot3dNativeGame::NativeAimProfileTransform aimProfileTransform;
    if (nativeCandidateDispatch.TopScreenUiProfile) {
      const auto topScreenAim =
          Oot3dNativeGame::ResolveTopScreenCStickAimPolicy(
              activeTopScreenConfig);
      aimProfileTransform.RightStickScale = topScreenAim.SpeedMultiplier;
      aimProfileTransform.RightStickSmoothingCoefficient =
          Oot3dNativeGame::ResolveTopScreenCStickSmoothingCoefficient(
              activeTopScreenConfig.FreeCameraSmoothing);
      aimProfileTransform.RightStickInvertX = topScreenAim.InvertX;
      aimProfileTransform.RightStickInvertY = topScreenAim.InvertY;
    }
    const bool hostGuiVisible =
        gui->GetMenuOrMenubarVisible() || gui->GetAnyGuiWindowVisible();
    auto physicalInputFrame =
        PollNativeA32Input(window, nativeFrontendTouchEnabled,
                           hostGuiVisible,
                           nativeCandidateDispatch.TopScreenUiProfile,
                           activeTopScreenConfig.FreeCameraEnabled,
                           *controlConfigRuntime, aimProfileTransform,
                           presentationElapsedSeconds,
                           guestRefreshesDue != 0U,
                           nativeControlPollingState);
    if (physicalInputFrame.Exit) {
      window.Close();
      break;
    }
    if (!inputTimeline.Enabled()) {
      const uint32_t start = Oot3dNativeGame::NativeA32HidButtonMask(
          Oot3dNativeGame::NativeA32HidButton::Start);
      const bool physicalStartHeld =
          (physicalInputFrame.Hid.Buttons & start) != 0U;
      Oot3dNativeGame::ObserveNativeA32PolledButton(
          physicalStartHeld, nativeCandidateDispatch.StartButtonLatch);
      Oot3dNativeGame::ReleaseTopScreenStartRoutingLatch(
          physicalStartHeld, nativeCandidateDispatch.TopScreenStartRouting);
    }
    {
      // Host-rate trigger edges. A tap shorter than one guest refresh would
      // never be seen by the guest-rate edge below; carry it forward.
      const bool zrPhysical = ThreeDsRecomp::Input::IsButtonHeld(
          physicalInputFrame, ThreeDsRecomp::Input::Button::Zr);
      const bool zlPhysical = ThreeDsRecomp::Input::IsButtonHeld(
          physicalInputFrame, ThreeDsRecomp::Input::Button::Zl);
      if (zrPhysical && !nativeCandidateDispatch.PreviousPhysicalZrHeld) {
        ++nativeCandidateDispatch.TopScreenZrHostEdges;
        nativeCandidateDispatch.TopScreenZrHostEdgePending = true;
      }
      if (zlPhysical && !nativeCandidateDispatch.PreviousPhysicalZlHeld) {
        ++nativeCandidateDispatch.TopScreenZlHostEdges;
        nativeCandidateDispatch.TopScreenZlHostEdgePending = true;
      }
      nativeCandidateDispatch.PreviousPhysicalZrHeld = zrPhysical;
      nativeCandidateDispatch.PreviousPhysicalZlHeld = zlPhysical;
    }
    Oot3dNativeGame::NativeA32InputFrame inputFrame;
    api.UpdateFramebufferParameters(0, width, height, 1, false, true, true,
                                    true);
    api.StartFrame();
    picaPresentationScheduler.BeginPresentation(presentationFrameCount);
    api.StartDrawToFramebuffer(0, 1.0F);
    api.SetClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    api.ClearFramebuffer(true, true);
    api.SetViewport(0, 0, static_cast<int>(width), static_cast<int>(height));
    api.SetScissor(0, 0, static_cast<int>(width), static_cast<int>(height));
    phaseTiming.FrameStartSeconds += SecondsSince(phaseStart);
    const uint32_t guestRefreshIterations =
        std::max<uint32_t>(1U, guestRefreshesDue);
    for (uint32_t guestRefreshIndex = 0;
         guestRefreshIndex < guestRefreshIterations; ++guestRefreshIndex) {
      const bool advanceGuest = guestRefreshIndex < guestRefreshesDue;
      const bool presentHostFrame =
          guestRefreshIndex + 1U == guestRefreshIterations;
      if (advanceGuest) {
        if (guestRefreshIndex != 0U) {
          nativeFrontendTouchEnabled = ResolveNativeA32TouchInputEnabled(
              process.Memory(), uiLifecycleBridge,
              nativeCandidateDispatch.TopScreenUiProfile);
          topScreenNativeGameMode = UINT32_MAX;
          const bool refreshedTopScreenGameplayInputMode =
              process.Memory().Read32(kNativeGameMode,
                                      &topScreenNativeGameMode) &&
              Oot3dNativeGame::IsTopScreenGameplayInputMode(
                  topScreenNativeGameMode);
          topScreenStartRoutingEligible =
              uiLifecycleBridge.NativeGameplayPresentationActive() &&
              refreshedTopScreenGameplayInputMode;
          picaCompositionDomain =
              refreshedTopScreenGameplayInputMode
                  ? Oot3dNativeGame::Oot3dPicaCompositionDomain::Scene
                  : Oot3dNativeGame::Oot3dPicaCompositionDomain::Unknown;
        }
        inputFrame = ResolveNativeA32GuestInput(
            physicalInputFrame, inputTimeline, frameCount, runFrameCount,
            nativeFrontendTouchEnabled, topScreenStartRoutingEligible,
            nativeCandidateDispatch.TopScreenUiProfile,
            nativeCandidateDispatch.StartButtonLatch,
            nativeCandidateDispatch.TopScreenStartRouting);
        inputDiagnostics.Observe(inputFrame);
        if (inputFrame.Exit) {
          window.Close();
          break;
        }
      }
      if (advanceGuest) {
        const uint32_t topScreenButtons = inputFrame.Hid.Buttons;
        const uint32_t topScreenPressed =
            topScreenButtons &
            ~nativeCandidateDispatch.PreviousTopScreenButtons;
        const auto topScreenButton =
            [](Oot3dNativeGame::NativeA32HidButton value) {
              return Oot3dNativeGame::NativeA32HidButtonMask(value);
            };
        Oot3dNativeGame::TopScreenDpadActionState topScreenDpadActions;
        if (nativeCandidateDispatch.TopScreenUiProfile &&
            topScreenStartRoutingEligible) {
          bool childLink = false;
          std::string childLinkError;
          if (!Oot3dNativeGame::ReadTopScreenChildLink(
                  process.Memory(), &childLink, &childLinkError)) {
            throw std::runtime_error("TopScreen Link age decode failed: " +
                                     childLinkError);
          }
          Oot3dNativeGame::TopScreenDpadPhysicalState dpadPhysical;
          dpadPhysical.ChildLink = childLink;
          constexpr std::array<Oot3dNativeGame::NativeA32HidButton, 4>
              kDpadButtons{Oot3dNativeGame::NativeA32HidButton::DpadUp,
                           Oot3dNativeGame::NativeA32HidButton::DpadDown,
                           Oot3dNativeGame::NativeA32HidButton::DpadLeft,
                           Oot3dNativeGame::NativeA32HidButton::DpadRight};
          for (std::size_t direction = 0U;
               direction < kDpadButtons.size(); ++direction) {
            const auto mask = topScreenButton(kDpadButtons[direction]);
            dpadPhysical.Pressed[direction] =
                (topScreenPressed & mask) != 0U;
            dpadPhysical.Held[direction] =
                (topScreenButtons & mask) != 0U;
          }
          topScreenDpadActions = Oot3dNativeGame::ResolveTopScreenDpadActions(
              activeTopScreenConfig, dpadPhysical);
        }
        const bool zrHeld = ThreeDsRecomp::Input::IsButtonHeld(
            inputFrame, ThreeDsRecomp::Input::Button::Zr);
        const bool zlHeld = ThreeDsRecomp::Input::IsButtonHeld(
            inputFrame, ThreeDsRecomp::Input::Button::Zl);
        // Any host-rate rising edge since the last guest refresh counts; a
        // timeline drives the guest frame directly and has no host edges.
        const bool zrEdge =
            (zrHeld && !nativeCandidateDispatch.PreviousTopScreenZrHeld) ||
            (!inputTimeline.Enabled() &&
             nativeCandidateDispatch.TopScreenZrHostEdgePending) ||
            topScreenDpadActions.WasPressed(
                Oot3dNativeGame::TopScreenDpadAction::ItemZr);
        const bool zlEdge =
            (zlHeld && !nativeCandidateDispatch.PreviousTopScreenZlHeld) ||
            (!inputTimeline.Enabled() &&
             nativeCandidateDispatch.TopScreenZlHostEdgePending) ||
            topScreenDpadActions.WasPressed(
                Oot3dNativeGame::TopScreenDpadAction::ItemZl);
        nativeCandidateDispatch.TopScreenZrHostEdgePending = false;
        nativeCandidateDispatch.TopScreenZlHostEdgePending = false;
        nativeCandidateDispatch.TopScreenZrGuestEdges += zrEdge ? 1U : 0U;
        nativeCandidateDispatch.TopScreenZlGuestEdges += zlEdge ? 1U : 0U;
        // Keep a press pending until the guest's pressed getter consumes it
        // (see the item-query hook), surviving a release that happens before
        // the game polls. Expire after a few guest frames so a press made
        // outside gameplay cannot fire later.
        constexpr uint32_t kTopScreenPressLatchFrames = 6U;
        const auto latch = [](bool edge, uint32_t &framesLeft) {
          if (edge) {
            framesLeft = kTopScreenPressLatchFrames;
          } else if (framesLeft != 0U) {
            --framesLeft;
          }
          return framesLeft != 0U;
        };
        nativeCandidateDispatch.TopScreenZrPressLatched =
            latch(zrEdge, nativeCandidateDispatch.TopScreenZrPressLatchFrames);
        nativeCandidateDispatch.TopScreenZlPressLatched =
            latch(zlEdge, nativeCandidateDispatch.TopScreenZlPressLatchFrames);
        nativeCandidateDispatch.TopScreenInput = {
            .ZrPressed = nativeCandidateDispatch.TopScreenZrPressLatched,
            .ZlPressed = nativeCandidateDispatch.TopScreenZlPressLatched,
            .ZrHeld =
                zrHeld ||
                topScreenDpadActions.IsHeld(
                    Oot3dNativeGame::TopScreenDpadAction::ItemZr),
            .ZlHeld =
                zlHeld ||
                topScreenDpadActions.IsHeld(
                    Oot3dNativeGame::TopScreenDpadAction::ItemZl),
            .DpadLeftHeld =
                (inputFrame.Hid.Buttons &
                 Oot3dNativeGame::NativeA32HidButtonMask(
                     Oot3dNativeGame::NativeA32HidButton::DpadLeft)) != 0U,
            .DpadRightHeld =
                (inputFrame.Hid.Buttons &
                 Oot3dNativeGame::NativeA32HidButtonMask(
                     Oot3dNativeGame::NativeA32HidButton::DpadRight)) != 0U};
        nativeCandidateDispatch.TopScreenInput.XPressed =
            (topScreenPressed &
             topScreenButton(Oot3dNativeGame::NativeA32HidButton::X)) != 0U;
        nativeCandidateDispatch.TopScreenInput.YPressed =
            (topScreenPressed &
             topScreenButton(Oot3dNativeGame::NativeA32HidButton::Y)) != 0U;
        nativeCandidateDispatch.TopScreenInput.XHeld =
            (topScreenButtons &
             topScreenButton(Oot3dNativeGame::NativeA32HidButton::X)) != 0U;
        nativeCandidateDispatch.TopScreenInput.YHeld =
            (topScreenButtons &
             topScreenButton(Oot3dNativeGame::NativeA32HidButton::Y)) != 0U;
        nativeCandidateDispatch.TopScreenInput.RestorationLayout =
            activeTopScreenConfig.HudLayout ==
            Oot3dNativeGame::TopScreenHudLayout::Restoration;
        uiLifecycleBridge.SetTopScreenInputFrame(
            nativeCandidateDispatch.TopScreenInput);
        nativeCandidateDispatch.PreviousTopScreenZrHeld =
            zrHeld;
        nativeCandidateDispatch.PreviousTopScreenZlHeld =
            zlHeld;
        nativeCandidateDispatch.TopScreenZrHeldFrames += zrHeld ? 1U : 0U;
        nativeCandidateDispatch.TopScreenZlHeldFrames += zlHeld ? 1U : 0U;
        if (nativeCandidateDispatch.TopScreenUiProfile &&
            activeTopScreenConfig.FreeCameraEnabled &&
            nativeControlPollingState.GameplayMouseOwned) {
          nativeCandidateDispatch.TopScreenCameraInput.Observe(
              inputFrame.CStick);
        } else if (inputFrame.CStick.Kind ==
                   Oot3dNativeGame::NativeFreeCameraInputKind::Absolute) {
          nativeCandidateDispatch.TopScreenCameraInput.Observe(
              inputFrame.CStick);
        } else {
          nativeCandidateDispatch.TopScreenCameraInput.Reset();
        }
        if (nativeCandidateDispatch.TopScreenUiProfile) {
          const uint32_t buttons = inputFrame.Hid.Buttons;
          const uint32_t pressed =
              buttons & ~nativeCandidateDispatch.PreviousTopScreenButtons;
          const auto button = [](Oot3dNativeGame::NativeA32HidButton value) {
            return Oot3dNativeGame::NativeA32HidButtonMask(value);
          };
          const bool gameplayViewPressed = topScreenDpadActions.WasPressed(
              Oot3dNativeGame::TopScreenDpadAction::View);
          const bool gameplayOcarinaPressed =
              topScreenDpadActions.WasPressed(
                  Oot3dNativeGame::TopScreenDpadAction::Ocarina);
          const bool leftShoulderHeld =
              (buttons &
               button(Oot3dNativeGame::NativeA32HidButton::L)) != 0U;
          const bool rightShoulderHeld =
              (buttons &
               button(Oot3dNativeGame::NativeA32HidButton::R)) != 0U;
          const bool selectPressed =
              (pressed &
               button(Oot3dNativeGame::NativeA32HidButton::Select)) != 0U;
          if (topScreenDpadActions.WasPressed(
                  Oot3dNativeGame::TopScreenDpadAction::MinimapToggle) ||
              (selectPressed &&
               activeTopScreenConfig.SelectAction ==
                   Oot3dNativeGame::TopScreenSelectAction::MinimapToggle)) {
            setTopScreenSessionMinimapVisible(
                !activeTopScreenConfig.MinimapVisible);
          }
          if (selectPressed &&
              activeTopScreenConfig.SelectAction ==
                  Oot3dNativeGame::TopScreenSelectAction::MinimapToggle) {
            inputFrame.Hid.Buttons &=
                ~button(Oot3dNativeGame::NativeA32HidButton::Select);
          }

          if (gameplayViewPressed || gameplayOcarinaPressed) {
            ++nativeCandidateDispatch.TopScreenGameplayDpadAttempts;
            Oot3dNativeGame::TopScreenGameplayDpadResult dpadResult;
            std::string dpadError;
            if (!Oot3dNativeGame::PrepareTopScreenGameplayDpadGuest(
                    process.Memory(),
                    {.ViewPressed = gameplayViewPressed,
                     .OcarinaPressed = gameplayOcarinaPressed,
                     .LeftShoulderHeld = leftShoulderHeld,
                     .RightShoulderHeld = rightShoulderHeld},
                    &dpadResult, &dpadError)) {
              throw std::runtime_error(
                  "TopScreen gameplay D-pad prepare failed: " + dpadError);
            }
            nativeCandidateDispatch.TopScreenNaviViewActivations +=
                dpadResult.NaviViewActivated ? 1U : 0U;
            if (dpadResult.OcarinaQueryRequired) {
              ++nativeCandidateDispatch.TopScreenOcarinaQueries;
              uint32_t nativeEligibility = 0U;
              constexpr std::array<std::uint32_t, 0> kNoArguments{};
              if (!process.InvokeFunctionWithResult(
                      kTopScreenOcarinaEligibility, kNoArguments,
                      kTopScreenDirectCallReturn, &nativeEligibility,
                      &dpadError) ||
                  !Oot3dNativeGame::CommitTopScreenGameplayOcarinaGuest(
                      process.Memory(), nativeEligibility, &dpadResult,
                      &dpadError)) {
                throw std::runtime_error(
                    "TopScreen ocarina D-pad route failed: " + dpadError);
              }
              nativeCandidateDispatch.TopScreenOcarinaActivations +=
                  dpadResult.OcarinaActivated ? 1U : 0U;
            }
          }
          std::string gameplayActionError;
          if (!Oot3dNativeGame::ConsumeTopScreenGameplayActions(
                  process, topScreenDpadActions,
                  nativeCandidateDispatch.TopScreenInput.ZrPressed ||
                      nativeCandidateDispatch.TopScreenInput.ZlPressed,
                  kTopScreenDirectCallReturn,
                  &nativeCandidateDispatch.TopScreenGameplayActionRuntime,
                  &nativeCandidateDispatch.TopScreenGameplayActions,
                  &gameplayActionError)) {
            throw std::runtime_error(
                "TopScreen gameplay action failed: " +
                gameplayActionError);
          }
          nativeCandidateDispatch.PreviousTopScreenButtons = buttons;
          Oot3dNativeGame::TopScreenPauseDrawInputs pauseInputs;
          if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(process.Memory(),
                                                             &pauseInputs)) {
            nativeCandidateDispatch.TopScreenPauseAotBarrier = true;
          } else {
            const auto closePlan =
                Oot3dNativeGame::ResolveTopScreenPauseStartClose(
                    pauseInputs,
                    (pressed &
                     button(Oot3dNativeGame::NativeA32HidButton::Start)) != 0U);
            if (closePlan.Page !=
                Oot3dNativeGame::TopScreenPauseClosePage::None) {
              std::string closeError;
              if (!ApplyTopScreenPauseClose(process, closePlan, &closeError)) {
                throw std::runtime_error(
                    "TopScreen START pause-close transaction failed: " +
                    closeError);
              }
              Oot3dNativeGame::MarkTopScreenStartCloseGestureConsumed(
                  nativeCandidateDispatch.TopScreenStartRouting);
              inputFrame.Hid.Buttons &=
                  ~button(Oot3dNativeGame::NativeA32HidButton::Start);
              ++nativeCandidateDispatch.TopScreenPauseStartCloseCalls;
              nativeCandidateDispatch.TopScreenPauseLastClosedPage =
                  closePlan.Page;
              if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(
                      process.Memory(), &pauseInputs)) {
                throw std::runtime_error("TopScreen pause state could not be "
                                         "read after START close");
              }
            }
            if (activeTopScreenConfig.ExitItemsToSaveScreen) {
              const auto systemOpenPlan =
                  Oot3dNativeGame::ResolveTopScreenPauseBSystemOpen(
                      pauseInputs,
                      (pressed &
                       button(Oot3dNativeGame::NativeA32HidButton::B)) != 0U,
                      nativeCandidateDispatch.TopScreenPauseSystemOpen);
              if (systemOpenPlan.Page !=
                  Oot3dNativeGame::TopScreenPauseClosePage::None) {
                std::string systemOpenError;
                if (!ApplyTopScreenPauseSystemOpen(
                        process, systemOpenPlan, &systemOpenError)) {
                  throw std::runtime_error(
                      "TopScreen B system-menu transaction failed: " +
                      systemOpenError);
                }
                ++nativeCandidateDispatch.TopScreenPauseSystemOpenCalls;
                nativeCandidateDispatch.TopScreenPauseSystemSourcePage =
                    systemOpenPlan.Page;
                if (!Oot3dNativeGame::ReadTopScreenPauseDrawInputs(
                        process.Memory(), &pauseInputs)) {
                  throw std::runtime_error("TopScreen pause state could not be "
                                           "read after B system-menu open");
                }
              }
            }
            const bool sceneRuntimeRoute = pauseInputs.HasScene &&
                                           pauseInputs.SceneMode == 3U &&
                                           (pauseInputs.RuntimeMode == 1U ||
                                            pauseInputs.RuntimeMode == 2U);
            nativeCandidateDispatch.TopScreenPauseAotBarrier =
                sceneRuntimeRoute ||
                Oot3dNativeGame::ResolveTopScreenPauseRouteActive(
                    pauseInputs,
                    &nativeCandidateDispatch.TopScreenAotPauseRoute);
          }
        }
        if (scenarioBootstrap.has_value() && !scenarioBootstrap->Finished()) {
          std::string scenarioError;
          if (!scenarioBootstrap->Advance(
                  process, sceneViewProbe.Stats().LastPlayStateAddress,
                  frameCount, &scenarioError)) {
            std::cerr << "oot3d_native_game: structural scenario failed: "
                      << scenarioError << '\n';
            if (launch.ScenarioStrict) {
              window.Close();
              break;
            }
          }
          if (launch.ScenarioAutoExit && scenarioBootstrap->Complete()) {
            window.Close();
            break;
          }
        }
        refreshTickRemainder += kCtrArm11TicksPerSecond;
        nextVblankTick += refreshTickRemainder / kGuestDisplayRefreshRate;
        refreshTickRemainder %= kGuestDisplayRefreshRate;
        const auto clockResolution = AdvanceGuestClockTo(
            process, hostServices, processResult, nextVblankTick,
            launch.WholeAotBlockBudget);
        if (clockResolution.OvershootTicks != 0U) {
          ++lateVblankDeadlineCount;
          maximumVblankDeadlineOvershootTicks =
              std::max(maximumVblankDeadlineOvershootTicks,
                       clockResolution.OvershootTicks);
        }
        const auto hidUpdate = hostServices.AdvanceHidToCurrentTick(
            process.Memory(), inputFrame.Hid);
        if (hidUpdate.Status ==
            Oot3dNativeGame::NativeA32CtrHidUpdateStatus::Failed) {
          throw std::runtime_error(
              "native HID sample could not be written to shared memory");
        }
        if (hidUpdate.EventsSignaled &&
            processResult.Kind !=
                Oot3dNativeGame::NativeA32ProcessRunKind::Terminated) {
          phaseStart = std::chrono::steady_clock::now();
          processResult =
              RunUntilGuestWait(process, launch.WholeAotBlockBudget);
          phaseTiming.GuestSeconds += SecondsSince(phaseStart);
          RequireRunnableGuest(processResult);
        }
        const uint32_t dspAudioFrames =
            hostServices.TakePendingDspAudioFrames();
        std::vector<int16_t> audioSamples;
        audioSamples.reserve(static_cast<size_t>(dspAudioFrames) * 320U);
        for (uint32_t audioFrame = 0; audioFrame < dspAudioFrames;
             ++audioFrame) {
          const size_t frameSampleOffset = audioSamples.size();
          const auto dspMixStart = std::chrono::steady_clock::now();
          if (!dspHle.ProcessFrame(process.Memory(), audioSamples, &error)) {
            throw std::runtime_error("native DSP HLE mix failed: " + error);
          }
          phaseTiming.DspMixSeconds += SecondsSince(dspMixStart);
          pcmContinuityDiagnostics.ObserveFrame(
              dspFramesMixed, std::span<const int16_t>(audioSamples)
                                  .subspan(frameSampleOffset));
          ++dspFramesMixed;
          dspFramesSuppressed += launch.DisableAudio ? 1U : 0U;
          if (!hostServices.SignalDspAudioFrame()) {
            break;
          }
          phaseStart = std::chrono::steady_clock::now();
          processResult =
              RunUntilGuestWait(process, launch.WholeAotBlockBudget);
          phaseTiming.GuestSeconds += SecondsSince(phaseStart);
          RequireRunnableGuest(processResult);
        }
        for (const int16_t sample : audioSamples) {
          const int32_t magnitude = std::abs(static_cast<int32_t>(sample));
          dspPeakMagnitude = std::max(dspPeakMagnitude, magnitude);
          dspNonzeroSamples += sample != 0 ? 1U : 0U;
          const uint16_t bits = static_cast<uint16_t>(sample);
          dspPcmFnv1a64 ^= static_cast<uint8_t>(bits);
          dspPcmFnv1a64 *= 1099511628211ULL;
          dspPcmFnv1a64 ^= static_cast<uint8_t>(bits >> 8U);
          dspPcmFnv1a64 *= 1099511628211ULL;
        }
        dspSamplesMixed += audioSamples.size() / 2U;
        if (!launch.AudioPcmDumpPath.empty()) {
          pcmDumpSamples.insert(pcmDumpSamples.end(), audioSamples.begin(),
                                audioSamples.end());
        }
        if (!launch.DisableAudio && audioPlayer != nullptr &&
            audioPlayer->IsInitialized() && !audioSamples.empty()) {
          const auto audioOutputStart = std::chrono::steady_clock::now();
          const int32_t bufferedBefore = audioPlayer->Buffered();
          audioPlayer->Play(
              reinterpret_cast<const uint8_t *>(audioSamples.data()),
              audioSamples.size() * sizeof(int16_t));
          audioOutputDiagnostics.Observe(
              frameCount, bufferedBefore, audioPlayer->Buffered(),
              audioSamples.size() / 2U, audioPlayer->GetDesiredBuffered());
          phaseTiming.AudioOutputSeconds += SecondsSince(audioOutputStart);
        }
        if (!hostServices.SignalVBlank(process.Memory())) {
          throw std::runtime_error(
              "native VBlank could not enter the GSP relay queue");
        }
        ++vblankCount;
      }
      if (presentHostFrame) {
        const auto visualPresentationStart = std::chrono::steady_clock::now();
        const bool lcdForceBlack = hostServices.LcdForceBlack();
        const auto topFramebuffer = hostServices.TopFramebuffer();
        const Oot3dNativeGame::Oot3dPicaDisplayTransferSubmission
            *traceSelectedTopTransfer = nullptr;
        if (topFramebuffer.has_value()) {
          auto traceSelected =
              displayTransfersByOutput.find(topFramebuffer->AddressLeft);
          if (traceSelected == displayTransfersByOutput.end() &&
              topFramebuffer->AddressRight != topFramebuffer->AddressLeft) {
            traceSelected =
                displayTransfersByOutput.find(topFramebuffer->AddressRight);
          }
          if (traceSelected != displayTransfersByOutput.end()) {
            traceSelectedTopTransfer = &traceSelected->second;
          }
        }
        picaSemanticTrace.RecordPresentationSelection(
            presentationFrameCount, frameCount, lcdForceBlack,
            topFramebuffer.has_value()
                ? std::optional<uint32_t>(topFramebuffer->AddressLeft)
                : std::nullopt,
            topFramebuffer.has_value()
                ? std::optional<uint32_t>(topFramebuffer->AddressRight)
                : std::nullopt,
            traceSelectedTopTransfer);
        if (!lcdForceBlack) {
          if (topFramebuffer.has_value()) {
            auto selected =
                displayTransfersByOutput.find(topFramebuffer->AddressLeft);
            if (selected == displayTransfersByOutput.end() &&
                topFramebuffer->AddressRight != topFramebuffer->AddressLeft) {
              selected =
                  displayTransfersByOutput.find(topFramebuffer->AddressRight);
            }
            if (selected != displayTransfersByOutput.end()) {
              bool presentedVisualSample = false;
              bool selectedNewVisualFrame = false;
              if (selected->second.CompletionId !=
                  lastSelectedTopTransferCompletionId) {
                lastSelectedTopTransferCompletionId =
                    selected->second.CompletionId;
                ++selectedTopTransferCount;
                auto currentVisualFrame =
                    picaPresentationScheduler.FinishFrame(selected->second);
                if (currentVisualFrame.has_value()) {
                  Oot3dNativeGame::PublishNativeActorInteractions(
                      process.Memory(), sceneViewProbe.Stats().LastPlayStateAddress, currentVisualFrame->Sequence);
                  Oot3dNativeGame::ComposeTopScreenFrontendFrame(
                      *currentVisualFrame,
                      Oot3dNativeGame::ShouldSuppressTopScreenFrontendBackdrop(
                          launch.UiProfile,
                          uiLifecycleBridge.NativeFrontendPresentationActive()));
                  selectedNewVisualFrame = true;
                  ++visualSnapshotCount;
                  auto &sample = visualFrameSampleScratch;
                  if (!visualInterpolationEnabled) {
                    if (Oot3dNativeGame::ViewOot3dPicaVisualFrame(
                            *currentVisualFrame, sample)) {
                      const auto temporalSample =
                          Fast::Oot3d::BuildNativeFrameTemporalSample(
                              frameCompositionPolicy,
                              Fast::Oot3d::NativeFrameTemporalSampleKind::
                                  Authoritative,
                              currentVisualFrame->Sequence,
                              currentVisualFrame->Sequence,
                              visualContinuityEpoch, 1.0F,
                              static_cast<float>(
                                  presentationElapsedSeconds));
                      executeVisualSample(
                          sample,
                          Oot3dNativeGame::
                              Oot3dPicaPresentationExecutionKind::CurrentFrame,
                          true, temporalSample);
                      ++visualDirectFramesPresented;
                      ++visualSamplesPresented;
                      visualSampleDrawCount += sample.Draws.size();
                      presentedVisualSample = true;
                    }
                    // Direct gameplay modes retain no cross-frame matching
                    // history. The scheduler keeps only the executed scanout
                    // snapshot needed if presentation temporarily outruns
                    // simulation.
                    preparedVisualTransition.Reset();
                    previousVisualFrame.reset();
                    latestVisualFrame.reset();
                    latestVisualTransitionContinuous = false;
                  } else {
                    bool resynchronizeVisualNamespace =
                        !latestVisualFrame.has_value();
                    if (latestVisualFrame.has_value()) {
                      const bool transitionPrepared =
                          preparedVisualTransition.Prepare(
                              *latestVisualFrame, *currentVisualFrame,
                              &visualInterpolationTiming);
                      const auto &transition =
                          preparedVisualTransition.Stats();
                      const float sampleAlpha =
                          visualSampleCadence.Resolve(
                              true, visualInterpolationAlpha);
                      const bool visualSampleReady =
                          transitionPrepared &&
                          preparedVisualTransition.Sample(
                              *latestVisualFrame, *currentVisualFrame,
                              sampleAlpha, sample,
                              &visualInterpolationTiming);
                      ++visualTransitionCount;
                      visualMatchedDrawCount += transition.MatchedDraws;
                      visualStrictUniqueMatchedDrawCount +=
                          transition.StrictUniqueMatchedDraws;
                      visualStrictOrdinalMatchedDrawCount +=
                          transition.StrictOrdinalMatchedDraws;
                      visualStructuralOrdinalMatchedDrawCount +=
                          transition.StructuralOrdinalMatchedDraws;
                      visualPipelineOrdinalMatchedDrawCount +=
                          transition.PipelineOrdinalMatchedDraws;
                      visualChangedContinuousDrawCount +=
                          transition.ChangedContinuousDraws;
                      visualChangedPerInstanceVertexDrawCount +=
                          transition.ChangedPerInstanceVertexDraws;
                      visualChangedPerVertexDrawCount +=
                          transition.ChangedPerVertexDraws;
                      visualAmbiguousPreviousDrawCount +=
                          transition.AmbiguousPreviousDraws;
                      visualAmbiguousCurrentDrawCount +=
                          transition.AmbiguousCurrentDraws;
                      visualUnmatchedPreviousDrawCount +=
                          transition.UnmatchedPreviousDraws;
                      visualUnmatchedCurrentDrawCount +=
                          transition.UnmatchedCurrentDraws;
                      const double continuityThreshold =
                          visualContinuityTracker.CurrentThreshold();
                      const bool continuousTransition =
                          transitionPrepared &&
                          visualContinuityTracker.Accept(transition);
                      latestVisualTransitionContinuous = continuousTransition;
                      visualDiscontinuityCount +=
                          continuousTransition ? 0U : 1U;
                      if (launch.ExtendedDiagnostics) {
                        if (visualTransitionEvents.size() == 1024U) {
                          visualTransitionEvents.erase(
                              visualTransitionEvents.begin());
                        }
                        visualTransitionEvents.push_back({
                            {"host_frame", frameCount},
                            {"previous_sequence",
                             latestVisualFrame->Sequence},
                            {"current_sequence",
                             currentVisualFrame->Sequence},
                            {"matched_draws", transition.MatchedDraws},
                            {"strict_unique_matched_draws",
                             transition.StrictUniqueMatchedDraws},
                            {"strict_ordinal_matched_draws",
                             transition.StrictOrdinalMatchedDraws},
                            {"structural_ordinal_matched_draws",
                             transition.StructuralOrdinalMatchedDraws},
                            {"pipeline_ordinal_matched_draws",
                             transition.PipelineOrdinalMatchedDraws},
                            {"changed_per_instance_vertex_draws",
                             transition.ChangedPerInstanceVertexDraws},
                            {"changed_per_vertex_draws",
                             transition.ChangedPerVertexDraws},
                            {"unmatched_previous_draws",
                             transition.UnmatchedPreviousDraws},
                            {"unmatched_current_draws",
                             transition.UnmatchedCurrentDraws},
                            {"top_target_matched_draws",
                             transition.TopTargetMatchedDraws},
                            {"matched_draw_coverage",
                             transition.MatchedDrawCoverage},
                            {"median_normalized_continuous_delta",
                             transition.MedianNormalizedContinuousDelta},
                            {"p90_normalized_continuous_delta",
                             transition.P90NormalizedContinuousDelta},
                            {"continuity_threshold", continuityThreshold},
                            {"continuous", continuousTransition},
                        });
                      }
                      if (continuousTransition && visualSampleReady) {
                        const auto temporalSample =
                            Fast::Oot3d::BuildNativeFrameTemporalSample(
                                frameCompositionPolicy,
                                Fast::Oot3d::NativeFrameTemporalSampleKind::
                                    Transition,
                                latestVisualFrame->Sequence,
                                currentVisualFrame->Sequence,
                                visualContinuityEpoch, sampleAlpha,
                                static_cast<float>(
                                    presentationElapsedSeconds));
                        executeVisualSample(
                            sample,
                            Oot3dNativeGame::
                                Oot3dPicaPresentationExecutionKind::
                                    InterpolatedFrame,
                            true, temporalSample);
                        ++visualSamplesPresented;
                        visualSampleDrawCount += sample.Draws.size();
                        visualInterpolatedDrawCount +=
                            sample.InterpolatedDraws;
                        visualInterpolatedPerInstanceVertexDrawCount +=
                            sample.InterpolatedPerInstanceVertexDraws;
                        visualInterpolatedPerVertexDrawCount +=
                            sample.InterpolatedPerVertexDraws;
                        presentedVisualSample = true;
                      } else {
                        resynchronizeVisualNamespace = true;
                      }
                    }
                    if (resynchronizeVisualNamespace &&
                        Oot3dNativeGame::ViewOot3dPicaVisualFrame(
                            *currentVisualFrame, sample)) {
                      preparedVisualTransition.Reset();
                      ++visualContinuityEpoch;
                      const auto temporalSample =
                          Fast::Oot3d::BuildNativeFrameTemporalSample(
                              frameCompositionPolicy,
                              Fast::Oot3d::NativeFrameTemporalSampleKind::
                                  Resynchronized,
                              currentVisualFrame->Sequence,
                              currentVisualFrame->Sequence,
                              visualContinuityEpoch, 1.0F,
                              static_cast<float>(
                                  presentationElapsedSeconds));
                      executeVisualSample(
                          sample,
                          Oot3dNativeGame::
                              Oot3dPicaPresentationExecutionKind::CurrentFrame,
                          true, temporalSample);
                      ++visualNamespaceResyncCount;
                      ++visualSamplesPresented;
                      visualSampleDrawCount += sample.Draws.size();
                      presentedVisualSample = true;
                    }
                    if (latestVisualFrame.has_value()) {
                      previousVisualFrame = std::move(latestVisualFrame);
                    } else {
                      latestVisualTransitionContinuous = false;
                    }
                    latestVisualFrame = std::move(currentVisualFrame);
                  }
                }
              }
              if (!selectedNewVisualFrame && !presentedVisualSample &&
                  visualInterpolationEnabled &&
                  latestVisualTransitionContinuous &&
                  previousVisualFrame.has_value() &&
                  latestVisualFrame.has_value() &&
                  preparedVisualTransition.Ready()) {
                auto &sample = visualFrameSampleScratch;
                const float sampleAlpha =
                    visualSampleCadence.Resolve(
                        false, visualInterpolationAlpha);
                if (preparedVisualTransition.Sample(
                        *previousVisualFrame, *latestVisualFrame, sampleAlpha,
                        sample, &visualInterpolationTiming)) {
                  const auto temporalSample =
                      Fast::Oot3d::BuildNativeFrameTemporalSample(
                          frameCompositionPolicy,
                          Fast::Oot3d::NativeFrameTemporalSampleKind::
                              Transition,
                          previousVisualFrame->Sequence,
                          latestVisualFrame->Sequence,
                          visualContinuityEpoch, sampleAlpha,
                          static_cast<float>(presentationElapsedSeconds));
                  executeVisualSample(
                      sample,
                      Oot3dNativeGame::
                          Oot3dPicaPresentationExecutionKind::RepeatedFrame,
                      true, temporalSample);
                  ++visualSamplesPresented;
                  visualSampleDrawCount += sample.Draws.size();
                  visualInterpolatedDrawCount += sample.InterpolatedDraws;
                  visualInterpolatedPerInstanceVertexDrawCount +=
                      sample.InterpolatedPerInstanceVertexDraws;
                  visualInterpolatedPerVertexDrawCount +=
                      sample.InterpolatedPerVertexDraws;
                  presentedVisualSample = true;
                }
              }
              if (!presentedVisualSample &&
                  picaPresentationScheduler.HasSnapshot(
                      selected->second,
                      kVisualInterpolationRenderTargetNamespace)) {
                const auto started = std::chrono::steady_clock::now();
                if (!picaPresentationScheduler.PresentExisting(
                        api, selected->second,
                        kVisualInterpolationRenderTargetNamespace, &error)) {
                  throw std::runtime_error(
                      "native top-screen scheduled scanout failed: " + error);
                }
                phaseTiming.VisualReplaySeconds += SecondsSince(started);
                ++visualReusedSnapshotPresentations;
              }
            }
          }

          const bool presentNativeBottomFrontend =
              Oot3dNativeGame::ShouldPresentNativeBottomFrontend(
                  launch.UiProfile,
                  uiLifecycleBridge.NativeFrontendPresentationActive());
          const auto bottomFramebuffer = hostServices.BottomFramebuffer();
          if (presentNativeBottomFrontend &&
              bottomFramebuffer.has_value()) {
            ++nativeFrontendPresentationActiveCount;
            auto selectedBottom =
                displayTransfersByOutput.find(bottomFramebuffer->AddressLeft);
            if (selectedBottom == displayTransfersByOutput.end() &&
                bottomFramebuffer->AddressRight != 0U &&
                bottomFramebuffer->AddressRight !=
                    bottomFramebuffer->AddressLeft) {
              selectedBottom = displayTransfersByOutput.find(
                  bottomFramebuffer->AddressRight);
            }
            nativeFrontendBottomTransferHitCount +=
                selectedBottom != displayTransfersByOutput.end() ? 1U : 0U;
            if (selectedBottom != displayTransfersByOutput.end() &&
                picaPresentationScheduler.HasSnapshot(
                    selectedBottom->second,
                    kVisualInterpolationRenderTargetNamespace)) {
              if (!picaPresentationScheduler.PresentExisting(
                      api, selectedBottom->second,
                      kVisualInterpolationRenderTargetNamespace, &error)) {
                throw std::runtime_error(
                    "native bottom-screen frontend presentation failed: " +
                    error);
              }
              ++nativeBottomFrontendPresentationCount;
            }
          }
        }
        phaseTiming.VisualPresentationSeconds +=
            SecondsSince(visualPresentationStart);
      }
      if (advanceGuest) {
        phaseStart = std::chrono::steady_clock::now();
        processResult =
            RunUntilGuestWait(process, launch.WholeAotBlockBudget);
        phaseTiming.GuestSeconds += SecondsSince(phaseStart);
        RequireRunnableGuest(processResult);

        phaseStart = std::chrono::steady_clock::now();
        const auto picaSubmitStart = phaseStart;
        size_t drainPassesThisRefresh = 0;
        bool refreshHadNativeDraws = false;
        while (drainPassesThisRefresh < kMaximumPicaDrainPassesPerRefresh) {
          publishNativeSceneView();
          submissionQueue.TakePendingDraws(drainedDraws);
          auto &draws = drainedDraws;
          auto completions = submissionQueue.TakePendingCompletions();
          auto displayTransfers = submissionQueue.TakePendingDisplayTransfers();
          auto memoryFills = submissionQueue.TakePendingMemoryFills();
          if (draws.empty() && completions.empty() &&
              displayTransfers.empty() && memoryFills.empty()) {
            break;
          }
          ++drainPassesThisRefresh;
          ++picaDrainPassCount;
          refreshHadNativeDraws = refreshHadNativeDraws || !draws.empty();
          const bool suppressTopScreenFrontendBackdrop =
              Oot3dNativeGame::ShouldSuppressTopScreenFrontendBackdrop(
                  launch.UiProfile,
                  uiLifecycleBridge.NativeFrontendPresentationActive());
          const auto commonBackgroundGuestAddress =
              suppressTopScreenFrontendBackdrop
                  ? uiLifecycleBridge.NativePauseSharedTextureGuestAddress(
                        oot3d::ui::UiPauseSharedTextureSlot::CommonBackground00)
                  : std::nullopt;
          const auto commonBackgroundPhysicalAddress =
              commonBackgroundGuestAddress.has_value()
                  ? picaMemoryView.TranslateGuest(*commonBackgroundGuestAddress,
                                                  1U)
                  : std::nullopt;
          std::vector<Oot3dNativeGame::TopScreenFrontendCanvas>
              frontendBackdropCanvases;
          const auto rememberFrontendBackdropCanvas =
              [&](const Oot3dNativeGame::Oot3dPicaDecodedDrawState& state) {
                if (state.Framebuffer.ColorPhysicalAddress != 0U &&
                    std::none_of(frontendBackdropCanvases.begin(),
                                 frontendBackdropCanvases.end(),
                                 [&](const auto& canvas) {
                                   return canvas.Matches(state);
                                 })) {
                  frontendBackdropCanvases.push_back(
                      {state.Framebuffer.ColorPhysicalAddress, state.Viewport});
                }
              };
          if (commonBackgroundPhysicalAddress.has_value()) {
            for (const auto &draw : draws) {
              const bool usesCommonBackground = std::any_of(
                  draw.State.Textures.begin(), draw.State.Textures.end(),
                  [&](const auto &texture) {
                    return texture.Enabled &&
                           texture.PhysicalAddress ==
                               *commonBackgroundPhysicalAddress;
                  });
              if (usesCommonBackground) {
                rememberFrontendBackdropCanvas(draw.State);
              }
            }
          }
          bool signaledPicaCompletion = false;
          size_t completionIndex = 0;
          size_t memoryFillIndex = 0;
          size_t displayTransferIndex = 0;
          const auto submitMemoryFill =
              [&](const Oot3dNativeGame::Oot3dPicaMemoryFillSubmission &fill) {
                picaSemanticTrace.RecordMemoryFill(
                    presentationFrameCount, frameCount, fill);
                picaPresentationScheduler.Capture(fill);
                if (fill.CompletionId != 0U) {
                  if (!fill.Interrupt.has_value() ||
                      !hostServices.SignalPicaInterrupt(process.Memory(),
                                                        *fill.Interrupt)) {
                    throw std::runtime_error(
                        "native PICA memory fill completion "
                        "could not enter the GSP relay queue");
                  }
                  if (*fill.Interrupt ==
                      Oot3dNativeGame::Oot3dPicaInterruptId::P3d) {
                    ++completedP3dCount;
                  } else if (*fill.Interrupt ==
                             Oot3dNativeGame::Oot3dPicaInterruptId::Ppf) {
                    ++completedPpfCount;
                  }
                  ++recordedCompletionCount;
                  signaledPicaCompletion = true;
                }
              };
          const auto submitDisplayTransfer =
              [&](const Oot3dNativeGame::Oot3dPicaDisplayTransferSubmission
                      &transfer) {
                const auto topFramebuffer = hostServices.TopFramebuffer();
                const bool selectedForTop = topFramebuffer.has_value() &&
                                            (transfer.Transfer.OutputAddress ==
                                                 topFramebuffer->AddressLeft ||
                                             transfer.Transfer.OutputAddress ==
                                                 topFramebuffer->AddressRight);
                picaSemanticTrace.RecordDisplayTransfer(
                    presentationFrameCount, frameCount, transfer,
                    selectedForTop, suppressTopScreenFrontendBackdrop);
                if (launch.ExtendedDiagnostics) {
                  if (displayTransferEvents.size() == 64U) {
                    displayTransferEvents.erase(displayTransferEvents.begin());
                  }
                  displayTransferEvents.push_back({
                      {"input_address", transfer.Transfer.InputAddress},
                      {"output_address", transfer.Transfer.OutputAddress},
                      {"input_physical_address", transfer.InputPhysicalAddress},
                      {"output_physical_address",
                       transfer.OutputPhysicalAddress},
                      {"input_size", transfer.Transfer.InputSize},
                      {"output_size", transfer.Transfer.OutputSize},
                      {"flags", transfer.Transfer.Flags},
                      {"after_draw_submission_id",
                       transfer.AfterDrawSubmissionId},
                      {"signals_guest_interrupt", transfer.SignalInterrupt},
                      {"selected_for_top_at_submission", selectedForTop},
                      {"topscreen_frontend_backdrop_suppression_active",
                       suppressTopScreenFrontendBackdrop},
                  });
                }
                picaPresentationScheduler.Capture(transfer);
                if (transfer.SignalInterrupt) {
                  if (!hostServices.SignalPicaInterrupt(
                          process.Memory(),
                          Oot3dNativeGame::Oot3dPicaInterruptId::Ppf)) {
                    throw std::runtime_error(
                        "native PICA display transfer completion could not "
                        "enter the GSP relay queue");
                  }
                  ++completedPpfCount;
                  ++recordedCompletionCount;
                  signaledPicaCompletion = true;
                }
                displayTransfersByOutput[transfer.Transfer.OutputAddress] =
                    transfer;
                ++displayTransferCount;
              };
          while (displayTransferIndex < displayTransfers.size() &&
                 displayTransfers[displayTransferIndex].AfterDrawSubmissionId <=
                     lastSubmittedDrawId) {
            submitDisplayTransfer(displayTransfers[displayTransferIndex++]);
          }
          for (auto &draw : draws) {
            while (memoryFillIndex < memoryFills.size() &&
                   memoryFills[memoryFillIndex].BeforeDrawSubmissionId <=
                       draw.Id) {
              submitMemoryFill(memoryFills[memoryFillIndex++]);
            }
            const bool usesCommonBackground =
                commonBackgroundPhysicalAddress.has_value() &&
                std::any_of(draw.State.Textures.begin(),
                            draw.State.Textures.end(),
                            [&](const auto &texture) {
                              return texture.Enabled &&
                                     texture.PhysicalAddress ==
                                         *commonBackgroundPhysicalAddress;
                            });
            const bool targetsFrontendBackdrop =
                std::any_of(frontendBackdropCanvases.begin(),
                            frontendBackdropCanvases.end(),
                            [&](const auto& canvas) {
                              return canvas.Matches(draw.State);
                            });
            const bool isOpaqueTargetInitialization =
                IsNativeFrontendOpaqueTargetInitializationDraw(draw);
            const bool suppressNativeFrontendBackdropDraw =
                suppressTopScreenFrontendBackdrop &&
                targetsFrontendBackdrop &&
                (usesCommonBackground || isOpaqueTargetInitialization);
            Oot3dNativeGame::Oot3dPicaVulkanDrawPlan plan;
            auto drawPhaseStart = std::chrono::steady_clock::now();
            if (!Oot3dNativeGame::
                    BuildOot3dPicaVulkanDrawPlanAndConsumeResources(
                        draw, plan, &error, &shaderSourceCache)) {
              throw std::runtime_error("native PICA Vulkan plan failed: " +
                                       error);
            }
            phaseTiming.PicaPlanSeconds += SecondsSince(drawPhaseStart);
            picaSemanticTrace.RecordDraw(
                presentationFrameCount, frameCount, draw, plan,
                suppressNativeFrontendBackdropDraw, usesCommonBackground,
                isOpaqueTargetInitialization);
            if (suppressNativeFrontendBackdropDraw) {
              nativeMenuBackdropDrawOmissionCount +=
                  usesCommonBackground ? 1U : 0U;
              nativeMenuClearDrawOmissionCount +=
                  isOpaqueTargetInitialization ? 1U : 0U;
            }
            if (launch.ExtendedDiagnostics) {
              drawPhaseStart = std::chrono::steady_clock::now();
              if (drawEvents.size() == 128U) {
                drawEvents.erase(drawEvents.begin());
              }
              nlohmann::json textures = nlohmann::json::array();
              for (size_t slot = 0; slot < draw.State.Textures.size();
                   ++slot) {
                const auto &texture = draw.State.Textures[slot];
                if (!texture.Enabled) {
                  continue;
                }
                const auto planTexture = std::find_if(
                    plan.Textures.begin(), plan.Textures.end(),
                    [slot](const auto &candidate) {
                      return candidate.Slot == slot;
                    });
                const std::span<const uint8_t> nativeBytes =
                    planTexture != plan.Textures.end()
                        ? std::span<const uint8_t>(planTexture->NativeBytes)
                        : std::span<const uint8_t>();
                textures.push_back({
                    {"slot", slot},
                    {"width", texture.Width},
                    {"height", texture.Height},
                    {"physical_address", texture.PhysicalAddress},
                    {"format", texture.Format},
                    {"type", texture.Type},
                    {"native_byte_count", nativeBytes.size()},
                    {"native_byte_hash", HashDiagnosticBytes(nativeBytes)},
                    {"native_nonzero_bytes",
                     std::count_if(nativeBytes.begin(), nativeBytes.end(),
                                   [](const uint8_t byte) {
                                     return byte != 0U;
                                   })},
                });
              }
              nlohmann::json vertexBindings = nlohmann::json::array();
              for (const auto &binding : plan.VertexBindings) {
                constexpr size_t kDiagnosticByteLimit = 256U;
                const auto bindingBytes = binding.ResolvedBytes();
                const size_t byteCount =
                    std::min(bindingBytes.size(), kDiagnosticByteLimit);
                vertexBindings.push_back({
                    {"binding", binding.Binding},
                    {"byte_stride", binding.ByteStride},
                    {"per_instance",
                     binding.InputRate ==
                         Oot3dNativeGame::Oot3dPicaVertexInputRate::
                             PerInstance},
                    {"byte_count", bindingBytes.size()},
                    {"prefix_bytes",
                     std::vector<uint8_t>(bindingBytes.begin(),
                                          bindingBytes.begin() + byteCount)},
                });
              }
              nlohmann::json defaultAttributes = nlohmann::json::array();
              for (size_t attributeIndex = 0;
                   attributeIndex < draw.State.VertexInput.AttributeCount;
                   ++attributeIndex) {
                if (!draw.State.VertexInput.Attributes[attributeIndex]
                         .Default) {
                  continue;
                }
                defaultAttributes.push_back({
                    {"attribute", attributeIndex},
                    {"input_register",
                     draw.State.ShaderInterface
                         .InputRegisterByAttribute[attributeIndex]},
                    {"value", draw.Packet.DefaultAttributes[attributeIndex]},
                });
              }
              constexpr std::array<uint16_t, 6> kTevStageRegisters{
                  0x0C0U, 0x0C8U, 0x0D0U, 0x0D8U, 0x0F0U, 0x0F8U};
              nlohmann::json tevStages = nlohmann::json::array();
              for (const uint16_t base : kTevStageRegisters) {
                tevStages.push_back({
                    {"base_register", base},
                    {"sources", draw.Packet.Registers[base]},
                    {"modifiers", draw.Packet.Registers[base + 1U]},
                    {"operations", draw.Packet.Registers[base + 2U]},
                    {"constant", draw.Packet.Registers[base + 3U]},
                    {"scales", draw.Packet.Registers[base + 4U]},
                });
              }
              drawEvents.push_back({
                  {"frame", frameCount},
                  {"submission_id", draw.Id},
                  {"command_list_address", draw.Packet.CommandListAddress},
                  {"command_list_offset_words",
                   draw.Packet.CommandListOffsetWords},
                  {"topscreen_frontend_backdrop_suppressed",
                   suppressNativeFrontendBackdropDraw},
                  {"native_frontend_background_texture", usesCommonBackground},
                  {"native_frontend_target_initialization",
                   isOpaqueTargetInitialization},
                  {"vertex_count", draw.State.VertexInput.VertexCount},
                  {"vertex_offset", draw.State.VertexInput.VertexOffset},
                  {"minimum_vertex_index", draw.MinimumVertexIndex},
                  {"maximum_vertex_index", draw.MaximumVertexIndex},
                  {"indexed", draw.State.VertexInput.Indexed},
                  {"indices_are_16_bit",
                   draw.State.VertexInput.IndicesAre16Bit},
                  {"attribute_count", draw.State.VertexInput.AttributeCount},
                  {"vulkan_base_vertex", plan.BaseVertex},
                  {"vulkan_vertex_bindings", plan.VertexBindings.size()},
                  {"vertex_shader_key", plan.VertexShader.StateKey},
                  {"fragment_shader_key", plan.FragmentShader.StateKey},
                  {"vertex_shader_source",
                   std::string(plan.ResolvedVertexShaderSource())},
                  {"fragment_shader_source",
                   std::string(plan.ResolvedFragmentShaderSource())},
                  {"vertex_uniform_floats", plan.VertexShader.Uniforms.Floats},
                  {"shader_output_mask", draw.State.ShaderInterface.OutputMask},
                  {"shader_output_total",
                   draw.Packet.Registers[0x04FU] & 7U},
                  {"geometry_shader_enabled",
                   draw.State.ShaderInterface.GeometryShaderEnabled},
                  {"geometry_shader_mode",
                   draw.Packet.Registers[0x229U] & 3U},
                  {"shader_output_maps",
                   {draw.Packet.Registers[0x050U],
                    draw.Packet.Registers[0x051U],
                    draw.Packet.Registers[0x052U],
                    draw.Packet.Registers[0x053U],
                    draw.Packet.Registers[0x054U],
                    draw.Packet.Registers[0x055U],
                    draw.Packet.Registers[0x056U]}},
                  {"framebuffer",
                   {{"color_address",
                     draw.State.Framebuffer.ColorPhysicalAddress},
                    {"depth_address",
                     draw.State.Framebuffer.DepthPhysicalAddress},
                    {"width", draw.State.Framebuffer.Width},
                    {"height", draw.State.Framebuffer.Height},
                    {"color_format", draw.State.Framebuffer.ColorFormat},
                    {"depth_format", draw.State.Framebuffer.DepthFormat},
                    {"flipped", draw.State.Framebuffer.Flipped}}},
                  {"viewport",
                   {{"half_width", draw.State.Viewport.HalfWidth},
                    {"half_height", draw.State.Viewport.HalfHeight},
                    {"depth_range", draw.State.Viewport.DepthRange},
                    {"near_plane", draw.State.Viewport.NearPlane},
                    {"z_buffering", draw.State.Viewport.ZBuffering},
                    {"corner_x", draw.State.Viewport.CornerX},
                    {"corner_y", draw.State.Viewport.CornerY}}},
                  {"scissor",
                   {{"mode", static_cast<uint32_t>(draw.State.Scissor.Mode)},
                    {"x1", draw.State.Scissor.X1},
                    {"y1", draw.State.Scissor.Y1},
                    {"x2", draw.State.Scissor.X2},
                    {"y2", draw.State.Scissor.Y2}}},
                  {"topology", static_cast<uint32_t>(draw.State.Topology)},
                  {"cull_mode", static_cast<uint32_t>(draw.State.CullMode)},
                  {"output_merger",
                   {{"fragment_operation_mode",
                     plan.State.OutputMerger.FragmentOperationMode},
                    {"color_write_mask",
                     plan.State.OutputMerger.ColorWriteMask},
                    {"logic_operation",
                     static_cast<uint32_t>(
                         plan.State.OutputMerger.LogicOperation)},
                    {"blend_enabled", plan.State.OutputMerger.Blend.Enabled},
                    {"blend_color_equation",
                     static_cast<uint32_t>(
                         plan.State.OutputMerger.Blend.ColorEquation)},
                    {"blend_source_color",
                     static_cast<uint32_t>(
                         plan.State.OutputMerger.Blend.SourceColor)},
                    {"blend_destination_color",
                     static_cast<uint32_t>(
                         plan.State.OutputMerger.Blend.DestinationColor)},
                    {"depth_test_enabled",
                     plan.State.OutputMerger.Depth.TestEnabled},
                    {"depth_write_enabled",
                     plan.State.OutputMerger.Depth.WriteEnabled},
                    {"depth_compare",
                     static_cast<uint32_t>(
                         plan.State.OutputMerger.Depth.Compare)}}},
                  {"textures", std::move(textures)},
                  {"vertex_bindings", std::move(vertexBindings)},
                  {"default_attributes", std::move(defaultAttributes)},
                  {"tev_stages", std::move(tevStages)},
                  {"combiner_buffer_color", draw.Packet.Registers[0x0FDU]},
              });
              phaseTiming.PicaDiagnosticsSeconds +=
                  SecondsSince(drawPhaseStart);
            }
            if (!suppressNativeFrontendBackdropDraw) {
              picaPresentationScheduler.Capture(std::move(plan));
            }
            while (completionIndex < completions.size() &&
                   completions[completionIndex].AfterDrawSubmissionId ==
                       draw.Id) {
              const auto &completion = completions[completionIndex++];
              if (!hostServices.SignalPicaInterrupt(process.Memory(),
                                                    completion.Interrupt)) {
                throw std::runtime_error(
                    "native PICA draw completion could not "
                    "enter the GSP relay queue");
              }
              if (completion.Interrupt ==
                  Oot3dNativeGame::Oot3dPicaInterruptId::P3d) {
                ++completedP3dCount;
              } else if (completion.Interrupt ==
                         Oot3dNativeGame::Oot3dPicaInterruptId::Ppf) {
                ++completedPpfCount;
              }
              ++recordedCompletionCount;
              signaledPicaCompletion = true;
            }
            lastSubmittedDrawId = draw.Id;
            while (
                displayTransferIndex < displayTransfers.size() &&
                displayTransfers[displayTransferIndex].AfterDrawSubmissionId ==
                    draw.Id) {
              submitDisplayTransfer(displayTransfers[displayTransferIndex++]);
            }
            ++drawCount;
          }
          if (completionIndex != completions.size()) {
            throw std::runtime_error(
                "native PICA completion did not match a submitted draw");
          }
          while (memoryFillIndex < memoryFills.size()) {
            submitMemoryFill(memoryFills[memoryFillIndex++]);
          }
          if (displayTransferIndex != displayTransfers.size()) {
            throw std::runtime_error("native PICA display transfer did not "
                                     "match a submitted draw");
          }
          if (!signaledPicaCompletion) {
            break;
          }
          phaseStart = std::chrono::steady_clock::now();
          processResult =
              RunUntilGuestWait(process, launch.WholeAotBlockBudget);
          phaseTiming.GuestSeconds += SecondsSince(phaseStart);
          RequireRunnableGuest(processResult);
        }
        if (drainPassesThisRefresh == kMaximumPicaDrainPassesPerRefresh &&
            (!submissionQueue.PendingDraws().empty() ||
             !submissionQueue.PendingCompletions().empty() ||
             !submissionQueue.PendingDisplayTransfers().empty() ||
             !submissionQueue.PendingMemoryFills().empty())) {
          throw std::runtime_error("native PICA completion chain exceeded "
                                   "the per-refresh drain limit");
        }
        maximumPicaDrainPassesPerRefresh = std::max<uint64_t>(
            maximumPicaDrainPassesPerRefresh, drainPassesThisRefresh);
        refreshesWithNativeDraws += refreshHadNativeDraws ? 1U : 0U;
        phaseTiming.PicaSubmitSeconds += SecondsSince(picaSubmitStart);
      }
      frameCount += advanceGuest ? 1U : 0U;
    }

    if (launch.ExtendedDiagnostics) {
      inputConsumerDiagnostics.Observe(process.Memory(), frameCount);
    }

    for (std::size_t subsystemIndex = 0;
         subsystemIndex < oot3d::ui::kUiSubsystemCount; ++subsystemIndex) {
      const auto subsystem =
          static_cast<oot3d::ui::UiSubsystem>(subsystemIndex);
      const bool topScreenProfile =
          launch.UiProfile == Oot3dNativeGame::Oot3dUiProfile::TopScreen;
      if (topScreenProfile &&
          subsystem != oot3d::ui::UiSubsystem::GameplayHud &&
          subsystem != oot3d::ui::UiSubsystem::Map) {
        continue;
      }
      const bool topScreenPresentation = topScreenProfile;
      if (!topScreenPresentation && !uiLifecycleBridge.Runtime()
                                         .PlanFrame(subsystem)
                                         .run_host_presentation) {
        continue;
      }
      auto primitives =
          topScreenPresentation
              ? uiLifecycleBridge.BuildTopScreenPresentation(subsystem)
              : uiLifecycleBridge.BuildShadowPresentation(subsystem);
      if (topScreenPresentation &&
          subsystem == oot3d::ui::UiSubsystem::GameplayHud) {
        Oot3dNativeGame::AppendTopScreenFreeCameraOptionPresentation(
            nativeCandidateDispatch.TopScreenCamera.Camera, primitives);
      }
      if (topScreenPresentation) {
        lastTopScreenPrimitivesBySubsystem[subsystemIndex] = primitives;
      }
      const auto canvasMode =
          topScreenPresentation
              ? oot3d::ui::N64UiCanvasMode::NativeTopScreen400x240
              : oot3d::ui::N64UiCanvasMode::Widescreen16x9;
      if (!n64UiRenderer.Render(primitives, width, height, canvasMode,
                                &error)) {
        throw std::runtime_error("integrated N64 UI presentation failed: " +
                                 error);
      }
    }

    phaseStart = std::chrono::steady_clock::now();
    gui->EndDraw();
    MaybeWriteFramebufferScreenshot(
        hostArgs, api, width, height,
        static_cast<uint32_t>(presentationFrameCount), screenshotState,
        capturedTemporalSample ? &*capturedTemporalSample : nullptr);
    capturedTemporalSample.reset();
    realtimePacer.WaitForNextRefresh();
    window.EndFrame();
    picaSemanticTrace.RecordFrameBoundary(
        presentationFrameCount, frameCount, guestRefreshesDue != 0U);
    phaseTiming.PresentSeconds += SecondsSince(phaseStart);
    ++presentationFrameCount;
    ++runFrameCount;
#if defined(__SWITCH__)
    gSwitchRuntimeHostFrames = runFrameCount;
    gSwitchRuntimeGuestFrames = frameCount;
    // Write once at the first frame and at most once more when a persistent
    // geometry is first observed. Periodic receipt writes would put
    // synchronous SD filesystem work directly in the presentation loop.
    if (runFrameCount == 1U || !gSwitchPicaGeometryCacheObserved) {
      const auto wholeAotStats =
          Oot3dNativeGame::GetOot3dCompiledFunctionStats();
      gSwitchWholeAotBlockLimitExits =
          wholeAotStats.WholeAotBlockLimitExits;
      const auto picaBackend = api.GetNativePicaBackendStats();
      gSwitchPicaGeometryCacheEnabled =
          picaBackend.GeometryCacheEnabled;
      gSwitchPicaGeometryPersistentDraws =
          picaBackend.GeometryPersistentDraws;
      gSwitchPicaGeometryPersistentUploads =
          picaBackend.GeometryPersistentUploads;
      gSwitchPicaGeometryRegistryEntries =
          picaBackend.GeometryRegistryEntries;
      gSwitchPicaGeometryCacheObserved =
          gSwitchPicaGeometryPersistentDraws != 0U &&
          gSwitchPicaGeometryPersistentUploads != 0U &&
          gSwitchPicaGeometryRegistryEntries != 0U;
      if (runFrameCount == 1U || gSwitchPicaGeometryCacheObserved) {
        WriteSwitchBootStage("runtime_running");
      }
    }
#endif
    const auto completedFrameTime = std::chrono::steady_clock::now();
    if (!benchmarkMeasurementStarted &&
        runFrameCount >= hostArgs.BenchmarkWarmupFrames) {
      benchmarkMeasurementStart = completedFrameTime;
      benchmarkMeasurementEnd = completedFrameTime;
      benchmarkMeasurementStarted = true;
    } else if (benchmarkMeasurementStarted &&
               runFrameCount > hostArgs.BenchmarkWarmupFrames) {
      benchmarkMeasurementEnd = completedFrameTime;
      ++benchmarkMeasuredFrames;
    }
    if (quickLoadRequested) {
      if (loadState(quickStatePath)) {
        quickSavePending = false;
      }
    } else {
      if (quickSavePending && saveState(quickStatePath)) {
        quickSavePending = false;
      }
      if (Oot3dNativeGame::ShouldCaptureNativeA32AutomatedState(
              launch.SaveStateFrameAvailable, automatedStateSaved,
              runFrameCount, launch.SaveStateFrame) &&
          saveState(launch.SaveStatePath)) {
        automatedStateSaved = true;
        automatedStateSavedAtRunFrame = runFrameCount - 1U;
        automatedStateSavedAtGuestFrame = frameCount;
      }
    }
    ApplyWindowDemoFrameLimit(hostArgs, window, runFrameCount);
  }

#if defined(__SWITCH__)
  WriteSwitchBootStage(
      processResult.Kind ==
              Oot3dNativeGame::NativeA32ProcessRunKind::Terminated
          ? "runtime_guest_terminated"
          : "runtime_window_closed");
#endif

  if (!launch.AudioPcmDumpPath.empty()) {
    Oot3dNativeGame::WriteStereoPcm16Wave(
        launch.AudioPcmDumpPath,
        Oot3dNativeGame::NativeA32DspHle::NativeSampleRate, pcmDumpSamples);
  }

  blockTraceWriter.Write();
  picaSemanticTrace.Finish();

  if (!launch.Host.OutputPath.empty()) {
    nlohmann::json topFramebuffer = nullptr;
    if (const auto top = hostServices.TopFramebuffer(); top.has_value()) {
      topFramebuffer = {
          {"address_left", top->AddressLeft},
          {"address_right", top->AddressRight},
          {"stride", top->Stride},
          {"format", top->Format},
          {"buffer_index", top->BufferIndex},
      };
    }
    nlohmann::json bottomFramebuffer = nullptr;
    if (const auto bottom = hostServices.BottomFramebuffer();
        bottom.has_value()) {
      bottomFramebuffer = {
          {"address_left", bottom->AddressLeft},
          {"address_right", bottom->AddressRight},
          {"stride", bottom->Stride},
          {"format", bottom->Format},
          {"buffer_index", bottom->BufferIndex},
      };
    }
    nlohmann::json recentSvcEvents = nlohmann::json::array();
    nlohmann::json filesystemSvcEvents = nlohmann::json::array();
    nlohmann::json ipcSvcEvents = nlohmann::json::array();
    const auto &svcEvents = hostServices.SvcEvents();
    const size_t firstSvcEvent =
        svcEvents.size() > 32U ? svcEvents.size() - 32U : 0U;
    for (size_t index = firstSvcEvent; index < svcEvents.size(); ++index) {
      const auto &event = svcEvents[index];
      recentSvcEvents.push_back({
          {"immediate", event.Immediate},
          {"pc", event.Pc},
          {"thread_id", event.ThreadId},
          {"handled", event.Handled},
          {"name", event.Name},
          {"detail", event.Detail},
      });
    }
    size_t firstFilesystemEvent = 0;
    size_t filesystemEventCount = 0;
    for (size_t index = svcEvents.size(); index > 0; --index) {
      const auto &event = svcEvents[index - 1U];
      if (event.Name.find("fs:USER") == std::string::npos &&
          event.Name.find("file:romfs") == std::string::npos &&
          event.Name.find("file:savedata") == std::string::npos) {
        continue;
      }
      firstFilesystemEvent = index - 1U;
      if (++filesystemEventCount == 64U) {
        break;
      }
    }
    for (size_t index = firstFilesystemEvent; index < svcEvents.size();
         ++index) {
      const auto &event = svcEvents[index];
      if (event.Name.find("fs:USER") == std::string::npos &&
          event.Name.find("file:romfs") == std::string::npos &&
          event.Name.find("file:savedata") == std::string::npos) {
        continue;
      }
      filesystemSvcEvents.push_back({
          {"immediate", event.Immediate},
          {"pc", event.Pc},
          {"thread_id", event.ThreadId},
          {"handled", event.Handled},
          {"name", event.Name},
          {"detail", event.Detail},
      });
    }
    size_t firstIpcEvent = 0;
    size_t ipcEventCount = 0;
    for (size_t index = svcEvents.size(); index > 0; --index) {
      const auto &event = svcEvents[index - 1U];
      if (event.Immediate != 0x32U ||
          event.Name.find("file:romfs:Read") != std::string::npos ||
          event.Name.find("gsp::Gpu:FlushDataCache") != std::string::npos) {
        continue;
      }
      firstIpcEvent = index - 1U;
      if (++ipcEventCount == 128U) {
        break;
      }
    }
    for (size_t index = firstIpcEvent; index < svcEvents.size(); ++index) {
      const auto &event = svcEvents[index];
      if (event.Immediate != 0x32U ||
          event.Name.find("file:romfs:Read") != std::string::npos ||
          event.Name.find("gsp::Gpu:FlushDataCache") != std::string::npos) {
        continue;
      }
      ipcSvcEvents.push_back({
          {"pc", event.Pc},
          {"thread_id", event.ThreadId},
          {"handled", event.Handled},
          {"name", event.Name},
          {"detail", event.Detail},
      });
    }
    std::map<uint32_t, const Oot3dNativeGame::NativeA32CtrSvcEvent *>
        lastSvcByThread;
    for (const auto &event : svcEvents) {
      lastSvcByThread[event.ThreadId] = &event;
    }
    nlohmann::json lastThreadSvcEvents = nlohmann::json::array();
    for (const auto &[threadId, event] : lastSvcByThread) {
      lastThreadSvcEvents.push_back({
          {"thread_id", threadId},
          {"immediate", event->Immediate},
          {"pc", event->Pc},
          {"handled", event->Handled},
          {"name", event->Name},
          {"detail", event->Detail},
      });
    }
    nlohmann::json threads = nlohmann::json::array();
    for (uint32_t threadId = 0; threadId < process.ThreadCount(); ++threadId) {
      const auto *state = process.ThreadState(threadId);
      nlohmann::json commandBufferWords = nlohmann::json::array();
      nlohmann::json registers = nlohmann::json::array();
      nlohmann::json stackWords = nlohmann::json::array();
      nlohmann::json threadArgumentWords = nlohmann::json::array();
      if (state != nullptr) {
        for (uint32_t value : state->r) {
          registers.push_back(value);
        }
        for (uint32_t offset = 0; offset < 0x40U; offset += 4U) {
          uint32_t value = 0;
          if (!process.Memory().Read32(state->r[13] + offset, &value)) {
            break;
          }
          stackWords.push_back(value);
        }
        const uint32_t commandBuffer = state->thread_pointer + 0x80U;
        for (uint32_t offset = 0; offset < 0x30U; offset += 4U) {
          uint32_t value = 0;
          if (!process.Memory().Read32(commandBuffer + offset, &value)) {
            break;
          }
          commandBufferWords.push_back(value);
        }
      }
      const uint32_t threadArgument =
          process.ThreadArgument(threadId).value_or(0U);
      for (uint32_t offset = 0; threadArgument != 0U && offset < 0x20U;
           offset += 4U) {
        uint32_t value = 0;
        if (!process.Memory().Read32(threadArgument + offset, &value)) {
          break;
        }
        threadArgumentWords.push_back(value);
      }
      threads.push_back({
          {"id", threadId},
          {"entry_address", process.ThreadEntryAddress(threadId).value_or(0U)},
          {"argument", process.ThreadArgument(threadId).value_or(0U)},
          {"priority", process.ThreadPriority(threadId).value_or(0U)},
          {"status", static_cast<uint32_t>(process.ThreadStatus(threadId))},
          {"pc", state != nullptr ? state->r[15] : 0U},
          {"registers", std::move(registers)},
          {"stack", std::move(stackWords)},
          {"command_buffer", std::move(commandBufferWords)},
          {"argument_words", std::move(threadArgumentWords)},
      });
    }
    nlohmann::json projectionCallsites = nlohmann::json::array();
    for (const auto &[key, stats] :
         widescreenProjection.Projection.Callsites) {
      projectionCallsites.push_back({
          {"function", key.first},
          {"return_address", key.second},
          {"calls", stats.Calls},
          {"first_left", stats.FirstLeft},
          {"first_right", stats.FirstRight},
      });
    }
    std::vector<std::pair<uint32_t, uint64_t>> sampledBlocks(
        widescreenProjection.BlockSamples.begin(),
        widescreenProjection.BlockSamples.end());
    std::sort(sampledBlocks.begin(), sampledBlocks.end(),
              [](const auto &left, const auto &right) {
                return left.second != right.second ? left.second > right.second
                                                   : left.first < right.first;
              });
    uint64_t totalBlockSampleCount = 0;
    for (const auto &sampledBlock : sampledBlocks) {
      totalBlockSampleCount += sampledBlock.second;
    }
    constexpr size_t kMaximumReportedA32Blocks = 4096U;
    if (sampledBlocks.size() > kMaximumReportedA32Blocks) {
      sampledBlocks.resize(kMaximumReportedA32Blocks);
    }
    nlohmann::json sampledBlockEntries = nlohmann::json::array();
    uint64_t blockSampleCount = 0;
    for (const auto &[pc, samples] : sampledBlocks) {
      blockSampleCount += samples;
      sampledBlockEntries.push_back({
          {"pc", pc},
          {"samples", samples},
          {"estimated_entries", samples * 64U},
      });
    }
    const auto readQueueState = [&](uint32_t address) {
      nlohmann::json words = nlohmann::json::array();
      std::array<uint32_t, 13> values{};
      size_t valueCount = 0;
      for (uint32_t offset = 0; offset < 0x34U; offset += 4U) {
        uint32_t value = 0;
        if (!process.Memory().Read32(address + offset, &value)) {
          break;
        }
        values[valueCount++] = value;
        words.push_back({{"offset", offset}, {"value", value}});
      }
      const auto valueAt = [&](size_t index) {
        return index < valueCount ? values[index] : 0U;
      };
      return nlohmann::json{
          {"address", address},
          {"storage", valueAt(0)},
          {"item_semaphore", valueAt(1)},
          {"free_semaphore", valueAt(3)},
          {"capacity", valueAt(8)},
          {"read_index", valueAt(9)},
          {"count", valueAt(10)},
          {"active_consumers", valueAt(11)},
          {"active_producers", valueAt(12)},
          {"words", std::move(words)},
      };
    };
    const auto readRendererCommandState = [&]() {
      nlohmann::json words = nlohmann::json::array();
      for (uint32_t offset = 0x168U; offset <= 0x188U; offset += 4U) {
        uint32_t value = 0;
        if (!process.Memory().Read32(kRendererCommandQueue + offset, &value)) {
          break;
        }
        words.push_back({{"offset", offset}, {"value", value}});
      }
      return words;
    };
    const auto sceneViewSnapshot =
        sceneViewProbe.Capture(process.Memory());
    const auto playerTimingSnapshot =
        playerTimingProbe.Capture(process.Memory());
    const auto serializeTopScreenPrimitives = [](const auto &primitives) {
      nlohmann::json serialized = nlohmann::json::array();
      for (const auto &primitive : primitives) {
        serialized.push_back(
            {{"role", static_cast<std::uint32_t>(primitive.role)},
             {"owner_address", primitive.owner_address},
             {"descriptor_address", primitive.descriptor_address},
             {"source_quad", primitive.source_quad},
             {"texture", primitive.texture.semantic_name},
             {"destination",
              {primitive.destination.x, primitive.destination.y,
               primitive.destination.width, primitive.destination.height}},
             {"uv",
              {primitive.uv.x, primitive.uv.y, primitive.uv.width,
               primitive.uv.height}},
             {"color",
              {primitive.color.red, primitive.color.green,
               primitive.color.blue, primitive.color.alpha}},
             {"layer", primitive.layer},
             {"visible", primitive.visible}});
      }
      return serialized;
    };
    nlohmann::json topScreenPresentations = nlohmann::json::object();
    for (std::size_t subsystemIndex = 0;
         subsystemIndex < oot3d::ui::kUiSubsystemCount; ++subsystemIndex) {
      const auto subsystem =
          static_cast<oot3d::ui::UiSubsystem>(subsystemIndex);
      topScreenPresentations[oot3d::ui::UiSubsystemName(subsystem)] =
          serializeTopScreenPrimitives(
              lastTopScreenPrimitivesBySubsystem[subsystemIndex]);
    }
    const auto topScreenGameplayPrimitiveAudit =
        serializeTopScreenPrimitives(lastTopScreenPrimitivesBySubsystem[
            static_cast<std::size_t>(oot3d::ui::UiSubsystem::GameplayHud)]);
    const auto graphicsSettings =
        Fast::Oot3d::GraphicsSettingsRuntime::Instance().Snapshot();
    const auto &directionalShadows =
        graphicsSettings.Effects.DirectionalShadows;
    const auto &realtimePacingStats = realtimePacer.Stats();
    const double benchmarkHostSeconds =
        benchmarkMeasuredFrames == 0U
            ? 0.0
            : std::chrono::duration<double>(benchmarkMeasurementEnd -
                                            benchmarkMeasurementStart)
                  .count();
    WriteJsonFile(
        launch.Host.OutputPath,
        {
            {"schema", "oot3d_native_a32_vulkan_runtime_v1"},
            {"structural_scenario", scenarioBootstrap.has_value()
                                        ? scenarioBootstrap->ToJson()
                                        : nlohmann::json(nullptr)},
            {"frames", frameCount},
            {"run_frames", runFrameCount},
            {"guest_refresh_frames", frameCount},
            {"presentation_frames", presentationFrameCount},
            {"pica_composition",
             [&]() {
               const auto &stats = picaCompositionTracker.Stats();
               return nlohmann::json{
                   {"cmb_opaque_pass_entries", stats.CmbOpaquePassEntries},
                   {"cmb_transparent_pass_entries",
                    stats.CmbTransparentPassEntries},
                   {"cmb_pass_exits", stats.CmbPassExits},
                   {"invalid_cmb_pass_entries", stats.InvalidCmbPassEntries},
                   {"invalid_cmb_return_addresses",
                    stats.InvalidCmbReturnAddresses},
                   {"atmosphere_scope_entries", stats.AtmosphereScopeEntries},
                   {"atmosphere_scope_exits", stats.AtmosphereScopeExits},
                   {"atmosphere_packet_spans", stats.AtmospherePacketSpans},
                   {"direct_atmosphere_spans", stats.DirectAtmosphereSpans},
                   {"direct_atmosphere_bytes", stats.DirectAtmosphereBytes},
                   {"nested_atmosphere_scope_entries",
                    stats.NestedAtmosphereScopeEntries},
                   {"invalid_atmosphere_command_ranges",
                    stats.InvalidAtmosphereCommandRanges},
                   {"recovered_atmosphere_packet_spans",
                    stats.RecoveredAtmospherePacketSpans},
                   {"invalid_atmosphere_return_addresses",
                    stats.InvalidAtmosphereReturnAddresses},
                   {"ignored_unrelated_command_list_executions",
                    stats.IgnoredUnrelatedCommandListExecutions},
                   {"atmosphere_selector_read_failures",
                    stats.AtmosphereSelectorReadFailures},
                   {"primitive_packet_entries",
                    stats.PrimitivePacketEntries},
                   {"primitive_packet_exits", stats.PrimitivePacketExits},
                   {"primitive_packet_spans", stats.PrimitivePacketSpans},
                   {"primitive_packet_bytes", stats.PrimitivePacketBytes},
                   {"nested_primitive_packet_entries",
                    stats.NestedPrimitivePacketEntries},
                   {"invalid_primitive_packet_return_addresses",
                    stats.InvalidPrimitivePacketReturnAddresses},
                   {"invalid_primitive_packet_ranges",
                    stats.InvalidPrimitivePacketRanges},
                   {"unmatched_primitive_packet_exits",
                    stats.UnmatchedPrimitivePacketExits},
                   {"mesh_packet_entries", stats.MeshPacketEntries},
                   {"recorded_packet_spans", stats.RecordedPacketSpans},
                   {"coalesced_packet_spans", stats.CoalescedPacketSpans},
                   {"unattributed_packet_spans",
                    stats.UnattributedPacketSpans},
                   {"invalid_packet_spans", stats.InvalidPacketSpans},
                   {"overlapping_packet_spans",
                    stats.OverlappingPacketSpans},
                   {"consumed_packet_spans", stats.ConsumedPacketSpans},
                   {"partially_overlapping_submissions",
                    stats.PartiallyOverlappingSubmissions},
                   {"evicted_packet_spans", stats.EvictedPacketSpans},
                   {"read_failures", stats.ReadFailures},
               };
             }()},
            {"topscreen_gameplay_primitives",
             topScreenGameplayPrimitiveAudit},
            {"topscreen_presentations", std::move(topScreenPresentations)},
            {"vblanks", vblankCount},
            {"savestates",
             {{"quick_path", quickStatePath.string()},
              {"initial_load_path", launch.LoadStatePath.string()},
              {"automated_save_path", launch.SaveStatePath.string()},
              {"automated_save_frame",
               launch.SaveStateFrameAvailable
                   ? nlohmann::json(launch.SaveStateFrame)
                   : nlohmann::json(nullptr)},
              {"automated_save_actual_run_frame",
               automatedStateSavedAtRunFrame.has_value()
                   ? nlohmann::json(*automatedStateSavedAtRunFrame)
                   : nlohmann::json(nullptr)},
              {"automated_save_actual_guest_frame",
               automatedStateSavedAtGuestFrame.has_value()
                   ? nlohmann::json(*automatedStateSavedAtGuestFrame)
                   : nlohmann::json(nullptr)},
              {"save_count", savestateSaveCount},
              {"load_count", savestateLoadCount},
              {"save_bytes", savestateSaveBytes},
              {"load_bytes", savestateLoadBytes},
              {"save_seconds", savestateSaveSeconds},
              {"load_seconds", savestateLoadSeconds},
              {"capture_seconds", savestateCaptureSeconds},
              {"encode_seconds", savestateEncodeSeconds},
              {"write_seconds", savestateWriteSeconds},
              {"read_seconds", savestateReadSeconds},
              {"decode_seconds", savestateDecodeSeconds},
              {"restore_seconds", savestateRestoreSeconds},
              {"last_semantic_fingerprint",
               std::to_string(lastSavestateSemanticFingerprint)},
              {"legacy_gameplay_clock_restores",
               legacySavestateClockRestoreCount},
              {"legacy_presentation_scheduler_restores",
               legacySavestatePresentationSchedulerRestoreCount},
              {"legacy_frame_rate_policy_restores",
               legacySavestateFrameRatePolicyRestoreCount},
              {"legacy_topscreen_temporal_restores",
               legacySavestateTopScreenTemporalRestoreCount},
              {"legacy_pica_visual_replay_restores",
               legacySavestatePicaVisualReplayRestoreCount},
              {"legacy_pica_texture_cache_restores",
               legacySavestatePicaTextureCacheRestoreCount},
              {"legacy_pica_color_target_restores",
               legacySavestatePicaColorTargetRestoreCount},
              {"legacy_pica_presentation_restores",
               legacySavestatePicaPresentationRestoreCount},
              {"pica_textures_captured",
               savestatePicaTexturesCaptured},
              {"pica_texture_bytes_captured",
               savestatePicaTextureBytesCaptured},
              {"pica_textures_restored",
               savestatePicaTexturesRestored},
              {"pica_texture_bytes_restored",
               savestatePicaTextureBytesRestored},
              {"pica_visual_replay_bytes_captured",
               savestatePicaVisualReplayBytesCaptured},
              {"pica_visual_replay_bytes_restored",
               savestatePicaVisualReplayBytesRestored},
              {"pica_color_targets_captured",
               savestatePicaColorTargetsCaptured},
              {"pica_color_target_bytes_captured",
               savestatePicaColorTargetBytesCaptured},
              {"pica_color_targets_restored",
               savestatePicaColorTargetsRestored},
              {"pica_color_target_bytes_restored",
               savestatePicaColorTargetBytesRestored},
              {"pica_display_images_captured",
               savestatePicaDisplayImagesCaptured},
              {"pica_display_image_bytes_captured",
               savestatePicaDisplayImageBytesCaptured},
              {"pica_display_images_restored",
               savestatePicaDisplayImagesRestored},
              {"pica_display_image_bytes_restored",
               savestatePicaDisplayImageBytesRestored}}},
             {"realtime_pacing",
             {{"enabled", realtimePacer.Enabled()},
              {"high_resolution_wait_available",
               realtimePacer.HighResolutionWaitAvailable()},
              {"basis", "realtime_when_no_frame_limit"},
              {"waits", realtimePacingStats.Waits},
              {"deadline_misses",
               realtimePacingStats.DeadlineMisses},
              {"carried_deadline_debt",
               realtimePacingStats.CarriedDeadlineDebt},
              {"deadline_resyncs",
               realtimePacingStats.DeadlineResyncs},
              {"sleep_seconds", realtimePacingStats.SleepSeconds},
              {"spin_seconds", realtimePacingStats.SpinSeconds},
              {"host_loop_seconds", realtimePacer.ElapsedSeconds()},
              {"presentation_intervals",
               realtimePacingStats.PresentationIntervals},
              {"mean_interval_seconds",
               realtimePacer.MeanIntervalSeconds()},
              {"minimum_interval_seconds",
               realtimePacingStats.MinimumIntervalSeconds},
              {"maximum_interval_seconds",
               realtimePacingStats.MaximumIntervalSeconds},
              {"rms_interval_error_seconds",
               realtimePacer.RmsIntervalErrorSeconds()},
              {"maximum_interval_error_seconds",
               realtimePacingStats.MaximumIntervalErrorSeconds},
              {"maximum_lateness_seconds",
               realtimePacingStats.MaximumLatenessSeconds},
              {"target_refresh_hz",
               realtimePacer.TargetRateHz() == 0U
                   ? nlohmann::json(nullptr)
                   : nlohmann::json(realtimePacer.TargetRateHz())}}},
            {"guest_clock",
             {{"system_ticks", hostServices.SystemTicks()},
              {"next_vblank_tick", nextVblankTick},
              {"late_vblank_deadlines", lateVblankDeadlineCount},
              {"maximum_vblank_deadline_overshoot_ticks",
               maximumVblankDeadlineOvershootTicks},
              {"maximum_vblank_deadline_overshoot_seconds",
               static_cast<double>(maximumVblankDeadlineOvershootTicks) /
                   static_cast<double>(kCtrArm11TicksPerSecond)}}},
            {"benchmark_window",
             {{"warmup_frames_requested",
               hostArgs.BenchmarkWarmupFrames},
              {"warmup_frames_completed",
               std::min<uint64_t>(runFrameCount,
                                  hostArgs.BenchmarkWarmupFrames)},
              {"measured_frames", benchmarkMeasuredFrames},
              {"host_seconds",
               benchmarkMeasuredFrames == 0U
                   ? nlohmann::json(nullptr)
                   : nlohmann::json(benchmarkHostSeconds)},
              {"frames_per_second",
               benchmarkMeasuredFrames == 0U || benchmarkHostSeconds <= 0.0
                   ? nlohmann::json(nullptr)
                   : nlohmann::json(
                         static_cast<double>(benchmarkMeasuredFrames) /
                         benchmarkHostSeconds)},
              {"fixed_delta_seconds",
               hostArgs.FixedDeltaSeconds > 0.0
                   ? nlohmann::json(hostArgs.FixedDeltaSeconds)
                   : nlohmann::json(nullptr)},
              {"throughput_mode", hostArgs.ThroughputBenchmark},
              {"vsync",
               hostArgs.ThroughputBenchmark ? false
                                            : graphicsSettings.VSync},
              {"sdl_frame_limiter_enabled",
               !hostArgs.ThroughputBenchmark &&
                   window.GetTargetFps() > 0},
              {"pacing_enabled", realtimePacer.Enabled()}}},
            {"frame_rate",
             [&]() {
                const auto &contract = frameRatePolicy.Contract();
                const auto &stats = frameRatePolicy.Stats();
                const auto &time =
                    frameRatePolicy.GameplayClock().Context();
                const auto &presentationStats = presentationScheduler.Stats();
                const auto &cadenceStats = visualSampleCadence.Stats();
                const auto &playerTemporalStats =
                    playerTemporalBridge.Stats();
                return nlohmann::json{
                    {"mode",
                     Oot3dNativeGame::GameplayTimingModeName(contract.Mode)},
                    {"implementation_status",
                     contract.Mode ==
                             Oot3dNativeGame::GameplayTimingMode::Enhanced60
                         ? "experimental_a32_compatibility"
                         : "baseline"},
                    {"original_simulation_hz",
                     Oot3dNativeGame::kOot3dOriginalSimulationRateHz},
                   {"simulation_hz", contract.SimulationRateHz},
                   {"presentation_hz",
                    contract.PresentationRateUnlimited
                        ? nlohmann::json(nullptr)
                        : nlohmann::json(contract.PresentationRateHz)},
                   {"presentation_unlimited",
                    contract.PresentationRateUnlimited},
                   {"presentation_frames",
                    presentationStats.PresentationFrames},
                   {"guest_refreshes_scheduled",
                    presentationStats.GuestRefreshes},
                   {"deferred_guest_refresh_observations",
                    presentationStats.DeferredGuestRefreshObservations},
                   {"guest_refreshes_dropped",
                    presentationStats.DroppedGuestRefreshes},
                   {"batched_presentation_frames",
                    presentationStats.BatchedPresentationFrames},
                   {"maximum_guest_refresh_batch",
                    presentationStats.MaximumGuestRefreshBatch},
                   {"guest_refresh_batch_limit",
                    Oot3dNativeGame::kMaximumGuestRefreshesPerPresentation},
                   {"maximum_deferred_guest_refreshes",
                    presentationStats.MaximumDeferredGuestRefreshes},
                   {"deferred_guest_refresh_limit",
                    Oot3dNativeGame::kMaximumDeferredGuestRefreshes},
                    {"step_seconds", contract.StepSeconds},
                    {"native_update_rate", contract.NativeUpdateRate},
                    {"native_step_scale", contract.NativeStepScale},
                    {"logical_frame_delta", contract.LogicalFrameDelta},
                    {"simulation_tick", time.SimulationTick},
                    {"previous_logical_frame",
                     time.PreviousLogicalFrame},
                    {"current_logical_frame", time.CurrentLogicalFrame},
                    {"logical_frame_index", time.LogicalFrameIndex},
                    {"crossed_logical_frame", time.CrossedLogicalFrame},
                    {"a32_update_rate", contract.A32UpdateRate},
                    {"a32_update_rate_exact", contract.A32UpdateRateExact},
                    {"visual_interpolation_allowed",
                     contract.VisualInterpolationAllowed},
                    {"visual_interpolation_active",
                     visualInterpolationEnabled},
                    {"visual_interpolation_mode",
                     Fast::Oot3d::NativeVisualInterpolationModeName(
                         frameCompositionPolicy.Interpolation)},
                    {"visual_sample_multiplier",
                     frameCompositionPolicy.FixedSampleMultiplier},
                    {"visual_sample_cadence",
                     {{"fixed_multiplier",
                       visualSampleCadence.FixedSampleMultiplier()},
                      {"new_source_frames",
                       cadenceStats.NewSourceFrames},
                      {"repeated_samples",
                       cadenceStats.RepeatedSamples},
                      {"clamped_repeated_samples",
                       cadenceStats.ClampedRepeatedSamples},
                      {"last_sample_ordinal",
                       cadenceStats.SampleOrdinal}}},
                    {"visual_presentation_rate_hz",
                     frameCompositionPolicy.TargetPresentationRateHz != 0U
                         ? nlohmann::json(
                               frameCompositionPolicy.TargetPresentationRateHz)
                         : nlohmann::json(nullptr)},
                    {"visual_continuity_epoch", visualContinuityEpoch},
                    {"source_owner_graphs_complete",
                     sourceGameplayOwners.EnabledOwnerCount()},
                    {"compatibility_island",
                     contract.Mode ==
                             Oot3dNativeGame::GameplayTimingMode::Enhanced60
                         ? nlohmann::json("a32_gameplay_owner_graph")
                         : nlohmann::json(nullptr)},
                    {"player_temporal_bridge",
                     {{"enabled",
                       widescreenProjection.PlayerTemporalBridge != nullptr},
                      {"collision_entries",
                       playerTemporalStats.CollisionEntries},
                      {"collision_returns",
                       playerTemporalStats.CollisionReturns},
                      {"logical_frame_returns",
                       playerTemporalStats.LogicalFrameReturns},
                      {"intermediate_returns",
                       playerTemporalStats.IntermediateReturns},
                      {"reconciliations",
                       playerTemporalStats.FloorTimerReconciliations},
                      {"immediate_floor_type_changes",
                       playerTemporalStats.ImmediateFloorTypeChanges},
                      {"native_mutation_mismatches",
                       playerTemporalStats.NativeMutationMismatches},
                      {"ledge_increment_paths",
                       playerTemporalStats.LedgeIncrementPaths},
                      {"ledge_wire_overflow_reconciliations",
                       playerTemporalStats
                           .LedgeWireOverflowReconciliations},
                      {"immediate_ledge_type_changes",
                       playerTemporalStats.ImmediateLedgeTypeChanges},
                      {"ledge_timer_resets",
                       playerTemporalStats.LedgeTimerResets},
                      {"ledge_mutation_mismatches",
                       playerTemporalStats.LedgeMutationMismatches},
                      {"dynamic_wall_gate_evaluations",
                       playerTemporalStats.DynamicWallGateEvaluations},
                      {"high_ledge_gate_evaluations",
                       playerTemporalStats.HighLedgeGateEvaluations},
                      {"low_ledge_gate_evaluations",
                       playerTemporalStats.LowLedgeGateEvaluations},
                      {"ledge_gates_reached",
                       playerTemporalStats.LedgeGatesReached},
                      {"unmatched_returns",
                       playerTemporalStats.UnmatchedReturns},
                      {"unmatched_ledge_increment_paths",
                       playerTemporalStats.UnmatchedLedgeIncrementPaths},
                      {"read_failures", playerTemporalStats.ReadFailures},
                      {"write_failures", playerTemporalStats.WriteFailures}}},
                    {"game_state_updates_observed",
                     stats.GameStateUpdatesObserved},
                   {"update_rate_writes", stats.UpdateRateWrites},
                   {"frame_pacing_commits_observed",
                    stats.FramePacingCommitsObserved},
                   {"frame_pacing_frames_observed",
                    stats.FramePacingFramesObserved},
                   {"frame_pacing_writes", stats.FramePacingWrites},
                   {"frame_pacing_decisions_observed",
                    stats.FramePacingDecisionsObserved},
                   {"frame_pacing_deadline_writes",
                    stats.FramePacingDeadlineWrites},
                   {"read_failures", stats.ReadFailures},
                   {"write_failures", stats.WriteFailures},
                   {"last_game_state_address", stats.LastGameStateAddress},
                   {"last_time_state_address", stats.LastTimeStateAddress},
                   {"last_observed_update_rate",
                    stats.HasObservedUpdateRate
                        ? nlohmann::json(stats.LastObservedUpdateRate)
                        : nlohmann::json(nullptr)},
                   {"last_frame_pacing_state_address",
                    stats.LastFramePacingStateAddress},
                   {"last_observed_maximum_interval",
                    stats.HasObservedFramePacing
                        ? nlohmann::json(stats.LastObservedMaximumInterval)
                        : nlohmann::json(nullptr)},
                   {"last_observed_interval",
                    stats.HasObservedFramePacing
                        ? nlohmann::json(stats.LastObservedInterval)
                        : nlohmann::json(nullptr)},
                   {"last_observed_deadline",
                    stats.HasObservedFramePacing
                        ? nlohmann::json(stats.LastObservedDeadline)
                        : nlohmann::json(nullptr)},
                   {"last_observed_vsync_counter",
                    stats.HasObservedFramePacing
                        ? nlohmann::json(stats.LastObservedVsyncCounter)
                        : nlohmann::json(nullptr)},
               };
             }()},
            {"temporal_event_ledger",
             [&]() {
               const auto &stats = temporalEventLedger.Stats();
               nlohmann::json events = nlohmann::json::array();
               for (const auto &event : temporalEventLedger.Events()) {
                 events.push_back({
                     {"sequence", event.Sequence},
                     {"simulation_tick", event.SimulationTick},
                     {"logical_frame", event.LogicalFrame},
                     {"logical_frame_index", event.LogicalFrameIndex},
                     {"kind",
                      std::string(
                          Oot3dNativeGame::NativeTemporalEventKindName(
                              event.Kind))},
                     {"subject_address", event.SubjectAddress},
                     {"previous_value", event.PreviousValue},
                     {"current_value", event.CurrentValue},
                 });
               }
               return nlohmann::json{
                   {"enabled", launch.ExtendedDiagnostics},
                   {"source", "a32_player_semantic_transition_probe"},
                   {"player_observations", stats.PlayerObservations},
                   {"player_identity_changes",
                    stats.PlayerIdentityChanges},
                   {"events_observed", stats.EventsObserved},
                   {"events_recorded", stats.EventsRecorded},
                   {"events_dropped", stats.EventsDropped},
                   {"baseline_fingerprint",
                    stats.BaselineFingerprint},
                   {"event_fingerprint", stats.EventFingerprint},
                   {"events", std::move(events)},
               };
             }()},
            {"player_timing",
             [&]() {
               const auto &stats = playerTimingProbe.Stats();
               nlohmann::json state = nullptr;
               if (playerTimingSnapshot.has_value()) {
                 const auto &player = *playerTimingSnapshot;
                 state = {
                     {"player_address", player.PlayerAddress},
                     {"play_state_address", player.PlayStateAddress},
                     {"time_state_address", player.TimeStateAddress},
                     {"world_position",
                      {player.WorldX, player.WorldY, player.WorldZ}},
                     {"velocity",
                      {player.VelocityX, player.VelocityY, player.VelocityZ}},
                     {"actor_speed", player.ActorSpeed},
                     {"player_speed", player.PlayerSpeed},
                     {"floor_height", player.FloorHeight},
                     {"underwater_timer", player.UnderwaterTimer},
                     {"melee_weapon_action_timer",
                      player.MeleeWeaponActionTimer},
                     {"melee_weapon_combo_state",
                      player.MeleeWeaponComboState},
                     {"item_action_state_or_burn_timer",
                      player.ItemActionStateOrBurnTimer},
                     {"ledge_climb_type", player.LedgeClimbType},
                     {"ledge_climb_delay_timer",
                      player.LedgeClimbDelayTimer},
                     {"textbox_button_cooldown_timer",
                      player.TextboxButtonCooldownTimer},
                     {"damage_flicker_animation_counter",
                      player.DamageFlickerAnimationCounter},
                     {"damage_run_timer", player.DamageRunTimer},
                     {"item_action_cooldown_timer",
                      player.ItemActionCooldownTimer},
                     {"collision_sfx_cooldown_timer",
                      player.CollisionSfxCooldownTimer},
                     {"invincibility_timer", player.InvincibilityTimer},
                     {"floor_type_timer", player.FloorTypeTimer},
                     {"previous_floor_type", player.PreviousFloorType},
                     {"respawn_damage_state",
                      player.RespawnDamageState},
                     {"random_turn_state", player.RandomTurnState},
                     {"random_turn_timer", player.RandomTurnTimer},
                     {"attention_persistence_counter",
                      player.AttentionPersistenceCounter},
                     {"fairy_revive_grace_timer",
                      player.FairyReviveGraceTimer},
                     {"shape_yaw", player.ShapeYaw},
                     {"background_check_flags", player.BackgroundCheckFlags},
                     {"action_function", player.ActionFunction},
                     {"state_flags_1", player.StateFlags1},
                     {"state_flags_2", player.StateFlags2},
                     {"animation_resource", player.AnimationResource},
                     {"animation_frame", player.AnimationFrame},
                     {"animation_play_speed", player.AnimationPlaySpeed},
                     {"animation_start_frame", player.AnimationStartFrame},
                     {"animation_end_frame", player.AnimationEndFrame},
                     {"animation_mode", player.AnimationMode},
                     {"time_state_update_rate", player.TimeStateUpdateRate},
                 };
               }
               return nlohmann::json{
                   {"source", "code_bin_player_update_layout"},
                   {"update_entries_observed", stats.UpdateEntriesObserved},
                   {"snapshots_captured", stats.SnapshotsCaptured},
                   {"read_failures", stats.ReadFailures},
                   {"last_error", playerTimingProbe.LastError()},
                   {"state", std::move(state)},
                   {"events_truncated",
                    widescreenProjection.PlayerTimingEventsTruncated},
                   {"events",
                    std::move(widescreenProjection.PlayerTimingEvents)},
               };
             }()},
            {"native_scene_view",
             [&]() {
               const auto &stats = sceneViewProbe.Stats();
               nlohmann::json state = nullptr;
               if (sceneViewSnapshot.has_value()) {
                 const auto &view = *sceneViewSnapshot;
                 state = {
                     {"serial", view.Serial},
                     {"play_state_address", view.PlayStateAddress},
                     {"guest_function", view.GuestFunction},
                     {"guest_return_address", view.GuestReturnAddress},
                     {"frustum",
                      {view.Left, view.Right, view.Bottom, view.Top,
                       view.NearPlane, view.FarPlane}},
                     {"eye", view.Eye},
                     {"at", view.At},
                     {"camera_available", view.CameraAvailable},
                 };
               }
               return nlohmann::json{
                   {"source",
                    "code_bin_mtx4x4_frustum_and_playstate_view"},
                   {"perspective_observations",
                    stats.PerspectiveObservations},
                   {"invalid_perspective_observations",
                    stats.InvalidPerspectiveObservations},
                   {"snapshots_captured", stats.SnapshotsCaptured},
                   {"read_failures", stats.ReadFailures},
                   {"published", publishedSceneViewCount},
                   {"last_error", sceneViewProbe.LastError()},
                   {"state", std::move(state)},
               };
             }()},
            {"hid_input",
             [&]() {
               const auto hid = hostServices.HidRuntimeProfile();
               const auto controls =
                   controlConfigRuntime->Snapshot().Config;
               return nlohmann::json{
                   {"controls_config",
                    controlConfigRuntime->Path().generic_string()},
                   {"controls_profile",
                    Oot3dNativeGame::NativeControlProfileName(
                        controls.Profile)},
                   {"native_aim_source",
                    Oot3dNativeGame::NativeMotionSourceName(
                        controls.NativeAimSource)},
                   {"free_camera_source",
                    Oot3dNativeGame::NativeMotionSourceName(
                        controls.FreeCameraSource)},
                   {"gameplay_mouse_owned",
                    nativeControlPollingState.GameplayMouseOwned},
                   {"free_camera_input",
                    {{"pending_x",
                      nativeCandidateDispatch.TopScreenCameraInput.Peek().X},
                     {"pending_y",
                      nativeCandidateDispatch.TopScreenCameraInput.Peek().Y},
                     {"relative_samples_observed",
                      nativeCandidateDispatch.TopScreenCameraInput
                          .RelativeSamplesObserved()},
                     {"relative_samples_consumed",
                      nativeCandidateDispatch.TopScreenCameraInput
                          .RelativeSamplesConsumed()}}},
                   {"timeline_enabled", inputTimeline.Enabled()},
                   {"timeline_path",
                    inputTimeline.SourcePath().generic_string()},
                   {"timeline_segments", inputTimeline.SegmentCount()},
                   {"timeline_frame_origin",
                    Oot3dNativeGame::NativeA32InputTimelineFrameOriginName(
                        inputTimeline.FrameOrigin())},
                   {"sampled_host_frames", inputDiagnostics.SampledFrameCount},
                   {"scripted_host_frames",
                    inputDiagnostics.ScriptedFrameCount},
                   {"active_segment_host_frames",
                    inputDiagnostics.ActiveSegmentFrameCount},
                   {"button_host_frames", inputDiagnostics.ButtonFrameCount},
                   {"circle_pad_host_frames",
                    inputDiagnostics.CirclePadFrameCount},
                   {"touch_host_frames", inputDiagnostics.TouchFrameCount},
                   {"start_edges_observed",
                    nativeCandidateDispatch.StartButtonLatch.ObservedPresses},
                   {"start_short_presses_recovered",
                    nativeCandidateDispatch.StartButtonLatch
                        .RecoveredShortPresses},
                   {"start_edge_pending",
                    nativeCandidateDispatch.StartButtonLatch.PendingPressed},
                   {"shared_memory_mapped", hid.SharedMemoryMapped},
                   {"samples_written", hid.SamplesWritten},
                   {"accelerometer_samples_written",
                    hid.AccelerometerSamplesWritten},
                   {"gyroscope_samples_written",
                    hid.GyroscopeSamplesWritten},
                   {"event_batches_signaled", hid.EventBatchesSignaled},
                   {"last_sample_tick", hid.LastSampleTick},
                   {"last_buttons", hid.LastButtons},
                   {"last_additions", hid.LastAdditions},
                   {"last_removals", hid.LastRemovals},
                   {"last_circle_pad_x", hid.LastCirclePadX},
                   {"last_circle_pad_y", hid.LastCirclePadY},
                   {"next_pad_index", hid.NextPadIndex},
                   {"last_accelerometer", hid.LastAccelerometer},
                   {"last_gyroscope", hid.LastGyroscope},
                   {"next_accelerometer_index",
                    hid.NextAccelerometerIndex},
                   {"next_gyroscope_index",
                    hid.NextGyroscopeIndex},
                   {"native_consumer",
                    {{"samples", inputConsumerDiagnostics.Samples},
                     {"read_failures", inputConsumerDiagnostics.ReadFailures},
                     {"held_frames", inputConsumerDiagnostics.HeldFrames},
                     {"pressed_frames", inputConsumerDiagnostics.PressedFrames},
                     {"released_frames",
                      inputConsumerDiagnostics.ReleasedFrames},
                     {"last_held", inputConsumerDiagnostics.LastHeld},
                     {"last_pressed", inputConsumerDiagnostics.LastPressed},
                     {"last_released", inputConsumerDiagnostics.LastReleased},
                     {"last_raw_current",
                      inputConsumerDiagnostics.LastRawCurrent},
                     {"last_raw_previous",
                      inputConsumerDiagnostics.LastRawPrevious},
                     {"transitions",
                      std::move(inputConsumerDiagnostics.Transitions)}}},
               };
             }()},
            {"dsp_audio",
             {{"enabled", !launch.DisableAudio},
              {"host_output_enabled", !launch.DisableAudio},
              {"guest_dsp_processing_enabled", true},
              {"frames_mixed", dspFramesMixed},
              {"frames_processed", dspFramesMixed},
              {"frames_suppressed", dspFramesSuppressed},
              {"stereo_samples_mixed", dspSamplesMixed},
              {"nonzero_channel_samples", dspNonzeroSamples},
              {"peak_magnitude", dspPeakMagnitude},
              {"pcm_fnv1a64", dspPcmFnv1a64},
              {"pcm_continuity", pcmContinuityDiagnostics.ToJson()},
              {"pcm_dump",
               {{"enabled", !launch.AudioPcmDumpPath.empty()},
                {"path", launch.AudioPcmDumpPath.string()},
                {"channel_samples", pcmDumpSamples.size()}}},
              {"output_initialized",
               audioPlayer != nullptr && audioPlayer->IsInitialized()},
              {"output_sample_rate",
               audioPlayer != nullptr ? audioPlayer->GetSampleRate() : 0},
              {"output_frame_samples",
               audioPlayer != nullptr ? audioPlayer->GetSampleLength() : 0},
              {"output_queue",
               {{"observed", audioOutputDiagnostics.HasBufferObservation},
                {"primed", audioOutputDiagnostics.Primed},
                {"submit_calls", audioOutputDiagnostics.SubmitCalls},
                {"requested_frames", audioOutputDiagnostics.RequestedFrames},
                {"maximum_submit_gap_seconds",
                 audioOutputDiagnostics.MaximumSubmitGapSeconds},
                {"submit_gap_events",
                 std::move(audioOutputDiagnostics.SubmitGapEvents)},
                {"short_growth_submissions",
                 audioOutputDiagnostics.ShortGrowthSubmissions},
                {"estimated_shortfall_frames",
                 audioOutputDiagnostics.EstimatedShortfallFrames},
                {"starvation_observations",
                 audioOutputDiagnostics.StarvationObservations},
                {"starvation_events",
                 std::move(audioOutputDiagnostics.StarvationEvents)},
                {"minimum_buffered_before",
                 audioOutputDiagnostics.MinimumBufferedBefore},
                {"maximum_buffered_before",
                 audioOutputDiagnostics.MaximumBufferedBefore},
                {"minimum_buffered_after",
                 audioOutputDiagnostics.MinimumBufferedAfter},
                {"maximum_buffered_after",
                 audioOutputDiagnostics.MaximumBufferedAfter},
                {"desired_buffered", audioPlayer != nullptr
                                         ? audioPlayer->GetDesiredBuffered()
                                         : 0}}}}},
            {"draws_submitted", drawCount},
            {"refreshes_with_native_draws", refreshesWithNativeDraws},
            {"display_transfers_submitted", displayTransferCount},
            {"gpu_completions_queued", 0},
            {"pica_completions_recorded", recordedCompletionCount},
            {"p3d_completions_delivered", completedP3dCount},
            {"ppf_completions_delivered", completedPpfCount},
            {"pending_gpu_completions", 0},
            {"pica_drain_passes", picaDrainPassCount},
            {"maximum_pica_drain_passes_per_refresh",
             maximumPicaDrainPassesPerRefresh},
            {"pending_draws", submissionQueue.PendingDraws().size()},
            {"pending_completion_markers",
             submissionQueue.PendingCompletions().size()},
            {"pending_display_transfers",
             submissionQueue.PendingDisplayTransfers().size()},
            {"process_run_kind", static_cast<uint32_t>(processResult.Kind)},
            {"ui_profile",
             Oot3dNativeGame::Oot3dUiProfileName(launch.UiProfile)},
            {"graphics_settings",
             {{"configuration_path", launch.Host.ConfigurationPath},
              {"preset", static_cast<std::uint32_t>(
                             graphicsSettings.Preset)},
              {"frame_rate_mode",
               static_cast<std::uint32_t>(
                   graphicsSettings.FrameRate)},
              {"fov_multiplier", graphicsSettings.FovMultiplier},
              {"toon_mode",
               static_cast<std::uint32_t>(
                   graphicsSettings.Effects.Toon)},
              {"directional_shadows",
               {{"enabled",
                 directionalShadows.Mode ==
                     Fast::Oot3d::DirectionalShadowMode::
                         SingleCascade},
                {"mode",
                 static_cast<std::uint32_t>(
                     directionalShadows.Mode)},
                {"resolution", directionalShadows.Resolution},
                {"maximum_distance",
                 directionalShadows.MaximumDistance},
                {"depth_padding", directionalShadows.DepthPadding},
                {"depth_bias_constant",
                 directionalShadows.DepthBiasConstant},
                {"depth_bias_slope",
                 directionalShadows.DepthBiasSlope},
                {"strength", directionalShadows.Strength},
                {"pcf_radius", directionalShadows.PcfRadius},
                {"stabilize", directionalShadows.Stabilize}}}}},
            {"topscreen_config",
             launch.TopScreenConfigPath.empty()
                 ? nlohmann::json(nullptr)
                 : nlohmann::json(launch.TopScreenConfigPath.string())},
            {"topscreen_settings",
             {{"source",
               launch.TopScreenConfigPath.empty()
                   ? nlohmann::json("built_in_defaults")
                   : nlohmann::json("external_json")},
              {"hud_layout",
               Oot3dNativeGame::TopScreenHudLayoutName(
                   activeTopScreenConfig.HudLayout)},
              {"hud_scale",
               std::round(static_cast<double>(
                              activeTopScreenConfig.HudScale) *
                          100.0) /
                   100.0},
              {"hud_margin_x", activeTopScreenConfig.HudMarginX},
              {"hud_margin_y", activeTopScreenConfig.HudMarginY},
              {"magic_bar_y", activeTopScreenConfig.MagicBarY},
              {"minimap_visible", activeTopScreenConfig.MinimapVisible},
              {"render_hud", activeTopScreenConfig.RenderHud},
              {"render_dpad_icons",
               activeTopScreenConfig.RenderDpadIcons},
              {"render_items_hint",
               activeTopScreenConfig.RenderItemsHint},
              {"select_action",
               Oot3dNativeGame::TopScreenSelectActionName(
                   activeTopScreenConfig.SelectAction)},
              {"exit_items_to_save_screen",
               activeTopScreenConfig.ExitItemsToSaveScreen},
              {"camera_zoom_percent",
               activeTopScreenConfig.CameraZoomPercent},
              {"camera_fov_percent",
               activeTopScreenConfig.CameraFovPercent},
              {"dpad_child",
               nlohmann::json::array(
                   {Oot3dNativeGame::TopScreenDpadActionName(
                        activeTopScreenConfig.ChildDpad[0]),
                    Oot3dNativeGame::TopScreenDpadActionName(
                        activeTopScreenConfig.ChildDpad[1]),
                    Oot3dNativeGame::TopScreenDpadActionName(
                        activeTopScreenConfig.ChildDpad[2]),
                    Oot3dNativeGame::TopScreenDpadActionName(
                        activeTopScreenConfig.ChildDpad[3])})},
              {"dpad_adult",
               nlohmann::json::array(
                   {Oot3dNativeGame::TopScreenDpadActionName(
                        activeTopScreenConfig.AdultDpad[0]),
                    Oot3dNativeGame::TopScreenDpadActionName(
                        activeTopScreenConfig.AdultDpad[1]),
                    Oot3dNativeGame::TopScreenDpadActionName(
                        activeTopScreenConfig.AdultDpad[2]),
                    Oot3dNativeGame::TopScreenDpadActionName(
                        activeTopScreenConfig.AdultDpad[3])})},
              {"free_camera_enabled",
               activeTopScreenConfig.FreeCameraEnabled},
              {"free_camera_speed_level",
               activeTopScreenConfig.FreeCameraSpeedLevel},
              {"free_camera_smoothing",
               Oot3dNativeGame::TopScreenFreeCameraSmoothingName(
                   activeTopScreenConfig.FreeCameraSmoothing)},
              {"free_camera_invert_x",
               activeTopScreenConfig.FreeCameraInvertX},
              {"free_camera_invert_y",
               activeTopScreenConfig.FreeCameraInvertY},
              {"c_stick_aim_speed_level",
               activeTopScreenConfig.CStickAimSpeedLevel},
              {"c_stick_aim_invert_x",
               activeTopScreenConfig.CStickAimInvertX},
              {"c_stick_aim_invert_y",
               activeTopScreenConfig.CStickAimInvertY},
              {"gameplay_actions",
               {{"attempts",
                 nativeCandidateDispatch.TopScreenGameplayActions.Attempts},
                {"eligible",
                 nativeCandidateDispatch.TopScreenGameplayActions.Eligible},
                {"equipment_changes",
                 nativeCandidateDispatch.TopScreenGameplayActions
                     .EquipmentChanges},
                {"player_refreshes",
                 nativeCandidateDispatch.TopScreenGameplayActions
                     .PlayerRefreshes},
                {"direct_item_assignments",
                 nativeCandidateDispatch.TopScreenGameplayActions
                     .DirectItemAssignments},
                {"direct_item_triggers",
                 nativeCandidateDispatch.TopScreenGameplayActions
                     .DirectItemTriggers},
                {"direct_item_clears",
                 nativeCandidateDispatch.TopScreenGameplayActions
                     .DirectItemClears}}},
              {"guest_save_preferences_used", false},
              {"gameplay_preference_chords_enabled", false}}},
            {"process_state_fingerprint", process.StateFingerprint()},
            {"memory_state_fingerprint", process.Memory().StateFingerprint()},
            {"memory_content_fingerprint",
             process.Memory().ContentFingerprint()},
            {"system_ticks", hostServices.SystemTicks()},
            {"lcd_force_black", hostServices.LcdForceBlack()},
            {"top_framebuffer", std::move(topFramebuffer)},
            {"bottom_framebuffer", std::move(bottomFramebuffer)},
            {"display_transfer_events", std::move(displayTransferEvents)},
            {"draw_events", std::move(drawEvents)},
            {"recent_svc_events", std::move(recentSvcEvents)},
            {"filesystem_svc_events", std::move(filesystemSvcEvents)},
            {"ipc_svc_events", std::move(ipcSvcEvents)},
            {"last_svc_by_thread", std::move(lastThreadSvcEvents)},
            {"threads", std::move(threads)},
            {"screenshot_written", screenshotState.Written},
            {"visual_interpolation",
             {{"snapshot_replay_enabled", visualInterpolationEnabled},
              {"cross_frame_history_enabled", visualInterpolationEnabled},
              {"direct_current_frames_presented",
               visualDirectFramesPresented},
              {"reused_snapshot_presentations",
               visualReusedSnapshotPresentations},
              {"selected_top_transfers", selectedTopTransferCount},
              {"complete_snapshots", visualSnapshotCount},
              {"transitions", visualTransitionCount},
              {"matched_draws", visualMatchedDrawCount},
              {"strict_unique_matched_draws",
               visualStrictUniqueMatchedDrawCount},
              {"strict_ordinal_matched_draws",
               visualStrictOrdinalMatchedDrawCount},
              {"structural_ordinal_matched_draws",
               visualStructuralOrdinalMatchedDrawCount},
              {"pipeline_ordinal_matched_draws",
               visualPipelineOrdinalMatchedDrawCount},
              {"changed_continuous_draws", visualChangedContinuousDrawCount},
              {"changed_per_instance_vertex_draws",
               visualChangedPerInstanceVertexDrawCount},
              {"changed_per_vertex_draws", visualChangedPerVertexDrawCount},
              {"ambiguous_previous_draws", visualAmbiguousPreviousDrawCount},
              {"ambiguous_current_draws", visualAmbiguousCurrentDrawCount},
              {"unmatched_previous_draws", visualUnmatchedPreviousDrawCount},
              {"unmatched_current_draws", visualUnmatchedCurrentDrawCount},
              {"samples_presented", visualSamplesPresented},
              {"discontinuities", visualDiscontinuityCount},
              {"namespace_resynchronizations", visualNamespaceResyncCount},
              {"sample_draws", visualSampleDrawCount},
              {"interpolated_draws", visualInterpolatedDrawCount},
              {"interpolated_per_instance_vertex_draws",
               visualInterpolatedPerInstanceVertexDrawCount},
              {"interpolated_per_vertex_draws",
               visualInterpolatedPerVertexDrawCount},
              {"matching_seconds",
               visualInterpolationTiming.MatchingNanoseconds * 1.0e-9},
              {"analysis_seconds",
               visualInterpolationTiming.AnalysisNanoseconds * 1.0e-9},
              {"sampling_seconds",
               visualInterpolationTiming.SamplingNanoseconds * 1.0e-9},
              {"pending_draws",
               picaPresentationScheduler.PendingDrawCount()},
              {"execution_scheduler",
               [&]() {
                 const auto &stats = picaPresentationScheduler.Stats();
                 const auto &timings = picaPresentationScheduler.Timings();
                 const double measuredSeconds =
                     timings.ValidationSeconds +
                     timings.CompositionPublicationSeconds +
                     timings.DrawSubmissionSeconds +
                     timings.MemoryFillSeconds +
                     timings.SnapshotTransferSeconds +
                     timings.PresentTransferSeconds;
                 return nlohmann::json{
                     {"produced_guest_draws", stats.ProducedGuestDraws},
                     {"captured_memory_fills", stats.CapturedMemoryFills},
                     {"captured_display_transfers",
                      stats.CapturedDisplayTransfers},
                     {"completed_visual_frames",
                      stats.CompletedVisualFrames},
                     {"selected_draws", stats.SelectedDraws},
                     {"executed_draws", stats.ExecutedDraws},
                     {"interpolated_draws", stats.InterpolatedDraws},
                     {"execution_lists", stats.ExecutionLists},
                     {"current_frame_lists", stats.CurrentFrameLists},
                     {"interpolated_frame_lists",
                      stats.InterpolatedFrameLists},
                     {"repeated_frame_lists", stats.RepeatedFrameLists},
                     {"dependency_flushes", stats.DependencyFlushes},
                     {"memory_fills_executed",
                      stats.MemoryFillsExecuted},
                     {"display_snapshots_executed",
                      stats.DisplaySnapshotsExecuted},
                     {"presented_transfers", stats.PresentedTransfers},
                     {"reused_snapshots", stats.ReusedSnapshots},
                     {"duplicate_draw_attempts",
                      stats.DuplicateDrawAttempts},
                     {"redundant_top_snapshot_copies_avoided",
                      stats.RedundantTopSnapshotCopiesAvoided},
                     {"composition_sequences_published",
                      stats.CompositionSequencesPublished},
                     {"composition_draw_references_published",
                      stats.CompositionDrawReferencesPublished},
                     {"composition_publication_failures",
                      stats.CompositionPublicationFailures},
                     {"resets", stats.Resets},
                     {"timing_enabled", timings.Enabled},
                     {"execute_seconds", timings.ExecuteSeconds},
                     {"validation_seconds", timings.ValidationSeconds},
                     {"composition_publication_seconds",
                      timings.CompositionPublicationSeconds},
                     {"draw_submission_seconds",
                      timings.DrawSubmissionSeconds},
                     {"draw_uniform_packing_seconds",
                      timings.DrawUniformPackingSeconds},
                     {"draw_view_construction_seconds",
                      timings.DrawViewConstructionSeconds},
                     {"draw_backend_submission_seconds",
                      timings.DrawBackendSubmissionSeconds},
                     {"memory_fill_seconds", timings.MemoryFillSeconds},
                     {"snapshot_transfer_seconds",
                      timings.SnapshotTransferSeconds},
                     {"present_transfer_seconds",
                      timings.PresentTransferSeconds},
                     {"present_existing_seconds",
                      timings.PresentExistingSeconds},
                     {"execute_overhead_seconds",
                      std::max(0.0,
                               timings.ExecuteSeconds - measuredSeconds)},
                 };
               }()},
              {"recent_transitions", std::move(visualTransitionEvents)}}},
            {"enmag_trace",
             {{"init_calls", widescreenProjection.EnMagInitCalls},
              {"update_calls", widescreenProjection.EnMagUpdateCalls},
              {"draw_calls", widescreenProjection.EnMagDrawCalls},
              {"events", std::move(widescreenProjection.EnMagEvents)}}},
            {"ui_lifecycle_trace",
             {{"call_counts", widescreenProjection.UiLifecycleCallCounts},
              {"events", std::move(widescreenProjection.UiLifecycleEvents)}}},
            {"game_state_trace",
             {{"update_calls", widescreenProjection.GameStateUpdateCalls},
              {"events", std::move(widescreenProjection.GameStateEvents)}}},
            {"n64_integrated_ui",
             [&]() {
               const auto &bridge = uiLifecycleBridge.Stats();
               const auto &ui = uiLifecycleBridge.Runtime().Stats();
               const auto &renderer = n64UiRenderer.Stats();
               const auto &nativeTextures = nativeUiTextureProvider.Stats();
               nlohmann::json subsystemCalls = nlohmann::json::object();
               for (std::size_t index = 0;
                    index < bridge.calls_by_subsystem.size(); ++index) {
                 const auto subsystem =
                     static_cast<oot3d::ui::UiSubsystem>(index);
                 subsystemCalls[oot3d::ui::UiSubsystemName(subsystem)] =
                     bridge.calls_by_subsystem[index];
               }
               nlohmann::json phaseCalls = nlohmann::json::object();
               for (std::size_t index = 0; index < bridge.calls_by_phase.size();
                    ++index) {
                 const auto phase = static_cast<oot3d::ui::UiSeamPhase>(index);
                 phaseCalls[oot3d::ui::UiSeamPhaseName(phase)] =
                     bridge.calls_by_phase[index];
               }
               return nlohmann::json{
                   {"selected_profile",
                    Oot3dNativeGame::Oot3dUiProfileName(launch.UiProfile)},
                   {"topscreen_profile_active",
                    nativeCandidateDispatch.TopScreenUiProfile},
                   {"topscreen_gameplay_composition_observed",
                    nativeCandidateDispatch.TopScreenLowerCompositionSkips !=
                        0U},
                   {"backend_profile",
                    uiLifecycleBridge.Runtime().Profile().id},
                   {"matched_entries", bridge.matched_entries},
                   {"guest_routed_entries", bridge.guest_routed_entries},
                   {"host_routed_entries", bridge.host_routed_entries},
                   {"state_captures", bridge.state_captures},
                   {"fields_read", bridge.fields_read},
                   {"fields_missing", bridge.fields_missing},
                   {"last_guest_entry", bridge.last_guest_entry},
                   {"calls_by_subsystem", std::move(subsystemCalls)},
                   {"calls_by_phase", std::move(phaseCalls)},
                   {"observed_states", ui.observed_states},
                   {"known_file_select_states", ui.known_file_select_states},
                   {"active_file_select_states", ui.active_file_select_states},
                   {"selected_slot_known", ui.selected_slot_known},
                   {"selected_slot", ui.selected_slot},
                   {"controller_state_known", ui.controller_state_known},
                   {"controller_state", ui.controller_state},
                   {"known_name_entry_states", ui.known_name_entry_states},
                   {"active_name_entry_states", ui.active_name_entry_states},
                   {"name_entry_controller_state_known",
                    ui.name_entry_controller_state_known},
                   {"name_entry_controller_state",
                    ui.name_entry_controller_state},
                   {"name_entry_keyboard_page_known",
                    ui.name_entry_keyboard_page_known},
                   {"name_entry_keyboard_page", ui.name_entry_keyboard_page},
                   {"name_entry_cursor_known", ui.name_entry_cursor_known},
                   {"name_entry_cursor_column", ui.name_entry_cursor_column},
                   {"name_entry_cursor_row", ui.name_entry_cursor_row},
                   {"name_entry_length_known", ui.name_entry_length_known},
                   {"name_entry_length", ui.name_entry_length},
                   {"name_entry_action_choice_known",
                    ui.name_entry_action_choice_known},
                   {"name_entry_action_choice", ui.name_entry_action_choice},
                   {"name_entry_confirmation_choice_known",
                    ui.name_entry_confirmation_choice_known},
                   {"name_entry_confirmation_choice",
                    ui.name_entry_confirmation_choice},
                   {"name_entry_save_commit_timer_known",
                    ui.name_entry_save_commit_timer_known},
                   {"name_entry_save_commit_timer",
                    ui.name_entry_save_commit_timer},
                   {"shadow_presentation_frames",
                    bridge.shadow_presentation_frames},
                   {"shadow_presentation_primitives",
                    bridge.shadow_presentation_primitives},
                   {"topscreen_native_touch_copy_frames",
                    bridge.topscreen_native_touch_copy_frames},
                   {"topscreen_native_touch_copy_primitives",
                    bridge.topscreen_native_touch_copy_primitives},
                   {"topscreen_native_touch_visible_primitives",
                    bridge.topscreen_native_touch_visible_primitives},
                   {"topscreen_horse_stamina_visible_frames",
                    bridge.topscreen_horse_stamina_visible_frames},
                   {"topscreen_horse_stamina_visible_primitives",
                    bridge.topscreen_horse_stamina_visible_primitives},
                   {"native_bottom_frontend_presentation_frames",
                    nativeBottomFrontendPresentationCount},
                   {"native_bottom_overlay_frames", 0U},
                   {"native_menu_backdrop_draws_omitted",
                    nativeMenuBackdropDrawOmissionCount},
                   {"native_menu_clear_draws_omitted",
                    nativeMenuClearDrawOmissionCount},
                   {"native_frontend_composition_draws", 0U},
                   {"native_frontend_presentation_active_frames",
                    nativeFrontendPresentationActiveCount},
                   {"native_frontend_bottom_transfer_hits",
                    nativeFrontendBottomTransferHitCount},
                   {"native_frontend_composed_output_hits", 0U},
                   {"renderer_frames", renderer.frames},
                   {"renderer_primitives_drawn", renderer.primitives_drawn},
                   {"renderer_texture_uploads", renderer.texture_uploads},
                   {"renderer_texture_cache_hits", renderer.texture_cache_hits},
                   {"renderer_native_texture_resolves",
                    renderer.native_texture_resolves},
                   {"renderer_native_texture_failures",
                    renderer.native_texture_failures},
                   {"native_texture_provider_resolves",
                    nativeTextures.resolve_calls},
                   {"native_texture_provider_physical_reads",
                    nativeTextures.physical_reads},
                   {"native_texture_provider_translated_guest_reads",
                    nativeTextures.translated_guest_reads},
                   {"native_texture_provider_bytes_read",
                    nativeTextures.bytes_read},
                   {"native_texture_provider_decode_failures",
                    nativeTextures.decode_failures},
                   {"native_texture_provider_override_checks",
                    nativeTextures.override_checks},
                   {"native_texture_provider_overrides_applied",
                    nativeTextures.overrides_applied},
                   {"native_texture_provider_overrides_already_applied",
                    nativeTextures.overrides_already_applied},
                   {"native_texture_provider_override_no_matches",
                    nativeTextures.override_no_matches},
                   {"topscreen_texture_override_pack_loaded",
                    topScreenTextureOverridePackPointer != nullptr},
                   {"topscreen_texture_override_pica",
                    [&]() {
                      if (!topScreenTextureOverrideRuntime.has_value()) {
                        return nlohmann::json(nullptr);
                      }
                      const auto &overrides =
                          topScreenTextureOverrideRuntime->Stats();
                      nlohmann::json targets = nlohmann::json::array();
                      for (const auto &target :
                           topScreenTextureOverrideRuntime->TargetStats()) {
                        targets.push_back({
                            {"physical_address", target.physical_address},
                            {"guest_surface_address",
                             target.guest_surface_address},
                            {"byte_count", target.byte_count},
                            {"semantic_names", target.semantic_names},
                            {"last_width", target.last_width},
                            {"last_height", target.last_height},
                            {"last_format", target.last_format},
                            {"last_payload_hash", target.last_payload_hash},
                            {"payload_checks", target.payload_checks},
                            {"applied", target.applied},
                            {"already_applied", target.already_applied},
                            {"no_match", target.no_match},
                        });
                      }
                      return nlohmann::json{
                          {"observe_calls", overrides.observe_calls},
                          {"identities_observed",
                           overrides.identities_observed},
                          {"target_bindings", overrides.target_bindings},
                          {"payload_checks", overrides.payload_checks},
                          {"applied", overrides.applied},
                          {"already_applied", overrides.already_applied},
                          {"no_match", overrides.no_match},
                          {"targets", std::move(targets)},
                      };
                    }()},
               };
             }()},
            {"renderer_queue_events",
             std::move(widescreenProjection.RendererQueueEvents)},
            {"renderer_queue_call_counts",
             widescreenProjection.RendererQueueCallCounts},
            {"renderer_command_queue_state",
             {{"submission", readQueueState(kRendererCommandQueue)},
              {"completion", readQueueState(kRendererCommandQueue + 0xB4U)},
              {"command_words", readRendererCommandState()}}},
            {"widescreen_projection",
             {{"native_aspect",
               widescreenProjection.Projection.Presentation.NativeAspect},
              {"output_aspect",
               widescreenProjection.Projection.Presentation.OutputAspect},
              {"aspect_extension",
               static_cast<std::uint32_t>(
                   widescreenProjection.Projection.Presentation.Extension)},
              {"horizontal_expansion",
               widescreenProjection.Projection.Presentation
                   .HorizontalFovExpansion},
              {"vertical_expansion",
               widescreenProjection.Projection.Presentation
                   .VerticalFovExpansion},
              {"fov_multiplier",
               widescreenProjection.Projection.FovMultiplier},
              {"frustum_calls",
               widescreenProjection.Projection.FrustumCalls},
              {"orthographic_calls",
               widescreenProjection.Projection.OrthographicCalls},
              {"callsites", std::move(projectionCallsites)},
              {"topscreen_pause_projection",
               {{"calls", widescreenProjection.TopScreenPauseProjectionCalls},
                {"failures",
                 widescreenProjection.TopScreenPauseProjectionFailures},
                {"events", std::move(widescreenProjection
                                         .TopScreenPauseProjectionEvents)}}}}},
            {"a32_block_profile",
             {{"enabled", launch.ProfileA32Blocks},
              {"sample_rate_denominator", 64},
              {"block_entries", widescreenProjection.BlockEntries},
              {"total_samples", totalBlockSampleCount},
              {"reported_samples", blockSampleCount},
              {"blocks", std::move(sampledBlockEntries)}}},
            {"compiled_functions",
              [&]() {
                const auto stats =
                    Oot3dNativeGame::GetOot3dCompiledFunctionStats();
                nlohmann::json externalTargets = nlohmann::json::array();
                for (const auto& target :
                     Oot3dNativeGame::GetOot3dWholeAotExternalTargets()) {
                  externalTargets.push_back({
                      {"entry", target.Entry},
                      {"calls", target.Calls},
                      {"manual_compiled_calls", target.ManualCompiledCalls},
                      {"timing_samples", target.TimingSamples},
                      {"estimated_seconds",
                       target.TimingSampleNanoseconds * 64.0e-9},
                  });
                }
                const auto typedGameplayStats =
                    Oot3dNativeGame::GetOot3dTypedGameplayStats();
               const auto massAotStats =
                   Oot3dNativeGame::GetOot3dMassAotStats();
               return nlohmann::json{
                   {"enabled", !launch.DisableCompiledFunctions},
                   {"typed_gameplay_entry_points",
                    Oot3dNativeGame::Oot3dTypedGameplayEntryPoints().size()},
                   {"typed_gameplay_calls", typedGameplayStats.Calls},
                   {"typed_gameplay_angle_calls",
                    typedGameplayStats.AngleCalls},
                   {"typed_gameplay_actor_calls",
                    typedGameplayStats.ActorCalls},
                   {"typed_gameplay_enko_blink_block_calls",
                    typedGameplayStats.EnKoBlinkBlockCalls},
                   {"typed_gameplay_enko_blink_logical_advances",
                    typedGameplayStats.EnKoBlinkLogicalAdvances},
                   {"typed_gameplay_enko_blink_intermediate_holds",
                    typedGameplayStats.EnKoBlinkIntermediateHolds},
                   {"typed_gameplay_enko_blink_timer_advances",
                    typedGameplayStats.EnKoBlinkTimerAdvances},
                   {"typed_gameplay_enko_blink_sequence_advances",
                    typedGameplayStats.EnKoBlinkSequenceAdvances},
                   {"typed_gameplay_enko_blink_rng_dispatches",
                    typedGameplayStats.EnKoBlinkRngDispatches},
                   {"typed_gameplay_enko_blink_rng_returns",
                    typedGameplayStats.EnKoBlinkRngReturns},
                   {"typed_gameplay_enkanban_phase_block_calls",
                    typedGameplayStats.EnKanbanPhaseBlockCalls},
                   {"typed_gameplay_enkanban_phase_logical_advances",
                    typedGameplayStats.EnKanbanPhaseLogicalAdvances},
                   {"typed_gameplay_enkanban_phase_intermediate_holds",
                    typedGameplayStats.EnKanbanPhaseIntermediateHolds},
                   {"typed_gameplay_enkanban_state0_countdown_block_calls",
                    typedGameplayStats.EnKanbanState0CountdownBlockCalls},
                   {"typed_gameplay_enkanban_state0_countdown_logical_advances",
                    typedGameplayStats
                        .EnKanbanState0CountdownLogicalAdvances},
                   {"typed_gameplay_enkanban_state0_countdown_intermediate_holds",
                    typedGameplayStats
                        .EnKanbanState0CountdownIntermediateHolds},
                   {"typed_gameplay_enkanban_actor_flag_countdown_block_calls",
                    typedGameplayStats
                        .EnKanbanActorFlagCountdownBlockCalls},
                   {"typed_gameplay_enkanban_actor_flag_countdown_logical_advances",
                    typedGameplayStats
                        .EnKanbanActorFlagCountdownLogicalAdvances},
                   {"typed_gameplay_enkanban_actor_flag_countdown_intermediate_holds",
                    typedGameplayStats
                        .EnKanbanActorFlagCountdownIntermediateHolds},
                   {"typed_gameplay_enkanban_actor_flag_clears",
                    typedGameplayStats.EnKanbanActorFlagClears},
                   {"typed_gameplay_enkanban_interaction_cooldown_block_calls",
                    typedGameplayStats
                        .EnKanbanInteractionCooldownBlockCalls},
                   {"typed_gameplay_enkanban_interaction_cooldown_logical_advances",
                    typedGameplayStats
                        .EnKanbanInteractionCooldownLogicalAdvances},
                   {"typed_gameplay_enkanban_interaction_cooldown_intermediate_holds",
                    typedGameplayStats
                        .EnKanbanInteractionCooldownIntermediateHolds},
                   {"typed_gameplay_enkanban_interaction_cooldown_ready_passes",
                    typedGameplayStats
                        .EnKanbanInteractionCooldownReadyPasses},
                   {"typed_gameplay_enkanban_interaction_cooldown_blocked_passes",
                    typedGameplayStats
                        .EnKanbanInteractionCooldownBlockedPasses},
                   {"typed_gameplay_enkanban_draw_gate_ramp_block_calls",
                    typedGameplayStats.EnKanbanDrawGateRampBlockCalls},
                   {"typed_gameplay_enkanban_draw_gate_ramp_logical_advances",
                    typedGameplayStats.EnKanbanDrawGateRampLogicalAdvances},
                   {"typed_gameplay_enkanban_draw_gate_ramp_intermediate_holds",
                    typedGameplayStats
                        .EnKanbanDrawGateRampIntermediateHolds},
                   {"typed_gameplay_enkanban_draw_gate_ramp_inactive_passes",
                    typedGameplayStats.EnKanbanDrawGateRampInactivePasses},
                   {"typed_gameplay_enkanban_draw_gate_ramp_increase_steps",
                    typedGameplayStats.EnKanbanDrawGateRampIncreaseSteps},
                   {"typed_gameplay_enkanban_draw_gate_ramp_decrease_steps",
                    typedGameplayStats.EnKanbanDrawGateRampDecreaseSteps},
                   {"typed_gameplay_enkanban_draw_gate_ramp_clamps",
                    typedGameplayStats.EnKanbanDrawGateRampClamps},
                   {"typed_gameplay_enkanban_oscillator_axis_block_calls",
                    typedGameplayStats.EnKanbanOscillatorAxisBlockCalls},
                   {"typed_gameplay_enkanban_oscillator_x_block_calls",
                    typedGameplayStats.EnKanbanOscillatorXBlockCalls},
                   {"typed_gameplay_enkanban_oscillator_y_block_calls",
                    typedGameplayStats.EnKanbanOscillatorYBlockCalls},
                   {"typed_gameplay_enkanban_oscillator_rate_adjusted_steps",
                    typedGameplayStats
                        .EnKanbanOscillatorRateAdjustedSteps},
                   {"typed_gameplay_enkanban_oscillator_ground_resets",
                    typedGameplayStats.EnKanbanOscillatorGroundResets},
                   {"typed_gameplay_enkanban_oscillator_velocity_clamps",
                    typedGameplayStats.EnKanbanOscillatorVelocityClamps},
                   {"typed_gameplay_enkanban_piece_lifetime_block_calls",
                    typedGameplayStats.EnKanbanPieceLifetimeBlockCalls},
                   {"typed_gameplay_enkanban_piece_lifetime_logical_decrements",
                    typedGameplayStats
                        .EnKanbanPieceLifetimeLogicalDecrements},
                   {"typed_gameplay_enkanban_piece_lifetime_intermediate_holds",
                    typedGameplayStats
                        .EnKanbanPieceLifetimeIntermediateHolds},
                   {"typed_gameplay_enkanban_piece_lifetime_state_transitions",
                    typedGameplayStats
                        .EnKanbanPieceLifetimeStateTransitions},
                   {"typed_gameplay_enkanban_piece_lifetime_zero_crossing_transitions",
                    typedGameplayStats
                        .EnKanbanPieceLifetimeZeroCrossingTransitions},
                   {"typed_gameplay_enkanban_piece_lifetime_existing_zero_transitions",
                    typedGameplayStats
                        .EnKanbanPieceLifetimeExistingZeroTransitions},
                   {"typed_gameplay_enkanban_ripple_gate_calls",
                    typedGameplayStats.EnKanbanRippleGateCalls},
                   {"typed_gameplay_enkanban_ripple_dispatches",
                    typedGameplayStats.EnKanbanRippleDispatches},
                   {"typed_gameplay_enkanban_ripple_intermediate_suppressions",
                    typedGameplayStats
                        .EnKanbanRippleIntermediateSuppressions},
                   {"typed_gameplay_actor_update_all_context_freeze_block_calls",
                    typedGameplayStats
                        .ActorUpdateAllContextFreezeBlockCalls},
                   {"typed_gameplay_actor_update_all_context_freeze_logical_advances",
                    typedGameplayStats
                        .ActorUpdateAllContextFreezeLogicalAdvances},
                   {"typed_gameplay_actor_update_all_context_freeze_intermediate_holds",
                    typedGameplayStats
                        .ActorUpdateAllContextFreezeIntermediateHolds},
                   {"typed_gameplay_actor_update_all_instance_freeze_block_calls",
                    typedGameplayStats
                        .ActorUpdateAllInstanceFreezeBlockCalls},
                   {"typed_gameplay_actor_update_all_instance_freeze_logical_advances",
                    typedGameplayStats
                        .ActorUpdateAllInstanceFreezeLogicalAdvances},
                   {"typed_gameplay_actor_update_all_instance_freeze_intermediate_holds",
                    typedGameplayStats
                        .ActorUpdateAllInstanceFreezeIntermediateHolds},
                   {"typed_gameplay_actor_update_all_instance_freeze_gate_passes",
                    typedGameplayStats
                        .ActorUpdateAllInstanceFreezeGatePasses},
                   {"typed_gameplay_actor_update_all_instance_freeze_gate_skips",
                    typedGameplayStats
                        .ActorUpdateAllInstanceFreezeGateSkips},
                   {"typed_gameplay_actor_update_all_effect_timer_block_calls",
                    typedGameplayStats.ActorUpdateAllEffectTimerBlockCalls},
                   {"typed_gameplay_actor_update_all_effect_timer_logical_field_advances",
                    typedGameplayStats
                        .ActorUpdateAllEffectTimerLogicalFieldAdvances},
                   {"typed_gameplay_actor_update_all_effect_timer_intermediate_field_holds",
                    typedGameplayStats
                        .ActorUpdateAllEffectTimerIntermediateFieldHolds},
                   {"typed_gameplay_actor_init_callback_dispatches",
                    typedGameplayStats.ActorInitCallbackDispatches},
                   {"typed_gameplay_actor_init_callback_returns",
                    typedGameplayStats.ActorInitCallbackReturns},
                   {"typed_gameplay_actor_update_callback_dispatches",
                    typedGameplayStats.ActorUpdateCallbackDispatches},
                   {"typed_gameplay_actor_destroy_callback_dispatches",
                    typedGameplayStats.ActorDestroyCallbackDispatches},
                   {"typed_gameplay_actor_resource_cleanup_dispatches",
                    typedGameplayStats.ActorResourceCleanupDispatches},
                   {"typed_gameplay_actor_destroy_returns",
                    typedGameplayStats.ActorDestroyReturns},
                   {"typed_gameplay_audio_request_reference_acquire_dispatches",
                    typedGameplayStats
                        .AudioRequestReferenceAcquireDispatches},
                   {"typed_gameplay_audio_request_status_query_dispatches",
                    typedGameplayStats.AudioRequestStatusQueryDispatches},
                   {"typed_gameplay_audio_request_reference_cleanup_dispatches",
                    typedGameplayStats
                        .AudioRequestReferenceCleanupDispatches},
                   {"typed_gameplay_audio_request_callback_returns",
                    typedGameplayStats.AudioRequestCallbackReturns},
                   {"typed_gameplay_pause_ui_alpha_pause_state_dispatches",
                    typedGameplayStats.PauseUiAlphaPauseStateDispatches},
                   {"typed_gameplay_pause_ui_alpha_first_step_dispatches",
                    typedGameplayStats.PauseUiAlphaFirstStepDispatches},
                   {"typed_gameplay_pause_ui_alpha_tail_step_dispatches",
                    typedGameplayStats.PauseUiAlphaTailStepDispatches},
                   {"typed_gameplay_pause_ui_alpha_returns",
                    typedGameplayStats.PauseUiAlphaReturns},
                   {"typed_gameplay_dyna_interaction_reset_matches",
                    typedGameplayStats.DynaInteractionResetMatches},
                   {"typed_gameplay_dyna_interaction_reset_misses",
                    typedGameplayStats.DynaInteractionResetMisses},
                   {"typed_gameplay_player_release_lock_on_calls",
                    typedGameplayStats.PlayerReleaseLockOnCalls},
                   {"typed_gameplay_actor_update_record_initialize_calls",
                    typedGameplayStats.ActorUpdateRecordInitializeCalls},
                   {"typed_gameplay_actor_update_record_clear_calls",
                    typedGameplayStats.ActorUpdateRecordClearCalls},
                   {"typed_gameplay_record_initializer_memzero_dispatches",
                    typedGameplayStats.RecordInitializerMemzeroDispatches},
                   {"typed_gameplay_record_initializer_returns",
                    typedGameplayStats.RecordInitializerReturns},
                   {"typed_gameplay_player_calls",
                    typedGameplayStats.PlayerCalls},
                   {"typed_gameplay_math_calls", typedGameplayStats.MathCalls},
                   {"typed_gameplay_animation_calls",
                    typedGameplayStats.AnimationCalls},
                   {"typed_gameplay_renderer_calls",
                    typedGameplayStats.RendererCalls},
                   {"typed_gameplay_cutscene_calls",
                    typedGameplayStats.CutsceneCalls},
                   {"typed_gameplay_camera_calls",
                    typedGameplayStats.CameraCalls},
                   {"typed_gameplay_camera_mode_frame_countdown_block_calls",
                    typedGameplayStats.CameraModeFrameCountdownBlockCalls},
                   {"typed_gameplay_camera_mode_frame_countdown_logical_advances",
                    typedGameplayStats
                        .CameraModeFrameCountdownLogicalAdvances},
                   {"typed_gameplay_camera_mode_frame_countdown_intermediate_holds",
                    typedGameplayStats
                        .CameraModeFrameCountdownIntermediateHolds},
                   {"typed_gameplay_camera_special5_timer_block_calls",
                    typedGameplayStats.CameraSpecial5TimerBlockCalls},
                   {"typed_gameplay_camera_special5_timer_logical_decrements",
                    typedGameplayStats
                        .CameraSpecial5TimerLogicalDecrements},
                   {"typed_gameplay_camera_special5_timer_intermediate_holds",
                    typedGameplayStats
                        .CameraSpecial5TimerIntermediateHolds},
                   {"typed_gameplay_camera_special5_timer_zero_transitions",
                    typedGameplayStats.CameraSpecial5TimerZeroTransitions},
                   {"typed_gameplay_camera_special5_timer_terminal_continues",
                    typedGameplayStats
                        .CameraSpecial5TimerTerminalContinues},
                   {"typed_gameplay_camera_floor_miss_counter_block_calls",
                    typedGameplayStats.CameraFloorMissCounterBlockCalls},
                   {"typed_gameplay_camera_floor_miss_counter_logical_advances",
                    typedGameplayStats
                        .CameraFloorMissCounterLogicalAdvances},
                   {"typed_gameplay_camera_floor_miss_counter_intermediate_holds",
                    typedGameplayStats
                        .CameraFloorMissCounterIntermediateHolds},
                   {"typed_gameplay_camera_interface_delay_block_calls",
                    typedGameplayStats.CameraInterfaceDelayBlockCalls},
                   {"typed_gameplay_camera_interface_delay_logical_advances",
                    typedGameplayStats
                        .CameraInterfaceDelayLogicalAdvances},
                   {"typed_gameplay_camera_interface_delay_intermediate_holds",
                    typedGameplayStats
                        .CameraInterfaceDelayIntermediateHolds},
                   {"typed_gameplay_camera_water_distortion_timer_block_calls",
                    typedGameplayStats
                        .CameraWaterDistortionTimerBlockCalls},
                   {"typed_gameplay_camera_water_distortion_timer_logical_advances",
                    typedGameplayStats
                        .CameraWaterDistortionTimerLogicalAdvances},
                   {"typed_gameplay_camera_water_distortion_timer_intermediate_holds",
                    typedGameplayStats
                        .CameraWaterDistortionTimerIntermediateHolds},
                   {"typed_gameplay_camera_water_distortion_fractional_samples",
                    typedGameplayStats
                        .CameraWaterDistortionFractionalSamples},
                   {"typed_gameplay_camera_water_distortion_flag4_samples",
                    typedGameplayStats.CameraWaterDistortionFlag4Samples},
                   {"typed_gameplay_camera_water_distortion_flag8_samples",
                    typedGameplayStats.CameraWaterDistortionFlag8Samples},
                   {"typed_gameplay_camera_water_distortion_custom_samples",
                    typedGameplayStats.CameraWaterDistortionCustomSamples},
                   {"typed_gameplay_camera_water_distortion_sample_failures",
                    typedGameplayStats.CameraWaterDistortionSampleFailures},
                   {"typed_gameplay_camera_quake_callback_calls",
                    typedGameplayStats.CameraQuakeCallbackCalls},
                   {"typed_gameplay_camera_quake_logical_advances",
                    typedGameplayStats.CameraQuakeLogicalAdvances},
                   {"typed_gameplay_camera_quake_intermediate_holds",
                    typedGameplayStats.CameraQuakeIntermediateHolds},
                   {"typed_gameplay_camera_quake_random_samples",
                    typedGameplayStats.CameraQuakeRandomSamples},
                   {"typed_gameplay_camera_quake_signal_reuses",
                    typedGameplayStats.CameraQuakeSignalReuses},
                   {"typed_gameplay_camera_quake_signal_cache_misses",
                    typedGameplayStats.CameraQuakeSignalCacheMisses},
                   {"typed_gameplay_camera_quake_helper_dispatches",
                    typedGameplayStats.CameraQuakeHelperDispatches},
                   {"typed_gameplay_camera_quake_return_dispatches",
                    typedGameplayStats.CameraQuakeReturnDispatches},
                   {"typed_gameplay_camera_quake_failures",
                    typedGameplayStats.CameraQuakeFailures},
                   {"typed_gameplay_cutscene_normal_frame_block_calls",
                    typedGameplayStats.CutsceneNormalFrameBlockCalls},
                   {"typed_gameplay_cutscene_logical_frame_advances",
                    typedGameplayStats.CutsceneLogicalFrameAdvances},
                   {"typed_gameplay_cutscene_intermediate_holds",
                    typedGameplayStats.CutsceneIntermediateHolds},
                   {"typed_gameplay_cutscene_command_dispatches",
                    typedGameplayStats.CutsceneCommandDispatches},
                   {"typed_gameplay_cutscene_actor_cue_interpolation_calls",
                    typedGameplayStats.CutsceneActorCueInterpolationCalls},
                   {"typed_gameplay_cutscene_actor_cue_fractional_samples",
                    typedGameplayStats.CutsceneActorCueFractionalSamples},
                   {"typed_gameplay_cutscene_camera_binding_observations",
                    typedGameplayStats.CutsceneCameraBindingObservations},
                   {"typed_gameplay_cutscene_camera_rejected_observations",
                    typedGameplayStats.CutsceneCameraRejectedObservations},
                   {"typed_gameplay_cutscene_camera_fractional_samples",
                    typedGameplayStats.CutsceneCameraFractionalSamples},
                   {"typed_gameplay_cutscene_camera_curve_samples",
                    typedGameplayStats.CutsceneCameraCurveSamples},
                   {"typed_gameplay_cutscene_camera_sample_failures",
                    typedGameplayStats.CutsceneCameraSampleFailures},
                   {"typed_gameplay_actor_cutscene_camera_binding_observations",
                    typedGameplayStats
                        .ActorCutsceneCameraBindingObservations},
                   {"typed_gameplay_actor_cutscene_camera_priming_observations",
                    typedGameplayStats
                        .ActorCutsceneCameraPrimingObservations},
                   {"typed_gameplay_actor_cutscene_camera_rejected_observations",
                    typedGameplayStats
                        .ActorCutsceneCameraRejectedObservations},
                   {"typed_gameplay_actor_cutscene_camera_intermediate_holds",
                    typedGameplayStats.ActorCutsceneCameraIntermediateHolds},
                   {"typed_gameplay_actor_cutscene_camera_fractional_samples",
                    typedGameplayStats.ActorCutsceneCameraFractionalSamples},
                   {"typed_gameplay_actor_cutscene_camera_curve_samples",
                    typedGameplayStats.ActorCutsceneCameraCurveSamples},
                   {"typed_gameplay_actor_cutscene_camera_sample_failures",
                    typedGameplayStats.ActorCutsceneCameraSampleFailures},
                   {"typed_gameplay_fishing_state_block_calls",
                    typedGameplayStats.FishingStateBlockCalls},
                   {"typed_gameplay_fishing_state_logical_advances",
                    typedGameplayStats.FishingStateLogicalAdvances},
                   {"typed_gameplay_fishing_state_intermediate_holds",
                    typedGameplayStats.FishingStateIntermediateHolds},
                   {"typed_gameplay_damage_run_timer_block_calls",
                    typedGameplayStats.DamageRunTimerBlockCalls},
                   {"typed_gameplay_damage_run_timer_logical_advances",
                    typedGameplayStats.DamageRunTimerLogicalAdvances},
                   {"typed_gameplay_damage_run_timer_intermediate_holds",
                    typedGameplayStats.DamageRunTimerIntermediateHolds},
                   {"typed_gameplay_invincibility_timer_block_calls",
                    typedGameplayStats.InvincibilityTimerBlockCalls},
                   {"typed_gameplay_invincibility_timer_logical_advances",
                    typedGameplayStats.InvincibilityTimerLogicalAdvances},
                   {"typed_gameplay_invincibility_timer_intermediate_holds",
                    typedGameplayStats.InvincibilityTimerIntermediateHolds},
                   {"typed_gameplay_invincibility_timer_positive_action_holds",
                    typedGameplayStats
                        .InvincibilityTimerPositiveActionHolds},
                   {"typed_gameplay_damage_flicker_counter_block_calls",
                    typedGameplayStats.DamageFlickerCounterBlockCalls},
                   {"typed_gameplay_damage_flicker_counter_logical_advances",
                    typedGameplayStats
                        .DamageFlickerCounterLogicalAdvances},
                   {"typed_gameplay_damage_flicker_counter_intermediate_holds",
                    typedGameplayStats
                        .DamageFlickerCounterIntermediateHolds},
                   {"typed_gameplay_respawn_damage_block_calls",
                    typedGameplayStats.RespawnDamageBlockCalls},
                   {"typed_gameplay_respawn_damage_logical_advances",
                    typedGameplayStats.RespawnDamageLogicalAdvances},
                   {"typed_gameplay_respawn_damage_intermediate_holds",
                    typedGameplayStats.RespawnDamageIntermediateHolds},
                   {"typed_gameplay_respawn_damage_audio_dispatches",
                    typedGameplayStats.RespawnDamageAudioDispatches},
                   {"typed_gameplay_random_turn_timer_decrement_block_calls",
                    typedGameplayStats
                        .RandomTurnTimerDecrementBlockCalls},
                   {"typed_gameplay_random_turn_timer_refresh_block_calls",
                    typedGameplayStats.RandomTurnTimerRefreshBlockCalls},
                   {"typed_gameplay_random_turn_timer_logical_advances",
                    typedGameplayStats.RandomTurnTimerLogicalAdvances},
                   {"typed_gameplay_random_turn_timer_intermediate_holds",
                    typedGameplayStats.RandomTurnTimerIntermediateHolds},
                   {"typed_gameplay_random_turn_timer_rng_dispatches",
                    typedGameplayStats.RandomTurnTimerRngDispatches},
                   {"typed_gameplay_random_turn_timer_rng_intermediate_suppressions",
                    typedGameplayStats
                        .RandomTurnTimerRngIntermediateSuppressions},
                   {"typed_gameplay_attention_persistence_block_calls",
                    typedGameplayStats.AttentionPersistenceBlockCalls},
                   {"typed_gameplay_attention_persistence_logical_advances",
                    typedGameplayStats
                        .AttentionPersistenceLogicalAdvances},
                   {"typed_gameplay_attention_persistence_intermediate_holds",
                    typedGameplayStats
                        .AttentionPersistenceIntermediateHolds},
                   {"typed_gameplay_attention_persistence_saturation_holds",
                    typedGameplayStats
                        .AttentionPersistenceSaturationHolds},
                   {"typed_gameplay_common_countdown_block_calls",
                    typedGameplayStats.CommonCountdownBlockCalls},
                   {"typed_gameplay_common_countdown_logical_field_advances",
                    typedGameplayStats.CommonCountdownLogicalFieldAdvances},
                   {"typed_gameplay_common_countdown_logical_field_intermediate_holds",
                    typedGameplayStats
                        .CommonCountdownLogicalFieldIntermediateHolds},
                   {"typed_gameplay_fairy_revive_grace_timer_advances",
                    typedGameplayStats.FairyReviveGraceTimerAdvances},
                   {"typed_gameplay_underwater_timer_reset_block_calls",
                    typedGameplayStats.UnderwaterTimerResetBlockCalls},
                   {"typed_gameplay_underwater_timer_increment_block_calls",
                    typedGameplayStats.UnderwaterTimerIncrementBlockCalls},
                   {"typed_gameplay_underwater_timer_logical_advances",
                    typedGameplayStats.UnderwaterTimerLogicalAdvances},
                   {"typed_gameplay_underwater_timer_intermediate_holds",
                    typedGameplayStats.UnderwaterTimerIntermediateHolds},
                   {"typed_gameplay_melee_action_timer_block_calls",
                    typedGameplayStats.MeleeActionTimerBlockCalls},
                   {"typed_gameplay_melee_action_timer_logical_advances",
                    typedGameplayStats.MeleeActionTimerLogicalAdvances},
                   {"typed_gameplay_melee_action_timer_intermediate_holds",
                    typedGameplayStats.MeleeActionTimerIntermediateHolds},
                   {"typed_gameplay_melee_action_combo_clears",
                    typedGameplayStats.MeleeActionComboClears},
                   {"typed_gameplay_melee_weapon_tip_combo_block_calls",
                    typedGameplayStats.MeleeWeaponTipComboBlockCalls},
                   {"typed_gameplay_melee_weapon_tip_combo_logical_advances",
                    typedGameplayStats
                        .MeleeWeaponTipComboLogicalAdvances},
                   {"typed_gameplay_melee_weapon_tip_combo_intermediate_holds",
                    typedGameplayStats
                        .MeleeWeaponTipComboIntermediateHolds},
                   {"typed_gameplay_retained_aot_fallbacks",
                    typedGameplayStats.RetainedAotFallbacks},
                   {"typed_gameplay_read_failures",
                    typedGameplayStats.ReadFailures},
                   {"typed_gameplay_write_failures",
                    typedGameplayStats.WriteFailures},
                   {"mass_aot_enabled",
                    !launch.DisableCompiledFunctions &&
                        !launch.DisableMassAot &&
                        Oot3dNativeGame::Oot3dMassAotAvailable()},
                   {"mass_aot_entry_points",
                    Oot3dNativeGame::Oot3dMassAotBlockEntryPoints().size()},
                   {"mass_aot_excluded_boundary_entries",
                    Oot3dNativeGame::Oot3dMassAotExcludedBoundaryCount()},
                   {"mass_aot_calls", massAotStats.Calls},
                   {"mass_aot_blocks", massAotStats.Blocks},
                   {"mass_aot_region_exits", massAotStats.RegionExits},
                   {"mass_aot_svc_exits", massAotStats.SvcExits},
                   {"mass_aot_block_limit_exits", massAotStats.BlockLimitExits},
                   {"mass_aot_memory_faults", massAotStats.MemoryFaults},
                   {"mass_aot_missing_blocks", massAotStats.MissingBlocks},
                   {"mass_aot_fallback_exits", massAotStats.FallbackExits},
                   {"mass_aot_unsupported_exits",
                    massAotStats.UnsupportedExits},
                   {"whole_aot_available", wholeAotAvailable},
                   {"whole_aot_requested",
                    !launch.DisableCompiledFunctions &&
                        !launch.DisableWholeAot},
                   {"whole_aot_enabled", enableWholeAot},
                   {"manual_compiled_functions_enabled",
                    !launch.DisableCompiledFunctions &&
                    enableManualCompiledFunctions},
                   {"entry_points",
                    Oot3dNativeGame::Oot3dCompiledFunctionEntryPoints(
                        enableWholeAot,
                        enableCompiledFunctions &&
                            enableManualCompiledFunctions)
                        .size()},
                   {"calls", stats.Calls},
                   {"bytes_processed", stats.BytesProcessed},
                   {"retained_arm_fallbacks", stats.RetainedArmFallbacks},
                   {"memcpy_overlap_fallbacks", stats.MemcpyOverlapFallbacks},
                   {"memcpy_overlap_bytes", stats.MemcpyOverlapBytes},
                   {"memcpy_destination_after_source_fallbacks",
                    stats.MemcpyDestinationAfterSourceFallbacks},
                    {"memcpy_destination_before_source_fallbacks",
                     stats.MemcpyDestinationBeforeSourceFallbacks},
                    {"mtx3x4_fast_calls", stats.Mtx3x4FastCalls},
                    {"mtx3x4_soft_calls", stats.Mtx3x4SoftCalls},
                    {"mtx3x4_validation_calls",
                     stats.Mtx3x4ValidationCalls},
                    {"mtx3x4_value_mismatches",
                     stats.Mtx3x4ValueMismatches},
                    {"mtx3x4_flag_mismatches",
                     stats.Mtx3x4FlagMismatches},
                    {"whole_aot_calls", stats.WholeAotCalls},
                   {"whole_aot_direct_calls", stats.WholeAotDirectCalls},
                   {"whole_aot_indirect_calls", stats.WholeAotIndirectCalls},
                   {"whole_aot_resolved_indirect_calls",
                    stats.WholeAotResolvedIndirectCalls},
                    {"whole_aot_external_calls", stats.WholeAotExternalCalls},
                    {"whole_aot_external_targets",
                     std::move(externalTargets)},
                    {"whole_aot_svc_exits", stats.WholeAotSvcExits},
                   {"whole_aot_block_budget", launch.WholeAotBlockBudget},
                   {"whole_aot_block_limit_exits",
                    stats.WholeAotBlockLimitExits},
                   {"whole_aot_memory_faults", stats.WholeAotMemoryFaults},
                   {"whole_aot_unsupported_exits",
                    stats.WholeAotUnsupportedExits},
               };
             }()},
            {"true_aot_blocks",
             [&]() {
               const auto stats = Oot3dNativeGame::GetOot3dTrueAotBlockStats();
               return nlohmann::json{
                   {"enabled", !launch.DisableCompiledFunctions &&
                                   !launch.DisableTrueAotBlocks},
                   {"entry_points",
                    Oot3dNativeGame::Oot3dTrueAotBlockEntryPoints().size()},
                   {"calls", stats.Calls},
                   {"iterations", stats.Iterations},
                   {"memory_faults", stats.MemoryFaults},
               };
             }()},
#if !defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
            {"source_gameplay_profile",
             {{"requested", sourceGameplayOwners.ProfileRequested},
              {"required_owner_count",
               Oot3dNativeGame::kSourceGameplayOwnerCount},
              {"enabled_owner_count",
               sourceGameplayOwners.EnabledOwnerCount()},
              {"complete", sourceGameplayOwners.Complete()},
              {"owners",
               {{"game_state_update",
                 sourceGameplayOwners.GameStateUpdate},
                {"actor_init_context",
                 sourceGameplayOwners.ActorInitContext},
                {"actor_update_all",
                 sourceGameplayOwners.ActorUpdateAll},
                {"cutscene_update_frame",
                 sourceGameplayOwners.CutsceneUpdateFrame},
                {"cutscene_process_commands",
                 sourceGameplayOwners.CutsceneProcessCommands},
                {"camera_update",
                 sourceGameplayOwners.CameraUpdate},
                {"player_update",
                 sourceGameplayOwners.PlayerUpdate},
                {"player_update_common",
                 sourceGameplayOwners.PlayerUpdateCommon}}}}},
            {"source_actor_update_all",
             [&]() {
               const auto stats = sourceActorUpdateAll.Stats();
               return nlohmann::json{
                   {"enabled", enableSourceActorUpdateAll},
                   {"owner_calls", stats.OwnerCalls},
                   {"nested_guest_calls", stats.NestedGuestCalls},
                   {"svc_calls", stats.SvcCalls},
                   {"callback_calls", stats.CallbackCalls},
                   {"init_callback_calls", stats.InitCallbackCalls},
                   {"update_callback_calls", stats.UpdateCallbackCalls},
                   {"source_callback_calls", stats.SourceCallbackCalls},
                   {"source_obj_hana_init_calls",
                    stats.SourceObjHanaInitCalls},
                   {"source_callback_failures",
                    stats.SourceCallbackFailures},
                   {"destroy_calls", stats.DestroyCalls},
                   {"free_calls", stats.FreeCalls},
                   {"registry_release_scans", stats.RegistryReleaseScans},
                   {"failures", stats.Failures},
                   {"last_error", sourceActorUpdateAll.LastError()},
               };
             }()},
            {"source_actor_init_context",
             [&]() {
               const auto stats = sourceActorInitContext.Stats();
               return nlohmann::json{
                   {"enabled", enableSourceActorInitContext},
                   {"owner_calls", stats.OwnerCalls},
                   {"nested_guest_calls", stats.NestedGuestCalls},
                   {"direct_dependency_calls",
                    stats.DirectDependencyCalls},
                   {"dynamic_factory_calls", stats.DynamicFactoryCalls},
                   {"dynamic_allocator_calls", stats.DynamicAllocatorCalls},
                   {"actor_spawn_calls", stats.ActorSpawnCalls},
                   {"light_setup_calls", stats.LightSetupCalls},
                   {"tail_helper_calls", stats.TailHelperCalls},
                   {"failures", stats.Failures},
                   {"last_error", sourceActorInitContext.LastError()},
               };
             }()},
            {"source_cutscene_update_frame",
             [&]() {
               const auto stats = sourceCutsceneUpdateFrame.Stats();
               return nlohmann::json{
                   {"enabled", enableSourceCutsceneUpdateFrame},
                   {"owner_calls", stats.OwnerCalls},
                   {"nested_guest_calls", stats.NestedGuestCalls},
                   {"clock_query_calls", stats.ClockQueryCalls},
                   {"clock_commit_calls", stats.ClockCommitCalls},
                   {"process_command_calls", stats.ProcessCommandCalls},
                   {"failures", stats.Failures},
                   {"last_error",
                    sourceCutsceneUpdateFrame.LastError()},
               };
              }()},
            {"source_cutscene_process_commands",
             [&]() {
               const auto stats =
                   sourceCutsceneProcessCommands.Stats();
               return nlohmann::json{
                   {"enabled", enableSourceCutsceneProcessCommands},
                   {"owner_calls", stats.OwnerCalls},
                   {"nested_guest_calls", stats.NestedGuestCalls},
                   {"direct_dependency_calls",
                    stats.DirectDependencyCalls},
                   {"dynamic_callback_calls",
                    stats.DynamicCallbackCalls},
                   {"guest_read_calls", stats.GuestReadCalls},
                   {"guest_write_calls", stats.GuestWriteCalls},
                   {"scratch_calls", stats.ScratchCalls},
                   {"hard_float_calls", stats.HardFloatCalls},
                   {"conversion_operations",
                    stats.ConversionOperations},
                   {"failures", stats.Failures},
                   {"last_error",
                    sourceCutsceneProcessCommands.LastError()},
               };
             }()},
            {"source_camera_update",
             [&]() {
               const auto stats = sourceCameraUpdate.Stats();
               return nlohmann::json{
                   {"enabled", enableSourceCameraUpdate},
                   {"owner_calls", stats.OwnerCalls},
                   {"nested_guest_calls", stats.NestedGuestCalls},
                   {"direct_dependency_calls",
                    stats.DirectDependencyCalls},
                   {"dynamic_camera_function_calls",
                    stats.DynamicCameraFunctionCalls},
                   {"guest_read_calls", stats.GuestReadCalls},
                   {"guest_write_calls", stats.GuestWriteCalls},
                   {"scratch_calls", stats.ScratchCalls},
                   {"hard_float_calls", stats.HardFloatCalls},
                   {"square_root_operations",
                    stats.SquareRootOperations},
                   {"failures", stats.Failures},
                   {"last_error", sourceCameraUpdate.LastError()},
               };
             }()},
            {"source_player_update",
             [&]() {
               const auto stats = sourcePlayerUpdate.Stats();
               return nlohmann::json{
                   {"enabled", enableSourcePlayerUpdate},
                   {"owner_calls", stats.OwnerCalls},
                   {"nested_guest_calls", stats.NestedGuestCalls},
                   {"direct_dependency_calls",
                    stats.DirectDependencyCalls},
                   {"player_update_common_calls",
                    stats.PlayerUpdateCommonCalls},
                   {"actor_spawn_calls", stats.ActorSpawnCalls},
                   {"guest_read_calls", stats.GuestReadCalls},
                   {"guest_write_calls", stats.GuestWriteCalls},
                   {"scratch_calls", stats.ScratchCalls},
                   {"hard_float_calls", stats.HardFloatCalls},
                   {"vfp_operations", stats.VfpOperations},
                   {"float_conversions", stats.FloatConversions},
                   {"failures", stats.Failures},
                   {"last_error", sourcePlayerUpdate.LastError()},
               };
             }()},
            {"source_player_update_common",
             [&]() {
               const auto stats = sourcePlayerUpdateCommon.Stats();
               return nlohmann::json{
                   {"enabled", enableSourcePlayerUpdateCommon},
                   {"owner_calls", stats.OwnerCalls},
                   {"nested_guest_calls", stats.NestedGuestCalls},
                   {"direct_dependency_calls",
                    stats.DirectDependencyCalls},
                   {"dynamic_player_action_calls",
                    stats.DynamicPlayerActionCalls},
                   {"guest_read_calls", stats.GuestReadCalls},
                   {"guest_write_calls", stats.GuestWriteCalls},
                   {"hard_float_calls", stats.HardFloatCalls},
                   {"vfp_operations", stats.VfpOperations},
                   {"float_conversions", stats.FloatConversions},
                   {"square_root_operations",
                    stats.SquareRootOperations},
                   {"failures", stats.Failures},
                   {"last_error",
                    sourcePlayerUpdateCommon.LastError()},
               };
             }()},
            {"source_csab_curves",
             [&]() {
               const auto stats = sourceCsabCurve.Stats();
               return nlohmann::json{
                   {"enabled", enableSourceCsabCurves},
                   {"s16_calls", stats.S16Calls},
                   {"f32_calls", stats.F32Calls},
                   {"guest_bytes_read", stats.GuestBytesRead},
                   {"fallbacks", stats.Fallbacks},
                   {"fpscr_exception_updates",
                    stats.FpscrExceptionUpdates},
                   {"last_error", sourceCsabCurve.LastError()},
               };
             }()},
#endif
            {"a32_runtime_profile",
             [&]() {
               const auto timing = process.TimingStats();
               const auto ctrTiming = hostServices.RuntimeProfile();
               const auto picaSubmissionTiming =
                   submissionQueue.RuntimeProfile();
               const auto picaBackend = api.GetNativePicaBackendStats();
               constexpr double nanosecondsToSeconds = 1.0e-9;
               const uint64_t candidateSampleNanoseconds =
                   nativeCandidateDispatch
                       .SourceActorInitContextSampleNanoseconds +
                   nativeCandidateDispatch
                       .SourceActorUpdateAllSampleNanoseconds +
                   nativeCandidateDispatch
                       .SourceCutsceneUpdateFrameSampleNanoseconds +
                   nativeCandidateDispatch
                       .SourceCutsceneProcessCommandsSampleNanoseconds +
                   nativeCandidateDispatch
                       .SourceCameraUpdateSampleNanoseconds +
                   nativeCandidateDispatch
                       .SourcePlayerUpdateSampleNanoseconds +
                   nativeCandidateDispatch
                       .SourcePlayerUpdateCommonSampleNanoseconds +
                   nativeCandidateDispatch
                       .SourceCsabCurveSampleNanoseconds +
                   nativeCandidateDispatch.TypedGameplaySampleNanoseconds +
                   nativeCandidateDispatch.CompiledSampleNanoseconds +
                   nativeCandidateDispatch.TrueAotSampleNanoseconds +
                   nativeCandidateDispatch.UnhandledSampleNanoseconds;
               nlohmann::json svcByImmediate = nlohmann::json::array();
               for (size_t immediate = 0;
                    immediate < timing.SvcCallsByImmediate.size();
                    ++immediate) {
                 if (timing.SvcCallsByImmediate[immediate] == 0U) {
                   continue;
                 }
                 svcByImmediate.push_back({
                     {"immediate", immediate},
                     {"calls", timing.SvcCallsByImmediate[immediate]},
                     {"seconds", timing.SvcNanosecondsByImmediate[immediate] *
                                     nanosecondsToSeconds},
                 });
               }
               const auto &ipcNameCounts = hostServices.IpcNameCounts();
               nlohmann::json ipcByName = nlohmann::json::array();
               for (const auto &[name, calls] : ipcNameCounts) {
                 ipcByName.push_back({
                     {"name", name},
                     {"calls", calls},
                 });
               }
               const uint64_t measuredTriggerNanoseconds =
                   ctrTiming.CommandListReadNanoseconds +
                   ctrTiming.PicaFrontendSubmitNanoseconds;
               const uint64_t triggerPostNanoseconds =
                   ctrTiming.TriggerCommandQueueNanoseconds >
                           measuredTriggerNanoseconds
                       ? ctrTiming.TriggerCommandQueueNanoseconds -
                             measuredTriggerNanoseconds
                       : 0U;
               return nlohmann::json{
                   {"enabled", launch.ProfileA32Runtime},
                   {"sample_rate_denominator", kA32RuntimeSampleDenominator},
                   {"process_run_calls", timing.ProcessRunCalls},
                   {"dispatch_calls", timing.DispatchCalls},
                   {"dispatch_seconds",
                    timing.DispatchNanoseconds * nanosecondsToSeconds},
                   {"svc_calls", timing.SvcCalls},
                   {"svc_seconds",
                    timing.SvcNanoseconds * nanosecondsToSeconds},
                   {"svc_event_count", hostServices.SvcEventCount()},
                   {"svc_by_immediate", std::move(svcByImmediate)},
                   {"ipc_by_name", std::move(ipcByName)},
                   {"romfs_stream_open_calls",
                    ctrTiming.RomFsStreamOpenCalls},
                   {"romfs_read_calls", ctrTiming.RomFsReadCalls},
                   {"romfs_read_bytes", ctrTiming.RomFsReadBytes},
                   {"romfs_read_seconds",
                    ctrTiming.RomFsReadNanoseconds * nanosecondsToSeconds},
                   {"gsp_trigger_command_queue_calls",
                    ctrTiming.TriggerCommandQueueCalls},
                   {"gsp_trigger_command_queue_seconds",
                    ctrTiming.TriggerCommandQueueNanoseconds *
                        nanosecondsToSeconds},
                   {"gsp_command_packets", ctrTiming.CommandPackets},
                   {"gsp_command_list_bytes", ctrTiming.CommandListBytes},
                   {"gsp_command_list_read_seconds",
                    ctrTiming.CommandListReadNanoseconds *
                        nanosecondsToSeconds},
                   {"gsp_pica_frontend_submit_seconds",
                    ctrTiming.PicaFrontendSubmitNanoseconds *
                        nanosecondsToSeconds},
                   {"gsp_trigger_post_seconds",
                    triggerPostNanoseconds * nanosecondsToSeconds},
                   {"pica_submission_draw_calls",
                    picaSubmissionTiming.DrawCalls},
                   {"pica_submission_total_seconds",
                    picaSubmissionTiming.TotalNanoseconds *
                        nanosecondsToSeconds},
                   {"pica_submission_packet_copy_seconds",
                    picaSubmissionTiming.PacketCopyNanoseconds *
                        nanosecondsToSeconds},
                   {"pica_submission_decode_seconds",
                    picaSubmissionTiming.DecodeNanoseconds *
                        nanosecondsToSeconds},
                   {"pica_submission_vertex_capture_seconds",
                    picaSubmissionTiming.VertexCaptureNanoseconds *
                        nanosecondsToSeconds},
                   {"pica_submission_texture_capture_seconds",
                    picaSubmissionTiming.TextureCaptureNanoseconds *
                        nanosecondsToSeconds},
                   {"pica_submission_enqueue_seconds",
                    picaSubmissionTiming.EnqueueNanoseconds *
                        nanosecondsToSeconds},
                   {"pica_submission_index_bytes",
                    picaSubmissionTiming.IndexBytes},
                   {"pica_submission_vertex_bytes",
                    picaSubmissionTiming.VertexBytes},
                   {"pica_submission_texture_bytes",
                    picaSubmissionTiming.TextureBytes},
                   {"pica_submission_texture_snapshot_cache_hits",
                    picaSubmissionTiming.TextureSnapshotCacheHits},
                   {"pica_submission_texture_snapshot_cache_misses",
                    picaSubmissionTiming.TextureSnapshotCacheMisses},
                   {"pica_submission_texture_snapshot_compared_bytes",
                    picaSubmissionTiming.TextureSnapshotComparedBytes},
                   {"pica_submission_texture_snapshot_copied_bytes",
                    picaSubmissionTiming.TextureSnapshotCopiedBytes},
                   {"pica_submission_geometry_snapshot_cache_hits",
                    picaSubmissionTiming.GeometrySnapshotCacheHits},
                   {"pica_submission_geometry_snapshot_cache_misses",
                    picaSubmissionTiming.GeometrySnapshotCacheMisses},
                   {"pica_submission_geometry_snapshot_version_hits",
                    picaSubmissionTiming.GeometrySnapshotVersionHits},
                   {"pica_submission_geometry_snapshot_version_misses",
                    picaSubmissionTiming.GeometrySnapshotVersionMisses},
                   {"pica_submission_geometry_snapshot_compared_bytes",
                    picaSubmissionTiming.GeometrySnapshotComparedBytes},
                   {"pica_submission_geometry_snapshot_copied_bytes",
                    picaSubmissionTiming.GeometrySnapshotCopiedBytes},
                   {"pica_backend_geometry_registry_hits",
                    picaBackend.GeometryRegistryHits},
                   {"pica_backend_stats_available",
                    picaBackend.Available},
                   {"pica_backend_geometry_cache_enabled",
                    picaBackend.GeometryCacheEnabled},
                   {"pica_backend_geometry_registry_misses",
                    picaBackend.GeometryRegistryMisses},
                   {"pica_backend_geometry_registry_updates",
                    picaBackend.GeometryRegistryUpdates},
                   {"pica_backend_geometry_registry_evictions",
                    picaBackend.GeometryRegistryEvictions},
                   {"pica_backend_geometry_registry_entries",
                    picaBackend.GeometryRegistryEntries},
                   {"pica_backend_geometry_persistent_draws",
                    picaBackend.GeometryPersistentDraws},
                   {"pica_backend_geometry_streaming_draws",
                    picaBackend.GeometryStreamingDraws},
                   {"pica_backend_geometry_persistent_uploads",
                    picaBackend.GeometryPersistentUploads},
                   {"pica_backend_geometry_persistent_upload_bytes",
                    picaBackend.GeometryPersistentUploadBytes},
                   {"pica_backend_geometry_streaming_uploads",
                    picaBackend.GeometryStreamingUploads},
                   {"pica_backend_geometry_streaming_upload_bytes",
                    picaBackend.GeometryStreamingUploadBytes},
                   {"pica_backend_uniform_uploads",
                    picaBackend.UniformUploads},
                   {"pica_backend_uniform_upload_bytes",
                    picaBackend.UniformUploadBytes},
                   {"fallback_calls", timing.FallbackCalls},
                   {"fallback_seconds",
                    timing.FallbackNanoseconds * nanosecondsToSeconds},
                   {"candidate_calls", nativeCandidateDispatch.Calls},
                   {"topscreen_source_port_calls",
                    nativeCandidateDispatch.TopScreenSourcePortCalls},
                   {"topscreen_title_logo_fade_hold_calls",
                    nativeCandidateDispatch.TopScreenTitleLogoFadeHoldCalls},
                   {"topscreen_lower_composition_skips",
                    nativeCandidateDispatch.TopScreenLowerCompositionSkips},
                   {"topscreen_pause_target_observations",
                    widescreenProjection.TopScreenPauseTargetObservations},
                   {"topscreen_pause_target_commands",
                    widescreenProjection.TopScreenPauseTargetCommands},
                   {"topscreen_pause_target_rewrites",
                    widescreenProjection.TopScreenPauseTargetRewrites},
                   {"topscreen_pause_start_close_calls",
                    nativeCandidateDispatch.TopScreenPauseStartCloseCalls},
                   {"topscreen_pause_system_open_calls",
                    nativeCandidateDispatch.TopScreenPauseSystemOpenCalls},
                   {"topscreen_aim_projectile_cycle_attempts",
                    nativeCandidateDispatch
                        .TopScreenAimProjectileCycleAttempts},
                   {"topscreen_aim_projectile_cycle_updates",
                    nativeCandidateDispatch
                        .TopScreenAimProjectileCycleUpdates},
                   {"topscreen_aim_projectile_cycle_sound_calls",
                    nativeCandidateDispatch
                        .TopScreenAimProjectileCycleSoundCalls},
                   {"topscreen_gameplay_dpad_attempts",
                    nativeCandidateDispatch.TopScreenGameplayDpadAttempts},
                   {"topscreen_navi_view_activations",
                    nativeCandidateDispatch.TopScreenNaviViewActivations},
                   {"topscreen_ocarina_queries",
                    nativeCandidateDispatch.TopScreenOcarinaQueries},
                   {"topscreen_ocarina_activations",
                    nativeCandidateDispatch.TopScreenOcarinaActivations},
                   {"topscreen_pause_last_closed_page",
                    static_cast<uint32_t>(
                        nativeCandidateDispatch.TopScreenPauseLastClosedPage)},
                   {"topscreen_pause_system_source_page",
                    static_cast<uint32_t>(nativeCandidateDispatch
                                              .TopScreenPauseSystemSourcePage)},
                   {"topscreen_pause_child_suppressions",
                    widescreenProjection.TopScreenPauseChildSuppressions},
                   {"topscreen_pause_child_native_draws",
                    widescreenProjection.TopScreenPauseChildNativeDraws},
                   {"topscreen_pause_page_composition_calls",
                    widescreenProjection.TopScreenPausePageCompositionCalls},
                   {"topscreen_pause_page_redraws",
                    widescreenProjection.TopScreenPausePageRedraws},
                   {"topscreen_pause_page_redraw_skips",
                    widescreenProjection.TopScreenPausePageRedrawSkips},
                   {"topscreen_pause_last_route_active",
                    widescreenProjection.LastTopScreenPauseRouteActive},
                   {"topscreen_pause_last_children_suppressed",
                    widescreenProjection
                        .LastTopScreenPauseChildrenSuppressed},
                   {"topscreen_pause_last_inputs",
                    widescreenProjection.HasTopScreenPauseDrawInputs
                        ? nlohmann::json{
                              {"has_scene",
                               widescreenProjection
                                   .LastTopScreenPauseDrawInputs.HasScene},
                              {"scene_mode",
                               widescreenProjection
                                   .LastTopScreenPauseDrawInputs.SceneMode},
                              {"scene_variant",
                               widescreenProjection
                                   .LastTopScreenPauseDrawInputs.SceneVariant},
                              {"scene_sequence",
                               widescreenProjection
                                   .LastTopScreenPauseDrawInputs.SceneSequence},
                              {"runtime_mode",
                               widescreenProjection
                                   .LastTopScreenPauseDrawInputs.RuntimeMode},
                              {"native_transition_active",
                               widescreenProjection.LastTopScreenPauseDrawInputs
                                   .NativeTransitionActive},
                              {"alternate_path_ready",
                               widescreenProjection.LastTopScreenPauseDrawInputs
                                   .AlternatePathReady},
                              {"alternate_path_visible",
                               widescreenProjection.LastTopScreenPauseDrawInputs
                                   .AlternatePathVisible},
                              {"suppression_override",
                               widescreenProjection.LastTopScreenPauseDrawInputs
                                   .SuppressionOverride},
                              {"any_page_gate_active",
                               widescreenProjection.LastTopScreenPauseDrawInputs
                                   .AnyPageGateActive},
                              {"gear_state",
                               widescreenProjection.LastTopScreenPauseDrawInputs
                                   .ChildStates[0]},
                              {"items_state",
                               widescreenProjection.LastTopScreenPauseDrawInputs
                                   .ChildStates[1]},
                              {"dungeon_map_state",
                               widescreenProjection.LastTopScreenPauseDrawInputs
                                   .ChildStates[2]},
                              {"system_state",
                               widescreenProjection.LastTopScreenPauseDrawInputs
                                   .ChildStates[3]},
                              {"selector_state",
                               widescreenProjection.LastTopScreenPauseDrawInputs
                                    .ChildStates[4]},
                              {"touch_button_state",
                               widescreenProjection.LastTopScreenPauseDrawInputs
                                   .ChildStates[5]},
                          }
                        : nlohmann::json(nullptr)},
                   {"topscreen_pause_route_state",
                    {{"previous_runtime_mode",
                      nativeCandidateDispatch.TopScreenPauseRoute
                          .PreviousRuntimeMode},
                     {"transition_phase",
                      nativeCandidateDispatch.TopScreenPauseRoute
                          .TransitionPhase},
                     {"remaining_calls",
                      nativeCandidateDispatch.TopScreenPauseRoute
                          .RemainingCalls}}},
                   {"topscreen_temporal_state",
                    {{"viewport_draw_phase",
                      nativeCandidateDispatch.TopScreenViewportDrawPhase},
                     {"scene_viewport_draw_phase",
                      nativeCandidateDispatch.TopScreenSceneViewportDrawPhase},
                     {"overlay_viewport_draw_phase",
                      nativeCandidateDispatch.TopScreenOverlayViewportDrawPhase},
                     {"touch_coordinate_update_phase",
                      nativeCandidateDispatch
                          .TopScreenTouchCoordinateUpdatePhase},
                     {"alternate_renderer_restore",
                      nativeCandidateDispatch.TopScreenAlternateRendererRestore
                          .has_value()},
                     {"aot_route_previous_runtime_mode",
                      nativeCandidateDispatch.TopScreenAotPauseRoute
                          .PreviousRuntimeMode},
                     {"aot_route_transition_phase",
                      nativeCandidateDispatch.TopScreenAotPauseRoute
                          .TransitionPhase},
                     {"aot_route_remaining_calls",
                      nativeCandidateDispatch.TopScreenAotPauseRoute
                          .RemainingCalls},
                     {"touch_runtime_scene_latch",
                      nativeCandidateDispatch.TopScreenTouchCoordinateRoute
                          .RuntimeSceneLatch},
                     {"renderer_visibility_fade_step",
                      nativeCandidateDispatch.TopScreenRendererVisibilityRoute
                          .FadeStep},
                     {"renderer_visibility_delay_calls",
                      nativeCandidateDispatch.TopScreenRendererVisibilityRoute
                          .DelayCalls},
                     {"renderer_visibility_conflict",
                      nativeCandidateDispatch.TopScreenRendererVisibilityRoute
                          .RendererConflict},
                     {"pause_controller_routed_latch",
                      nativeCandidateDispatch.TopScreenPauseController
                          .RoutedRendererLatched},
                     {"pause_controller_draw_phase",
                      nativeCandidateDispatch
                          .TopScreenPauseControllerDrawPhase},
                     {"pending_items_selection",
                      nativeCandidateDispatch.PendingTopScreenItemsSelection},
                     {"pending_target",
                      widescreenProjection.PendingTopScreenPauseTarget
                          .has_value()},
                     {"pending_target_renderer",
                      widescreenProjection.PendingTopScreenPauseRenderer},
                     {"draw_native_transition_latched",
                      widescreenProjection.TopScreenPauseDrawRouting
                          .NativeTransitionLatched},
                     {"draw_suppression_delay_armed",
                      widescreenProjection.TopScreenPauseDrawRouting
                          .SuppressionDelayArmed},
                     {"draw_suppression_delay_commands",
                      widescreenProjection.TopScreenPauseDrawRouting
                          .SuppressionDelayCommands},
                     {"page_redraw_delay_calls",
                      widescreenProjection.TopScreenPausePageRedraw.DelayCalls},
                     {"page_redraw_active",
                      widescreenProjection.TopScreenPausePageRedrawActive},
                     {"pending_child_restore",
                      widescreenProjection.PendingTopScreenPauseChildRestore
                          .has_value()}}},
                   {"topscreen_camera_normal1_calls",
                    nativeCandidateDispatch.TopScreenCameraNormal1Calls},
                   {"topscreen_camera_update_calls",
                    nativeCandidateDispatch.TopScreenCameraUpdateCalls},
                   {"topscreen_camera_active_calls",
                    nativeCandidateDispatch.TopScreenCameraActiveCalls},
                   {"topscreen_camera_enabled",
                    nativeCandidateDispatch.TopScreenCamera.Camera.Enabled},
                   {"topscreen_camera_n64_style_zoom",
                    nativeCandidateDispatch.TopScreenCamera.Camera
                        .N64StyleZoom},
                   {"topscreen_camera_last_blockers",
                    nativeCandidateDispatch.TopScreenCamera
                        .LastOwnershipBlockers},
                   {"topscreen_camera_last_ownership",
                    {{"game_mode",
                      nativeCandidateDispatch.TopScreenCamera
                          .LastOwnershipInput.GameMode},
                     {"entrance_index",
                      nativeCandidateDispatch.TopScreenCamera
                          .LastOwnershipInput.EntranceIndex},
                     {"cutscene_index",
                      nativeCandidateDispatch.TopScreenCamera
                          .LastOwnershipInput.CutsceneIndex},
                     {"scene",
                      nativeCandidateDispatch.TopScreenCamera
                          .LastOwnershipInput.Scene},
                     {"is_main_camera",
                      nativeCandidateDispatch.TopScreenCamera
                          .LastOwnershipInput.IsMainCamera},
                     {"has_player",
                      nativeCandidateDispatch.TopScreenCamera
                          .LastOwnershipInput.HasPlayer},
                     {"player_state_flags_1",
                      nativeCandidateDispatch.TopScreenCamera
                          .LastOwnershipInput.PlayerStateFlags1},
                     {"player_state_flags_2",
                      nativeCandidateDispatch.TopScreenCamera
                          .LastOwnershipInput.PlayerStateFlags2},
                     {"status",
                      nativeCandidateDispatch.TopScreenCamera
                          .LastOwnershipInput.CameraStatus},
                     {"setting",
                      nativeCandidateDispatch.TopScreenCamera
                          .LastOwnershipInput.CameraSetting},
                     {"right_stick_x",
                      nativeCandidateDispatch.TopScreenCamera
                          .LastOwnershipInput.RightStickX},
                     {"right_stick_y",
                      nativeCandidateDispatch.TopScreenCamera
                          .LastOwnershipInput.RightStickY}}},
                   {"topscreen_item_query_calls",
                    nativeCandidateDispatch.TopScreenItemQueryCalls},
                   {"topscreen_item_query_true_results",
                    nativeCandidateDispatch.TopScreenItemQueryTrueResults},
                   {"topscreen_slot_item_attempts",
                    nativeCandidateDispatch.TopScreenSlotItemAttempts},
                   {"topscreen_slot_item_overrides",
                    nativeCandidateDispatch.TopScreenSlotItemOverrides},
                   {"topscreen_slot_item_native_fallbacks",
                    nativeCandidateDispatch.TopScreenSlotItemNativeFallbacks},
                   {"topscreen_item_query_entries",
                    nativeCandidateDispatch.TopScreenItemQueryEntries},
                   {"topscreen_slot_item_entries",
                    nativeCandidateDispatch.TopScreenSlotItemEntries},
                   {"topscreen_input_zr_held_frames",
                    nativeCandidateDispatch.TopScreenZrHeldFrames},
                   {"topscreen_input_zl_held_frames",
                    nativeCandidateDispatch.TopScreenZlHeldFrames},
                   {"topscreen_input_zr_host_edges",
                    nativeCandidateDispatch.TopScreenZrHostEdges},
                   {"topscreen_input_zl_host_edges",
                    nativeCandidateDispatch.TopScreenZlHostEdges},
                   {"topscreen_input_zr_guest_edges",
                    nativeCandidateDispatch.TopScreenZrGuestEdges},
                   {"topscreen_input_zl_guest_edges",
                    nativeCandidateDispatch.TopScreenZlGuestEdges},
                   {"candidate_samples", nativeCandidateDispatch.Samples},
                   {"candidate_estimated_seconds",
                    candidateSampleNanoseconds * kA32RuntimeSampleDenominator *
                        nanosecondsToSeconds},
                   {"source_actor_init_context_samples",
                    nativeCandidateDispatch.SourceActorInitContextSamples},
                   {"source_actor_init_context_estimated_seconds",
                    nativeCandidateDispatch
                            .SourceActorInitContextSampleNanoseconds *
                        kA32RuntimeSampleDenominator * nanosecondsToSeconds},
                   {"source_actor_update_all_samples",
                    nativeCandidateDispatch.SourceActorUpdateAllSamples},
                   {"source_actor_update_all_estimated_seconds",
                    nativeCandidateDispatch
                            .SourceActorUpdateAllSampleNanoseconds *
                        kA32RuntimeSampleDenominator * nanosecondsToSeconds},
                   {"source_cutscene_update_frame_samples",
                    nativeCandidateDispatch
                        .SourceCutsceneUpdateFrameSamples},
                   {"source_cutscene_update_frame_estimated_seconds",
                    nativeCandidateDispatch
                            .SourceCutsceneUpdateFrameSampleNanoseconds *
                        kA32RuntimeSampleDenominator * nanosecondsToSeconds},
                   {"source_cutscene_process_commands_samples",
                    nativeCandidateDispatch
                        .SourceCutsceneProcessCommandsSamples},
                   {"source_cutscene_process_commands_estimated_seconds",
                    nativeCandidateDispatch
                            .SourceCutsceneProcessCommandsSampleNanoseconds *
                        kA32RuntimeSampleDenominator * nanosecondsToSeconds},
                   {"source_camera_update_samples",
                    nativeCandidateDispatch.SourceCameraUpdateSamples},
                   {"source_camera_update_estimated_seconds",
                    nativeCandidateDispatch
                            .SourceCameraUpdateSampleNanoseconds *
                        kA32RuntimeSampleDenominator * nanosecondsToSeconds},
                   {"source_player_update_samples",
                    nativeCandidateDispatch.SourcePlayerUpdateSamples},
                   {"source_player_update_estimated_seconds",
                    nativeCandidateDispatch
                            .SourcePlayerUpdateSampleNanoseconds *
                        kA32RuntimeSampleDenominator * nanosecondsToSeconds},
                   {"source_player_update_common_samples",
                    nativeCandidateDispatch
                        .SourcePlayerUpdateCommonSamples},
                   {"source_player_update_common_estimated_seconds",
                    nativeCandidateDispatch
                            .SourcePlayerUpdateCommonSampleNanoseconds *
                        kA32RuntimeSampleDenominator *
                        nanosecondsToSeconds},
                   {"source_csab_curve_samples",
                    nativeCandidateDispatch.SourceCsabCurveSamples},
                   {"source_csab_curve_estimated_seconds",
                    nativeCandidateDispatch
                            .SourceCsabCurveSampleNanoseconds *
                        kA32RuntimeSampleDenominator *
                        nanosecondsToSeconds},
                   {"typed_gameplay_samples",
                    nativeCandidateDispatch.TypedGameplaySamples},
                   {"typed_gameplay_estimated_seconds",
                    nativeCandidateDispatch.TypedGameplaySampleNanoseconds *
                        kA32RuntimeSampleDenominator * nanosecondsToSeconds},
                   {"compiled_samples",
                    nativeCandidateDispatch.CompiledSamples},
                   {"compiled_estimated_seconds",
                    nativeCandidateDispatch.CompiledSampleNanoseconds *
                        kA32RuntimeSampleDenominator * nanosecondsToSeconds},
                   {"true_aot_samples", nativeCandidateDispatch.TrueAotSamples},
                   {"true_aot_estimated_seconds",
                    nativeCandidateDispatch.TrueAotSampleNanoseconds *
                        kA32RuntimeSampleDenominator * nanosecondsToSeconds},
                   {"unhandled_samples",
                    nativeCandidateDispatch.UnhandledSamples},
                   {"unhandled_estimated_seconds",
                    nativeCandidateDispatch.UnhandledSampleNanoseconds *
                        kA32RuntimeSampleDenominator * nanosecondsToSeconds},
               };
             }()},
            {"phase_timing",
             {{"guest_seconds", phaseTiming.GuestSeconds},
              {"frame_start_seconds", phaseTiming.FrameStartSeconds},
              {"dsp_mix_seconds", phaseTiming.DspMixSeconds},
              {"audio_output_seconds", phaseTiming.AudioOutputSeconds},
              {"pica_submit_seconds", phaseTiming.PicaSubmitSeconds},
              {"pica_plan_seconds", phaseTiming.PicaPlanSeconds},
              {"pica_backend_seconds", phaseTiming.PicaBackendSeconds},
              {"pica_diagnostics_seconds", phaseTiming.PicaDiagnosticsSeconds},
              {"visual_presentation_seconds",
               phaseTiming.VisualPresentationSeconds},
              {"visual_replay_seconds", phaseTiming.VisualReplaySeconds},
              {"present_seconds", phaseTiming.PresentSeconds}}},
            {"shader_source_cache",
             {{"vertex_entries", shaderSourceCache.VertexSources.size()},
              {"vertex_structural_entries",
               shaderSourceCache.VertexStructuralStates.size()},
              {"vertex_hits", shaderSourceCache.VertexHits},
              {"vertex_misses", shaderSourceCache.VertexMisses},
              {"fragment_entries", shaderSourceCache.FragmentSources.size()},
              {"fragment_hits", shaderSourceCache.FragmentHits},
              {"fragment_misses", shaderSourceCache.FragmentMisses}}},
            {"authority", "oot3d_a32_process_pica_vulkan"},
        });
  }

  if (launch.ScenarioStrict &&
      (!scenarioBootstrap.has_value() || !scenarioBootstrap->Complete())) {
    const std::string detail =
        scenarioBootstrap.has_value()
            ? scenarioBootstrap->Stats().Error
            : "structural scenario bootstrap is unavailable";
    throw std::runtime_error("structural scenario did not complete capture" +
                             (detail.empty() ? std::string() : ": " + detail));
  }
}
