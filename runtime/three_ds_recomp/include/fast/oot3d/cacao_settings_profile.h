#pragma once

#include "fast/oot3d/cacao_normal_input.h"
#include "fast/oot3d/graphics_settings.h"

#include <cstdint>

namespace Fast::Oot3d {

struct CacaoSettings {
    CacaoQuality Quality = CacaoQuality::Low;
    float Radius = 1.0F;
    float Strength = 1.0F;
    float ShadowPower = 1.5F;
    float ShadowClamp = 0.98F;
    float HorizonAngleThreshold = 0.06F;
    float FadeOutFrom = 50.0F;
    float FadeOutTo = 300.0F;
    uint32_t BlurPassCount = 2;
    float Sharpness = 0.98F;
    float DetailStrength = 0.5F;
};

[[nodiscard]] inline CacaoSettings ResolveCacaoSettings(
    const EffectsSettings& effects) {
    return {
        effects.AoQuality == 0U ? CacaoQuality::Low
                                : CacaoQuality::Medium,
        effects.AoRadius,
        effects.AoStrength,
        effects.AoShadowPower,
        effects.AoShadowClamp,
        effects.AoHorizonAngleThreshold,
        effects.AoFadeOutFrom,
        effects.AoFadeOutTo,
        effects.AoBlurPassCount,
        effects.AoSharpness,
        effects.AoDetailStrength};
}

} // namespace Fast::Oot3d
