#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "fast/renderer3ds/pica_lighting_program.h"

namespace Fast::Extension {

inline constexpr size_t kPicaLightingLutTableCount = 24U;
inline constexpr size_t kPicaLightingLutEntryCount = 256U;

enum class PicaLightingLutTable : uint8_t {
    Distribution0 = 0,
    Distribution1 = 1,
    Fresnel = 3,
    ReflectBlue = 4,
    ReflectGreen = 5,
    ReflectRed = 6,
    SpotlightAttenuation = 8,
    DistanceAttenuation = 16,
};

enum class PicaLightingLutInput : uint8_t {
    NormalHalf = 0,
    ViewHalf = 1,
    NormalView = 2,
    LightNormal = 3,
    NegatedLightSpot = 4,
    CosinePhi = 5,
};

enum class PicaLightingBumpMode : uint8_t {
    None = 0,
    NormalMap = 1,
    TangentMap = 2,
};

struct PicaLightingColor {
    std::array<float, 3> Rgb{};
};

struct PicaLightingLutSampler {
    bool AbsoluteInput = true;
    PicaLightingLutInput Input = PicaLightingLutInput::NormalHalf;
    float Scale = 1.0F;
};

struct PicaFragmentLight {
    PicaLightingColor Specular0;
    PicaLightingColor Specular1;
    PicaLightingColor Diffuse;
    PicaLightingColor Ambient;
    std::array<float, 3> Position{};
    std::array<float, 3> SpotDirection{};
    float DistanceAttenuationBias = 0.0F;
    float DistanceAttenuationScale = 0.0F;
    bool Directional = false;
    bool TwoSidedDiffuse = false;
    bool GeometricFactor0 = false;
    bool GeometricFactor1 = false;
    bool ShadowEnabled = false;
    bool SpotAttenuationEnabled = false;
    bool DistanceAttenuationEnabled = false;
};

struct PicaFragmentLightingState {
    bool Enabled = false;
    uint8_t ActiveLightCount = 0;
    std::array<uint8_t, 8> LightPermutation{};
    std::array<PicaFragmentLight, 8> Lights{};
    PicaLightingColor GlobalAmbient;
    std::array<PicaLightingLutSampler, 7> LutSamplers{};
    uint8_t EnvironmentConfiguration = 0;
    uint8_t FresnelSelector = 0;
    uint8_t BumpTextureUnit = 0;
    uint8_t ShadowTextureUnit = 0;
    PicaLightingBumpMode BumpMode = PicaLightingBumpMode::None;
    bool ClampHighlights = false;
    bool RecalculateBumpVectors = true;
    bool ShadowFactorEnabled = false;
    bool ShadowPrimary = false;
    bool ShadowSecondary = false;
    bool ShadowAlpha = false;
    bool InvertShadow = false;
    uint32_t Config0 = 0;
    uint32_t Config1 = 0;
};

struct PicaLightingLutWriteCursor {
    uint8_t Index = 0;
    uint8_t Table = 0;
};

struct PicaLightingLutEntry {
    float Value = 0.0F;
    float Delta = 0.0F;
};

[[nodiscard]] PicaFragmentLightingState DecodePicaFragmentLighting(
    std::span<const uint32_t> registers);
[[nodiscard]] PicaLightingLutWriteCursor DecodePicaLightingLutWriteCursor(
    uint32_t value);
[[nodiscard]] PicaLightingLutEntry DecodePicaLightingLutEntry(uint32_t value);
[[nodiscard]] bool IsPicaLightingLutTableValid(uint8_t table);
[[nodiscard]] bool IsPicaLightingLutSamplerSupported(
    uint8_t environmentConfiguration, uint8_t table);
[[nodiscard]] uint64_t ComputePicaFragmentLightingStructuralKey(
    std::span<const uint32_t> registers);

// Packs the decoded native lighting state into the shared std140 program the
// renderer uploads. Title-side by construction: it reads native register
// decoding, so it cannot live in the title-neutral renderer boundary.
[[nodiscard]] inline Renderer3ds::PicaLightingProgram BuildPicaLightingProgram(
    const PicaFragmentLightingState& state) {
    Renderer3ds::PicaLightingProgram p;
    p.Control = {state.ActiveLightCount, state.EnvironmentConfiguration, state.FresnelSelector,
        uint32_t(state.ClampHighlights) | (uint32_t(state.ShadowPrimary) << 1) |
        (uint32_t(state.ShadowSecondary) << 2) | (uint32_t(state.ShadowAlpha) << 3) |
        (uint32_t(state.BumpMode) << 4) | (uint32_t(state.RecalculateBumpVectors) << 6) |
        (uint32_t(state.ShadowFactorEnabled) << 7) | (uint32_t(state.InvertShadow) << 8) |
        (uint32_t(state.BumpTextureUnit) << 9) | (uint32_t(state.ShadowTextureUnit) << 11)};
    for (size_t slot = 0; slot < p.Lights.size(); ++slot) {
        const auto index = state.LightPermutation[slot];
        const auto& light = state.Lights[index];
        const bool spot = light.SpotAttenuationEnabled &&
            IsPicaLightingLutSamplerSupported(state.EnvironmentConfiguration, uint8_t(8 + index));
        p.Lights[slot] = {index, uint32_t(light.Directional) | (uint32_t(light.TwoSidedDiffuse) << 1) |
            (uint32_t(light.GeometricFactor0) << 2) | (uint32_t(light.GeometricFactor1) << 3) |
            (uint32_t(light.ShadowEnabled) << 4) | (uint32_t(spot) << 5) |
            (uint32_t(light.DistanceAttenuationEnabled) << 6), 0, 0};
    }
    constexpr uint8_t tables[]{0, 1, 8, 3, 4, 5, 6};
    constexpr uint8_t disableBits[]{16, 17, 0, 19, 22, 21, 20};
    for (size_t i = 0; i < p.Luts.size(); ++i) {
        const auto& lut = state.LutSamplers[i];
        const bool enabled = i == 2 || ((state.Config1 & (1U << disableBits[i])) == 0 &&
            IsPicaLightingLutSamplerSupported(state.EnvironmentConfiguration, tables[i]));
        p.Luts[i] = {float(lut.Input), float(lut.AbsoluteInput), lut.Scale, float(enabled)};
    }
    return p;
}

} // namespace Fast::Extension
