#pragma once

#include "oot3d_demo_host_input.h"
#include "oot3d_demo_host_types.h"
#include "oot3d_link_locomotion_config.h"
#include "oot3d_link_runtime_types.h"
#include "three_ds_recomp/oot3d/Oot3dNativeDemoScene.h"
#include "three_ds_recomp/oot3d/Oot3dNativeRenderScene.h"

class Oot3dDemoHostPlayerController {
  public:
    virtual ~Oot3dDemoHostPlayerController() = default;

    virtual LinkMotionState UpdateMovement(
        const Oot3dDemoHostInputState& input,
        const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
        const LinkNativeLocomotionConfig& locomotionConfig,
        const Camera& camera,
        LinkInstance& link,
        const LinkInstance& resetLink,
        double deltaSeconds) = 0;

    virtual void UpdateAnimation(
        const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
        const LinkNativeLocomotionConfig& locomotionConfig,
        ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene,
        LinkAnimation& animation,
        const LinkMotionState& motion,
        LinkInstance& link,
        double deltaSeconds) = 0;
};
