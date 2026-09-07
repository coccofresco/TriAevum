#include "fast/oot3d/grass_primitive_topology.h"

namespace Fast::Oot3d {

std::vector<uint32_t> ExpandGrassPrimitiveIndices(
    std::span<const uint32_t> source,
    ::Oot3d::Renderer::PicaTopology topology) {
    using Topology = ::Oot3d::Renderer::PicaTopology;
    std::vector<uint32_t> result;
    if (source.size() < 3U) {
        return result;
    }
    if (topology == Topology::TriangleStrip) {
        result.reserve((source.size() - 2U) * 3U);
        for (size_t index = 2U; index < source.size(); ++index) {
            if ((index & 1U) == 0U) {
                result.insert(
                    result.end(),
                    {source[index - 2U], source[index - 1U],
                     source[index]});
            } else {
                result.insert(
                    result.end(),
                    {source[index - 1U], source[index - 2U],
                     source[index]});
            }
        }
        return result;
    }
    if (topology == Topology::TriangleFan) {
        result.reserve((source.size() - 2U) * 3U);
        for (size_t index = 2U; index < source.size(); ++index) {
            result.insert(
                result.end(),
                {source[0], source[index - 1U], source[index]});
        }
        return result;
    }
    if (topology == Topology::TriangleList ||
        topology == Topology::GeometryShader) {
        const size_t count = source.size() - source.size() % 3U;
        result.assign(source.begin(), source.begin() + count);
    }
    return result;
}

} // namespace Fast::Oot3d
