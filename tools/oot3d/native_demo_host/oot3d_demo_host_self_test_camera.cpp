#include "oot3d_demo_host_self_test_camera.h"

#include <cmath>

#include "oot3d_demo_host_diagnostics.h"
#include "oot3d_demo_math.h"
#include "oot3d_link_instance.h"
#include "oot3d_link_movement.h"
#include "oot3d_link_surface_state.h"
#include "oot3d_native_bg_camera.h"
#include "oot3d_native_camera_collision.h"
#include "oot3d_native_camera_controller.h"
#include "oot3d_native_camera_normal.h"
#include "oot3d_native_camera_scene.h"

CameraSelfTestDiagnostics BuildCameraSelfTestDiagnostics(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, const LinkNativeLocomotionConfig& locomotionConfig,
    const NativeCameraConfig& cameraConfig) {
    CameraSelfTestDiagnostics diagnostics;
    diagnostics.CameraTestLink = InitialLinkInstance(scene, locomotionConfig);
    diagnostics.InitialCamera = InitialCamera(scene, cameraConfig, diagnostics.CameraTestLink);
    diagnostics.CameraState = diagnostics.InitialCamera;
    const double cameraTestDt = 1.0 / locomotionConfig.PlayerTickRate;
    for (int frame = 0; frame < 18; ++frame) {
        diagnostics.CameraTestMotion =
            ApplyLinkMovementIntentWithCollision(scene, locomotionConfig, diagnostics.CameraTestLink,
                                                 { 1.0, 0.0, 0.0 }, diagnostics.CameraTestLink.MoveSpeed,
                                                 cameraTestDt);
        UpdateNativeOot3dCamera(scene, cameraConfig, diagnostics.CameraTestLink, diagnostics.CameraState,
                                cameraTestDt);
    }
    InitializeNativeNormal0Camera(cameraConfig, diagnostics.CameraTestLink, diagnostics.NormalFollowCamera);
    diagnostics.NormalFollowCamera.NativeFloorCameraDataIndex =
        NativeCameraDataIndexForLinkFloor(scene, diagnostics.CameraTestLink);
    diagnostics.NormalFollowCamera.NativeSceneCameraDataIndex =
        diagnostics.NormalFollowCamera.NativeFloorCameraDataIndex;
    diagnostics.NormalFollowCamera.NativeSceneCameraSetting = kNativeCameraSetNone;
    UpdateNativeNormal0Camera(cameraConfig, diagnostics.CameraTestLink, diagnostics.NormalFollowCamera, cameraTestDt);
    ApplyNativeCameraCollision(scene, cameraConfig, diagnostics.NormalFollowCamera);
    diagnostics.FinalExpectedCameraTarget = NativeCameraFocusTargetForLink(diagnostics.CameraTestLink, cameraConfig);
    diagnostics.CameraTargetError =
        std::sqrt(Dot(Subtract(diagnostics.NormalFollowCamera.Target, diagnostics.FinalExpectedCameraTarget),
                      Subtract(diagnostics.NormalFollowCamera.Target, diagnostics.FinalExpectedCameraTarget)));

    Camera cameraWallProbe;
    InitializeNativeNormal0Camera(cameraConfig, InitialLinkInstance(scene, locomotionConfig), cameraWallProbe);
    cameraWallProbe.Target = NativeCameraFocusTargetForLink(InitialLinkInstance(scene, locomotionConfig), cameraConfig);
    cameraWallProbe.Position = {
        cameraWallProbe.Target.X,
        cameraWallProbe.Target.Y,
        cameraWallProbe.Target.Z + 220.0,
    };
    LookAt(cameraWallProbe, cameraWallProbe.Target);
    diagnostics.CameraWallProbeBefore = cameraWallProbe;
    diagnostics.CameraWallProbeHit = ApplyNativeCameraCollision(scene, cameraConfig, cameraWallProbe);
    diagnostics.CameraWallProbe = cameraWallProbe;
    diagnostics.CameraWallProbeRepeat = cameraWallProbe;
    diagnostics.CameraWallProbeRepeatHit =
        ApplyNativeCameraCollision(scene, cameraConfig, diagnostics.CameraWallProbeRepeat);

    diagnostics.CameraInputBehindTarget.Position = { 0.0, 0.0, 10.0 };
    diagnostics.CameraInputBehindTarget.Target = { 0.0, 0.0, 0.0 };
    LookAt(diagnostics.CameraInputBehindTarget, diagnostics.CameraInputBehindTarget.Target);
    diagnostics.CameraInputLeftOfTarget.Position = { -10.0, 0.0, 0.0 };
    diagnostics.CameraInputLeftOfTarget.Target = { 0.0, 0.0, 0.0 };
    LookAt(diagnostics.CameraInputLeftOfTarget, diagnostics.CameraInputLeftOfTarget.Target);
    diagnostics.CameraYawBasisLink = InitialLinkInstance(scene, locomotionConfig);
    diagnostics.CameraYawBasisLink.Yaw = kOot3dDemoPi * 0.5;
    InitializeNativeNormal0Camera(cameraConfig, diagnostics.CameraYawBasisLink, diagnostics.CameraYawBasis);
    diagnostics.CameraYawBasisPositionOffset =
        Subtract(diagnostics.CameraYawBasis.Position, diagnostics.CameraYawBasis.Target);

    diagnostics.BgCameraApplicationTests = nlohmann::json::array();
    diagnostics.Unique0ExitTest = {
        { "available", false },
    };
    for (size_t index = 0; index < scene.Collision.BgCameras.size(); ++index) {
        const auto& bgCamera = scene.Collision.BgCameras[index];
        if (bgCamera.Setting == kNativeCameraSetNone) {
            continue;
        }
        Camera bgCameraProbe = diagnostics.InitialCamera;
        const bool applied =
            ApplyNativeBgCamera(scene, cameraConfig, diagnostics.CameraTestLink, static_cast<int>(index), bgCamera,
                                bgCameraProbe, cameraTestDt);
        diagnostics.BgCameraApplicationTests.push_back({
            { "index", index },
            { "setting", bgCamera.Setting },
            { "camera_position_vector_index", bgCamera.CameraPositionVectorIndex },
            { "applied", applied },
            { "camera", CameraToJson(bgCameraProbe) },
        });

        if (bgCamera.Setting == kNativeCameraSetStart1 && diagnostics.Unique0ExitTest["available"] == false) {
            LinkInstance unique0Link = InitialLinkInstance(scene, locomotionConfig);
            Camera unique0Camera = diagnostics.InitialCamera;
            const bool initialApplied =
                ApplyNativeBgCamera(scene, cameraConfig, unique0Link, static_cast<int>(index), bgCamera,
                                    unique0Camera, 0.0);
            bool exitApplied = initialApplied;
            for (int frame = 0; frame < 16 && exitApplied; ++frame) {
                unique0Link.NativeLinearVelocityUnitsPerTick = locomotionConfig.RunSpeedUnitsPerTick;
                auto actor = LinkActorPosition(unique0Link);
                actor.X += 2.0;
                SetLinkActorPosition(unique0Link, actor);
                exitApplied =
                    ApplyNativeBgCamera(scene, cameraConfig, unique0Link, static_cast<int>(index), bgCamera,
                                        unique0Camera, cameraTestDt);
            }
            diagnostics.Unique0ExitTest = {
                { "available", true },
                { "index", index },
                { "initial_applied", initialApplied },
                { "exit_applied", exitApplied },
                { "camera", CameraToJson(unique0Camera) },
            };
        }
    }
    return diagnostics;
}

void AddCameraSelfTestSummary(nlohmann::json& summary, const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                              const LinkNativeLocomotionConfig& locomotionConfig,
                              const NativeCameraConfig& cameraConfig,
                              const CameraSelfTestDiagnostics& diagnostics) {
    summary["camera"] = CameraToJson(diagnostics.CameraState);
    summary["native_oot3d_camera_test"] = {
        { "initial_camera", CameraToJson(diagnostics.InitialCamera) },
        { "final_camera", CameraToJson(diagnostics.CameraState) },
        { "final_link_instance", LinkInstanceToJson(scene, diagnostics.CameraTestLink, locomotionConfig) },
        { "final_motion", LinkMotionToJson(diagnostics.CameraTestMotion) },
        { "expected_final_target",
          { { "x", diagnostics.FinalExpectedCameraTarget.X },
            { "y", diagnostics.FinalExpectedCameraTarget.Y },
            { "z", diagnostics.FinalExpectedCameraTarget.Z } } },
        { "final_target_error", diagnostics.CameraTargetError },
        { "active_camera_data_index", diagnostics.CameraState.NativeSceneCameraDataIndex },
        { "active_camera_setting", diagnostics.CameraState.NativeSceneCameraSetting },
        { "floor_camera_data_index", diagnostics.CameraState.NativeFloorCameraDataIndex },
        { "surface_camera_data_index", diagnostics.CameraState.NativeFloorCameraDataIndex },
        { "surface_camera_setting",
          NativeBgCameraForDataIndex(scene, diagnostics.CameraState.NativeFloorCameraDataIndex) != nullptr
              ? NativeBgCameraForDataIndex(scene, diagnostics.CameraState.NativeFloorCameraDataIndex)->Setting
              : kNativeCameraSetNone },
        { "behavior", diagnostics.CameraState.NativeBehavior },
        { "collision_camera_data_supported", cameraConfig.CollisionCameraDataSupported },
        { "collision_line_test_supported", cameraConfig.CollisionLineTestSupported },
        { "collision_polygon_flags_supported", cameraConfig.CollisionPolygonFlagsSupported },
        { "start_camera_data_supported", cameraConfig.StartCameraDataSupported },
        { "start_camera_data_index", cameraConfig.StartCameraDataIndex },
        { "normal_follow_camera", CameraToJson(diagnostics.NormalFollowCamera) },
        { "wall_probe_hit", diagnostics.CameraWallProbeHit },
        { "wall_probe_before", CameraToJson(diagnostics.CameraWallProbeBefore) },
        { "wall_probe_after", CameraToJson(diagnostics.CameraWallProbe) },
        { "wall_probe_repeat_hit", diagnostics.CameraWallProbeRepeatHit },
        { "wall_probe_repeat_after", CameraToJson(diagnostics.CameraWallProbeRepeat) },
        { "bgcam_application_tests", diagnostics.BgCameraApplicationTests },
        { "unique0_exit_test", diagnostics.Unique0ExitTest },
    };
    summary["native_oot3d_input_direction_basis_test"] = {
        { "basis", kNativeCameraInputDirectionBasis },
        { "behind_target",
          {
              { "camera", CameraToJson(diagnostics.CameraInputBehindTarget) },
              { "forward", Vec3ToJson(NativeCameraInputForward(diagnostics.CameraInputBehindTarget)) },
              { "right", Vec3ToJson(NativeCameraInputRight(diagnostics.CameraInputBehindTarget)) },
          } },
        { "left_of_target",
          {
              { "camera", CameraToJson(diagnostics.CameraInputLeftOfTarget) },
              { "forward", Vec3ToJson(NativeCameraInputForward(diagnostics.CameraInputLeftOfTarget)) },
              { "right", Vec3ToJson(NativeCameraInputRight(diagnostics.CameraInputLeftOfTarget)) },
          } },
    };
    summary["native_oot3d_camera_yaw_convention_test"] = {
        { "basis", kNativeCameraYawConventionBasis },
        { "link_yaw", diagnostics.CameraYawBasisLink.Yaw },
        { "camera_at_eye_yaw", NativeCameraAtEyeYaw(diagnostics.CameraYawBasis) },
        { "camera", CameraToJson(diagnostics.CameraYawBasis) },
        { "position_offset", Vec3ToJson(diagnostics.CameraYawBasisPositionOffset) },
        { "forward", Vec3ToJson(NativeCameraInputForward(diagnostics.CameraYawBasis)) },
        { "right", Vec3ToJson(NativeCameraInputRight(diagnostics.CameraYawBasis)) },
    };
}
