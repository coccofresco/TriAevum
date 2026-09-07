#pragma once

#include <cstdint>

namespace oot3d::ui {

struct Oot3dNativeHudAtlasRect {
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
};

inline constexpr std::uint16_t kOot3dPauseTopPageAtlasWidth = 512;
inline constexpr std::uint16_t kOot3dPauseTopPageAtlasHeight = 256;

// Native PauseTouchButtonPanel_Init tables at code.bin
// 0x0050B578/0x0050B858, entries 26 and 27.
inline constexpr Oot3dNativeHudAtlasRect kOot3dRupeeAtlasRect{
    128, 96, 18, 18};
inline constexpr Oot3dNativeHudAtlasRect kOot3dSmallKeyAtlasRect{
    152, 96, 18, 18};

// Health-meter UV selection recovered from code.bin
// HealthMeterGeometry@0x0044425C. Fractions use the native five buckets;
// double defense selects the alternate row by subtracting 80 pixels.
constexpr Oot3dNativeHudAtlasRect Oot3dHeartAtlasRect(
    std::uint8_t fraction_units, bool double_defense) noexcept {
    const std::uint8_t clamped = fraction_units > 16U ? 16U : fraction_units;
    const std::uint8_t bucket =
        clamped == 0U
            ? 0U
            : static_cast<std::uint8_t>((clamped + 3U) / 4U);
    return {240U,
            static_cast<std::uint16_t>((double_defense ? 0U : 80U) +
                                       bucket * 16U),
            12U, 12U};
}

} // namespace oot3d::ui
