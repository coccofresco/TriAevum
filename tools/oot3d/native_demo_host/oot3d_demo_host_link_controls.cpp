#include "oot3d_demo_host_link_controls.h"

#include "oot3d_demo_math.h"
#include "oot3d_link_movement.h"

Vec3 LinkInputDirection(const Oot3dDemoHostInputState& input, const Camera& camera) {
    const Vec3 forward = NativeCameraInputForward(camera);
    const Vec3 right = NativeCameraInputRight(camera);
    Vec3 direction = Add(Scale(forward, input.MoveY), Scale(right, input.MoveX));
    return Normalize(direction);
}

LinkMotionState UpdateLinkInstance(const Oot3dDemoHostInputState& input,
                                   const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                                   const LinkNativeLocomotionConfig& config, const Camera& camera,
                                   LinkInstance& link, const LinkInstance& resetState, double dt) {
    const double speed = link.MoveSpeed * (input.Fast ? 2.5 : 1.0);
    const Vec3 direction = LinkInputDirection(input, camera);
    LinkMotionState motion;

    if (input.Reset) {
        link = resetState;
        return {};
    }

    motion = ApplyLinkMovementIntentWithCollision(scene, config, link, direction, speed, dt);
    link.Yaw = NormalizeAngleRadians(link.Yaw);
    link.MovementYaw = NormalizeAngleRadians(link.MovementYaw);
    return motion;
}
