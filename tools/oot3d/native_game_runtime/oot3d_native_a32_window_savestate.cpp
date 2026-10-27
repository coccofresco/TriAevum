// Native A32 window session: savestate save, load and the restore of renderer and title state.
#include "oot3d_native_a32_window_session.h"
#include "fast/renderer3ds/pica_framebuffer_encode.h"
#include "fast/renderer3ds/pica_guest_extent_snapshot.h"

#include <optional>

namespace Oot3dA32Window {

void NativeA32WindowSession::RestoreSavestateTiming(const Oot3dNativeGame::NativeA32SavestateRuntimeState &runtime) {
  auto &frameRatePolicy = *frameRatePolicyStorage;
  auto &presentationScheduler = *presentationSchedulerStorage;
  if (!runtime.GameplayClockAvailable) {
    frameRatePolicy.ResetGameplayClock();
    ++savestateStats.LegacyClockRestoreCount;
    std::cerr
        << "oot3d_native_game: legacy savestate has no gameplay clock; "
           "starting at a quiescent logical-frame boundary\n";
  } else {
    if (!frameRatePolicy.RestoreGameplayClock(
            runtime.GameplayTiming, runtime.GameplayClock)) {
      throw std::runtime_error(
          "savestate gameplay clock restore failed: " +
          frameRatePolicy.LastError());
    }
  }
  if (!runtime.PresentationSchedulerAvailable) {
    presentationScheduler.Reset();
    ++savestateStats.LegacyPresentationSchedulerRestoreCount;
  } else if (!presentationScheduler.Restore(
                 runtime.PresentationScheduler, &error)) {
    throw std::runtime_error(
        "savestate presentation scheduler restore failed: " + error);
  }
  if (!runtime.FrameRatePolicyTemporalStateAvailable) {
    frameRatePolicy.ResetTemporalState();
    ++savestateStats.LegacyFrameRatePolicyRestoreCount;
  } else if (!frameRatePolicy.RestoreTemporalState(
                 runtime.FrameRatePolicyTemporalState, &error)) {
    throw std::runtime_error(
        "savestate frame-rate policy restore failed: " + error);
  }
}

void NativeA32WindowSession::RestoreSavestateTopScreenTemporalState(const Oot3dNativeGame::NativeA32SavestateRuntimeState &runtime) {
  auto &uiLifecycleBridge = *uiLifecycleBridgeStorage;
  if (launch.UiProfile !=
      Oot3dNativeGame::Oot3dUiProfile::TopScreen) {
    return;
  }
  if (!runtime.TopScreenTemporalStateAvailable ||
      !runtime.TopScreenTemporalState.ProfileActive) {
    ++savestateStats.LegacyTopScreenTemporalRestoreCount;
    return;
  }
  const auto &state = runtime.TopScreenTemporalState;
  if (state.ProjectionAvailable) {
    uiLifecycleBridge.TopScreenPauseProjection() = state.Projection;
    uiLifecycleBridge.TopScreenPauseProjection().AlternatePage =
        activeTopScreenConfig.MinimapVisible;
  }
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
}

void NativeA32WindowSession::RestoreSavestatePicaTextureCache(const Oot3dNativeGame::NativeA32SavestateRuntimeState &runtime) {
  auto &api = *apiPointer;
  if (!runtime.PicaTextureCacheAvailable) {
    ++savestateStats.LegacyPicaTextureCacheRestoreCount;
    return;
  }
  if (!api.RestorePicaTextureCache(
          runtime.PicaTextureCache, &error)) {
    throw std::runtime_error(
        "savestate native PICA texture restore failed: " + error);
  }
  for (const auto &texture : runtime.PicaTextureCache) {
    savestateStats.PicaTextureBytesRestored +=
        texture.PixelBytes.size();
  }
  savestateStats.PicaTexturesRestored +=
      runtime.PicaTextureCache.size();
}

void NativeA32WindowSession::RestoreSavestatePicaColorTargets(const Oot3dNativeGame::NativeA32SavestateRuntimeState &runtime) {
  auto &submissionQueue = *submissionQueueStorage;
  auto &api = *apiPointer;
  if (runtime.PicaGuestFramebuffersAvailable) {
    // Graphics-free state: the targets are decoded from the restored
    // emulated memory at the guest extent and rebuilt at this renderer's
    // resolution, like a fresh renderer reading VRAM.
    auto &process = *processStorage;
    const auto regions = dspHleStorage->PhysicalRegions();
    const auto readPhysical = [&](uint32_t address, size_t size) {
      std::vector<uint8_t> bytes(size);
      for (const auto &region : regions) {
        if (address < region.PhysicalAddress) continue;
        const uint64_t offset = address - region.PhysicalAddress;
        if (offset + size <= region.Size &&
            process.Memory().ReadBytes(region.VirtualAddress + static_cast<uint32_t>(offset), bytes))
          return bytes;
      }
      throw std::runtime_error("savestate guest framebuffer lies outside emulated memory");
    };
    std::vector<Oot3d::Renderer::PicaRenderTargetColorSnapshot> targets = runtime.PicaGuestFramebuffers;
    std::vector<uint32_t> restoredAddresses;
    for (auto &target : targets) {
      const uint32_t width = target.FramebufferWidth;
      const uint32_t height = target.FramebufferHeight;
      const uint8_t colorFormat = target.FramebufferColorFormat;
      const uint32_t colorBytes = colorFormat == 0 ? 4U : colorFormat == 1 ? 3U : 2U;
      target.ColorRgba8 = Fast::Renderer3ds::DecodePicaFramebuffer(
          readPhysical(target.ColorPhysicalAddress, size_t{width} * height * colorBytes), width, height,
          colorFormat);
      const uint32_t depthBytes = Fast::Renderer3ds::PicaDepthFormatBytes(target.FramebufferDepthFormat);
      if (target.DepthPhysicalAddress != 0U && depthBytes != 0U) {
        Fast::Renderer3ds::DecodePicaDepthStencil(
            readPhysical(target.DepthPhysicalAddress, size_t{width} * height * depthBytes), width, height,
            target.FramebufferDepthFormat, target.DepthValues, target.StencilValues);
      } else {
        target.DepthValues.assign(size_t{width} * height, 1.0F);
      }
      restoredAddresses.push_back(target.ColorPhysicalAddress);
      savestateStats.PicaColorTargetBytesRestored += target.ColorRgba8.size();
    }
    if (!api.RestorePicaColorTargets(targets, &error)) {
      throw std::runtime_error("savestate guest framebuffer restore failed: " + error);
    }
    submissionQueue.RestoreGpuColorRenderTargetOwnership(restoredAddresses);
    savestateStats.PicaColorTargetsRestored += restoredAddresses.size();
    return;
  }
  if (!runtime.PicaColorTargetsAvailable) {
    ++savestateStats.LegacyPicaColorTargetRestoreCount;
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
    savestateStats.PicaColorTargetBytesRestored +=
        target.ColorRgba8.size();
  }
  submissionQueue.RestoreGpuColorRenderTargetOwnership(
      restoredAddresses);
  savestateStats.PicaColorTargetsRestored += restoredAddresses.size();
}

void NativeA32WindowSession::RestoreSavestatePicaPresentationState(const Oot3dNativeGame::NativeA32SavestateRuntimeState &runtime) {
  auto &api = *apiPointer;
  if (!runtime.PicaPresentationStateAvailable) {
    ++savestateStats.LegacyPicaPresentationRestoreCount;
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
    savestateStats.PicaDisplayImageBytesRestored +=
        image.ColorRgba8.size();
  }
  savestateStats.PicaDisplayImagesRestored +=
      runtime.PicaPresentationState.DisplayImages.size();
}

void NativeA32WindowSession::RestoreSavestatePicaVisualReplayState(const Oot3dNativeGame::NativeA32SavestateRuntimeState &runtime) {
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
    ++savestateStats.LegacyPicaVisualReplayRestoreCount;
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
  savestateStats.PicaVisualReplayBytesRestored +=
      runtime.PicaVisualReplayState.size();
}

NativeA32WindowSession::SaveStateOutcome
NativeA32WindowSession::FailSave(std::string message) {
  std::cerr << "oot3d_native_game: " << message << '\n';
  ++savestateStats.SaveFailureCount;
  lastSavestateMessage = std::move(message);
  return SaveStateOutcome::Failed;
}

NativeA32WindowSession::SaveStateOutcome
NativeA32WindowSession::SaveState(const std::filesystem::path &path, bool withSettings,
                                  const std::string *storedSettings) {
  auto &hostServices = *hostServicesStorage;
  auto &frameRatePolicy = *frameRatePolicyStorage;
  auto &process = *processStorage;
  auto &uiLifecycleBridge = *uiLifecycleBridgeStorage;
  auto &submissionQueue = *submissionQueueStorage;
  auto &api = *apiPointer;
  auto &ocarinaText = *ocarinaTextStorage;
  auto &dspHle = *dspHleStorage;
  auto &presentationScheduler = *presentationSchedulerStorage;
  // Barrier: the savestate captures renderer-owned replay state.
  renderStream.Drain();
  // A state holds the native Items labels' own colours, never the ones the
  // HUD button labels cleared for a draw (the next draw clears them again).
  if (wideItemsStorage) wideItemsStorage->RestoreHiddenItemsLabels();
  // Host-owned continuation registers must never escape into a standalone savestate.
  if (ocarinaText.Pending()) return SaveStateOutcome::Deferred;
  if (pauseTransactionStorage->NeedsExecution()) return SaveStateOutcome::Deferred;
  if (systemPageStorage->Pending() || widescreenProjection.PendingTopScreenPauseChildRestore.has_value())
    return SaveStateOutcome::Deferred;
  if (itemDescriptionStorage->Pending()) return SaveStateOutcome::Deferred;
  if (Oot3dNativeGame::NativeActorVisibilityHasPendingRestore(widescreenProjection.ActorVisibility))
    return SaveStateOutcome::Deferred;
  if (objectBankStorage->Pending()) return SaveStateOutcome::Deferred;
  // The temporary rain count and the outer weather frame are host-held.
  if (rainStorage->Pending()) return SaveStateOutcome::Deferred;
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
  topScreenState.ProjectionAvailable = true;
  topScreenState.Projection = uiLifecycleBridge.TopScreenPauseProjection();
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
  // Presentation state (visual replay, interpolation targets, display
  // images) is not the game moment: the restoring session starts its
  // presentation afresh and the next guest frame rebuilds it.
  // Graphics-free state, like an Azahar state after its rasterizer flush:
  // the game's render targets (the native namespace) are written at their
  // native extent and formats into the saved copy of emulated memory (VRAM),
  // and only their identities travel. GPU copies the game made are already in
  // memory (the memory capture resolves deferred writes). No texture cache,
  // interpolation target or display image is saved: the restoring renderer
  // rebuilds them from memory and the next frame.
  {
    std::vector<Oot3d::Renderer::PicaRenderTargetColorSnapshot> targets;
    if (!api.CapturePicaColorTargets(targets, &error)) {
      return FailSave("native PICA color capture failed: " + error);
    }
    const auto regions = dspHleStorage->PhysicalRegions();
    const auto virtualAddress = [&](uint32_t address, size_t size) -> std::optional<uint32_t> {
      for (const auto &region : regions) {
        if (address < region.PhysicalAddress) continue;
        const uint64_t offset = address - region.PhysicalAddress;
        if (offset + size <= region.Size) return region.VirtualAddress + static_cast<uint32_t>(offset);
      }
      return std::nullopt;
    };
    try {
      for (auto &target : targets) {
        if (target.RenderTargetNamespace != kNativeDependencyRenderTargetNamespace) continue;
        if (target.RenderScalePermille != Fast::Renderer3ds::kPicaGuestExtentRenderScalePermille ||
            target.ImageWidth != target.FramebufferWidth || target.ImageHeight != target.FramebufferHeight) {
          return FailSave("native PICA color capture is not at the guest extent");
        }
        auto color = Fast::Renderer3ds::EncodePicaFramebuffer(
            target.ColorRgba8, target.ImageWidth, target.ImageHeight, target.FramebufferWidth,
            target.FramebufferHeight, target.FramebufferColorFormat);
        const auto colorAddress = virtualAddress(target.ColorPhysicalAddress, color.size());
        if (!colorAddress) return FailSave("native PICA color target lies outside emulated memory");
        savestateStats.PicaColorTargetBytesCaptured += color.size();
        runtime.GuestMemoryOverlays.push_back({*colorAddress, std::move(color)});
        if (target.DepthPhysicalAddress != 0U &&
            Fast::Renderer3ds::PicaDepthFormatBytes(target.FramebufferDepthFormat) != 0U &&
            target.DepthValues.size() == size_t{target.ImageWidth} * target.ImageHeight) {
          auto depth = Fast::Renderer3ds::EncodePicaDepthStencil(
              target.DepthValues, target.StencilValues, target.ImageWidth, target.ImageHeight,
              target.FramebufferDepthFormat);
          const auto depthAddress = virtualAddress(target.DepthPhysicalAddress, depth.size());
          if (!depthAddress) return FailSave("native PICA depth target lies outside emulated memory");
          runtime.GuestMemoryOverlays.push_back({*depthAddress, std::move(depth)});
        }
        target.ColorRgba8.clear();
        target.DepthValues.clear();
        target.StencilValues.clear();
        runtime.PicaGuestFramebuffers.push_back(std::move(target));
      }
    } catch (const std::exception &exception) {
      return FailSave(std::string("native PICA framebuffer encode failed: ") + exception.what());
    }
    runtime.PicaGuestFramebuffersAvailable = true;
    savestateStats.PicaColorTargetsCaptured += runtime.PicaGuestFramebuffers.size();
  }
  if (storedSettings != nullptr) {
    runtime.HostSettingsAvailable = true;
    runtime.HostSettingsJson = *storedSettings;
  } else if (withSettings) {
    runtime.HostSettingsAvailable = true;
    runtime.HostSettingsJson =
        Oot3dNativeGame::CaptureSavestateSettings(SavestateSettingsOwners());
  }
  Oot3dNativeGame::NativeA32SavestateIoResult ioResult;
  if (!Oot3dNativeGame::SaveNativeA32State(
          path, savestateCompatibility, runtime, process, hostServices,
          picaFrontend, submissionQueue, dspHle, &ioResult, &error)) {
    return FailSave("savestate save failed: " + error);
  }
  ++savestateStats.SaveCount;
  savestateStats.SaveBytes += ioResult.FileBytes;
  savestateStats.LastSemanticFingerprint = ioResult.SemanticFingerprint;
  savestateStats.SaveSeconds += SecondsSince(started);
  savestateStats.CaptureSeconds += ioResult.CaptureSeconds;
  savestateStats.EncodeSeconds += ioResult.EncodeSeconds;
  savestateStats.WriteSeconds += ioResult.IoSeconds;
  std::cout << "oot3d_native_game: saved state at frame " << frameCount
            << " to " << path.string() << '\n';
  lastSavestateMessage.clear();
  return SaveStateOutcome::Saved;
}

bool NativeA32WindowSession::LoadState(const std::filesystem::path &path,
                                       bool applySettings) {
  auto &hostServices = *hostServicesStorage;
  auto &process = *processStorage;
  auto &uiLifecycleBridge = *uiLifecycleBridgeStorage;
  auto &submissionQueue = *submissionQueueStorage;
  auto &api = *apiPointer;
  auto &ocarinaText = *ocarinaTextStorage;
  auto &n64UiRenderer = *n64UiRendererStorage;
  auto &dspHle = *dspHleStorage;
  auto &realtimePacer = *realtimePacerStorage;
  // Barrier: loading resets renderer-owned state.
  renderStream.Drain();
  const auto started = std::chrono::steady_clock::now();
  Oot3dNativeGame::NativeA32SavestateRuntimeState runtime;
  Oot3dNativeGame::NativeA32SavestateIoResult ioResult;
  if (!Oot3dNativeGame::LoadNativeA32State(
          path, savestateCompatibility, runtime, process, hostServices,
          picaFrontend, submissionQueue, dspHle, &ioResult, &error)) {
    std::cerr << "oot3d_native_game: savestate load failed: " << error
              << '\n';
    lastSavestateMessage = error;
    return false;
  }
  // Settings first: the renderer restore below rebuilds the saved content at
  // the scale they select.
  ApplyLoadedSavestateSettings(runtime, applySettings);
  Oot3dNativeGame::ResetOot3dTypedGameplayTransientState();
  Oot3dNativeGame::ResetNativeActorVisibilityAfterStateLoad(widescreenProjection.ActorVisibility);
  picaCompositionTracker.Reset();
  if (titleSession) titleSession->Reset();
  widescreenProjection.Projection.PendingSceneFrustum.reset();
  const auto restoredGravity = hostServices.HidRuntimeProfile().LastAccelerometer;
  nativeControlPollingState.VirtualMotion.RestoreGravity(
      {static_cast<float>(restoredGravity[0]), static_cast<float>(restoredGravity[1]),
       static_cast<float>(restoredGravity[2])});
  nativeControlPollingState.PendingMouseDeltaX = 0;
  nativeControlPollingState.PendingMouseDeltaY = 0;
  nativeControlPollingState.PendingInputSeconds = 0.0;
  ApplySelectedUiProfile();
  if (!api.ResetPicaState(&error)) {
    throw std::runtime_error(
        "native PICA reset after savestate load failed: " + error);
  }
  if (titleRenderBackend != nullptr &&
      !titleRenderBackend->ResetTitleState(&error)) {
    throw std::runtime_error(
        "OOT3D renderer reset after savestate load failed: " + error);
  }
  RestoreSavestatePicaTextureCache(runtime);
  RestoreSavestatePicaColorTargets(runtime);
  RestoreSavestatePicaPresentationState(runtime);
  if (audioPlayer != nullptr && audioPlayer->IsInitialized()) {
    audioPlayer->Flush();
  }
  frameCount = runtime.FrameCount;
  refreshTickRemainder = runtime.RefreshTickRemainder;
  nextVblankTick = runtime.NextVblankTick;
  processResult = {};
  processResult.Kind = static_cast<Oot3dNativeGame::NativeA32ProcessRunKind>(
      runtime.ProcessRunKind);
  RestoreSavestateTiming(runtime);
  picaFrontend.SetPacketSink(&submissionQueue);
  uiLifecycleBridge.ResetAfterStateLoad(frameCount);
  hdFonts.Reset();
  ocarinaText.Reset();
  pauseTransactionStorage->Reset();
  systemPageStorage->Reset();
  itemDescriptionStorage->Reset();
  wideItemsStorage->Reset();
  if (visionsStorage) visionsStorage->Reset();
  if (reframedFileSelectStorage) reframedFileSelectStorage->Reset();
  gameplayStorage->Reset();
  // Saves wait for an in-flight native load or free, so the loaded memory
  // (its extra-slot page included) is the whole object bank state.
  objectBankStorage->Reset();
  rainStorage->Reset();
  PublishTopScreenUiConfig();
  widescreenProjection.TopScreenPauseDrawRouting = {};
  widescreenProjection.TopScreenPausePageRedraw = {};
  widescreenProjection.TopScreenPausePageRedrawActive = false;
  widescreenProjection.TopScreenPausePageCompositionCalls = 0U;
  widescreenProjection.TopScreenPausePageRedraws = 0U;
  widescreenProjection.TopScreenPausePageRedrawSkips = 0U;
  widescreenProjection.PendingTopScreenPauseChildRestore.reset();
  nativeCandidateDispatch.TopScreenInput = {};
  nativeCandidateDispatch.TopScreenInputClock.Reset();
  nativeCandidateDispatch.TopScreenGameplayActionRuntime = {};
  nativeCandidateDispatch.PreviousTopScreenButtons = 0U;
  nativeCandidateDispatch.StartButtonLatch = {};
  nativeCandidateDispatch.TopScreenStartRouting = {};
  nativeCandidateDispatch.TopScreenVersionChoiceReveal = {};
  nativeCandidateDispatch.TopScreenPauseSystemOpen = {};
  nativeCandidateDispatch.TopScreenViewportDrawPhase = 0U;
  nativeCandidateDispatch.TopScreenViewportDrawArgument = 0U;
  nativeCandidateDispatch.TopScreenOverlayViewportDrawPhase = 0U;
  nativeCandidateDispatch.TopScreenTouchCoordinateUpdatePhase = 0U;
  nativeCandidateDispatch.TopScreenAlternateRendererRestore.reset();
  nativeCandidateDispatch.TopScreenPauseRoute = {};
  nativeCandidateDispatch.TopScreenTouchCoordinateRoute = {};
  nativeCandidateDispatch.TopScreenRendererVisibilityRoute = {};
  nativeCandidateDispatch.TopScreenGameOverDrawPhase = 0U;
  nativeCandidateDispatch.TopScreenVisionsRouted = false;
  nativeCandidateDispatch.TopScreenVisionsFadeStep = 0;
  nativeCandidateDispatch.TopScreenVisionsDrawPhase = 0U;
  nativeCandidateDispatch.TopScreenPauseController = {};
  nativeCandidateDispatch.TopScreenPauseControllerDrawPhase = 0U;
  nativeCandidateDispatch.TopScreenPauseControllerVisibleRenderers = {};
  nativeCandidateDispatch.TopScreenItems.PendingSelection = 0U;
  nativeCandidateDispatch.TopScreenItems.PreviousSelectionButtons = 0U;
  nativeCandidateDispatch.TopScreenItems.PreviousSelectionPressed = 0U;
  nativeCandidateDispatch.TopScreenCamera = {};
  nativeCandidateDispatch.TopScreenCameraInput.Reset();
  Oot3dNativeGame::ApplyTopScreenFreeCameraConfig(
      nativeCandidateDispatch.TopScreenCamera.Camera,
      activeTopScreenConfig);
  nativeCandidateDispatch.TopScreenExtendedCameraActive = false;
  RestoreSavestateTopScreenTemporalState(runtime);
  sceneViewProbe.Reset();
  sceneEnvironment->ObserveScene(std::nullopt);
  Fast::Extension::GrassInteractionBridge::Instance().Reset();
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
  RestoreSavestatePicaVisualReplayState(runtime);
  pcmContinuityDiagnostics = {};
  realtimePacer.ResetDeadline();
  lastPresentationTime = std::chrono::steady_clock::now();
  ++savestateStats.LoadCount;
  savestateStats.LoadBytes += ioResult.FileBytes;
  savestateStats.LoadSeconds += SecondsSince(started);
  savestateStats.ReadSeconds += ioResult.IoSeconds;
  savestateStats.DecodeSeconds += ioResult.DecodeSeconds;
  savestateStats.RestoreSeconds += ioResult.RestoreSeconds;
  std::cout << "oot3d_native_game: loaded state at frame " << frameCount
            << " from " << path.string() << '\n';
  return true;
}

void NativeA32WindowSession::ApplyLoadedSavestateSettings(
    const Oot3dNativeGame::NativeA32SavestateRuntimeState &runtime,
    bool applySettings) {
  lastSavestateMessage.clear();
  if (!applySettings) return;
  if (!runtime.HostSettingsAvailable) {
    lastSavestateMessage = "this state has no saved settings; current settings kept";
    std::cout << "oot3d_native_game: savestate settings: none stored\n";
    return;
  }
  const auto applied = Oot3dNativeGame::ApplySavestateSettings(
      runtime.HostSettingsJson, SavestateSettingsOwners());
  for (const auto &line : applied.Lines) {
    std::cout << "oot3d_native_game: savestate settings: " << line << '\n';
    lastSavestateMessage += (lastSavestateMessage.empty() ? "" : "\n") + line;
  }
  if (topScreenConfigRuntime != nullptr) ApplyTopScreenConfigSnapshot();
}

Oot3dNativeGame::SavestateSettingsOwners
NativeA32WindowSession::SavestateSettingsOwners() const {
  return {launch.UiProfile == Oot3dNativeGame::Oot3dUiProfile::TopScreen
              ? topScreenConfigRuntime
              : nullptr,
          controlConfigRuntime, languageSettings};
}

void NativeA32WindowSession::ServiceSavestateRequests() {
  auto request = savestateLibrary->TakeRequest();
  if (!request.has_value()) return;
  const auto report = [&](bool success, std::string message) {
    std::cout << "oot3d_native_game: savestate request "
              << (success ? "succeeded" : "failed") << ": " << message << '\n';
    Fast::Extension::PostHostNotice(message, !success);
    savestateLibrary->Report(success, std::move(message));
  };
  const auto fileName = [](const std::filesystem::path &path) {
    const auto text = path.filename().u8string();
    return std::string(text.begin(), text.end());
  };
  if (request->Kind == Oot3dNativeGame::SavestateRequestKind::Save) {
    const auto path = savestateLibrary->NewStatePath(request->WithSettings);
    std::error_code directoryError;
    std::filesystem::create_directories(path.parent_path(), directoryError);
    if (directoryError) {
      report(false, "Save state failed: cannot create the folder (" +
                        directoryError.message() + ")");
      return;
    }
    switch (SaveState(path, request->WithSettings)) {
    case SaveStateOutcome::Deferred:
      ++savestateStats.SaveDeferralCount;
      savestateLibrary->Request(*request);  // Retried at the next boundary.
      return;
    case SaveStateOutcome::Failed:
      report(false, "Save state failed: " + lastSavestateMessage);
      return;
    case SaveStateOutcome::Saved:
      savestateLibrary->SetLastState(path);
      report(true, std::string(request->WithSettings ? "State and settings saved: "
                                                     : "State saved: ") +
                       fileName(path));
      return;
    }
    return;
  }
  const auto path = request->Path.empty() ? savestateLibrary->LastState()
                                          : std::optional(request->Path);
  if (!path.has_value()) {
    report(false, "No save state to load yet");
    return;
  }
  std::error_code existsError;
  if (!std::filesystem::is_regular_file(*path, existsError)) {
    report(false, "Save state not found: " + fileName(*path));
    return;
  }
  if (!LoadState(*path, request->WithSettings)) {
    report(false, "Load failed: " + lastSavestateMessage);
    return;
  }
  savestateLibrary->SetLastState(*path);
  std::string message = (request->WithSettings ? "State and settings loaded: "
                                               : "State loaded: ") +
                        fileName(*path);
  if (!lastSavestateMessage.empty()) message += "\n" + lastSavestateMessage;
  report(true, std::move(message));
}

} // namespace Oot3dA32Window
