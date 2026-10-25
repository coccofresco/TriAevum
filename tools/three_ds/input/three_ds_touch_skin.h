#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ThreeDsRecomp::Input {

// Vector shapes for on-screen controls a title draws itself: white coverage
// rasterized from signed distances with an anti-aliased edge, made at run
// time (no title data). A title tints them through its UI renderer.
enum class TouchSkinShape : std::uint8_t {
    Disc,
    Ring,
    // 2:1, drawn in three slices (caps and a stretched middle) for any width.
    Pill,
    PillRing,
    Cross,
    CrossRing,
    // One arm of the cross, pointing up (rotated for the others).
    CrossArm,
    // A small triangle pointing up.
    Arrow,
    GlyphA,
    GlyphB,
    GlyphX,
    GlyphY,
    GlyphL,
    GlyphR,
    GlyphZl,
    GlyphZr,
    GlyphStart,
    GlyphSelect,
    GlyphHome,
    Count,
};

inline constexpr std::string_view kTouchSkinSemanticPrefix = "triaevum/touch_skin/";

[[nodiscard]] std::string TouchSkinSemanticName(TouchSkinShape shape);
[[nodiscard]] std::optional<TouchSkinShape> TouchSkinShapeFromSemanticName(std::string_view name) noexcept;

struct TouchSkinPixels {
    std::uint16_t Width = 0;
    std::uint16_t Height = 0;
    std::vector<std::uint8_t> Rgba8;
};

// The shape's texture: 256 x 256, or 256 x 128 for the pills.
[[nodiscard]] TouchSkinPixels RasterizeTouchSkinShape(TouchSkinShape shape);

// The cross's arm half-width as a fraction of its size, for a title that
// places content on the arms.
inline constexpr float kTouchSkinCrossArmHalfWidth = 0.17F;

} // namespace ThreeDsRecomp::Input
