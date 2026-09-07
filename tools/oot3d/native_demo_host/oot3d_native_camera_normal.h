#pragma once

#include "oot3d_link_runtime_types.h"
#include "oot3d_native_camera_config.h"

Vec3 NativeCameraFocusTargetForLink(const LinkInstance& link, const NativeCameraConfig& config);
Vec3 NativeCameraFocusTargetForProfile(const LinkInstance& link, const NativeCameraConfig& config,
                                       const NativeCameraModeProfile& profile);
double NativeCameraSpeedRatio(const NativeCameraConfig& config, const LinkInstance& link);
double NativeCameraNormal1StartSwingTicks(const NativeCameraConfig& config);
void ResetNativeNormal1AnimationState(const NativeCameraConfig& config, const LinkInstance& link, Camera& camera);
void ApplyNativeConfiguredNormalCamera(const NativeCameraConfig& config, const LinkInstance& link,
                                       const NativeCameraModeProfile& profile, Camera& camera, double dt);
void InitializeNativeNormal0Camera(const NativeCameraConfig& config, const LinkInstance& link, Camera& camera);
void UpdateNativeNormal0Camera(const NativeCameraConfig& config, const LinkInstance& link, Camera& camera,
                               double dt);
