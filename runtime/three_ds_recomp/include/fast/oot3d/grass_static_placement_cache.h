#pragma once

#include "fast/oot3d/grass_async_placement_builder.h"
#include <span>
#include <unordered_map>

namespace Fast::Oot3d {

// Camera-independent scene distribution. Visibility and per-frame draw budgets
// do not invalidate it; inactive rooms remain in the byte-bounded worker cache.
class GrassStaticPlacementCache final {
  public:
    void SetCandidateCapacity(uint32_t capacity) {
        mCandidateCapacity = capacity;
    }
    [[nodiscard]] std::vector<GrassAsyncPlacementResult> Resolve(
        std::span<const GrassAsyncPlacementRequest> requests, bool waitUntilReady = true);
    [[nodiscard]] GrassAsyncPlacementStats Stats() const {
        return mBuilder.Stats();
    }
    void Clear();
    void WaitForIdle() {
        mBuilder.WaitForIdle();
    }

  private:
    struct Demand {
        double Candidates = 0.0;
        uint64_t LastFrame = 0;
    };
    uint32_t mCandidateCapacity = 8U * 1024U * 1024U;
    GrassAsyncPlacementBuilder mBuilder{ 32U, 2U, 768U * 1024U * 1024U };
    std::unordered_map<uint64_t, Demand> mDemands;
};

[[nodiscard]] double MeasureGrassSurfaceCandidates(const GrassAsyncPlacementRequest& request);

} // namespace Fast::Oot3d
