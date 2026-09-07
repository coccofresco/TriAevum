#include "fast/oot3d/reflection_provider.h"

namespace Fast::Oot3d {

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

} // namespace Fast::Oot3d
