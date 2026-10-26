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

// Where the stored output request comes from (Graphics.Output.Provenance):
// the product's fresh default, which nobody chose, or the user's own choice
// of a size. A default request opens the window at the default fit of the
// screen (DefaultWindowedExtent); a user request is honoured as stored.
enum class WindowExtentProvenance : uint8_t { Default, User };

// The extent a new installation stores, before any choice.
inline constexpr WindowExtent kProductDefaultWindowExtent{1280U, 720U};

// A configuration written before the provenance existed: the product's
// default windowed extent is taken for the fresh default, any other extent
// (or a fullscreen mode) for a choice the user made.
constexpr WindowExtentProvenance InferWindowExtentProvenance(bool windowed, WindowExtent stored) noexcept {
    return windowed && stored == kProductDefaultWindowExtent ? WindowExtentProvenance::Default
                                                             : WindowExtentProvenance::User;
}

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

// The window of a default request: the largest 16:9 extent inside 90 % of
// the work area left by the decorations, so the whole frame and some desktop
// stay visible. Even sizes. An unknown work area keeps the product default.
inline constexpr uint32_t kDefaultWindowWorkAreaPercent = 90U;

constexpr WindowExtent DefaultWindowedExtent(const WindowWorkArea& area) noexcept {
    const int64_t availableWidth =
        (int64_t{area.Width} - std::max(0, area.DecorationWidth)) * kDefaultWindowWorkAreaPercent / 100;
    const int64_t availableHeight =
        (int64_t{area.Height} - std::max(0, area.DecorationHeight)) * kDefaultWindowWorkAreaPercent / 100;
    if (availableWidth <= 0 || availableHeight <= 0) return kProductDefaultWindowExtent;
    int64_t width = availableWidth;
    int64_t height = width * 9 / 16;
    if (height > availableHeight) {
        height = availableHeight;
        width = height * 16 / 9;
    }
    width -= width % 2;
    height -= height % 2;
    return {std::max(static_cast<uint32_t>(width), kMinimumWindowExtent.Width),
            std::max(static_cast<uint32_t>(height), kMinimumWindowExtent.Height)};
}

} // namespace Fast
