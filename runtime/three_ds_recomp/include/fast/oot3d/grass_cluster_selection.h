#pragma once

#include "fast/oot3d/grass_visibility.h"
#include "fast/oot3d/grass_world_placement_cache.h"

namespace Fast::Oot3d {

// Cluster ownership precedes root density selection. The original accepted
// roots remain the only geometric source, including partial masks and seams.
template<class Emit>
uint32_t SelectGrassDrawableClusters(const GrassWorldPlacement& world,
    const std::array<float, 16>& clip, const std::array<float, 3>& eye,
    const GrassLodPolicy& policy, float farBladeFraction, float bladeRadiusScale, bool frustumCulling,
    uint32_t budget, uint64_t& evaluated, Emit emit, uint64_t* testedNodes = nullptr) {
    const auto radiusScale = BuildGrassFrustumRadiusScale(clip);
    uint32_t visible = 0;
    std::vector<uint32_t> candidates;
    for (uint32_t i=0; i<world.Midrange.Nodes.size();) {
        if (testedNodes) ++*testedNodes;
        const auto& node=world.Midrange.Nodes[i];
        const float radius=node.Radius+node.MaximumBladeRadius*bladeRadiusScale;
        float squared=0;
        for (size_t axis=0; axis<3; ++axis) {
            const float delta=node.Center[axis]-eye[axis]; squared+=delta*delta;
        }
        const float nearest = std::max(0.0F,std::sqrt(squared)-radius);
        const bool entirelyGrouped = policy.NearBladeSegments <= 2 ||
            (nearest >= policy.SegmentEndDistance && policy.FarBladeSegments <= 2);
        if (nearest>policy.DrawDistance ||
            (entirelyGrouped && node.MinimumStableVisibility > GrassLodRetentionUpperBound(policy,nearest)) ||
            (frustumCulling && !GrassSphereIntersectsFrustum(clip,radiusScale,node.Center,radius))) {
            i=node.Escape;
            continue;
        }
        for (uint32_t j=0; j<node.Count; ++j) candidates.push_back(world.Midrange.GroupOrder[node.First+j]);
        ++i;
    }
    // The immutable tree already defines a stable order. Culling only skips
    // entries; sorting the visible groups every frame adds no stability.
    for (const auto groupIndex : candidates) {
        const auto& group = world.Midrange.Groups[groupIndex];
        ++evaluated;
        const auto firstRoot = world.Midrange.Members[group.FirstMember];
        const auto& representative = world.CullingAnchors[firstRoot];
        float squared = 0;
        for (size_t axis = 0; axis < 3; ++axis) {
            const float delta = representative.Position[axis] - eye[axis];
            squared += delta * delta;
        }
        const float distance = std::sqrt(squared);
        const float radius = group.RootRadius + group.MaximumBladeRadius * bladeRadiusScale;
        if (distance - 2.0F * group.RootRadius - radius > policy.DrawDistance ||
            (frustumCulling && !GrassSphereIntersectsFrustum(clip, radiusScale, group.Center, radius))) continue;
        const auto groupLod = ResolveGrassLodWithStableVisibility(policy, distance, representative.StableVisibility);
        // Keep individual near geometry, but choose ownership for the entire group
        // to avoid simultaneous individual and grouped representations.
        const bool grouped = groupLod.BladeSegments <= 2;
        if (grouped) {
            const uint32_t children = GrassClusterChildCount(group.MemberCount, distance,
                policy.SegmentEndDistance,
                policy.LodReferenceDistance > 0 ? std::min(policy.DrawDistance, policy.LodReferenceDistance) : policy.DrawDistance,
                farBladeFraction);
            if (!groupLod.Visible || children > budget - visible) continue;
            for (uint32_t child = 0; child < children; ++child)
                emit(world.Midrange.Members[group.FirstMember + child], groupLod);
            visible += children;
        } else {
            for (uint32_t child = 0; child < group.MemberCount && visible < budget; ++child) {
                const auto index = world.Midrange.Members[group.FirstMember + child];
                const auto& root = world.CullingAnchors[index];
                float rootSquared = 0;
                for (size_t axis = 0; axis < 3; ++axis) {
                    const float delta = root.Position[axis] - eye[axis];
                    rootSquared += delta * delta;
                }
                auto lod = ResolveGrassLodWithStableVisibility(policy, std::sqrt(rootSquared), root.StableVisibility);
                if (!lod.Visible) continue;
                lod.BladeSegments = groupLod.BladeSegments;
                emit(index, lod);
                ++visible;
            }
        }
        if (visible == budget) break;
    }
    return visible;
}

} // namespace Fast::Oot3d
