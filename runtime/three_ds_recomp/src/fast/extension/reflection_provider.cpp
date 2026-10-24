#include "fast/extension/reflection_provider.h"

namespace Fast::Extension {

ReflectionProvider SelectReflectionProvider(
    ReflectionMode requested,
    const ReflectionProviderAvailability& availability) {
    if (requested == ReflectionMode::Off)
        return ReflectionProvider::Off;
    if (requested == ReflectionMode::FidelityFxSssr &&
        availability.FidelityFxSssr &&
        availability.MotionVectors &&
        availability.TemporalHistory &&
        availability.PerspectiveDepth &&
        availability.LinearHdrWorkingColor) {
        return ReflectionProvider::FidelityFxSssr;
    }
    return availability.HiZ ? ReflectionProvider::HiZ
                            : ReflectionProvider::Off;
}

} // namespace Fast::Extension
