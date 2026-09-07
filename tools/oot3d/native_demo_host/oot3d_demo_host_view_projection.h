#pragma once

#include <cstdint>

#include "oot3d_demo_host_types.h"
#include "three_ds_recomp/oot3d/Oot3dNativeRenderScene.h"

double AspectFromDimensions(uint32_t width, uint32_t height);

struct NativeViewProjectionMatrices {
    ThreeDsRecomp::Oot3d::Matrix4f ViewToClip;
    ThreeDsRecomp::Oot3d::Matrix4f WorldToClip;
};

NativeViewProjectionMatrices BuildAndMaterializeCurrentFrameNativeViewProjection(
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene, const Camera& camera,
    double aspect);
