#pragma once

#include <cstdint>
#include <span>

namespace Fast::Renderer {

// Readback conversion only: resample the full drawable, not a logical-size crop.
// Nearest sampling preserves equal-size pixels and does not alter display gamma.
inline bool CopyScaledFramebufferRgba5551(
    std::span<const uint8_t> source, uint32_t sourceWidth, uint32_t sourceHeight,
    bool bgra, std::span<uint16_t> destination, uint32_t width, uint32_t height) {
    if (!sourceWidth || !sourceHeight || !width || !height ||
        uint64_t(sourceWidth) * sourceHeight > source.size() / 4 ||
        uint64_t(width) * height > destination.size()) return false;
    for (uint32_t y = 0; y < height; ++y) {
        const auto sourceY = uint64_t(y) * sourceHeight / height;
        for (uint32_t x = 0; x < width; ++x) {
            const auto sourceX = uint64_t(x) * sourceWidth / width;
            const auto offset = (sourceY * sourceWidth + sourceX) * 4;
            const auto r = source[offset + (bgra ? 2 : 0)];
            const auto g = source[offset + 1];
            const auto b = source[offset + (bgra ? 0 : 2)];
            const auto a = source[offset + 3];
            destination[uint64_t(y) * width + x] = static_cast<uint16_t>(
                ((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | (a != 0));
        }
    }
    return true;
}
} // namespace Fast::Renderer
