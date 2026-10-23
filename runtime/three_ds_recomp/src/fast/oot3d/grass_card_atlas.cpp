#include "fast/oot3d/grass_card_atlas.h"

#include "fast/oot3d/grass_blade_geometry.h"
#include "fast/oot3d/grass_parallel_work.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>

namespace Fast::Oot3d {
namespace {

constexpr uint32_t kSupersample = 2U;
constexpr float kPi = std::numbers::pi_v<float>;

struct Random {
    uint64_t State;
    explicit Random(uint64_t seed) : State(seed * 0x9e3779b97f4a7c15ULL + 0x632be59bd9b4e019ULL) {}
    float Next() {
        State += 0x9e3779b97f4a7c15ULL;
        uint64_t z = State;
        z = (z ^ (z >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27U)) * 0x94d049bb133111ebULL;
        z ^= z >> 31U;
        return static_cast<float>(z >> 40U) / 16777216.0F;
    }
};

struct Vec3 {
    float X, Y, Z;
};

// grass_blade_shape of grass_blade_shape.h, in C++: the same stable per-blade
// variation from the phase, the same curve, sway, droop and twist.
Vec3 BladeShape(float t, float phase, const std::array<float, 2>& rootAxis, const GrassCardAtlasShape& shape,
                float& twist) {
    const float random = std::fmod(std::sin(phase * 12.9898F + 4.1414F) * 43758.5453F, 1.0F);
    const float wrapped = random < 0.0F ? random + 1.0F : random;
    const float signedRandom = wrapped * 2.0F - 1.0F;
    const float angle = signedRandom * shape.Variation * kPi;
    const float c = std::cos(angle), s = std::sin(angle);
    const std::array<float, 2> direction{c * rootAxis[0] - s * rootAxis[1], s * rootAxis[0] + c * rootAxis[1]};
    const std::array<float, 2> side{-direction[1], direction[0]};
    const float curve = shape.Curvature * (1.0F + signedRandom * shape.Variation);
    std::array<float, 2> offset{direction[0] * curve * t * t, direction[1] * curve * t * t};
    const float sway = 0.35F * shape.Variation * std::sin(kPi * t + phase) * t * t;
    offset[0] += side[0] * sway;
    offset[1] += side[1] * sway;
    twist = shape.Twist * signedRandom * t * t;
    return {offset[0], t - shape.Droop * t * t * t, offset[1]};
}

struct Blade {
    float RootX, RootZ, Height, Phase, HalfWidth;
    std::array<float, 2> RootAxis;
};

// A blade edge point as grass_blade_position builds it for a camera-facing
// blade without wind or interaction: the width axis faces the camera (the
// tile's horizontal axis), rotated by the twist.
Vec3 BladePoint(const Blade& blade, float t, float widthSign, const GrassCardAtlasShape& shape) {
    float twist = 0.0F;
    const Vec3 offset = BladeShape(t, blade.Phase, blade.RootAxis, shape, twist);
    const float widthFactor = t >= 1.0F ? 0.0F : 1.0F - 0.75F * t;
    const float ax = std::cos(twist), az = std::sin(twist);
    return {blade.RootX + offset.X * blade.Height + ax * blade.HalfWidth * widthFactor * widthSign,
            offset.Y * blade.Height,
            blade.RootZ + offset.Z * blade.Height + az * blade.HalfWidth * widthFactor * widthSign};
}

struct TileVertex {
    float U, V, Depth, Factor;
};

// The supersampled tile: per sample the nearest covered blade's offset toward
// the viewer (-inf where nothing covers) and its height factor.
struct SampleGrid {
    uint32_t Size;
    std::vector<float> Depth;
    std::vector<float> Factor;
    explicit SampleGrid(uint32_t size)
        : Size(size), Depth(static_cast<size_t>(size) * size, -std::numeric_limits<float>::infinity()),
          Factor(static_cast<size_t>(size) * size, 0.0F) {}
};

void RasterizeTriangle(SampleGrid& grid, const TileVertex& a, const TileVertex& b, const TileVertex& c) {
    const float scale = static_cast<float>(grid.Size);
    const float ax = a.U * scale, ay = a.V * scale, bx = b.U * scale, by = b.V * scale, cx = c.U * scale,
                cy = c.V * scale;
    const float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    if (std::abs(area) < 1.0e-12F) return;
    const int minX = std::max(0, static_cast<int>(std::floor(std::min({ax, bx, cx}))));
    const int maxX = std::min(static_cast<int>(grid.Size) - 1, static_cast<int>(std::ceil(std::max({ax, bx, cx}))));
    const int minY = std::max(0, static_cast<int>(std::floor(std::min({ay, by, cy}))));
    const int maxY = std::min(static_cast<int>(grid.Size) - 1, static_cast<int>(std::ceil(std::max({ay, by, cy}))));
    const float inverseArea = 1.0F / area;
    for (int y = minY; y <= maxY; ++y) {
        const float py = static_cast<float>(y) + 0.5F;
        for (int x = minX; x <= maxX; ++x) {
            const float px = static_cast<float>(x) + 0.5F;
            const float w0 = ((bx - px) * (cy - py) - (by - py) * (cx - px)) * inverseArea;
            const float w1 = ((cx - px) * (ay - py) - (cy - py) * (ax - px)) * inverseArea;
            const float w2 = 1.0F - w0 - w1;
            if (w0 < 0.0F || w1 < 0.0F || w2 < 0.0F) continue;
            const size_t index = static_cast<size_t>(y) * grid.Size + static_cast<size_t>(x);
            const float depth = w0 * a.Depth + w1 * b.Depth + w2 * c.Depth;
            if (depth <= grid.Depth[index]) continue;
            grid.Depth[index] = depth;
            grid.Factor[index] = w0 * a.Factor + w1 * b.Factor + w2 * c.Factor;
        }
    }
}

// Draws the blades of one tile (unit footprint radius) seen from the given
// elevation, projected along the view onto the plane through the tile centre
// that faces the viewer. The view direction in tile space is (0, sin e, cos e).
SampleGrid RasterizeTile(const GrassCardAtlasShape& shape, uint32_t set, uint32_t elevationIndex, uint32_t variant,
                         uint32_t size) {
    const float elevation = kGrassCardAtlasElevationDegrees[elevationIndex] * kPi / 180.0F;
    const float cosE = std::cos(elevation), sinE = std::sin(elevation);
    const float height = shape.HeightOverRadius[set];
    const auto extent = GrassCardTileExtentAt(height, 1.0F, elevation);
    const float span = std::max(extent.Above + extent.Below, 1.0e-6F);
    const uint32_t segments = std::clamp<uint32_t>(shape.Segments, kMinimumGrassBladeSegments, kMaximumGrassBladeSegments);
    Random random(static_cast<uint64_t>(set) * 1009U + static_cast<uint64_t>(variant) * 7U + 1U);
    SampleGrid grid(size * kSupersample);
    const auto project = [&](const Vec3& p, float factor) {
        return TileVertex{(p.X + 1.0F) * 0.5F, (p.Y * cosE - p.Z * sinE + extent.Below) / span,
                          p.Y * sinE + p.Z * cosE, factor};
    };
    for (uint32_t index = 0; index < shape.BladeCount[set]; ++index) {
        Blade blade{};
        // Roots over the footprint disc; the same elevation set of a variant
        // shares its blades, so the tiles of one card agree between elevations.
        const float r = 0.92F * std::sqrt(random.Next());
        const float angle = random.Next() * 2.0F * kPi;
        blade.RootX = r * std::cos(angle);
        blade.RootZ = r * std::sin(angle);
        blade.Height = height * (shape.MinimumHeightRatio + (1.0F - shape.MinimumHeightRatio) * random.Next());
        blade.Phase = random.Next() * 2.0F * kPi;
        blade.HalfWidth = shape.HalfWidthOverRadius[set];
        const float axis = random.Next() * 2.0F * kPi;
        blade.RootAxis = {std::cos(axis), std::sin(axis)};
        for (uint32_t segment = 0; segment < segments; ++segment) {
            const float lower = static_cast<float>(segment) / static_cast<float>(segments);
            const float upper = static_cast<float>(segment + 1U) / static_cast<float>(segments);
            const auto lowerLeft = project(BladePoint(blade, lower, -1.0F, shape), lower);
            const auto lowerRight = project(BladePoint(blade, lower, 1.0F, shape), lower);
            if (segment + 1U == segments) {
                const auto tip = project(BladePoint(blade, 1.0F, 0.0F, shape), 1.0F);
                RasterizeTriangle(grid, lowerLeft, lowerRight, tip);
                continue;
            }
            const auto upperLeft = project(BladePoint(blade, upper, -1.0F, shape), upper);
            const auto upperRight = project(BladePoint(blade, upper, 1.0F, shape), upper);
            RasterizeTriangle(grid, lowerLeft, lowerRight, upperRight);
            RasterizeTriangle(grid, lowerLeft, upperRight, upperLeft);
        }
    }
    return grid;
}

// Resolves one mip of a tile from the supersamples: each texel's coverage is
// the covered fraction of its block of samples, its height factor and depth
// the means over the covered samples. Row 0 is the bottom of the card.
void ResolveMip(const SampleGrid& grid, uint32_t size, float depthScale, uint8_t* texels, std::vector<float>& coverage) {
    const uint32_t block = grid.Size / size;
    coverage.assign(static_cast<size_t>(size) * size, 0.0F);
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            uint32_t covered = 0U;
            float factor = 0.0F, depth = 0.0F;
            for (uint32_t sy = 0; sy < block; ++sy)
                for (uint32_t sx = 0; sx < block; ++sx) {
                    const size_t index = static_cast<size_t>(y * block + sy) * grid.Size + x * block + sx;
                    if (!std::isfinite(grid.Depth[index])) continue;
                    ++covered;
                    factor += grid.Factor[index];
                    depth += grid.Depth[index];
                }
            const size_t index = static_cast<size_t>(y) * size + x;
            coverage[index] = static_cast<float>(covered) / static_cast<float>(block * block);
            uint8_t* texel = texels + index * kGrassCardAtlasTexelBytes;
            texel[1] = covered == 0U ? 0U : static_cast<uint8_t>(std::lround(255.0F * std::clamp(factor / covered, 0.0F, 1.0F)));
            texel[2] = covered == 0U ? 128U
                : static_cast<uint8_t>(std::lround(255.0F * std::clamp(depth / covered / depthScale * 0.5F + 0.5F, 0.0F, 1.0F)));
            texel[3] = 0U;
        }
    }
}

constexpr float kAlphaThreshold = 128.0F / 255.0F;

float PassingFraction(const std::vector<float>& coverage, float scale) noexcept {
    size_t count = 0U;
    for (const float value : coverage)
        if (std::min(value * scale, 1.0F) >= kAlphaThreshold) ++count;
    return coverage.empty() ? 0.0F : static_cast<float>(count) / static_cast<float>(coverage.size());
}

// Writes the coverage channel scaled so that the alpha-tested fraction of the
// mip matches the target (the base level's), instead of thinning sparse blades
// away or thickening dense ones with distance. The scale is the smallest at
// which at least the target passes; the resolved values are fine grained, so
// the step past the target is small.
void WriteCoverage(const std::vector<float>& coverage, float target, uint8_t* texels) {
    float scale = 1.0F;
    if (target > 0.0F) {
        float low = 1.0F / 32.0F, high = 32.0F;
        if (PassingFraction(coverage, low) >= target) {
            scale = low;
        } else if (PassingFraction(coverage, high) < target) {
            scale = high;
        } else {
            for (int iteration = 0; iteration < 24; ++iteration) {
                const float middle = 0.5F * (low + high);
                if (PassingFraction(coverage, middle) < target) low = middle;
                else high = middle;
            }
            scale = high;
        }
    }
    for (size_t i = 0; i < coverage.size(); ++i)
        texels[i * kGrassCardAtlasTexelBytes] = static_cast<uint8_t>(std::lround(255.0F * std::min(coverage[i] * scale, 1.0F)));
}

float Coverage(const uint8_t* texels, size_t count) noexcept {
    size_t passing = 0U;
    for (size_t i = 0; i < count; ++i)
        if (texels[i * kGrassCardAtlasTexelBytes] >= 128U) ++passing;
    return count == 0U ? 0.0F : static_cast<float>(passing) / static_cast<float>(count);
}

} // namespace

GrassCardTileExtent GrassCardTileExtentAt(float heightOverRadius, float radius, float elevation) noexcept {
    const float cosE = std::cos(elevation), sinE = std::sin(elevation);
    return {radius * (heightOverRadius * cosE + sinE), radius * sinE};
}

GrassCardAtlasShape GrassCardAtlasShapeFromSettings(const InteractiveGrassSettings& settings) noexcept {
    GrassCardAtlasShape shape;
    const auto& appearance = settings.Appearance;
    shape.Curvature = appearance.BladeCurvature;
    shape.Droop = appearance.BladeDroop;
    shape.Variation = appearance.ShapeVariation;
    shape.Twist = appearance.BladeTwistDegrees * kPi / 180.0F;
    shape.Segments = std::clamp<uint8_t>(appearance.BladeSegments, kMinimumGrassBladeSegments, kMaximumGrassBladeSegments);
    const float tallest = std::max(settings.Generation.BladeHeightMax, 0.01F) * appearance.HeightScale;
    const float shortest = std::max(settings.Generation.BladeHeightMin, 0.0F) * appearance.HeightScale;
    shape.MinimumHeightRatio = std::clamp(shortest / tallest, 0.3F, 1.0F);
    const float halfWidth = std::max(settings.Generation.BladeWidthMax, 0.01F) * 0.5F;
    const float clusterSize = std::max(settings.CullingClusterSize, 1.0F);
    for (uint32_t set = 0; set < kGrassCardAtlasSets; ++set) {
        // Footprint radius of a whole cell of the level: a little over half
        // its side (one cluster, four, sixteen).
        const float cells = static_cast<float>(set == 0U ? 1U : set == 1U ? 4U : 16U);
        const float radius = clusterSize * cells * 0.6F;
        shape.HeightOverRadius[set] = std::clamp(tallest / radius, 0.02F, 8.0F);
        shape.HalfWidthOverRadius[set] = std::clamp(halfWidth / radius, 0.001F, 0.5F);
    }
    return shape;
}

uint64_t GrassCardAtlasKey(const GrassCardAtlasShape& shape) noexcept {
    uint64_t key = 0xcbf29ce484222325ULL ^ 0x43415245U; // "CARE", bumped with the format
    const auto mix = [&key](uint64_t value) {
        key ^= value;
        key *= 0x100000001b3ULL;
    };
    for (const float value : {shape.Curvature, shape.Droop, shape.Variation, shape.Twist, shape.MinimumHeightRatio})
        mix(std::bit_cast<uint32_t>(value));
    mix(shape.Segments);
    for (uint32_t set = 0; set < kGrassCardAtlasSets; ++set) {
        mix(std::bit_cast<uint32_t>(shape.HeightOverRadius[set]));
        mix(std::bit_cast<uint32_t>(shape.HalfWidthOverRadius[set]));
        mix(shape.BladeCount[set]);
    }
    mix(kGrassCardAtlasTileSize);
    mix(kGrassCardAtlasLayers);
    mix(kGrassCardAtlasTexelBytes);
    mix(kSupersample);
    return key == 0U ? 1U : key;
}

uint32_t GrassCardAtlasSetFor(const GrassCardAtlasShape& shape, float height, float radius) noexcept {
    const float ratio = std::log(std::max(height, 1.0e-3F) / std::max(radius, 1.0e-3F));
    uint32_t best = 0U;
    float nearest = std::numeric_limits<float>::infinity();
    for (uint32_t set = 0; set < kGrassCardAtlasSets; ++set) {
        const float distance = std::abs(ratio - std::log(shape.HeightOverRadius[set]));
        if (distance < nearest) {
            nearest = distance;
            best = set;
        }
    }
    return best;
}

GrassCardAtlasImage BuildGrassCardAtlas(const GrassCardAtlasShape& shape) {
    GrassCardAtlasImage image;
    image.Key = GrassCardAtlasKey(shape);
    image.Size = kGrassCardAtlasTileSize;
    image.Layers = kGrassCardAtlasLayers;
    uint32_t mips = 1U;
    while ((image.Size >> mips) >= 1U) ++mips;
    image.Mips.resize(mips);
    for (uint32_t mip = 0; mip < mips; ++mip) image.Mips[mip].assign(image.LayerBytes(mip) * image.Layers, 0U);

    const size_t workers = std::min<size_t>(GrassParallelWorkerCount(kGrassParallelWorkItems), image.Layers);
    RunGrassParallelWork(workers, [&](size_t worker) {
        for (uint32_t layer = static_cast<uint32_t>(worker); layer < image.Layers; layer += static_cast<uint32_t>(workers)) {
            const uint32_t variant = layer % kGrassCardAtlasVariants;
            const uint32_t elevation = (layer / kGrassCardAtlasVariants) % kGrassCardAtlasElevations;
            const uint32_t set = layer / (kGrassCardAtlasVariants * kGrassCardAtlasElevations);
            const float depthScale = GrassCardTileDepthScale(shape.HeightOverRadius[set]);
            const SampleGrid grid = RasterizeTile(shape, set, elevation, variant, image.Size);
            // Every mip resolves from the same samples (the true covered area
            // of its block), with the alpha-tested fraction held at the base
            // level's so the blades neither thin away nor thicken with distance.
            std::vector<float> coverage;
            uint8_t* base = image.Mips[0].data() + layer * image.LayerBytes(0);
            ResolveMip(grid, image.Size, depthScale, base, coverage);
            for (size_t i = 0; i < coverage.size(); ++i)
                base[i * kGrassCardAtlasTexelBytes] = static_cast<uint8_t>(std::lround(255.0F * coverage[i]));
            const float target = Coverage(base, coverage.size());
            for (uint32_t mip = 1; mip < mips; ++mip) {
                uint8_t* destination = image.Mips[mip].data() + layer * image.LayerBytes(mip);
                ResolveMip(grid, image.MipSize(mip), depthScale, destination, coverage);
                WriteCoverage(coverage, target, destination);
            }
        }
    });
    return image;
}

float GrassCardAtlasCoverage(const GrassCardAtlasImage& image, uint32_t layer, uint32_t mip) noexcept {
    if (mip >= image.Mips.size() || layer >= image.Layers) return 0.0F;
    return Coverage(image.Mips[mip].data() + layer * image.LayerBytes(mip),
                    static_cast<size_t>(image.MipSize(mip)) * image.MipSize(mip));
}

} // namespace Fast::Oot3d
