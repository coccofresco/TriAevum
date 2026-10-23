#include "fast/oot3d/grass_card_lod.h"

#include "fast/oot3d/grass_anchor_codec.h"
#include "fast/oot3d/grass_distant_tuft.h"
#include "fast/oot3d/grass_parallel_work.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <tuple>

namespace Fast::Oot3d {
namespace {

uint64_t SpreadBits(uint32_t value) noexcept {
    uint64_t result = value;
    result = (result | (result << 16U)) & 0x0000ffff0000ffffULL;
    result = (result | (result << 8U)) & 0x00ff00ff00ff00ffULL;
    result = (result | (result << 4U)) & 0x0f0f0f0f0f0f0f0fULL;
    result = (result | (result << 2U)) & 0x3333333333333333ULL;
    result = (result | (result << 1U)) & 0x5555555555555555ULL;
    return result;
}

// Orders a level's cards along a Morton curve of their roots (cells of four
// times the level's cell size) and cuts them into chunks with their bounds.
// Stable: equal keys keep the build order, so the result is deterministic.
void OrderAndChunkCards(GrassCardLevel& level) {
    level.Chunks.clear();
    if (level.Cards.empty()) return;
    const float unit = std::max(level.CellSize * 4.0F, 1.0F);
    float minX = std::numeric_limits<float>::infinity(), minZ = minX;
    for (const auto& card : level.Cards) {
        minX = std::min(minX, card.RootHeight[0]);
        minZ = std::min(minZ, card.RootHeight[2]);
    }
    std::vector<uint32_t> order(level.Cards.size());
    std::iota(order.begin(), order.end(), 0U);
    std::vector<uint64_t> keys(level.Cards.size());
    for (size_t index = 0; index < keys.size(); ++index) {
        const auto& root = level.Cards[index].RootHeight;
        const auto cell = [&](float value, float minimum) {
            return static_cast<uint32_t>(std::clamp(std::floor((value - minimum) / unit), 0.0F, 4294967040.0F));
        };
        keys[index] = SpreadBits(cell(root[0], minX)) | (SpreadBits(cell(root[2], minZ)) << 1U);
    }
    std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return keys[a] < keys[b]; });
    std::vector<GrassCardRecord> sorted(level.Cards.size());
    for (size_t index = 0; index < order.size(); ++index) sorted[index] = level.Cards[order[index]];
    level.Cards = std::move(sorted);

    for (uint32_t first = 0; first < level.Cards.size(); first += kGrassCardChunkCards) {
        GrassCardChunk chunk;
        chunk.First = first;
        chunk.Count = std::min<uint32_t>(kGrassCardChunkCards, static_cast<uint32_t>(level.Cards.size()) - first);
        std::array<double, 3> sum{};
        for (uint32_t index = first; index < first + chunk.Count; ++index)
            for (size_t axis = 0; axis < 3U; ++axis) sum[axis] += level.Cards[index].RootHeight[axis];
        for (size_t axis = 0; axis < 3U; ++axis) chunk.Center[axis] = static_cast<float>(sum[axis] / chunk.Count);
        for (uint32_t index = first; index < first + chunk.Count; ++index) {
            const auto& card = level.Cards[index];
            float squared = 0.0F;
            for (size_t axis = 0; axis < 3U; ++axis) {
                const float delta = card.RootHeight[axis] - chunk.Center[axis];
                squared += delta * delta;
            }
            chunk.RootRadius = std::max(chunk.RootRadius, std::sqrt(squared));
            chunk.MaxHeight = std::max(chunk.MaxHeight, card.RootHeight[3]);
            chunk.MaxFootprint = std::max(chunk.MaxFootprint, card.ExtentPhase[0]);
        }
        level.Chunks.push_back(chunk);
    }
}

uint32_t MixSeed(uint32_t value) noexcept {
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    value ^= value >> 16U;
    return value;
}

// The card of one group of anchors (a cluster or a cell of clusters).
struct CardAccumulator {
    std::array<double, 3> PositionSum{};
    std::array<double, 3> NormalSum{};
    std::array<double, 3> ColorSum{};
    uint32_t ColorCount = 0U;
    float Height = 0.0F;
    uint32_t Count = 0U;

    void Add(const GrassWorldAnchor& anchor) {
        for (size_t axis = 0; axis < 3U; ++axis) PositionSum[axis] += anchor.BaseHeight[axis];
        const auto normal = UnpackGrassNormal(anchor.PackedWorldNormal);
        for (size_t axis = 0; axis < 3U; ++axis) NormalSum[axis] += normal[axis];
        if ((anchor.SurfaceColor >> 24U) != 0U) {
            ColorSum[0] += (anchor.SurfaceColor & 255U);
            ColorSum[1] += ((anchor.SurfaceColor >> 8U) & 255U);
            ColorSum[2] += ((anchor.SurfaceColor >> 16U) & 255U);
            ++ColorCount;
        }
        Height = std::max(Height, anchor.BaseHeight[3]);
        ++Count;
    }
};

template <typename Visit>
GrassCardRecord FinishCard(const GrassWorldPlacement& placement, const CardAccumulator& sum, Visit&& visitAnchors) {
    GrassCardRecord card;
    if (sum.Count == 0U) return card;
    const std::array<float, 3> mean{static_cast<float>(sum.PositionSum[0] / sum.Count),
                                    static_cast<float>(sum.PositionSum[1] / sum.Count),
                                    static_cast<float>(sum.PositionSum[2] / sum.Count)};
    // The representative root: the accepted root nearest the mean, so the
    // card stands on the native surface and reads that root's lighting texel.
    uint32_t representative = std::numeric_limits<uint32_t>::max();
    float nearest = std::numeric_limits<float>::infinity();
    visitAnchors([&](uint32_t index) {
        const auto& anchor = placement.Anchors[index];
        float distance = 0.0F;
        for (size_t axis = 0; axis < 3U; ++axis) {
            const float delta = anchor.BaseHeight[axis] - mean[axis];
            distance += delta * delta;
        }
        if (distance < nearest || (distance == nearest && index < representative)) {
            nearest = distance;
            representative = index;
        }
    });
    const auto& root = placement.Anchors[representative];
    float radius = 0.0F;
    visitAnchors([&](uint32_t index) {
        const auto& anchor = placement.Anchors[index];
        const float dx = anchor.BaseHeight[0] - root.BaseHeight[0];
        const float dz = anchor.BaseHeight[2] - root.BaseHeight[2];
        radius = std::max(radius, std::sqrt(dx * dx + dz * dz) + anchor.HalfWidthPhase[0]);
    });
    card.RootHeight = {root.BaseHeight[0], root.BaseHeight[1], root.BaseHeight[2], sum.Height};
    card.ExtentPhase = {std::max(radius, root.HalfWidthPhase[0]), root.HalfWidthPhase[1],
                        static_cast<float>(sum.Count), 0.0F};
    card.PackedNormal = PackGrassNormal({static_cast<float>(sum.NormalSum[0]), static_cast<float>(sum.NormalSum[1]),
                                         static_cast<float>(sum.NormalSum[2])});
    card.Seed = MixSeed(root.StableId * 0x9e3779b1U + 0x5bd1e995U);
    if (sum.ColorCount != 0U) {
        const auto channel = [&](size_t axis) {
            return static_cast<uint32_t>(std::lround(sum.ColorSum[axis] / sum.ColorCount)) & 255U;
        };
        card.SurfaceColor = channel(0) | (channel(1) << 8U) | (channel(2) << 16U) | (255U << 24U);
    }
    card.SurfaceReference = root.SurfaceReference;
    if (representative < placement.ShadowReferences.size()) {
        const auto& reference = placement.ShadowReferences[representative];
        card.ShadowFirst = reference.First;
        card.ShadowSecond = reference.Second;
        card.ShadowWeight = reference.Weight;
    } else {
        card.ShadowFirst = kGrassCardNoShadow;
        card.ShadowSecond = kGrassCardNoShadow;
        card.ShadowWeight = 0.0F;
    }
    return card;
}

uint32_t ClusterAnchorEnd(const GrassWorldPlacement& placement, const GrassWorldCluster& cluster) noexcept {
    if (cluster.FirstAnchor >= placement.Anchors.size()) return cluster.FirstAnchor;
    return cluster.FirstAnchor +
           std::min<uint32_t>(cluster.AnchorCount, static_cast<uint32_t>(placement.Anchors.size() - cluster.FirstAnchor));
}

GrassCardRecord ClusterCard(const GrassWorldPlacement& placement, const GrassWorldCluster& cluster) {
    const uint32_t end = ClusterAnchorEnd(placement, cluster);
    CardAccumulator sum;
    for (uint32_t index = cluster.FirstAnchor; index < end; ++index) sum.Add(placement.Anchors[index]);
    return FinishCard(placement, sum, [&](auto&& visit) {
        for (uint32_t index = cluster.FirstAnchor; index < end; ++index) visit(index);
    });
}

GrassCardRecord CellCard(const GrassWorldPlacement& placement, std::span<const uint32_t> clusters) {
    CardAccumulator sum;
    for (const uint32_t clusterIndex : clusters) {
        const auto& cluster = placement.Clusters[clusterIndex];
        const uint32_t end = ClusterAnchorEnd(placement, cluster);
        for (uint32_t index = cluster.FirstAnchor; index < end; ++index) sum.Add(placement.Anchors[index]);
    }
    return FinishCard(placement, sum, [&](auto&& visit) {
        for (const uint32_t clusterIndex : clusters) {
            const auto& cluster = placement.Clusters[clusterIndex];
            const uint32_t end = ClusterAnchorEnd(placement, cluster);
            for (uint32_t index = cluster.FirstAnchor; index < end; ++index) visit(index);
        }
    });
}

} // namespace

GrassCardLodPolicy BuildGrassCardLodPolicy(const InteractiveGrassSettings& settings) noexcept {
    GrassCardLodPolicy policy;
    policy.Enabled = settings.CardLodEnabled && settings.DrawDistance > 0.0F;
    // The blades never end beyond the configured draw distance; the tiers
    // keep their order.
    policy.BladeDistance = std::clamp(std::min(settings.CardLodBladeDistance, settings.DrawDistance), 1.0F, 50000.0F);
    policy.ClumpDistance = std::max(policy.BladeDistance, settings.CardLodClumpDistance);
    policy.GroupDistance = std::max(policy.ClumpDistance, settings.CardLodGroupDistance);
    policy.BlendFraction = std::clamp(settings.CardLodBlendFraction, 0.0F, 0.9F);
    return policy;
}

float GrassCardLodBladeDrawDistance(const GrassCardLodPolicy& policy, float drawDistance) noexcept {
    return policy.Enabled ? std::min(drawDistance, policy.BladeDistance) : drawDistance;
}

float GrassCardLodBladeFadeFraction(const GrassCardLodPolicy& policy, float drawFadeFraction) noexcept {
    return policy.Enabled ? policy.BlendFraction * 0.5F : drawFadeFraction;
}

float GrassCardLevelWeight(const GrassCardLodPolicy& policy, uint32_t level, float distance) noexcept {
    if (!policy.Enabled || level >= kGrassCardLevelCount) return 0.0F;
    const std::array<float, 3> ends{policy.BladeDistance, policy.ClumpDistance, policy.GroupDistance};
    const float half = policy.BlendFraction * 0.5F;
    const float enter = ends[level];
    float weight = GrassTuftWeight(distance, enter * (1.0F - policy.BlendFraction), enter * (1.0F - half));
    if (level + 1U < kGrassCardLevelCount) {
        const float leave = ends[level + 1U];
        weight *= 1.0F - GrassTuftWeight(distance, leave * (1.0F - half), leave);
    }
    return weight;
}

GrassCardSet BuildGrassCards(const GrassWorldPlacement& placement, float clusterSize) {
    GrassCardSet set;
    set.Identity = placement.Identity;
    set.ContentVersion = placement.ContentVersion;
    set.ClusterSize = clusterSize;
    if (placement.Anchors.empty() || placement.Clusters.empty()) return set;
    const size_t workers = GrassParallelWorkerCount(placement.Anchors.size());

    auto& clumps = set.Levels[0];
    clumps.CellSize = clusterSize;
    clumps.Cards.resize(placement.Clusters.size());
    RunGrassParallelWork(workers, [&](size_t worker) {
        const size_t first = placement.Clusters.size() * worker / workers;
        const size_t last = placement.Clusters.size() * (worker + 1U) / workers;
        for (size_t index = first; index < last; ++index)
            clumps.Cards[index] = ClusterCard(placement, placement.Clusters[index]);
    });
    std::erase_if(clumps.Cards, [](const GrassCardRecord& card) { return card.ExtentPhase[2] <= 0.0F; });
    OrderAndChunkCards(clumps);

    const float unit = std::max(clusterSize, 1.0F);
    for (uint32_t level = 1U; level < kGrassCardLevelCount; ++level) {
        auto& cards = set.Levels[level];
        cards.CellSize = unit * static_cast<float>(kGrassCardCellClusters[level]);
        // Cells in key order, clusters in index order: deterministic groups,
        // independent of the thread count.
        std::map<std::tuple<int64_t, int64_t, int64_t>, std::vector<uint32_t>> cells;
        for (uint32_t index = 0; index < placement.Clusters.size(); ++index) {
            const auto& center = placement.Clusters[index].Center;
            const auto cell = [&](float value) { return static_cast<int64_t>(std::floor(value / cards.CellSize)); };
            cells[{cell(center[0]), cell(center[1]), cell(center[2])}].push_back(index);
        }
        // A cell whose clusters cover less than half of it splits into its
        // quadrants (down to the cluster size), so a card never spans a gap the
        // extractor rejected (a path, a pond): sparse cells become several
        // small cards instead of one wide one.
        std::vector<std::vector<uint32_t>> groups;
        const auto split = [&](const auto& self, std::vector<uint32_t> members, float side) -> void {
            const uint32_t slots = static_cast<uint32_t>(std::lround(side / unit));
            if (side <= unit * 1.5F || members.size() * 2U >= static_cast<size_t>(slots) * slots) {
                groups.push_back(std::move(members));
                return;
            }
            const float half = side * 0.5F;
            std::array<std::vector<uint32_t>, 4> quadrants;
            float originX = std::numeric_limits<float>::infinity(), originZ = originX;
            for (const uint32_t index : members) {
                originX = std::min(originX, placement.Clusters[index].Center[0]);
                originZ = std::min(originZ, placement.Clusters[index].Center[2]);
            }
            for (const uint32_t index : members) {
                const auto& center = placement.Clusters[index].Center;
                const uint32_t qx = center[0] - originX >= half ? 1U : 0U;
                const uint32_t qz = center[2] - originZ >= half ? 1U : 0U;
                quadrants[qz * 2U + qx].push_back(index);
            }
            for (auto& quadrant : quadrants)
                if (!quadrant.empty()) self(self, std::move(quadrant), half);
        };
        for (auto& [key, members] : cells) split(split, std::move(members), cards.CellSize);
        cards.Cards.resize(groups.size());
        RunGrassParallelWork(workers, [&](size_t worker) {
            const size_t first = groups.size() * worker / workers;
            const size_t last = groups.size() * (worker + 1U) / workers;
            for (size_t index = first; index < last; ++index) cards.Cards[index] = CellCard(placement, groups[index]);
        });
        std::erase_if(cards.Cards, [](const GrassCardRecord& card) { return card.ExtentPhase[2] <= 0.0F; });
        OrderAndChunkCards(cards);
    }
    return set;
}

std::array<GrassCardLevelRanges, kGrassCardLevelCount> AppendGrassCards(
    const GrassCardSet& set, uint32_t shadowPointBase, bool shadowGrid, GrassCardStream& stream) {
    std::array<GrassCardLevelRanges, kGrassCardLevelCount> ranges{};
    for (uint32_t level = 0; level < kGrassCardLevelCount; ++level) {
        auto& range = ranges[level];
        range.Cards.First = static_cast<uint32_t>(stream.Records.size());
        range.Chunks.First = static_cast<uint32_t>(stream.Chunks.size());
        for (auto card : set.Levels[level].Cards) {
            if (!shadowGrid || card.ShadowFirst == kGrassCardNoShadow) {
                card.ShadowFirst = kGrassCardNoShadow;
                card.ShadowSecond = kGrassCardNoShadow;
                card.ShadowWeight = 0.0F;
            } else {
                card.ShadowFirst += shadowPointBase;
                card.ShadowSecond += shadowPointBase;
            }
            stream.Records.push_back(card);
        }
        for (auto chunk : set.Levels[level].Chunks) {
            chunk.First += range.Cards.First;
            stream.Chunks.push_back(chunk);
        }
        range.Cards.Count = static_cast<uint32_t>(stream.Records.size()) - range.Cards.First;
        range.Chunks.Count = static_cast<uint32_t>(stream.Chunks.size()) - range.Chunks.First;
    }
    return ranges;
}

float GrassCardReach(float height, float footprint, float largestSetRatio) noexcept {
    // The tile set nearest the card's height/radius ratio lies within a factor
    // of two of it unless the card is taller than the tallest set, whose tile
    // then has radius height / ratio; the depth scale adds the height, the
    // vertex frustum test takes twice the depth scale.
    const float tileRadius = std::max(2.0F * footprint, height / std::max(largestSetRatio, 1.0e-3F));
    return 2.0F * (tileRadius + height);
}

void SelectGrassCardChunks(std::span<const GrassCardChunk> chunks, const GrassCardLodPolicy& policy, uint32_t level,
                           const GrassCardCullView& view, std::vector<GrassCardRange>& runs) {
    if (!policy.Enabled || level >= kGrassCardLevelCount) return;
    const std::array<float, 3> ends{policy.BladeDistance, policy.ClumpDistance, policy.GroupDistance};
    // The weight is positive strictly inside (enter (1 - blend), leave).
    const float enter = ends[level] * (1.0F - policy.BlendFraction);
    const float leave = level + 1U < kGrassCardLevelCount ? ends[level + 1U] : std::numeric_limits<float>::infinity();
    GrassCardRange run;
    for (const auto& chunk : chunks) {
        float squared = 0.0F;
        for (size_t axis = 0; axis < 3U; ++axis) {
            const float delta = chunk.Center[axis] - view.Eye[axis];
            squared += delta * delta;
        }
        const float distance = std::sqrt(squared);
        const bool inBand = distance + chunk.RootRadius > enter && distance - chunk.RootRadius < leave;
        const float reach = chunk.RootRadius + GrassCardReach(chunk.MaxHeight, chunk.MaxFootprint, view.LargestSetRatio);
        const bool visible = inBand && (!view.FrustumCulling ||
            GrassSphereIntersectsFrustum(view.PositionToClip, view.RadiusScale, chunk.Center, reach));
        if (visible && run.Count != 0U && run.First + run.Count == chunk.First) {
            run.Count += chunk.Count;
            continue;
        }
        if (run.Count != 0U) runs.push_back(run);
        run = visible ? GrassCardRange{chunk.First, chunk.Count} : GrassCardRange{};
    }
    if (run.Count != 0U) runs.push_back(run);
}

} // namespace Fast::Oot3d
