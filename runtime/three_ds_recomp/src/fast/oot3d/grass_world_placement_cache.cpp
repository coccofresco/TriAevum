#include "fast/oot3d/grass_world_placement_cache.h"
#include "fast/oot3d/grass_visibility.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <utility>

namespace Fast::Oot3d {
namespace {

constexpr uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

template <typename Value> void HashValue(uint64_t& hash, const Value& value) noexcept {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    for (size_t index = 0U; index < sizeof(Value); ++index) {
        hash = (hash ^ bytes[index]) * kFnvPrime;
    }
}

uint64_t TransformVersion(const GrassWorldPlacementRequest& request) noexcept {
    uint64_t hash = kFnvOffset;
    HashValue(hash, request.ColorSource.ContentVersion());
    HashValue(hash, request.ColorWrapS);
    HashValue(hash, request.ColorWrapT);
    for (const float value : request.ModelToWorld) {
        HashValue(hash, std::bit_cast<uint32_t>(value));
    }
    HashValue(hash, request.TransformBakedIntoVertices);
    HashValue(hash, std::bit_cast<uint32_t>(request.NormalOffset));
    HashValue(hash, std::bit_cast<uint32_t>(request.HeightScale));
    HashValue(hash, request.Anchors.size());
    HashValue(hash, request.Clusters.size());
    return hash == 0U ? 1U : hash;
}

std::array<float, 3> Normalize(std::array<float, 3> value) noexcept {
    const float length = std::sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
    if (!std::isfinite(length) || length <= 1.0e-6F) {
        return {};
    }
    for (float& component : value) {
        component /= length;
    }
    return value;
}

std::array<float, 3> TransformPoint(const GrassWorldPlacementRequest& request,
                                    const std::array<float, 3>& point) noexcept {
    if (request.TransformBakedIntoVertices) {
        return point;
    }
    const auto& matrix = request.ModelToWorld;
    const float w = matrix[12] * point[0] + matrix[13] * point[1] + matrix[14] * point[2] + matrix[15];
    const float inverseW = std::abs(w) > 1.0e-6F ? 1.0F / w : 1.0F;
    return {
        (matrix[0] * point[0] + matrix[1] * point[1] + matrix[2] * point[2] + matrix[3]) * inverseW,
        (matrix[4] * point[0] + matrix[5] * point[1] + matrix[6] * point[2] + matrix[7]) * inverseW,
        (matrix[8] * point[0] + matrix[9] * point[1] + matrix[10] * point[2] + matrix[11]) * inverseW,
    };
}

std::array<float, 3> TransformNormal(const GrassWorldPlacementRequest& request,
                                     const std::array<float, 3>& normal) noexcept {
    if (request.TransformBakedIntoVertices) {
        return normal;
    }
    const auto& matrix = request.ModelToWorld;
    return Normalize({
        matrix[0] * normal[0] + matrix[1] * normal[1] + matrix[2] * normal[2],
        matrix[4] * normal[0] + matrix[5] * normal[1] + matrix[6] * normal[2],
        matrix[8] * normal[0] + matrix[9] * normal[1] + matrix[10] * normal[2],
    });
}

float MaximumModelScale(const GrassWorldPlacementRequest& request) noexcept {
    if (request.TransformBakedIntoVertices) {
        return 1.0F;
    }
    const auto& matrix = request.ModelToWorld;
    float maximum = 0.0F;
    for (size_t column = 0U; column < 3U; ++column) {
        maximum =
            std::max(maximum, std::sqrt(matrix[column] * matrix[column] + matrix[4U + column] * matrix[4U + column] +
                                        matrix[8U + column] * matrix[8U + column]));
    }
    return std::max(maximum, 1.0e-6F);
}

void BuildVisibilityIndex(GrassWorldPlacement& placement) {
    auto& order = placement.VisibilityClusterOrder;
    auto& nodes = placement.VisibilityNodes;
    order.resize(placement.Clusters.size());
    std::iota(order.begin(), order.end(), 0U);
    nodes.reserve(order.size() / 4U + 1U);
    const auto build = [&](auto&& self, uint32_t first, uint32_t count) -> void {
        const uint32_t nodeIndex = static_cast<uint32_t>(nodes.size());
        nodes.emplace_back();
        std::array<float, 3> minimum;
        std::array<float, 3> maximum;
        minimum.fill(std::numeric_limits<float>::max());
        maximum.fill(std::numeric_limits<float>::lowest());
        float height = 0.0F;
        float minimumVisibility = 1.0F;
        for (uint32_t index = first; index < first + count; ++index) {
            const auto& cluster = placement.Clusters[order[index]];
            for (size_t axis = 0; axis < 3U; ++axis) {
                minimum[axis] = std::min(minimum[axis], cluster.Center[axis] - cluster.Radius);
                maximum[axis] = std::max(maximum[axis], cluster.Center[axis] + cluster.Radius);
            }
            height = std::max(height, cluster.MaximumBladeHeight);
            minimumVisibility = std::min(minimumVisibility, cluster.MinimumStableVisibility);
        }
        float radiusSquared = 0.0F;
        size_t splitAxis = 0U;
        for (size_t axis = 0; axis < 3U; ++axis) {
            nodes[nodeIndex].Center[axis] = (minimum[axis] + maximum[axis]) * 0.5F;
            const float halfExtent = (maximum[axis] - minimum[axis]) * 0.5F;
            radiusSquared += halfExtent * halfExtent;
            if (maximum[axis] - minimum[axis] > maximum[splitAxis] - minimum[splitAxis]) splitAxis = axis;
        }
        nodes[nodeIndex].Radius = std::sqrt(radiusSquared);
        nodes[nodeIndex].MaximumBladeHeight = height;
        nodes[nodeIndex].MinimumStableVisibility = minimumVisibility;
        if (count <= 16U) {
            nodes[nodeIndex].FirstCluster = first;
            nodes[nodeIndex].ClusterCount = count;
        } else {
            const uint32_t leftCount = count / 2U;
            std::nth_element(order.begin() + first, order.begin() + first + leftCount,
                order.begin() + first + count, [&](uint32_t a, uint32_t b) {
                    const float x = placement.Clusters[a].Center[splitAxis];
                    const float y = placement.Clusters[b].Center[splitAxis];
                    return x == y ? a < b : x < y;
                });
            self(self, first, leftCount);
            self(self, first + leftCount, count - leftCount);
        }
        nodes[nodeIndex].Escape = static_cast<uint32_t>(nodes.size());
    };
    if (!order.empty()) build(build, 0U, static_cast<uint32_t>(order.size()));
}

GrassWorldPlacement BuildPlacement(const GrassWorldPlacementRequest& request, uint64_t transformVersion) {
    GrassWorldPlacement result;
    result.Identity = request.Identity;
    uint64_t contentVersion = kFnvOffset;
    HashValue(contentVersion, request.ContentVersion);
    HashValue(contentVersion, transformVersion);
    result.ContentVersion = contentVersion == 0U ? 1U : contentVersion;
    result.LastUsedFrame = request.FrameId;
    result.Anchors.resize(request.Anchors.size());
    result.CullingAnchors.resize(request.Anchors.size());
    for (size_t index = 0U; index < request.Anchors.size(); ++index) {
        const auto& source = request.Anchors[index];
        auto base = TransformPoint(request, source.LocalPosition);
        auto normal = TransformNormal(request, source.LocalNormal);
        if (normal == std::array<float, 3>{}) {
            normal = { 0.0F, 1.0F, 0.0F };
        }
        for (size_t axis = 0U; axis < 3U; ++axis) {
            base[axis] += normal[axis] * request.NormalOffset;
        }
        auto& destination = result.Anchors[index];
        destination.BaseHeight = { base[0], base[1], base[2], source.BladeHeight * request.HeightScale };
        destination.HalfWidthPhase = { source.BladeWidth * 0.5F, source.Phase };
        destination.PackedWidthAxis = PackGrassDirection(source.WidthAxis);
        destination.PackedWorldNormal = PackGrassNormal(normal);
        destination.StableId = source.StableId;
        destination.SurfaceReference = source.SurfaceReference;
        destination.SurfaceColor = request.ColorSource.SamplePacked(
            source.Uv[0], source.Uv[1], request.ColorWrapS, request.ColorWrapT);
        result.CullingAnchors[index] = {
            { destination.BaseHeight[0], destination.BaseHeight[1], destination.BaseHeight[2] },
            GrassStableVisibilityValue(source.StableId),
        };
    }

    const float modelScale = MaximumModelScale(request);
    result.Clusters.reserve(request.Clusters.size());
    for (const auto& source : request.Clusters) {
        const auto center = TransformPoint(request, source.LocalCenter);
        result.Clusters.push_back({
            source.FirstAnchor,
            std::min<uint32_t>(source.AnchorCount,
                               source.FirstAnchor < result.Anchors.size()
                                   ? static_cast<uint32_t>(result.Anchors.size() - source.FirstAnchor)
                                   : 0U),
            center,
            modelScale * source.LocalRadius + source.MaximumBladeHeight * request.HeightScale +
                std::abs(request.NormalOffset),
            source.MaximumBladeHeight * request.HeightScale,
            source.FirstAnchor < result.CullingAnchors.size() ?
                result.CullingAnchors[source.FirstAnchor].StableVisibility : 1.0F,
        });
    }
    BuildVisibilityIndex(result);
    return result;
}

struct CacheKey {
    uint64_t Identity = 0U;
    uint64_t Transform = 0U;

    bool operator==(const CacheKey&) const = default;
};

struct CacheKeyHash {
    size_t operator()(const CacheKey& key) const noexcept {
        uint64_t hash = kFnvOffset;
        HashValue(hash, key.Identity);
        HashValue(hash, key.Transform);
        return static_cast<size_t>(hash);
    }
};

} // namespace

GrassWorldPlacement BuildGrassWorldPlacement(const GrassWorldPlacementRequest& request) {
    return BuildPlacement(request, TransformVersion(request));
}

struct GrassWorldPlacementCache::Impl {
    struct Entry {
        uint64_t SourceContentVersion = 0U;
        Placement Value;
    };

    explicit Impl(size_t capacity) : Capacity(std::max<size_t>(capacity, 1U)) {
    }

    size_t Capacity = 1U;
    std::unordered_map<CacheKey, Entry, CacheKeyHash> Entries;
    GrassWorldPlacementCacheStats Statistics;
};

GrassWorldPlacementCache::GrassWorldPlacementCache(size_t capacity) : mImpl(std::make_unique<Impl>(capacity)) {
}

GrassWorldPlacementCache::~GrassWorldPlacementCache() = default;

GrassWorldPlacementCache::Placement GrassWorldPlacementCache::Resolve(const GrassWorldPlacementRequest& request) {
    if (request.Identity == 0U || request.ContentVersion == 0U || request.Anchors.empty()) {
        return nullptr;
    }
    const uint64_t transformVersion = TransformVersion(request);
    const CacheKey key{ request.Identity, transformVersion };
    const auto found = mImpl->Entries.find(key);
    if (found != mImpl->Entries.end() && found->second.SourceContentVersion == request.ContentVersion) {
        found->second.Value->LastUsedFrame = request.FrameId;
        ++mImpl->Statistics.Hits;
        mImpl->Statistics.Entries = mImpl->Entries.size();
        return found->second.Value;
    }

    auto placement = std::make_shared<GrassWorldPlacement>(BuildPlacement(request, transformVersion));
    ++mImpl->Statistics.Misses;
    if (found != mImpl->Entries.end()) {
        ++mImpl->Statistics.Updates;
        found->second = { request.ContentVersion, placement };
    } else {
        if (mImpl->Entries.size() >= mImpl->Capacity) {
            const auto oldest =
                std::min_element(mImpl->Entries.begin(), mImpl->Entries.end(), [](const auto& left, const auto& right) {
                    return left.second.Value->LastUsedFrame < right.second.Value->LastUsedFrame;
                });
            if (oldest != mImpl->Entries.end()) {
                mImpl->Entries.erase(oldest);
                ++mImpl->Statistics.Evictions;
            }
        }
        mImpl->Entries.emplace(key, Impl::Entry{ request.ContentVersion, placement });
    }
    mImpl->Statistics.Entries = mImpl->Entries.size();
    return placement;
}

GrassWorldPlacementCacheStats GrassWorldPlacementCache::Stats() const {
    auto stats = mImpl->Statistics;
    stats.Entries = mImpl->Entries.size();
    return stats;
}

void GrassWorldPlacementCache::Clear() {
    mImpl->Entries.clear();
    mImpl->Statistics = {};
}

} // namespace Fast::Oot3d
