#include "oot3d_native_camera_controller.h"

#include "oot3d_link_surface_state.h"
#include "oot3d_native_bg_camera.h"
#include "oot3d_native_camera_collision.h"
#include "oot3d_native_camera_normal.h"
#include "oot3d_native_camera_scene.h"

Camera InitialCamera(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, const NativeCameraConfig& config,
                     const LinkInstance& link) {
    Camera camera;
    InitializeNativeNormal0Camera(config, link, camera);
    camera.NativeStartCameraDataIndex = NativeStartCameraDataIndex(scene, config);
    camera.NativeFloorCameraDataIndex = NativeCameraDataIndexForLinkFloor(scene, link);
    camera.NativeSceneCameraDataIndex = camera.NativeStartCameraDataIndex;

    const auto* startBgCamera = NativeBgCameraForDataIndex(scene, camera.NativeStartCameraDataIndex);
    const auto* floorBgCamera = NativeBgCameraForDataIndex(scene, camera.NativeFloorCameraDataIndex);
    const int floorNoneDefaultCameraDataIndex =
        NativeBgCameraHasActiveSetting(floorBgCamera) ? -1 : NativeFloorNoneDefaultCameraDataIndex(scene, config);
    const auto* floorNoneDefaultBgCamera =
        NativeBgCameraForDataIndex(scene, floorNoneDefaultCameraDataIndex);
    if (NativeBgCameraHasActiveSetting(floorNoneDefaultBgCamera) &&
        NativeStartCameraYieldsToFloorNoneDefault(config, startBgCamera) &&
        ApplyNativeBgCamera(scene, config, link, floorNoneDefaultCameraDataIndex, *floorNoneDefaultBgCamera,
                            camera, 0.0)) {
        return camera;
    }

    if (NativeBgCameraHasActiveSetting(startBgCamera) &&
        ApplyNativeBgCamera(scene, config, link, camera.NativeStartCameraDataIndex, *startBgCamera, camera, 0.0)) {
        camera.NativeStartSceneCameraApplied = true;
        return camera;
    }

    if (NativeBgCameraHasActiveSetting(floorBgCamera)) {
        camera.NativeSceneCameraDataIndex = camera.NativeFloorCameraDataIndex;
        if (ApplyNativeBgCamera(scene, config, link, camera.NativeFloorCameraDataIndex, *floorBgCamera, camera, 0.0)) {
            return camera;
        }
    }
    if (NativeBgCameraHasActiveSetting(floorNoneDefaultBgCamera) &&
        ApplyNativeBgCamera(scene, config, link, floorNoneDefaultCameraDataIndex, *floorNoneDefaultBgCamera,
                            camera, 0.0)) {
        return camera;
    }
    camera.NativeSceneCameraDataIndex = camera.NativeFloorCameraDataIndex;
    camera.NativeSceneCameraSetting =
        floorBgCamera != nullptr ? floorBgCamera->Setting : kNativeCameraSetNone;
    ApplyNativeCameraCollision(scene, config, camera);
    return camera;
}

void UpdateNativeOot3dCamera(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, const NativeCameraConfig& config,
                             const LinkInstance& link, Camera& camera, double dt) {
    camera.NativeStartCameraDataIndex = NativeStartCameraDataIndex(scene, config);
    camera.NativeFloorCameraDataIndex = NativeCameraDataIndexForLinkFloor(scene, link);
    const auto* floorBgCamera = NativeBgCameraForDataIndex(scene, camera.NativeFloorCameraDataIndex);
    const int previousSceneCameraDataIndex = camera.NativeSceneCameraDataIndex;
    const int previousSceneCameraSetting = camera.NativeSceneCameraSetting;
    const bool floorCameraSuppressed =
        camera.NativeFloorCameraDataIndex == camera.NativeSuppressedCameraDataIndex &&
        floorBgCamera != nullptr &&
        floorBgCamera->Setting == camera.NativeSuppressedCameraSetting;
    if (!floorCameraSuppressed) {
        camera.NativeSuppressedCameraDataIndex = -1;
        camera.NativeSuppressedCameraSetting = kNativeCameraSetNone;
    }

    if (NativeBgCameraHasActiveSetting(floorBgCamera) && !floorCameraSuppressed) {
        const bool sceneCameraChanged =
            previousSceneCameraDataIndex != camera.NativeFloorCameraDataIndex ||
            previousSceneCameraSetting != floorBgCamera->Setting;
        camera.NativeSceneCameraDataIndex = camera.NativeFloorCameraDataIndex;
        camera.NativeSceneCameraSetting = floorBgCamera->Setting;
        camera.NativeSceneCameraPositionIndex = floorBgCamera->CameraPositionVectorIndex;
        if (sceneCameraChanged && NativeCameraSettingIsNormalFollowFamily(floorBgCamera->Setting)) {
            ResetNativeNormal1AnimationState(config, link, camera);
        }
    } else if (!floorCameraSuppressed) {
        const int floorNoneDefaultCameraDataIndex = NativeFloorNoneDefaultCameraDataIndex(scene, config);
        const auto* floorNoneDefaultBgCamera =
            NativeBgCameraForDataIndex(scene, floorNoneDefaultCameraDataIndex);
        const bool activeCameraYields =
            camera.NativeSceneCameraDataIndex < 0 ||
            camera.NativeSceneCameraSetting == kNativeCameraSetNone ||
            camera.NativeSceneCameraDataIndex == camera.NativeFloorCameraDataIndex ||
            NativeCameraSettingListed(config.FloorNoneDefaultStartSettingsThatYield,
                                      camera.NativeSceneCameraSetting);
        if (NativeBgCameraHasActiveSetting(floorNoneDefaultBgCamera) && activeCameraYields) {
            camera.NativeSceneCameraDataIndex = floorNoneDefaultCameraDataIndex;
            camera.NativeSceneCameraSetting = floorNoneDefaultBgCamera->Setting;
            camera.NativeSceneCameraPositionIndex = floorNoneDefaultBgCamera->CameraPositionVectorIndex;
        }
    } else if (floorCameraSuppressed && camera.NativeSceneCameraDataIndex == camera.NativeFloorCameraDataIndex) {
        camera.NativeSceneCameraDataIndex = -1;
        camera.NativeSceneCameraSetting = kNativeCameraSetNone;
        camera.NativeSceneCameraPositionIndex = -1;
    }

    const auto* activeBgCamera = NativeBgCameraForDataIndex(scene, camera.NativeSceneCameraDataIndex);
    if (NativeBgCameraHasActiveSetting(activeBgCamera) &&
        ApplyNativeBgCamera(scene, config, link, camera.NativeSceneCameraDataIndex, *activeBgCamera, camera, dt)) {
        camera.NativeStartSceneCameraApplied =
            camera.NativeSceneCameraDataIndex == camera.NativeStartCameraDataIndex;
        return;
    }

    UpdateNativeNormal0Camera(config, link, camera, dt);
    camera.NativeFloorCameraDataIndex = NativeCameraDataIndexForLinkFloor(scene, link);
    camera.NativeSceneCameraDataIndex = floorCameraSuppressed ? -1 : camera.NativeFloorCameraDataIndex;
    camera.NativeSceneCameraSetting =
        (!floorCameraSuppressed && floorBgCamera != nullptr) ? floorBgCamera->Setting : kNativeCameraSetNone;
    ApplyNativeCameraCollision(scene, config, camera);
}
