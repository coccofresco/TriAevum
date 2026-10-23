#include "oot3d_native_a32_savestate.h"

#include "oot3d_native_a32_ctr_host.h"
#include "oot3d_native_a32_dsp_hle.h"
#include "oot3d_native_a32_process.h"
#include "oot3d_native_pica_frontend.h"
#include "oot3d_native_pica_submission.h"
#include "oot3d_savestate_library.h"
#include "fast/renderer3ds/pica_guest_extent_snapshot.h"

#include <chrono>
#include <fstream>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <string_view>
#include <vector>

#include <zstd.h>

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
    runtime.TopScreenTemporalState.ProjectionAvailable = true;
    runtime.TopScreenTemporalState.Projection.OffsetX = 280.0F;
    runtime.TopScreenTemporalState.Projection.MapX = {0x1000U, 0x42400000U, 0x43a40000U, true};
    runtime.TopScreenTemporalState.Projection.IconX[255] = {0x1004U, 0x42000000U, 0x439c0000U, true};
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
    // A GPU copy version carries its identity in CopyProducer; the renderer
    // leaves every ordinary native cache key field zero for these entries.
    Oot3d::Renderer::PicaTextureCacheEntrySnapshot copiedTexture;
    copiedTexture.ImageWidth = 2U;
    copiedTexture.ImageHeight = 1U;
    copiedTexture.PixelBytes = {
        0x11U, 0x22U, 0x33U, 0x44U,
        0x55U, 0x66U, 0x77U, 0x88U};
    copiedTexture.HasCopyProducer = true;
    copiedTexture.CopyProducer.RenderTargetNamespace = 0U;
    copiedTexture.CopyProducer.CompletionId = 41U;
    copiedTexture.CopyProducer.AfterDrawSubmissionId = 39U;
    copiedTexture.CopyProducer.SourcePhysicalAddress = 0x20000200U;
    copiedTexture.CopyProducer.SourceWidth = 2U;
    copiedTexture.CopyProducer.SourceHeight = 1U;
    copiedTexture.CopyProducer.SourceNativeFormat = 0U;
    copiedTexture.CopyProducer.Spans = {{0x20000200U, 0x20000300U, 8U}};
    copiedTexture.CopyProducer.ColorEncoding = 1U;
    copiedTexture.CopyProducer.SceneEffectsResolved = true;
    runtime.PicaTextureCache.push_back(copiedTexture);
    // Equal native cache keys can still name different immutable GPU versions.
    copiedTexture.CopyProducer.CompletionId = 42U;
    copiedTexture.CopyProducer.AfterDrawSubmissionId = 40U;
    copiedTexture.PixelBytes[0] = 0x99U;
    runtime.PicaTextureCache.push_back(copiedTexture);
    runtime.PicaColorTargetsAvailable = true;
    Oot3d::Renderer::PicaRenderTargetColorSnapshot target;
    target.ColorPhysicalAddress = 0x20000200U;
    target.DepthPhysicalAddress = 0x20000400U;
    target.FramebufferWidth = 2U;
    target.FramebufferHeight = 1U;
    target.FramebufferDepthFormat = 2U;
    target.ImageWidth = 2U;
    target.ImageHeight = 1U;
    target.SceneEffectsResolved = true;
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
    displayImage.SceneEffectsResolved = true;
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
    Require(io.FileBytes > 40U && io.PayloadBytes != 0U &&
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

    // A producer-only entry is judged by its producer schema: malformed
    // identity, layout or pixels must still be rejected.
    {
        auto malformed = runtime;
        malformed.PicaTextureCache[1].CopyProducer.CompletionId = 0U;
        std::string producerError;
        Require(!SaveNativeA32State(
                    root / "malformed-producer-id.oot3dsav", compatibility,
                    malformed, process, host, picaFrontend, submissionQueue,
                    dspHle, nullptr, &producerError),
                "savestate accepted a producer without a completion identity");
    }
    {
        auto malformed = runtime;
        malformed.PicaTextureCache[1].CopyProducer.Spans[0].SourceAddress =
            0x20000100U;
        std::string producerError;
        Require(!SaveNativeA32State(
                    root / "malformed-producer-layout.oot3dsav", compatibility,
                    malformed, process, host, picaFrontend, submissionQueue,
                    dspHle, nullptr, &producerError),
                "savestate accepted a producer span outside its source");
    }
    {
        auto malformed = runtime;
        malformed.PicaTextureCache[1].PixelBytes.resize(4U);
        std::string producerError;
        Require(!SaveNativeA32State(
                    root / "malformed-producer-pixels.oot3dsav", compatibility,
                    malformed, process, host, picaFrontend, submissionQueue,
                    dspHle, nullptr, &producerError),
                "savestate accepted a producer with incomplete pixels");
    }

    // The container is compressed (version 2); version 1 files still load and
    // a size that disagrees with the compressed frame is refused.
    {
        std::ifstream input(statePath, std::ios::binary);
        std::vector<uint8_t> file((std::istreambuf_iterator<char>(input)),
                                  std::istreambuf_iterator<char>());
        const auto readWord = [&](size_t offset, size_t count) {
            uint64_t value = 0U;
            for (size_t i = 0; i < count; ++i)
                value |= uint64_t{file[offset + i]} << (8U * i);
            return value;
        };
        const auto writeWord = [](std::vector<uint8_t>& bytes, size_t offset,
                                  size_t count, uint64_t value) {
            for (size_t i = 0; i < count; ++i)
                bytes[offset + i] = static_cast<uint8_t>(value >> (8U * i));
        };
        const auto fnv = [](const uint8_t* bytes, size_t size) {
            uint64_t hash = 14695981039346656037ULL;
            for (size_t i = 0; i < size; ++i)
                hash = (hash ^ bytes[i]) * 1099511628211ULL;
            return hash;
        };
        Require(file.size() > 40U && readWord(8U, 4U) == 2U &&
                    readWord(32U, 8U) == io.PayloadBytes &&
                    file.size() - 40U < io.PayloadBytes,
                "savestate payload was not compressed");
        std::vector<uint8_t> document(io.PayloadBytes);
        Require(ZSTD_decompress(document.data(), document.size(),
                                file.data() + 40U, file.size() - 40U) ==
                    document.size(),
                "savestate payload is not one Zstandard frame");

        std::vector<uint8_t> legacy(file.begin(), file.begin() + 40);
        writeWord(legacy, 8U, 4U, 1U);
        writeWord(legacy, 16U, 8U, document.size());
        writeWord(legacy, 24U, 8U, fnv(document.data(), document.size()));
        writeWord(legacy, 32U, 8U, 0U);
        legacy.insert(legacy.end(), document.begin(), document.end());
        const auto legacyPath = root / "uncompressed-v1.oot3dsav";
        std::ofstream(legacyPath, std::ios::binary)
            .write(reinterpret_cast<const char*>(legacy.data()),
                   static_cast<std::streamsize>(legacy.size()));

        auto wrongSize = file;
        writeWord(wrongSize, 32U, 8U, io.PayloadBytes + 1U);
        const auto wrongSizePath = root / "wrong-document-size.oot3dsav";
        std::ofstream(wrongSizePath, std::ios::binary)
            .write(reinterpret_cast<const char*>(wrongSize.data()),
                   static_cast<std::streamsize>(wrongSize.size()));

        Require(process.Memory().Write32(0x00100020U, 0xDEADBEEFU),
                "cannot mutate guest memory");
        NativeA32SavestateRuntimeState refused;
        std::string refusal;
        uint32_t word = 0U;
        Require(!LoadNativeA32State(wrongSizePath, compatibility, refused,
                                    process, host, picaFrontend,
                                    submissionQueue, dspHle, nullptr,
                                    &refusal) &&
                    process.Memory().Read32(0x00100020U, &word) &&
                    word == 0xDEADBEEFU,
                "savestate accepted a document size the frame contradicts");
        NativeA32SavestateRuntimeState legacyRuntime;
        Require(LoadNativeA32State(legacyPath, compatibility, legacyRuntime,
                                   process, host, picaFrontend,
                                   submissionQueue, dspHle, nullptr, &error),
                error);
        Require(process.Memory().Read32(0x00100020U, &word) &&
                    word == 0x12345678U && legacyRuntime.FrameCount == 321U,
                "uncompressed version 1 savestate did not restore");
    }

    Require(process.Memory().Write32(0x00100020U, 0xDEADBEEFU),
            "cannot mutate guest memory");
    runtime = {};
    const bool loaded = LoadNativeA32State(
                statePath, compatibility, runtime, process, host,
                picaFrontend, submissionQueue, dspHle, &io, &error);
    Require(loaded, error);
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
                runtime.TopScreenTemporalState.ProjectionAvailable &&
                runtime.TopScreenTemporalState.Projection.OffsetX == 280.0F &&
                runtime.TopScreenTemporalState.Projection.MapX ==
                    TopScreenPauseProjectionState::TrackedPosition{0x1000U, 0x42400000U, 0x43a40000U, true} &&
                runtime.TopScreenTemporalState.Projection.IconX[255] ==
                    TopScreenPauseProjectionState::TrackedPosition{0x1004U, 0x42000000U, 0x439c0000U, true} &&
                runtime.PicaVisualReplayStateAvailable &&
                runtime.PicaVisualReplayState ==
                    std::vector<uint8_t>({0x50U, 0x56U, 0x52U, 0x31U}) &&
                runtime.PicaTextureCacheAvailable &&
                runtime.PicaTextureCache.size() == 3U &&
                !runtime.PicaTextureCache[0].HasCopyProducer &&
                runtime.PicaTextureCache[1].HasCopyProducer &&
                runtime.PicaTextureCache[1].CopyProducer
                        .RenderTargetNamespace == 0U &&
                runtime.PicaTextureCache[1].CopyProducer.CompletionId == 41U &&
                runtime.PicaTextureCache[1].CopyProducer
                        .AfterDrawSubmissionId == 39U &&
                runtime.PicaTextureCache[1].CopyProducer
                        .SourcePhysicalAddress == 0x20000200U &&
                runtime.PicaTextureCache[1].CopyProducer.SourceWidth == 2U &&
                runtime.PicaTextureCache[1].CopyProducer.SourceHeight == 1U &&
                runtime.PicaTextureCache[1].CopyProducer
                        .SourceNativeFormat == 0U &&
                runtime.PicaTextureCache[1].CopyProducer.ColorEncoding == 1U &&
                runtime.PicaTextureCache[1].CopyProducer.SceneEffectsResolved &&
                runtime.PicaTextureCache[1].CopyProducer.Spans.size() == 1U &&
                runtime.PicaTextureCache[1].CopyProducer.Spans[0]
                        .DestinationAddress == 0x20000300U &&
                runtime.PicaTextureCache[1].CopyProducer.Spans[0].ByteCount ==
                    8U &&
                runtime.PicaTextureCache[1].PixelBytes ==
                    std::vector<uint8_t>({0x11U, 0x22U, 0x33U, 0x44U, 0x55U,
                                          0x66U, 0x77U, 0x88U}) &&
                runtime.PicaTextureCache[2].CopyProducer.CompletionId == 42U &&
                runtime.PicaTextureCache[2].CopyProducer.AfterDrawSubmissionId == 40U &&
                runtime.PicaTextureCache[2].CopyProducer.SceneEffectsResolved &&
                runtime.PicaTextureCache[2].PixelBytes[0] == 0x99U &&
                runtime.PicaTextureCache[1].ContentHash == 0U &&
                runtime.PicaTextureCache[1].PhysicalAddress == 0U &&
                runtime.PicaTextureCache[1].SourceWidth == 0U &&
                runtime.PicaTextureCache[1].SourceHeight == 0U &&
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
                runtime.PicaColorTargets[0].SceneEffectsResolved &&
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
                runtime.PicaPresentationState.DisplayImages[0].SceneEffectsResolved &&
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

    // A state holds the game moment at the guest extent: targets carry colour,
    // depth and stencil only, and the optional host settings travel beside it.
    {
        auto moment = runtime;
        auto& guestTarget = moment.PicaColorTargets[0];
        guestTarget.RenderScalePermille =
            Fast::Renderer3ds::kPicaGuestExtentRenderScalePermille;
        guestTarget.NormalGuideRgba8.clear();
        guestTarget.MaterialGuideRgba8.clear();
        guestTarget.RigidMotionGuideRgba16FloatLe.clear();
        guestTarget.AmbientGuideRgba8.clear();
        guestTarget.ShadowR32UintLe.clear();
        moment.PicaPresentationState.DisplayImages[0]
            .DepthTargetRenderScalePermille =
            Fast::Renderer3ds::kPicaGuestExtentRenderScalePermille;
        moment.HostSettingsAvailable = true;
        moment.HostSettingsJson = R"({"schema":1,"language":"it"})";
        const auto momentPath = root / "moment.oot3dsav";
        Require(SaveNativeA32State(momentPath, compatibility, moment, process,
                                   host, picaFrontend, submissionQueue, dspHle,
                                   nullptr, &error),
                error);
        NativeA32SavestateRuntimeState restored;
        Require(LoadNativeA32State(momentPath, compatibility, restored, process,
                                   host, picaFrontend, submissionQueue, dspHle,
                                   nullptr, &error),
                error);
        const auto& restoredTarget = restored.PicaColorTargets.at(0);
        Require(restoredTarget.RenderScalePermille == 0U &&
                    restoredTarget.NormalGuideRgba8.empty() &&
                    restoredTarget.ShadowR32UintLe.empty() &&
                    restoredTarget.ColorRgba8 == guestTarget.ColorRgba8 &&
                    restoredTarget.DepthValues == guestTarget.DepthValues &&
                    restored.HostSettingsAvailable &&
                    restored.HostSettingsJson ==
                        R"({"language":"it","schema":1})",
                "guest-extent state or host settings did not round-trip");

        auto withGuides = moment;
        withGuides.PicaColorTargets[0].NormalGuideRgba8.assign(8U, 0x80U);
        Require(!SaveNativeA32State(root / "guest-guides.oot3dsav",
                                    compatibility, withGuides, process, host,
                                    picaFrontend, submissionQueue, dspHle,
                                    nullptr, &error),
                "guest-extent target accepted enhancement guides");
        auto scaled = moment;
        scaled.PicaColorTargets[0].ImageWidth = 4U;
        scaled.PicaColorTargets[0].ColorRgba8.resize(16U);
        scaled.PicaColorTargets[0].DepthValues.resize(4U, 1.0F);
        scaled.PicaColorTargets[0].StencilValues.resize(4U);
        Require(!SaveNativeA32State(root / "guest-scaled.oot3dsav",
                                    compatibility, scaled, process, host,
                                    picaFrontend, submissionQueue, dspHle,
                                    nullptr, &error),
                "guest-extent target accepted a renderer extent");

        // The simulation rate is part of the moment: refused before any
        // guest state is touched.
        auto thirtyHz = compatibility;
        thirtyHz.GameplayTiming = GameplayTimingMode::Native30Interpolated;
        Require(process.Memory().Write32(0x00100020U, 0xCAFEF00DU),
                "cannot mutate guest memory");
        std::string rateError;
        Require(!LoadNativeA32State(momentPath, thirtyHz, restored, process,
                                    host, picaFrontend, submissionQueue,
                                    dspHle, nullptr, &rateError) &&
                    rateError.find("60 Hz") != std::string::npos,
                "a 60 Hz state loaded into a 30 Hz run");
        uint32_t untouched = 0;
        Require(process.Memory().Read32(0x00100020U, &untouched) &&
                    untouched == 0xCAFEF00DU,
                "a refused state changed guest memory");
        auto sixtyHz = compatibility;
        sixtyHz.GameplayTiming = GameplayTimingMode::Enhanced60;
        Require(LoadNativeA32State(momentPath, sixtyHz, restored, process,
                                   host, picaFrontend, submissionQueue, dspHle,
                                   nullptr, &error),
                error);
    }

    // Guest-extent resampling: area average down, bilinear up, nearest for
    // values that must not blend.
    {
        const std::vector<uint8_t> source{
            0U, 0U, 0U, 0U,   100U, 100U, 100U, 100U,
            200U, 0U, 0U, 255U, 0U, 200U, 0U, 255U};
        const auto down = Fast::Renderer3ds::ResamplePicaRgba8(source, 4U, 1U, 2U, 1U);
        Require(down == std::vector<uint8_t>({50U, 50U, 50U, 50U, 100U, 100U, 0U, 255U}),
                "area-average downsample is wrong");
        const std::vector<uint8_t> flat{10U, 20U, 30U, 40U};
        const auto up = Fast::Renderer3ds::ResamplePicaRgba8(flat, 1U, 1U, 3U, 2U);
        Require(up.size() == 24U && up[0] == 10U && up[23] == 40U,
                "bilinear upsample is wrong");
        const std::vector<float> depth{0.1F, 0.2F, 0.3F, 0.4F};
        const auto nearest =
            Fast::Renderer3ds::ResamplePicaNearest<float>(depth, 2U, 2U, 4U, 4U);
        Require(nearest.size() == 16U && nearest[0] == 0.1F && nearest[3] == 0.2F &&
                    nearest[12] == 0.3F && nearest[15] == 0.4F,
                "nearest resample is wrong");
        const auto shrink =
            Fast::Renderer3ds::ResamplePicaNearest<float>(depth, 2U, 2U, 1U, 1U);
        Require(shrink.size() == 1U, "nearest shrink is wrong");
    }

    // The default folder: next to the executable in the portable layout,
    // beside the save data where the executable sits in a read-only image.
    {
        const std::filesystem::path exe = "/opt/app", data = "/home/u/TriAevum/data";
        Require(DefaultSavestateDirectory(true, exe, data / "savedata") == exe / "savestates",
                "the portable layout does not keep states next to the executable");
        Require(DefaultSavestateDirectory(false, exe, data / "savedata") == data / "savestates" &&
                    DefaultSavestateDirectory(false, exe, data / "savedata" / "") == data / "savestates",
                "a read-only package does not keep states beside the save data");
        Require(DefaultSavestateDirectory(false, exe, {}) == exe / "savestates",
                "no save data folder does not fall back to the executable's");
    }

    // The library: default folder, timestamped names, last state and
    // preferences that survive a restart, newest-first listing, fixed slot.
    {
        const auto libraryRoot = root / "library";
        const auto preferences = libraryRoot / "config" / "savestates.json";
        const auto defaultFolder = libraryRoot / "exe" / "savestates";
        SavestateLibrary library(preferences, defaultFolder);
        Require(library.Directory() == defaultFolder &&
                    library.UsingDefaultDirectory() && !library.LastState(),
                "library does not start at the default folder");
        const auto now = std::chrono::system_clock::now();
        const auto plain = library.NewStatePath(false, now);
        const auto withSettings = library.NewStatePath(true, now);
        Require(plain.parent_path() == defaultFolder &&
                    plain.extension() == ".oot3dsav" &&
                    !SavestateLibrary::NameCarriesSettings(plain) &&
                    SavestateLibrary::NameCarriesSettings(withSettings),
                "state names do not record their kind");
        std::filesystem::create_directories(defaultFolder);
        { std::ofstream(plain, std::ios::binary) << "a"; }
        Require(library.NewStatePath(false, now) != plain,
                "a new state would overwrite an existing file");
        { std::ofstream(withSettings, std::ios::binary) << "bb"; }
        std::filesystem::last_write_time(
            withSettings, std::filesystem::last_write_time(plain) + std::chrono::seconds(5));
        const auto entries = library.List(&error);
        Require(entries.size() == 2U && entries[0].Path == withSettings &&
                    entries[0].WithSettings && entries[1].Bytes == 1U,
                "listing is not newest first");
        const auto chosen = libraryRoot / "chosen";
        Require(library.SetDirectory(chosen, &error), error);
        library.SetLastState(plain);
        Require(library.PreferencesError().empty(), library.PreferencesError());
        SavestateLibrary reopened(preferences, defaultFolder);
        Require(reopened.Directory() == chosen && !reopened.UsingDefaultDirectory() &&
                    reopened.LastState() == plain,
                "folder or last state was not remembered");
        Require(reopened.SetDirectory(defaultFolder, &error) &&
                    reopened.UsingDefaultDirectory(),
                "choosing the default folder did not restore the default");
        reopened.Request({SavestateRequestKind::Load, true, {}});
        reopened.Request({SavestateRequestKind::Save, false, {}});
        const auto request = reopened.TakeRequest();
        Require(request && request->Kind == SavestateRequestKind::Save &&
                    !reopened.TakeRequest(),
                "the latest request must replace an unserviced one");
        const auto slot = libraryRoot / "scripted" / "quick.oot3dsav";
        SavestateLibrary fixed(preferences, defaultFolder, slot);
        Require(fixed.NewStatePath(true) == slot && fixed.LastState() == slot &&
                    !fixed.SetDirectory(chosen, &error),
                "--quick-state does not pin one file");
    }

    std::filesystem::remove_all(root, cleanupError);
    std::cout << "oot3d_native_a32_savestate_tests: ok\n";
    return 0;
}
