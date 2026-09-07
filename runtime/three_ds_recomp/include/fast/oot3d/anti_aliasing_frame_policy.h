#pragma once

#include "fast/oot3d/graphics_settings.h"

#include <cstdint>

namespace Fast::Oot3d {

struct AntiAliasingFrameCapabilities {
    bool MsaaDepthResolve = false;
    bool Msaa2x = false;
    bool Msaa4x = false;
    bool Msaa8x = false;
};

struct AntiAliasingFramePolicy {
    bool TemporalJitter = false;
    bool TemporalMotion = false;
    uint32_t TemporalJitterPhases = 8U;
    uint32_t SpatialAaMode = 0U;
    uint8_t MsaaSamples = 1U;
};

[[nodiscard]] inline AntiAliasingFramePolicy
ResolveAntiAliasingFramePolicy(
    const GraphicsSettings& settings,
    const AntiAliasingFrameCapabilities& capabilities,
    bool forceTaa = false, bool forceMotion = false) {
    AntiAliasingFramePolicy policy;
    const bool temporalUpscaler =
        settings.AntiAliasing == AntiAliasingMode::Upscaler &&
        IsTemporalUpscaler(settings.Upscaler);
    policy.TemporalJitter =
        settings.AntiAliasing == AntiAliasingMode::Taa ||
        temporalUpscaler || forceTaa;
    policy.TemporalMotion = policy.TemporalJitter || forceMotion;
    policy.TemporalJitterPhases = temporalUpscaler
        ? ResolveUpscalerContract(
              settings.Upscaler, settings.UpscalerMode, 1U, 1U)
              .MinimumJitterPhases
        : 8U;
    policy.SpatialAaMode =
        settings.AntiAliasing == AntiAliasingMode::Fxaa ? 1U : 0U;

    if (settings.AntiAliasing != AntiAliasingMode::Msaa ||
        !capabilities.MsaaDepthResolve) {
        return policy;
    }
    const uint8_t requested = settings.MsaaSamples >= 8U ? 8U
        : settings.MsaaSamples >= 4U ? 4U
                                    : 2U;
    const bool supported = requested == 8U ? capabilities.Msaa8x
        : requested == 4U ? capabilities.Msaa4x
                          : capabilities.Msaa2x;
    policy.MsaaSamples = supported ? requested : 1U;
    return policy;
}

} // namespace Fast::Oot3d
