#pragma once

#include <chrono>
#include <cstdint>

#include "fast/Fast3dWindow.h"
#include "oot3d_demo_host_types.h"

struct WindowDemoFrameTiming {
    std::chrono::steady_clock::time_point Now{};
    double DeltaSeconds = 0.0;
};

struct WindowDemoTimingState {
    std::chrono::steady_clock::time_point StartTime = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point LastTime = StartTime;
    double FixedElapsedSeconds = 0.0;
};

bool PrepareNextWindowDemoFrame(const Args& args, Fast::Fast3dWindow& window, WindowDemoTimingState& timing,
                                WindowDemoFrameTiming& frameTiming);
void ApplyWindowDemoFrameLimit(const Args& args, Fast::Fast3dWindow& window, uint32_t frameCount);
