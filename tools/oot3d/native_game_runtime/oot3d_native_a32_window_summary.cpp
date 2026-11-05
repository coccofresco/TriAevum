// Runtime summary of the A32 window (runtime.json), moved verbatim from RunOot3dNativeA32Window.
#include "oot3d_native_a32_window_summary.h"

namespace Oot3dA32Window {

void WriteNativeA32WindowRuntimeSummary(const NativeA32WindowSummaryInputs &in) {
  const auto &launch = in.launch;
  auto &controlConfigRuntime = in.controlConfigRuntime;
  auto &activeTopScreenConfig = in.activeTopScreenConfig;
  auto &scenarioBootstrap = in.scenarioBootstrap;
  auto &hdFonts = in.hdFonts;
  auto &hdFontsLoaded = in.hdFontsLoaded;
  auto &quickStatePath = in.quickStatePath;
  auto &hostServices = in.hostServices;
  hostServices.WriteFsRequestSummary();
  auto &frameRatePolicy = in.frameRatePolicy;
  auto &sceneViewProbe = in.sceneViewProbe;
  auto &playerTimingProbe = in.playerTimingProbe;
  auto &temporalEventLedger = in.temporalEventLedger;
  auto &playerTemporalBridge = in.playerTemporalBridge;
  auto &widescreenProjection = in.widescreenProjection;
  auto &enableCompiledFunctions = in.enableCompiledFunctions;
  auto &wholeAotAvailable = in.wholeAotAvailable;
  auto &enableWholeAot = in.enableWholeAot;
  auto &enableManualCompiledFunctions = in.enableManualCompiledFunctions;
  auto &process = in.process;
  auto &uiLifecycleBridge = in.uiLifecycleBridge;
  auto &nativeCandidateDispatch = in.nativeCandidateDispatch;
  auto &submissionQueue = in.submissionQueue;
  auto &hostArgs = in.hostArgs;
  auto &window = in.window;
  auto &api = in.api;
  auto &nativeUiTextureProvider = in.nativeUiTextureProvider;
  auto &ocarinaText = in.ocarinaText;
  auto &n64UiRenderer = in.n64UiRenderer;
  auto &inputTimeline = in.inputTimeline;
  auto &phaseTiming = in.phaseTiming;
  auto &inputDiagnostics = in.inputDiagnostics;
  auto &inputConsumerDiagnostics = in.inputConsumerDiagnostics;
  auto &nativeControlPollingState = in.nativeControlPollingState;
  auto &shaderSourceCache = in.shaderSourceCache;
  auto &audioPlayer = in.audioPlayer;
  auto &processResult = in.processResult;
  auto &frameCount = in.frameCount;
  auto &runFrameCount = in.runFrameCount;
  auto &presentationFrameCount = in.presentationFrameCount;
  auto &nextVblankTick = in.nextVblankTick;
  auto &lateVblankDeadlineCount = in.lateVblankDeadlineCount;
  auto &maximumVblankDeadlineOvershootTicks = in.maximumVblankDeadlineOvershootTicks;
  auto &savestateStats = in.savestateStats;
  auto &presentationScheduler = in.presentationScheduler;
  auto &visualSampleCadence = in.visualSampleCadence;
  auto &visualContinuityEpoch = in.visualContinuityEpoch;
  auto &picaStats = in.picaStats;
  auto &visualStats = in.visualStats;
  auto &visualInterpolationTiming = in.visualInterpolationTiming;
  auto &vblankCount = in.vblankCount;
  auto &dspStats = in.dspStats;
  auto &displayTransferEvents = in.displayTransferEvents;
  auto &drawEvents = in.drawEvents;
  auto &visualTransitionEvents = in.visualTransitionEvents;
  auto &timing = in.timing;
  auto &frameCompositionPolicy = in.frameCompositionPolicy;
  auto &realtimePacer = in.realtimePacer;
  auto &visualInterpolationEnabled = in.visualInterpolationEnabled;
  auto &audioOutputDiagnostics = in.audioOutputDiagnostics;
  auto &pcmContinuityDiagnostics = in.pcmContinuityDiagnostics;
  auto &pcmDumpSamples = in.pcmDumpSamples;
  auto &screenshotState = in.screenshotState;
  auto &publishedSceneViewCount = in.publishedSceneViewCount;
  auto &automatedStateSavedAtRunFrame = in.automatedStateSavedAtRunFrame;
  auto &automatedStateSavedAtGuestFrame = in.automatedStateSavedAtGuestFrame;
  auto &benchmark = in.benchmark;
  auto &benchmarkFrameTimes = in.benchmarkFrameTimes;
  auto &cpuProbeRequested = in.cpuProbeRequested;
  auto &benchmarkCpuPhases = in.benchmarkCpuPhases;
  auto &picaCompositionTracker = in.picaCompositionTracker;
  auto &topScreenTextureOverridePackPointer = in.topScreenTextureOverridePackPointer;
  auto &topScreenTextureOverrideRuntime = in.topScreenTextureOverrideRuntime;
  auto &preparationScreen = in.preparationScreen;
  auto &lastTopScreenPrimitivesBySubsystem = in.lastTopScreenPrimitivesBySubsystem;
  auto &picaPresentationScheduler = in.picaPresentationScheduler;
  auto &parallelPresentation = in.parallelPresentation;
  const auto &inputAdmission = in.inputAdmission;
  const auto &inputSampleDelay = in.inputSampleDelay;
  const auto &mainPerformanceHint = in.mainPerformanceHint;
  const auto &workerPerformanceHint = in.workerPerformanceHint;
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
    const size_t recentSvcWindow = launch.ExtendedDiagnostics ? 4096U : 32U;
    const size_t firstSvcEvent = svcEvents.size() > recentSvcWindow
                                     ? svcEvents.size() - recentSvcWindow
                                     : 0U;
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
          {"last_frustum", stats.LastFrustum},
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
    // Bring-up probe: TRIAEVUM_PROBE_GUEST_RANGES=ADDR:SIZE[,ADDR:SIZE...]
    // (hex) records those guest words at exit, e.g. a title's scheduler
    // state while its boot is stalled.
    nlohmann::json guestMemoryProbe = nlohmann::json::array();
    if (const char* ranges = std::getenv("TRIAEVUM_PROBE_GUEST_RANGES")) {
      for (const char* cursor = ranges; *cursor != '\0';) {
        unsigned long long start = 0, size = 0;
        int consumed = 0;
        if (std::sscanf(cursor, "%llx:%llx%n", &start, &size, &consumed) != 2) break;
        nlohmann::json words = nlohmann::json::array();
        for (unsigned long long offset = 0; offset < std::min(size, 0x1000ULL); offset += 4U) {
          uint32_t value = 0;
          if (!process.Memory().Read32(static_cast<uint32_t>(start + offset), &value)) break;
          words.push_back(value);
        }
        guestMemoryProbe.push_back({{"address", start}, {"words", std::move(words)}});
        cursor += consumed;
        if (*cursor == ',') ++cursor;
      }
    }
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
        Fast::Extension::GraphicsSettingsRuntime::Instance().Snapshot();
    const auto &directionalShadows =
        graphicsSettings.Effects.DirectionalShadows;
    const auto &realtimePacingStats = realtimePacer.Stats();
    const double benchmarkHostSeconds =
        benchmark.MeasuredFrames == 0U
            ? 0.0
            : std::chrono::duration<double>(benchmark.MeasurementEnd -
                                            benchmark.MeasurementStart)
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
                   {"ui_scope_entries", stats.UiScopeEntries},
                   {"ui_scope_exits", stats.UiScopeExits},
                   {"ui_command_spans", stats.UiCommandSpans},
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
              {"save_count", savestateStats.SaveCount},
              {"save_failure_count", savestateStats.SaveFailureCount},
              {"save_deferral_count", savestateStats.SaveDeferralCount},
              {"load_count", savestateStats.LoadCount},
              {"save_bytes", savestateStats.SaveBytes},
              {"load_bytes", savestateStats.LoadBytes},
              {"save_seconds", savestateStats.SaveSeconds},
              {"load_seconds", savestateStats.LoadSeconds},
              {"capture_seconds", savestateStats.CaptureSeconds},
              {"encode_seconds", savestateStats.EncodeSeconds},
              {"write_seconds", savestateStats.WriteSeconds},
              {"read_seconds", savestateStats.ReadSeconds},
              {"decode_seconds", savestateStats.DecodeSeconds},
              {"restore_seconds", savestateStats.RestoreSeconds},
              {"last_semantic_fingerprint",
               std::to_string(savestateStats.LastSemanticFingerprint)},
              {"legacy_gameplay_clock_restores",
               savestateStats.LegacyClockRestoreCount},
              {"legacy_presentation_scheduler_restores",
               savestateStats.LegacyPresentationSchedulerRestoreCount},
              {"legacy_frame_rate_policy_restores",
               savestateStats.LegacyFrameRatePolicyRestoreCount},
              {"legacy_topscreen_temporal_restores",
               savestateStats.LegacyTopScreenTemporalRestoreCount},
              {"legacy_pica_visual_replay_restores",
               savestateStats.LegacyPicaVisualReplayRestoreCount},
              {"legacy_pica_texture_cache_restores",
               savestateStats.LegacyPicaTextureCacheRestoreCount},
              {"legacy_pica_color_target_restores",
               savestateStats.LegacyPicaColorTargetRestoreCount},
              {"legacy_pica_presentation_restores",
               savestateStats.LegacyPicaPresentationRestoreCount},
              {"pica_textures_captured",
               savestateStats.PicaTexturesCaptured},
              {"pica_texture_bytes_captured",
               savestateStats.PicaTextureBytesCaptured},
              {"pica_textures_restored",
               savestateStats.PicaTexturesRestored},
              {"pica_texture_bytes_restored",
               savestateStats.PicaTextureBytesRestored},
              {"pica_visual_replay_bytes_captured",
               savestateStats.PicaVisualReplayBytesCaptured},
              {"pica_visual_replay_bytes_restored",
               savestateStats.PicaVisualReplayBytesRestored},
              {"pica_color_targets_captured",
               savestateStats.PicaColorTargetsCaptured},
              {"pica_color_target_bytes_captured",
               savestateStats.PicaColorTargetBytesCaptured},
              {"pica_color_targets_restored",
               savestateStats.PicaColorTargetsRestored},
              {"pica_color_target_bytes_restored",
               savestateStats.PicaColorTargetBytesRestored},
              {"pica_display_images_captured",
               savestateStats.PicaDisplayImagesCaptured},
              {"pica_display_image_bytes_captured",
               savestateStats.PicaDisplayImageBytesCaptured},
              {"pica_display_images_restored",
               savestateStats.PicaDisplayImagesRestored},
              {"pica_display_image_bytes_restored",
               savestateStats.PicaDisplayImageBytesRestored}}},
             {"realtime_pacing",
             {{"enabled", realtimePacer.Enabled()},
              {"multimedia_scheduling_active", realtimePacer.MultimediaSchedulingActive()},
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
            {"initial_preparation",
             {{"parts", preparationScreen.Parts()},
              {"startup_seconds", preparationScreen.StartupSeconds()},
              {"wait_seconds", preparationScreen.WaitSeconds()},
              {"first_frames", preparationScreen.FirstFrames()},
              {"first_frames_seconds", preparationScreen.FirstFramesSeconds()},
              {"completed", preparationScreen.Done()},
              {"note", "The guest waits for the startup preparation; the first frames render under the "
                       "preparation screen until they run within budget. The benchmark window starts after it."}}},
            {"benchmark_window",
             {{"warmup_frames_requested",
               hostArgs.BenchmarkWarmupFrames},
              {"warmup_frames_completed",
               std::min<uint64_t>(runFrameCount,
                                  hostArgs.BenchmarkWarmupFrames)},
              {"measured_frames", benchmark.MeasuredFrames},
              {"thread_cpu_ledger", [&]() -> nlohmann::json {
                nlohmann::json phases = nlohmann::json::object();
                double windowCpu = 0.0;
                for (size_t i = 0; i < benchmarkCpuPhases.size(); ++i) {
                  const auto& p = benchmarkCpuPhases[i];
                  windowCpu += p.User + p.Kernel;
                  phases[CpuProbe::Names[i]] = {
                      {"wall_seconds", p.Wall}, {"thread_cycles", p.Cycles},
                      {"intervals", p.Intervals}};
                }
                return {{"requested", cpuProbeRequested},
                    {"available_in_build", CpuProbe::BuildEnabled},
                    {"clock", "GetThreadTimes_and_QueryThreadCycleTime"},
                    {"read_failures", CpuProbe::Thread.ReadFailures},
                    {"window_thread_cpu_seconds", windowCpu},
                    {"scope", "exclusive_main_thread_same_post_warmup_window"},
                    {"warning", "quantized_CPU_valid_only_for_whole_window; off_CPU_includes_preemption_not_just_waits; phase_cycles_are_not_seconds"},
                    {"phases", std::move(phases)}};
              }()},
              {"pre_backend_cpu", [&]() -> nlohmann::json {
                const auto& p=benchmark.PhaseSeconds;
                const Oot3dNativeGame::PreBackendBudget b{
                  p[0],p[7],benchmark.NestedGuestSeconds,p[8],p[2],p[3],p[5],p[6]};
                return {{"scope","guest_including_gsp_pica_plus_plan_capture_host_audio_before_nri_execute"},
                  {"samples",benchmark.PreBackendTimes.Count()}, {"invalid_intervals",benchmark.InvalidPreBackendIntervals},
                  {"guest_seconds",b.Guest}, {"plan_seconds",b.Plan},
                  {"guest_clock_included_seconds",benchmark.GuestClockSeconds},
                  {"queue_capture_seconds",b.QueueAndCapture()},
                  {"nested_guest_subtracted_seconds",b.GuestInsideSubmission},
                  {"host_seconds",b.Host}, {"input_seconds",b.Input},
                  {"dsp_seconds",b.Dsp}, {"audio_output_seconds",b.AudioOutput},
                  {"total_seconds",b.Total()},
                  {"mean_ms",benchmark.PreBackendTimes.MeanMs()},
                  {"host_envelope_mean_ms",benchmark.PreBackendEnvelopeTimes.MeanMs()},
                  {"host_envelope_p95_upper_ms",benchmark.PreBackendEnvelopeTimes.Count()?nlohmann::json(*benchmark.PreBackendEnvelopeTimes.QuantileUpperMs(.95)):nlohmann::json(nullptr)},
                  {"unattributed_host_ui_seconds",benchmark.UnattributedSeconds},
                  {"p95_upper_ms",benchmark.PreBackendTimes.Count()?nlohmann::json(*benchmark.PreBackendTimes.QuantileUpperMs(.95)):nlohmann::json(nullptr)},
                  {"maximum_ms",benchmark.PreBackendTimes.MaximumMs()}};
              }()},
              {"frame_times", [&]() -> nlohmann::json {
                 if (!benchmarkFrameTimes || !benchmarkFrameTimes->Count()) return nullptr;
                 const auto& times = *benchmarkFrameTimes;
                 return {{"samples", times.Count()}, {"invalid_samples", times.Invalid()},
                         {"quantile_resolution_ms", times.ResolutionMs},
                         {"overflow_samples", times.Overflow()},
                         {"p50_upper_ms", *times.QuantileUpperMs(0.50)},
                         {"p95_upper_ms", *times.QuantileUpperMs(0.95)},
                         {"p99_upper_ms", *times.QuantileUpperMs(0.99)},
                         {"maximum_ms", times.MaximumMs()}, {"mean_ms", times.MeanMs()},
                         {"over_60hz_budget", times.Over60Hz()},
                         {"over_30hz_budget", times.Over30Hz()}};
               }()},
              {"host_seconds",
               benchmark.MeasuredFrames == 0U
                   ? nlohmann::json(nullptr)
                   : nlohmann::json(benchmarkHostSeconds)},
              {"frames_per_second",
               benchmark.MeasuredFrames == 0U || benchmarkHostSeconds <= 0.0
                   ? nlohmann::json(nullptr)
                   : nlohmann::json(
                         static_cast<double>(benchmark.MeasuredFrames) /
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
              {"pacing_enabled", realtimePacer.Enabled()},
              {"parallel_presentation", parallelPresentation},
              {"input_admission",
               [&]() {
                 const auto &stats = inputAdmission.Stats();
                 return nlohmann::json{
                     {"mode", Oot3dNativeGame::NativeInputAdmissionModeName(
                                  inputAdmission.Mode())},
                     {"admitting_at_exit", inputAdmission.Admitting()},
                     {"admitted_frames", stats.AdmittedFrames},
                     {"overlapped_frames", stats.OverlappedFrames},
                     {"observed_refresh_frames", stats.ObservedRefreshFrames},
                     {"late_refresh_frames", stats.LateRefreshFrames},
                     {"overlap_entries", stats.OverlapEntries},
                     {"probes", stats.Probes},
                     {"successful_probes", stats.SuccessfulProbes},
                     {"hold_presentations", inputAdmission.HoldPresentations()}};
               }()},
              {"input_sample_delay",
               [&]() {
                 const auto &stats = inputSampleDelay.Stats();
                 return nlohmann::json{
                     {"delay_us_at_exit", inputSampleDelay.DelayMicroseconds()},
                     {"maximum_delay_us", stats.MaximumMicroseconds},
                     {"bound_us", inputSampleDelay.MaximumMicroseconds()},
                     {"observed_refresh_frames", stats.ObservedRefreshFrames},
                     {"late_refresh_frames", stats.LateRefreshFrames},
                     {"increases", stats.Increases},
                     {"decreases", stats.Decreases}};
               }()},
              {"performance_hint",
               [&]() {
                 const auto session = [](const Oot3dNativeGame::NativePerformanceHintSession &hint) {
                   return nlohmann::json{{"active", hint.Active()},
                                         {"target_ns", hint.TargetNanoseconds()},
                                         {"reports", hint.Reports()},
                                         {"reports_over_target", hint.ReportsOverTarget()}};
                 };
                 return nlohmann::json{
                     {"available", Oot3dNativeGame::NativePerformanceHintSession::Available()},
                     {"main", session(mainPerformanceHint)},
                     {"render", session(workerPerformanceHint)}};
               }()}}},
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
                     Fast::Extension::NativeVisualInterpolationModeName(
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
                   {"mouse_polling", {
                       {"eligible", nativeControlPollingState.MouseEligiblePolls},
                       {"released", nativeControlPollingState.MouseReleasedPolls},
                       {"host_ui", nativeControlPollingState.MouseHostUiPolls},
                       {"native_ui", nativeControlPollingState.MouseNativeUiPolls},
                       {"capture_transitions", nativeControlPollingState.MouseCaptureTransitions},
                       {"movement", nativeControlPollingState.MouseMovementPolls}}},
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
              {"frames_mixed", dspStats.FramesMixed},
              {"frames_processed", dspStats.FramesMixed},
              {"frames_suppressed", dspStats.FramesSuppressed},
              {"stereo_samples_mixed", dspStats.SamplesMixed},
              {"nonzero_channel_samples", dspStats.NonzeroSamples},
              {"peak_magnitude", dspStats.PeakMagnitude},
              {"pcm_fnv1a64", dspStats.PcmFnv1a64},
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
              {"output_underruns",
               audioPlayer != nullptr ? audioPlayer->OutputUnderruns() : uint64_t{0}},
              // The host output gain (docs/TRIAEVUM_AUDIO_SETTINGS.md): at
              // 100 % the bytes passed to the backend are the guest's, so
              // output_pcm_fnv1a64 equals pcm_fnv1a64 when every mixed
              // sample was played.
              {"output_gain_q16",
               audioPlayer != nullptr ? audioPlayer->GetOutputGainTarget() : uint32_t{0}},
              {"output_frames_scaled",
               audioPlayer != nullptr ? audioPlayer->GetOutputFramesScaled() : uint64_t{0}},
              {"output_pcm_fnv1a64",
               audioPlayer != nullptr ? audioPlayer->GetOutputPcmFnv1a64() : uint64_t{0}},
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
            {"draws_submitted", picaStats.DrawCount},
            {"refreshes_with_native_draws", picaStats.RefreshesWithNativeDraws},
            {"display_transfers_submitted", picaStats.DisplayTransferCount},
            {"gpu_completions_queued", 0},
            {"pica_completions_recorded", picaStats.RecordedCompletionCount},
            {"p3d_completions_delivered", picaStats.CompletedP3dCount},
            {"ppf_completions_delivered", picaStats.CompletedPpfCount},
            {"pending_gpu_completions", 0},
            {"pica_drain_passes", picaStats.PicaDrainPassCount},
            {"maximum_pica_drain_passes_per_refresh",
             picaStats.MaximumPicaDrainPassesPerRefresh},
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
                     Fast::Extension::DirectionalShadowMode::
                         VirtualShadowMaps},
                {"mode",
                 static_cast<std::uint32_t>(
                     directionalShadows.Mode)},
                {"resolution", directionalShadows.Resolution},
                {"maximum_distance",
                 directionalShadows.MaximumDistance},
                {"depth_bias_constant",
                 directionalShadows.DepthBiasConstant},
                {"depth_bias_slope",
                 directionalShadows.DepthBiasSlope},
                {"strength", directionalShadows.Strength},
                {"pcf_radius", directionalShadows.PcfRadius}}}}},
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
              {"dpad_offset_x", activeTopScreenConfig.DpadOffsetX},
              {"dpad_offset_y", activeTopScreenConfig.DpadOffsetY},
              {"dpad_size_percent", activeTopScreenConfig.DpadSizePercent},
              {"button_group_offset_x",
               activeTopScreenConfig.ButtonGroupOffsetX},
              {"button_group_offset_y",
               activeTopScreenConfig.ButtonGroupOffsetY},
              {"button_group_size_percent",
               activeTopScreenConfig.ButtonGroupSizePercent},
              {"minimap_visible", activeTopScreenConfig.MinimapVisible},
              {"render_hud", activeTopScreenConfig.RenderHud},
              {"render_dpad_icons",
               activeTopScreenConfig.RenderDpadIcons},
              {"render_items_hint",
               activeTopScreenConfig.RenderItemsHint},
              {"hud_button_labels",
               Oot3dNativeGame::TopScreenHudButtonLabelsName(
                   activeTopScreenConfig.HudButtonLabels)},
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
              {"quick_put_away", activeTopScreenConfig.QuickPutAway},
              {"landing_roll_fix", activeTopScreenConfig.LandingRollFix},
              {"roll_dash", activeTopScreenConfig.RollDash},
              {"aiming_reticle", activeTopScreenConfig.AimingReticle},
              {"rain_3d", activeTopScreenConfig.Rain3d},
              {"rain_3d_density_percent",
               activeTopScreenConfig.Rain3dDensityPercent},
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
            {"guest_memory_probe", std::move(guestMemoryProbe)},
            {"filesystem_svc_events", std::move(filesystemSvcEvents)},
            {"ipc_svc_events", std::move(ipcSvcEvents)},
            {"last_svc_by_thread", std::move(lastThreadSvcEvents)},
            {"threads", std::move(threads)},
            {"screenshot_written", screenshotState.Written},
            {"visual_interpolation",
             {{"snapshot_replay_enabled", visualInterpolationEnabled},
              {"cross_frame_history_enabled", visualInterpolationEnabled},
              {"direct_current_frames_presented",
               visualStats.DirectFramesPresented},
              {"reused_snapshot_presentations",
               visualStats.ReusedSnapshotPresentations},
              {"selected_top_transfers", picaStats.SelectedTopTransferCount},
              {"complete_snapshots", visualStats.SnapshotCount},
              {"transitions", visualStats.TransitionCount},
              {"matched_draws", visualStats.MatchedDrawCount},
              {"strict_unique_matched_draws",
               visualStats.StrictUniqueMatchedDrawCount},
              {"strict_ordinal_matched_draws",
               visualStats.StrictOrdinalMatchedDrawCount},
              {"structural_ordinal_matched_draws",
               visualStats.StructuralOrdinalMatchedDrawCount},
              {"pipeline_ordinal_matched_draws",
               visualStats.PipelineOrdinalMatchedDrawCount},
              {"changed_continuous_draws", visualStats.ChangedContinuousDrawCount},
              {"changed_per_instance_vertex_draws",
               visualStats.ChangedPerInstanceVertexDrawCount},
              {"changed_per_vertex_draws", visualStats.ChangedPerVertexDrawCount},
              {"ambiguous_previous_draws", visualStats.AmbiguousPreviousDrawCount},
              {"ambiguous_current_draws", visualStats.AmbiguousCurrentDrawCount},
              {"unmatched_previous_draws", visualStats.UnmatchedPreviousDrawCount},
              {"unmatched_current_draws", visualStats.UnmatchedCurrentDrawCount},
              {"samples_presented", visualStats.SamplesPresented},
              {"discontinuities", visualStats.DiscontinuityCount},
              {"namespace_resynchronizations", visualStats.NamespaceResyncCount},
              {"sample_draws", visualStats.SampleDrawCount},
              {"interpolated_draws", visualStats.InterpolatedDrawCount},
              {"interpolated_per_instance_vertex_draws",
               visualStats.InterpolatedPerInstanceVertexDrawCount},
              {"interpolated_per_vertex_draws",
               visualStats.InterpolatedPerVertexDrawCount},
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
                     timings.DrawPreparationSeconds +
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
                     {"superseded_visual_frames",
                      stats.SupersededVisualFrames},
                     {"selected_draws", stats.SelectedDraws},
                     {"executed_draws", stats.ExecutedDraws},
                     {"prepared_draws", stats.PreparedDraws},
                     {"interpolated_draws", stats.InterpolatedDraws},
                     {"execution_lists", stats.ExecutionLists},
                     {"current_frame_lists", stats.CurrentFrameLists},
                     {"native_frame_lists", stats.NativeFrameLists},
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
                     {"draw_preparation_seconds",
                      timings.DrawPreparationSeconds},
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
                   {"topscreen_quest_hook_calls", bridge.topscreen_quest_hook_calls},
                   {"topscreen_quest_transforms", bridge.topscreen_quest_transforms},
                   {"topscreen_quest_hook_failures", bridge.topscreen_quest_hook_failures},
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
                   {"topscreen_ocarina_frames", bridge.topscreen_ocarina_frames},
                   {"topscreen_ocarina_text_builds", ocarinaText.Builds()},
                   {"topscreen_ocarina_text_releases", ocarinaText.Releases()},
                   {"topscreen_ocarina_primitives", bridge.topscreen_ocarina_primitives},
                   {"topscreen_ocarina_failures", bridge.topscreen_ocarina_failures},
                   {"topscreen_ocarina_error", bridge.topscreen_ocarina_error},
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
                   {"version_choice_captures", bridge.version_choice_captures},
                   {"version_choice_last_selection",
                    bridge.version_choice_last_selection},
                   {"version_choice_reveals", bridge.version_choice_reveals},
                   {"native_bottom_frontend_presentation_frames",
                    picaStats.NativeBottomFrontendPresentationCount},
                   {"native_bottom_overlay_frames", 0U},
                   {"native_menu_backdrop_draws_omitted",
                    picaStats.NativeMenuBackdropDrawOmissionCount},
                   {"native_menu_clear_draws_omitted",
                    picaStats.NativeMenuClearDrawOmissionCount},
                   {"native_frontend_composition_draws", 0U},
                   {"native_frontend_presentation_active_frames",
                    picaStats.NativeFrontendPresentationActiveCount},
                   {"native_frontend_bottom_transfer_hits",
                    picaStats.NativeFrontendBottomTransferHitCount},
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
                   {"hd_font_pack_loaded", hdFontsLoaded},
                   {"hd_font_native_blits", hdFonts.Blits},
                   {"hd_font_atlas_rebuilds", hdFonts.Rebuilds},
                   {"hd_font_texture_replacements", hdFonts.Replacements},
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
#if !defined(__APPLE__) || defined(TRIAEVUM_TITLE_MM3D)
              {"mm3d_composition",
               widescreenProjection.Title == nullptr
                   ? nlohmann::json(nullptr)
                   : widescreenProjection.Title->CompositionSummary()},
              {"mm3d_guest_patches",
               widescreenProjection.Title == nullptr
                   ? nlohmann::json(nullptr)
                   : widescreenProjection.Title->GuestPatchSummary()},
#endif
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
                   {"whole_aot_observable_exits",
                    [] {
#if defined(OOT3D_NATIVE_DIRECT_AOT_PLUGIN)
                      const auto report =
                          Oot3dNativeGame::Oot3dWholeAotPluginObservableExitReport();
                      nlohmann::json byPc = nlohmann::json::object();
                      for (const auto &[pc, exits] : report.ExitsByPc) {
                        char key[16];
                        std::snprintf(key, sizeof(key), "0x%08X", pc);
                        byPc[key] = exits;
                      }
                      return nlohmann::json{
                          {"thrown", report.Exits},
                          {"unwind_ns", report.UnwindNanoseconds},
                          {"by_pc", std::move(byPc)}};
#else
                      // Only the direct whole-AOT plugin observes these exits.
                      return nlohmann::json(nullptr);
#endif
                    }()},
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
            {"a32_runtime_profile",
             [&]() {
               const auto timing = process.TimingStats();
               const auto ctrTiming = hostServices.RuntimeProfile();
               const auto picaSubmissionTiming =
                   submissionQueue.RuntimeProfile();
               const auto picaBackend = api.GetNativePicaBackendStats();
               constexpr double nanosecondsToSeconds = 1.0e-9;
               const uint64_t candidateSampleNanoseconds =
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
                   {"fs_unserved_requests", ctrTiming.FsUnservedRequests},
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
                   {"topscreen_pause_last_page_opens",
                    nativeCandidateDispatch.PauseTransaction->LastPageOpens()},
                   {"topscreen_pause_start_close_calls",
                    nativeCandidateDispatch.PauseTransaction->StartCloses()},
                   {"topscreen_pause_system_open_calls",
                    nativeCandidateDispatch.PauseTransaction->SystemOpens()},
                   {"topscreen_pause_select_save_calls",
                    nativeCandidateDispatch.PauseTransaction->SelectSaves()},
                   {"topscreen_pause_retired_transactions",
                    nativeCandidateDispatch.PauseTransaction->Retired()},
                   {"topscreen_item_description_toggles",
                    nativeCandidateDispatch.ItemDescription->Toggles()},
                   {"topscreen_item_description_redraws",
                    nativeCandidateDispatch.ItemDescription->Redraws()},
                   {"topscreen_wide_pages",
                    nativeCandidateDispatch.WideItems->WidePages()},
                   {"topscreen_wide_items_draws",
                    nativeCandidateDispatch.WideItems->WideItemsDraws()},
                   {"topscreen_wide_items_cursor_draws",
                    nativeCandidateDispatch.WideItems->CursorDraws()},
                   {"topscreen_items_inset_updates",
                    nativeCandidateDispatch.WideItems->ItemsInsetUpdates()},
                   {"topscreen_gear_model_draws",
                    nativeCandidateDispatch.WideItems->GearModelDraws()},
                   {"topscreen_quick_put_aways",
                    nativeCandidateDispatch.Gameplay->QuickPutAways()},
                   {"topscreen_landing_rolls",
                    nativeCandidateDispatch.Gameplay->LandingRolls()},
                   {"topscreen_landing_roll_returns",
                    nativeCandidateDispatch.Gameplay->LandingRollReturns()},
                   {"topscreen_landing_roll_web_breaks",
                    nativeCandidateDispatch.Gameplay->LandingRollWebBreaks()},
                   {"topscreen_dash_queries",
                    nativeCandidateDispatch.Gameplay->DashQueries()},
                   {"topscreen_dash_frames",
                    nativeCandidateDispatch.Gameplay->DashFrames()},
                   {"topscreen_dash_navi_hides",
                    nativeCandidateDispatch.Gameplay->DashNaviHides()},
                   {"topscreen_dash_speed_applications",
                    nativeCandidateDispatch.Gameplay->DashSpeedApplications()},
                   {"topscreen_dash_blends",
                    nativeCandidateDispatch.Gameplay->DashBlends()},
                   {"topscreen_dash_blend_poses",
                    nativeCandidateDispatch.Gameplay->DashBlendPoses()},
                   {"topscreen_link_ext_answers",
                    nativeCandidateDispatch.Gameplay->LinkExtAnswers()},
                   {"topscreen_link_ext_entry_restores",
                    nativeCandidateDispatch.Gameplay->LinkExtEntryRestores()},
                   {"topscreen_reticle_casts",
                    nativeCandidateDispatch.Gameplay->ReticleCasts()},
                   {"topscreen_reticle_hits",
                    nativeCandidateDispatch.Gameplay->ReticleHits()},
                   {"topscreen_link_ext_error",
                    nativeCandidateDispatch.Gameplay->LinkExtError()},
                   {"object_bank_lookups",
                    nativeCandidateDispatch.ObjectBank->Stats().Lookups},
                   {"object_bank_extra_hits",
                    nativeCandidateDispatch.ObjectBank->Stats().ExtraHits},
                   {"object_bank_loads",
                    nativeCandidateDispatch.ObjectBank->Stats().Loads},
                   {"object_bank_load_failures",
                    nativeCandidateDispatch.ObjectBank->Stats().LoadFailures},
                   {"object_bank_frees",
                    nativeCandidateDispatch.ObjectBank->Stats().Frees},
                   {"object_bank_room_marks",
                    nativeCandidateDispatch.ObjectBank->Stats().RoomMarks},
                   {"object_bank_sweeps",
                    nativeCandidateDispatch.ObjectBank->Stats().Sweeps},
                   {"object_bank_native_calls",
                    nativeCandidateDispatch.ObjectBank->Stats().NativeCalls},
                   {"object_bank_loading_cancels",
                    nativeCandidateDispatch.ObjectBank->Stats().LoadingCancels},
                   {"object_bank_deferred_queued",
                    nativeCandidateDispatch.ObjectBank->Stats().DeferredQueued},
                   {"object_bank_deferred_releases",
                    nativeCandidateDispatch.ObjectBank->Stats().DeferredReleases},
                   {"object_bank_pending",
                    nativeCandidateDispatch.ObjectBank->Pending()},
                   {"rain_draws", nativeCandidateDispatch.Rain->Stats().Draws},
                   {"rain_surface_draws",
                    nativeCandidateDispatch.Rain->Stats().SurfaceDraws},
                   {"rain_queries", nativeCandidateDispatch.Rain->Stats().Queries},
                   {"rain_streaks_kept",
                    nativeCandidateDispatch.Rain->Stats().StreaksKept},
                   {"rain_streaks_dropped",
                    nativeCandidateDispatch.Rain->Stats().StreaksDropped},
                   {"rain_splashes_kept",
                    nativeCandidateDispatch.Rain->Stats().SplashesKept},
                   {"rain_splashes_dropped",
                    nativeCandidateDispatch.Rain->Stats().SplashesDropped},
                   {"rain_last_native_count",
                    nativeCandidateDispatch.Rain->Stats().LastNativeCount},
                   {"rain_last_count", nativeCandidateDispatch.Rain->Stats().LastCount},
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
                        nativeCandidateDispatch.PauseTransaction->LastClosedPage())},
                   {"topscreen_pause_system_source_page",
                    static_cast<uint32_t>(nativeCandidateDispatch.PauseTransaction
                                              ->LastSystemSourcePage())},
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
                      nativeCandidateDispatch.TopScreenItems.PendingSelection},
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
                    nativeCandidateDispatch.TopScreenItems.QueryCalls},
                   {"topscreen_item_query_trace", [&] {
                      auto rows = nlohmann::json::array();
                      for (const auto& row : nativeCandidateDispatch.TopScreenItems.QueryTrace)
                        rows.push_back({{"entry", row.Entry}, {"return_pc", row.ReturnPc},
                            {"flags", row.SuppressionFlags}, {"native", row.NativeResult},
                            {"result", row.Result}, {"zr_pressed", row.Input.ZrPressed},
                            {"zl_pressed", row.Input.ZlPressed}, {"zr_held", row.Input.ZrHeld},
                            {"zl_held", row.Input.ZlHeld}});
                      return rows;
                    }()},
                   {"topscreen_input_native_updates",
                    nativeCandidateDispatch.TopScreenInputClock.Updates},
                   {"topscreen_input_zr_press_updates",
                    nativeCandidateDispatch.TopScreenInputClock.ZrPressUpdates},
                   {"topscreen_input_zl_press_updates",
                    nativeCandidateDispatch.TopScreenInputClock.ZlPressUpdates},
                   {"topscreen_items_selection_updates",
                    nativeCandidateDispatch.TopScreenItems.SelectionUpdates},
                   {"topscreen_item_query_true_results",
                    nativeCandidateDispatch.TopScreenItems.QueryTrueResults},
                   {"topscreen_slot_item_attempts",
                    nativeCandidateDispatch.TopScreenItems.SlotAttempts},
                   {"topscreen_slot_item_overrides",
                    nativeCandidateDispatch.TopScreenItems.SlotOverrides},
                   {"topscreen_slot_item_native_fallbacks",
                    nativeCandidateDispatch.TopScreenItems.SlotNativeFallbacks},
                   {"topscreen_item_query_entries",
                    nativeCandidateDispatch.TopScreenItems.QueryEntries},
                   {"topscreen_slot_item_entries",
                    nativeCandidateDispatch.TopScreenItems.SlotEntries},
                   {"topscreen_items_selection_begins",
                    nativeCandidateDispatch.TopScreenItems.SelectionBegins},
                   {"topscreen_items_selection_completions",
                    nativeCandidateDispatch.TopScreenItems.SelectionCompletions},
                   {"candidate_samples", nativeCandidateDispatch.Samples},
                   {"candidate_estimated_seconds",
                    candidateSampleNanoseconds * kA32RuntimeSampleDenominator *
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
             {{"slow_frames", [&]() {
                 constexpr const char* names[]{"guest", "frame_start", "host_frame_start", "input_poll",
                   "renderer_frame_start", "dsp_mix", "audio_output", "pica_submit", "pica_plan",
                   "pica_backend", "pica_diagnostics", "visual_presentation", "visual_replay", "present", "pacing_wait"};
                 auto samples = nlohmann::json::array();
                 for (const auto& sample : benchmark.SlowFrames.Samples()) {
                   nlohmann::json phases;
                   for (size_t i = 0; i < std::size(names); ++i) phases[names[i]] = sample.PhaseMilliseconds[i];
                   samples.push_back({{"frame", sample.Frame}, {"total_ms", sample.Milliseconds}, {"phases_ms", phases}});
                 }
                 return samples;
               }()},
              {"slow_frame_note", "Nested phases overlap; do not sum parent and child timers. Measured benchmark frames only."},
              {"guest_seconds", phaseTiming.GuestSeconds},
              {"frame_start_seconds", phaseTiming.FrameStartSeconds},
              {"host_frame_start_seconds", phaseTiming.HostFrameStartSeconds},
              {"input_poll_seconds", phaseTiming.InputPollSeconds},
              {"renderer_frame_start_seconds", phaseTiming.RendererFrameStartSeconds},
              {"dsp_mix_seconds", phaseTiming.DspMixSeconds},
              {"audio_output_seconds", phaseTiming.AudioOutputSeconds},
              {"pica_submit_seconds", phaseTiming.PicaSubmitSeconds},
              {"pica_plan_seconds", phaseTiming.PicaPlanSeconds},
              {"pica_backend_seconds", phaseTiming.PicaBackendSeconds},
              {"pica_diagnostics_seconds", phaseTiming.PicaDiagnosticsSeconds},
              {"visual_presentation_seconds",
               phaseTiming.VisualPresentationSeconds},
              {"visual_replay_seconds", phaseTiming.VisualReplaySeconds},
              {"present_seconds", phaseTiming.PresentSeconds},
              {"pacing_wait_seconds", phaseTiming.PacingWaitSeconds}}},
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
}

} // namespace Oot3dA32Window
