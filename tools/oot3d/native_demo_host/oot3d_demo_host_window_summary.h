#pragma once

#include <cstdint>

#include <nlohmann/json.hpp>

#include "fast/Fast3dWindow.h"
#include "oot3d_demo_host_screenshot.h"
#include "oot3d_demo_host_types.h"
#include "oot3d_intro_cutscene_runtime_types.h"
#include "oot3d_link_animation.h"
#include "oot3d_link_locomotion_config.h"
#include "oot3d_link_runtime_types.h"
#include "oot3d_native_camera_config.h"
#include "oot3d_native_camera_runtime.h"
#include "oot3d_title_intro_runtime_types.h"
#include "three_ds_recomp/oot3d/Oot3dNativeDemoScene.h"
#include "three_ds_recomp/oot3d/Oot3dNativeRenderScene.h"

nlohmann::json BuildWindowDemoSummary(
    const Args& args, const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene, const LinkNativeLocomotionConfig& locomotionConfig,
    const NativeCameraConfig& cameraConfig, uint32_t frameCount, const Camera& camera,
    const IntroCutscenePlayback& introCutscene, const TitleIntroPlayback& titleIntro, const LinkInstance& link,
    const LinkMotionState& lastLinkMotion, const LinkAnimation& animation, const nlohmann::json& lastSubmission,
    const nlohmann::json& lastAdapterStats, const nlohmann::json& fast3dLifetimeStats, Fast::Fast3dWindow& window,
    const FramebufferScreenshotState& screenshotState);
