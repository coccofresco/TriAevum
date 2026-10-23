// Native A32 window: the session driver (Run, Finish) and the guest-run helpers the session files share.
#include "oot3d_native_a32_window.h"
#include "oot3d_native_a32_window_session.h"
#include "oot3d_native_a32_window_summary.h"

namespace Oot3dA32Window {

#if defined(__SWITCH__)
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
         << "topscreen_30_ui_atlas=" << (gSwitchTopScreen30UiAtlas ? 1 : 0)
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

Oot3dNativeGame::NativeA32ProcessRunResult
RunUntilGuestWait(Oot3dNativeGame::NativeA32Process &process,
                  uint32_t wholeAotBlockBudget) {
  static const bool diagnoseWait = std::getenv("TRIAEVUM_DIAGNOSE_GUEST_WAIT") != nullptr;
  const auto waitStart = diagnoseWait ? std::chrono::steady_clock::now()
                                    : std::chrono::steady_clock::time_point{};
  // A block-limit exit unwinds the native whole-AOT call chain. The caller
  // resumes it immediately: this is a stack-safety boundary, not an SDL event
  // pump or guest scheduling boundary.
  auto result = process.Run(wholeAotBlockBudget);
  while (result.Kind == Oot3dNativeGame::NativeA32ProcessRunKind::Yielded) {
    // Low-priority workers may poll indefinitely. Once the foreground is
    // waiting, let the host deliver its timers, HID and GPU completions.
    // The worker stays Ready and resumes after those events, not next boot.
    if (process.OnlyBackgroundThreadsReady()) return result;
    if (diagnoseWait && std::chrono::steady_clock::now() - waitStart > std::chrono::seconds(20)) {
      for (uint32_t id = 0; id < process.ThreadCount(); ++id) {
        const auto* thread = process.ThreadState(id);
        if (thread != nullptr)
          std::fprintf(stderr, "GUEST_WAIT_TIMEOUT thread=%u priority=%u status=%u pc=%08x lr=%08x sp=%08x r0=%08x r1=%08x r2=%08x\n",
                       id, process.ThreadPriority(id).value_or(0xFFU),
                       static_cast<unsigned>(process.ThreadStatus(id)),
                       thread->r[15], thread->r[14], thread->r[13],
                       thread->r[0], thread->r[1], thread->r[2]);
      }
      throw std::runtime_error("guest did not reach a wait boundary within diagnostic deadline");
    }
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

void NativeA32WindowSession::Run() {
  Setup();
  if (!launch.ConvertStatePath.empty()) {
    // The loaded moment, re-saved before any guest frame runs.
    const std::string *settings =
        initialStateHostSettings ? &*initialStateHostSettings : nullptr;
    const auto outcome = SaveState(launch.ConvertStatePath, false, settings);
    Finish();
    if (outcome != SaveStateOutcome::Saved) {
      throw std::runtime_error("savestate conversion failed: " +
                               (lastSavestateMessage.empty()
                                    ? std::string("a host continuation is pending")
                                    : lastSavestateMessage));
    }
    return;
  }
  auto &window = *windowPointer;
  while (window.IsRunning() &&
         processResult.Kind !=
             Oot3dNativeGame::NativeA32ProcessRunKind::Terminated) {
    if (!RunFrame()) break;
  }
  Finish();
}

void NativeA32WindowSession::Finish() {
  auto &hostServices = *hostServicesStorage;
  auto &frameRatePolicy = *frameRatePolicyStorage;
  auto &blockTraceWriter = *blockTraceWriterStorage;
  auto &process = *processStorage;
  auto &uiLifecycleBridge = *uiLifecycleBridgeStorage;
  auto &submissionQueue = *submissionQueueStorage;
  auto &window = *windowPointer;
  auto &api = *apiPointer;
  auto &preparationScreen = *preparationScreenStorage;
  auto &nativeUiTextureProvider = *nativeUiTextureProviderStorage;
  auto &ocarinaText = *ocarinaTextStorage;
  auto &n64UiRenderer = *n64UiRendererStorage;
  auto &picaSemanticTrace = *picaSemanticTraceStorage;
  auto &presentationScheduler = *presentationSchedulerStorage;
  auto &realtimePacer = *realtimePacerStorage;
  // Every renderer-owned value below is read after the last command ran.
  renderStream.SetMode(Oot3dNativeGame::NativeRenderCommandStream::Mode::Inline);
  if (rendererTiming != &phaseTiming) {
    phaseTiming.PicaBackendSeconds += workerPhaseTiming.PicaBackendSeconds;
    phaseTiming.VisualReplaySeconds += workerPhaseTiming.VisualReplaySeconds;
    phaseTiming.PacingWaitSeconds += workerPhaseTiming.PacingWaitSeconds;
    rendererTiming = &phaseTiming;
  }
  CpuProbe::Thread.Active = false;
  if (inputLatencyTrace.Enabled()) {
    // The first two seconds hold the start-up and the preparation screen.
    const std::string mode =
        std::string(parallelPresentation ? "parallel" : "serial") +
        " pacer_hz=" + std::to_string(realtimePacer.Enabled() ? realtimePacer.TargetRateHz() : 0U) +
        " interpolation=" + (visualInterpolationEnabled ? "1" : "0");
    std::fprintf(stderr, "%s\n", inputLatencyTrace.Summary(mode, 120U).c_str());
    if (const char *csv = std::getenv("TRIAEVUM_INPUT_LATENCY_CSV"); csv != nullptr && *csv != '\0' &&
        !inputLatencyTrace.WriteCsv(csv)) {
      std::fprintf(stderr, "TRIAEVUM_INPUT_LATENCY cannot write %s\n", csv);
    }
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

  WriteNativeA32WindowRuntimeSummary({launch,
      controlConfigRuntime, activeTopScreenConfig, scenarioBootstrap, hdFonts,
      hdFontsLoaded, quickStatePath, hostServices, frameRatePolicy,
      sceneViewProbe, playerTimingProbe, temporalEventLedger, playerTemporalBridge,
      widescreenProjection, enableCompiledFunctions, wholeAotAvailable, enableWholeAot,
      enableManualCompiledFunctions, process, uiLifecycleBridge, nativeCandidateDispatch,
      submissionQueue, hostArgs, window, api,
      nativeUiTextureProvider, ocarinaText, n64UiRenderer, inputTimeline,
      phaseTiming, inputDiagnostics, inputConsumerDiagnostics, nativeControlPollingState,
      shaderSourceCache, audioPlayer, processResult, frameCount,
      runFrameCount, presentationFrameCount, nextVblankTick, lateVblankDeadlineCount,
      maximumVblankDeadlineOvershootTicks, savestateStats, presentationScheduler, visualSampleCadence,
      visualContinuityEpoch, picaStats, visualStats, visualInterpolationTiming,
      vblankCount, dspStats, displayTransferEvents, drawEvents,
      visualTransitionEvents, timing, frameCompositionPolicy, realtimePacer,
      visualInterpolationEnabled, audioOutputDiagnostics, pcmContinuityDiagnostics, pcmDumpSamples,
      screenshotState, publishedSceneViewCount, automatedStateSavedAtRunFrame, automatedStateSavedAtGuestFrame,
      benchmark, benchmarkFrameTimes, cpuProbeRequested, benchmarkCpuPhases,
      picaCompositionTracker, topScreenTextureOverridePackPointer, topScreenTextureOverrideRuntime, preparationScreen,
      lastTopScreenPrimitivesBySubsystem, picaPresentationScheduler, parallelPresentation,
      inputAdmission});

  if (launch.ExtendedDiagnostics &&
      (process.Memory().RangeWatchActive() || watchReportRequested)) {
    const auto& watched = process.Memory().WatchCounters();
    std::fprintf(
        stderr,
        "TRIAEVUM_RAW_COPY_HOST_ACCESS active=%d ranges=%zu typed_reads=%llu typed_writes=%llu "
        "byte_reads=%llu pointer_reads=%llu pointer_writes=%llu byte_writes=%llu "
        "host_byte_writes=%llu fills=%llu suppressed_byte_writes=%llu first_read=0x%08X "
        "first_size=%u first_write=0x%08X first_write_size=%u\n",
        process.Memory().RangeWatchActive() ? 1 : 0,
        process.Memory().WatchedRangeCount(),
        static_cast<unsigned long long>(watched.TypedReads),
        static_cast<unsigned long long>(watched.TypedWrites),
        static_cast<unsigned long long>(watched.ByteReads),
        static_cast<unsigned long long>(watched.PointerReads),
        static_cast<unsigned long long>(watched.PointerWrites),
        static_cast<unsigned long long>(watched.ByteWrites),
        static_cast<unsigned long long>(watched.HostByteWrites),
        static_cast<unsigned long long>(watched.Fills),
        static_cast<unsigned long long>(watched.SuppressedByteWrites),
        watched.FirstReadAddress,
        static_cast<unsigned>(watched.FirstReadSize),
        watched.FirstWriteAddress,
        static_cast<unsigned>(watched.FirstWriteSize));
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

} // namespace Oot3dA32Window

void RunOot3dNativeA32Window(const Oot3dNativeGameLaunch &launch) {
  Oot3dA32Window::NativeA32WindowSession session(launch);
  session.Run();
}
