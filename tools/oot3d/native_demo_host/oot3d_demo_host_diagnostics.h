#pragma once

#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "oot3d_demo_host_types.h"
#include "oot3d_link_locomotion_config.h"
#include "oot3d_link_runtime_types.h"
#include "oot3d_native_camera_config.h"

std::map<std::string, float> MaterialAnimationFramesByRole(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoLinkMaterialFrameSelection& selection);
nlohmann::json Vec3ToJson(const Vec3& value);
nlohmann::json DemoVec3ToJson(const ThreeDsRecomp::Oot3d::Oot3dDemoVec3& value);
nlohmann::json CameraToJson(const Camera& camera);
nlohmann::json LinkInstanceToJson(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, const LinkInstance& link,
                                  const LinkNativeLocomotionConfig& config);
nlohmann::json LinkMotionToJson(const LinkMotionState& motion);
nlohmann::json LinkMaterialFrameSelectionToJson(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoLinkMaterialFrameSelection& selection);
nlohmann::json LinkNativeClipReferenceToJson(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, size_t clipIndex);
nlohmann::json NativeCameraConfigToJson(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                                        const NativeCameraConfig& config);
nlohmann::json LinkNativeLocomotionConfigToJson(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                                                const LinkNativeLocomotionConfig& config);
nlohmann::json LinkAnimationToJson(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                                   const LinkAnimation& animation,
                                   const LinkNativeLocomotionConfig& config);
