#include "oot3d_native_a32_savestate.h"

#include "oot3d_native_a32_ctr_host.h"
#include "oot3d_native_a32_dsp_hle.h"
#include "oot3d_native_a32_process.h"
#include "oot3d_native_pica_frontend.h"
#include "oot3d_native_pica_submission.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

using namespace Oot3dNativeGame;

[[noreturn]] void Fail(std::string_view message) {
    std::cerr << "oot3d_native_a32_savestate_tests: " << message << '\n';
    std::exit(1);
}

void Require(bool condition, std::string_view message) {
    if (!condition) {
        Fail(message);
    }
}

} // namespace

int main() {
    Require(!ShouldCaptureNativeA32AutomatedState(false, false, 100U, 0U),
            "automated capture ignored an unavailable frame contract");
    Require(!ShouldCaptureNativeA32AutomatedState(true, true, 100U, 0U),
            "automated capture repeated an accepted request");
    Require(!ShouldCaptureNativeA32AutomatedState(true, false, 66U, 66U),
            "automated capture treated a zero-based frame as a count");
    Require(ShouldCaptureNativeA32AutomatedState(true, false, 67U, 66U),
            "automated capture did not use the completed run-frame count");
    Require(!ShouldCaptureNativeA32AutomatedState(
                true, false, 67U, 28576U),
            "automated capture leaked the restored guest frame domain");

    const auto root = std::filesystem::temp_directory_path() /
                      "oot3d_native_a32_savestate_test";
    const auto statePath = root / "checkpoint.oot3dsav";
    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);

    Oot3dNativePicaFrontend picaFrontend;
    NativeA32CtrHostConfig hostConfig;
    hostConfig.LinearHeapBaseAddress = 0x00100000U;
    hostConfig.LinearHeapSize = 0x1000U;
    hostConfig.HeapBaseAddress = 0x00200000U;
    hostConfig.HeapSize = 0x1000U;
    hostConfig.PicaFrontend = &picaFrontend;
    hostConfig.SaveDataDirectory = root / "savedata";
    NativeA32CtrHostServices host(hostConfig);
    const oot3d::recomp::a32::Registry registry{};
    NativeA32Process process(registry, host);
    std::string error;
    Require(process.MapRegion(
                {"test", 0x00100000U, 0x1000U, true, false, {}}, &error),
            error);
    Require(process.Memory().Write32(0x00100020U, 0x12345678U),
            "cannot initialize guest memory");

    Oot3dPicaPhysicalMemoryView picaMemory(
        process.Memory(), {{0x20000000U, 0x00100000U, 0x1000U}});
    Oot3dNativePicaSubmissionQueue submissionQueue(picaMemory, true);
    NativeA32DspHle dspHle({{0x20000000U, 0x00100000U, 0x1000U}});
    const NativeA32SavestateCompatibility compatibility{
        "code-sha", "oot3d", "source-snapshot"};
    NativeA32SavestateRuntimeState runtime{
        321U, 17U, 0x123456789ULL,
        static_cast<uint32_t>(NativeA32ProcessRunKind::Waiting)};
    runtime.GameplayClockAvailable = true;
    runtime.GameplayTiming = GameplayTimingMode::Enhanced60;
    runtime.GameplayClock = {44U, 21.5, 22.0};
    runtime.PresentationSchedulerAvailable = true;
    runtime.PresentationScheduler = {0.25, 0.5, true};
    runtime.FrameRatePolicyTemporalStateAvailable = true;
    runtime.FrameRatePolicyTemporalState = {0x12340000U, false};
    runtime.TopScreenTemporalStateAvailable = true;
    runtime.TopScreenTemporalState = {
        true, true, 7U, true, true, 11U, 4U, 2U, 13U,
        2U, 1U, 17U, true};
    runtime.PicaVisualReplayStateAvailable = true;
    runtime.PicaVisualReplayState = {0x50U, 0x56U, 0x52U, 0x31U};
    runtime.PicaTextureCacheAvailable = true;
    Oot3d::Renderer::PicaTextureCacheEntrySnapshot cachedTexture;
    cachedTexture.ContentHash = 0x1122334455667788ULL;
    cachedTexture.ReplacementGeneration = 3U;
    cachedTexture.PhysicalAddress = 0x20000100U;
    cachedTexture.SourceWidth = 2U;
    cachedTexture.SourceHeight = 1U;
    cachedTexture.NativeFormat = 0U;
    cachedTexture.NativeWrapS = 2U;
    cachedTexture.NativeWrapT = 3U;
    cachedTexture.MinLinear = true;
    cachedTexture.ImageWidth = 2U;
    cachedTexture.ImageHeight = 1U;
    cachedTexture.CustomReplacementHash = 0x8877665544332211ULL;
    cachedTexture.CustomReplacementPending = true;
    cachedTexture.PixelBytes = {
        0x01U, 0x02U, 0x03U, 0x04U,
        0x05U, 0x06U, 0x07U, 0x08U};
    runtime.PicaTextureCache.push_back(cachedTexture);
    runtime.PicaColorTargetsAvailable = true;
    Oot3d::Renderer::PicaRenderTargetColorSnapshot target;
    target.ColorPhysicalAddress = 0x20000200U;
    target.DepthPhysicalAddress = 0x20000400U;
    target.FramebufferWidth = 2U;
    target.FramebufferHeight = 1U;
    target.FramebufferDepthFormat = 2U;
    target.ImageWidth = 2U;
    target.ImageHeight = 1U;
    target.ColorRgba8 = {
        0x10U, 0x20U, 0x30U, 0x40U,
        0x50U, 0x60U, 0x70U, 0x80U};
    target.NormalGuideRgba8 = {
        0x80U, 0x80U, 0xFFU, 0U,
        0x80U, 0x80U, 0xFFU, 0U};
    target.MaterialGuideRgba8.assign(8U, 0U);
    target.RigidMotionGuideRgba16FloatLe.assign(16U, 0U);
    target.AmbientGuideRgba8 = {
        0xFFU, 0xFFU, 0xFFU, 0U,
        0xFFU, 0xFFU, 0xFFU, 0U};
    target.ShadowR32UintLe.assign(8U, 0xFFU);
    target.DepthValues = {0.25F, 0.75F};
    target.StencilValues = {1U, 2U};
    runtime.PicaColorTargets.push_back(target);
    runtime.PicaPresentationStateAvailable = true;
    Oot3d::Renderer::PicaDisplayImageColorSnapshot displayImage;
    displayImage.OutputPhysicalAddress = 0x20000600U;
    displayImage.ImageWidth = 2U;
    displayImage.ImageHeight = 1U;
    displayImage.Initialized = true;
    displayImage.ColorRgba8 = {
        0x80U, 0x70U, 0x60U, 0x50U,
        0x40U, 0x30U, 0x20U, 0x10U};
    displayImage.HasDepthTarget = true;
    displayImage.DepthTargetColorPhysicalAddress = 0x20000200U;
    displayImage.DepthTargetDepthPhysicalAddress = 0x20000400U;
    displayImage.DepthTargetFramebufferWidth = 2U;
    displayImage.DepthTargetFramebufferHeight = 1U;
    displayImage.DepthTargetFramebufferDepthFormat = 2U;
    runtime.PicaPresentationState.DisplayImages.push_back(displayImage);
    runtime.PicaPresentationState.LastPresentedTransfer =
        Oot3d::Renderer::PicaDisplayTransferView{
            9U, 0U, 0x20000200U, 0x20000600U,
            2U, 1U, 2U, 1U, 0U, true,
            Oot3d::Renderer::PicaPresentationMode::Replace};
    NativeA32SavestateIoResult io;
    Require(SaveNativeA32State(
                statePath, compatibility, runtime, process, host,
                picaFrontend, submissionQueue, dspHle, &io, &error),
            error);
    Require(io.FileBytes > io.PayloadBytes &&
                io.SemanticFingerprint != 0U &&
                std::filesystem::is_regular_file(statePath),
            "savestate container was not written");
    const uint64_t semanticFingerprint = io.SemanticFingerprint;
    const auto redundantStatePath = root / "redundant-write.oot3dsav";
    Require(process.Memory().Write32(0x00100020U, 0x12345678U),
            "cannot issue redundant guest-memory write");
    NativeA32SavestateIoResult redundantIo;
    Require(SaveNativeA32State(
                redundantStatePath, compatibility, runtime, process, host,
                picaFrontend, submissionQueue, dspHle, &redundantIo, &error),
            error);
    Require(redundantIo.SemanticFingerprint == semanticFingerprint,
            "host write generation leaked into savestate semantic identity");
    std::cout << "save capture=" << io.CaptureSeconds
              << " encode=" << io.EncodeSeconds
              << " io=" << io.IoSeconds << '\n';

    Require(process.Memory().Write32(0x00100020U, 0xDEADBEEFU),
            "cannot mutate guest memory");
    runtime = {};
    Require(LoadNativeA32State(
                statePath, compatibility, runtime, process, host,
                picaFrontend, submissionQueue, dspHle, &io, &error),
            error);
    uint32_t restoredWord = 0;
    Require(process.Memory().Read32(0x00100020U, &restoredWord) &&
                restoredWord == 0x12345678U && runtime.FrameCount == 321U &&
                runtime.RefreshTickRemainder == 17U &&
                runtime.NextVblankTick == 0x123456789ULL &&
                runtime.GameplayClockAvailable &&
                runtime.GameplayTiming == GameplayTimingMode::Enhanced60 &&
                runtime.GameplayClock.SimulationTick == 44U &&
                runtime.GameplayClock.PreviousLogicalFrame == 21.5 &&
                runtime.GameplayClock.CurrentLogicalFrame == 22.0 &&
                runtime.PresentationSchedulerAvailable &&
                runtime.PresentationScheduler.GuestRefreshPhase == 0.25 &&
                runtime.PresentationScheduler.VisualSamplePhase == 0.5 &&
                runtime.PresentationScheduler.Started &&
                runtime.FrameRatePolicyTemporalStateAvailable &&
                runtime.FrameRatePolicyTemporalState
                        .FramePacingStateAddress == 0x12340000U &&
                !runtime.FrameRatePolicyTemporalState
                     .FramePacingDecisionPending &&
                runtime.TopScreenTemporalStateAvailable &&
                runtime.TopScreenTemporalState.ProfileActive &&
                runtime.TopScreenTemporalState.PausePageRedrawActive &&
                runtime.TopScreenTemporalState.PausePageRedrawDelayCalls ==
                    7U &&
                runtime.TopScreenTemporalState
                    .PauseDrawNativeTransitionLatched &&
                runtime.TopScreenTemporalState
                    .PauseDrawSuppressionDelayArmed &&
                runtime.TopScreenTemporalState
                        .PauseDrawSuppressionDelayCommands == 11U &&
                runtime.TopScreenTemporalState
                        .PauseRoutePreviousRuntimeMode == 4U &&
                runtime.TopScreenTemporalState.PauseRouteTransitionPhase ==
                    2U &&
                runtime.TopScreenTemporalState.PauseRouteRemainingCalls ==
                    13U &&
                runtime.TopScreenTemporalState
                        .AotPauseRoutePreviousRuntimeMode == 2U &&
                runtime.TopScreenTemporalState
                        .AotPauseRouteTransitionPhase == 1U &&
                runtime.TopScreenTemporalState
                        .AotPauseRouteRemainingCalls == 17U &&
                runtime.TopScreenTemporalState
                    .TouchCoordinateRuntimeSceneLatch &&
                runtime.PicaVisualReplayStateAvailable &&
                runtime.PicaVisualReplayState ==
                    std::vector<uint8_t>({0x50U, 0x56U, 0x52U, 0x31U}) &&
                runtime.PicaTextureCacheAvailable &&
                runtime.PicaTextureCache.size() == 1U &&
                runtime.PicaTextureCache[0].ContentHash ==
                    0x1122334455667788ULL &&
                runtime.PicaTextureCache[0].ReplacementGeneration == 3U &&
                runtime.PicaTextureCache[0].PhysicalAddress ==
                    0x20000100U &&
                runtime.PicaTextureCache[0].SourceWidth == 2U &&
                runtime.PicaTextureCache[0].SourceHeight == 1U &&
                runtime.PicaTextureCache[0].MinLinear &&
                runtime.PicaTextureCache[0].CustomReplacementPending &&
                runtime.PicaTextureCache[0].PixelBytes ==
                    std::vector<uint8_t>({
                        0x01U, 0x02U, 0x03U, 0x04U,
                        0x05U, 0x06U, 0x07U, 0x08U}) &&
                runtime.PicaColorTargetsAvailable &&
                runtime.PicaColorTargets.size() == 1U &&
                runtime.PicaColorTargets[0].ColorPhysicalAddress ==
                    0x20000200U &&
                runtime.PicaColorTargets[0].ImageWidth == 2U &&
                runtime.PicaColorTargets[0].ImageHeight == 1U &&
                runtime.PicaColorTargets[0].ColorRgba8 ==
                    std::vector<uint8_t>({
                        0x10U, 0x20U, 0x30U, 0x40U,
                        0x50U, 0x60U, 0x70U, 0x80U}) &&
                runtime.PicaColorTargets[0].DepthValues ==
                    std::vector<float>({0.25F, 0.75F}) &&
                runtime.PicaColorTargets[0].StencilValues ==
                    std::vector<uint8_t>({1U, 2U}) &&
                runtime.PicaPresentationStateAvailable &&
                runtime.PicaPresentationState.DisplayImages.size() == 1U &&
                runtime.PicaPresentationState.DisplayImages[0]
                        .OutputPhysicalAddress == 0x20000600U &&
                runtime.PicaPresentationState.DisplayImages[0]
                        .HasDepthTarget &&
                runtime.PicaPresentationState.DisplayImages[0].ColorRgba8 ==
                    std::vector<uint8_t>({
                        0x80U, 0x70U, 0x60U, 0x50U,
                        0x40U, 0x30U, 0x20U, 0x10U}) &&
                runtime.PicaPresentationState.LastPresentedTransfer
                    .has_value() &&
                runtime.PicaPresentationState.LastPresentedTransfer
                        ->CompletionId == 9U,
            "savestate did not restore guest memory and runtime clock");
    std::cout << "load io=" << io.IoSeconds
              << " decode=" << io.DecodeSeconds
              << " restore=" << io.RestoreSeconds << '\n';

    auto incompatible = compatibility;
    incompatible.CodeBinSha256 = "other-code";
    Require(!LoadNativeA32State(
                statePath, incompatible, runtime, process, host,
                picaFrontend, submissionQueue, dspHle, nullptr, &error),
            "savestate accepted an incompatible code.bin identity");

    std::filesystem::remove_all(root, cleanupError);
    std::cout << "oot3d_native_a32_savestate_tests: ok\n";
    return 0;
}
