#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <span>
#include <vector>

namespace Fast::Renderer3ds {

// A top-down map of the drawn water surface (whole-water draws: a catalog
// water texture), for passes that find the surface per pixel: the shape of
// each level piece of a river with steps, and where the falls between the
// pieces land. Generic data: world triangles only.
using PicaWaterTriangle = std::array<std::array<float, 3>, 3>;

// Lying faces (the surface: level, or sloped as a stream running downhill)
// and falling ones (steeper than kPicaWaterFallSlope: waterfalls, the drops
// of a river).
inline constexpr float kPicaWaterFallSlope = 0.75F;

struct PicaWaterTriangles {
    std::vector<PicaWaterTriangle> Level;
    std::vector<PicaWaterTriangle> Falling;
};

// World triangles of a mesh (column-major world-from-local), by slope.
[[nodiscard]] inline PicaWaterTriangles SplitPicaWaterTriangles(std::span<const std::array<float, 3>> positions,
                                                                std::span<const uint32_t> indices,
                                                                const std::array<float, 16>& worldFromLocal) {
    PicaWaterTriangles result;
    const auto world = [&](const std::array<float, 3>& p) {
        std::array<float, 3> out{};
        for (size_t row = 0; row < 3; ++row)
            out[row] = worldFromLocal[row] * p[0] + worldFromLocal[4 + row] * p[1] + worldFromLocal[8 + row] * p[2] +
                       worldFromLocal[12 + row];
        return out;
    };
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        if (indices[i] >= positions.size() || indices[i + 1] >= positions.size() || indices[i + 2] >= positions.size())
            continue;
        const PicaWaterTriangle triangle{world(positions[indices[i]]), world(positions[indices[i + 1]]),
                                         world(positions[indices[i + 2]])};
        std::array<float, 3> u{}, v{};
        for (size_t axis = 0; axis < 3; ++axis) {
            u[axis] = triangle[1][axis] - triangle[0][axis];
            v[axis] = triangle[2][axis] - triangle[0][axis];
        }
        const std::array<float, 3> normal{u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2],
                                          u[0] * v[1] - u[1] * v[0]};
        const float length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
        if (!(length > 1.0e-6F)) continue;
        const float up = std::abs(normal[1]) / length;
        if (up > kPicaWaterFallSlope)
            result.Level.push_back(triangle);
        else
            result.Falling.push_back(triangle);
    }
    return result;
}

inline constexpr uint32_t kPicaWaterMapSize = 512U;
inline constexpr uint32_t kPicaWaterMapLevels = 16U;
// World units around a fall's footprint where its landing churns.
inline constexpr float kPicaWaterLandingReach = 18.0F;

struct PicaWaterMap {
    // World x, z of texel (0, 0)'s corner, world units per texel, texels per
    // side (0: no map).
    std::array<float, 2> Origin{};
    float Texel = 0.0F;
    uint32_t Size = 0;
    // Per texel: the surface height at its centre (NaN: no water), landing
    // foam (0-1) and the surface's slope there (d height / dx, dz): the
    // height anywhere in the texel is its plane's (a sloped stream is
    // continuous, a step between pieces is not).
    std::vector<std::array<float, 4>> Texels;
    // The distinct surface heights, ascending (at most kPicaWaterMapLevels).
    std::vector<float> Levels;
    // The lowest and highest surface heights.
    std::array<float, 2> HeightRange{};

    [[nodiscard]] const std::array<float, 4>* At(float x, float z) const noexcept {
        if (Size == 0U) return nullptr;
        const float u = std::floor((x - Origin[0]) / Texel), v = std::floor((z - Origin[1]) / Texel);
        if (u < 0.0F || v < 0.0F || u >= float(Size) || v >= float(Size)) return nullptr;
        return &Texels[static_cast<size_t>(v) * Size + static_cast<size_t>(u)];
    }
};

// The map of a set of water triangles: square, over their horizontal
// extent, at least kPicaWaterMapMinimumTexel world units per texel.
inline constexpr float kPicaWaterMapMinimumTexel = 4.0F;

[[nodiscard]] inline PicaWaterMap BuildPicaWaterMap(std::span<const PicaWaterTriangles* const> sets) {
    PicaWaterMap map;
    float minX = std::numeric_limits<float>::max(), minZ = minX;
    float maxX = std::numeric_limits<float>::lowest(), maxZ = maxX;
    for (const auto* set : sets)
        for (const auto* list : {&set->Level, &set->Falling})
            for (const auto& triangle : *list)
                for (const auto& p : triangle) {
                    minX = std::min(minX, p[0]);
                    maxX = std::max(maxX, p[0]);
                    minZ = std::min(minZ, p[2]);
                    maxZ = std::max(maxZ, p[2]);
                }
    if (!(maxX >= minX)) return map;
    map.Size = kPicaWaterMapSize;
    map.Texel = std::max((std::max(maxX - minX, maxZ - minZ) + 2.0F * kPicaWaterLandingReach) / float(map.Size),
                         kPicaWaterMapMinimumTexel);
    map.Origin = {minX - kPicaWaterLandingReach, minZ - kPicaWaterLandingReach};
    map.Texels.assign(size_t(map.Size) * map.Size, {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F, 0.0F});
    const auto range = [&](float low, float high, float origin, int32_t& first, int32_t& last) {
        first = std::max(int32_t(std::floor((low - origin) / map.Texel)), 0);
        last = std::min(int32_t(std::floor((high - origin) / map.Texel)), int32_t(map.Size) - 1);
        return first <= last;
    };
    // Lying faces: the height at each texel centre they cover and their
    // slope (the higher where two overlap).
    for (const auto* set : sets) {
        for (const auto& t : set->Level) {
            int32_t u0, u1, v0, v1;
            if (!range(std::min({t[0][0], t[1][0], t[2][0]}), std::max({t[0][0], t[1][0], t[2][0]}), map.Origin[0],
                       u0, u1) ||
                !range(std::min({t[0][2], t[1][2], t[2][2]}), std::max({t[0][2], t[1][2], t[2][2]}), map.Origin[1],
                       v0, v1))
                continue;
            const float area = (t[1][0] - t[0][0]) * (t[2][2] - t[0][2]) - (t[2][0] - t[0][0]) * (t[1][2] - t[0][2]);
            if (std::abs(area) < 1.0e-6F) continue;
            const float rise1 = t[1][1] - t[0][1], rise2 = t[2][1] - t[0][1];
            const float slopeX = (rise1 * (t[2][2] - t[0][2]) - rise2 * (t[1][2] - t[0][2])) / area;
            const float slopeZ = ((t[1][0] - t[0][0]) * rise2 - (t[2][0] - t[0][0]) * rise1) / area;
            for (int32_t v = v0; v <= v1; ++v) {
                for (int32_t u = u0; u <= u1; ++u) {
                    const float x = map.Origin[0] + (float(u) + 0.5F) * map.Texel;
                    const float z = map.Origin[1] + (float(v) + 0.5F) * map.Texel;
                    const float w1 = ((x - t[0][0]) * (t[2][2] - t[0][2]) - (t[2][0] - t[0][0]) * (z - t[0][2])) / area;
                    const float w2 = ((t[1][0] - t[0][0]) * (z - t[0][2]) - (x - t[0][0]) * (t[1][2] - t[0][2])) / area;
                    const float w0 = 1.0F - w1 - w2;
                    if (w0 < -1.0e-4F || w1 < -1.0e-4F || w2 < -1.0e-4F) continue;
                    const float height = w0 * t[0][1] + w1 * t[1][1] + w2 * t[2][1];
                    auto& texel = map.Texels[size_t(v) * map.Size + size_t(u)];
                    if (!(texel[0] >= height)) texel = {height, texel[1], slopeX, slopeZ};
                }
            }
        }
    }
    // Falls: where each lands (level water at or a little below its lowest
    // point and clearly below its top: never the piece it falls from),
    // around its footprint, foam fading over kPicaWaterLandingReach.
    for (const auto* set : sets) {
        for (const auto& t : set->Falling) {
            const float lowX = std::min({t[0][0], t[1][0], t[2][0]}), highX = std::max({t[0][0], t[1][0], t[2][0]});
            const float lowZ = std::min({t[0][2], t[1][2], t[2][2]}), highZ = std::max({t[0][2], t[1][2], t[2][2]});
            const float bottom = std::min({t[0][1], t[1][1], t[2][1]});
            const float top = std::max({t[0][1], t[1][1], t[2][1]});
            int32_t u0, u1, v0, v1;
            if (!range(lowX - kPicaWaterLandingReach, highX + kPicaWaterLandingReach, map.Origin[0], u0, u1) ||
                !range(lowZ - kPicaWaterLandingReach, highZ + kPicaWaterLandingReach, map.Origin[1], v0, v1))
                continue;
            for (int32_t v = v0; v <= v1; ++v) {
                for (int32_t u = u0; u <= u1; ++u) {
                    auto& texel = map.Texels[size_t(v) * map.Size + size_t(u)];
                    if (!(texel[0] <= bottom + 3.0F && texel[0] >= bottom - 60.0F && texel[0] <= top - 5.0F))
                        continue;
                    const float x = map.Origin[0] + (float(u) + 0.5F) * map.Texel;
                    const float z = map.Origin[1] + (float(v) + 0.5F) * map.Texel;
                    const float dx = std::max({lowX - x, 0.0F, x - highX});
                    const float dz = std::max({lowZ - z, 0.0F, z - highZ});
                    const float foam = 1.0F - std::min(std::sqrt(dx * dx + dz * dz) / kPicaWaterLandingReach, 1.0F);
                    texel[1] = std::max(texel[1], foam);
                }
            }
        }
    }
    // Distinct heights (two-unit bins, their mean), the most covered first.
    std::map<int32_t, std::pair<double, uint32_t>> bins;
    for (const auto& texel : map.Texels) {
        if (std::isnan(texel[0])) continue;
        auto& bin = bins[int32_t(std::floor(texel[0] * 0.5F))];
        bin.first += texel[0];
        ++bin.second;
    }
    // Neighbouring bins within two units are one level (a height on a bin
    // edge splits across both).
    std::vector<std::pair<double, uint32_t>> sums;
    for (const auto& [key, bin] : bins) {
        if (!sums.empty() && bin.first / bin.second - sums.back().first / sums.back().second <= 2.0) {
            sums.back().first += bin.first;
            sums.back().second += bin.second;
        } else {
            sums.push_back(bin);
        }
    }
    std::vector<std::pair<float, uint32_t>> levels;
    for (const auto& [sum, count] : sums) levels.push_back({float(sum / count), count});
    std::stable_sort(levels.begin(), levels.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    for (size_t i = 0; i < std::min<size_t>(levels.size(), kPicaWaterMapLevels); ++i)
        map.Levels.push_back(levels[i].first);
    std::sort(map.Levels.begin(), map.Levels.end());
    map.HeightRange = {std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest()};
    for (const auto& texel : map.Texels) {
        if (std::isnan(texel[0])) continue;
        // The surface anywhere in the texel (its corners).
        const float reach = 0.5F * map.Texel * (std::abs(texel[2]) + std::abs(texel[3]));
        map.HeightRange = {std::min(map.HeightRange[0], texel[0] - reach),
                           std::max(map.HeightRange[1], texel[0] + reach)};
    }
    if (map.Levels.empty()) map.HeightRange = {};
    return map;
}

// Connected regions of a map: pieces (texels whose surfaces meet, within two
// units: a level piece, a sloped stream) and landings (foam above a quarter), largest first; their world
// bounds and height. For tools that frame every piece of the drawn water.
struct PicaWaterMapRegion {
    bool Landing = false;
    float Height = 0.0F;
    std::array<float, 2> MinimumXZ{};
    std::array<float, 2> MaximumXZ{};
    uint32_t Texels = 0;
};

[[nodiscard]] inline std::vector<PicaWaterMapRegion> PicaWaterMapRegions(const PicaWaterMap& map) {
    std::vector<PicaWaterMapRegion> regions;
    if (map.Size == 0U) return regions;
    const size_t size = map.Size;
    for (const bool landing : {false, true}) {
        std::vector<uint8_t> seen(size * size, 0U);
        std::vector<size_t> stack;
        for (size_t start = 0; start < seen.size(); ++start) {
            const auto& first = map.Texels[start];
            if (seen[start] || std::isnan(first[0]) || (landing && first[1] <= 0.25F)) continue;
            PicaWaterMapRegion region;
            region.Landing = landing;
            region.Height = first[0];
            region.MinimumXZ = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
            region.MaximumXZ = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
            stack.assign(1U, start);
            seen[start] = 1U;
            while (!stack.empty()) {
                const size_t index = stack.back();
                stack.pop_back();
                const auto& here = map.Texels[index];
                const size_t u = index % size, v = index / size;
                const float x = map.Origin[0] + float(u) * map.Texel, z = map.Origin[1] + float(v) * map.Texel;
                region.MinimumXZ = {std::min(region.MinimumXZ[0], x), std::min(region.MinimumXZ[1], z)};
                region.MaximumXZ = {std::max(region.MaximumXZ[0], x + map.Texel),
                                    std::max(region.MaximumXZ[1], z + map.Texel)};
                ++region.Texels;
                const std::array<std::pair<int, int>, 4> steps{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
                for (const auto& [du, dv] : steps) {
                    const int nu = int(u) + du, nv = int(v) + dv;
                    if (nu < 0 || nv < 0 || nu >= int(size) || nv >= int(size)) continue;
                    const size_t next = size_t(nv) * size + size_t(nu);
                    const auto& texel = map.Texels[next];
                    // Its surface where this one's plane continues.
                    const float continued = here[0] + (float(du) * here[2] + float(dv) * here[3]) * map.Texel;
                    if (seen[next] || std::isnan(texel[0]) || std::abs(texel[0] - continued) > 2.0F ||
                        (landing && texel[1] <= 0.25F))
                        continue;
                    seen[next] = 1U;
                    stack.push_back(next);
                }
            }
            regions.push_back(region);
        }
    }
    std::stable_sort(regions.begin(), regions.end(),
                     [](const auto& a, const auto& b) { return a.Texels > b.Texels; });
    return regions;
}

} // namespace Fast::Renderer3ds
