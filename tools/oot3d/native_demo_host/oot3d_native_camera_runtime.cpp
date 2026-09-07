#include "oot3d_native_camera_runtime.h"

#include <cmath>

#include "oot3d_demo_math.h"

double NativeCameraPercent(double value) {
    return value / 100.0;
}

double NativeCameraS16ToRadians(double value) {
    return value * kOot3dS16AngleToRadians;
}

double NativeCameraAtEyeYaw(const Camera& camera) {
    return NormalizeAngleRadians(-camera.Yaw);
}

double NativeCameraViewYawFromAtEyeYaw(double atEyeYaw) {
    return NormalizeAngleRadians(-atEyeYaw);
}

double NativeCameraPlayerBehindAtEyeYaw(const LinkInstance& link) {
    return NormalizeAngleRadians(link.Yaw + kOot3dDemoPi);
}

double NativeCameraPlayerBehindViewYaw(const LinkInstance& link) {
    return NativeCameraViewYawFromAtEyeYaw(NativeCameraPlayerBehindAtEyeYaw(link));
}

int16_t NativeCameraWrapS16(int32_t value) {
    int32_t wrapped = value & 0xFFFF;
    if (wrapped >= 0x8000) {
        wrapped -= 0x10000;
    }
    return static_cast<int16_t>(wrapped);
}

int16_t NativeCameraRadiansToBinang(double value) {
    const int32_t raw =
        static_cast<int32_t>(std::lround(NormalizeAngleRadians(value) / kOot3dS16AngleToRadians));
    return NativeCameraWrapS16(raw);
}

int16_t NativeCameraBinangRot180(int16_t angle) {
    return NativeCameraWrapS16(static_cast<int32_t>(angle) - 0x7FFF);
}

int16_t NativeCameraBinangSub(int16_t left, int16_t right) {
    return NativeCameraWrapS16(static_cast<int32_t>(left) - static_cast<int32_t>(right));
}

double NativeCameraColPolyGetNormal(int16_t value) {
    return static_cast<double>(value) / 32767.0;
}

double NativeCameraClampAtLerpScale(const NativeCameraConfig& config, double currentScale, double maxScale) {
    const double minScale = NativeCameraPercent(config.Normal0.Oreg41AtLerpMinPercent);
    if (currentScale < minScale) {
        return minScale;
    }
    if (currentScale >= maxScale) {
        return maxScale;
    }
    return NativeCameraPercent(config.Normal0.Oreg42AtLerpScalePercent) * currentScale;
}
