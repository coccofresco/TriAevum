#pragma once

#include "fast/extension/graphics_settings.h"

#include <cstdint>

namespace Fast::Extension {

enum class ReflectionProvider : uint8_t {
    Off,
    HiZ,
    FidelityFxSssr,
    // Brixelizer GI specular resolved through the material guide (selected
    // by the display plan, never by SelectReflectionProvider).
    GlobalIllumination,
};

struct ReflectionProviderAvailability {
    bool HiZ = false;
    bool FidelityFxSssr = false;
    bool MotionVectors = false;
    bool TemporalHistory = false;
    bool PerspectiveDepth = false;
    bool LinearHdrWorkingColor = false;
};

// Resolves a user-facing reflection mode to the provider that can execute for
// the current view. FidelityFX SSSR deliberately falls back to the local Hi-Z
// implementation for W-buffered scenes and incomplete temporal inputs.
[[nodiscard]] ReflectionProvider SelectReflectionProvider(
    ReflectionMode requested,
    const ReflectionProviderAvailability& availability);

} // namespace Fast::Extension
