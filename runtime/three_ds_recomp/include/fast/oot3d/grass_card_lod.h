#pragma once

// Geometric grass LOD beyond the blades: cards (cut-out billboards) that stand
// in for the roots of a cluster, of a group of clusters and of a patch of
// clusters, in three distance tiers. Everything derives from the roots the
// extractor accepted; no tier adds surface, and the last tier has no end.
// docs/TRIAEVUM_GRASS_CARD_LOD.md.

#include "fast/oot3d/graphics_settings.h"
#include "fast/oot3d/grass_visibility.h"
#include "fast/oot3d/grass_world_placement_cache.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Fast::Oot3d {

inline constexpr uint32_t kGrassCardLevelCount = 3U;
// Clusters per cell side of the group and patch tiers (4x4 and 16x16).
inline constexpr std::array<uint32_t, kGrassCardLevelCount> kGrassCardCellClusters{1U, 4U, 16U};

struct GrassCardLodPolicy {
    bool Enabled = false;
    // Tier ends in world units: blades [0, T1), clump cards [T1, T2), group
    // cards [T2, T3), patch cards [T3, infinity).
    float BladeDistance = 1000.0F;
    float ClumpDistance = 4000.0F;
    float GroupDistance = 15000.0F;
    // Width of a tier change as a fraction of its distance. The entering
    // tier dithers in over the first half of the band, the leaving tier
    // dithers out over the second half, so one tier is always whole.
    float BlendFraction = 0.25F;
    bool operator==(const GrassCardLodPolicy&) const = default;
};

[[nodiscard]] GrassCardLodPolicy BuildGrassCardLodPolicy(const InteractiveGrassSettings& settings) noexcept;

// The blades' draw distance and final fade fraction under the policy: the
// blades end where the clump cards are whole.
[[nodiscard]] float GrassCardLodBladeDrawDistance(const GrassCardLodPolicy& policy, float drawDistance) noexcept;
[[nodiscard]] float GrassCardLodBladeFadeFraction(const GrassCardLodPolicy& policy, float drawFadeFraction) noexcept;

// Visibility weight of a card of the given level at a root distance, the CPU
// mirror of the shader's grass_card_weight (unit tests compare them).
[[nodiscard]] float GrassCardLevelWeight(const GrassCardLodPolicy& policy, uint32_t level, float distance) noexcept;

// One card as the card shaders read it (std430, four uvec4/vec4).
struct GrassCardRecord {
    // Representative root (on the native surface) and the tallest blade.
    std::array<float, 4> RootHeight{};
    // Horizontal footprint radius, wind phase, root count, reserved.
    std::array<float, 4> ExtentPhase{};
    uint32_t PackedNormal = 0x7fff0000U;
    uint32_t Seed = 0U;
    uint32_t SurfaceColor = 0U;
    // Coarse shadow grid points of the representative root (kGrassCardNoShadow
    // when the placement has no grid) and their mix weight.
    uint32_t ShadowFirst = 0U;
    uint32_t ShadowSecond = 0U;
    float ShadowWeight = 0.0F;
    std::array<uint32_t, 2> SurfaceReference{};
};
static_assert(sizeof(GrassCardRecord) == 64U);
static_assert(offsetof(GrassCardRecord, PackedNormal) == 32U);
static_assert(offsetof(GrassCardRecord, ShadowSecond) == 48U);
inline constexpr uint32_t kGrassCardNoShadow = 0xffffffffU;

// A level's cards lie in Morton order of their roots and are cut into chunks
// of consecutive cards with a bounding sphere of the roots, so a frame draws
// the chunks in view and in the level's distance band and never instances
// the rest (a field holds hundreds of thousands of cards).
inline constexpr uint32_t kGrassCardChunkCards = 256U;
struct GrassCardChunk {
    std::array<float, 3> Center{};
    // Of the roots around Center; the tallest blade and the widest footprint
    // bound how far a card's quad reaches beyond its root.
    float RootRadius = 0.0F;
    float MaxHeight = 0.0F;
    float MaxFootprint = 0.0F;
    // Cards of the chunk: within the level in a set, within the record
    // stream once appended.
    uint32_t First = 0U;
    uint32_t Count = 0U;
};

struct GrassCardLevel {
    std::vector<GrassCardRecord> Cards;
    std::vector<GrassCardChunk> Chunks;
    float CellSize = 0.0F;
};

struct GrassCardSet {
    uint64_t Identity = 0U;
    uint64_t ContentVersion = 0U;
    float ClusterSize = 0.0F;
    std::array<GrassCardLevel, kGrassCardLevelCount> Levels;
    [[nodiscard]] size_t CardCount() const noexcept {
        size_t count = 0U;
        for (const auto& level : Levels) count += level.Cards.size();
        return count;
    }
};

// Builds the three card levels of a placement: level 0 one card per cluster,
// levels 1 and 2 one card per cell of kGrassCardCellClusters clusters (world
// cells of clusterSize times that count). Deterministic for a placement.
[[nodiscard]] GrassCardSet BuildGrassCards(const GrassWorldPlacement& placement, float clusterSize);

// Appends a set's cards and chunks per level to a flat stream with the shadow
// grid points rebased to a concatenated point array and the chunks rebased
// to the stream; returns the first index and count of each level's records
// and chunks.
struct GrassCardRange {
    uint32_t First = 0U;
    uint32_t Count = 0U;
};
struct GrassCardLevelRanges {
    GrassCardRange Cards;
    GrassCardRange Chunks;
};
struct GrassCardStream {
    std::vector<GrassCardRecord> Records;
    std::vector<GrassCardChunk> Chunks;
};
[[nodiscard]] std::array<GrassCardLevelRanges, kGrassCardLevelCount> AppendGrassCards(
    const GrassCardSet& set, uint32_t shadowPointBase, bool shadowGrid, GrassCardStream& stream);

// The frame's view of a placement for the chunk selection.
struct GrassCardCullView {
    std::array<float, 16> PositionToClip{};
    GrassFrustumRadiusScale RadiusScale{};
    std::array<float, 3> Eye{};
    // The largest tile set ratio (GrassCardAtlasShape::HeightOverRadius[0]):
    // bounds a card's tile radius (height over that ratio) and so its reach.
    float LargestSetRatio = 1.0F;
    bool FrustumCulling = true;
};

// How far any point of a card (its quad, wind shear and blade offsets) may
// lie from its root: twice the depth scale of the largest tile it can draw.
[[nodiscard]] float GrassCardReach(float height, float footprint, float largestSetRatio) noexcept;

// Appends the runs of consecutive chunks of a level whose cards may be
// visible (in the frustum, in the level's distance band) as record ranges.
void SelectGrassCardChunks(std::span<const GrassCardChunk> chunks, const GrassCardLodPolicy& policy, uint32_t level,
                           const GrassCardCullView& view, std::vector<GrassCardRange>& runs);

} // namespace Fast::Oot3d
