#pragma once

#include "oot3d_link_animation.h"
#include "three_ds_recomp/oot3d/Oot3dNativeRenderScene.h"

void ApplyLinkInstance(ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene, const LinkInstance& link);
void ApplyLinkAnimationFrame(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                             const LinkNativeLocomotionConfig& config,
                             ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene, LinkAnimation& animation,
                             const LinkInstance& link);
void SampleLinkAnimationPose(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                             const LinkNativeLocomotionConfig& config,
                             LinkAnimation& animation);
void ApplyLinkCurrentAnimationPose(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene,
    LinkAnimation& animation, const LinkInstance& link);
