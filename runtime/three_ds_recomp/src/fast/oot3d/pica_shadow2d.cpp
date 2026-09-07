#include "fast/oot3d/pica_shadow2d.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace Fast::Oot3d {
namespace {

uint32_t Morton8(uint32_t x, uint32_t y) {
    return ((x & 1U) << 0U) | ((y & 1U) << 1U) |
           ((x & 2U) << 1U) | ((y & 2U) << 2U) |
           ((x & 4U) << 2U) | ((y & 4U) << 3U);
}

void SetError(std::string* error, const char* message) {
    if (error != nullptr) *error = message;
}

} // namespace

bool DetilePicaShadow2d(std::span<const uint8_t> tiledBytes,
                        uint32_t width, uint32_t height,
                        std::vector<uint32_t>& linearPixels,
                        std::string* error) {
    linearPixels.clear();
    if (width == 0U || height == 0U) {
        SetError(error, "PICA Shadow2D dimensions are empty");
        return false;
    }
    const uint32_t alignedWidth = std::max(8U, (width + 7U) & ~7U);
    const uint32_t alignedHeight = std::max(8U, (height + 7U) & ~7U);
    const size_t requiredBytes =
        static_cast<size_t>(alignedWidth) * alignedHeight * sizeof(uint32_t);
    if (tiledBytes.size() < requiredBytes) {
        SetError(error, "PICA Shadow2D tiled buffer is truncated");
        return false;
    }
    linearPixels.resize(static_cast<size_t>(width) * height);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t tile =
                (y / 8U) * (alignedWidth / 8U) + x / 8U;
            const size_t source =
                static_cast<size_t>(tile * 64U + Morton8(x & 7U, y & 7U)) *
                sizeof(uint32_t);
            std::memcpy(&linearPixels[static_cast<size_t>(y) * width + x],
                        tiledBytes.data() + source, sizeof(uint32_t));
        }
    }
    return true;
}

float DecodePicaFloat16(uint16_t bits) {
    const uint32_t sign = static_cast<uint32_t>(bits & 0x8000U) << 16U;
    const uint32_t exponent = (bits >> 10U) & 0x1FU;
    const uint32_t fraction = bits & 0x3FFU;
    uint32_t ieee = sign;
    if (exponent == 0U) {
        if (fraction != 0U) {
            uint32_t normalized = fraction;
            uint32_t shift = 0U;
            while ((normalized & 0x400U) == 0U) {
                normalized <<= 1U;
                ++shift;
            }
            normalized &= 0x3FFU;
            ieee |= (113U - shift) << 23U;
            ieee |= normalized << 13U;
        }
    } else if (exponent == 0x1FU) {
        ieee |= 0x7F800000U | (fraction << 13U);
    } else {
        ieee |= (exponent + 112U) << 23U;
        ieee |= fraction << 13U;
    }
    return std::bit_cast<float>(ieee);
}

uint32_t PackPicaShadow2d(uint32_t depth24, uint32_t penumbra8) {
    return (std::min(depth24, 0xFFFFFFU) << 8U) |
           std::min(penumbra8, 0xFFU);
}

uint32_t UpdatePicaShadow2d(uint32_t packedPixel,
                            uint32_t fragmentDepth24,
                            uint32_t fragmentPenumbra8,
                            float biasConstant,
                            float biasLinear) {
    uint32_t referenceDepth = packedPixel >> 8U;
    uint32_t referencePenumbra = packedPixel & 0xFFU;
    const uint32_t depth = std::min(fragmentDepth24, 0xFFFFFFU);
    const uint32_t penumbra = std::min(fragmentPenumbra8, 0xFFU);
    if (depth >= referenceDepth) return packedPixel;
    if (penumbra == 0U) {
        referenceDepth = depth;
    } else if (referenceDepth != 0U) {
        const float divisor = biasConstant + biasLinear *
            static_cast<float>(depth) / static_cast<float>(referenceDepth);
        const float scaled = static_cast<float>(penumbra) / divisor;
        const uint32_t adjusted =
            std::isfinite(scaled) && scaled > 0.0F
                ? (scaled >= static_cast<float>(
                                 std::numeric_limits<uint32_t>::max())
                       ? std::numeric_limits<uint32_t>::max()
                       : static_cast<uint32_t>(scaled))
                : (scaled <= 0.0F ? 0U
                                  : std::numeric_limits<uint32_t>::max());
        referencePenumbra = std::min(referencePenumbra, adjusted);
    }
    return PackPicaShadow2d(referenceDepth, referencePenumbra);
}

float ComparePicaShadow2d(uint32_t packedPixel,
                          uint32_t fragmentDepth24) {
    const uint32_t storedDepth = packedPixel >> 8U;
    if (storedDepth <= std::min(fragmentDepth24, 0xFFFFFFU)) return 0.0F;
    return static_cast<float>(packedPixel & 0xFFU) / 255.0F;
}

} // namespace Fast::Oot3d
