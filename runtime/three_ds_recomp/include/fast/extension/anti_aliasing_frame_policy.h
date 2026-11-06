#pragma once

#include "fast/extension/graphics_settings.h"

#include <cstdint>

namespace Fast::Extension {

struct AntiAliasingFrameCapabilities {
    bool MsaaDepthResolve = false;
    bool Msaa2x = false;
    bool Msaa4x = false;
    bool Msaa8x = false;
    // The selected upscaler's provider can execute. One that cannot is never
    // planned, so the frame is resolved as with anti-aliasing Off: no jitter,
    // no motion and no presentation stages are requested for it.
    bool UpscalerAvailable = true;
};

struct AntiAliasingFramePolicy {
    bool TemporalJitter = false;
    bool TemporalMotion = false;
    // Presentation stages read the display's world surface: TAA, SMAA, the
    // upscalers and forced motion vectors (FXAA and MSAA do not).
    bool PresentationWorldSurface = false;
    // Same-extent anti-aliasing (TAA, SMAA) is a scene stage: a target's
    // boundary before native UI resolves it, so the UI draws over the result.
    bool SceneAntiAliasing = false;
    // An upscaler changes the extent and runs at presentation: a target's
    // boundary before native UI keeps the scene it completed, so the upscaler
    // reconstructs that scene and the UI is laid over its output.
    bool PresentationUpscaler = false;
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
    if (settings.AntiAliasing == AntiAliasingMode::Upscaler && !capabilities.UpscalerAvailable) {
        auto withoutUpscaler = settings;
        withoutUpscaler.AntiAliasing = AntiAliasingMode::Off;
        return ResolveAntiAliasingFramePolicy(withoutUpscaler, capabilities, forceTaa, forceMotion);
    }
    const bool temporalUpscaler =
        settings.AntiAliasing == AntiAliasingMode::Upscaler &&
        IsTemporalUpscaler(settings.Upscaler);
    policy.TemporalJitter =
        settings.AntiAliasing == AntiAliasingMode::Taa ||
        temporalUpscaler || forceTaa;
    policy.TemporalMotion = policy.TemporalJitter || forceMotion;
    policy.PresentationWorldSurface = policy.TemporalMotion ||
        settings.AntiAliasing == AntiAliasingMode::Smaa1x ||
        settings.AntiAliasing == AntiAliasingMode::Upscaler;
    policy.SceneAntiAliasing =
        settings.AntiAliasing == AntiAliasingMode::Taa || forceTaa ||
        settings.AntiAliasing == AntiAliasingMode::Smaa1x;
    policy.PresentationUpscaler =
        settings.AntiAliasing == AntiAliasingMode::Upscaler;
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

} // namespace Fast::Extension
