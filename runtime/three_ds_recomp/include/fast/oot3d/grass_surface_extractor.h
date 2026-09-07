#pragma once

#include "fast/oot3d/grass_texture_source_cache.h"
#include "fast/oot3d/grass_types.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Fast::Oot3d {

struct GrassPlacementView {
    bool Enabled = false;
    std::array<float, 16> WorldToClip{};
    std::array<float, 3> Eye{};
    float DrawDistance = 0.0F;
    float FullDensityDistance = 0.0F;
    float FarDensity = 0.0F;
    float GuardDistance = 0.0F;
};

struct GrassSourceVertex {
    std::array<float, 3> Position{};
    std::array<float, 3> Normal{0.0F, 1.0F, 0.0F};
    std::array<float, 2> Uv{};
};

struct GrassSourceSurface {
    uint64_t GeometryId = 0;
    uint64_t ContentVersion = 0;
    uint64_t InstanceId = 0;
    uint64_t TextureHash = 0;
    uint8_t MapperSlot = 0;
    GrassTextureWrap MaterialWrapS = GrassTextureWrap::Repeat;
    GrassTextureWrap MaterialWrapT = GrassTextureWrap::Repeat;
    std::array<float, 16> ModelToWorld{};
    bool TransformBakedIntoVertices = true;
    std::span<const GrassSourceVertex> Vertices;
    std::span<const uint32_t> Indices;
    GrassPlacementView PlacementView;
};

struct GrassAnchor {
    std::array<float, 3> LocalPosition{};
    std::array<float, 3> LocalNormal{};
    std::array<float, 2> Uv{};
    float BladeHeight = 0.3F;
    float BladeWidth = 0.03F;
    float Phase = 0.0F;
    std::array<float, 2> WidthAxis{1.0F, 0.0F};
    uint32_t StableId = 0U;
};

struct GrassAnchorCluster {
    uint32_t FirstAnchor = 0U;
    uint32_t AnchorCount = 0U;
    std::array<float, 3> LocalCenter{};
    float LocalRadius = 0.0F;
    float MaximumBladeHeight = 0.0F;
};

struct GrassPlacementSet {
    std::vector<GrassAnchor> Anchors;
    std::vector<GrassAnchorCluster> Clusters;
};

[[nodiscard]] GrassTextureWrap ResolveGrassTextureWrap(
    GrassWrapOverride overrideMode,
    GrassTextureWrap materialMode) noexcept;
[[nodiscard]] float WrapGrassTextureCoordinate(
    float coordinate, GrassTextureWrap wrap) noexcept;
// Converts one selected texture channel into placement probability. This is
// the canonical mask evaluation used by extraction and editor previews.
[[nodiscard]] float EvaluateGrassMaskLevel(
    float sample, const GrassPlacementRule& rule) noexcept;

[[nodiscard]] uint64_t GrassPlacementRuleVersion(
    const GrassPlacementRule& rule,
    const GrassGenerationSettings& generation) noexcept;

[[nodiscard]] bool GrassPlacementViewNeedsRefresh(
    const GrassPlacementView& previous, const GrassPlacementView& current) noexcept;

class GrassSurfaceExtractor final {
  public:
    [[nodiscard]] static std::vector<GrassAnchor> Extract(
        const GrassSourceSurface& surface, const GrassPlacementRule& rule,
        const GrassGenerationSettings& generation,
        const GrassScalarMask& mask, uint32_t budget);
};

[[nodiscard]] GrassPlacementSet BuildGrassPlacementSet(
    std::vector<GrassAnchor> anchors, float clusterSize);

} // namespace Fast::Oot3d
