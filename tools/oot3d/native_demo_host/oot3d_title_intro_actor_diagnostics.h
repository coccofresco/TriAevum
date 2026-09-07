#pragma once

#include <cstddef>
#include <vector>

#include <nlohmann/json.hpp>

#include "oot3d_title_intro_runtime_types.h"
#include "three_ds_recomp/oot3d/Oot3dNativeRenderScene.h"

nlohmann::json TitleIntroActorBatchBoneDiagnostics(
    const ThreeDsRecomp::Oot3d::CmbModel& model,
    const ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel& renderModel,
    size_t maxBatchCount);
nlohmann::json TitleIntroActorTransformDiagnostics(
    const TitleIntroNativeActor& actor,
    const ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel& renderModel,
    const ThreeDsRecomp::Oot3d::CsabPose& pose,
    const std::vector<ThreeDsRecomp::Oot3d::Matrix4f>& skinTransforms,
    float poseFrame,
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 drawPosition);
