#include "fast/oot3d/temporal_jitter.h"

#include <algorithm>
#include <cmath>

namespace Fast::Oot3d {
namespace {

float RadicalInverse(uint32_t index, uint32_t base) {
    float result = 0.0F;
    float fraction = 1.0F / static_cast<float>(base);
    while (index != 0U) {
        result += static_cast<float>(index % base) * fraction;
        index /= base;
        fraction /= static_cast<float>(base);
    }
    return result;
}

} // namespace

TemporalJitterSample TemporalJitterForFrame(uint64_t frameId,
                                             uint32_t phaseCount) {
    phaseCount = std::clamp(phaseCount, 1U, 256U);
    const uint32_t sequenceIndex =
        static_cast<uint32_t>(frameId % phaseCount) + 1U;
    return {{{RadicalInverse(sequenceIndex, 2U) - 0.5F,
              RadicalInverse(sequenceIndex, 3U) - 0.5F}}};
}

std::array<float, 16> ApplyTemporalJitterToClipMatrix(
    const std::array<float, 16>& matrix,
    const std::array<float, 2>& pixelOffset,
    float viewportWidth, float viewportHeight) {
    auto result = matrix;
    if (viewportWidth <= 0.0F || viewportHeight <= 0.0F)
        return result;
    const float jitterX = 2.0F * pixelOffset[0] / viewportWidth;
    const float jitterY = 2.0F * pixelOffset[1] / viewportHeight;
    for (size_t column = 0; column < 4; ++column) {
        const size_t base = column * 4U;
        result[base] += jitterX * matrix[base + 3U];
        result[base + 1U] += jitterY * matrix[base + 3U];
    }
    return result;
}

bool HasPerspectiveClipW(const std::array<float, 16>& matrix) {
    return std::abs(matrix[3]) + std::abs(matrix[7]) +
               std::abs(matrix[11]) > 1.0e-6F;
}

} // namespace Fast::Oot3d
