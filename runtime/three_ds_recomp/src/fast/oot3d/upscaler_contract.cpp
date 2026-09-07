#include "fast/oot3d/upscaler_contract.h"

#include <algorithm>
#include <cmath>

namespace Fast::Oot3d {

float UpscalerScalingFactor(UpscalerQuality quality) {
    switch (quality) {
        case UpscalerQuality::UltraQuality: return 1.3F;
        case UpscalerQuality::Quality: return 1.5F;
        case UpscalerQuality::Balanced: return 1.7F;
        case UpscalerQuality::Performance: return 2.0F;
        case UpscalerQuality::UltraPerformance: return 3.0F;
        case UpscalerQuality::Native: return 1.0F;
    }
    return 1.0F;
}

bool IsTemporalUpscaler(UpscalerProvider provider) {
    return provider != UpscalerProvider::Nis;
}

UpscalerContract ResolveUpscalerContract(
    UpscalerProvider provider, UpscalerQuality quality,
    uint32_t outputWidth, uint32_t outputHeight) {
    UpscalerContract result;
    result.Provider = provider;
    result.Quality = quality;
    result.Output = {std::max(1U, outputWidth), std::max(1U, outputHeight)};
    result.ScalingFactor = UpscalerScalingFactor(quality);
    result.Render = {
        std::max(1U, static_cast<uint32_t>(std::lround(
            static_cast<float>(result.Output.Width) / result.ScalingFactor))),
        std::max(1U, static_cast<uint32_t>(std::lround(
            static_cast<float>(result.Output.Height) / result.ScalingFactor)))};
    result.MinimumJitterPhases = static_cast<uint32_t>(std::ceil(
        8.0F * result.ScalingFactor * result.ScalingFactor));
    result.Temporal = IsTemporalUpscaler(provider);
    return result;
}

UpscalerContract ResolveUpscalerContractFromRender(
    UpscalerProvider provider, UpscalerQuality quality,
    uint32_t renderWidth, uint32_t renderHeight) {
    UpscalerContract result;
    result.Provider = provider;
    result.Quality = quality;
    result.Render = {std::max(1U, renderWidth), std::max(1U, renderHeight)};
    result.ScalingFactor = UpscalerScalingFactor(quality);
    result.Output = {
        std::max(1U, static_cast<uint32_t>(std::lround(
            static_cast<float>(result.Render.Width) * result.ScalingFactor))),
        std::max(1U, static_cast<uint32_t>(std::lround(
            static_cast<float>(result.Render.Height) * result.ScalingFactor)))};
    result.MinimumJitterPhases = static_cast<uint32_t>(std::ceil(
        8.0F * result.ScalingFactor * result.ScalingFactor));
    result.Temporal = IsTemporalUpscaler(provider);
    return result;
}

} // namespace Fast::Oot3d
