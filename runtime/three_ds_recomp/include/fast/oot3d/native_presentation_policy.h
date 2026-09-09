#pragma once

#include "fast/oot3d/graphics_settings.h"

namespace Fast::Oot3d {

inline constexpr bool BuildRequiresNativePresentation() {
#if defined(__ANDROID__)
    return true;
#else
    return false;
#endif
}

// The mobile baseline uses the exact F2 mask, not the Authentic preset. Keep
// configured values intact and leave native lighting, pacing and composition alone.
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
    }

  private:
    bool mRequired;
    bool mRequested = false;
};

} // namespace Fast::Oot3d
