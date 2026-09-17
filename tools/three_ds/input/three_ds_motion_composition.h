#pragma once

#include <array>

namespace ThreeDsRecomp::Input {

struct ComposedMotion {
    std::array<float, 3> Gravity;
    std::array<float, 3> AngularVelocity;
};

// Relative orientation of a virtual 3DS with respect to the physical controller.
// Transform gravity and body rates together; never feed virtual gyro against
// unrelated physical gravity to the guest's sensor fusion.
struct MotionCompositionState {
    std::array<double, 4> SensorToVirtual{1, 0, 0, 0};
    std::array<float, 3> LastGravity{0, -1, 0};
    std::array<float, 3> BaseGravity{0, -1, 0};
    bool HasOutput = false;
    bool Initialized = false;
    bool PhysicalBase = false;

    void RestoreGravity(const std::array<float, 3>& gravity) noexcept;
    void ResetReference() noexcept { Initialized = false; BaseGravity = {0, -1, 0}; }
    ComposedMotion Sample(const std::array<float, 3>& gravity, bool gravityValid,
        const std::array<float, 3>& bodyRate, float pitchRate, float yawRate,
        double seconds, bool physicalBase, bool advance) noexcept;
};

} // namespace ThreeDsRecomp::Input
