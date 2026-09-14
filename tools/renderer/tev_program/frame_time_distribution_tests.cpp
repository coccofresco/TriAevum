#include "fast/renderer/frame_time_distribution.h"
#include <iostream>
#include <limits>
#include <stdexcept>

using Fast::Renderer::FrameTimeDistribution;
void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
int main() try {
    FrameTimeDistribution times;
    Check(!times.QuantileUpperMs(0.95), "empty quantile");
    times.RecordMilliseconds(-1.0);
    times.RecordMilliseconds(std::numeric_limits<double>::infinity());
    times.RecordMilliseconds(std::numeric_limits<double>::quiet_NaN());
    Check(times.Invalid() == 3 && times.Count() == 0, "invalid samples counted");
    for (unsigned i = 0; i < 95; ++i) times.RecordMilliseconds(10.01);
    for (unsigned i = 0; i < 4; ++i) times.RecordMilliseconds(20.01);
    times.RecordMilliseconds(2100.0);
    Check(times.Count() == 100 && times.Overflow() == 1, "sample/overflow count");
    Check(times.QuantileUpperMs(0.95) == 10.125, "p95 bucket");
    Check(times.QuantileUpperMs(0.99) == 20.125, "p99 bucket");
    Check(times.QuantileUpperMs(1.0) == 2100.0, "overflow must not clip a long stall");
    Check(times.Over60Hz() == 5 && times.Over30Hz() == 1, "frame budgets");
    Check(!times.QuantileUpperMs(-0.1) && !times.QuantileUpperMs(1.1), "invalid quantile");
    FrameTimeDistribution boundary;
    boundary.RecordMilliseconds(0.0);
    boundary.RecordMilliseconds(FrameTimeDistribution::RangeMs);
    Check(boundary.Overflow() == 1 && boundary.QuantileUpperMs(0.5) == 0.125,
          "zero/boundary samples");
    std::cout << "bounded frame-time distribution verified\n";
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
