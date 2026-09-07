#pragma once

#include "oot3d_demo_host_input.h"
#include "oot3d_link_locomotion_config.h"

Vec3 LinkInputDirection(const Oot3dDemoHostInputState& input, const Camera& camera);
LinkMotionState UpdateLinkInstance(const Oot3dDemoHostInputState& input,
                                   const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                                   const LinkNativeLocomotionConfig& config, const Camera& camera,
                                   LinkInstance& link, const LinkInstance& resetState, double dt);
