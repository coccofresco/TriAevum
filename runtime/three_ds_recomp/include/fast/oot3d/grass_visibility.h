#pragma once

#include "fast/oot3d/graphics_settings.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Fast::Oot3d {

struct GrassWorldPlacement;
struct GrassLodPolicy;

struct GrassBudgetDemand {
    uint32_t Maximum = 0U;
    double Weight = 0.0;
};

// Capped weighted allocation; sums never exceed the shared budget. Enumeration
// and integer rounding are deterministic, including when near patches saturate.
[[nodiscard]] std::vector<uint32_t> AllocateGrassBudget(
    const std::vector<GrassBudgetDemand>& demands, uint32_t budget);

// Recover the eye in the same coordinate system and temporal sample as the
// rendered geometry. Independent of depth range, handedness and axis flips.
[[nodiscard]] std::optional<std::array<float, 3>> GrassCameraFromProjection(
    const std::array<float, 16>& positionToClip) noexcept;

struct GrassClusterSelectionStats {
    uint32_t TestedNodes = 0U;
    uint32_t CandidateClusters = 0U;
    uint64_t CandidateAnchors = 0U;
};

[[nodiscard]] GrassClusterSelectionStats SelectGrassVisibleClusters(
    const GrassWorldPlacement& placement, const std::array<float, 16>& positionToClip,
    const std::array<float, 3>& eye, float drawDistance, float bladeRadiusScale,
    bool frustumCulling, std::vector<uint32_t>& clusterIndices, const GrassLodPolicy* lodPolicy = nullptr);

struct GrassLodDecision {
    bool Visible = false;
    uint8_t BladeSegments = 1U;
    uint8_t PlaneCount = 2U;
    float Retention = 1.0F;
    bool DistantTuft = false;
};

struct GrassLodPolicy {
    float DrawDistance = 0.0F;
    float LodStart = 0.0F;
    float LodEnd = 0.0F;
    float FarDensity = 0.0F;
    float SegmentStartDistance = 200.0F;
    float SegmentEndDistance = 800.0F;
    uint8_t NearBladeSegments = 1U;
    uint8_t FarBladeSegments = 1U;
    float LodReferenceDistance = 0.0F;
    bool FarTuftsEnabled = false;
    uint8_t FarTuftBladeCount = 5U;
    float TuftTransitionFraction = 0.20F;
    float FarTuftDensity = 1.0F;
    float SegmentLodSoftness = 0.5F;
    bool operator==(const GrassLodPolicy&) const = default;
};

struct GrassClusterWork {
    uint32_t ClusterIndex = 0U;
    uint32_t AnchorEnd = 0U;
    bool CullIndividualBlades = false;
    bool operator==(const GrassClusterWork&) const = default;
};

// Fused visibility and prefix preparation: each cluster's distance, frustum
// classification and retention bound are evaluated once, not in two passes.
[[nodiscard]] GrassClusterSelectionStats SelectGrassClusterWork(
    const GrassWorldPlacement& placement, const std::array<float, 16>& positionToClip,
    const std::array<float, 3>& eye, const GrassLodPolicy& policy, float bladeRadiusScale,
    bool frustumCulling, std::vector<GrassClusterWork>& work);

// Resolve the conservative LOD prefix once, before choosing worker count.
// The sum bounds output size, unlike the unfiltered BVH candidate count.
[[nodiscard]] uint64_t PrepareGrassClusterWork(const GrassWorldPlacement& placement,
    std::span<const uint32_t> clusters, const std::array<float, 16>& positionToClip,
    const std::array<float, 3>& eye, const GrassLodPolicy& policy, float bladeRadiusScale,
    bool frustumCulling, std::vector<GrassClusterWork>& work);

struct GrassFrustumRadiusScale {
    float X = 0.0F;
    float Y = 0.0F;
    float W = 0.0F;
};

enum class GrassFrustumRelation : uint8_t {
    Outside,
    Intersecting,
    Inside,
};

[[nodiscard]] GrassFrustumRadiusScale
BuildGrassFrustumRadiusScale(
    const std::array<float, 16>& positionToClip) noexcept;

[[nodiscard]] GrassFrustumRelation ClassifyGrassSphereInFrustum(
    const std::array<float, 16>& positionToClip,
    const GrassFrustumRadiusScale& radiusScale,
    const std::array<float, 3>& center,
    float radius) noexcept;

[[nodiscard]] bool GrassSphereIntersectsFrustum(const std::array<float, 16>& positionToClip,
                                                const std::array<float, 3>& center, float radius) noexcept;

[[nodiscard]] bool GrassSphereIntersectsFrustum(
    const std::array<float, 16>& positionToClip,
    const GrassFrustumRadiusScale& radiusScale,
    const std::array<float, 3>& center,
    float radius) noexcept;

[[nodiscard]] float GrassStableVisibilityValue(
    uint32_t stableId) noexcept;

[[nodiscard]] float GrassLodRetention(
    const InteractiveGrassSettings& settings,
    float distance) noexcept;

[[nodiscard]] GrassLodPolicy BuildGrassLodPolicy(const InteractiveGrassSettings& settings) noexcept;

[[nodiscard]] float GrassLodRetention(const GrassLodPolicy& policy, float distance) noexcept;

// Conservative over all greater distances, including increased distant density.
[[nodiscard]] float GrassLodRetentionUpperBound(const GrassLodPolicy& policy, float distance) noexcept;

[[nodiscard]] GrassLodDecision ResolveGrassLod(const InteractiveGrassSettings& settings, float distance,
                                               uint32_t stableId) noexcept;

[[nodiscard]] GrassLodDecision ResolveGrassLodWithStableVisibility(const InteractiveGrassSettings& settings,
                                                                   float distance, float stableVisibility) noexcept;

[[nodiscard]] GrassLodDecision ResolveGrassLodWithStableVisibility(const GrassLodPolicy& policy, float distance,
                                                                   float stableVisibility) noexcept;

} // namespace Fast::Oot3d
