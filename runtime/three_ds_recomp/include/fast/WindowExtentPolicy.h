#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

namespace Fast {

// The window host owns the output extent. The user's request is the one F1
// Display stores (Graphics.Output); launch arguments only seed a configuration
// that holds none yet. Whatever wins, the window opens inside the work area of
// its display, so a small or scaled screen never hides part of it.
struct WindowExtent {
    uint32_t Width = 0;
    uint32_t Height = 0;
    friend bool operator==(const WindowExtent&, const WindowExtent&) = default;
};

// The renderer refuses smaller presentation extents.
inline constexpr WindowExtent kMinimumWindowExtent{320U, 240U};

constexpr bool IsUsableWindowExtent(WindowExtent extent) noexcept {
    return extent.Width >= kMinimumWindowExtent.Width && extent.Height >= kMinimumWindowExtent.Height;
}

constexpr WindowExtent ResolveInitialWindowExtent(std::optional<WindowExtent> persisted,
                                                  WindowExtent launchSeed) noexcept {
    return persisted.has_value() && IsUsableWindowExtent(*persisted) ? *persisted : launchSeed;
}

// The usable area of the window's display (without task bars, panels or docks)
// and the window's decorations, all in SDL window coordinates. Under
// per-monitor DPI awareness these are the units of the requested extent, and
// the decorations are those of the monitor's scale.
struct WindowWorkArea {
    int32_t Width = 0;
    int32_t Height = 0;
    int32_t DecorationWidth = 0;  // left + right borders
    int32_t DecorationHeight = 0; // title bar + bottom border
};

// The largest extent of the requested shape whose decorated window fits the
// work area. A request that fits is returned unchanged, and so is any request
// when the work area is unknown.
constexpr WindowExtent FitWindowedExtent(WindowExtent requested, const WindowWorkArea& area) noexcept {
    const int64_t availableWidth = int64_t{area.Width} - std::max(0, area.DecorationWidth);
    const int64_t availableHeight = int64_t{area.Height} - std::max(0, area.DecorationHeight);
    if (requested.Width == 0U || requested.Height == 0U || availableWidth <= 0 || availableHeight <= 0 ||
        (requested.Width <= availableWidth && requested.Height <= availableHeight)) {
        return requested;
    }
    WindowExtent fitted;
    if (availableWidth * requested.Height <= availableHeight * requested.Width) {
        fitted.Width = static_cast<uint32_t>(availableWidth);
        fitted.Height = static_cast<uint32_t>(availableWidth * requested.Height / requested.Width);
    } else {
        fitted.Width = static_cast<uint32_t>(availableHeight * requested.Width / requested.Height);
        fitted.Height = static_cast<uint32_t>(availableHeight);
    }
    return {std::max(fitted.Width, kMinimumWindowExtent.Width), std::max(fitted.Height, kMinimumWindowExtent.Height)};
}

} // namespace Fast
