#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Launch arguments of the application host (window, renderer, audio, capture,
// timing and qualification inputs).
struct Args {
    std::string ApplicationName = "OOT3D Native Fast3D Demo";
    std::string ApplicationId = "oot3d_native_fast3d_demo";
    std::string ConfigurationPath = "oot3d_native_fast3d_demo.json";
    // The log folder (--log-directory, D/logs); empty keeps logs/ in the
    // application directory. Routed before the logger starts.
    std::filesystem::path LogDirectory;
    std::filesystem::path ResourceRoot;
    std::vector<std::filesystem::path> ResourceArchives;
    std::filesystem::path OutputPath;
    std::filesystem::path ScreenshotPath;
    bool ScreenshotSequence = false;
    // Deterministic screenshots at full 8-bit precision instead of the
    // RGBA5551 baseline (for comparisons that must not band gradients).
    bool ScreenshotRgb8 = false;
    uint32_t ScreenshotStartFrame = 0;
    uint32_t ScreenshotSequenceInterval = 1;
    uint32_t FrameLimit = 0;
    uint32_t BenchmarkWarmupFrames = 0;
    // Runs a bounded, full-workload throughput measurement. The native-game
    // bootstrap supplies one complete simulation step per host frame while
    // the host disables VSync and its SDL software frame limiter.
    bool ThroughputBenchmark = false;
    double MaxSeconds = 0.0;
    double FixedDeltaSeconds = 0.0;
    std::filesystem::path InputTimelinePath;
    // Developer qualification: oot3d_graphics_settings_schedule.h.
    std::filesystem::path GraphicsSettingsSchedulePath;
    uint32_t Width = 1280;
    uint32_t Height = 720;
    uint32_t AudioSampleRate = 44100;
    uint32_t AudioSampleLength = 1024;
    int32_t AudioDesiredBuffered = 2480;
    int32_t BackendId = -1;
    std::string Renderer = "nri";

    // A scripted session (captures, fixed steps, bounded or replayed runs,
    // benchmarks, scheduled settings): what the host adds for a person at the
    // screen (the default window fit, the first-play help card) stays out, so
    // such runs keep their stored window size and reproduce frame for frame.
    [[nodiscard]] bool Scripted() const noexcept {
        return !ScreenshotPath.empty() || FrameLimit != 0U || ThroughputBenchmark || MaxSeconds > 0.0 ||
               FixedDeltaSeconds > 0.0 || !InputTimelinePath.empty() || !GraphicsSettingsSchedulePath.empty();
    }
};
