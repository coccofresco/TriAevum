#pragma once

#include <nlohmann/json.hpp>

#include "oot3d_demo_host_types.h"
#include "oot3d_link_locomotion_config.h"
#include "oot3d_link_runtime_types.h"
#include "oot3d_native_camera_config.h"
#include "oot3d_native_camera_runtime.h"
#include "three_ds_recomp/oot3d/Oot3dNativeDemoScene.h"

struct CameraSelfTestDiagnostics {
    LinkInstance CameraTestLink;
    LinkInstance CameraYawBasisLink;
    Camera InitialCamera;
    Camera CameraState;
    Camera NormalFollowCamera;
    Camera CameraWallProbeBefore;
    Camera CameraWallProbe;
    Camera CameraWallProbeRepeat;
    Camera CameraInputBehindTarget;
    Camera CameraInputLeftOfTarget;
    Camera CameraYawBasis;
    LinkMotionState CameraTestMotion;
    Vec3 FinalExpectedCameraTarget{};
    Vec3 CameraYawBasisPositionOffset{};
    double CameraTargetError = 0.0;
    bool CameraWallProbeHit = false;
    bool CameraWallProbeRepeatHit = false;
    nlohmann::json BgCameraApplicationTests;
    nlohmann::json Unique0ExitTest;
};

CameraSelfTestDiagnostics BuildCameraSelfTestDiagnostics(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, const LinkNativeLocomotionConfig& locomotionConfig,
    const NativeCameraConfig& cameraConfig);

void AddCameraSelfTestSummary(nlohmann::json& summary, const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                              const LinkNativeLocomotionConfig& locomotionConfig,
                              const NativeCameraConfig& cameraConfig,
                              const CameraSelfTestDiagnostics& diagnostics);
