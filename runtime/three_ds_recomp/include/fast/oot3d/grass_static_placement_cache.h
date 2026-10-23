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
    [[nodiscard]] std::vector<GrassAsyncPlacementResult> Resolve(std::span<const GrassAsyncPlacementRequest> requests);
    [[nodiscard]] GrassAsyncPlacementStats Stats() const {
        return mBuilder.Stats();
    }
    void SetCacheDirectory(std::filesystem::path directory) {
        mBuilder.SetCacheDirectory(std::move(directory));
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
    // The placement each source last showed. When only its overlays or its
    // shadow grid change (a mask edit, a decal or the key light's shadows
    // appearing at dusk) it stays on screen while the new one builds,
    // instead of the frame waiting for the rebuild.
    struct Shown {
        uint64_t Base = 0;
        std::shared_ptr<const GrassWorldPlacement> Placement;
        double BuildMilliseconds = 0.0;
        uint64_t LastFrame = 0;
    };
    uint32_t mCandidateCapacity = 8U * 1024U * 1024U;
    GrassAsyncPlacementBuilder mBuilder{ 32U, 2U, 768U * 1024U * 1024U };
    std::unordered_map<uint64_t, Demand> mDemands;
    std::unordered_map<uint64_t, Shown> mShown;
};

[[nodiscard]] double MeasureGrassSurfaceCandidates(const GrassAsyncPlacementRequest& request);

} // namespace Fast::Oot3d
