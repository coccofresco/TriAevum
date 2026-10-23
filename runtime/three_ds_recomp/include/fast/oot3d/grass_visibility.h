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

// Cull ancestors before splitting independent immutable subtrees. The caller
// owns execution and scratch storage; merge and order all work before budgets.
[[nodiscard]] uint32_t SplitGrassClusterSelection(
    const GrassWorldPlacement& placement, const std::array<float, 16>& positionToClip,
    const std::array<float, 3>& eye, const GrassLodPolicy& policy, float bladeRadiusScale,
    bool frustumCulling, uint32_t maximumJobs, std::vector<uint32_t>& roots);

[[nodiscard]] GrassClusterSelectionStats SelectGrassClusterSubtreeWork(
    const GrassWorldPlacement& placement, const std::array<float, 16>& positionToClip,
    const std::array<float, 3>& eye, const GrassLodPolicy& policy, float bladeRadiusScale,
    bool frustumCulling, uint32_t root, std::vector<GrassClusterWork>& work);

// A camera envelope around a reference view: every view whose eye stays within
// EyeRadius of Eye and whose clip rows x, y and w, re-based at Eye, differ from
// the reference by at most RowDelta (3x3 part, Euclidean) and TranslationDelta
// (per row). Candidate lists built for an envelope are supersets of the exact
// cluster selection of every view inside it.
struct GrassCandidateEnvelope {
    std::array<double, 16> ClipAtEye{};
    std::array<float, 3> Eye{};
    float EyeRadius = 0.0F;
    float RowDelta = 0.0F;
    std::array<float, 3> TranslationDelta{};
};

struct GrassClusterCandidate {
    uint32_t ClusterIndex = 0U;
    // Conservative end of the retained prefix over the whole envelope.
    uint32_t AnchorEnd = 0U;
    bool operator==(const GrassClusterCandidate&) const = default;
};

[[nodiscard]] GrassCandidateEnvelope MakeGrassCandidateEnvelope(
    const std::array<float, 16>& positionToClip, const std::array<float, 3>& eye, float eyeRadius,
    float rowDelta, float translationScale) noexcept;

[[nodiscard]] bool GrassCandidateEnvelopeContains(const GrassCandidateEnvelope& envelope,
    const std::array<float, 16>& positionToClip, const std::array<float, 3>& eye) noexcept;

// Conservative traversal of one subtree (root 0 = whole placement), unordered.
[[nodiscard]] GrassClusterSelectionStats SelectGrassClusterCandidates(
    const GrassWorldPlacement& placement, const GrassCandidateEnvelope& envelope, const GrassLodPolicy& policy,
    float bladeRadiusScale, bool frustumCulling, uint32_t root, std::vector<GrassClusterCandidate>& candidates);

[[nodiscard]] uint32_t SplitGrassClusterCandidates(
    const GrassWorldPlacement& placement, const GrassCandidateEnvelope& envelope, const GrassLodPolicy& policy,
    float bladeRadiusScale, bool frustumCulling, uint32_t maximumJobs, std::vector<uint32_t>& roots);

// The exact per-cluster decisions of SelectGrassClusterWork for the current
// view, restricted to the candidates (kept in their order). Returns false when
// a retained prefix reaches past its conservative end: the view left the
// envelope and the candidates must be rebuilt.
[[nodiscard]] bool RefineGrassClusterCandidates(
    const GrassWorldPlacement& placement, std::span<const GrassClusterCandidate> candidates,
    const std::array<float, 16>& positionToClip, const std::array<float, 3>& eye, const GrassLodPolicy& policy,
    float bladeRadiusScale, bool frustumCulling, std::vector<GrassClusterWork>& work, uint64_t& candidateAnchors);

// RefineGrassClusterCandidates one candidate at a time, for callers that
// evaluate each cluster's roots right after its decision.
class GrassClusterRefiner {
  public:
    enum class Result : uint8_t { Rejected, Kept, Violation };
    GrassClusterRefiner(const GrassWorldPlacement& placement, const std::array<float, 16>& positionToClip,
        const std::array<float, 3>& eye, const GrassLodPolicy& policy, float bladeRadiusScale,
        bool frustumCulling) noexcept;
    [[nodiscard]] Result Refine(const GrassClusterCandidate& candidate, GrassClusterWork& work) const noexcept;

  private:
    const GrassWorldPlacement& mPlacement;
    std::array<float, 16> mPositionToClip;
    std::array<float, 3> mEye;
    const GrassLodPolicy& mPolicy;
    float mBladeRadiusScale;
    bool mFrustumCulling;
    std::array<float, 3> mRadiusScale;
};

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

// ResolveGrassLodWithStableVisibility with the far band precomputed. Beyond
// the segment and tuft transitions every topology weight is exactly 1, so the
// decision there is constant except for the density retention, which uses the
// same expressions. Results are bit-identical to the general function.
class GrassLodEvaluator {
  public:
    explicit GrassLodEvaluator(const GrassLodPolicy& policy) noexcept;
    [[nodiscard]] GrassLodDecision Resolve(float distance, float stableVisibility) const noexcept;

  private:
    GrassLodPolicy mPolicy;
    float mReference = 0.0F;
    float mTuftEnd = 0.0F;
    float mFarScale = 1.0F;
    bool mFarBand = false;
};

} // namespace Fast::Oot3d
