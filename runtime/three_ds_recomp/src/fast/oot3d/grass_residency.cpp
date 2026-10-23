#include "fast/oot3d/grass_residency.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Fast::Oot3d {

uint64_t GrassResidencyPolicyKey(const GrassLodPolicy& policy, float bladeRadiusScale) noexcept {
    uint64_t hash = 14695981039346656037ULL;
    const auto mix = [&hash](uint32_t value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    for (const float value : {policy.DrawDistance, policy.LodStart, policy.LodEnd, policy.FarDensity,
                              policy.SegmentStartDistance, policy.SegmentEndDistance, policy.LodReferenceDistance,
                              policy.TuftTransitionFraction, policy.FarTuftDensity, policy.SegmentLodSoftness,
                              bladeRadiusScale})
        mix(std::bit_cast<uint32_t>(value));
    mix(policy.NearBladeSegments);
    mix(policy.FarBladeSegments);
    mix(policy.FarTuftsEnabled ? 1U : 0U);
    mix(policy.FarTuftBladeCount);
    mix(policy.CrossedBlades ? 1U : 0U);
    return hash;
}

GrassResidency BuildGrassResidency(std::vector<std::shared_ptr<const GrassWorldPlacement>> sources,
                                   uint64_t placements, const std::array<float, 3>& eye,
                                   const GrassLodPolicy& policy, float bladeRadiusScale, float margin,
                                   bool shadowReferences) {
    GrassResidency residency;
    residency.Placements = placements;
    residency.PolicyKey = GrassResidencyPolicyKey(policy, bladeRadiusScale);
    residency.Eye = eye;
    residency.Margin = margin;
    residency.SourceBase.reserve(sources.size());
    residency.Clusters.reserve(sources.size());
    // The pass concatenates the sources' shadow grids in this order: each
    // source's references address its points after the earlier sources'.
    uint32_t pointBase = 0;
    for (const auto& source : sources) {
        if (!source || source->CullingAnchors.size() != source->Anchors.size())
            throw std::runtime_error("grass residency source has no culling anchors");
        if (shadowReferences && source->ShadowReferences.size() != source->Anchors.size())
            throw std::runtime_error("grass residency source has no shadow references");
        residency.SourceBase.push_back(static_cast<uint32_t>(residency.Anchors.size()));
        // Only clusters with resident anchors: the selection visits clusters
        // in table order, so dropping empty ones keeps the visible order.
        auto& clusters = residency.Clusters.emplace_back();
        uint32_t stored = 0;
        for (auto cluster : source->Clusters) {
            const size_t first = std::min<size_t>(cluster.FirstAnchor, source->Anchors.size());
            const size_t count = std::min<size_t>(cluster.AnchorCount, source->Anchors.size() - first);
            float distanceSquared = 0.0F;
            for (size_t axis = 0; axis < 3U; ++axis) {
                const float delta = cluster.Center[axis] - eye[axis];
                distanceSquared += delta * delta;
            }
            // The selection's own bound (its cluster radius grows with the
            // tallest blade's bend), at the nearest distance any camera
            // within the margin can have.
            const float radius = cluster.Radius + cluster.MaximumBladeHeight * std::max(0.0F, bladeRadiusScale - 1.0F);
            const float nearest = std::max(0.0F, std::sqrt(distanceSquared) - radius - margin);
            size_t prefix = 0;
            if (nearest <= policy.DrawDistance) {
                const float retention = GrassLodRetentionUpperBound(policy, nearest);
                const auto begin = source->CullingAnchors.begin() + static_cast<ptrdiff_t>(first);
                prefix = static_cast<size_t>(std::upper_bound(begin, begin + static_cast<ptrdiff_t>(count), retention,
                    [](float value, const GrassWorldCullingAnchor& anchor) {
                        return value < anchor.StableVisibility;
                    }) - begin);
            }
            if (prefix == 0) continue;
            if (residency.Anchors.size() + prefix > std::numeric_limits<uint32_t>::max())
                throw std::runtime_error("grass residency exceeds uint32 addressing");
            cluster.FirstAnchor = stored;
            cluster.AnchorCount = static_cast<uint32_t>(prefix);
            clusters.push_back(cluster);
            stored += static_cast<uint32_t>(prefix);
            residency.Anchors.insert(residency.Anchors.end(), source->Anchors.begin() + first,
                                     source->Anchors.begin() + first + prefix);
            if (shadowReferences)
                for (size_t a = first; a < first + prefix; ++a) {
                    auto reference = source->ShadowReferences[a];
                    reference.First += pointBase;
                    reference.Second += pointBase;
                    residency.ShadowReferences.push_back(reference);
                }
        }
        // A source with nothing resident keeps one empty cluster, as a
        // source always has at least one selection workgroup.
        if (clusters.empty() && !source->Clusters.empty()) {
            auto& empty = clusters.emplace_back(source->Clusters.front());
            empty.FirstAnchor = 0;
            empty.AnchorCount = 0;
        }
        pointBase += static_cast<uint32_t>(source->ShadowPoints.size());
    }
    residency.Sources = std::move(sources);
    return residency;
}

bool GrassResidencyCovers(const GrassResidency& residency, const std::array<float, 3>& eye) noexcept {
    float distanceSquared = 0.0F;
    for (size_t axis = 0; axis < 3U; ++axis) {
        const float delta = residency.Eye[axis] - eye[axis];
        distanceSquared += delta * delta;
    }
    const float reach = residency.Margin * 0.5F;
    return std::isfinite(distanceSquared) && distanceSquared <= reach * reach;
}

} // namespace Fast::Oot3d
