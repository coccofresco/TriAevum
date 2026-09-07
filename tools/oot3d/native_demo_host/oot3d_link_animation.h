#pragma once

#include "oot3d_link_locomotion_config.h"
#include "three_ds_recomp/oot3d/Oot3dNativeDemoScene.h"

void UpdateLinkMotionLocomotionAnimationClass(LinkMotionState& motion, const LinkInstance& link,
                                              const LinkNativeLocomotionConfig& config);
LinkAnimation InitialLinkAnimation(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                                   const LinkNativeLocomotionConfig& config);
void AdvanceLinkAnimation(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                          const LinkNativeLocomotionConfig& config, LinkAnimation& animation,
                          const LinkMotionState& motion, double dt);
ThreeDsRecomp::Oot3d::Matrix4f BlendMatrix(const ThreeDsRecomp::Oot3d::Matrix4f& from, const ThreeDsRecomp::Oot3d::Matrix4f& to,
                                  double weight);
ThreeDsRecomp::Oot3d::CsabPose BlendCsabPoses(const ThreeDsRecomp::Oot3d::CsabPose& from, const ThreeDsRecomp::Oot3d::CsabPose& to,
                                     double weight);
