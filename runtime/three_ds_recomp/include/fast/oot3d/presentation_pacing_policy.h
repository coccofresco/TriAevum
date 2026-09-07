#pragma once

#include "fast/oot3d/graphics_settings.h"
#include "fast/oot3d/native_frame_composer.h"

#include <cstdint>

namespace Fast::Oot3d {

struct PresentationPacingPolicy {
    uint32_t TargetRateHz = 0;
    bool Enabled = false;
    NativeFrameCompositionPolicy Composition;
};

[[nodiscard]] constexpr PresentationPacingPolicy
ResolvePresentationPacingPolicy(FrameRateMode mode) {
    switch (mode) {
        case FrameRateMode::Original30:
            return {30U, true,
                    {NativeVisualInterpolationMode::Disabled, 30U, 30U, 1U,
                     true}};
        case FrameRateMode::Interpolated2x:
            return {60U, true,
                    {NativeVisualInterpolationMode::Fixed2x, 30U, 60U, 2U,
                     true}};
        case FrameRateMode::Interpolated3x:
            return {90U, true,
                    {NativeVisualInterpolationMode::Fixed3x, 30U, 90U, 3U,
                     true}};
        case FrameRateMode::Uncapped:
            return {0U, false,
                    {NativeVisualInterpolationMode::Adaptive, 30U, 0U, 0U,
                     false}};
    }
    return {30U, true,
            {NativeVisualInterpolationMode::Disabled, 30U, 30U, 1U, true}};
}

[[nodiscard]] constexpr bool PresentationPacingPolicyChanged(
    const PresentationPacingPolicy& current,
    const PresentationPacingPolicy& requested) {
    return current.TargetRateHz != requested.TargetRateHz ||
           current.Enabled != requested.Enabled ||
           current.Composition.Interpolation != requested.Composition.Interpolation ||
           current.Composition.FixedSampleMultiplier != requested.Composition.FixedSampleMultiplier;
}

} // namespace Fast::Oot3d
