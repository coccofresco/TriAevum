#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Fast::Oot3d {

enum class DepthConvention : uint8_t {
    Perspective,
    ReversedPerspective,
    WBuffer,
    ReversedWBuffer,
};

struct HiZMipExtent {
    uint32_t Width = 0;
    uint32_t Height = 0;
};

struct HiZPyramidLayout {
    std::vector<HiZMipExtent> Mips;
    uint64_t TexelCount = 0;
    [[nodiscard]] uint64_t R32ByteSize() const { return TexelCount * 4U; }
};

// Converts the native depth sample to positive view-space distance. Keeping
// this contract outside the Vulkan pass lets CACAO and SSR share the exact
// same interpretation of PICA depth.
[[nodiscard]] float LinearizeDepth(float sample, float nearPlane,
                                   float farPlane,
                                   DepthConvention convention);

// Hi-Z stores the closest covered surface in every tile. Ray marching can
// therefore conservatively skip a tile only when the ray is in front of it.
[[nodiscard]] float ReduceHiZClosest(float a, float b, float c, float d);

[[nodiscard]] uint32_t ResolveHiZMipCount(uint32_t width,
                                          uint32_t height) noexcept;

[[nodiscard]] HiZPyramidLayout BuildHiZPyramidLayout(uint32_t width,
                                                      uint32_t height);

// One compute dispatch writes one destination mip. For mip zero SourceIsRaw
// is set and the shader linearizes native depth; following dispatches reduce
// four source texels into one R32_SFLOAT texel.
[[nodiscard]] std::string BuildHiZReductionComputeShader();

} // namespace Fast::Oot3d
