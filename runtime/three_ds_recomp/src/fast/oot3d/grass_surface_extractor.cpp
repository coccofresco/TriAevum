#include "fast/oot3d/grass_surface_extractor.h"
#include "fast/oot3d/grass_surface_reference.h"
#include "fast/oot3d/grass_visibility.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <compare>
#include <limits>
#include <numbers>
#include <unordered_map>

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

bool HasMinimumSpacing(
    const std::array<float, 3>& worldPosition, float spacing,
    const std::unordered_map<
        SpatialCell, std::vector<std::array<float, 3>>,
        SpatialCellHash>& occupied) {
    if (spacing <= 0.0F) {
        return true;
    }
    const auto center = CellFor(worldPosition, spacing);
    const float spacingSquared = spacing * spacing;
    for (int32_t z = -1; z <= 1; ++z) {
        for (int32_t y = -1; y <= 1; ++y) {
            for (int32_t x = -1; x <= 1; ++x) {
                const SpatialCell neighbor{
                    center.X + x, center.Y + y, center.Z + z};
                const auto found = occupied.find(neighbor);
                if (found == occupied.end()) {
                    continue;
                }
                for (const auto& existing : found->second) {
                    const float dx =
                        existing[0] - worldPosition[0];
                    const float dy =
                        existing[1] - worldPosition[1];
                    const float dz =
                        existing[2] - worldPosition[2];
                    if (dx * dx + dy * dy + dz * dz <
                        spacingSquared) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
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
        ? std::clamp(sample, 0.0F, 1.0F)
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
    std::unordered_map<
        SpatialCell, std::vector<std::array<float, 3>>,
        SpatialCellHash>
        occupied;
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
    for (size_t triangle = 0;
         triangle + 2 < surface.Indices.size() && anchors.size() < limit;
         triangle += 3) {
        const uint32_t candidates = candidateCounts[triangle / 3U];
        const uint32_t selectedCandidates = viewQuotas.empty() ? static_cast<uint32_t>(
            quotaAt(candidatePrefix + candidates) - quotaAt(candidatePrefix)) : viewQuotas[triangle / 3U];
        candidatePrefix += candidates;
        if (selectedCandidates == 0U) continue;
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
        for (uint32_t selected = 0;
             selected < selectedCandidates && anchors.size() < limit; ++selected) {
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
                rule);
            GrassAnchor anchor;
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
            const auto worldPosition =
                TransformPoint(surface, anchor.LocalPosition);
            sample *= ClusterWeight(generation, worldPosition);
            const float acceptanceSample = UnitRandom(random);
            if (!(acceptanceSample < sample) ||
                !HasMinimumSpacing(
                    worldPosition, generation.MinimumSpacing,
                    occupied)) {
                continue;
            }
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
            anchors.push_back(anchor);
            if (generation.MinimumSpacing > 0.0F) {
                occupied[CellFor(
                    worldPosition, generation.MinimumSpacing)]
                    .push_back(worldPosition);
            }
        }
    }
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
    std::unordered_map<
        SpatialCell, std::vector<GrassAnchor>, SpatialCellHash>
        buckets;
    for (auto& anchor : anchors) {
        buckets[CellFor(anchor.LocalPosition, clusterSize)]
            .push_back(std::move(anchor));
    }
    const auto anchorCount = anchors.size();
    std::vector<GrassAnchor>().swap(anchors);

    std::vector<SpatialCell> keys;
    keys.reserve(buckets.size());
    for (const auto& [key, values] : buckets) {
        (void)values;
        keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end());
    result.Anchors.reserve(anchorCount);
    result.Clusters.reserve(keys.size());
    for (const auto& key : keys) {
        auto& values = buckets.at(key);
        std::sort(
            values.begin(), values.end(),
            [](const GrassAnchor& left,
               const GrassAnchor& right) {
                return GrassStableVisibilityValue(
                           left.StableId) <
                       GrassStableVisibilityValue(
                           right.StableId);
            });
        GrassAnchorCluster cluster;
        cluster.FirstAnchor =
            static_cast<uint32_t>(result.Anchors.size());
        cluster.AnchorCount =
            static_cast<uint32_t>(values.size());
        std::array<float, 3> minimum{
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()};
        std::array<float, 3> maximum{
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest()};
        for (const auto& anchor : values) {
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
        for (const auto& anchor : values) {
            cluster.LocalRadius = std::max(
                cluster.LocalRadius,
                Length(Subtract(
                    anchor.LocalPosition,
                    cluster.LocalCenter)));
        }
        for (auto& anchor : values) {
            result.Anchors.push_back(std::move(anchor));
        }
        result.Clusters.push_back(cluster);
    }
    return result;
}

} // namespace Fast::Oot3d
