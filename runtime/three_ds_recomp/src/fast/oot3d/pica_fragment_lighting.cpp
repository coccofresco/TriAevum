#include "fast/oot3d/pica_fragment_lighting.h"

#include <algorithm>
#include <cmath>

namespace Fast::Oot3d {
namespace {

constexpr size_t kRegisterCount = 0x300U;
constexpr size_t kLightBase = 0x140U;
constexpr size_t kLightStride = 0x10U;

uint32_t Register(std::span<const uint32_t> registers, size_t index) {
    return index < registers.size() ? registers[index] : 0U;
}

float DecodePicaFloat(uint32_t raw, uint32_t exponentBits,
                      uint32_t mantissaBits) {
    const uint32_t exponentMask = (1U << exponentBits) - 1U;
    const uint32_t mantissaMask = (1U << mantissaBits) - 1U;
    const uint32_t exponent = (raw >> mantissaBits) & exponentMask;
    const uint32_t mantissa = raw & mantissaMask;
    const bool negative = ((raw >> (exponentBits + mantissaBits)) & 1U) != 0U;
    const int bias = (1 << (exponentBits - 1U)) - 1;
    float value = 0.0F;
    if (exponent == exponentMask) {
        value = mantissa == 0U ? INFINITY : NAN;
    } else if (exponent == 0U) {
        value = std::ldexp(static_cast<float>(mantissa),
                           1 - bias - static_cast<int>(mantissaBits));
    } else {
        value = std::ldexp(1.0F + static_cast<float>(mantissa) /
                                      static_cast<float>(1U << mantissaBits),
                           static_cast<int>(exponent) - bias);
    }
    return negative ? -value : value;
}

float DecodeFloat16(uint32_t raw) {
    return DecodePicaFloat(raw & 0xFFFFU, 5U, 10U);
}

float DecodeFloat20(uint32_t raw) {
    return DecodePicaFloat(raw & 0xFFFFFU, 7U, 12U);
}

float DecodeSignedFixed13(uint32_t raw) {
    int32_t value = static_cast<int32_t>(raw & 0x1FFFU);
    if ((value & 0x1000) != 0) {
        value -= 0x2000;
    }
    return static_cast<float>(value) / 2047.0F;
}

PicaLightingColor DecodeLightingColor(uint32_t raw) {
    return { {
        static_cast<float>((raw >> 20U) & 0xFFU) / 255.0F,
        static_cast<float>((raw >> 10U) & 0xFFU) / 255.0F,
        static_cast<float>(raw & 0xFFU) / 255.0F,
    } };
}

float DecodeLutScale(uint8_t value) {
    switch (value & 7U) {
    case 0: return 1.0F;
    case 1: return 2.0F;
    case 2: return 4.0F;
    case 3: return 8.0F;
    case 6: return 0.25F;
    case 7: return 0.5F;
    default: return 1.0F;
    }
}

uint64_t HashWord(uint64_t hash, uint32_t word) {
    constexpr uint64_t kFnvPrime = 1099511628211ULL;
    for (size_t byte = 0; byte < sizeof(word); ++byte) {
        hash ^= static_cast<uint8_t>(word >> (byte * 8U));
        hash *= kFnvPrime;
    }
    return hash;
}

} // namespace

PicaFragmentLightingState DecodePicaFragmentLighting(
    std::span<const uint32_t> registers) {
    PicaFragmentLightingState state;
    if (registers.size() < kRegisterCount) {
        return state;
    }

    state.Enabled = (Register(registers, 0x08FU) & 1U) != 0U &&
                    (Register(registers, 0x1C6U) & 1U) == 0U;
    state.ActiveLightCount = state.Enabled
                                 ? static_cast<uint8_t>(
                                       (Register(registers, 0x1C2U) & 7U) + 1U)
                                 : 0U;
    state.Config0 = Register(registers, 0x1C3U);
    state.Config1 = Register(registers, 0x1C4U);
    state.GlobalAmbient = DecodeLightingColor(Register(registers, 0x1C0U));
    state.ShadowFactorEnabled = (state.Config0 & 1U) != 0U;
    state.FresnelSelector = static_cast<uint8_t>((state.Config0 >> 2U) & 3U);
    state.EnvironmentConfiguration =
        static_cast<uint8_t>((state.Config0 >> 4U) & 0xFU);
    state.ShadowPrimary = ((state.Config0 >> 16U) & 1U) != 0U;
    state.ShadowSecondary = ((state.Config0 >> 17U) & 1U) != 0U;
    state.InvertShadow = ((state.Config0 >> 18U) & 1U) != 0U;
    state.ShadowAlpha = ((state.Config0 >> 19U) & 1U) != 0U;
    state.BumpTextureUnit = static_cast<uint8_t>((state.Config0 >> 22U) & 3U);
    state.ShadowTextureUnit = static_cast<uint8_t>((state.Config0 >> 24U) & 3U);
    state.ClampHighlights = ((state.Config0 >> 27U) & 1U) != 0U;
    state.BumpMode = static_cast<PicaLightingBumpMode>(
        (state.Config0 >> 28U) & 3U);
    state.RecalculateBumpVectors = ((state.Config0 >> 30U) & 1U) == 0U;

    const uint32_t permutation = Register(registers, 0x1D9U);
    for (size_t slot = 0; slot < state.LightPermutation.size(); ++slot) {
        state.LightPermutation[slot] =
            static_cast<uint8_t>((permutation >> (slot * 4U)) & 7U);
    }

    for (size_t lightIndex = 0; lightIndex < state.Lights.size(); ++lightIndex) {
        const size_t base = kLightBase + lightIndex * kLightStride;
        auto& light = state.Lights[lightIndex];
        light.Specular0 = DecodeLightingColor(Register(registers, base));
        light.Specular1 = DecodeLightingColor(Register(registers, base + 1U));
        light.Diffuse = DecodeLightingColor(Register(registers, base + 2U));
        light.Ambient = DecodeLightingColor(Register(registers, base + 3U));
        const uint32_t xy = Register(registers, base + 4U);
        light.Position = { DecodeFloat16(xy), DecodeFloat16(xy >> 16U),
                           DecodeFloat16(Register(registers, base + 5U)) };
        const uint32_t spotXy = Register(registers, base + 6U);
        light.SpotDirection = {
            DecodeSignedFixed13(spotXy),
            DecodeSignedFixed13(spotXy >> 16U),
            DecodeSignedFixed13(Register(registers, base + 7U)),
        };
        const uint32_t config = Register(registers, base + 9U);
        light.Directional = (config & 1U) != 0U;
        light.TwoSidedDiffuse = (config & 2U) != 0U;
        light.GeometricFactor0 = (config & 4U) != 0U;
        light.GeometricFactor1 = (config & 8U) != 0U;
        light.DistanceAttenuationBias =
            DecodeFloat20(Register(registers, base + 10U));
        light.DistanceAttenuationScale =
            DecodeFloat20(Register(registers, base + 11U));
        light.ShadowEnabled = ((state.Config1 >> lightIndex) & 1U) == 0U;
        light.SpotAttenuationEnabled =
            ((state.Config1 >> (8U + lightIndex)) & 1U) == 0U;
        light.DistanceAttenuationEnabled =
            ((state.Config1 >> (24U + lightIndex)) & 1U) == 0U;
    }

    constexpr std::array<uint8_t, 7> shifts{ 0, 4, 8, 12, 16, 20, 24 };
    const uint32_t absolute = Register(registers, 0x1D0U);
    const uint32_t input = Register(registers, 0x1D1U);
    const uint32_t scale = Register(registers, 0x1D2U);
    for (size_t index = 0; index < state.LutSamplers.size(); ++index) {
        const uint8_t shift = shifts[index];
        auto& sampler = state.LutSamplers[index];
        sampler.AbsoluteInput = ((absolute >> (shift + 1U)) & 1U) == 0U;
        sampler.Input = static_cast<PicaLightingLutInput>(
            (input >> shift) & 7U);
        sampler.Scale = DecodeLutScale(
            static_cast<uint8_t>((scale >> shift) & 7U));
    }
    return state;
}

PicaLightingLutWriteCursor DecodePicaLightingLutWriteCursor(uint32_t value) {
    return { static_cast<uint8_t>(value & 0xFFU),
             static_cast<uint8_t>((value >> 8U) & 0x1FU) };
}

PicaLightingLutEntry DecodePicaLightingLutEntry(uint32_t value) {
    const int32_t magnitude =
        static_cast<int32_t>((value >> 12U) & 0x7FFU);
    const int32_t delta = (value & (1U << 23U)) != 0U
                              ? -magnitude
                              : magnitude;
    return { static_cast<float>(value & 0xFFFU) / 4095.0F,
             static_cast<float>(delta) / 2047.0F };
}

bool IsPicaLightingLutTableValid(uint8_t table) {
    return table == 0U || table == 1U || table == 3U ||
           (table >= 4U && table <= 6U) ||
           (table >= 8U && table <= 23U);
}

bool IsPicaLightingLutSamplerSupported(
    uint8_t environmentConfiguration, uint8_t table) {
    if (environmentConfiguration > 6U && environmentConfiguration != 8U) {
        return false;
    }
    if (table >= 16U && table <= 23U) {
        return true;
    }
    if (table >= 8U && table <= 15U) {
        return environmentConfiguration != 2U &&
               environmentConfiguration != 3U;
    }
    switch (static_cast<PicaLightingLutTable>(table)) {
    case PicaLightingLutTable::Distribution0:
        return environmentConfiguration != 1U;
    case PicaLightingLutTable::Distribution1:
        return environmentConfiguration != 0U &&
               environmentConfiguration != 1U &&
               environmentConfiguration != 5U;
    case PicaLightingLutTable::Fresnel:
        return environmentConfiguration != 0U &&
               environmentConfiguration != 2U &&
               environmentConfiguration != 4U;
    case PicaLightingLutTable::ReflectRed:
        return environmentConfiguration != 3U;
    case PicaLightingLutTable::ReflectGreen:
    case PicaLightingLutTable::ReflectBlue:
        return environmentConfiguration == 4U ||
               environmentConfiguration == 5U ||
               environmentConfiguration == 8U;
    default:
        return false;
    }
}

uint64_t ComputePicaFragmentLightingStructuralKey(
    std::span<const uint32_t> registers) {
    constexpr uint64_t kFnvOffset = 1469598103934665603ULL;
    if (registers.size() < kRegisterCount) {
        return kFnvOffset;
    }
    uint64_t key = HashWord(kFnvOffset, Register(registers, 0x08FU) & 1U);
    key = HashWord(key, Register(registers, 0x1C2U) & 7U);
    key = HashWord(key, Register(registers, 0x1C3U));
    key = HashWord(key, Register(registers, 0x1C4U));
    key = HashWord(key, Register(registers, 0x1C6U) & 1U);
    key = HashWord(key, Register(registers, 0x1D0U));
    key = HashWord(key, Register(registers, 0x1D1U));
    key = HashWord(key, Register(registers, 0x1D2U));
    key = HashWord(key, Register(registers, 0x1D9U));
    for (size_t light = 0; light < 8U; ++light) {
        key = HashWord(key,
                       Register(registers, kLightBase + light * kLightStride + 9U));
    }
    return key;
}

} // namespace Fast::Oot3d
