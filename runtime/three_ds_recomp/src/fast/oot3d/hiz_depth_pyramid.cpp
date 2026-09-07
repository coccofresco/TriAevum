#include "fast/oot3d/hiz_depth_pyramid.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Fast::Oot3d {

float LinearizeDepth(float sample, float nearPlane, float farPlane,
                     DepthConvention convention) {
    if (!std::isfinite(sample) || !std::isfinite(nearPlane) ||
        !std::isfinite(farPlane) || nearPlane <= 0.0F ||
        farPlane <= nearPlane) {
        return std::numeric_limits<float>::infinity();
    }
    const float depth = std::clamp(sample, 0.0F, 1.0F);
    switch (convention) {
        case DepthConvention::Perspective:
            return nearPlane * farPlane /
                   std::max(farPlane - depth * (farPlane - nearPlane),
                            std::numeric_limits<float>::min());
        case DepthConvention::ReversedPerspective:
            return nearPlane * farPlane /
                   std::max(nearPlane + depth * (farPlane - nearPlane),
                            std::numeric_limits<float>::min());
        case DepthConvention::WBuffer:
            return nearPlane + depth * (farPlane - nearPlane);
        case DepthConvention::ReversedWBuffer:
            return farPlane - depth * (farPlane - nearPlane);
    }
    return std::numeric_limits<float>::infinity();
}

float ReduceHiZClosest(float a, float b, float c, float d) {
    return std::min(std::min(a, b), std::min(c, d));
}

uint32_t ResolveHiZMipCount(uint32_t width, uint32_t height) noexcept {
    if (width == 0U || height == 0U) return 0U;
    uint32_t count = 1U;
    while (width != 1U || height != 1U) {
        width = std::max(1U, width / 2U);
        height = std::max(1U, height / 2U);
        ++count;
    }
    return count;
}

HiZPyramidLayout BuildHiZPyramidLayout(uint32_t width, uint32_t height) {
    HiZPyramidLayout result;
    const uint32_t mipCount = ResolveHiZMipCount(width, height);
    if (mipCount == 0U) return result;
    result.Mips.reserve(mipCount);
    while (true) {
        result.Mips.push_back({ width, height });
        result.TexelCount += static_cast<uint64_t>(width) * height;
        if (width == 1 && height == 1) break;
        width = std::max(1U, width / 2U);
        height = std::max(1U, height / 2U);
    }
    return result;
}

std::string BuildHiZReductionComputeShader() {
    return R"glsl(#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform texture2D source_depth;
layout(set = 0, binding = 1, r32f) uniform writeonly image2D destination_hiz;
layout(set = 0, binding = 2) uniform sampler hiz_sampler;

layout(push_constant) uniform HiZPush {
    uvec2 source_extent;
    uvec2 destination_extent;
    float near_plane;
    float far_plane;
    uint depth_convention;
    uint source_is_raw;
} pc;

float oot3d_linearize_depth(float sample_value) {
    float d = clamp(sample_value, 0.0, 1.0);
    float range = pc.far_plane - pc.near_plane;
    if (pc.depth_convention == 0u) {
        return pc.near_plane * pc.far_plane /
               max(pc.far_plane - d * range, 1.17549435e-38);
    }
    if (pc.depth_convention == 1u) {
        return pc.near_plane * pc.far_plane /
               max(pc.near_plane + d * range, 1.17549435e-38);
    }
    return pc.depth_convention == 2u
        ? pc.near_plane + d * range
        : pc.far_plane - d * range;
}

float oot3d_source_depth(ivec2 coordinate) {
    ivec2 bounded = clamp(coordinate, ivec2(0), ivec2(pc.source_extent) - 1);
    float value = texelFetch(sampler2D(source_depth,hiz_sampler), bounded, 0).r;
    return pc.source_is_raw != 0u ? oot3d_linearize_depth(value) : value;
}

void main() {
    ivec2 destination = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(destination, ivec2(pc.destination_extent)))) return;
    if (pc.source_is_raw != 0u) {
        imageStore(destination_hiz, destination,
                   vec4(oot3d_source_depth(destination)));
        return;
    }
    ivec2 source_begin = ivec2(
        (uvec2(destination) * pc.source_extent) /
        pc.destination_extent);
    ivec2 source_end = ivec2(
        (uvec2(destination + 1) * pc.source_extent) /
        pc.destination_extent);
    float closest = 3.402823466e+38;
    for (int y = source_begin.y; y < source_end.y; ++y)
        for (int x = source_begin.x; x < source_end.x; ++x)
            closest = min(
                closest, oot3d_source_depth(ivec2(x, y)));
    imageStore(destination_hiz, destination, vec4(closest));
}
)glsl";
}

} // namespace Fast::Oot3d
