#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "oot3d/renderer/pica_render_backend.h"
#include "oot3d_native_frame_rate.h"

namespace Oot3dNativeGame {

class NativeA32CtrHostServices;
class NativeA32DspHle;
class NativeA32Process;
class Oot3dNativePicaFrontend;
class Oot3dNativePicaSubmissionQueue;

inline constexpr const char* NativeA32SavestateRuntimeAbi =
    "oot3d-native-a32-state-v1";

struct NativeA32SavestateCompatibility {
    std::string CodeBinSha256;
    std::string ProcessName;
    std::string A32SourceSnapshotId;
};

// Host-side TopScreen routing survives across guest refreshes even though it
// is not part of the game's save data. Savestates must preserve this small
// temporal contract or a resumed process can route the first pause draws to a
// different target while reconstructing the latch from guest observations.
struct NativeTopScreenTemporalState {
    bool ProfileActive = false;
    bool PausePageRedrawActive = false;
    uint16_t PausePageRedrawDelayCalls = 0;
    bool PauseDrawNativeTransitionLatched = false;
    bool PauseDrawSuppressionDelayArmed = false;
    uint16_t PauseDrawSuppressionDelayCommands = 0;
    uint32_t PauseRoutePreviousRuntimeMode = 0;
    uint8_t PauseRouteTransitionPhase = 0;
    uint16_t PauseRouteRemainingCalls = 0;
    uint32_t AotPauseRoutePreviousRuntimeMode = 0;
    uint8_t AotPauseRouteTransitionPhase = 0;
    uint16_t AotPauseRouteRemainingCalls = 0;
    bool TouchCoordinateRuntimeSceneLatch = false;
};

struct NativeA32SavestateRuntimeState {
    uint32_t FrameCount = 0;
    uint64_t RefreshTickRemainder = 0;
    uint64_t NextVblankTick = 0;
    uint32_t ProcessRunKind = 0;
    bool GameplayClockAvailable = false;
    GameplayTimingMode GameplayTiming =
        GameplayTimingMode::Native30Interpolated;
    NativeGameplayClockState GameplayClock;
    bool PresentationSchedulerAvailable = false;
    NativePresentationSchedulerState PresentationScheduler;
    bool FrameRatePolicyTemporalStateAvailable = false;
    NativeFrameRatePolicyTemporalState FrameRatePolicyTemporalState;
    bool TopScreenTemporalStateAvailable = false;
    NativeTopScreenTemporalState TopScreenTemporalState;
    bool PicaVisualReplayStateAvailable = false;
    std::vector<uint8_t> PicaVisualReplayState;
    bool PicaTextureCacheAvailable = false;
    std::vector<Oot3d::Renderer::PicaTextureCacheEntrySnapshot>
        PicaTextureCache;
    bool PicaColorTargetsAvailable = false;
    std::vector<Oot3d::Renderer::PicaRenderTargetColorSnapshot>
        PicaColorTargets;
    bool PicaPresentationStateAvailable = false;
    Oot3d::Renderer::PicaPresentationStateSnapshot
        PicaPresentationState;
};

struct NativeA32SavestateIoResult {
    uint64_t FileBytes = 0;
    uint64_t PayloadBytes = 0;
    uint64_t SemanticFingerprint = 0;
    double CaptureSeconds = 0.0;
    double EncodeSeconds = 0.0;
    double IoSeconds = 0.0;
    double DecodeSeconds = 0.0;
    double RestoreSeconds = 0.0;
};

// The command-line capture frame is zero-based and relative to the current
// host run. Guest FrameCount survives a reload and therefore must never be
// used to decide when this process has reached the requested capture point.
inline constexpr bool ShouldCaptureNativeA32AutomatedState(
    bool frameAvailable, bool alreadyCaptured,
    uint64_t completedRunFrames, uint32_t requestedRunFrame) noexcept {
    return frameAvailable && !alreadyCaptured &&
           completedRunFrames > static_cast<uint64_t>(requestedRunFrame);
}

bool SaveNativeA32State(
    const std::filesystem::path& path,
    const NativeA32SavestateCompatibility& compatibility,
    const NativeA32SavestateRuntimeState& runtime,
    const NativeA32Process& process,
    const NativeA32CtrHostServices& host,
    const Oot3dNativePicaFrontend& picaFrontend,
    const Oot3dNativePicaSubmissionQueue& submissionQueue,
    const NativeA32DspHle& dspHle,
    NativeA32SavestateIoResult* result = nullptr,
    std::string* error = nullptr);

bool LoadNativeA32State(
    const std::filesystem::path& path,
    const NativeA32SavestateCompatibility& compatibility,
    NativeA32SavestateRuntimeState& runtime,
    NativeA32Process& process,
    NativeA32CtrHostServices& host,
    Oot3dNativePicaFrontend& picaFrontend,
    Oot3dNativePicaSubmissionQueue& submissionQueue,
    NativeA32DspHle& dspHle,
    NativeA32SavestateIoResult* result = nullptr,
    std::string* error = nullptr);

} // namespace Oot3dNativeGame
