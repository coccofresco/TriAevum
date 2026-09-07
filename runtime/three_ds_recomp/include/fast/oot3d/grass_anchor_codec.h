#pragma once

#include <algorithm>
#include <array>
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

} // namespace Fast::Oot3d
