#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace Fast::Renderer3ds {
// A separate optional factorization for native fragment-lit grass: texture0 *
// fragment-primary, optionally multiplied once by vertex tint/AO. No early RGB
// scaling before tint: that would introduce texture-dependent saturation.
struct PicaFragmentGrassColorResponse {
    uint32_t PackedScales = 0;
    uint8_t TintStage = 6;
    bool Available = false;
    bool Valid() const noexcept {
        if (!Available || TintStage == 0 || TintStage > 6 || (PackedScales & ~0xFFFU) != 0) return false;
        for (uint32_t i = 0; i < 6; ++i) if (((PackedScales >> (2 * i)) & 3U) > 2) return false;
        return TintStage == 6 || (PackedScales & ((1U << (2 * TintStage)) - 1U)) == 0;
    }
    uint32_t Key() const noexcept { return Valid() ? PackedScales | (uint32_t(TintStage) << 12) | 0x10000U : 0U; }
    bool operator==(const PicaFragmentGrassColorResponse&) const = default;
};
inline PicaFragmentGrassColorResponse DecodePicaFragmentGrassColorResponse(std::span<const uint32_t> registers) {
    constexpr std::array<uint16_t, 6> bases{0xc0, 0xc8, 0xd0, 0xd8, 0xf0, 0xf8};
    if (registers.size() <= bases.back() + 4U) return {};
    PicaFragmentGrassColorResponse result;
    for (size_t i = 0; i < bases.size(); ++i) {
        const auto b = bases[i];
        const auto sources = registers[b], operands = registers[b + 1];
        const auto op = registers[b + 2] & 15U, scale = registers[b + 4] & 3U;
        const auto a = sources & 15U, c = (sources >> 4U) & 15U;
        if (scale > 2) return {};
        if (i == 0) {
            if (op != 1 || (operands & 255U) != 0 || !((a == 1 && c == 3) || (a == 3 && c == 1))) return {};
        } else if (op == 1 && result.TintStage == 6 && (operands & 255U) == 0 &&
                   ((a == 15 && c == 0) || (a == 0 && c == 15))) {
            if (result.PackedScales != 0) return {};
            result.TintStage = uint8_t(i);
        } else if (op != 0 || a != 15 || (operands & 15U) != 0) return {};
        result.PackedScales |= scale << (2U * i);
    }
    result.Available = true;
    return result;
}
// The legacy vertex capability and its packed scales retain exactly their
// previous meaning. Fragment consumers explicitly opt into the new member.
struct PicaSurfaceColorResponse {
    uint32_t PackedScales = 0;
    bool Available = false;
    PicaFragmentGrassColorResponse Fragment;
    bool operator==(const PicaSurfaceColorResponse&) const = default;
};
inline PicaSurfaceColorResponse DecodePicaVertexSurfaceColorResponse(std::span<const uint32_t> registers) {
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
inline PicaSurfaceColorResponse DecodePicaSurfaceColorResponse(std::span<const uint32_t> registers) {
    auto result = DecodePicaVertexSurfaceColorResponse(registers);
    result.Fragment = DecodePicaFragmentGrassColorResponse(registers);
    return result;
}
} // namespace Fast::Renderer3ds
