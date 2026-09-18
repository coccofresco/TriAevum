#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace Fast::Renderer3ds {
// Resolve an internally scaled RGBA attachment to its native tiled bytes.
// Native image axes are retained; no screen rotation or presentation effects.
inline std::vector<uint8_t> EncodePicaFramebuffer(
    std::span<const uint8_t> rgba, uint32_t imageWidth, uint32_t imageHeight,
    uint32_t width, uint32_t height, uint8_t format) {
    if (width == 0 || height == 0 || width % 8 || height % 8 || format > 4 ||
        imageWidth == 0 || imageHeight == 0 ||
        rgba.size() % 4 != 0 || uint64_t{imageWidth} * imageHeight != rgba.size() / 4)
        throw std::runtime_error("invalid PICA framebuffer encode extent/format");
    const uint32_t bpp = format == 0 ? 4 : format == 1 ? 3 : 2;
    if (uint64_t{width} * height > std::numeric_limits<std::size_t>::max() / bpp)
        throw std::runtime_error("PICA framebuffer byte size overflows");
    std::vector<uint8_t> result(std::size_t{width} * height * bpp);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const auto sx = static_cast<uint32_t>((uint64_t{x} * 2 + 1) * imageWidth / (uint64_t{width} * 2));
            const auto sy = static_cast<uint32_t>((uint64_t{y} * 2 + 1) * imageHeight / (uint64_t{height} * 2));
            const auto* c = rgba.data() + (std::size_t{sy} * imageWidth + sx) * 4;
            uint32_t morton = 0;
            for (uint32_t bit = 0; bit < 3; ++bit) {
                morton |= ((x >> bit) & 1) << (bit * 2);
                morton |= ((y >> bit) & 1) << (bit * 2 + 1);
            }
            auto* out = result.data() + (((y / 8) * (width / 8) + x / 8) * 64 + morton) * bpp;
            if (format == 0) { out[0] = c[3]; out[1] = c[2]; out[2] = c[1]; out[3] = c[0]; }
            else if (format == 1) { out[0] = c[2]; out[1] = c[1]; out[2] = c[0]; }
            else {
                const uint16_t packed = format == 2 ? ((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3) :
                    format == 3 ? ((c[0] >> 3) << 11) | ((c[1] >> 3) << 6) | ((c[2] >> 3) << 1) | (c[3] >> 7) :
                    ((c[0] >> 4) << 12) | ((c[1] >> 4) << 8) | ((c[2] >> 4) << 4) | (c[3] >> 4);
                out[0] = static_cast<uint8_t>(packed); out[1] = static_cast<uint8_t>(packed >> 8);
            }
        }
    }
    return result;
}
} // namespace Fast::Renderer3ds
