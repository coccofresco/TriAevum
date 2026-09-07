#include "oot3d_demo_host_camera_controls.h"

#include "oot3d_native_camera_controller.h"

void UpdateCamera(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                  const NativeCameraConfig& config, const LinkInstance& link,
                  Camera& camera, double dt) {
    UpdateNativeOot3dCamera(scene, config, link, camera, dt);
}
