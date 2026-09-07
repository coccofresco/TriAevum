#pragma once

#include "oot3d_link_locomotion_config.h"

LinkMotionState ApplyLinkMovementIntent(LinkInstance& link, const LinkNativeLocomotionConfig& config,
                                        const Vec3& direction, double speed, double dt);
LinkMotionState ApplyLinkMovementIntentWithCollision(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                                                     const LinkNativeLocomotionConfig& config,
                                                     LinkInstance& link, const Vec3& direction, double speed,
                                                     double dt);
