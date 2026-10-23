#pragma once

// The grass cards' texture: tiles of blades drawn with the blades' own shape
// function (grass_blade_shape) at several view elevations, as coverage,
// height factor and view depth. Colour, lighting, toon and fog are applied
// per card at draw time with the blade functions, so the atlas depends only
// on the blade shape settings and on the card footprints.
// docs/TRIAEVUM_GRASS_CARD_LOD.md.

#include "fast/oot3d/graphics_settings.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Fast::Oot3d {

// Sets differ by the blades' height over the footprint radius; a card picks
// the set nearest its own ratio (grass_card_lod.h, GrassCardAtlasSetFor).
inline constexpr uint32_t kGrassCardAtlasSets = 3U;
inline constexpr uint32_t kGrassCardAtlasElevations = 4U; // 0, 30, 60, 90 degrees
inline constexpr uint32_t kGrassCardAtlasVariants = 8U;
inline constexpr uint32_t kGrassCardAtlasTileSize = 128U;
inline constexpr uint32_t kGrassCardAtlasTexelBytes = 4U;
inline constexpr uint32_t kGrassCardAtlasLayers =
    kGrassCardAtlasSets * kGrassCardAtlasElevations * kGrassCardAtlasVariants;
inline constexpr std::array<float, kGrassCardAtlasElevations> kGrassCardAtlasElevationDegrees{0.0F, 30.0F, 60.0F, 90.0F};

[[nodiscard]] constexpr uint32_t GrassCardAtlasLayer(uint32_t set, uint32_t elevation, uint32_t variant) noexcept {
    return (set * kGrassCardAtlasElevations + elevation) * kGrassCardAtlasVariants + variant;
}

struct GrassCardAtlasShape {
    // grass_blade_shape parameters: curvature, droop, variation, twist (radians).
    float Curvature = 0.0F;
    float Droop = 0.0F;
    float Variation = 0.0F;
    float Twist = 0.0F;
    uint8_t Segments = 2U;
    // Shortest blade relative to the tallest.
    float MinimumHeightRatio = 0.6F;
    // Per set: the tallest blade over the card's footprint radius, a blade's
    // half width over that radius, and the blades drawn per tile.
    std::array<float, kGrassCardAtlasSets> HeightOverRadius{1.0F, 0.25F, 0.0625F};
    std::array<float, kGrassCardAtlasSets> HalfWidthOverRadius{0.07F, 0.0175F, 0.0044F};
    std::array<uint32_t, kGrassCardAtlasSets> BladeCount{16U, 48U, 96U};
    bool operator==(const GrassCardAtlasShape&) const = default;
};

// The shape the settings describe: blade shape from Appearance, blade sizes
// from Generation and the footprints from the culling cluster size.
[[nodiscard]] GrassCardAtlasShape GrassCardAtlasShapeFromSettings(const InteractiveGrassSettings& settings) noexcept;
[[nodiscard]] uint64_t GrassCardAtlasKey(const GrassCardAtlasShape& shape) noexcept;

// The set whose height/radius ratio is nearest (in log space) to a card's,
// the CPU mirror of the card vertex shader's choice.
[[nodiscard]] uint32_t GrassCardAtlasSetFor(const GrassCardAtlasShape& shape, float height, float radius) noexcept;

struct GrassCardAtlasImage {
    uint64_t Key = 0U;
    uint32_t Size = 0U;
    uint32_t Layers = 0U;
    // Mip levels, each Layers x size x size texels of four bytes: R coverage
    // (alpha, coverage-preserving in the mips), G height factor along the
    // blade (0 root, 1 tip), B the covered blade's offset toward the viewer
    // from the tile plane (GrassCardTileDepthScale units, 0.5 on the plane),
    // A unused.
    std::vector<std::vector<uint8_t>> Mips;
    [[nodiscard]] uint32_t MipSize(uint32_t mip) const noexcept { return Size >> mip; }
    [[nodiscard]] size_t LayerBytes(uint32_t mip) const noexcept {
        return static_cast<size_t>(MipSize(mip)) * MipSize(mip) * kGrassCardAtlasTexelBytes;
    }
    [[nodiscard]] const uint8_t* Texel(uint32_t mip, uint32_t layer, uint32_t x, uint32_t y) const noexcept {
        return Mips[mip].data() + layer * LayerBytes(mip) +
               (static_cast<size_t>(y) * MipSize(mip) + x) * kGrassCardAtlasTexelBytes;
    }
};

// The geometry of one tile: the card's vertical extent above and below the
// representative root along the view-plane up axis, at a view elevation
// (radians above the horizon), for a tile of unit radius whose tallest blade
// is heightOverRadius. Shared by the generator and the card shader.
struct GrassCardTileExtent {
    float Above = 0.0F;
    float Below = 0.0F;
};
[[nodiscard]] GrassCardTileExtent GrassCardTileExtentAt(float heightOverRadius, float radius, float elevation) noexcept;
// The largest blade offset toward the viewer a tile of unit radius encodes.
[[nodiscard]] constexpr float GrassCardTileDepthScale(float heightOverRadius) noexcept {
    return 1.0F + heightOverRadius;
}

[[nodiscard]] GrassCardAtlasImage BuildGrassCardAtlas(const GrassCardAtlasShape& shape);

// Fraction of a layer's texels whose coverage passes the alpha test.
[[nodiscard]] float GrassCardAtlasCoverage(const GrassCardAtlasImage& image, uint32_t layer, uint32_t mip) noexcept;

} // namespace Fast::Oot3d
