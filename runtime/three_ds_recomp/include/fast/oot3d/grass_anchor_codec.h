#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace Fast::Oot3d {

// Procedural directions only: position, size and animation phase remain float32.
// Matches GLSL pack/unpackSnorm2x16 and octahedral normal reconstruction.
inline uint32_t PackGrassDirection(const std::array<float, 2>& v) noexcept {
    const auto component = [](float x) {
        return static_cast<uint16_t>(static_cast<int16_t>(std::lround(std::clamp(x, -1.0F, 1.0F) * 32767.0F)));
    };
    return component(v[0]) | (static_cast<uint32_t>(component(v[1])) << 16U);
}

inline std::array<float, 2> UnpackGrassDirection(uint32_t packed) noexcept {
    const auto component = [](uint32_t x) {
        const int32_t signedValue = (x & 0x8000U) != 0U ? static_cast<int32_t>(x) - 65536 : static_cast<int32_t>(x);
        return std::max(-1.0F, static_cast<float>(signedValue) / 32767.0F);
    };
    return { component(packed & 0xffffU), component(packed >> 16U) };
}

inline uint32_t PackGrassNormal(std::array<float, 3> n) noexcept {
    const float sum = std::abs(n[0]) + std::abs(n[1]) + std::abs(n[2]);
    if (!(sum > 1.0e-6F) || !std::isfinite(sum))
        n = { 0.0F, 1.0F, 0.0F };
    else
        for (auto& c : n)
            c /= sum;
    std::array<float, 2> oct{ n[0], n[1] };
    if (n[2] < 0.0F)
        oct = { (1.0F - std::abs(n[1])) * (n[0] < 0.0F ? -1.0F : 1.0F),
                (1.0F - std::abs(n[0])) * (n[1] < 0.0F ? -1.0F : 1.0F) };
    return PackGrassDirection(oct);
}

inline std::array<float, 4> UnpackGrassNormal(uint32_t packed) noexcept {
    const auto oct = UnpackGrassDirection(packed);
    std::array<float, 4> n{ oct[0], oct[1], 1.0F - std::abs(oct[0]) - std::abs(oct[1]), 0.0F };
    const float fold = std::max(-n[2], 0.0F);
    n[0] += n[0] >= 0.0F ? -fold : fold;
    n[1] += n[1] >= 0.0F ? -fold : fold;
    const float inverse = 1.0F / std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    for (auto& c : n)
        c *= inverse;
    return n;
}

// Interaction bends of the drawn roots: GLSL packHalf2x16, rounding to
// nearest even.
inline uint32_t GrassHalfBits(float value) noexcept {
    const uint32_t bits = std::bit_cast<uint32_t>(value);
    const uint32_t sign = (bits >> 16U) & 0x8000U;
    const uint32_t magnitude = bits & 0x7fffffffU;
    if (magnitude > 0x7f800000U) return sign | 0x7e00U;
    if (magnitude >= 0x477ff000U) return sign | 0x7c00U;
    if (magnitude < 0x38800000U)
        return sign | static_cast<uint32_t>(std::nearbyint(std::bit_cast<float>(magnitude) * 16777216.0F));
    return sign | ((magnitude + 0xfffU + ((magnitude >> 13U) & 1U) - 0x38000000U) >> 13U);
}

inline float GrassHalfValue(uint32_t half) noexcept {
    const uint32_t exponent = (half >> 10U) & 0x1fU, mantissa = half & 0x3ffU;
    const float magnitude = exponent == 0U ? static_cast<float>(mantissa) / 16777216.0F
        : exponent == 0x1fU ? (mantissa != 0U ? std::nanf("") : INFINITY)
        : std::ldexp(static_cast<float>(mantissa | 0x400U), static_cast<int>(exponent) - 25);
    return (half & 0x8000U) != 0U ? -magnitude : magnitude;
}

inline uint32_t PackGrassBend(const std::array<float, 2>& bend) noexcept {
    return GrassHalfBits(bend[0]) | (GrassHalfBits(bend[1]) << 16U);
}

inline std::array<float, 2> UnpackGrassBend(uint32_t packed) noexcept {
    return { GrassHalfValue(packed & 0xffffU), GrassHalfValue(packed >> 16U) };
}

} // namespace Fast::Oot3d
