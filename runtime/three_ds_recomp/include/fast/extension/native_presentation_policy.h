#pragma once

#include "fast/extension/graphics_settings.h"

namespace Fast::Extension {

// No build forces the native presentation: Android renders the configured
// effects like the desktop (its product defaults keep grass off), and F2
// toggles the mask on every platform. A required policy remains available to
// a platform that cannot render the effect graph.
inline constexpr bool BuildRequiresNativePresentation() {
    return false;
}

// The native presentation is the exact F2 mask, not the Authentic preset. Keep
// configured values intact and preserve pacing and native composition.
class NativePresentationPolicy {
  public:
    explicit constexpr NativePresentationPolicy(bool required = BuildRequiresNativePresentation())
        : mRequired(required) {}

    [[nodiscard]] bool Active() const { return mRequired || mRequested; }
    [[nodiscard]] bool Required() const { return mRequired; }
    bool Toggle() {
        if (mRequired) return false;
        mRequested = !mRequested;
        return true;
    }
    void Apply(GraphicsSettings& effective) const {
        if (!Active()) return;
        effective.Grass.Quality = GrassQuality::Off;
        effective.Effects.Toon = ToonMode::Off;
        effective.Effects.ToonStyle.OutlineEnabled = false;
        effective.Effects.AmbientOcclusion = AmbientOcclusionMode::Off;
        effective.Effects.Reflections = ReflectionMode::Off;
        effective.Effects.DirectionalShadows.Mode = DirectionalShadowMode::Off;
        effective.Effects.PointShadows.Mode = PointShadowMode::Off;
        effective.Effects.GlobalIllumination.Mode = GlobalIlluminationMode::Off;
        effective.Effects.SurfaceLighting = SurfaceLightingMode::Native;
    }

  private:
    bool mRequired;
    bool mRequested = false;
};

} // namespace Fast::Extension
