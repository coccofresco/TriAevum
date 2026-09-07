#pragma once

#include "oot3d/renderer/pica_render_backend.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Fast::Oot3d {

[[nodiscard]] constexpr bool IsGrassPrimitiveTopologySupported(
    ::Oot3d::Renderer::PicaTopology topology) noexcept {
    using Topology = ::Oot3d::Renderer::PicaTopology;
    switch (topology) {
        case Topology::TriangleList:
        case Topology::TriangleStrip:
        case Topology::TriangleFan:
        case Topology::GeometryShader:
            return true;
    }
    return false;
}

// Produces the triangle list consumed by the CPU surface extractor. PICA's
// topology-3 programmable setup arrives here only after the frontend has
// assembled it as the same triangle-list stream used by the main renderer.
[[nodiscard]] std::vector<uint32_t> ExpandGrassPrimitiveIndices(
    std::span<const uint32_t> source,
    ::Oot3d::Renderer::PicaTopology topology);

} // namespace Fast::Oot3d
