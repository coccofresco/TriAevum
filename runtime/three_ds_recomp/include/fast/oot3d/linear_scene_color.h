#pragma once

#include <cstdint>
#include <string>

namespace Fast::Oot3d {

enum class SceneColorEncoding : uint8_t {
    Unknown,
    Linear,
    Srgb,
};

[[nodiscard]] constexpr bool IsKnownSceneColorEncoding(
    SceneColorEncoding encoding) {
    return encoding == SceneColorEncoding::Linear ||
           encoding == SceneColorEncoding::Srgb;
}

[[nodiscard]] constexpr bool SceneColorIsLinear(
    SceneColorEncoding encoding) {
    return encoding == SceneColorEncoding::Linear;
}

[[nodiscard]] constexpr bool SceneColorIsSrgb(
    SceneColorEncoding encoding) {
    return encoding == SceneColorEncoding::Srgb;
}

struct LinearSceneColorPolicy {
    SceneColorEncoding Source = SceneColorEncoding::Unknown;
    bool DecodeSrgb = false;
    bool Valid = false;
};

// Native PICA display-transfer color is already in the display-response domain
// expected by direct scanout. Extensions that need linear working color must
// decode it and encode exactly once when returning to presentation.
[[nodiscard]] constexpr SceneColorEncoding NativePicaSceneColorEncoding() {
    return SceneColorEncoding::Srgb;
}

[[nodiscard]] LinearSceneColorPolicy BuildLinearSceneColorPolicy(
    SceneColorEncoding source);
[[nodiscard]] float SrgbToLinear(float value);
[[nodiscard]] float LinearToSrgb(float value);
[[nodiscard]] std::string BuildLinearSceneColorComputeShader();

} // namespace Fast::Oot3d
