#pragma once

#include <string>
#include <algorithm>
#include <cstdint>

namespace Fast::Oot3d {

// Reusable compute-shader library for depth/normal toon outlines. The
// caller supplies separate texture and sampler objects so the library stays
// compatible with NRI's Vulkan descriptor model.
[[nodiscard]] std::string ToonOutlineComputeShaderLibrary();
// Caller supplies depth/normal/transparent sample functions for its bindings.
[[nodiscard]] std::string ToonOutlineShaderLibrary();

// Reference pixels keep the visible weight constant across render scales.
inline float ToonOutlineRenderWidth(float width, uint32_t renderWidth,
                                    uint32_t renderHeight) {
    return width * static_cast<float>(std::min(renderWidth, renderHeight)) / 1080.0F;
}

} // namespace Fast::Oot3d
