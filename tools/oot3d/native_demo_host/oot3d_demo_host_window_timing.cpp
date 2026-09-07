#include "oot3d_demo_host_window_timing.h"

#include <algorithm>
#include <chrono>
#include <thread>

bool PrepareNextWindowDemoFrame(const Args& args, Fast::Fast3dWindow& window, WindowDemoTimingState& timing,
                                WindowDemoFrameTiming& frameTiming) {
    window.HandleEvents();
    if (args.MaxSeconds > 0.0 &&
        std::chrono::duration<double>(std::chrono::steady_clock::now() - timing.StartTime).count() >=
            args.MaxSeconds) {
        window.Close();
        return false;
    }
    if (!window.IsFrameReady()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return false;
    }

    if (args.FixedDeltaSeconds > 0.0) {
        timing.FixedElapsedSeconds += args.FixedDeltaSeconds;
        frameTiming.Now = timing.StartTime + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                std::chrono::duration<double>(timing.FixedElapsedSeconds));
        frameTiming.DeltaSeconds = args.FixedDeltaSeconds;
    } else {
        frameTiming.Now = std::chrono::steady_clock::now();
        frameTiming.DeltaSeconds =
            std::clamp(std::chrono::duration<double>(frameTiming.Now - timing.LastTime).count(), 0.0, 0.05);
    }
    timing.LastTime = frameTiming.Now;
    return true;
}

void ApplyWindowDemoFrameLimit(const Args& args, Fast::Fast3dWindow& window, uint32_t frameCount) {
    if (args.FrameLimit > 0 && frameCount >= args.FrameLimit) {
        window.Close();
    }
}
