#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace Fast::Oot3d {

// Budget thinning. When more roots are visible than the per-room budget, the
// selection keeps the roots with the lowest thinning key: the root's stable
// visibility over the LOD retention at its distance (at most 1 for a visible
// root). Every cluster and every distance band keeps the same fraction, so
// the budget never empties a direction, and each root's admission depends on
// its own key alone, so moving the camera only flips the roots at the cut.
// Keys are quantized into buckets; the cut is the first bucket whose
// cumulative count would exceed the budget. The admitted total never exceeds
// the budget and uses it up to one bucket. The CPU selection and the GPU
// selection (grass_shader_sources.cpp) apply this identically.
inline constexpr uint32_t kGrassBudgetBuckets = 1024U;
// Cut value admitting every bucket: no thinning.
inline constexpr uint32_t kGrassBudgetCutAll = kGrassBudgetBuckets;
using GrassBudgetHistogram = std::array<uint32_t, kGrassBudgetBuckets>;

[[nodiscard]] inline uint32_t GrassBudgetBucket(float stableVisibility, float retention) noexcept {
    if (!(retention > 0.0F)) return 0U;
    const float scaled = stableVisibility / retention * static_cast<float>(kGrassBudgetBuckets);
    if (!(scaled > 0.0F)) return 0U;
    return std::min(kGrassBudgetBuckets - 1U, static_cast<uint32_t>(scaled));
}

[[nodiscard]] inline uint32_t GrassBudgetCut(const GrassBudgetHistogram& histogram, uint32_t budget) noexcept {
    uint64_t running = 0U;
    for (uint32_t bucket = 0U; bucket < kGrassBudgetBuckets; ++bucket) {
        if (running + histogram[bucket] > budget) return bucket;
        running += histogram[bucket];
    }
    return kGrassBudgetCutAll;
}

[[nodiscard]] constexpr bool GrassBudgetAdmits(uint32_t bucket, uint32_t cut) noexcept { return bucket < cut; }

} // namespace Fast::Oot3d
