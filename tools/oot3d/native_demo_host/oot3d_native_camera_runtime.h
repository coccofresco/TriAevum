#pragma once

#include <cstdint>

#include "oot3d_link_runtime_types.h"
#include "oot3d_native_camera_config.h"

double NativeCameraPercent(double value);
double NativeCameraS16ToRadians(double value);
double NativeCameraAtEyeYaw(const Camera& camera);
double NativeCameraViewYawFromAtEyeYaw(double atEyeYaw);
double NativeCameraPlayerBehindAtEyeYaw(const LinkInstance& link);
double NativeCameraPlayerBehindViewYaw(const LinkInstance& link);
int16_t NativeCameraWrapS16(int32_t value);
int16_t NativeCameraRadiansToBinang(double value);
int16_t NativeCameraBinangRot180(int16_t angle);
int16_t NativeCameraBinangSub(int16_t left, int16_t right);
double NativeCameraColPolyGetNormal(int16_t value);
double NativeCameraClampAtLerpScale(const NativeCameraConfig& config, double currentScale, double maxScale);
