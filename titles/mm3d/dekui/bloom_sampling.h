// Ported from mm3d-dekui src/mm3d/bloom_sampling.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <cstdint>

namespace Mm3dDekui::BloomSampling {
// Enabled by default; player Graphics setting applies immediately.
inline std::atomic<bool> enabled{true};
inline std::atomic<unsigned> bindings{0};
struct Draw {
    std::uint64_t shader, swizzle;
    unsigned vertices, width, height, format, viewport_x, viewport_y;
    bool blending, feedback;
};
// Captured MM3D effect consumers. Do not match the 72-vertex forest scenery
// draw, which shares this shader, or other UI using the same shader.
constexpr bool Matches(const Draw& d) {
    if (d.shader != 0x89b1ab98da3d9241ULL || d.swizzle != 0x6f0b398b1fc186a9ULL ||
        d.vertices != 4 || d.format != 0 || !d.blending || d.feedback)
        return false;
    const bool small = d.viewport_x == 4325376 && d.viewport_y == 4390912;
    const bool medium = d.viewport_x == 4390912 && d.viewport_y == 4456448;
    const bool large = d.viewport_x == 4456448 && d.viewport_y == 4521984;
    const bool composite = d.viewport_x == 4644864 && d.viewport_y == 4624384;
    return (d.width == 256 && d.height == 256 && large) ||
           (d.width == 32 && d.height == 64 && (small || medium || composite)) ||
           (d.width == 16 && d.height == 32 && (small || composite));
}
}
