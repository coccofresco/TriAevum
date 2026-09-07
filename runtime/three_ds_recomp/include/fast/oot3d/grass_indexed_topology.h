#pragma once

#include "fast/oot3d/grass_blade_geometry.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <vector>

namespace Fast::Oot3d {

struct GrassIndexRange {
    uint32_t First = 0;
    uint32_t Count = 0;
};

// Triangle order and winding match the former expanded procedural stream.
// Each row has two unique vertices, plus a single tip, independently per plane.
struct GrassIndexedTopology {
    std::vector<uint16_t> Indices;
    std::array<std::array<GrassIndexRange, 2>, kMaximumGrassBladeSegments> Blades{};
    GrassIndexRange Tuft;
};

inline GrassIndexedTopology BuildGrassIndexedTopology() {
    GrassIndexedTopology result;
    for (uint16_t segments = 1; segments <= kMaximumGrassBladeSegments; ++segments) {
        for (uint16_t planes = 1; planes <= 2; ++planes) {
            auto& range = result.Blades[segments - 1][planes - 1];
            range.First = static_cast<uint32_t>(result.Indices.size());
            for (uint16_t plane = 0; plane < planes; ++plane) {
                const uint16_t base = plane * (2 * segments + 1);
                for (uint16_t row = 0; row < segments; ++row) {
                    const uint16_t left = base + 2 * row;
                    if (row + 1 == segments) {
                        for (const uint16_t offset : {0, 1, 2}) result.Indices.push_back(left + offset);
                    } else {
                        for (const uint16_t offset : {0, 1, 3, 0, 3, 2}) result.Indices.push_back(left + offset);
                    }
                }
            }
            range.Count = static_cast<uint32_t>(result.Indices.size()) - range.First;
        }
    }
    result.Tuft = {static_cast<uint32_t>(result.Indices.size()), 6};
    result.Indices.insert(result.Indices.end(), {0, 1, 2, 0, 2, 3});
    return result;
}

inline constexpr std::string_view kGrassIndexedVertexShader = R"glsl(
void grass_indexed_vertex(uint vertex, uint segments, bool tuft,
                          out uint plane, out float height_factor, out float width_sign) {
    uint plane_vertices = tuft ? 4u : 2u*segments+1u;
    plane = vertex / plane_vertices;
    uint local_vertex = vertex % plane_vertices;
    if (tuft) {
        height_factor = local_vertex >= 2u ? 1.0 : 0.0;
        width_sign = local_vertex == 0u || local_vertex == 3u ? -1.0 : 1.0;
    } else {
        height_factor = float(local_vertex/2u)/float(segments);
        width_sign = local_vertex == 2u*segments ? 0.0 : (local_vertex%2u == 0u ? -1.0 : 1.0);
    }
}
)glsl";

} // namespace Fast::Oot3d
