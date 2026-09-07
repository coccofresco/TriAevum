#include "oot3d/renderer/pica_texture_decode.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>

namespace Oot3d::Renderer {
namespace {

uint8_t Expand4To8(uint8_t value) {
    return static_cast<uint8_t>((value << 4U) | value);
}

uint8_t Expand5To8(uint8_t value) {
    return static_cast<uint8_t>((value << 3U) | (value >> 2U));
}

uint8_t Expand6To8(uint8_t value) {
    return static_cast<uint8_t>((value << 2U) | (value >> 4U));
}

int SignExtend(uint32_t value, uint32_t bits) {
    const uint32_t signBit = 1U << (bits - 1U);
    return static_cast<int>((value ^ signBit) - signBit);
}

uint8_t ClampU8(int value) {
    return static_cast<uint8_t>(std::clamp(value, 0, 255));
}

uint32_t Morton8(uint32_t x, uint32_t y) {
    return ((x & 0x1U) << 0U) | ((y & 0x1U) << 1U) |
           ((x & 0x2U) << 1U) | ((y & 0x2U) << 2U) |
           ((x & 0x4U) << 2U) | ((y & 0x4U) << 3U);
}

uint32_t AlignedTextureDimension(uint32_t value) {
    return std::max<uint32_t>(8U, (value + 7U) & ~7U);
}

uint16_t ReadU16Le(std::span<const uint8_t> bytes, size_t offset) {
    if (offset + 2U > bytes.size()) {
        throw std::runtime_error("texture data is truncated");
    }
    return static_cast<uint16_t>(bytes[offset]) |
           (static_cast<uint16_t>(bytes[offset + 1U]) << 8U);
}

uint64_t ReadU64Le(std::span<const uint8_t> bytes, size_t offset) {
    if (offset + 8U > bytes.size()) {
        throw std::runtime_error("ETC1 texture data is truncated");
    }
    uint64_t value = 0;
    for (size_t index = 0; index < 8U; ++index) {
        value |= static_cast<uint64_t>(bytes[offset + index])
                 << (index * 8U);
    }
    return value;
}

std::vector<uint8_t> Detile(
    std::span<const uint8_t> bytes, uint32_t width, uint32_t height,
    uint32_t bytesPerPixel) {
    const uint32_t alignedWidth = AlignedTextureDimension(width);
    std::vector<uint8_t> output(
        static_cast<size_t>(width) * height * bytesPerPixel);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t tileIndex =
                (y / 8U) * (alignedWidth / 8U) + (x / 8U);
            const uint32_t sourcePixel =
                tileIndex * 64U + Morton8(x % 8U, y % 8U);
            const size_t source =
                static_cast<size_t>(sourcePixel) * bytesPerPixel;
            const size_t destination =
                (static_cast<size_t>(y) * width + x) * bytesPerPixel;
            if (source + bytesPerPixel > bytes.size()) {
                throw std::runtime_error(
                    "tiled texture data is truncated");
            }
            std::copy_n(
                bytes.begin() + static_cast<std::ptrdiff_t>(source),
                bytesPerPixel,
                output.begin() +
                    static_cast<std::ptrdiff_t>(destination));
        }
    }
    return output;
}

std::vector<uint8_t> Detile4Bpp(
    std::span<const uint8_t> bytes, uint32_t width, uint32_t height) {
    const uint32_t alignedWidth = AlignedTextureDimension(width);
    std::vector<uint8_t> output(static_cast<size_t>(width) * height);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t sourcePixel =
                ((y / 8U) * (alignedWidth / 8U) + (x / 8U)) * 64U +
                Morton8(x % 8U, y % 8U);
            const size_t source = sourcePixel / 2U;
            if (source >= bytes.size()) {
                throw std::runtime_error(
                    "tiled 4bpp texture data is truncated");
            }
            const uint8_t packed = bytes[source];
            output[static_cast<size_t>(y) * width + x] =
                (sourcePixel & 1U) != 0U
                    ? static_cast<uint8_t>(packed >> 4U)
                    : static_cast<uint8_t>(packed & 0x0FU);
        }
    }
    return output;
}

void PushRgba(std::vector<uint8_t>& output, uint8_t red, uint8_t green,
              uint8_t blue, uint8_t alpha) {
    output.push_back(red);
    output.push_back(green);
    output.push_back(blue);
    output.push_back(alpha);
}

std::vector<uint8_t> DecodeRgb565(
    std::span<const uint8_t> bytes, uint32_t pixelCount) {
    std::vector<uint8_t> output;
    output.reserve(static_cast<size_t>(pixelCount) * 4U);
    for (uint32_t pixel = 0; pixel < pixelCount; ++pixel) {
        const uint16_t value = ReadU16Le(bytes, pixel * 2U);
        PushRgba(output, Expand5To8((value >> 11U) & 0x1FU),
                 Expand6To8((value >> 5U) & 0x3FU),
                 Expand5To8(value & 0x1FU), 255U);
    }
    return output;
}

std::vector<uint8_t> DecodeRgba4444(
    std::span<const uint8_t> bytes, uint32_t pixelCount) {
    std::vector<uint8_t> output;
    output.reserve(static_cast<size_t>(pixelCount) * 4U);
    for (uint32_t pixel = 0; pixel < pixelCount; ++pixel) {
        const uint16_t value = ReadU16Le(bytes, pixel * 2U);
        PushRgba(output, Expand4To8((value >> 12U) & 0x0FU),
                 Expand4To8((value >> 8U) & 0x0FU),
                 Expand4To8((value >> 4U) & 0x0FU),
                 Expand4To8(value & 0x0FU));
    }
    return output;
}

std::vector<uint8_t> DecodeLa8(
    std::span<const uint8_t> bytes, uint32_t pixelCount) {
    if (bytes.size() < static_cast<size_t>(pixelCount) * 2U) {
        throw std::runtime_error("LA8 texture data is truncated");
    }
    std::vector<uint8_t> output;
    output.reserve(static_cast<size_t>(pixelCount) * 4U);
    for (uint32_t pixel = 0; pixel < pixelCount; ++pixel) {
        const uint8_t alpha = bytes[pixel * 2U];
        const uint8_t luminance = bytes[pixel * 2U + 1U];
        PushRgba(output, luminance, luminance, luminance, alpha);
    }
    return output;
}

std::vector<uint8_t> DecodeL8(
    std::span<const uint8_t> bytes, uint32_t pixelCount) {
    if (bytes.size() < pixelCount) {
        throw std::runtime_error("L8 texture data is truncated");
    }
    std::vector<uint8_t> output;
    output.reserve(static_cast<size_t>(pixelCount) * 4U);
    for (uint32_t pixel = 0; pixel < pixelCount; ++pixel) {
        PushRgba(output, bytes[pixel], bytes[pixel], bytes[pixel], 255U);
    }
    return output;
}

std::vector<uint8_t> DecodeL4(
    std::span<const uint8_t> nibbles, uint32_t pixelCount) {
    if (nibbles.size() < pixelCount) {
        throw std::runtime_error("L4 texture data is truncated");
    }
    std::vector<uint8_t> output;
    output.reserve(static_cast<size_t>(pixelCount) * 4U);
    for (uint32_t pixel = 0; pixel < pixelCount; ++pixel) {
        const uint8_t value = Expand4To8(nibbles[pixel] & 0x0FU);
        PushRgba(output, value, value, value, 255U);
    }
    return output;
}

std::vector<uint8_t> DecodeLa4(
    std::span<const uint8_t> bytes, uint32_t pixelCount) {
    if (bytes.size() < pixelCount) {
        throw std::runtime_error("LA4 texture data is truncated");
    }
    std::vector<uint8_t> output;
    output.reserve(static_cast<size_t>(pixelCount) * 4U);
    for (uint32_t pixel = 0; pixel < pixelCount; ++pixel) {
        const uint8_t luminance = Expand4To8(bytes[pixel] >> 4U);
        const uint8_t alpha = Expand4To8(bytes[pixel] & 0x0FU);
        PushRgba(output, luminance, luminance, luminance, alpha);
    }
    return output;
}

constexpr std::array<std::array<int, 2>, 8> kEtc1ModifierTable{{
    {2, 8}, {5, 17}, {9, 29}, {13, 42},
    {18, 60}, {24, 80}, {33, 106}, {47, 183},
}};

std::array<uint8_t, 3> SampleEtc1(
    uint64_t raw, uint32_t x, uint32_t y) {
    const uint32_t texel = 4U * x + y;
    const bool flip = ((raw >> 32U) & 1U) != 0U;
    const bool differential = ((raw >> 33U) & 1U) != 0U;
    const uint32_t table2 = static_cast<uint32_t>((raw >> 34U) & 0x7U);
    const uint32_t table1 = static_cast<uint32_t>((raw >> 37U) & 0x7U);
    const uint32_t lookupX = flip ? y : x;

    int red = 0;
    int green = 0;
    int blue = 0;
    if (differential) {
        int baseRed = static_cast<int>((raw >> 59U) & 0x1FU);
        int baseGreen = static_cast<int>((raw >> 51U) & 0x1FU);
        int baseBlue = static_cast<int>((raw >> 43U) & 0x1FU);
        if (lookupX >= 2U) {
            baseRed += SignExtend(
                static_cast<uint32_t>((raw >> 56U) & 0x7U), 3U);
            baseGreen += SignExtend(
                static_cast<uint32_t>((raw >> 48U) & 0x7U), 3U);
            baseBlue += SignExtend(
                static_cast<uint32_t>((raw >> 40U) & 0x7U), 3U);
        }
        red = Expand5To8(static_cast<uint8_t>(baseRed & 0x1F));
        green = Expand5To8(static_cast<uint8_t>(baseGreen & 0x1F));
        blue = Expand5To8(static_cast<uint8_t>(baseBlue & 0x1F));
    } else if (lookupX < 2U) {
        red = Expand4To8(static_cast<uint8_t>((raw >> 60U) & 0x0FU));
        green = Expand4To8(static_cast<uint8_t>((raw >> 52U) & 0x0FU));
        blue = Expand4To8(static_cast<uint8_t>((raw >> 44U) & 0x0FU));
    } else {
        red = Expand4To8(static_cast<uint8_t>((raw >> 56U) & 0x0FU));
        green = Expand4To8(static_cast<uint8_t>((raw >> 48U) & 0x0FU));
        blue = Expand4To8(static_cast<uint8_t>((raw >> 40U) & 0x0FU));
    }

    const uint32_t table = lookupX < 2U ? table1 : table2;
    const uint32_t subIndex =
        static_cast<uint32_t>((raw >> texel) & 1U);
    int modifier = kEtc1ModifierTable[table][subIndex];
    if (((raw >> (16U + texel)) & 1U) != 0U) {
        modifier = -modifier;
    }
    return {ClampU8(red + modifier), ClampU8(green + modifier),
            ClampU8(blue + modifier)};
}

std::vector<uint8_t> DecodeEtc1(
    std::span<const uint8_t> bytes, uint32_t width, uint32_t height,
    bool hasAlpha) {
    const uint32_t subtileSize = hasAlpha ? 16U : 8U;
    const uint32_t tileSize = subtileSize * 4U;
    const uint32_t tilesX = std::max<uint32_t>(1U, (width + 7U) / 8U);
    std::vector<uint8_t> output(
        static_cast<size_t>(width) * height * 4U);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t fineX = x % 8U;
            const uint32_t fineY = y % 8U;
            const uint32_t tileBase =
                ((y / 8U) * tilesX + (x / 8U)) * tileSize;
            const uint32_t subtileIndex =
                fineX / 4U + 2U * (fineY / 4U);
            const uint32_t subtileBase =
                tileBase + subtileIndex * subtileSize;
            const uint32_t localX = fineX % 4U;
            const uint32_t localY = fineY % 4U;

            uint8_t alpha = 255U;
            uint32_t etcBase = subtileBase;
            if (hasAlpha) {
                const uint64_t packedAlpha =
                    ReadU64Le(bytes, subtileBase);
                const uint32_t alphaNibble = static_cast<uint32_t>(
                    (packedAlpha >>
                     (4U * (localX * 4U + localY))) &
                    0x0FU);
                alpha = Expand4To8(static_cast<uint8_t>(alphaNibble));
                etcBase += 8U;
            }
            const auto rgb =
                SampleEtc1(ReadU64Le(bytes, etcBase), localX, localY);
            const size_t destination =
                (static_cast<size_t>(y) * width + x) * 4U;
            output[destination] = rgb[0];
            output[destination + 1U] = rgb[1];
            output[destination + 2U] = rgb[2];
            output[destination + 3U] = alpha;
        }
    }
    return output;
}

std::vector<uint8_t> Decode(
    uint8_t nativeFormat, uint16_t width, uint16_t height,
    std::span<const uint8_t> bytes) {
    const uint32_t pixelCount = static_cast<uint32_t>(width) * height;
    if (width == 0U || height == 0U) {
        return {};
    }
    if (nativeFormat == 12U) {
        return DecodeEtc1(bytes, width, height, false);
    }
    if (nativeFormat == 13U) {
        return DecodeEtc1(bytes, width, height, true);
    }
    if (nativeFormat == 10U) {
        const auto detiled = Detile4Bpp(bytes, width, height);
        return DecodeL4(detiled, pixelCount);
    }
    if (nativeFormat == 11U) {
        const auto detiled = Detile4Bpp(bytes, width, height);
        std::vector<uint8_t> output;
        output.reserve(static_cast<size_t>(pixelCount) * 4U);
        for (const uint8_t value : detiled) {
            PushRgba(output, 0U, 0U, 0U,
                     Expand4To8(value & 0x0FU));
        }
        return output;
    }

    constexpr std::array<uint8_t, 10> kBytesPerPixel{
        4U, 3U, 2U, 2U, 2U, 2U, 2U, 1U, 1U, 1U};
    if (nativeFormat >= kBytesPerPixel.size()) {
        throw std::runtime_error(
            "unsupported native PICA texture format");
    }
    const auto detiled = Detile(
        bytes, width, height, kBytesPerPixel[nativeFormat]);
    switch (nativeFormat) {
        case 0U: {
            std::vector<uint8_t> output;
            output.reserve(static_cast<size_t>(pixelCount) * 4U);
            for (uint32_t pixel = 0; pixel < pixelCount; ++pixel) {
                const size_t offset = static_cast<size_t>(pixel) * 4U;
                PushRgba(output, detiled[offset + 3U],
                         detiled[offset + 2U],
                         detiled[offset + 1U], detiled[offset]);
            }
            return output;
        }
        case 1U: {
            std::vector<uint8_t> output;
            output.reserve(static_cast<size_t>(pixelCount) * 4U);
            for (uint32_t pixel = 0; pixel < pixelCount; ++pixel) {
                const size_t offset = static_cast<size_t>(pixel) * 3U;
                PushRgba(output, detiled[offset + 2U],
                         detiled[offset + 1U], detiled[offset], 255U);
            }
            return output;
        }
        case 2U: {
            std::vector<uint8_t> output;
            output.reserve(static_cast<size_t>(pixelCount) * 4U);
            for (uint32_t pixel = 0; pixel < pixelCount; ++pixel) {
                const uint16_t value = ReadU16Le(detiled, pixel * 2U);
                PushRgba(output,
                         Expand5To8((value >> 11U) & 0x1FU),
                         Expand5To8((value >> 6U) & 0x1FU),
                         Expand5To8((value >> 1U) & 0x1FU),
                         (value & 1U) != 0U ? 255U : 0U);
            }
            return output;
        }
        case 3U:
            return DecodeRgb565(detiled, pixelCount);
        case 4U:
            return DecodeRgba4444(detiled, pixelCount);
        case 5U:
            return DecodeLa8(detiled, pixelCount);
        case 6U: {
            std::vector<uint8_t> output;
            output.reserve(static_cast<size_t>(pixelCount) * 4U);
            for (uint32_t pixel = 0; pixel < pixelCount; ++pixel) {
                const size_t offset = static_cast<size_t>(pixel) * 2U;
                PushRgba(output, detiled[offset + 1U],
                         detiled[offset], 0U, 255U);
            }
            return output;
        }
        case 7U:
            return DecodeL8(detiled, pixelCount);
        case 8U: {
            std::vector<uint8_t> output;
            output.reserve(static_cast<size_t>(pixelCount) * 4U);
            for (const uint8_t alpha : detiled) {
                PushRgba(output, 0U, 0U, 0U, alpha);
            }
            return output;
        }
        case 9U:
            return DecodeLa4(detiled, pixelCount);
        default:
            throw std::runtime_error(
                "unsupported native PICA texture format");
    }
}

} // namespace

bool DecodePicaTextureRgba8(
    uint8_t nativeFormat, uint16_t width, uint16_t height,
    std::span<const uint8_t> nativeBytes, std::vector<uint8_t>& rgba8,
    std::string* error) {
    rgba8.clear();
    try {
        rgba8 = Decode(nativeFormat, width, height, nativeBytes);
        if (rgba8.size() !=
            static_cast<size_t>(width) * height * 4U) {
            if (error != nullptr) {
                *error =
                    "native PICA texture did not decode to RGBA8";
            }
            rgba8.clear();
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        rgba8.clear();
        return false;
    }
}

} // namespace Oot3d::Renderer
