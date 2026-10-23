#pragma once

#include "fast/oot3d/grass_visibility.h"
#include "fast/oot3d/grass_world_placement_cache.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace Fast::Oot3d {

// The camera-centred GPU anchor store. Within a cluster the anchors are in
// stable-visibility order and the selection only reads the prefix its LOD
// retains at the cluster's nearest distance. The store keeps, for each
// cluster, the prefix retained at its nearest distance from the residency eye
// less a margin: from any camera within the margin of that eye the selection
// needs no anchor the store lacks. Only clusters with a stored prefix remain
// (a source with none keeps one empty cluster), in order with their bounds;
// FirstAnchor addresses the store, AnchorCount is the stored prefix.
struct GrassResidency {
    uint64_t Placements = 0;   // the active placement set it was built for
    uint64_t PolicyKey = 0;    // LOD policy and blade radius scale
    std::array<float, 3> Eye{};
    float Margin = 0.0F;
    std::vector<GrassWorldAnchor> Anchors;
    std::vector<uint32_t> SourceBase;
    std::vector<std::vector<GrassWorldCluster>> Clusters;
    // Per stored anchor, when every source has a shadow grid; they address
    // the sources' shadow points concatenated in source order.
    std::vector<GrassShadowGridReference> ShadowReferences;
    // The placements stay alive while the store refers to them.
    std::vector<std::shared_ptr<const GrassWorldPlacement>> Sources;
};

[[nodiscard]] uint64_t GrassResidencyPolicyKey(const GrassLodPolicy& policy, float bladeRadiusScale) noexcept;

[[nodiscard]] GrassResidency BuildGrassResidency(std::vector<std::shared_ptr<const GrassWorldPlacement>> sources,
                                                 uint64_t placements, const std::array<float, 3>& eye,
                                                 const GrassLodPolicy& policy, float bladeRadiusScale, float margin,
                                                 bool shadowReferences);

// The store serves this eye until it leaves half the margin (the rest covers
// the camera's motion while the next store is being built).
[[nodiscard]] bool GrassResidencyCovers(const GrassResidency& residency, const std::array<float, 3>& eye) noexcept;

} // namespace Fast::Oot3d
