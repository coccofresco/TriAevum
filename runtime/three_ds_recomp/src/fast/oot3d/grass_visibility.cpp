#include "fast/oot3d/grass_visibility.h"
#include "fast/oot3d/grass_world_placement_cache.h"
#include "fast/oot3d/grass_distant_tuft.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Fast::Oot3d {
namespace {

std::array<float, 4> TransformClip(const std::array<float, 16>& matrix, const std::array<float, 3>& point) {
    return {
        matrix[0] * point[0] + matrix[4] * point[1] + matrix[8] * point[2] + matrix[12],
        matrix[1] * point[0] + matrix[5] * point[1] + matrix[9] * point[2] + matrix[13],
        matrix[2] * point[0] + matrix[6] * point[1] + matrix[10] * point[2] + matrix[14],
        matrix[3] * point[0] + matrix[7] * point[1] + matrix[11] * point[2] + matrix[15],
    };
}

float RowRadius(const std::array<float, 16>& matrix, size_t row, float radius) {
    return radius * std::sqrt(matrix[row] * matrix[row] + matrix[4U + row] * matrix[4U + row] +
                              matrix[8U + row] * matrix[8U + row]);
}

float StableUnit(uint32_t value) {
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    value ^= value >> 16U;
    return static_cast<float>(value >> 8U) / static_cast<float>(1U << 24U);
}

float GrassDensityRetention(const GrassLodPolicy& policy, float normalized) noexcept {
    if (normalized <= policy.LodStart) {
        return 1.0F;
    }
    if (normalized > 1.0F) return policy.FarDensity / (normalized * normalized);
    const float remaining = std::clamp((1.0F - normalized) /
        std::max(1.0F - policy.LodStart, 1.0e-6F), 0.0F, 1.0F);
    const float squared = remaining * remaining;
    return policy.FarDensity + (1.0F - policy.FarDensity) * squared * squared;
}

float GrassLodRetentionFromNormalized(const GrassLodPolicy& policy, float normalized) noexcept {
    const float weight = GrassTuftWeight(normalized, policy.LodEnd, policy.LodEnd + policy.TuftTransitionFraction);
    const float scale = policy.FarTuftsEnabled
        ? GrassTuftRetentionScale(weight, policy.FarTuftBladeCount, policy.FarTuftDensity) : 1.0F;
    return std::clamp(GrassDensityRetention(policy, normalized) * scale, 0.0F, 1.0F);
}

} // namespace

std::vector<uint32_t> AllocateGrassBudget(const std::vector<GrassBudgetDemand>& demands, uint32_t budget) {
    std::vector<uint32_t> result(demands.size());
    uint64_t maximum = 0U;
    double maximumWeight = 0.0;
    std::vector<size_t> order;
    for (size_t i = 0; i < demands.size(); ++i) {
        const auto& demand = demands[i];
        if (!(demand.Weight > 0.0) || !std::isfinite(demand.Weight) || demand.Maximum == 0U) continue;
        maximum += demand.Maximum;
        maximumWeight = std::max(maximumWeight, demand.Weight);
        order.push_back(i);
    }
    const uint64_t target = std::min<uint64_t>(maximum, budget);
    if (target == 0U) return result;
    std::vector<double> weights(demands.size());
    for (size_t i : order) weights[i] = std::max(demands[i].Weight / maximumWeight,
                                                std::numeric_limits<double>::min());
    // Saturate small capacities first, then redistribute their unused share.
    // Suffix sums preserve tiny weights after the larger demands saturate.
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const double left = demands[a].Maximum / weights[a], right = demands[b].Maximum / weights[b];
        return left == right ? a < b : left < right;
    });
    std::vector<double> suffix(order.size() + 1U);
    for (size_t i = order.size(); i > 0U; --i) suffix[i - 1U] = suffix[i] + weights[order[i - 1U]];
    std::vector<std::pair<double, size_t>> remainders;
    uint64_t assigned = 0U;
    for (size_t start = 0; start < order.size(); ++start) {
        const size_t first = order[start];
        const double remaining = static_cast<double>(target - assigned);
        if (remaining * (weights[first] / suffix[start]) >= demands[first].Maximum) {
            result[first] = demands[first].Maximum;
            assigned += result[first];
            continue;
        }
        for (size_t j = start; j < order.size(); ++j) {
            const size_t i = order[j];
            const double count = std::min(static_cast<double>(demands[i].Maximum),
                                          remaining * (weights[i] / suffix[start]));
            result[i] = static_cast<uint32_t>(std::min(count, static_cast<double>(target - assigned)));
            assigned += result[i];
            if (result[i] < demands[i].Maximum) remainders.emplace_back(count - result[i], i);
        }
        break;
    }
    std::sort(remainders.begin(), remainders.end(), [](const auto& a, const auto& b) {
        return a.first == b.first ? a.second < b.second : a.first > b.first;
    });
    for (const auto& [fraction, index] : remainders) {
        if (assigned >= target) break;
        ++result[index];
        ++assigned;
    }
    return result;
}

std::optional<std::array<float, 3>> GrassCameraFromProjection(
    const std::array<float, 16>& matrix) noexcept {
    // The perspective eye is the intersection of clip X=0, Y=0 and W=0.
    // Normalize each plane so off-axis/FOV changes do not affect conditioning.
    std::array<std::array<double, 4>, 3> planes{};
    constexpr std::array<size_t, 3> rows{0U, 1U, 3U};
    for (size_t i = 0; i < rows.size(); ++i) {
        double lengthSquared = 0.0;
        for (size_t j = 0; j < 4U; ++j) {
            planes[i][j] = matrix[j * 4U + rows[i]];
            if (!std::isfinite(planes[i][j])) return std::nullopt;
            if (j < 3U) lengthSquared += planes[i][j] * planes[i][j];
        }
        if (lengthSquared <= 1.0e-24) return std::nullopt;
        for (auto& component : planes[i]) component /= std::sqrt(lengthSquared);
    }
    const auto cross = [](const auto& a, const auto& b) {
        return std::array<double, 3>{a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]};
    };
    const auto a = cross(planes[1], planes[2]);
    const auto b = cross(planes[2], planes[0]);
    const auto c = cross(planes[0], planes[1]);
    const double determinant = planes[0][0]*a[0] + planes[0][1]*a[1] + planes[0][2]*a[2];
    if (std::abs(determinant) <= 1.0e-12) return std::nullopt;
    std::array<float, 3> eye{};
    for (size_t i = 0; i < 3U; ++i) {
        eye[i] = static_cast<float>(-(planes[0][3]*a[i] + planes[1][3]*b[i] + planes[2][3]*c[i]) / determinant);
        if (!std::isfinite(eye[i])) return std::nullopt;
    }
    return eye;
}

static GrassClusterSelectionStats SelectGrassVisibility(
    const GrassWorldPlacement& placement, const std::array<float, 16>& positionToClip,
    const std::array<float, 3>& eye, float drawDistance, float bladeRadiusScale,
    bool frustumCulling, std::vector<uint32_t>* clusterIndices, const GrassLodPolicy* lodPolicy,
    std::vector<GrassClusterWork>* clusterWork) {
    if (clusterIndices) clusterIndices->clear();
    if (clusterWork) clusterWork->clear();
    GrassClusterSelectionStats stats;
    const auto radiusScale = BuildGrassFrustumRadiusScale(positionToClip);
    struct Visibility {
        GrassFrustumRelation Relation = GrassFrustumRelation::Outside;
        float Retention = 0;
    };
    const auto visible = [&](const auto& bound) {
        const float radius = bound.Radius + bound.MaximumBladeHeight * std::max(0.0F, bladeRadiusScale - 1.0F);
        float distanceSquared = 0.0F;
        for (size_t axis = 0; axis < 3U; ++axis) {
            const float delta = bound.Center[axis] - eye[axis];
            distanceSquared += delta * delta;
        }
        const float limit = drawDistance + radius;
        if (distanceSquared > limit * limit) return Visibility{};
        const float nearest = std::max(0.0F, std::sqrt(distanceSquared) - radius);
        if (clusterWork && nearest > drawDistance) return Visibility{};
        const float retention = lodPolicy ? GrassLodRetentionUpperBound(*lodPolicy, nearest) : 1.0F;
        if (lodPolicy && bound.MinimumStableVisibility > retention) return Visibility{};
        return Visibility{frustumCulling ?
            ClassifyGrassSphereInFrustum(positionToClip, radiusScale, bound.Center, radius) : GrassFrustumRelation::Inside,
            retention};
    };
    const auto append = [&](uint32_t index) {
        const auto& cluster = placement.Clusters[index];
        const auto visibility = visible(cluster);
        if (visibility.Relation == GrassFrustumRelation::Outside) return;
        if (clusterWork) {
            if (cluster.FirstAnchor >= placement.CullingAnchors.size()) return;
            const auto begin = placement.CullingAnchors.begin() + cluster.FirstAnchor;
            const auto end = begin + std::min<size_t>(cluster.AnchorCount, placement.CullingAnchors.end() - begin);
            const auto retained = std::upper_bound(begin, end, visibility.Retention,
                [](float retention, const GrassWorldCullingAnchor& a) { return retention < a.StableVisibility; });
            if (retained == begin) return;
            clusterWork->push_back({index, static_cast<uint32_t>(retained-placement.CullingAnchors.begin()),
                visibility.Relation != GrassFrustumRelation::Inside});
            stats.CandidateAnchors += retained - begin;
        } else {
            clusterIndices->push_back(index);
            stats.CandidateAnchors += cluster.AnchorCount;
        }
    };
    if (placement.VisibilityNodes.empty()) {
        for (uint32_t i = 0; i < placement.Clusters.size(); ++i) append(i);
    } else {
        for (uint32_t i = 0; i < placement.VisibilityNodes.size();) {
            const auto& node = placement.VisibilityNodes[i];
            ++stats.TestedNodes;
            if (visible(node).Relation == GrassFrustumRelation::Outside) {
                i = node.Escape;
                continue;
            }
            for (uint32_t j = 0; j < node.ClusterCount; ++j)
                append(placement.VisibilityClusterOrder[node.FirstCluster + j]);
            ++i;
        }
        // Budget decisions cannot depend on traversal order or camera motion.
        if (clusterIndices) std::sort(clusterIndices->begin(), clusterIndices->end());
        else std::sort(clusterWork->begin(), clusterWork->end(),
            [](const auto& a, const auto& b) { return a.ClusterIndex < b.ClusterIndex; });
    }
    stats.CandidateClusters = static_cast<uint32_t>(clusterIndices ? clusterIndices->size() : clusterWork->size());
    return stats;
}

GrassClusterSelectionStats SelectGrassVisibleClusters(
    const GrassWorldPlacement& placement, const std::array<float, 16>& positionToClip,
    const std::array<float, 3>& eye, float drawDistance, float bladeRadiusScale,
    bool frustumCulling, std::vector<uint32_t>& clusters, const GrassLodPolicy* lodPolicy) {
    return SelectGrassVisibility(placement, positionToClip, eye, drawDistance, bladeRadiusScale,
        frustumCulling, &clusters, lodPolicy, nullptr);
}

GrassClusterSelectionStats SelectGrassClusterWork(
    const GrassWorldPlacement& placement, const std::array<float, 16>& positionToClip,
    const std::array<float, 3>& eye, const GrassLodPolicy& policy, float bladeRadiusScale,
    bool frustumCulling, std::vector<GrassClusterWork>& work) {
    return SelectGrassVisibility(placement, positionToClip, eye, policy.DrawDistance, bladeRadiusScale,
        frustumCulling, nullptr, &policy, &work);
}

float GrassStableVisibilityValue(uint32_t stableId) noexcept {
    return StableUnit(stableId);
}

GrassLodPolicy BuildGrassLodPolicy(const InteractiveGrassSettings& settings) noexcept {
    GrassLodPolicy policy;
    policy.DrawDistance = settings.DrawDistance;
    policy.LodReferenceDistance = settings.LodReferenceDistance > 0.0F ? settings.LodReferenceDistance : settings.DrawDistance;
    policy.LodStart = std::clamp(settings.LodStartFraction, 0.0F, 1.0F);
    policy.LodEnd = std::clamp(settings.LodEndFraction, policy.LodStart, 1.0F);
    policy.FarDensity = settings.FarDensity;
    policy.SegmentStartDistance = settings.SegmentLodStartDistance;
    policy.SegmentEndDistance = settings.SegmentLodEndDistance;
    policy.NearBladeSegments = settings.Appearance.BladeSegments;
    policy.FarBladeSegments = std::min(policy.NearBladeSegments, settings.FarBladeSegments);
    policy.FarTuftsEnabled = settings.FarTuftsEnabled && !settings.MidrangeClustersEnabled;
    policy.FarTuftBladeCount = settings.FarTuftBladeCount;
    policy.TuftTransitionFraction = settings.TuftTransitionFraction;
    policy.FarTuftDensity = settings.FarTuftDensity;
    policy.SegmentLodSoftness = settings.SegmentLodSoftness;
    return policy;
}

float GrassLodRetention(const GrassLodPolicy& policy, float distance) noexcept {
    if (!std::isfinite(distance) || distance < 0.0F || policy.DrawDistance <= 0.0F || distance > policy.DrawDistance) {
        return 0.0F;
    }
    const float normalized = distance / (policy.LodReferenceDistance > 0.0F ? policy.LodReferenceDistance : policy.DrawDistance);
    return GrassLodRetentionFromNormalized(policy, normalized);
}

float GrassLodRetention(const InteractiveGrassSettings& settings, float distance) noexcept {
    return GrassLodRetention(BuildGrassLodPolicy(settings), distance);
}

float GrassLodRetentionUpperBound(const GrassLodPolicy& policy, float distance) noexcept {
    if (!std::isfinite(distance) || distance < 0 || distance > policy.DrawDistance || policy.DrawDistance <= 0) return 0;
    const float normalized = distance / (policy.LodReferenceDistance > 0 ? policy.LodReferenceDistance : policy.DrawDistance);
    if (!policy.FarTuftsEnabled || policy.FarTuftDensity <= 1) return GrassLodRetention(policy, distance);
    // The numerator cannot exceed distant density and mean footprint only
    // grows. After the transition the bound is exact again. A distant-density
    // boost need not be monotonic, so the near-point retention alone is unsafe.
    const float weight = GrassTuftWeight(normalized, policy.LodEnd, policy.LodEnd + policy.TuftTransitionFraction);
    const float coverage = 1.0F + (policy.FarTuftBladeCount - 1U) * GrassTuftMeanGrowth(weight);
    return std::min(1.0F, GrassDensityRetention(policy, normalized) * policy.FarTuftDensity / coverage);
}

bool GrassSphereIntersectsFrustum(const std::array<float, 16>& positionToClip, const std::array<float, 3>& center,
                                  float radius) noexcept {
    return GrassSphereIntersectsFrustum(
        positionToClip,
        BuildGrassFrustumRadiusScale(positionToClip),
        center, radius);
}

GrassFrustumRadiusScale BuildGrassFrustumRadiusScale(
    const std::array<float, 16>& positionToClip) noexcept {
    return {
        RowRadius(positionToClip, 0U, 1.0F),
        RowRadius(positionToClip, 1U, 1.0F),
        RowRadius(positionToClip, 3U, 1.0F),
    };
}

bool GrassSphereIntersectsFrustum(
    const std::array<float, 16>& positionToClip,
    const GrassFrustumRadiusScale& radiusScale,
    const std::array<float, 3>& center,
    float radius) noexcept {
    return ClassifyGrassSphereInFrustum(
               positionToClip, radiusScale, center, radius) !=
           GrassFrustumRelation::Outside;
}

GrassFrustumRelation ClassifyGrassSphereInFrustum(
    const std::array<float, 16>& positionToClip,
    const GrassFrustumRadiusScale& radiusScale,
    const std::array<float, 3>& center,
    float radius) noexcept {
    radius = std::max(radius, 0.0F);
    const auto clip = TransformClip(positionToClip, center);
    if (!std::all_of(clip.begin(), clip.end(), [](float value) { return std::isfinite(value); })) {
        return GrassFrustumRelation::Outside;
    }
    const float radiusX = radiusScale.X * radius;
    const float radiusY = radiusScale.Y * radius;
    const float radiusW = radiusScale.W * radius;
    if (clip[3] + radiusW <= 1.0e-7F) {
        return GrassFrustumRelation::Outside;
    }
    const float extent = std::abs(clip[3]) + radiusW;
    if (clip[0] + radiusX < -extent ||
        clip[0] - radiusX > extent ||
        clip[1] + radiusY < -extent ||
        clip[1] - radiusY > extent) {
        return GrassFrustumRelation::Outside;
    }
    const float minimumW = clip[3] - radiusW;
    if (minimumW > 1.0e-7F &&
        clip[0] - radiusX >= -minimumW &&
        clip[0] + radiusX <= minimumW &&
        clip[1] - radiusY >= -minimumW &&
        clip[1] + radiusY <= minimumW) {
        return GrassFrustumRelation::Inside;
    }
    return GrassFrustumRelation::Intersecting;
}

uint64_t PrepareGrassClusterWork(const GrassWorldPlacement& placement,
    std::span<const uint32_t> clusters, const std::array<float, 16>& positionToClip,
    const std::array<float, 3>& eye, const GrassLodPolicy& policy, float bladeRadiusScale,
    bool frustumCulling, std::vector<GrassClusterWork>& work) {
    work.clear();
    const auto radiusScale = BuildGrassFrustumRadiusScale(positionToClip);
    uint64_t candidates = 0U;
    for (const auto index : clusters) {
        if (index >= placement.Clusters.size()) continue;
        const auto& cluster = placement.Clusters[index];
        if (cluster.FirstAnchor >= placement.CullingAnchors.size()) continue;
        const float radius = cluster.Radius + cluster.MaximumBladeHeight * (bladeRadiusScale - 1.0F);
        float distanceSquared = 0.0F;
        for (size_t axis = 0; axis < 3U; ++axis) {
            const float delta = cluster.Center[axis] - eye[axis];
            distanceSquared += delta * delta;
        }
        const float nearest = std::max(0.0F, std::sqrt(distanceSquared) - radius);
        const auto relation = frustumCulling ?
            ClassifyGrassSphereInFrustum(positionToClip, radiusScale, cluster.Center, radius) : GrassFrustumRelation::Inside;
        if (nearest > policy.DrawDistance || relation == GrassFrustumRelation::Outside) continue;
        const auto begin = placement.CullingAnchors.begin() + cluster.FirstAnchor;
        const auto end = begin + std::min<size_t>(cluster.AnchorCount, placement.CullingAnchors.end() - begin);
        const auto retained = std::upper_bound(begin, end, GrassLodRetentionUpperBound(policy, nearest),
            [](float retention, const GrassWorldCullingAnchor& a) { return retention < a.StableVisibility; });
        if (retained == begin) continue;
        candidates += retained - begin;
        work.push_back({index, static_cast<uint32_t>(retained - placement.CullingAnchors.begin()),
                        relation != GrassFrustumRelation::Inside});
    }
    return candidates;
}

GrassLodDecision ResolveGrassLodWithStableVisibility(const GrassLodPolicy& policy, float distance,
                                                     float stableVisibility) noexcept {
    GrassLodDecision result;
    if (!std::isfinite(distance) || distance < 0.0F || policy.DrawDistance <= 0.0F || distance > policy.DrawDistance) {
        return result;
    }

    const float normalized = distance / (policy.LodReferenceDistance > 0.0F ? policy.LodReferenceDistance : policy.DrawDistance);

    float progress = 0.0F;
    if (distance > policy.SegmentStartDistance) {
        progress = policy.SegmentEndDistance > policy.SegmentStartDistance
                       ? std::clamp((distance - policy.SegmentStartDistance) /
                                        (policy.SegmentEndDistance - policy.SegmentStartDistance), 0.0F, 1.0F)
                       : 1.0F;
    }
    // Three shared topology bands replace one batch for every intermediate
    // segment count. Full detail is retained at the configured near boundary.
    const uint8_t middleSegments = std::max<uint8_t>(policy.FarBladeSegments, (policy.NearBladeSegments + 1U) / 2U);
    // Decorrelate topology selection from density retention. A fixed seed
    // spreads the transition across anchors without camera-dependent shuffling.
    const float choice = StableUnit(static_cast<uint32_t>(stableVisibility * 16777216.0F));
    const float halfBand = policy.SegmentLodSoftness / 6.0F;
    const float first = GrassTuftWeight(progress, 1.0F/3.0F-halfBand, 1.0F/3.0F+halfBand);
    const float second = GrassTuftWeight(progress, 2.0F/3.0F-halfBand, 2.0F/3.0F+halfBand);
    result.BladeSegments = choice < second ? policy.FarBladeSegments :
        choice < first ? middleSegments : policy.NearBladeSegments;
    const float tuftWeight = GrassTuftWeight(normalized, policy.LodEnd, policy.LodEnd + policy.TuftTransitionFraction);
    const bool distant = choice < tuftWeight;
    result.PlaneCount = distant ? 1U : 2U;
    result.DistantTuft = policy.FarTuftsEnabled && distant;
    result.Retention = GrassLodRetentionFromNormalized(policy, normalized);
    result.Visible = stableVisibility <= result.Retention;
    return result;
}

GrassLodDecision ResolveGrassLodWithStableVisibility(const InteractiveGrassSettings& settings, float distance,
                                                     float stableVisibility) noexcept {
    return ResolveGrassLodWithStableVisibility(BuildGrassLodPolicy(settings), distance, stableVisibility);
}

GrassLodDecision ResolveGrassLod(const InteractiveGrassSettings& settings, float distance, uint32_t stableId) noexcept {
    return ResolveGrassLodWithStableVisibility(settings, distance, GrassStableVisibilityValue(stableId));
}

} // namespace Fast::Oot3d
