#pragma once

// The native A32 window session: one run of the game window, from setup through the frame loop to the
// runtime summary. Methods: oot3d_native_a32_window.cpp (driver and guest-run helpers), _setup.cpp,
// _frame.cpp (frame loop and renderer side) and _savestate.cpp.

#include "three_ds_sdl_controller.h"
#include "mm3d_pica_composition.h"
#include "mm3d_title_contracts.h"
#include "triaevum_title_adapter.h"
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
#include "oot3d_savestate_library.h"
#include "oot3d_savestate_panel.h"
#include "oot3d_savestate_settings.h"
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

#include "oot3d_native_a32_window_shared.h"
#include "oot3d_native_a32_window_stats.h"

namespace Oot3dA32Window {

#if defined(__SWITCH__)
inline size_t gSwitchBlockHookCount = 0U;
inline bool gSwitchBlockHooksSorted = false;
inline bool gSwitchBlockHooksContainProcessEntry = false;
inline uint64_t gSwitchRuntimeHostFrames = 0U;
inline uint32_t gSwitchRuntimeGuestFrames = 0U;
inline std::string gSwitchUiProfile = "unknown";
inline std::string gSwitchGameplayTiming = "unknown";
inline std::string gSwitchControlProfile = "unknown";
inline uint32_t gSwitchPresentationRateHz = 0U;
inline bool gSwitchVisualInterpolation = false;
inline bool gSwitchTopScreen211Assets = false;
inline uint32_t gSwitchWholeAotBlockBudget = 0U;
inline uint64_t gSwitchWholeAotBlockLimitExits = 0U;
inline bool gSwitchPicaGeometryCacheEnabled = false;
inline uint64_t gSwitchPicaGeometryPersistentDraws = 0U;
inline uint64_t gSwitchPicaGeometryPersistentUploads = 0U;
inline uint64_t gSwitchPicaGeometryRegistryEntries = 0U;
inline bool gSwitchPicaGeometryCacheObserved = false;

void WriteSwitchBootStage(const char* stage);
#endif

inline double SecondsSince(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
      .count();
}

Oot3dNativeGame::NativeA32ProcessRunResult
RunUntilGuestWait(Oot3dNativeGame::NativeA32Process &process,
                  uint32_t wholeAotBlockBudget);

void RequireRunnableGuest(
    const Oot3dNativeGame::NativeA32ProcessRunResult &result);

void WriteA32BlockTrace(const std::filesystem::path &path,
                        const NativeWidescreenProjectionState &runtime);

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

// One run of the native A32 window. The members are the former locals of RunOot3dNativeA32Window, in
// declaration order and with their names: Setup() is the code before the frame loop, RunFrame() its body
// and Finish() the code after it; the setup's lambdas are member functions. A member constructed with
// arguments lives in `<name>Storage` and is emplaced where the local was declared; a reference lives in
// `<name>Pointer`. Methods bind `<name>` to either on entry, so the bodies read as before.
class NativeA32WindowSession {
public:
  explicit NativeA32WindowSession(const Oot3dNativeGameLaunch &launch) : launch(launch) {}
  NativeA32WindowSession(const NativeA32WindowSession &) = delete;
  NativeA32WindowSession &operator=(const NativeA32WindowSession &) = delete;

  void Run();

private:
  struct CopyWriteObserverLifetime {
    Oot3dNativeGame::NativeA32Memory& Memory;
    ~CopyWriteObserverLifetime() { Memory.SetWriteObserver({}); }
  };

  struct PhysicalMemoryAccessLifetime {
    Fast::Renderer3ds::PicaRenderBackend& Backend;
    ~PhysicalMemoryAccessLifetime() { Backend.SetPicaPhysicalMemoryAccess({}, {}, {}, {}, {}); }
  };

  struct RenderStreamStop {
    Oot3dNativeGame::NativeRenderCommandStream& Stream;
    ~RenderStreamStop() {
      try {
        Stream.SetMode(Oot3dNativeGame::NativeRenderCommandStream::Mode::Inline);
      } catch (...) {
      }
    }
  };

  struct GuiRendererRoutingReset {
    Fast::Fast3dGui* Gui;
    ~GuiRendererRoutingReset() {
      if (Gui != nullptr) Gui->SetRendererRouting({});
    }
  };

  void Setup();
  // One iteration of the frame loop; false when the frame closed the window.
  bool RunFrame();
  // The phases of one guest refresh inside RunFrame() (oot3d_native_a32_window_frame.cpp).
  bool SampleGuestRefreshInput(
      const uint32_t &guestRefreshIndex,
      bool &nativeFrontendTouchEnabled,
      uint32_t &topScreenNativeGameMode,
      bool &topScreenStartRoutingEligible,
      Oot3dNativeGame::NativeA32InputFrame &physicalInputFrame,
      Oot3dNativeGame::NativeA32InputFrame &inputFrame);
  bool UpdateGuestServicesForRefresh(
      bool &topScreenStartRoutingEligible,
      Oot3dNativeGame::NativeA32InputFrame &inputFrame);
  void RunGuestRefresh();
  void PresentVisualFrame(const double &presentationElapsedSeconds, const float &visualInterpolationAlpha);
  void Finish();
  // The setup's former lambdas.
  void ApplyTopScreenConfigSnapshot();
  void SetTopScreenSessionMinimapVisible(bool visible);
  void ApplySelectedUiProfile();
  void UpdateScenePresentation();
  void RestoreSavestateTiming(const Oot3dNativeGame::NativeA32SavestateRuntimeState &runtime);
  void RestoreSavestateTopScreenTemporalState(const Oot3dNativeGame::NativeA32SavestateRuntimeState &runtime);
  void RestoreSavestatePicaTextureCache(const Oot3dNativeGame::NativeA32SavestateRuntimeState &runtime);
  void RestoreSavestatePicaColorTargets(const Oot3dNativeGame::NativeA32SavestateRuntimeState &runtime);
  void RestoreSavestatePicaPresentationState(const Oot3dNativeGame::NativeA32SavestateRuntimeState &runtime);
  void RestoreSavestatePicaVisualReplayState(const Oot3dNativeGame::NativeA32SavestateRuntimeState &runtime);
  Fast::Renderer3ds::PicaFrameCompositionPolicy ResolveFrameCompositionPolicy(const Fast::Oot3d::PresentationPacingPolicy& pacing);
  void PublishNativeSceneView();
  void RequireStoppedGuest();
  std::vector<Fast::Renderer3ds::PicaTextureCopyReference> CollectGuestCopyReferences();
  void PinGpuCopyResources(const Oot3dNativeGame::Oot3dPicaVisualFrameViewSample& sample);
  bool FlushNativeDependencies();
  void ExecuteVisualSample(const Oot3dNativeGame::Oot3dPicaVisualFrameViewSample &sample, Oot3dNativeGame::Oot3dPicaPresentationExecutionKind kind, bool present, const Fast::Oot3d::NativeFrameTemporalSample& temporalSample, uint64_t renderTargetNamespace = kVisualInterpolationRenderTargetNamespace);
  // Deferred: a host continuation is pending, retry at a later frame boundary. Failed: reported once; the request is
  // dropped because every retry repeats the full GPU drain and readback.
  enum class SaveStateOutcome : uint8_t { Saved, Deferred, Failed };
  // A save state holds the game moment only; withSettings also stores the host settings beside it, or
  // storedSettings verbatim when given (a conversion keeps the settings block it loaded).
  SaveStateOutcome SaveState(const std::filesystem::path &path, bool withSettings = false,
                             const std::string *storedSettings = nullptr);
  SaveStateOutcome FailSave(std::string message);
  // applySettings applies the settings a state carries before the renderer restore, so the restored
  // content is rebuilt at their scale. lastSavestateMessage describes the outcome.
  bool LoadState(const std::filesystem::path &path, bool applySettings = false);
  void ApplyLoadedSavestateSettings(const Oot3dNativeGame::NativeA32SavestateRuntimeState &runtime,
                                    bool applySettings);
  // F5/F8 and the F1 Save states tab: one request per frame boundary.
  void ServiceSavestateRequests();
  Oot3dNativeGame::SavestateSettingsOwners SavestateSettingsOwners() const;

  // The former locals, in declaration order: construction (in Setup()), lifetime and destruction order are
  // those of the function.
  const Oot3dNativeGameLaunch &launch;
  std::shared_ptr<Oot3dNativeGame::TopScreenUiConfigRuntime> topScreenConfigRuntime{};
  std::shared_ptr<Oot3dNativeGame::NativeControlConfigRuntime> controlConfigRuntime{};
  Oot3dNativeGame::TopScreenUiConfig activeTopScreenConfig{};
  std::string error;
  std::optional<Oot3dNativeGame::NativeA32ProcessImageManifest> manifest{};
  std::optional<Oot3dNativeGame::NativeScenarioCatalog> scenarioCatalog;
  std::optional<Oot3dNativeGame::NativeScenarioBootstrap> scenarioBootstrap;
  Oot3dNativeGame::TopScreenTextureOverridePack topScreenTextureOverridePack;
  Oot3dNativeGame::NativeHdFontRuntime hdFonts;
  bool hdFontsLoaded = false;
  const Oot3dNativeGame::TopScreenTextureOverridePack *topScreenTextureOverridePackPointer = nullptr;
  Oot3dNativeGame::Oot3dNativePicaFrontend picaFrontend;
  Oot3dNativeGame::Oot3dNativePicaCompositionTracker picaCompositionTracker;
  Oot3dNativeGame::Mm3dNativePicaCompositionTracker mm3dCompositionTracker;
  Oot3dNativeGame::Oot3dPicaCompositionDomain picaCompositionDomain{};
  Oot3dNativeGame::NativeA32CtrHostConfig hostConfig;
  std::shared_ptr<Oot3dNativeGame::GameLanguageSettings> languageSettings{};
  std::filesystem::path saveDataDirectory{};
  std::filesystem::path quickStatePath{};
  std::shared_ptr<Oot3dNativeGame::SavestateLibrary> savestateLibrary{};
  std::string lastSavestateMessage;
  Oot3dNativeGame::NativeA32SavestateCompatibility savestateCompatibility{};
  std::optional<Oot3dNativeGame::NativeA32CtrHostServices> hostServicesStorage;
  Oot3dNativeGame::NativeFrameRateContract frameRateContract{};
  std::optional<Oot3dNativeGame::NativeFrameRatePolicy> frameRatePolicyStorage;
  Oot3dNativeGame::NativeA32SceneViewProbe sceneViewProbe;
  Oot3dNativeGame::NativeA32PlayerTimingProbe playerTimingProbe;
  Oot3dNativeGame::NativeTemporalEventLedger temporalEventLedger;
  Oot3dNativeGame::NativeA32PlayerTemporalBridge playerTemporalBridge;
  NativeWidescreenProjectionState widescreenProjection;
  std::optional<ScopedA32BlockTraceWriter> blockTraceWriterStorage;
  std::vector<uint32_t> blockEntryHooks{};
  std::span<const uint32_t> picaCompositionHooks{};
  std::span<const uint32_t> typedGameplayObservableExitHooks{};
  // Resolved from the process image's code.bin (triaevum_title_adapter.h).
  Oot3dNativeGame::TitleAdapter titleAdapter{Oot3dNativeGame::TitleAdapter::Generic};
  bool enableCompiledFunctions{};
  bool wholeAotAvailable{};
  bool enableWholeAot{};
  bool enableMassAot{};
  bool enableTrueAot{};
  bool enableManualCompiledFunctions{};
  std::span<const uint32_t> trueAotEntries{};
  std::span<const uint32_t> compiledFunctionEntries{};
  std::span<const uint32_t> typedGameplayEntries{};
  std::vector<uint32_t> nativeCandidateEntries;
  std::vector<uint32_t> topScreenSourcePortBlocks;
  std::vector<uint32_t> wholeAotObservableExitBlocks;
  std::optional<Oot3dNativeGame::NativeA32Process> processStorage;
  std::optional<Oot3dNativeGame::Oot3dNativeUiLifecycleBridge> uiLifecycleBridgeStorage;
  std::span<const uint32_t> nativeBlockEntryPcs{};
  NativeCandidateDispatchState nativeCandidateDispatch{};
  std::uint64_t appliedTopScreenConfigRevision{};
  std::vector<Oot3dNativeGame::Oot3dPicaPhysicalMemoryRegion> picaMemoryRegions{};
  std::optional<Oot3dNativeGame::Oot3dPicaPhysicalMemoryView> picaMemoryViewStorage;
  std::optional<Oot3dNativeGame::TopScreenTextureOverrideRuntime> topScreenTextureOverrideRuntime;
  std::optional<Oot3dNativeGame::Oot3dNativePicaSubmissionQueue> submissionQueueStorage;
  bool copyWritesObservable{};
  std::optional<CopyWriteObserverLifetime> copyWriteObserverLifetime;
  Args hostArgs{};
  std::shared_ptr<Oot3dNativeGame::SceneEnvironmentRuntime> sceneEnvironment{};
  std::vector<std::shared_ptr<Fast::Oot3d::GraphicsSettingsPanelTab>> applicationSettingsTabs;
  Fast::Fast3dWindow *windowPointer = nullptr;
  Fast::GfxRenderingAPI *apiPointer = nullptr;
  Fast::Renderer3ds::PicaProgramPreparationBackend *programPreparation{};
  bool alwaysAvailablePrograms{};
  std::optional<Oot3dNativeGame::NativePreparationScreen> preparationScreenStorage;
  Fast::Oot3d::TitleRenderBackend *titleRenderBackend{};
  std::shared_ptr<Ship::Gui> gui{};
  std::optional<Oot3dNativeGame::Oot3dNativeA32UiTextureProvider> nativeUiTextureProviderStorage;
  std::optional<Oot3dNativeGame::TopScreenOcarinaTextRuntime> ocarinaTextStorage;
  std::optional<oot3d::ui::Fast3dUiRenderBackend> uiRenderBackendStorage;
  std::optional<oot3d::ui::N64UiFast3dRenderer> n64UiRendererStorage;
  std::array<std::vector<oot3d::ui::UiPrimitive>, oot3d::ui::kUiSubsystemCount> lastTopScreenPrimitivesBySubsystem;
  Oot3dNativeGame::NativeA32InputTimeline inputTimeline{};
  Oot3dNativeGame::GraphicsSettingsSchedule graphicsSettingsSchedule{};
  NativeFramePhaseTiming phaseTiming;
  Oot3dNativeGame::NativeA32InputDiagnostics inputDiagnostics;
  NativeInputConsumerDiagnostics inputConsumerDiagnostics;
  NativeControlPollingState nativeControlPollingState;
  Oot3dNativeGame::Oot3dPicaVulkanShaderSourceCache shaderSourceCache;
  std::optional<Oot3dNativeGame::Oot3dPicaSemanticTraceWriter> picaSemanticTraceStorage;
  std::optional<Oot3dNativeGame::NativeA32DspHle> dspHleStorage;
  Ship::Context *context{};
  std::shared_ptr<Ship::Audio> audio{};
  std::shared_ptr<Ship::AudioPlayer> audioPlayer{};
  std::chrono::steady_clock::time_point phaseStart{};
  Oot3dNativeGame::NativeA32ProcessRunResult processResult;
  uint32_t frameCount = 0;
  uint32_t runFrameCount = 0;
  uint64_t presentationFrameCount = 0;
  uint64_t refreshTickRemainder = 0;
  uint64_t nextVblankTick = 0;
  uint64_t lateVblankDeadlineCount = 0;
  uint64_t maximumVblankDeadlineOvershootTicks = 0;
  NativeA32SavestateStats savestateStats;
  std::optional<Oot3dNativeGame::NativePresentationScheduler> presentationSchedulerStorage;
  Oot3dNativeGame::NativeVisualSampleCadence visualSampleCadence;
  Oot3dNativeGame::Oot3dPicaPresentationScheduler picaPresentationScheduler;
  Oot3dNativeGame::Oot3dPicaVisualContinuityTracker visualContinuityTracker;
  Oot3dNativeGame::Oot3dPicaPreparedVisualTransition preparedVisualTransition;
  std::optional<Oot3dNativeGame::Oot3dPicaVisualFrame> latestVisualFrame;
  std::optional<Oot3dNativeGame::Oot3dPicaVisualFrame> previousVisualFrame;
  bool latestVisualTransitionContinuous = false;
  uint64_t visualContinuityEpoch = 1U;
  std::map<uint32_t, Oot3dNativeGame::Oot3dPicaDisplayTransferSubmission> displayTransfersByOutput;
  uint64_t lastSubmittedDrawId = 0;
  // Diagnostic: raw TextureCopy destinations watched for guest access while
  // extended diagnostics are on. Empty otherwise, so normal runs are untouched.
  std::vector<std::pair<uint32_t, uint32_t>> rawCopyWatchRanges;
  // Instrument control: TRIAEVUM_WATCH_GUEST_RANGE=0xSTART:0xSIZE watches an
  // arbitrary guest range, so a run can show that typed accesses from the
  // precompiled title module really are counted instead of assumed.
  bool watchReportRequested = false;
  uint64_t lastSelectedTopTransferCompletionId = 0;
  NativeA32PicaSubmissionStats picaStats;
  NativeA32VisualStats visualStats;
  Oot3dNativeGame::Oot3dPicaVisualInterpolationTiming visualInterpolationTiming;
  // Output addresses of selected top-screen transfers (the display
  // framebuffers): an earlier snapshot into one ends a superseded guest frame.
  std::vector<uint32_t> topDisplayOutputs;
  uint64_t vblankCount = 0;
  NativeA32DspMixStats dspStats;
  nlohmann::json displayTransferEvents = nlohmann::json::array();
  nlohmann::json drawEvents = nlohmann::json::array();
  nlohmann::json visualTransitionEvents = nlohmann::json::array();
  WindowDemoTimingState timing;
  Fast::Oot3d::PresentationPacingPolicy presentationPacingPolicy{};
  Fast::Renderer3ds::PicaFrameCompositionPolicy frameCompositionPolicy{};
  std::optional<Oot3dNativeGame::NativeRealtimeRefreshPacer> realtimePacerStorage;
  bool visualInterpolationEnabled{};
  Fast::Renderer3ds::PicaFrameCompositionPolicy guestCompositionPolicy{};
  bool guestInterpolationEnabled{};
  std::chrono::steady_clock::time_point lastPresentationTime{};
  NativeAudioOutputDiagnostics audioOutputDiagnostics;
  Oot3dNativeGame::NativePcmContinuityDiagnostics pcmContinuityDiagnostics;
  std::vector<int16_t> pcmDumpSamples;
  FramebufferScreenshotState screenshotState;
  uint64_t lastPublishedSceneViewSerial = 0;
  uint64_t publishedSceneViewCount = 0;
  // Renderer work of the loop, in program order. Inline: runs at submission.
  // Declared after every object its commands touch, so destruction drains it
  // first.
  Oot3dNativeGame::NativeRenderCommandStream renderStream;
  std::optional<PhysicalMemoryAccessLifetime> physicalMemoryAccessLifetime;
  std::optional<Fast::Oot3d::NativeFrameTemporalSample> capturedTemporalSample;
  std::vector<Fast::Renderer3ds::PicaTextureCopyReference> pinnedCopyReferences;
  // Renderer-side state written only by render commands (or at a drain).
  // Inline mode points the timings at the loop's own phase timing.
  NativeFramePhaseTiming workerPhaseTiming;
  NativeFramePhaseTiming* rendererTiming{};
  std::string renderError;
  // A renderer frame that acquired no image skips its drawing commands. In
  // worker mode the guest learns it at its next frame begin and synchronizes.
  bool rendererFrameSkipped = false;
  std::atomic<bool> rendererFrameWasInactive{false};
  // Copy references still owned by the guest side (submission queue and the
  // accumulator), snapshotted where the serial loop read them.
  std::vector<Fast::Renderer3ds::PicaTextureCopyReference> guestCopyReferences;
  Oot3dNativeGame::Oot3dPicaVisualFrameViewSample dependencySampleScratch;
  Oot3dNativeGame::Oot3dPicaVisualFrameViewSample visualFrameSampleScratch;
  std::optional<RenderStreamStop> renderStreamStop;
  std::vector<Oot3dNativeGame::Oot3dPicaDrawSubmission> drainedDraws;
  bool quickSaveKeyWasDown = false;
  bool quickLoadKeyWasDown = false;
  bool automatedStateSaved = false;
  bool automatedStateSaveFailed = false;
  // The settings block of the --load-state file, for --convert-state.
  std::optional<std::string> initialStateHostSettings;
  std::optional<uint32_t> automatedStateSavedAtRunFrame;
  std::optional<uint32_t> automatedStateSavedAtGuestFrame;
  NativeA32BenchmarkWindow benchmark;
  const char* pacingTrace{};
  bool measurePacing{};
  std::unique_ptr<Fast::Renderer::FrameTimeDistribution> benchmarkFrameTimes{};
  // Measurement starts after the initial preparation and the warm-up frames.
  bool benchmarkMeasurementStarted = false;
  std::chrono::steady_clock::time_point lastCompletedFrameTime{};
  const char* cpuProbeEnvironment{};
  bool cpuProbeRequested{};
  CpuProbe::Totals benchmarkCpuPhases{};
  uint32_t guestFrameBeginWidth = 0;
  uint32_t guestFrameBeginHeight = 0;
  uint64_t guestFrameBeginSettings = 0;
  bool guestFrameBeginNativePresentation = false;
  const char* parallelPresentationEnvironment{};
  bool parallelPresentation{};
  Fast::Fast3dGui *fast3dGui{};
  std::optional<GuiRendererRoutingReset> guiRendererRoutingReset;
};

} // namespace Oot3dA32Window
