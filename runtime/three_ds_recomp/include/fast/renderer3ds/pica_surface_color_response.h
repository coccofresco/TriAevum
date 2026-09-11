#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace Fast::Renderer3ds {
// Explicit capability for texture0 * primary RGB followed by RGB pass-through
// stages. Other TEV expressions remain unavailable, never guessed from shaders.
struct PicaSurfaceColorResponse {
    uint32_t PackedScales = 0;
    bool Available = false;
    bool operator==(const PicaSurfaceColorResponse&) const = default;
};
inline PicaSurfaceColorResponse DecodePicaSurfaceColorResponse(std::span<const uint32_t> registers) {
    constexpr std::array<uint16_t, 6> bases{0xc0, 0xc8, 0xd0, 0xd8, 0xf0, 0xf8};
    if (registers.size() <= bases.back() + 4U) return {};
    PicaSurfaceColorResponse result;
    for (size_t i = 0; i < bases.size(); ++i) {
        const auto base = bases[i];
        const auto sources = registers[base], operands = registers[base + 1];
        const auto operation = registers[base + 2] & 15U;
        const auto scale = registers[base + 4] & 3U;
        if (scale > 2U) return {};
        if (i == 0) {
            const auto a = sources & 15U, b = (sources >> 4U) & 15U;
            if (operation != 1 || (operands & 255U) != 0 ||
                !((a == 0 && b == 3) || (a == 3 && b == 0))) return {};
        } else if (operation != 0 || (sources & 15U) != 15 || (operands & 15U) != 0) return {};
        result.PackedScales |= scale << (2U * i);
    }
    result.Available = true;
    return result;
}
} // namespace Fast::Renderer3ds
