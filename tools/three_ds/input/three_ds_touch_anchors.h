#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ThreeDsRecomp::Input {

// The on-screen controls a title draws itself inside a touch overlay's
// buttons: the overlay owns where they are and what a finger on them presses,
// the title draws its own HUD art there and hides that part of its HUD.
enum class TouchAnchor : std::uint8_t { A, B, X, Y, Zl, Zr, Dpad, Count };
inline constexpr std::size_t kTouchAnchorCount = static_cast<std::size_t>(TouchAnchor::Count);

// A control's rectangle as fractions of the window (0..1, top-left origin).
struct TouchAnchorRect {
    bool Present = false;
    float Left = 0.0F;
    float Top = 0.0F;
    float Right = 0.0F;
    float Bottom = 0.0F;

    [[nodiscard]] float CenterX() const noexcept { return (Left + Right) * 0.5F; }
    [[nodiscard]] float CenterY() const noexcept { return (Top + Bottom) * 0.5F; }
    bool operator==(const TouchAnchorRect&) const = default;
};
using TouchAnchorSet = std::array<TouchAnchorRect, kTouchAnchorCount>;

// Whether a touch overlay reports any control.
[[nodiscard]] inline bool AnyTouchAnchored(const TouchAnchorSet& anchors) noexcept {
    return std::any_of(anchors.begin(), anchors.end(), [](const TouchAnchorRect& rect) { return rect.Present; });
}

// Whether any face or shoulder button (not the D-pad) is anchored.
[[nodiscard]] inline bool AnyTouchButtonAnchored(const TouchAnchorSet& anchors) noexcept {
    for (std::size_t index = 0; index < static_cast<std::size_t>(TouchAnchor::Dpad); ++index)
        if (anchors[index].Present) return true;
    return false;
}

// The platform UI thread writes the overlay's layout; the frame reads a copy.
// The title says whether it draws the anchored controls, so the overlay draws
// its own art when the title does not.
class TouchAnchors {
  public:
    void Set(TouchAnchor anchor, float left, float top, float right, float bottom) noexcept {
        if (anchor == TouchAnchor::Count) return;
        const auto clean = [](float value) { return std::isfinite(value) ? std::clamp(value, 0.0F, 1.0F) : 0.0F; };
        TouchAnchorRect rect{true, clean(left), clean(top), clean(right), clean(bottom)};
        if (!(rect.Right > rect.Left) || !(rect.Bottom > rect.Top)) rect = {};
        std::lock_guard lock(mMutex);
        mAnchors[static_cast<std::size_t>(anchor)] = rect;
    }
    void Clear(TouchAnchor anchor) noexcept {
        if (anchor == TouchAnchor::Count) return;
        std::lock_guard lock(mMutex);
        mAnchors[static_cast<std::size_t>(anchor)] = {};
    }
    [[nodiscard]] TouchAnchorSet Snapshot() const {
        std::lock_guard lock(mMutex);
        return mAnchors;
    }

    void SetTitleDrawsControls(bool draws) noexcept { mTitleDraws.store(draws, std::memory_order_release); }
    [[nodiscard]] bool TitleDrawsControls() const noexcept { return mTitleDraws.load(std::memory_order_acquire); }

  private:
    mutable std::mutex mMutex;
    TouchAnchorSet mAnchors{};
    std::atomic<bool> mTitleDraws{false};
};

// Developer layouts without an overlay: "A=l,t,r,b;DPAD=l,t,r,b" in window
// fractions, anchors A B X Y ZL ZR DPAD. False (and nothing set) when malformed.
inline bool ApplyTouchAnchorList(std::string_view text, TouchAnchors& anchors) {
    constexpr std::array<std::string_view, kTouchAnchorCount> kNames{"A", "B", "X", "Y", "ZL", "ZR", "DPAD"};
    std::vector<std::pair<TouchAnchor, std::array<float, 4>>> parsed;
    while (!text.empty()) {
        const auto end = text.find(';');
        const auto entry = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        const auto equals = entry.find('=');
        if (equals == std::string_view::npos) return false;
        const auto name = std::find(kNames.begin(), kNames.end(), entry.substr(0, equals));
        if (name == kNames.end()) return false;
        std::string values(entry.substr(equals + 1));
        std::array<float, 4> rect{};
        const char* cursor = values.c_str();
        for (std::size_t index = 0; index < rect.size(); ++index) {
            char* next = nullptr;
            rect[index] = std::strtof(cursor, &next);
            if (next == cursor || (index < 3U ? *next != ',' : *next != '\0')) return false;
            cursor = next + 1;
        }
        parsed.emplace_back(static_cast<TouchAnchor>(name - kNames.begin()), rect);
    }
    for (const auto& [anchor, rect] : parsed) anchors.Set(anchor, rect[0], rect[1], rect[2], rect[3]);
    return true;
}

// The process's touch overlay layout. Empty on hosts without one.
inline TouchAnchors& OverlayTouchAnchors() noexcept {
    static TouchAnchors anchors;
    return anchors;
}

} // namespace ThreeDsRecomp::Input
