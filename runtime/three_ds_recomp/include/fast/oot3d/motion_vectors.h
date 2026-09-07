#pragma once

#include <array>
#include <string>

namespace Fast::Oot3d {

struct CameraMotionSample {
    std::array<float, 2> MotionUv{};
    bool Disoccluded = true;
};

[[nodiscard]] CameraMotionSample ComputeCameraMotionUv(
    const std::array<float, 2>& currentUv, float linearViewDepth,
    const std::array<float, 16>& inverseCurrentWorldToClip,
    const std::array<float, 16>& previousWorldToClip,
    const std::array<float, 3>& eye,
    const std::array<float, 3>& forward, bool historyValid);

[[nodiscard]] std::string BuildCameraMotionComputeShader();

} // namespace Fast::Oot3d
