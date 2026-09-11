#pragma once

#include "fast/oot3d/grass_surface_extractor.h"
#include "fast/oot3d/grass_anchor_codec.h"
#include "fast/oot3d/grass_midrange_clusters.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace Fast::Oot3d {

struct GrassWorldAnchor {
    std::array<float, 4> BaseHeight{};
    std::array<float, 2> HalfWidthPhase{};
    uint32_t PackedWidthAxis = 0x00007fffU;
    uint32_t PackedWorldNormal = 0x7fff0000U;
    uint32_t StableId = 0U;
    uint32_t SurfaceColor = 0U;
    std::array<uint32_t, 2> SurfaceReference{};
};
// Shared CPU/storage-buffer format: twelve scalar words, no std430 vec3 padding.
static_assert(sizeof(GrassWorldAnchor) == 48U);
static_assert(offsetof(GrassWorldAnchor, PackedWidthAxis) == 24U);
static_assert(offsetof(GrassWorldAnchor, StableId) == 32U);
static_assert(offsetof(GrassWorldAnchor, SurfaceColor) == 36U);
static_assert(offsetof(GrassWorldAnchor, SurfaceReference) == 40U);

// Compact immutable stream used only by camera-dependent visibility. It
// retains the exact anchor order while keeping the full GPU payload cold.
struct GrassWorldCullingAnchor {
    std::array<float, 3> Position{};
    float StableVisibility = 0.0F;
};
static_assert(sizeof(GrassWorldCullingAnchor) == 16U);

struct GrassWorldCluster {
    uint32_t FirstAnchor = 0U;
    uint32_t AnchorCount = 0U;
    std::array<float, 3> Center{};
    float Radius = 0.0F;
    float MaximumBladeHeight = 0.0F;
    float MinimumStableVisibility = 0.0F;
};

struct GrassClusterVisibilityNode {
    std::array<float, 3> Center{};
    float Radius = 0.0F;
    float MaximumBladeHeight = 0.0F;
    uint32_t FirstCluster = 0U;
    uint32_t ClusterCount = 0U;
    uint32_t Escape = 0U;
    float MinimumStableVisibility = 0.0F;
};

struct GrassWorldPlacement {
    uint64_t Identity = 0U;
    uint64_t ContentVersion = 0U;
    mutable uint64_t LastUsedFrame = 0U;
    std::vector<GrassWorldAnchor> Anchors;
    std::vector<GrassWorldCullingAnchor> CullingAnchors;
    std::vector<GrassWorldCluster> Clusters;
    // Immutable spatial index. Leaves reference clusters without reordering
    // anchors or changing their deterministic identity.
    std::vector<uint32_t> VisibilityClusterOrder;
    std::vector<GrassClusterVisibilityNode> VisibilityNodes;
    GrassMidrangeClusters Midrange;
};

struct GrassWorldPlacementRequest {
    GrassTextureColorSource ColorSource;
    GrassTextureWrap ColorWrapS = GrassTextureWrap::Repeat;
    GrassTextureWrap ColorWrapT = GrassTextureWrap::Repeat;
    uint64_t Identity = 0U;
    uint64_t ContentVersion = 0U;
    uint64_t FrameId = 0U;
    std::span<const GrassAnchor> Anchors;
    std::span<const GrassAnchorCluster> Clusters;
    std::array<float, 16> ModelToWorld{};
    bool TransformBakedIntoVertices = true;
    float NormalOffset = 0.0F;
    float HeightScale = 1.0F;
    // Disabled until the cluster draw consumer is selected. Independent of
    // the existing coarse-culling cluster size and never camera-dependent.
    float MidrangeCellExtent = 0.0F;
};

struct GrassWorldPlacementCacheStats {
    uint64_t Hits = 0U;
    uint64_t Misses = 0U;
    uint64_t Updates = 0U;
    uint64_t Evictions = 0U;
    size_t Entries = 0U;
};

[[nodiscard]] GrassWorldPlacement BuildGrassWorldPlacement(const GrassWorldPlacementRequest& request);

class GrassWorldPlacementCache final {
  public:
    using Placement = std::shared_ptr<const GrassWorldPlacement>;

    explicit GrassWorldPlacementCache(size_t capacity = 64U);
    ~GrassWorldPlacementCache();
    GrassWorldPlacementCache(const GrassWorldPlacementCache&) = delete;
    GrassWorldPlacementCache& operator=(const GrassWorldPlacementCache&) = delete;

    [[nodiscard]] Placement Resolve(const GrassWorldPlacementRequest& request);
    [[nodiscard]] GrassWorldPlacementCacheStats Stats() const;
    void Clear();

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Oot3d
