#pragma once

#include <array>
#include <cstdint>

namespace Fast::Oot3d {

struct TemporalJitterSample {
    std::array<float, 2> PixelOffset{};
};

// Halton(2,3) sequence centered on the pixel. TAA defaults to eight phases;
// temporal upscalers can request the larger provider-specific phase count.
[[nodiscard]] TemporalJitterSample TemporalJitterForFrame(
    uint64_t frameId, uint32_t phaseCount = 8U);

[[nodiscard]] std::array<float, 16> ApplyTemporalJitterToClipMatrix(
    const std::array<float, 16>& matrix,
    const std::array<float, 2>& pixelOffset,
    float viewportWidth, float viewportHeight);

[[nodiscard]] bool HasPerspectiveClipW(
    const std::array<float, 16>& matrix);

} // namespace Fast::Oot3d
