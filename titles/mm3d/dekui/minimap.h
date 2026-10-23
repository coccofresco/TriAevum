// Ported from mm3d-dekui src/mm3d/minimap.h (tgacquin/mm3d-dekui).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <string_view>
namespace Mm3dDekui::Minimap {
inline std::atomic<bool> enabled{true};
// Native guest drawables replace the experimental framebuffer-copy path.
constexpr bool LegacyCopy = false;
inline std::atomic<float> scale{.36f}, opacity{.85f};
inline std::atomic<unsigned> last_draws{0};
inline std::atomic<bool> last_marker{false};
// Renderer thread only; reset alongside the presentation layers each guest frame.
inline bool marker_seen=false, marker_draw=false;
inline unsigned draws=0;
constexpr std::string_view RadarHash="737db90be66625c5babcadca4050d118a0fdd4d876690fb2703a60e4d1d47a36";
inline bool Candidate(unsigned width,unsigned height) {
    return LegacyCopy && enabled.load() && width==128 && height==64;
}
inline void NextFrame() {
    last_draws.store(draws); last_marker.store(marker_seen);
    marker_seen=marker_draw=false; draws=0;
}
}
