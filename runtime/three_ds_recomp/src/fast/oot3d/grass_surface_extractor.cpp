#include "fast/oot3d/grass_surface_extractor.h"
#include "fast/oot3d/grass_overlay_mask.h"
#include "fast/oot3d/grass_parallel_work.h"
#include "fast/oot3d/grass_surface_reference.h"
#include "fast/oot3d/grass_visibility.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <compare>
#include <exception>
#include <limits>
#include <mutex>
#include <numbers>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

namespace Fast::Oot3d {
namespace {

uint32_t NextRandom(uint32_t& state) {
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return state;
}

float UnitRandom(uint32_t& state) {
    return static_cast<float>(NextRandom(state) >> 8U) /
           static_cast<float>(1U << 24U);
}

float SampleMask(const GrassScalarMask& mask, float u, float v,
                 GrassTextureWrap wrapS, GrassTextureWrap wrapT) {
    if (mask.Samples.empty() || mask.Width == 0 || mask.Height == 0) return 0.0F;
    u = WrapGrassTextureCoordinate(u, wrapS);
    v = WrapGrassTextureCoordinate(v, wrapT);
    const uint32_t x = std::min<uint32_t>(
        static_cast<uint32_t>(u * mask.Width), mask.Width - 1U);
    const uint32_t y = std::min<uint32_t>(
        static_cast<uint32_t>(v * mask.Height), mask.Height - 1U);
    return mask.Samples[static_cast<size_t>(y) * mask.Width + x] / 255.0F;
}

std::array<float, 3> Subtract(const std::array<float, 3>& a,
                              const std::array<float, 3>& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

std::array<float, 3> Cross(const std::array<float, 3>& a,
                           const std::array<float, 3>& b) {
    return {a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0]};
}

float Length(const std::array<float, 3>& value) {
    return std::sqrt(value[0] * value[0] + value[1] * value[1] +
                     value[2] * value[2]);
}

std::array<float, 3> TransformPoint(
    const GrassSourceSurface& surface,
    const std::array<float, 3>& point) {
    if (surface.TransformBakedIntoVertices) {
        return point;
    }
    const auto& matrix = surface.ModelToWorld;
    const float w =
        matrix[12] * point[0] + matrix[13] * point[1] +
        matrix[14] * point[2] + matrix[15];
    const float inverseW =
        std::abs(w) > 1.0e-6F ? 1.0F / w : 1.0F;
    return {
        (matrix[0] * point[0] + matrix[1] * point[1] +
         matrix[2] * point[2] + matrix[3]) *
            inverseW,
        (matrix[4] * point[0] + matrix[5] * point[1] +
         matrix[6] * point[2] + matrix[7]) *
            inverseW,
        (matrix[8] * point[0] + matrix[9] * point[1] +
         matrix[10] * point[2] + matrix[11]) *
            inverseW,
    };
}

uint32_t HashCoordinates(int32_t x, int32_t y, uint32_t seed) {
    uint32_t value = seed;
    value ^= static_cast<uint32_t>(x) * 0x9e3779b9U;
    value ^= static_cast<uint32_t>(y) * 0x85ebca6bU;
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    value ^= value >> 16U;
    return value;
}

float HashUnit(int32_t x, int32_t y, uint32_t seed) {
    return static_cast<float>(HashCoordinates(x, y, seed) >> 8U) /
           static_cast<float>(1U << 24U);
}

float Smooth(float value) {
    value = std::clamp(value, 0.0F, 1.0F);
    return value * value * (3.0F - 2.0F * value);
}

float ValueNoise(float x, float y, uint32_t seed) {
    const int32_t x0 = static_cast<int32_t>(std::floor(x));
    const int32_t y0 = static_cast<int32_t>(std::floor(y));
    const float tx = Smooth(x - static_cast<float>(x0));
    const float ty = Smooth(y - static_cast<float>(y0));
    const float a = std::lerp(
        HashUnit(x0, y0, seed), HashUnit(x0 + 1, y0, seed), tx);
    const float b = std::lerp(
        HashUnit(x0, y0 + 1, seed),
        HashUnit(x0 + 1, y0 + 1, seed), tx);
    return std::lerp(a, b, ty);
}

float ClusterWeight(const GrassGenerationSettings& generation,
                    const std::array<float, 3>& worldPosition) {
    if (generation.ClusterStrength <= 0.0F) {
        return 1.0F;
    }
    const float scale = std::max(generation.ClusterScale, 1.0F);
    const float x = worldPosition[0] / scale;
    const float z = worldPosition[2] / scale;
    const uint32_t seed = generation.Seed ^ 0x6d2b79f5U;
    const float coarse = ValueNoise(x, z, seed);
    const float detail = ValueNoise(
        x * 2.0F, z * 2.0F, seed ^ 0xa511e9b3U);
    const float noise = coarse * 0.72F + detail * 0.28F;
    const float threshold =
        1.0F - std::clamp(
                     generation.ClusterCoverage, 0.0F, 1.0F);
    constexpr float kGroupEdgeWidth = 0.12F;
    const float group = Smooth(
        (noise - (threshold - kGroupEdgeWidth)) /
        (2.0F * kGroupEdgeWidth));
    return std::lerp(
        1.0F, group,
        std::clamp(generation.ClusterStrength, 0.0F, 1.0F));
}

struct SpatialCell {
    int32_t X = 0;
    int32_t Y = 0;
    int32_t Z = 0;
    auto operator<=>(const SpatialCell&) const = default;
};

struct SpatialCellHash {
    size_t operator()(const SpatialCell& cell) const noexcept {
        uint64_t hash = static_cast<uint32_t>(cell.X);
        hash ^= static_cast<uint64_t>(
                    static_cast<uint32_t>(cell.Y)) +
                0x9e3779b97f4a7c15ULL + (hash << 6U) +
                (hash >> 2U);
        hash ^= static_cast<uint64_t>(
                    static_cast<uint32_t>(cell.Z)) +
                0x9e3779b97f4a7c15ULL + (hash << 6U) +
                (hash >> 2U);
        return static_cast<size_t>(hash);
    }
};

SpatialCell CellFor(const std::array<float, 3>& position,
                    float cellSize) {
    return {
        static_cast<int32_t>(std::floor(position[0] / cellSize)),
        static_cast<int32_t>(std::floor(position[1] / cellSize)),
        static_cast<int32_t>(std::floor(position[2] / cellSize)),
    };
}

// A blade that passed its mask and acceptance test, before the spacing test.
struct PendingAnchor {
    GrassAnchor Anchor;
    std::array<float, 3> World{};
};

// Sorts by a strict total order: chunks in parallel, then pairwise merges.
template <typename Value, typename Less>
void SortInParallel(std::vector<Value>& values, Less less, size_t workers) {
    workers = std::clamp<size_t>(workers, 1U, std::max<size_t>(values.size() / 4096U, 1U));
    std::vector<size_t> bounds(workers + 1U);
    for (size_t chunk = 0; chunk <= workers; ++chunk) bounds[chunk] = values.size() * chunk / workers;
    RunGrassParallelWork(workers, [&](size_t chunk) {
        std::sort(values.begin() + static_cast<std::ptrdiff_t>(bounds[chunk]),
                  values.begin() + static_cast<std::ptrdiff_t>(bounds[chunk + 1U]), less);
    });
    for (size_t width = 1U; width < workers; width *= 2U) {
        std::vector<size_t> merges;
        for (size_t chunk = 0; chunk + width < workers; chunk += 2U * width) merges.push_back(chunk);
        RunGrassParallelWork(merges.size(), [&](size_t merge) {
            const size_t chunk = merges[merge];
            const auto begin = values.begin();
            std::inplace_merge(begin + static_cast<std::ptrdiff_t>(bounds[chunk]),
                               begin + static_cast<std::ptrdiff_t>(bounds[chunk + width]),
                               begin + static_cast<std::ptrdiff_t>(bounds[std::min(chunk + 2U * width, workers)]),
                               less);
        });
    }
}

// Orders cells by X, then Z, as signed values.
uint64_t ColumnKey(int64_t x, int64_t z) noexcept {
    return (static_cast<uint64_t>(static_cast<uint32_t>(static_cast<int32_t>(x)) ^ 0x80000000U) << 32U) |
           (static_cast<uint32_t>(static_cast<int32_t>(z)) ^ 0x80000000U);
}

// The minimum spacing of the sequential walk: a blade is kept unless a kept
// blade before it lies within the spacing (in its own or a neighbouring cell
// column and cell layer). Each blade's earlier neighbours are found in
// parallel by stripes of cell columns; the few blades that have any are then
// decided in index order, which is all the walk's order ever decided.
std::vector<uint8_t> ResolveMinimumSpacing(const std::vector<PendingAnchor>& pending, float spacing,
                                           size_t workers) {
    std::vector<uint8_t> kept(pending.size(), 1U);
    if (spacing <= 0.0F || pending.size() < 2U) return kept;
    if (pending.size() > std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("grass candidate count exceeds the spacing index");
    std::vector<SpatialCell> cells(pending.size());
    int64_t minimumX = std::numeric_limits<int64_t>::max();
    int64_t maximumX = std::numeric_limits<int64_t>::min();
    for (size_t index = 0; index < pending.size(); ++index) {
        cells[index] = CellFor(pending[index].World, spacing);
        minimumX = std::min<int64_t>(minimumX, cells[index].X);
        maximumX = std::max<int64_t>(maximumX, cells[index].X);
    }
    const size_t stripes = std::max<size_t>(workers, 1U);
    const int64_t width = (maximumX - minimumX) / static_cast<int64_t>(stripes) + 1;
    const float spacingSquared = spacing * spacing;
    std::vector<std::vector<std::pair<uint32_t, uint32_t>>> conflicts(stripes);
    RunGrassParallelWork(stripes, [&](size_t stripe) {
        const int64_t first = minimumX + static_cast<int64_t>(stripe) * width;
        const int64_t last = first + width;
        // The stripe's blades and a one-column halo, by column, then index.
        std::vector<std::pair<uint64_t, uint32_t>> entries;
        for (size_t index = 0; index < pending.size(); ++index) {
            const int64_t x = cells[index].X;
            if (x >= first - 1 && x <= last)
                entries.emplace_back(ColumnKey(x, cells[index].Z), static_cast<uint32_t>(index));
        }
        std::sort(entries.begin(), entries.end());
        auto& output = conflicts[stripe];
        for (const auto& [ownKey, index] : entries) {
            const auto& center = cells[index];
            if (center.X < first || center.X >= last) continue;
            const auto& world = pending[index].World;
            for (int64_t dx = -1; dx <= 1; ++dx) {
                const int64_t x = static_cast<int64_t>(center.X) + dx;
                const int64_t z = static_cast<int64_t>(center.Z);
                if (x < std::numeric_limits<int32_t>::min() || x > std::numeric_limits<int32_t>::max() ||
                    z - 1 < std::numeric_limits<int32_t>::min() || z + 1 > std::numeric_limits<int32_t>::max())
                    continue;
                const uint64_t lastKey = ColumnKey(x, z + 1);
                for (auto entry = std::lower_bound(entries.begin(), entries.end(),
                                                   std::pair<uint64_t, uint32_t>{ColumnKey(x, z - 1), 0U});
                     entry != entries.end() && entry->first <= lastKey; ++entry) {
                    const uint32_t earlier = entry->second;
                    if (earlier >= index) continue;
                    const auto& cell = cells[earlier];
                    if (static_cast<int64_t>(cell.Y) < static_cast<int64_t>(center.Y) - 1 ||
                        static_cast<int64_t>(cell.Y) > static_cast<int64_t>(center.Y) + 1) continue;
                    const auto& point = pending[earlier].World;
                    const float px = point[0] - world[0];
                    const float py = point[1] - world[1];
                    const float pz = point[2] - world[2];
                    if (px * px + py * py + pz * pz < spacingSquared) output.emplace_back(index, earlier);
                }
            }
        }
    });
    std::vector<std::pair<uint32_t, uint32_t>> ordered;
    for (auto& part : conflicts) ordered.insert(ordered.end(), part.begin(), part.end());
    std::sort(ordered.begin(), ordered.end());
    for (size_t position = 0; position < ordered.size();) {
        const uint32_t index = ordered[position].first;
        bool blocked = false;
        for (; position < ordered.size() && ordered[position].first == index; ++position)
            blocked = blocked || kept[ordered[position].second] != 0U;
        if (blocked) kept[index] = 0U;
    }
    return kept;
}

} // namespace

GrassTextureWrap ResolveGrassTextureWrap(
    GrassWrapOverride overrideMode,
    GrassTextureWrap materialMode) noexcept {
    switch (overrideMode) {
        case GrassWrapOverride::Material:
            return materialMode;
        case GrassWrapOverride::Clamp:
            return GrassTextureWrap::Clamp;
        case GrassWrapOverride::Repeat:
            return GrassTextureWrap::Repeat;
        case GrassWrapOverride::Mirror:
            return GrassTextureWrap::Mirror;
    }
    return materialMode;
}

float WrapGrassTextureCoordinate(
    float coordinate, GrassTextureWrap wrap) noexcept {
    if (!std::isfinite(coordinate)) {
        return 0.0F;
    }
    switch (wrap) {
        case GrassTextureWrap::Clamp:
            return std::clamp(coordinate, 0.0F, 1.0F);
        case GrassTextureWrap::Repeat:
            return coordinate - std::floor(coordinate);
        case GrassTextureWrap::Mirror: {
            float mirrored = std::fmod(coordinate, 2.0F);
            if (mirrored < 0.0F) {
                mirrored += 2.0F;
            }
            return mirrored <= 1.0F
                ? mirrored
                : 2.0F - mirrored;
        }
    }
    return 0.0F;
}

float EvaluateGrassMaskLevel(
    float sample, const GrassPlacementRule& rule) noexcept {
    sample = std::isfinite(sample)
        ? std::clamp(sample * rule.SampleScale + rule.SampleOffset, 0.0F, 1.0F)
        : 0.0F;
    if (rule.Invert) {
        sample = 1.0F - sample;
    }
    const float inputBlack =
        std::clamp(rule.InputBlack, 0.0F, 1.0F - 1.0e-6F);
    const float inputWhite =
        std::clamp(rule.InputWhite, inputBlack + 1.0e-6F, 1.0F);
    const float normalized = std::clamp(
        (sample - inputBlack) /
            std::max(inputWhite - inputBlack, 1.0e-6F),
        0.0F, 1.0F);
    const float shaped = std::pow(
        normalized,
        std::clamp(rule.ResponseExponent, 0.05F, 8.0F));
    const float level = std::lerp(
        std::clamp(rule.OutputBlack, 0.0F, 1.0F),
        std::clamp(rule.OutputWhite, 0.0F, 1.0F),
        shaped);
    // The editor displays an 8-bit grayscale mask. Keep extraction aligned
    // with that contract: a value quantized to black is exactly absent.
    constexpr float kDisplayBlackThreshold = 0.5F / 255.0F;
    return level > kDisplayBlackThreshold ? level : 0.0F;
}

bool GrassMaskPositionActive(
    const GrassPlacementRule& rule, float u, float v) noexcept {
    if (rule.ActiveRects.empty()) {
        return true;
    }
    return std::any_of(
        rule.ActiveRects.begin(), rule.ActiveRects.end(),
        [u, v](const GrassMaskRect& rect) {
            return u >= rect.MinU && u <= rect.MaxU &&
                   v >= rect.MinV && v <= rect.MaxV;
        });
}

float EvaluateGrassMaskLevel(
    float sample, const GrassPlacementRule& rule, float u,
    float v) noexcept {
    return GrassMaskPositionActive(rule, u, v)
        ? EvaluateGrassMaskLevel(sample, rule)
        : 0.0F;
}

uint64_t GrassPlacementRuleVersion(
    const GrassPlacementRule& rule,
    const GrassGenerationSettings& generation) noexcept {
    uint64_t hash = 14695981039346656037ULL;
    const auto mix = [&hash](uint64_t value) {
        for (uint32_t byte = 0U; byte < 8U; ++byte) {
            hash ^= (value >> (byte * 8U)) & 0xFFU;
            hash *= 1099511628211ULL;
        }
    };
    const auto mixFloat = [&mix](float value) {
        mix(std::bit_cast<uint32_t>(value));
    };
    mix(rule.RuleId);
    mix(rule.Target.Rgba8Hash);
    mix(rule.Target.Width);
    mix(rule.Target.Height);
    mix(rule.Target.MapperSlotMask);
    mix(static_cast<uint32_t>(rule.Channel));
    mix(static_cast<uint32_t>(rule.Wrap));
    mix(rule.Invert ? 1U : 0U);
    mixFloat(rule.InputBlack);
    mixFloat(rule.InputWhite);
    mixFloat(rule.ResponseExponent);
    mixFloat(rule.OutputBlack);
    mixFloat(rule.OutputWhite);
    mixFloat(rule.MaximumSlopeDegrees);
    mixFloat(rule.NormalOffset);
    mixFloat(rule.SampleScale);
    mixFloat(rule.SampleOffset);
    mix(rule.ActiveRects.size());
    for (const auto& rect : rule.ActiveRects) {
        mixFloat(rect.MinU);
        mixFloat(rect.MinV);
        mixFloat(rect.MaxU);
        mixFloat(rect.MaxV);
    }
    mixFloat(generation.InstancesPerSquareMeter);
    mixFloat(generation.MinimumSpacing);
    mix(generation.Seed);
    mixFloat(generation.IndividualRandomness);
    mixFloat(generation.ClusterStrength);
    mixFloat(generation.ClusterScale);
    mixFloat(generation.ClusterCoverage);
    mixFloat(generation.BladeHeightMin);
    mixFloat(generation.BladeHeightMax);
    mixFloat(generation.BladeWidthMin);
    mixFloat(generation.BladeWidthMax);
    return hash;
}

static std::vector<GrassAnchor> ExtractAnchors(
    const GrassSourceSurface& surface, const GrassPlacementRule& rule,
    const GrassGenerationSettings& generation,
    const GrassScalarMask& mask, uint32_t budget) {
    std::vector<GrassAnchor> anchors;
    if (surface.Vertices.empty() || surface.Indices.size() < 3 ||
        mask.Samples.empty() || budget == 0) return anchors;
    if (!surface.TriangleSources.empty() && surface.TriangleSources.size() != surface.Indices.size() / 3U)
        return anchors;
    const uint32_t limit = budget;
    anchors.reserve(std::min<uint32_t>(limit, 4096U));
    const GrassTextureWrap wrapS = ResolveGrassTextureWrap(
        rule.Wrap, surface.MaterialWrapS);
    const GrassTextureWrap wrapT = ResolveGrassTextureWrap(
        rule.Wrap, surface.MaterialWrapT);

    // Bound candidate work over the entire eligible surface, not a prefix of
    // its index buffer. Large meshes must keep coverage in every camera shot.
    std::vector<uint32_t> candidateCounts(surface.Indices.size() / 3U);
    std::vector<double> viewWeights(candidateCounts.size());
    double totalViewWeight = 0.0;
    const auto& view = surface.PlacementView;
    const auto frustum = BuildGrassFrustumRadiusScale(view.WorldToClip);
    uint64_t totalCandidates = 0U;
    for (size_t triangle = 0; triangle + 2U < surface.Indices.size(); triangle += 3U) {
        const auto ia = surface.Indices[triangle];
        const auto ib = surface.Indices[triangle + 1U];
        const auto ic = surface.Indices[triangle + 2U];
        if (ia >= surface.Vertices.size() || ib >= surface.Vertices.size() || ic >= surface.Vertices.size()) continue;
        const auto a = TransformPoint(surface, surface.Vertices[ia].Position);
        const auto b = TransformPoint(surface, surface.Vertices[ib].Position);
        const auto c = TransformPoint(surface, surface.Vertices[ic].Position);
        const auto face = Cross(Subtract(b, a), Subtract(c, a));
        const float doubledArea = Length(face);
        if (!std::isfinite(doubledArea) || doubledArea <= 1.0e-6F) continue;
        const float slope = std::acos(std::clamp(std::abs(face[1]) / doubledArea, 0.0F, 1.0F)) *
                            180.0F / std::numbers::pi_v<float>;
        if (slope > rule.MaximumSlopeDegrees) continue;
        const double count = std::ceil(static_cast<double>(doubledArea) * 0.5 * 0.0001 *
                                       generation.InstancesPerSquareMeter);
        if (!std::isfinite(count) || count <= 0.0) continue;
        candidateCounts[triangle / 3U] = static_cast<uint32_t>(
            std::min(count, static_cast<double>(std::numeric_limits<uint32_t>::max())));
        totalCandidates += candidateCounts[triangle / 3U];
        if (view.Enabled) {
            std::array<float, 3> center;
            for (size_t axis = 0; axis < 3U; ++axis) center[axis] = (a[axis] + b[axis] + c[axis]) / 3.0F;
            const float radius = std::max({Length(Subtract(a, center)), Length(Subtract(b, center)),
                                          Length(Subtract(c, center))});
            const float distance = std::max(0.0F, Length(Subtract(center, view.Eye)) - radius);
            if (distance < view.DrawDistance + view.GuardDistance && GrassSphereIntersectsFrustum(
                    view.WorldToClip, frustum, center, radius + view.GuardDistance)) {
                const float full = std::max(view.FullDensityDistance, 1.0F);
                const float ratio = full / std::max(full, distance);
                viewWeights[triangle / 3U] = count * std::max(static_cast<double>(view.FarDensity),
                                                             static_cast<double>(ratio * ratio));
                totalViewWeight += viewWeights[triangle / 3U];
            }
        }
    }
    std::vector<uint32_t> viewQuotas;
    if (view.Enabled && totalViewWeight > 0.0) {
        std::vector<GrassBudgetDemand> demands(candidateCounts.size());
        for (size_t i = 0; i < demands.size(); ++i) {
            // A small coarse reserve keeps unpredicted camera cuts populated
            // while the asynchronous view-focused placement is being prepared.
            demands[i] = {candidateCounts[i], 0.1 * candidateCounts[i] / static_cast<double>(totalCandidates) +
                0.9 * viewWeights[i] / totalViewWeight};
        }
        viewQuotas = AllocateGrassBudget(demands, limit);
    }
    uint64_t candidatePrefix = 0U;
    const auto quotaAt = [totalCandidates, limit](uint64_t prefix) -> uint64_t {
        return totalCandidates <= limit ? prefix : static_cast<uint64_t>(
            static_cast<double>(prefix) * limit / static_cast<double>(totalCandidates));
    };
    std::vector<uint32_t> selectedCounts(candidateCounts.size());
    uint64_t totalSelected = 0U;
    for (size_t triangle = 0; triangle < candidateCounts.size(); ++triangle) {
        const uint32_t candidates = candidateCounts[triangle];
        selectedCounts[triangle] = viewQuotas.empty() ? static_cast<uint32_t>(
            quotaAt(candidatePrefix + candidates) - quotaAt(candidatePrefix)) : viewQuotas[triangle];
        candidatePrefix += candidates;
        totalSelected += selectedCounts[triangle];
    }

    // Every candidate depends only on its (triangle, candidate) pair; only the
    // minimum spacing depends on the order of the blades already kept. The
    // candidates are evaluated in parallel by triangle ranges, each blade's
    // earlier neighbours within the spacing are found in parallel by spatial
    // stripes, and those (rare) conflicts are resolved in the original order:
    // the result equals the sequential walk exactly.
    const size_t workers = GrassParallelWorkerCount(totalSelected);
    std::vector<size_t> rangeBegin(workers + 1U, candidateCounts.size());
    rangeBegin[0] = 0U;
    {
        uint64_t running = 0U;
        size_t next = 1U;
        for (size_t triangle = 0; triangle < candidateCounts.size() && next < workers; ++triangle) {
            running += selectedCounts[triangle];
            while (next < workers && running * workers >= totalSelected * next) rangeBegin[next++] = triangle + 1U;
        }
    }
    std::vector<std::vector<PendingAnchor>> evaluated(workers);
    RunGrassParallelWork(workers, [&](size_t worker) {
        auto& output = evaluated[worker];
        for (size_t triangleIndex = rangeBegin[worker]; triangleIndex < rangeBegin[worker + 1U]; ++triangleIndex) {
            const uint32_t selectedCandidates = selectedCounts[triangleIndex];
            if (selectedCandidates == 0U) continue;
            const size_t triangle = triangleIndex * 3U;
            const uint32_t ia = surface.Indices[triangle];
            const uint32_t ib = surface.Indices[triangle + 1];
            const uint32_t ic = surface.Indices[triangle + 2];
            if (ia >= surface.Vertices.size() || ib >= surface.Vertices.size() ||
                ic >= surface.Vertices.size()) continue;
            const auto& a = surface.Vertices[ia];
            const auto& b = surface.Vertices[ib];
            const auto& c = surface.Vertices[ic];
            const auto face = Cross(Subtract(b.Position, a.Position),
                                    Subtract(c.Position, a.Position));
            const float doubledArea = Length(face);
            if (doubledArea <= 1.0e-6F) continue;
            const auto overlays = surface.Overlays ? GrassOverlaysForTriangle({ TransformPoint(surface, a.Position),
                                                                                TransformPoint(surface, b.Position),
                                                                                TransformPoint(surface, c.Position) },
                                                                              *surface.Overlays)
                                                   : std::vector<const GrassOverlayTriangle*>{};
            for (uint32_t selected = 0; selected < selectedCandidates; ++selected) {
                // A progressive prefix is independent of the view/budget. Increasing
                // a patch's quota adds blades without moving its existing samples.
                const uint32_t candidate = selected;
                uint32_t random = HashCoordinates(static_cast<int32_t>(triangle / 3U), static_cast<int32_t>(candidate),
                    generation.Seed ^ static_cast<uint32_t>(surface.GeometryId));
                if (random == 0U) random = 1U;
                float randomU = UnitRandom(random);
                float randomV = UnitRandom(random);
                if (randomU + randomV > 1.0F) {
                    randomU = 1.0F - randomU;
                    randomV = 1.0F - randomV;
                }
                float sequenceU = static_cast<float>(std::fmod((static_cast<double>(candidate) + 0.5) * 0.7548776662466927, 1.0));
                float sequenceV =
                    std::fmod(
                        static_cast<float>(candidate) *
                                0.61803398875F +
                            HashUnit(
                                static_cast<int32_t>(triangle / 3U),
                                static_cast<int32_t>(candidate),
                                generation.Seed),
                        1.0F);
                if (sequenceU + sequenceV > 1.0F) {
                    sequenceU = 1.0F - sequenceU;
                    sequenceV = 1.0F - sequenceV;
                }
                const float randomness =
                    std::clamp(
                        generation.IndividualRandomness, 0.0F, 1.0F);
                const float u =
                    std::lerp(sequenceU, randomU, randomness);
                const float v =
                    std::lerp(sequenceV, randomV, randomness);
                const float w = 1.0F - u - v;
                const std::array<float, 2> uv{
                    a.Uv[0] * w + b.Uv[0] * u + c.Uv[0] * v,
                    a.Uv[1] * w + b.Uv[1] * u + c.Uv[1] * v};
                float sample = EvaluateGrassMaskLevel(
                    SampleMask(mask, uv[0], uv[1], wrapS, wrapT),
                    rule,
                    WrapGrassTextureCoordinate(uv[0], wrapS),
                    WrapGrassTextureCoordinate(uv[1], wrapT));
                PendingAnchor pending;
                auto& anchor = pending.Anchor;
                if (surface.TriangleSources.empty()) {
                    anchor.SurfaceReference = PackGrassSurfaceReference({ia, ib, ic}, u, v);
                } else {
                    anchor.SurfaceReference = PackGrassSurfaceReference(surface.TriangleSources[triangle / 3U],
                        a.SourceWeights[1] * w + b.SourceWeights[1] * u + c.SourceWeights[1] * v,
                        a.SourceWeights[2] * w + b.SourceWeights[2] * u + c.SourceWeights[2] * v);
                }
                for (size_t axis = 0; axis < 3; ++axis) {
                    anchor.LocalPosition[axis] =
                        a.Position[axis] * w + b.Position[axis] * u +
                        c.Position[axis] * v;
                    anchor.LocalNormal[axis] = face[axis] / doubledArea;
                }
                pending.World = TransformPoint(surface, anchor.LocalPosition);
                sample *= ClusterWeight(generation, pending.World);
                if (!overlays.empty())
                    sample *= GrassOverlaySurvival(pending.World, overlays);
                const float acceptanceSample = UnitRandom(random);
                if (!(acceptanceSample < sample)) continue;
                anchor.Uv = uv;
                anchor.BladeHeight =
                    generation.BladeHeightMin +
                    (generation.BladeHeightMax -
                     generation.BladeHeightMin) *
                        UnitRandom(random);
                anchor.BladeWidth =
                    generation.BladeWidthMin +
                    (generation.BladeWidthMax -
                     generation.BladeWidthMin) *
                        UnitRandom(random);
                anchor.Phase = UnitRandom(random) * 2.0F * std::numbers::pi_v<float>;
                const float orientation =
                    UnitRandom(random) * 2.0F *
                    std::numbers::pi_v<float>;
                anchor.WidthAxis = {
                    std::cos(orientation),
                    std::sin(orientation)};
                anchor.StableId = NextRandom(random);
                if (anchor.StableId == 0U) {
                    anchor.StableId = 1U;
                }
                output.push_back(pending);
            }
        }
    });
    std::vector<PendingAnchor> pending;
    {
        size_t count = 0U;
        for (const auto& part : evaluated) count += part.size();
        pending.reserve(count);
        for (auto& part : evaluated) {
            pending.insert(pending.end(), part.begin(), part.end());
            std::vector<PendingAnchor>().swap(part);
        }
    }
    const auto kept = ResolveMinimumSpacing(pending, generation.MinimumSpacing, workers);
    anchors.reserve(std::min<size_t>(limit, pending.size()));
    for (size_t index = 0; index < pending.size() && anchors.size() < limit; ++index)
        if (kept[index]) anchors.push_back(pending[index].Anchor);
    return anchors;
}

bool GrassPlacementViewNeedsRefresh(const GrassPlacementView& previous, const GrassPlacementView& current) noexcept {
    if (previous.Enabled != current.Enabled || previous.DrawDistance != current.DrawDistance ||
        previous.FullDensityDistance != current.FullDensityDistance || previous.FarDensity != current.FarDensity ||
        previous.GuardDistance != current.GuardDistance)
        return true;
    if (!current.Enabled) return false;
    if (Length(Subtract(previous.Eye, current.Eye)) > previous.GuardDistance * 0.5F) return true;
    const auto rowLength = [](const auto& matrix, size_t row) {
        return Length({matrix[row], matrix[4U+row], matrix[8U+row]});
    };
    const float previousW = std::max(rowLength(previous.WorldToClip, 3U), 1.0e-12F);
    const float currentW = std::max(rowLength(current.WorldToClip, 3U), 1.0e-12F);
    for (size_t row : {0U, 1U, 3U}) {
        std::array<float, 3> a{previous.WorldToClip[row], previous.WorldToClip[4U+row], previous.WorldToClip[8U+row]};
        std::array<float, 3> b{current.WorldToClip[row], current.WorldToClip[4U+row], current.WorldToClip[8U+row]};
        const float lengths = Length(a) * Length(b);
        if (lengths > 1.0e-12F && (a[0]*b[0]+a[1]*b[1]+a[2]*b[2]) / lengths < 0.995F) return true;
        const float oldScale = Length(a) / previousW, newScale = Length(b) / currentW;
        if (std::abs(oldScale - newScale) > std::max(oldScale, newScale) * 0.02F) return true;
    }
    return false;
}

std::vector<GrassAnchor> GrassSurfaceExtractor::Extract(
    const GrassSourceSurface& surface, const GrassPlacementRule& rule,
    const GrassGenerationSettings& generation, const GrassScalarMask& mask, uint32_t budget) {
    if (budget == 0U || !(generation.InstancesPerSquareMeter > 0.0F) || surface.Indices.size() < 3U ||
        mask.Samples.empty()) return {};
    if (!surface.PlacementView.Enabled) return ExtractAnchors(surface, rule, generation, mask, budget);
    // Camera-independent subdivision bounds a sampling patch. Its identity and
    // barycentric sequence survive every change of frustum or allocation.
    std::vector<GrassSourceVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<std::array<uint32_t, 3>> triangleSources;
    const unsigned maxDepth = std::min(16U, static_cast<unsigned>(std::bit_width(
        std::max<size_t>(1U, 524288U / (surface.Indices.size() / 3U))) - 1U));
    const auto split = [&](auto&& self, std::array<GrassSourceVertex, 3> triangle,
                           std::array<uint32_t, 3> sourceIndices, unsigned depth) -> void {
        std::array<float, 3> lengths;
        for (size_t i = 0; i < 3U; ++i) lengths[i] = Length(Subtract(
            TransformPoint(surface, triangle[i].Position), TransformPoint(surface, triangle[(i+1U)%3U].Position)));
        const size_t edge = static_cast<size_t>(std::max_element(lengths.begin(), lengths.end()) - lengths.begin());
        if (!std::all_of(lengths.begin(), lengths.end(), [](float v) { return std::isfinite(v); })) return;
        if (lengths[edge] > 400.0F && depth < maxDepth) {
            const size_t next = (edge + 1U) % 3U;
            GrassSourceVertex middle;
            for (size_t i = 0; i < 3U; ++i) {
                middle.Position[i] = (triangle[edge].Position[i] + triangle[next].Position[i]) * 0.5F;
                middle.Normal[i] = (triangle[edge].Normal[i] + triangle[next].Normal[i]) * 0.5F;
                middle.SourceWeights[i] = (triangle[edge].SourceWeights[i] + triangle[next].SourceWeights[i]) * 0.5F;
            }
            for (size_t i = 0; i < 2U; ++i) middle.Uv[i] = (triangle[edge].Uv[i] + triangle[next].Uv[i]) * 0.5F;
            auto second = triangle;
            triangle[next] = middle;
            second[edge] = middle;
            self(self, triangle, sourceIndices, depth + 1U);
            self(self, second, sourceIndices, depth + 1U);
        } else {
            triangleSources.push_back(sourceIndices);
            for (const auto& vertex : triangle) {
                indices.push_back(static_cast<uint32_t>(vertices.size()));
                vertices.push_back(vertex);
            }
        }
    };
    for (size_t i = 0; i + 2U < surface.Indices.size(); i += 3U) {
        if (surface.Indices[i] >= surface.Vertices.size() || surface.Indices[i+1U] >= surface.Vertices.size() ||
            surface.Indices[i+2U] >= surface.Vertices.size()) continue;
        std::array<GrassSourceVertex, 3> triangle{surface.Vertices[surface.Indices[i]],
            surface.Vertices[surface.Indices[i+1U]], surface.Vertices[surface.Indices[i+2U]]};
        for (size_t j = 0; j < 3; ++j) {
            triangle[j].SourceWeights = {};
            triangle[j].SourceWeights[j] = 1.0F;
        }
        split(split, triangle, {surface.Indices[i], surface.Indices[i+1U], surface.Indices[i+2U]}, 0U);
    }
    auto patches = surface;
    patches.Vertices = vertices;
    patches.Indices = indices;
    patches.TriangleSources = triangleSources;
    return ExtractAnchors(patches, rule, generation, mask, budget);
}

GrassPlacementSet BuildGrassPlacementSet(
    std::vector<GrassAnchor> anchors, float clusterSize) {
    GrassPlacementSet result;
    if (anchors.empty()) {
        return result;
    }
    clusterSize = std::max(clusterSize, 1.0F);
    if (anchors.size() > std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("grass anchor count exceeds the cluster index");
    // One cluster per cell, in cell order (X, then Y, then Z). Within a cell
    // the anchors keep their extraction order before the visibility sort, as
    // a bucket per cell filled in order would; clusters are then independent.
    const size_t workers = GrassParallelWorkerCount(anchors.size());
    struct Entry {
        SpatialCell Cell;
        uint32_t Index = 0U;
    };
    std::vector<Entry> order(anchors.size());
    for (size_t index = 0; index < anchors.size(); ++index)
        order[index] = {CellFor(anchors[index].LocalPosition, clusterSize), static_cast<uint32_t>(index)};
    SortInParallel(order, [](const Entry& left, const Entry& right) {
        if (left.Cell != right.Cell) return left.Cell < right.Cell;
        return left.Index < right.Index;
    }, workers);
    std::vector<size_t> starts;
    for (size_t position = 0; position < order.size(); ++position)
        if (position == 0U || order[position].Cell != order[position - 1U].Cell) starts.push_back(position);
    starts.push_back(order.size());
    const size_t clusterCount = starts.size() - 1U;
    result.Anchors.resize(anchors.size());
    result.Clusters.resize(clusterCount);
    RunGrassParallelWork(workers, [&](size_t worker) {
        const size_t firstCluster = clusterCount * worker / workers;
        const size_t lastCluster = clusterCount * (worker + 1U) / workers;
        for (size_t clusterIndex = firstCluster; clusterIndex < lastCluster; ++clusterIndex) {
            const size_t begin = starts[clusterIndex];
            const size_t end = starts[clusterIndex + 1U];
            for (size_t position = begin; position < end; ++position)
                result.Anchors[position] = anchors[order[position].Index];
            const auto values = result.Anchors.begin();
            std::sort(
                values + static_cast<std::ptrdiff_t>(begin), values + static_cast<std::ptrdiff_t>(end),
                [](const GrassAnchor& left,
                   const GrassAnchor& right) {
                    return GrassStableVisibilityValue(
                               left.StableId) <
                           GrassStableVisibilityValue(
                               right.StableId);
                });
            GrassAnchorCluster cluster;
            cluster.FirstAnchor = static_cast<uint32_t>(begin);
            cluster.AnchorCount = static_cast<uint32_t>(end - begin);
            std::array<float, 3> minimum{
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max()};
            std::array<float, 3> maximum{
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest()};
            for (size_t position = begin; position < end; ++position) {
                const auto& anchor = result.Anchors[position];
                for (size_t axis = 0U; axis < 3U; ++axis) {
                    minimum[axis] = std::min(
                        minimum[axis], anchor.LocalPosition[axis]);
                    maximum[axis] = std::max(
                        maximum[axis], anchor.LocalPosition[axis]);
                }
                cluster.MaximumBladeHeight = std::max(
                    cluster.MaximumBladeHeight, anchor.BladeHeight);
            }
            for (size_t axis = 0U; axis < 3U; ++axis) {
                cluster.LocalCenter[axis] =
                    (minimum[axis] + maximum[axis]) * 0.5F;
            }
            for (size_t position = begin; position < end; ++position) {
                cluster.LocalRadius = std::max(
                    cluster.LocalRadius,
                    Length(Subtract(
                        result.Anchors[position].LocalPosition,
                        cluster.LocalCenter)));
            }
            result.Clusters[clusterIndex] = cluster;
        }
    });
    return result;
}

} // namespace Fast::Oot3d
