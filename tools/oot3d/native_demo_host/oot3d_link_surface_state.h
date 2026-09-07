#pragma once

#include "oot3d_link_instance.h"
#include "three_ds_recomp/oot3d/Oot3dNativeRenderScene.h"

int NativeCameraDataIndexForLinkFloor(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                                      const LinkInstance& link);
int NativeLightSettingRawIndexForLinkFloor(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                                           const LinkInstance& link);
int NativeLightSettingIndexForLinkFloor(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                                        const LinkInstance& link);
void UpdateLinkActorShadowState(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, const LinkInstance& link,
                                ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene);
