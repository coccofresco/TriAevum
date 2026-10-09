#include "fast/oot3d/grass_world_placement_cache.h"
#include "fast/oot3d/grass_parallel_work.h"
#include "fast/oot3d/grass_visibility.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <stdexcept>
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
    if (request.ShadowGrid) HashValue(hash, request.ShadowGrid);
    for (const float value : request.ModelToWorld) {
        HashValue(hash, std::bit_cast<uint32_t>(value));
    }
    HashValue(hash, request.TransformBakedIntoVertices);
    HashValue(hash, std::bit_cast<uint32_t>(request.NormalOffset));
    HashValue(hash, std::bit_cast<uint32_t>(request.HeightScale));
    if (request.MidrangeCellExtent != 0.0F)
        HashValue(hash, std::bit_cast<uint32_t>(request.MidrangeCellExtent));
    if (request.MidrangeAdaptive) {
        HashValue(hash, request.MidrangeAdaptive);
        HashValue(hash, request.MidrangeAdaptiveCapacity);
        if (request.MidrangeMask && request.MidrangeMaskRule) {
            HashValue(hash, request.MidrangeMask->Width);
            HashValue(hash, request.MidrangeMask->Height);
            for (uint8_t sample : request.MidrangeMask->Samples) HashValue(hash,sample);
            HashValue(hash,GrassPlacementRuleVersion(*request.MidrangeMaskRule,GrassGenerationSettings{}));
        }
    }
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

// A colour texel is reusable across repetitions; a shadow receiver is not.
// Key each sample by its source triangle and unwrapped UV point, within this
// immutable world placement. UV seams/overlapping islands never alias.
class ShadowGridBuilder {
    struct Triangle {
        std::array<std::array<float, 3>, 3> Position{};
        std::array<std::array<float, 2>, 3> Uv{};
        std::array<float, 3> Normal{};
        double Determinant = 0.0;
        uint32_t SurfaceIndex = 0;
    };
    struct Key {
        uint64_t TriangleId = 0;
        uint64_t Uv = 0;
        bool operator==(const Key&) const = default;
    };
    struct Hash {
        size_t operator()(const Key& key) const noexcept {
            uint64_t hash = kFnvOffset;
            HashValue(hash, key.TriangleId);
            HashValue(hash, key.Uv);
            return static_cast<size_t>(hash);
        }
    };
    const GrassWorldPlacementRequest& Request;
    std::vector<GrassShadowGridPoint>& Points;
    std::vector<GrassShadowSurface>& Surfaces;
    std::unordered_map<uint64_t, Triangle> Triangles;
    std::unordered_map<Key, uint32_t, Hash> Samples;

    static std::array<double, 3> Weights(const Triangle& triangle, const std::array<float, 2>& uv) {
        const auto& p = triangle.Uv;
        if (std::abs(triangle.Determinant) <= 1.0e-12)
            return {1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0};
        const double x = double(uv[0]) - p[0][0], y = double(uv[1]) - p[0][1];
        const double b = (x * (double(p[2][1]) - p[0][1]) - y * (double(p[2][0]) - p[0][0])) / triangle.Determinant;
        const double c = ((double(p[1][0]) - p[0][0]) * y - (double(p[1][1]) - p[0][1]) * x) / triangle.Determinant;
        if (b >= 0 && c >= 0 && b + c <= 1) return {1 - b - c, b, c};
        // Grid centres beyond a triangle must stay on its real surface, not
        // extrapolate across a cliff or into another overlapping UV island.
        double nearest = std::numeric_limits<double>::max();
        std::array<double, 3> result{};
        for (size_t edge = 0; edge < 3; ++edge) {
            const size_t next = (edge + 1) % 3;
            const double dx = double(p[next][0]) - p[edge][0], dy = double(p[next][1]) - p[edge][1];
            const double squared = dx * dx + dy * dy;
            const double t = squared > 0 ? std::clamp(
                ((double(uv[0]) - p[edge][0]) * dx + (double(uv[1]) - p[edge][1]) * dy) / squared, 0.0, 1.0) : 0.0;
            const double ex = p[edge][0] + t * dx - uv[0], ey = p[edge][1] + t * dy - uv[1];
            if (ex * ex + ey * ey < nearest) {
                nearest = ex * ex + ey * ey;
                result = {};
                result[edge] = 1 - t;
                result[next] = t;
            }
        }
        return result;
    }

  public:
    ShadowGridBuilder(const GrassWorldPlacementRequest& request, std::vector<GrassShadowGridPoint>& points,
                      std::vector<GrassShadowSurface>& surfaces)
        : Request(request), Points(points), Surfaces(surfaces) {}

    GrassShadowGridReference Resolve(const GrassAnchor& anchor, const GrassTextureGridSample& sample) {
        if (!sample.Valid) throw std::runtime_error("grass shadow grid requires finite texture coordinates");
        const uint64_t triangleId = uint64_t(anchor.SurfaceReference[0]) |
            (uint64_t(anchor.SurfaceReference[1] & 65535U) << 32U);
        auto [entry, inserted] = Triangles.try_emplace(triangleId);
        auto& triangle = entry->second;
        if (inserted) {
            const std::array<uint32_t, 3> indices{
                anchor.SurfaceReference[0] & 65535U, anchor.SurfaceReference[0] >> 16U,
                anchor.SurfaceReference[1] & 65535U};
            for (size_t i = 0; i < 3; ++i) {
                if (indices[i] >= Request.SourceVertices.size())
                    throw std::runtime_error("grass shadow grid source triangle is unavailable");
                const auto& vertex = Request.SourceVertices[indices[i]];
                triangle.Position[i] = TransformPoint(Request, vertex.Position);
                triangle.Uv[i] = vertex.Uv;
            }
            const auto& uv = triangle.Uv;
            triangle.Determinant = (double(uv[1][0]) - uv[0][0]) * (double(uv[2][1]) - uv[0][1]) -
                (double(uv[1][1]) - uv[0][1]) * (double(uv[2][0]) - uv[0][0]);
            std::array<float, 3> a{}, b{};
            for (size_t axis = 0; axis < 3; ++axis) {
                a[axis] = triangle.Position[1][axis] - triangle.Position[0][axis];
                b[axis] = triangle.Position[2][axis] - triangle.Position[0][axis];
            }
            triangle.Normal = Normalize({a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]});
            if (Surfaces.size() >= std::numeric_limits<uint32_t>::max())
                throw std::runtime_error("grass shadow surfaces exceed uint32 addressing");
            triangle.SurfaceIndex = static_cast<uint32_t>(Surfaces.size());
            GrassShadowSurface surface;
            for (size_t vertex = 0; vertex < 3; ++vertex)
                std::copy(triangle.Position[vertex].begin(), triangle.Position[vertex].end(),
                          surface.Position[vertex].begin());
            std::copy(triangle.Normal.begin(), triangle.Normal.end(), surface.Normal.begin());
            Surfaces.push_back(surface);
        }
        std::array<uint32_t, 2> indices{};
        for (size_t i = 0; i < 2; ++i) {
            auto uv = UnwrapGrassTextureGridPoint(sample.Points[i], anchor.Uv, Request.ColorWrapS, Request.ColorWrapT);
            // A collapsed UV triangle has no spatial texture gradient. Its
            // single receiver is the actual geometric centroid, not a root.
            if (std::abs(triangle.Determinant) <= 1.0e-12) uv = {};
            const Key key{triangleId, uint64_t(std::bit_cast<uint32_t>(uv[0])) |
                (uint64_t(std::bit_cast<uint32_t>(uv[1])) << 32U)};
            auto [point, created] = Samples.try_emplace(key, static_cast<uint32_t>(Points.size()));
            if (created) {
                if (Points.size() >= std::numeric_limits<uint32_t>::max())
                    throw std::runtime_error("grass shadow grid exceeds uint32 addressing");
                const auto weights = Weights(triangle, uv);
                GrassShadowGridPoint value;
                for (size_t axis = 0; axis < 3; ++axis) {
                    value.Position[axis] = static_cast<float>(
                        triangle.Position[0][axis] * weights[0] +
                        triangle.Position[1][axis] * weights[1] +
                        triangle.Position[2][axis] * weights[2]);
                    value.Normal[axis] = triangle.Normal[axis];
                }
                value.Position[3] = 1.0F;
                value.Normal[3] = std::bit_cast<float>(triangle.SurfaceIndex);
                Points.push_back(value);
            }
            indices[i] = point->second;
        }
        return {indices[0], indices[1], sample.Weight};
    }
};

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
    ShadowGridBuilder shadowGrid(request, result.ShadowPoints, result.ShadowSurfaces);
    if (request.ShadowGrid) result.ShadowReferences.resize(request.Anchors.size());
    // Each anchor is independent except for the shadow grid, which appends
    // shared points in anchor order: it runs afterwards, in that order.
    std::vector<GrassTextureGridSample> samples(request.ShadowGrid ? request.Anchors.size() : 0U);
    const size_t workers = GrassParallelWorkerCount(request.Anchors.size());
    RunGrassParallelWork(workers, [&](size_t worker) {
        const size_t first = request.Anchors.size() * worker / workers;
        const size_t last = request.Anchors.size() * (worker + 1U) / workers;
        for (size_t index = first; index < last; ++index) {
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
            if (request.ShadowGrid) {
                samples[index] = ResolveGrassTextureGridSample(
                    source.Uv[0], source.Uv[1], request.ColorWrapS, request.ColorWrapT);
                destination.SurfaceColor = request.ColorSource.SamplePacked(samples[index]);
            } else {
                destination.SurfaceColor = request.ColorSource.SamplePacked(
                    source.Uv[0], source.Uv[1], request.ColorWrapS, request.ColorWrapT);
            }
            result.CullingAnchors[index] = {
                { destination.BaseHeight[0], destination.BaseHeight[1], destination.BaseHeight[2] },
                GrassStableVisibilityValue(source.StableId),
            };
        }
    });
    if (request.ShadowGrid) {
        for (size_t index = 0U; index < request.Anchors.size(); ++index)
            result.ShadowReferences[index] = shadowGrid.Resolve(request.Anchors[index], samples[index]);
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
    if (request.MidrangeCellExtent != 0.0F) {
        std::optional<GrassMaskInterior> interior;
        if (request.MidrangeAdaptive && request.MidrangeMask && request.MidrangeMaskRule) {
            const auto& mask = *request.MidrangeMask;
            interior.emplace(mask.Width,mask.Height,mask.Samples,[&](uint8_t sample) {
                // Grayscale probability already shaped the immutable roots.
                // Only excluded texels mark a boundary for geometric grouping.
                return EvaluateGrassMaskLevel(sample/255.0F,*request.MidrangeMaskRule) > 0.0F;
            }, [&](float u, float v) { return GrassMaskPositionActive(*request.MidrangeMaskRule, u, v); });
            if (std::getenv("OOT3D_GRASS_DIAGNOSTICS")) {
                std::array<uint64_t,256> histogram{};
                for (uint8_t sample : mask.Samples) ++histogram[sample];
                uint64_t zero=0,partial=0,full=0;
                for (uint32_t sample=0;sample<256;++sample) {
                    const float level=EvaluateGrassMaskLevel(sample/255.0F,*request.MidrangeMaskRule);
                    (level<=0 ? zero : level>=1 ? full : partial)+=histogram[sample];
                }
                std::fprintf(stderr,"[grass-adaptive-mask] rule=%llu size=%ux%u tiles=%u/%u zero=%llu partial=%llu full=%llu\n",
                    static_cast<unsigned long long>(request.MidrangeMaskRule->RuleId),mask.Width,mask.Height,
                    interior->InteriorTiles(),interior->TileCount(),static_cast<unsigned long long>(zero),
                    static_cast<unsigned long long>(partial),static_cast<unsigned long long>(full));
            }
        }
        result.Midrange = BuildGrassMidrangeClusters(result.Anchors.size(), request.MidrangeCellExtent,
            [&](uint32_t index) {
                const auto& anchor = result.Anchors[index];
                const uint64_t triangle = uint64_t(anchor.SurfaceReference[0]) |
                    (uint64_t(anchor.SurfaceReference[1] & 0xffffU) << 32U);
                return GrassMidrangeRoot{{anchor.BaseHeight[0], anchor.BaseHeight[1], anchor.BaseHeight[2]},
                                         anchor.StableId, triangle,
                                         anchor.BaseHeight[3] + anchor.HalfWidthPhase[0] * 2.0F,
                                         result.CullingAnchors[index].StableVisibility, request.Anchors[index].Uv};
            }, request.MidrangeAdaptive, [&](std::array<float,2> lo,std::array<float,2> hi) {
                if (!interior) return false;
                for (size_t axis=0; axis<2; ++axis) {
                    if (!std::isfinite(lo[axis]) || !std::isfinite(hi[axis])) return false;
                    const auto wrap=axis==0 ? request.ColorWrapS : request.ColorWrapT;
                    if (wrap!=GrassTextureWrap::Clamp && std::floor(lo[axis])!=std::floor(hi[axis])) {
                        lo[axis]=0; hi[axis]=1;
                    } else {
                        const float a=WrapGrassTextureCoordinate(lo[axis],wrap);
                        const float b=WrapGrassTextureCoordinate(hi[axis],wrap);
                        lo[axis]=std::min(a,b); hi[axis]=std::max(a,b);
                    }
                }
                return interior->Contains(lo,hi);
            }, request.MidrangeAdaptiveCapacity);
    }
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
