#include "fast/oot3d/pica_toon_telemetry.h"

#include <numeric>

namespace Fast::Oot3d {

uint64_t PicaToonTelemetrySnapshot::Total() const {
    return std::accumulate(ByEligibility.begin(), ByEligibility.end(), 0ULL);
}

PicaToonTelemetry& PicaToonTelemetry::Instance() {
    static PicaToonTelemetry telemetry;
    return telemetry;
}

uint64_t PicaToonTelemetry::Record(PicaToonEligibility eligibility) {
    mCounts[static_cast<size_t>(eligibility)].fetch_add(1U, std::memory_order_relaxed);
    return mTotal.fetch_add(1U, std::memory_order_relaxed) + 1U;
}

PicaToonTelemetrySnapshot PicaToonTelemetry::Snapshot() const {
    PicaToonTelemetrySnapshot result;
    for (size_t index = 0; index < mCounts.size(); ++index) {
        result.ByEligibility[index] = mCounts[index].load(std::memory_order_relaxed);
    }
    return result;
}

} // namespace Fast::Oot3d
