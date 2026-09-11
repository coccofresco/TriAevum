#include "oot3d_demo_host_screenshot.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <nlohmann/json.hpp>
#include "fast/renderer3ds/pica_frame_timing.h"
#include "fast/oot3d/grass_render_telemetry.h"

#include "oot3d_demo_host_io.h"

bool ShouldWriteFramebufferScreenshot(const Args& args, uint32_t frameCount,
                                      const FramebufferScreenshotState& state) {
    if (args.ScreenshotPath.empty() || frameCount < args.ScreenshotStartFrame ||
        (!args.ScreenshotSequence && state.Written)) {
        return false;
    }
    if (args.ScreenshotSequence &&
        (frameCount - args.ScreenshotStartFrame) % args.ScreenshotSequenceInterval != 0) {
        return false;
    }
    return true;
}

void MaybeWriteFramebufferScreenshot(const Args& args, Fast::GfxRenderingAPI& api, uint32_t width, uint32_t height,
                                     uint32_t frameCount, FramebufferScreenshotState& state,
                                     const Fast::Renderer3ds::PicaFrameTemporalSample* temporalSample) {
    const bool grassDiagnostics = std::getenv("OOT3D_SCREENSHOT_GRASS_TRANSITIONS") != nullptr &&
                                  args.ScreenshotSequence && !args.ScreenshotPath.empty();
    Fast::Oot3d::GrassRenderTelemetrySnapshot grass;
    bool grassTransition = false;
    bool cameraTransition = false;
    if (std::getenv("OOT3D_SCREENSHOT_CAMERA_TRANSITIONS") && temporalSample && temporalSample->Available() &&
        args.ScreenshotSequence && !args.ScreenshotPath.empty()) {
        if (state.LastTemporalDiagnosticEpoch != temporalSample->ContinuityEpoch) {
            state.TemporalDiagnosticFrames = 3;
            state.LastTemporalDiagnosticEpoch = temporalSample->ContinuityEpoch;
        }
        cameraTransition = state.TemporalDiagnosticFrames != 0;
        if (cameraTransition) --state.TemporalDiagnosticFrames;
    }
    if (grassDiagnostics) {
        grass = Fast::Oot3d::GrassRenderTelemetry::Instance().Snapshot();
        const auto status = static_cast<int>(grass.Status);
        grassTransition = grass.FrameId != 0U && status != state.LastGrassDiagnosticStatus;
        state.LastGrassDiagnosticStatus = status;
    }
    if (!grassTransition && !cameraTransition && !ShouldWriteFramebufferScreenshot(args, frameCount, state)) {
        return;
    }

    std::vector<uint16_t> framebuffer(static_cast<size_t>(width) * height, 0);
    if (!grassDiagnostics) grass = Fast::Oot3d::GrassRenderTelemetry::Instance().Snapshot();
    api.ReadFramebufferToCPU(0, width, height, framebuffer.data());
    const auto screenshotPath =
        args.ScreenshotSequence ? ScreenshotSequenceFramePath(args.ScreenshotPath, frameCount) : args.ScreenshotPath;
    WriteBmpFromRgba5551Framebuffer(screenshotPath, width, height, framebuffer);
    nlohmann::json metadata = {
        {"format", "triaevum_framebuffer_sample_v1"}, {"host_frame", frameCount},
        {"width", width}, {"height", height}, {"temporal_sample", nullptr},
        {"scope", "synchronous_capture_not_performance_measurement"}};
    if (temporalSample && temporalSample->Available()) {
        const auto& sample = *temporalSample;
        metadata["temporal_sample"] = {
            {"previous_source", sample.PreviousSourceFrameId},
            {"current_source", sample.CurrentSourceFrameId},
            {"continuity_epoch", sample.ContinuityEpoch},
            {"multiplier", sample.FixedSampleMultiplier},
            {"ordinal", sample.SampleOrdinal}, {"alpha", sample.Alpha},
            {"synthetic", sample.Synthetic}, {"history_reset", sample.HistoryReset}};
    }
    if (grass.FrameId != 0U) {
        metadata["grass"] = {{"frame", grass.FrameId}, {"status", Fast::Oot3d::GrassRenderStatusName(grass.Status)},
            {"transition", grassTransition}, {"visible_blades", grass.VisibleBlades},
            {"cluster_instances", grass.ClusterDrawInstances}, {"cluster_blades", grass.ClusterRepresentedBlades},
            {"candidate_clusters", grass.CandidateClusters}, {"tested_nodes", grass.VisibilityNodesTested},
            {"cpu_ms", grass.CpuMilliseconds}, {"selection_ms", grass.SelectionMilliseconds},
            {"placement_ms", grass.PlacementMilliseconds}, {"upload_ms", grass.UploadMilliseconds},
            {"static_upload_bytes", grass.StaticUploadedBytes}, {"dynamic_upload_bytes", grass.DynamicUploadedBytes},
            {"gpu_compaction", grass.GpuCompaction}};
    }
    auto metadataPath = screenshotPath;
    metadataPath += ".json";
    WriteJsonFile(metadataPath, metadata);
    state.Written = true;
    ++state.WrittenCount;
    state.Width = width;
    state.Height = height;
}
