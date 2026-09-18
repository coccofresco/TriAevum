#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <tuple>
#include <vector>

namespace Fast::Renderer3ds {

struct PicaRawCopySpan {
    uint32_t SourceAddress = 0;
    uint32_t DestinationAddress = 0;
    uint32_t ByteCount = 0;
};

struct PicaTiledCopySurface {
    uint32_t Address = 0;
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t BytesPerPixel = 0;
    uint32_t NativeFormat = 0;
};

struct PicaTextureCopyRegion {
    uint32_t SourceX = 0, SourceY = 0;
    uint32_t DestinationX = 0, DestinationY = 0;
    uint32_t Width = 0, Height = 0;
};

// Coordinates use native tiled-image axes. Backend orientation and internal
// resolution belong to a later image mapping, never to raw address decoding.
// Partial texels and format reinterpretation require a byte-oriented backend;
// they must not silently turn into filtered/color-converting image copies.
inline bool BuildPicaTiledTextureCopyRegions(
    std::span<const PicaRawCopySpan> spans,
    const PicaTiledCopySurface& source, const PicaTiledCopySurface& destination,
    std::vector<PicaTextureCopyRegion>& regions, std::string* error = nullptr) {
    regions.clear();
    const auto fail = [&](const char* message) {
        if (error != nullptr) *error = message;
        return false;
    };
    const auto validSurface = [](const PicaTiledCopySurface& s) {
        return s.Width != 0 && s.Height != 0 && s.Width % 8 == 0 && s.Height % 8 == 0 &&
               s.NativeFormat <= 4 &&
               s.BytesPerPixel == (s.NativeFormat == 0 ? 4U : s.NativeFormat == 1 ? 3U : 2U) &&
               s.Height <= ((uint64_t{1} << 32) - s.Address) / s.Width / s.BytesPerPixel;
    };
    if (!validSurface(source) || !validSurface(destination) ||
        source.NativeFormat != destination.NativeFormat || spans.empty()) {
        return fail("raw tiled copy requires compatible native surfaces");
    }
    const auto contains = [](const PicaTiledCopySurface& s, uint32_t address, uint32_t bytes) {
        return address >= s.Address && bytes != 0 &&
               uint64_t{address} + bytes <= uint64_t{s.Address} + uint64_t{s.Width} * s.Height * s.BytesPerPixel &&
               (address - s.Address) % s.BytesPerPixel == 0 && bytes % s.BytesPerPixel == 0;
    };
    uint64_t previousDestinationEnd = 0;
    for (const auto& span : spans) {
        if (!contains(source, span.SourceAddress, span.ByteCount) ||
            !contains(destination, span.DestinationAddress, span.ByteCount)) {
            return fail("raw tiled copy range exceeds a surface or splits a texel");
        }
        if (span.DestinationAddress < previousDestinationEnd) {
            return fail("raw tiled copy destination spans overlap or are unordered");
        }
        previousDestinationEnd = uint64_t{span.DestinationAddress} + span.ByteCount;
    }
    const auto xy = [](uint32_t pixel, uint32_t width) {
        const uint32_t tile = pixel / 64;
        const uint32_t morton = pixel % 64;
        const uint32_t x = (morton & 1U) | ((morton >> 1U) & 2U) | ((morton >> 2U) & 4U);
        const uint32_t y = ((morton >> 1U) & 1U) | ((morton >> 2U) & 2U) | ((morton >> 3U) & 4U);
        return std::pair{(tile % (width / 8)) * 8 + x, (tile / (width / 8)) * 8 + y};
    };
    std::vector<PicaTextureCopyRegion> pieces;
    for (const auto& span : spans) {
        uint32_t src = (span.SourceAddress - source.Address) / source.BytesPerPixel;
        uint32_t dst = (span.DestinationAddress - destination.Address) / destination.BytesPerPixel;
        uint32_t left = span.ByteCount / source.BytesPerPixel;
        while (left != 0) {
            const auto [sx, sy] = xy(src, source.Width);
            const auto [dx, dy] = xy(dst, destination.Width);
            const bool tile = src % 64 == 0 && dst % 64 == 0 && left >= 64;
            const uint32_t count = tile ? 64 : 1;
            pieces.push_back({sx, sy, dx, dy, tile ? 8U : 1U, tile ? 8U : 1U});
            src += count;
            dst += count;
            left -= count;
        }
    }
    // Coalesce only equal translations, retaining every untouched gap.
    // No sampling, scaling, clears, or presentation passes are introduced.
    std::sort(pieces.begin(), pieces.end(), [](const auto& a, const auto& b) {
        return std::tie(a.SourceY, a.DestinationY, a.Height, a.SourceX, a.DestinationX) <
               std::tie(b.SourceY, b.DestinationY, b.Height, b.SourceX, b.DestinationX);
    });
    std::vector<PicaTextureCopyRegion> rows;
    for (const auto& p : pieces) {
        if (!rows.empty()) {
            auto& last = rows.back();
            if (last.SourceY == p.SourceY && last.DestinationY == p.DestinationY && last.Height == p.Height &&
                last.SourceX + last.Width == p.SourceX && last.DestinationX + last.Width == p.DestinationX) {
                last.Width += p.Width;
                continue;
            }
        }
        rows.push_back(p);
    }
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        return std::tie(a.SourceX, a.DestinationX, a.Width, a.SourceY, a.DestinationY) <
               std::tie(b.SourceX, b.DestinationX, b.Width, b.SourceY, b.DestinationY);
    });
    for (const auto& row : rows) {
        if (!regions.empty()) {
            auto& last = regions.back();
            if (last.SourceX == row.SourceX && last.DestinationX == row.DestinationX && last.Width == row.Width &&
                last.SourceY + last.Height == row.SourceY && last.DestinationY + last.Height == row.DestinationY) {
                last.Height += row.Height;
                continue;
            }
        }
        regions.push_back(row);
    }
    if (error != nullptr) error->clear();
    return true;
}

} // namespace Fast::Renderer3ds
