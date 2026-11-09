#pragma once

// Run statistics of the A32 window (RunOot3dNativeA32Window), grouped by subsystem.

#include "fast/renderer/frame_time_distribution.h"
#include "fast/renderer/slow_frame_samples.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace Oot3dA32Window {

// Savestate saves and loads: counts, bytes, phase times and the legacy-format restores.
struct NativeA32SavestateStats {
    uint64_t SaveCount = 0;
    uint64_t SaveFailureCount = 0;
    // Save requests retried because a guest continuation was in flight.
    uint64_t SaveDeferralCount = 0;
    uint64_t LoadCount = 0;
    uint64_t SaveBytes = 0;
    uint64_t LoadBytes = 0;
    double SaveSeconds = 0.0;
    double LoadSeconds = 0.0;
    double CaptureSeconds = 0.0;
    double EncodeSeconds = 0.0;
    double WriteSeconds = 0.0;
    double ReadSeconds = 0.0;
    double DecodeSeconds = 0.0;
    double RestoreSeconds = 0.0;
    uint64_t LegacyClockRestoreCount = 0;
    uint64_t LegacyPresentationSchedulerRestoreCount = 0;
    uint64_t LegacyFrameRatePolicyRestoreCount = 0;
    uint64_t LegacyTopScreenTemporalRestoreCount = 0;
    uint64_t LegacyPicaVisualReplayRestoreCount = 0;
    uint64_t LegacyPicaTextureCacheRestoreCount = 0;
    uint64_t LegacyPicaColorTargetRestoreCount = 0;
    uint64_t LegacyPicaPresentationRestoreCount = 0;
    uint64_t PicaTexturesCaptured = 0;
    uint64_t PicaTextureBytesCaptured = 0;
    uint64_t PicaTexturesRestored = 0;
    uint64_t PicaTextureBytesRestored = 0;
    uint64_t PicaColorTargetsCaptured = 0;
    uint64_t PicaColorTargetBytesCaptured = 0;
    uint64_t PicaColorTargetsRestored = 0;
    uint64_t PicaColorTargetBytesRestored = 0;
    uint64_t PicaDisplayImagesCaptured = 0;
    uint64_t PicaDisplayImageBytesCaptured = 0;
    uint64_t PicaDisplayImagesRestored = 0;
    uint64_t PicaDisplayImageBytesRestored = 0;
    uint64_t PicaVisualReplayBytesCaptured = 0;
    uint64_t PicaVisualReplayBytesRestored = 0;
    uint64_t LastSemanticFingerprint = 0;
};

// PICA submissions drained per refresh: draws, display transfers, completions and native frontend presentation.
struct NativeA32PicaSubmissionStats {
    uint64_t DrawCount = 0;
    uint64_t RefreshesWithNativeDraws = 0;
    uint64_t DisplayTransferCount = 0;
    uint64_t RecordedCompletionCount = 0;
    uint64_t CompletedP3dCount = 0;
    uint64_t CompletedPpfCount = 0;
    uint64_t PicaDrainPassCount = 0;
    uint64_t MaximumPicaDrainPassesPerRefresh = 0;
    uint64_t SelectedTopTransferCount = 0;
    uint64_t NativeBottomFrontendPresentationCount = 0;
    uint64_t NativeMenuBackdropDrawOmissionCount = 0;
    uint64_t NativeMenuClearDrawOmissionCount = 0;
    uint64_t NativeFrontendPresentationActiveCount = 0;
    uint64_t NativeFrontendBottomTransferHitCount = 0;
};

// Visual interpolation: snapshots, transitions, draw matching and the samples presented.
struct NativeA32VisualStats {
    uint64_t SnapshotCount = 0;
    uint64_t TransitionCount = 0;
    uint64_t MatchedDrawCount = 0;
    uint64_t StrictUniqueMatchedDrawCount = 0;
    uint64_t StrictOrdinalMatchedDrawCount = 0;
    uint64_t StructuralOrdinalMatchedDrawCount = 0;
    uint64_t PipelineOrdinalMatchedDrawCount = 0;
    uint64_t ChangedContinuousDrawCount = 0;
    uint64_t ChangedPerInstanceVertexDrawCount = 0;
    uint64_t ChangedPerVertexDrawCount = 0;
    uint64_t AmbiguousPreviousDrawCount = 0;
    uint64_t AmbiguousCurrentDrawCount = 0;
    uint64_t UnmatchedPreviousDrawCount = 0;
    uint64_t UnmatchedCurrentDrawCount = 0;
    uint64_t SamplesPresented = 0;
    uint64_t DirectFramesPresented = 0;
    uint64_t ReusedSnapshotPresentations = 0;
    uint64_t DiscontinuityCount = 0;
    uint64_t NamespaceResyncCount = 0;
    uint64_t SampleDrawCount = 0;
    uint64_t InterpolatedDrawCount = 0;
    uint64_t InterpolatedPerInstanceVertexDrawCount = 0;
    uint64_t InterpolatedPerVertexDrawCount = 0;
};

// DSP output mixed by the host: frames, samples, PCM hash and peak.
struct NativeA32DspMixStats {
    uint64_t FramesMixed = 0;
    uint64_t FramesSuppressed = 0;
    uint64_t SamplesMixed = 0;
    uint64_t NonzeroSamples = 0;
    uint64_t PcmFnv1a64 = 14695981039346656037ULL;
    int32_t PeakMagnitude = 0;
};

// The measured benchmark window: its bounds, phase times and frame-time distributions.
// PERF profile: where the game thread waits for the render worker, and the
// work of the frame with the longest pre-backend time. Reported only; the
// frame loop does nothing differently.
inline constexpr size_t kNativeA32FrameWaitValues = 17U;
inline constexpr const char* kNativeA32FrameWaitNames[kNativeA32FrameWaitValues] = {
    // Seconds.
    "frames_in_flight_wait_seconds",    // admitted frame: WaitForFramesInFlight(0)
    "frame_start_drain_seconds",        // synchronous frame begin: worker drained before StartFrame
    "renderer_start_frame_seconds",     // api.StartFrame() itself, on whichever thread ran it
    "refresh_flush_drain_seconds",      // dependency flush before a guest refresh: drain
    "refresh_flush_execute_seconds",    // ... and its execution on the game thread
    "submit_flush_drain_seconds",       // dependency flush inside the PICA submit loop: drain
    "submit_flush_execute_seconds",     // ... and its execution
    "frame_end_back_pressure_seconds",  // Submit of the frame end with a frame still in flight
    "stream_drain_seconds",             // every drain of the stream (includes the three above)
    "stream_run_command_seconds",       // every synchronous command (includes the flush executions)
    // Counts.
    "frames_in_flight_waits", "synchronous_frame_starts", "renderer_start_frames", "refresh_flushes",
    "submit_flushes", "frame_end_submits", "stream_drains"};
using NativeA32FrameWaits = std::array<double, kNativeA32FrameWaitValues>;

struct NativeA32LongestFrame {
    bool Valid = false;
    uint64_t RunFrame = 0;
    // The frame's end, in seconds after the measurement start.
    double WindowSeconds = 0.0;
    double PreBackendSeconds = 0.0;
    double EnvelopeSeconds = 0.0;
    double FrameSeconds = 0.0;
    double NestedGuestSeconds = 0.0;
    double GuestClockSeconds = 0.0;
    std::array<double, 15> PhaseSeconds{};
    NativeA32FrameWaits Waits{};
    uint64_t GuestRefreshes = 0;
    // The guest's work in the frame (--profile-a32-runtime only).
    bool Profiled = false;
    uint64_t ProcessRunCalls = 0;
    uint64_t DispatchCalls = 0;
    uint64_t DispatchNanoseconds = 0;
    std::array<uint64_t, 16> DispatchCallsByThread{};
    std::array<uint64_t, 16> DispatchNanosecondsByThread{};
    uint64_t ClockDispatchCalls = 0;
    uint64_t ClockDispatchNanoseconds = 0;
    uint64_t SvcCalls = 0;
    uint64_t SvcNanoseconds = 0;
    std::array<uint64_t, 256> SvcCallsByImmediate{};
    std::array<uint64_t, 256> SvcNanosecondsByImmediate{};
    uint64_t PatchExitRequests = 0;
    uint64_t WholeAotBlocksConsumed = 0;
    uint64_t RomFsStreamOpenCalls = 0;
    uint64_t RomFsReadCalls = 0;
    uint64_t RomFsReadBytes = 0;
    uint64_t RomFsReadNanoseconds = 0;
    uint64_t GspTriggerCalls = 0;
    uint64_t GspTriggerNanoseconds = 0;
    uint64_t GspCommandPackets = 0;
};

struct NativeA32BenchmarkWindow {
    std::chrono::steady_clock::time_point MeasurementStart = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point MeasurementEnd = MeasurementStart;
    uint64_t MeasuredFrames = 0U;
    std::array<double, 15> PhaseSeconds{};
    double NestedGuestSeconds = 0.0;
    double GuestClockSeconds = 0.0;
    double UnattributedSeconds = 0.0;
    Fast::Renderer::FrameTimeDistribution PreBackendTimes;
    Fast::Renderer::FrameTimeDistribution PreBackendEnvelopeTimes;
    uint64_t InvalidPreBackendIntervals = 0;
    Fast::Renderer::SlowFrameSamples<15> SlowFrames;
    NativeA32FrameWaits Waits{};
    NativeA32LongestFrame LongestPreBackendFrame;
};

} // namespace Oot3dA32Window
