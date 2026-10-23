#pragma once
// PICA scissor mode 1 (exclude) rasterizes everything outside a rectangle.
// Vulkan has one scissor rectangle per draw, so the visible region becomes up
// to four disjoint rectangles and the draw is submitted once per rectangle.
// Disjointness keeps blending and stencil at exactly one execution per
// retained fragment.
//
// Adapted from Ocarina ReTuned (OTPR26, GPL), tools/reframed/exclude_scissor.h.
#include <algorithm>
#include <array>
#include <cstdint>

namespace Fast::Renderer3ds {

struct PicaScissorRect {
    int32_t X = 0;
    int32_t Y = 0;
    uint32_t Width = 0;
    uint32_t Height = 0;
};

struct PicaScissorParts {
    std::array<PicaScissorRect, 4> Rects{};
    uint32_t Count = 0;
};

inline PicaScissorParts ExcludePicaScissor(PicaScissorRect bounds, PicaScissorRect hole) {
    PicaScissorParts result;
    const int64_t left = bounds.X;
    const int64_t top = bounds.Y;
    const int64_t right = left + bounds.Width;
    const int64_t bottom = top + bounds.Height;
    const int64_t x1 = std::clamp<int64_t>(hole.X, left, right);
    const int64_t y1 = std::clamp<int64_t>(hole.Y, top, bottom);
    const int64_t x2 = std::clamp<int64_t>(int64_t{ hole.X } + hole.Width, left, right);
    const int64_t y2 = std::clamp<int64_t>(int64_t{ hole.Y } + hole.Height, top, bottom);
    const auto add = [&](int64_t x, int64_t y, int64_t r, int64_t b) {
        if (r > x && b > y) {
            result.Rects[result.Count++] = { static_cast<int32_t>(x), static_cast<int32_t>(y),
                                             static_cast<uint32_t>(r - x), static_cast<uint32_t>(b - y) };
        }
    };
    if (x2 <= x1 || y2 <= y1) {
        add(left, top, right, bottom);
        return result;
    }
    add(left, top, right, y1);
    add(left, y2, right, bottom);
    add(left, y1, x1, y2);
    add(x2, y1, right, y2);
    return result;
}

} // namespace Fast::Renderer3ds
