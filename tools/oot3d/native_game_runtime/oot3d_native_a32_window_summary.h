#pragma once

// Runtime summary of the A32 window: the runtime.json document written at the end of a run.

#include "oot3d_native_a32_window_shared.h"
#include "oot3d_native_a32_window_stats.h"
#include "oot3d_native_input_admission.h"
#include "oot3d_native_performance_hint.h"
#include "ship/audio/Audio.h"

namespace Oot3dA32Window {

// The window state the summary reads, by reference.
struct NativeA32WindowSummaryInputs {
    const Oot3dNativeGameLaunch &launch;
    std::shared_ptr<Oot3dNativeGame::NativeControlConfigRuntime> &controlConfigRuntime;
    Oot3dNativeGame::TopScreenUiConfig &activeTopScreenConfig;
    std::optional<Oot3dNativeGame::NativeScenarioBootstrap> &scenarioBootstrap;
    Oot3dNativeGame::NativeHdFontRuntime &hdFonts;
    bool &hdFontsLoaded;
    const std::filesystem::path &quickStatePath;
    Oot3dNativeGame::NativeA32CtrHostServices &hostServices;
    Oot3dNativeGame::NativeFrameRatePolicy &frameRatePolicy;
    Oot3dNativeGame::NativeA32SceneViewProbe &sceneViewProbe;
    Oot3dNativeGame::NativeA32PlayerTimingProbe &playerTimingProbe;
    Oot3dNativeGame::NativeTemporalEventLedger &temporalEventLedger;
    Oot3dNativeGame::NativeA32PlayerTemporalBridge &playerTemporalBridge;
    NativeWidescreenProjectionState &widescreenProjection;
    const bool &enableCompiledFunctions;
    const bool &wholeAotAvailable;
    const bool &enableWholeAot;
    const bool &enableManualCompiledFunctions;
    Oot3dNativeGame::NativeA32Process &process;
    Oot3dNativeGame::Oot3dNativeUiLifecycleBridge &uiLifecycleBridge;
    NativeCandidateDispatchState &nativeCandidateDispatch;
    Oot3dNativeGame::Oot3dNativePicaSubmissionQueue &submissionQueue;
    Args &hostArgs;
    Fast::Fast3dWindow &window;
    Fast::GfxRenderingAPI &api;
    Oot3dNativeGame::Oot3dNativeA32UiTextureProvider &nativeUiTextureProvider;
    Oot3dNativeGame::TopScreenOcarinaTextRuntime &ocarinaText;
    oot3d::ui::N64UiFast3dRenderer &n64UiRenderer;
    const Oot3dNativeGame::NativeA32InputTimeline &inputTimeline;
    NativeFramePhaseTiming &phaseTiming;
    Oot3dNativeGame::NativeA32InputDiagnostics &inputDiagnostics;
    NativeInputConsumerDiagnostics &inputConsumerDiagnostics;
    NativeControlPollingState &nativeControlPollingState;
    Oot3dNativeGame::Oot3dPicaVulkanShaderSourceCache &shaderSourceCache;
    std::shared_ptr<Ship::Audio> &audio;
    std::shared_ptr<Ship::AudioPlayer> &audioPlayer;
    Oot3dNativeGame::NativeA32ProcessRunResult &processResult;
    uint32_t &frameCount;
    uint32_t &runFrameCount;
    uint64_t &presentationFrameCount;
    uint64_t &nextVblankTick;
    uint64_t &lateVblankDeadlineCount;
    uint64_t &maximumVblankDeadlineOvershootTicks;
    NativeA32SavestateStats &savestateStats;
    Oot3dNativeGame::NativePresentationScheduler &presentationScheduler;
    Oot3dNativeGame::NativeVisualSampleCadence &visualSampleCadence;
    uint64_t &visualContinuityEpoch;
    NativeA32PicaSubmissionStats &picaStats;
    NativeA32VisualStats &visualStats;
    Oot3dNativeGame::Oot3dPicaVisualInterpolationTiming &visualInterpolationTiming;
    uint64_t &vblankCount;
    NativeA32DspMixStats &dspStats;
    nlohmann::json &displayTransferEvents;
    nlohmann::json &drawEvents;
    nlohmann::json &visualTransitionEvents;
    WindowDemoTimingState &timing;
    Fast::Renderer3ds::PicaFrameCompositionPolicy &frameCompositionPolicy;
    Oot3dNativeGame::NativeRealtimeRefreshPacer &realtimePacer;
    bool &visualInterpolationEnabled;
    NativeAudioOutputDiagnostics &audioOutputDiagnostics;
    Oot3dNativeGame::NativePcmContinuityDiagnostics &pcmContinuityDiagnostics;
    std::vector<int16_t> &pcmDumpSamples;
    FramebufferScreenshotState &screenshotState;
    uint64_t &publishedSceneViewCount;
    std::optional<uint32_t> &automatedStateSavedAtRunFrame;
    std::optional<uint32_t> &automatedStateSavedAtGuestFrame;
    NativeA32BenchmarkWindow &benchmark;
    std::unique_ptr<Fast::Renderer::FrameTimeDistribution> &benchmarkFrameTimes;
    const bool &cpuProbeRequested;
    CpuProbe::Totals &benchmarkCpuPhases;
    Oot3dNativeGame::Oot3dNativePicaCompositionTracker &picaCompositionTracker;
    const Oot3dNativeGame::TopScreenTextureOverridePack * &topScreenTextureOverridePackPointer;
    std::optional<Oot3dNativeGame::TopScreenTextureOverrideRuntime> &topScreenTextureOverrideRuntime;
    Oot3dNativeGame::NativePreparationScreen &preparationScreen;
    std::array<std::vector<oot3d::ui::UiPrimitive>, oot3d::ui::kUiSubsystemCount> &lastTopScreenPrimitivesBySubsystem;
    Oot3dNativeGame::Oot3dPicaPresentationScheduler &picaPresentationScheduler;
    const bool &parallelPresentation;
    const Oot3dNativeGame::NativeInputAdmissionController &inputAdmission;
    const Oot3dNativeGame::NativeInputSampleDelayController &inputSampleDelay;
    const Oot3dNativeGame::NativePerformanceHintSession &mainPerformanceHint;
    const Oot3dNativeGame::NativePerformanceHintSession &workerPerformanceHint;
};

// Writes launch.Host.OutputPath when it is set.
void WriteNativeA32WindowRuntimeSummary(const NativeA32WindowSummaryInputs &in);

} // namespace Oot3dA32Window
