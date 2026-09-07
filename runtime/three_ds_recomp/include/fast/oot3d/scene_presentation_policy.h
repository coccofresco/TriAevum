#pragma once

#include <cmath>
#include <cstdint>

namespace Fast::Oot3d {

inline constexpr std::uint32_t kNativeTopScreenLogicalWidth = 400U;
inline constexpr std::uint32_t kNativeTopScreenLogicalHeight = 240U;
inline constexpr std::uint32_t kNativeTopScreenPhysicalWidth = 480U;
inline constexpr std::uint32_t kNativeTopScreenPhysicalHeight = 400U;
inline constexpr float kNativeTopScreenAspect =
    static_cast<float>(kNativeTopScreenLogicalWidth) /
    static_cast<float>(kNativeTopScreenLogicalHeight);

enum class SceneAspectExtension : std::uint8_t {
    None = 0,
    Horizontal,
    Vertical,
};

struct ScenePresentationPolicy {
    float NativeAspect = kNativeTopScreenAspect;
    float OutputAspect = kNativeTopScreenAspect;
    float HorizontalFovExpansion = 1.0F;
    float VerticalFovExpansion = 1.0F;
    SceneAspectExtension Extension = SceneAspectExtension::None;
    bool Valid = false;
};

// The native 400x240 view is the optical baseline. Wider outputs reveal more
// horizontally; narrower outputs reveal more vertically. Neither path crops
// or stretches the scene, and UI presentation remains a separate contract.
[[nodiscard]] inline ScenePresentationPolicy ResolveScenePresentationPolicy(
    std::uint32_t outputWidth, std::uint32_t outputHeight,
    float nativeAspect = kNativeTopScreenAspect) noexcept {
    ScenePresentationPolicy result;
    if (outputWidth == 0U || outputHeight == 0U ||
        !std::isfinite(nativeAspect) || nativeAspect <= 0.0F) {
        return result;
    }

    result.NativeAspect = nativeAspect;
    result.OutputAspect = static_cast<float>(outputWidth) /
                          static_cast<float>(outputHeight);
    if (!std::isfinite(result.OutputAspect) || result.OutputAspect <= 0.0F) {
        return result;
    }

    constexpr float kAspectEpsilon = 1.0e-6F;
    if (result.OutputAspect > nativeAspect + kAspectEpsilon) {
        result.HorizontalFovExpansion = result.OutputAspect / nativeAspect;
        result.Extension = SceneAspectExtension::Horizontal;
    } else if (result.OutputAspect < nativeAspect - kAspectEpsilon) {
        result.VerticalFovExpansion = nativeAspect / result.OutputAspect;
        result.Extension = SceneAspectExtension::Vertical;
    }
    result.Valid = true;
    return result;
}

} // namespace Fast::Oot3d
