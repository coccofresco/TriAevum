#include "fast/oot3d/grass_card_atlas.h"
#include "fast/oot3d/grass_card_lod.h"
#include "fast/oot3d/grass_anchor_codec.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <set>

namespace {

using namespace Fast::Oot3d;

// A placement of a regular grid of clusters, each with a few roots around
// its centre, laid out directly in the world format (no extractor needed).
GrassWorldPlacement GridPlacement(uint32_t clustersPerSide, float clusterSize, uint32_t rootsPerCluster,
                                  bool shadowGrid) {
    GrassWorldPlacement placement;
    placement.Identity = 77U;
    placement.ContentVersion = 3U;
    uint32_t stable = 1U;
    for (uint32_t cz = 0; cz < clustersPerSide; ++cz) {
        for (uint32_t cx = 0; cx < clustersPerSide; ++cx) {
            GrassWorldCluster cluster;
            cluster.FirstAnchor = static_cast<uint32_t>(placement.Anchors.size());
            cluster.AnchorCount = rootsPerCluster;
            cluster.Center = {(static_cast<float>(cx) + 0.5F) * clusterSize, 0.0F,
                              (static_cast<float>(cz) + 0.5F) * clusterSize};
            cluster.Radius = clusterSize * 0.5F;
            for (uint32_t root = 0; root < rootsPerCluster; ++root) {
                GrassWorldAnchor anchor;
                const float angle = static_cast<float>(root) / static_cast<float>(rootsPerCluster) * 2.0F *
                                    std::numbers::pi_v<float>;
                const float radius = root == 0U ? 0.0F : clusterSize * 0.3F;
                anchor.BaseHeight = {cluster.Center[0] + radius * std::cos(angle), 5.0F,
                                     cluster.Center[2] + radius * std::sin(angle), 20.0F + static_cast<float>(root)};
                anchor.HalfWidthPhase = {1.5F, static_cast<float>(root) * 0.3F};
                anchor.PackedWorldNormal = PackGrassNormal({0.0F, 1.0F, 0.0F});
                anchor.StableId = stable++;
                anchor.SurfaceColor = 0xff2040a0U | (root << 24U);
                anchor.SurfaceReference = {cx, cz};
                placement.Anchors.push_back(anchor);
                if (shadowGrid) placement.ShadowReferences.push_back({root, root + 1U, 0.25F});
            }
            placement.Clusters.push_back(cluster);
        }
    }
    return placement;
}

} // namespace

TEST(Oot3dGrassCardLod, PolicyKeepsTierOrderAndNeverEndsTheLastTier) {
    InteractiveGrassSettings settings;
    settings.CardLodEnabled = true;
    settings.DrawDistance = 50000.0F;
    settings.CardLodBladeDistance = 1000.0F;
    settings.CardLodClumpDistance = 500.0F;   // below the blade distance: follows it
    settings.CardLodGroupDistance = 200.0F;   // below the clump distance: follows it
    const auto policy = BuildGrassCardLodPolicy(settings);
    EXPECT_TRUE(policy.Enabled);
    EXPECT_FLOAT_EQ(policy.BladeDistance, 1000.0F);
    EXPECT_GE(policy.ClumpDistance, policy.BladeDistance);
    EXPECT_GE(policy.GroupDistance, policy.ClumpDistance);
    EXPECT_FLOAT_EQ(GrassCardLodBladeDrawDistance(policy, 50000.0F), 1000.0F);
    EXPECT_FLOAT_EQ(GrassCardLodBladeFadeFraction(policy, 0.15F), policy.BlendFraction * 0.5F);

    // The patch tier stays whole at any distance.
    settings.CardLodClumpDistance = 4000.0F;
    settings.CardLodGroupDistance = 15000.0F;
    const auto wide = BuildGrassCardLodPolicy(settings);
    for (const float distance : {15000.0F, 50000.0F, 500000.0F, 5.0e6F})
        EXPECT_FLOAT_EQ(GrassCardLevelWeight(wide, 2U, distance), 1.0F) << distance;

    // Disabled: the blades keep their own draw distance and no card weighs.
    settings.CardLodEnabled = false;
    const auto off = BuildGrassCardLodPolicy(settings);
    EXPECT_FALSE(off.Enabled);
    EXPECT_FLOAT_EQ(GrassCardLodBladeDrawDistance(off, 50000.0F), 50000.0F);
    EXPECT_FLOAT_EQ(GrassCardLodBladeFadeFraction(off, 0.15F), 0.15F);
    for (uint32_t level = 0; level < kGrassCardLevelCount; ++level)
        EXPECT_FLOAT_EQ(GrassCardLevelWeight(off, level, 2000.0F), 0.0F);
}

TEST(Oot3dGrassCardLod, OneTierIsWholeAtEveryDistance) {
    InteractiveGrassSettings settings;
    settings.CardLodEnabled = true;
    settings.DrawDistance = 50000.0F;
    const auto policy = BuildGrassCardLodPolicy(settings);
    const float half = policy.BlendFraction * 0.5F;
    // Inside the blades' band nothing but blades; past the blade distance the
    // clump cards are whole until the group band, and so on.
    EXPECT_FLOAT_EQ(GrassCardLevelWeight(policy, 0U, policy.BladeDistance * (1.0F - policy.BlendFraction)), 0.0F);
    EXPECT_FLOAT_EQ(GrassCardLevelWeight(policy, 0U, policy.BladeDistance * (1.0F - half)), 1.0F);
    EXPECT_FLOAT_EQ(GrassCardLevelWeight(policy, 0U, policy.BladeDistance), 1.0F);
    EXPECT_FLOAT_EQ(GrassCardLevelWeight(policy, 1U, policy.BladeDistance), 0.0F);
    EXPECT_FLOAT_EQ(GrassCardLevelWeight(policy, 0U, policy.ClumpDistance), 0.0F);
    EXPECT_FLOAT_EQ(GrassCardLevelWeight(policy, 1U, policy.ClumpDistance), 1.0F);
    EXPECT_FLOAT_EQ(GrassCardLevelWeight(policy, 1U, policy.GroupDistance), 0.0F);
    EXPECT_FLOAT_EQ(GrassCardLevelWeight(policy, 2U, policy.GroupDistance), 1.0F);
    // The entering tier reaches one before the leaving tier starts to go, so
    // the sum of the card weights never drops below one beyond the blades.
    for (float distance = policy.BladeDistance; distance < policy.GroupDistance * 2.0F; distance += 7.0F) {
        float sum = 0.0F;
        for (uint32_t level = 0; level < kGrassCardLevelCount; ++level) sum += GrassCardLevelWeight(policy, level, distance);
        EXPECT_GE(sum, 1.0F - 1.0e-5F) << distance;
        EXPECT_LE(sum, 2.0F + 1.0e-5F) << distance;
    }
}

TEST(Oot3dGrassCardLod, BuildsOneCardPerClusterAndPerCellOnTheNativeRoots) {
    const float clusterSize = 25.0F;
    const auto placement = GridPlacement(8U, clusterSize, 5U, true);
    const auto set = BuildGrassCards(placement, clusterSize);
    EXPECT_EQ(set.Identity, placement.Identity);
    EXPECT_EQ(set.ContentVersion, placement.ContentVersion);
    ASSERT_EQ(set.Levels[0].Cards.size(), placement.Clusters.size());
    // 8x8 clusters: 2x2 cells of four clusters, one cell of sixteen.
    EXPECT_EQ(set.Levels[1].Cards.size(), 4U);
    EXPECT_EQ(set.Levels[2].Cards.size(), 1U);
    EXPECT_FLOAT_EQ(set.Levels[1].CellSize, clusterSize * 4.0F);
    EXPECT_FLOAT_EQ(set.Levels[2].CellSize, clusterSize * 16.0F);

    for (uint32_t level = 0; level < kGrassCardLevelCount; ++level) {
        for (const auto& card : set.Levels[level].Cards) {
            // The representative root is an accepted root: its position and
            // surface reference match one anchor exactly.
            const auto root = std::find_if(placement.Anchors.begin(), placement.Anchors.end(), [&](const auto& anchor) {
                return anchor.BaseHeight[0] == card.RootHeight[0] && anchor.BaseHeight[1] == card.RootHeight[1] &&
                       anchor.BaseHeight[2] == card.RootHeight[2];
            });
            ASSERT_NE(root, placement.Anchors.end());
            EXPECT_EQ(card.SurfaceReference, root->SurfaceReference);
            EXPECT_GT(card.ExtentPhase[2], 0.0F);
            EXPECT_GT(card.ExtentPhase[0], 0.0F);
            EXPECT_FLOAT_EQ(card.RootHeight[3], 24.0F); // the tallest root of the group
            EXPECT_NE(card.ShadowFirst, kGrassCardNoShadow);
            EXPECT_FLOAT_EQ(card.ShadowWeight, 0.25F);
            const auto normal = UnpackGrassNormal(card.PackedNormal);
            EXPECT_NEAR(normal[1], 1.0F, 1.0e-3F);
        }
    }
    EXPECT_EQ(set.Levels[0].Cards.front().ExtentPhase[2], 5.0F);
    EXPECT_EQ(set.Levels[1].Cards.front().ExtentPhase[2], 80.0F);
    EXPECT_EQ(set.Levels[2].Cards.front().ExtentPhase[2], 320.0F);
    // The footprint grows with the tier.
    EXPECT_LT(set.Levels[0].Cards.front().ExtentPhase[0], set.Levels[1].Cards.front().ExtentPhase[0]);
    EXPECT_LT(set.Levels[1].Cards.front().ExtentPhase[0], set.Levels[2].Cards.front().ExtentPhase[0]);

    // Deterministic, and the clump cards' seeds differ.
    const auto again = BuildGrassCards(placement, clusterSize);
    for (uint32_t level = 0; level < kGrassCardLevelCount; ++level) {
        ASSERT_EQ(again.Levels[level].Cards.size(), set.Levels[level].Cards.size());
        for (size_t i = 0; i < set.Levels[level].Cards.size(); ++i)
            EXPECT_EQ(std::memcmp(&again.Levels[level].Cards[i], &set.Levels[level].Cards[i], sizeof(GrassCardRecord)), 0);
    }
    std::set<uint32_t> seeds;
    for (const auto& card : set.Levels[0].Cards) seeds.insert(card.Seed);
    EXPECT_EQ(seeds.size(), set.Levels[0].Cards.size());
}

TEST(Oot3dGrassCardLod, AppendRebasesShadowPointsOrDropsThemWithoutAGrid) {
    const auto placement = GridPlacement(2U, 25.0F, 3U, true);
    const auto set = BuildGrassCards(placement, 25.0F);
    GrassCardStream stream;
    const auto first = AppendGrassCards(set, 100U, true, stream);
    const auto& records = stream.Records;
    EXPECT_EQ(first[0].Cards.First, 0U);
    EXPECT_EQ(first[0].Cards.Count, 4U);
    EXPECT_EQ(first[1].Cards.First, 4U);
    EXPECT_EQ(first[2].Cards.First, first[1].Cards.First + first[1].Cards.Count);
    EXPECT_EQ(records.size(), set.CardCount());
    for (const auto& record : records) {
        EXPECT_GE(record.ShadowFirst, 100U);
        EXPECT_GE(record.ShadowSecond, 100U);
    }
    // One chunk per level here, rebased to the stream.
    for (uint32_t level = 0; level < kGrassCardLevelCount; ++level) {
        EXPECT_EQ(first[level].Chunks.Count, 1U);
        EXPECT_EQ(stream.Chunks[first[level].Chunks.First].First, first[level].Cards.First);
        EXPECT_EQ(stream.Chunks[first[level].Chunks.First].Count, first[level].Cards.Count);
    }
    const auto second = AppendGrassCards(set, 0U, false, stream);
    EXPECT_EQ(second[0].Cards.First, set.CardCount());
    EXPECT_EQ(stream.Chunks[second[0].Chunks.First].First, set.CardCount());
    for (size_t i = set.CardCount(); i < records.size(); ++i) {
        EXPECT_EQ(records[i].ShadowFirst, kGrassCardNoShadow);
        EXPECT_EQ(records[i].ShadowSecond, kGrassCardNoShadow);
        EXPECT_FLOAT_EQ(records[i].ShadowWeight, 0.0F);
    }
    // No shadow references in the placement: the cards carry no points.
    const auto plain = BuildGrassCards(GridPlacement(2U, 25.0F, 3U, false), 25.0F);
    for (const auto& card : plain.Levels[0].Cards) EXPECT_EQ(card.ShadowFirst, kGrassCardNoShadow);
}

TEST(Oot3dGrassCardLod, EmptyPlacementsBuildNoCards) {
    GrassWorldPlacement placement;
    const auto set = BuildGrassCards(placement, 25.0F);
    EXPECT_EQ(set.CardCount(), 0U);
    GrassCardStream stream;
    const auto ranges = AppendGrassCards(set, 0U, true, stream);
    EXPECT_TRUE(stream.Records.empty());
    EXPECT_TRUE(stream.Chunks.empty());
    for (const auto& range : ranges) {
        EXPECT_EQ(range.Cards.Count, 0U);
        EXPECT_EQ(range.Chunks.Count, 0U);
    }
}

TEST(Oot3dGrassCardLod, ChunksBoundTheirCardsInMortonOrder) {
    // 32x32 clusters: 1024 clump cards in four chunks of 256, each a compact
    // region (a 16x16 block of clusters) around its centre.
    const float clusterSize = 25.0F;
    const auto set = BuildGrassCards(GridPlacement(32U, clusterSize, 3U, false), clusterSize);
    const auto& clumps = set.Levels[0];
    ASSERT_EQ(clumps.Cards.size(), 1024U);
    ASSERT_EQ(clumps.Chunks.size(), 4U);
    uint32_t covered = 0U;
    for (const auto& chunk : clumps.Chunks) {
        EXPECT_EQ(chunk.First, covered);
        covered += chunk.Count;
        EXPECT_EQ(chunk.Count, 256U);
        // A 16x16 block of 25-unit clusters: roots within ~283 of the centre.
        EXPECT_LT(chunk.RootRadius, clusterSize * 16.0F * 0.75F);
        EXPECT_FLOAT_EQ(chunk.MaxHeight, 22.0F);
        EXPECT_GT(chunk.MaxFootprint, 0.0F);
        for (uint32_t index = chunk.First; index < chunk.First + chunk.Count; ++index) {
            float squared = 0.0F;
            for (size_t axis = 0; axis < 3U; ++axis) {
                const float delta = clumps.Cards[index].RootHeight[axis] - chunk.Center[axis];
                squared += delta * delta;
            }
            EXPECT_LE(std::sqrt(squared), chunk.RootRadius + 1.0e-3F);
        }
    }
    EXPECT_EQ(covered, clumps.Cards.size());
    // The order is deterministic.
    const auto again = BuildGrassCards(GridPlacement(32U, clusterSize, 3U, false), clusterSize);
    for (size_t i = 0; i < clumps.Cards.size(); ++i)
        EXPECT_EQ(std::memcmp(&again.Levels[0].Cards[i], &clumps.Cards[i], sizeof(GrassCardRecord)), 0);
}

TEST(Oot3dGrassCardLod, ChunkSelectionKeepsTheViewAndTheDistanceBand) {
    // Chunks of 10 cards along +z at 500-unit steps from the eye at the
    // origin; a camera looking down +z with a 90-degree field of view.
    std::vector<GrassCardChunk> chunks;
    for (uint32_t index = 0; index < 12U; ++index) {
        GrassCardChunk chunk;
        chunk.Center = {0.0F, 0.0F, 500.0F + 500.0F * static_cast<float>(index)};
        chunk.RootRadius = 50.0F;
        chunk.MaxHeight = 60.0F;
        chunk.MaxFootprint = 12.5F;
        chunk.First = index * 10U;
        chunk.Count = 10U;
        chunks.push_back(chunk);
    }
    // Column-major: clip.x = x, clip.y = y, clip.z = z, clip.w = z.
    GrassCardCullView view;
    view.PositionToClip = {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F};
    view.RadiusScale = BuildGrassFrustumRadiusScale(view.PositionToClip);
    view.Eye = {0.0F, 0.0F, 0.0F};
    view.LargestSetRatio = 4.0F;
    GrassCardLodPolicy policy;
    policy.Enabled = true;
    policy.BladeDistance = 1000.0F;
    policy.ClumpDistance = 4000.0F;
    policy.GroupDistance = 6000.0F;
    policy.BlendFraction = 0.25F;
    std::vector<GrassCardRange> runs;
    // Clump cards: beyond 750 (1000 less the blend) and before 4000, one run
    // of the chunks at 1000..4000 (the one at 500 reaches only 550).
    SelectGrassCardChunks(chunks, policy, 0U, view, runs);
    ASSERT_EQ(runs.size(), 1U);
    EXPECT_EQ(runs[0].First, 10U);
    EXPECT_EQ(runs[0].Count, 70U);
    runs.clear();
    // Patch cards: everything reaching beyond 4500 (the chunk at 4500 does,
    // by its root radius), to the end.
    SelectGrassCardChunks(chunks, policy, 2U, view, runs);
    ASSERT_EQ(runs.size(), 1U);
    EXPECT_EQ(runs[0].First, 80U);
    EXPECT_EQ(runs[0].Count, 40U);
    runs.clear();
    // Behind the eye nothing draws; two visible chunks apart give two runs.
    chunks[2].Center[2] = -1500.0F;
    SelectGrassCardChunks(chunks, policy, 0U, view, runs);
    ASSERT_EQ(runs.size(), 2U);
    EXPECT_EQ(runs[0].First, 10U);
    EXPECT_EQ(runs[0].Count, 10U);
    EXPECT_EQ(runs[1].First, 30U);
    EXPECT_EQ(runs[1].Count, 50U);
    runs.clear();
    // Without frustum culling the band alone decides.
    view.FrustumCulling = false;
    SelectGrassCardChunks(chunks, policy, 0U, view, runs);
    ASSERT_EQ(runs.size(), 1U);
    EXPECT_EQ(runs[0].Count, 70U);
    runs.clear();
    policy.Enabled = false;
    SelectGrassCardChunks(chunks, policy, 0U, view, runs);
    EXPECT_TRUE(runs.empty());
}

TEST(Oot3dGrassCardLod, SparseCellsSplitIntoSmallerCards) {
    // A clump cell of 4x4 clusters keeping only one full quadrant and one
    // cluster in the opposite corner: two cards (the quadrant, the corner)
    // instead of one card spanning the gap between them.
    const float clusterSize = 25.0F;
    auto placement = GridPlacement(4U, clusterSize, 5U, false);
    std::erase_if(placement.Clusters, [](const GrassWorldCluster& cluster) {
        const bool lowQuadrant = cluster.Center[0] < 50.0F && cluster.Center[2] < 50.0F;
        const bool farCorner = cluster.Center[0] > 75.0F && cluster.Center[2] > 75.0F;
        return !lowQuadrant && !farCorner;
    });
    ASSERT_EQ(placement.Clusters.size(), 5U);
    const auto sparse = BuildGrassCards(placement, clusterSize);
    ASSERT_EQ(sparse.Levels[1].Cards.size(), 2U);
    std::vector<float> counts;
    for (const auto& card : sparse.Levels[1].Cards) counts.push_back(card.ExtentPhase[2]);
    std::sort(counts.begin(), counts.end());
    EXPECT_FLOAT_EQ(counts[0], 5.0F);
    EXPECT_FLOAT_EQ(counts[1], 20.0F);
    // The full cell keeps its one card over all sixteen clusters.
    const auto full = BuildGrassCards(GridPlacement(4U, clusterSize, 5U, false), clusterSize);
    ASSERT_EQ(full.Levels[1].Cards.size(), 1U);
    EXPECT_FLOAT_EQ(full.Levels[1].Cards.front().ExtentPhase[2], 80.0F);
    for (const auto& card : sparse.Levels[1].Cards)
        EXPECT_LT(card.ExtentPhase[0], full.Levels[1].Cards.front().ExtentPhase[0]);
}

TEST(Oot3dGrassCardAtlas, TileExtentFollowsTheViewElevation) {
    // A tile of radius 4 whose tallest blade is 2.5 radii: level with the
    // horizon it shows the height, from above the footprint.
    const auto level = GrassCardTileExtentAt(2.5F, 4.0F, 0.0F);
    EXPECT_FLOAT_EQ(level.Above, 10.0F);
    EXPECT_FLOAT_EQ(level.Below, 0.0F);
    const auto top = GrassCardTileExtentAt(2.5F, 4.0F, std::numbers::pi_v<float> * 0.5F);
    EXPECT_NEAR(top.Above, 4.0F, 1.0e-5F);
    EXPECT_NEAR(top.Below, 4.0F, 1.0e-5F);
    EXPECT_FLOAT_EQ(GrassCardTileDepthScale(2.5F), 3.5F);
}

TEST(Oot3dGrassCardAtlas, SetFollowsTheNearestHeightRatio) {
    GrassCardAtlasShape shape;
    shape.HeightOverRadius = {1.0F, 0.25F, 0.0625F};
    EXPECT_EQ(GrassCardAtlasSetFor(shape, 4.0F, 4.0F), 0U);
    EXPECT_EQ(GrassCardAtlasSetFor(shape, 8.0F, 4.0F), 0U);
    EXPECT_EQ(GrassCardAtlasSetFor(shape, 1.0F, 4.0F), 1U);
    EXPECT_EQ(GrassCardAtlasSetFor(shape, 0.6F, 4.0F), 1U);
    EXPECT_EQ(GrassCardAtlasSetFor(shape, 0.1F, 4.0F), 2U);
    EXPECT_EQ(GrassCardAtlasSetFor(shape, 1.0F, 0.0F), 0U);
}

TEST(Oot3dGrassCardAtlas, ShapeFollowsTheBladeSettingsAndKeysChange) {
    InteractiveGrassSettings settings;
    settings.CullingClusterSize = 25.0F;
    const auto shape = GrassCardAtlasShapeFromSettings(settings);
    EXPECT_GT(shape.HeightOverRadius[0], shape.HeightOverRadius[1]);
    EXPECT_GT(shape.HeightOverRadius[1], shape.HeightOverRadius[2]);
    EXPECT_GT(shape.HalfWidthOverRadius[0], shape.HalfWidthOverRadius[2]);
    EXPECT_GE(shape.MinimumHeightRatio, 0.3F);
    EXPECT_LE(shape.MinimumHeightRatio, 1.0F);
    const auto key = GrassCardAtlasKey(shape);
    EXPECT_NE(key, 0U);
    auto curved = settings;
    curved.Appearance.BladeCurvature += 0.5F;
    EXPECT_NE(GrassCardAtlasKey(GrassCardAtlasShapeFromSettings(curved)), key);
    auto wider = settings;
    wider.CullingClusterSize = 50.0F;
    EXPECT_NE(GrassCardAtlasKey(GrassCardAtlasShapeFromSettings(wider)), key);
    auto windier = settings;
    windier.WindStrength += 1.0F;
    EXPECT_EQ(GrassCardAtlasKey(GrassCardAtlasShapeFromSettings(windier)), key);
}

TEST(Oot3dGrassCardAtlas, TilesCoverBladesWithRootToTipGradientAndCoveragePreservingMips) {
    // The Enhanced blade shape: curved, drooping blades have a top view too
    // (straight ones are lines from above).
    InteractiveGrassSettings settings;
    settings.CullingClusterSize = 25.0F;
    settings.Appearance.BladeCurvature = 1.8F;
    settings.Appearance.BladeDroop = 0.8F;
    settings.Appearance.ShapeVariation = 0.9F;
    settings.Appearance.BladeTwistDegrees = 150.0F;
    settings.Appearance.BladeSegments = 4U;
    const auto shape = GrassCardAtlasShapeFromSettings(settings);
    const auto image = BuildGrassCardAtlas(shape);
    EXPECT_EQ(image.Key, GrassCardAtlasKey(shape));
    EXPECT_EQ(image.Size, kGrassCardAtlasTileSize);
    EXPECT_EQ(image.Layers, kGrassCardAtlasLayers);
    ASSERT_EQ(image.Mips.size(), 8U);
    EXPECT_EQ(image.MipSize(7U), 1U);
    for (uint32_t set = 0; set < kGrassCardAtlasSets; ++set) {
        for (uint32_t elevation = 0; elevation < kGrassCardAtlasElevations; ++elevation) {
            for (uint32_t variant = 0; variant < kGrassCardAtlasVariants; ++variant) {
                const uint32_t layer = GrassCardAtlasLayer(set, elevation, variant);
                const float base = GrassCardAtlasCoverage(image, layer, 0U);
                EXPECT_GT(base, 0.02F) << "set " << set << " elevation " << elevation << " variant " << variant;
                EXPECT_LT(base, 0.95F) << "set " << set << " elevation " << elevation << " variant " << variant;
                // The mips keep the base coverage within a few percent instead
                // of thinning the blades away (down to the 8x8 level).
                for (uint32_t mip = 1; mip + 3U < image.Mips.size(); ++mip) {
                    EXPECT_NEAR(GrassCardAtlasCoverage(image, layer, mip), base, 0.06F)
                        << "layer " << layer << " mip " << mip;
                }
            }
        }
        // Looking from the horizon, the lower rows hold roots (low height
        // factor) and the upper covered rows hold tips.
        const uint32_t layer = GrassCardAtlasLayer(set, 0U, 0U);
        float lower = 0.0F, upper = 0.0F;
        uint32_t lowerCount = 0U, upperCount = 0U;
        for (uint32_t y = 0; y < image.Size; ++y) {
            for (uint32_t x = 0; x < image.Size; ++x) {
                const uint8_t* texel = image.Texel(0U, layer, x, y);
                if (texel[0] < 128U) continue;
                if (y < image.Size / 8U) {
                    lower += texel[1];
                    ++lowerCount;
                } else if (y >= image.Size / 4U) {
                    upper += texel[1];
                    ++upperCount;
                }
            }
        }
        ASSERT_GT(lowerCount, 0U);
        ASSERT_GT(upperCount, 0U);
        EXPECT_LT(lower / lowerCount, upper / upperCount) << "set " << set;
        // The depth channel: covered texels place their blade in front of
        // or behind the tile plane, texels no blade touched sit on it.
        uint8_t nearest = 255U, farthest = 0U;
        bool emptyOnPlane = true;
        for (uint32_t y = 0; y < image.Size; ++y) {
            for (uint32_t x = 0; x < image.Size; ++x) {
                const uint8_t* texel = image.Texel(0U, layer, x, y);
                if (texel[0] == 0U) emptyOnPlane = emptyOnPlane && texel[2] == 128U;
                if (texel[0] < 128U) continue;
                nearest = std::min(nearest, texel[2]);
                farthest = std::max(farthest, texel[2]);
            }
        }
        EXPECT_TRUE(emptyOnPlane) << "set " << set;
        EXPECT_LT(nearest, farthest) << "set " << set;
    }
    // The same shape builds the same image.
    const auto again = BuildGrassCardAtlas(shape);
    EXPECT_EQ(again.Mips[0], image.Mips[0]);
    EXPECT_EQ(again.Mips[3], image.Mips[3]);
}
