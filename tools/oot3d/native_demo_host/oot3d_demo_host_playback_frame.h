#pragma once

#include <chrono>
#include <cstddef>

#include "fast/Fast3dWindow.h"
#include "oot3d_demo_host_input.h"
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

struct WindowDemoPlaybackTiming {
    bool IntroCutsceneStartTimeResolved = false;
    std::chrono::steady_clock::time_point IntroCutsceneStartTime{};
    bool TitleIntroStartTimeResolved = false;
    std::chrono::steady_clock::time_point TitleIntroStartTime{};
    uint64_t PerformanceSampleCount = 0;
    double TitleIntroSeconds = 0.0;
    double PlayerMovementSeconds = 0.0;
    double CameraSeconds = 0.0;
    double PlayerAnimationSeconds = 0.0;
    double PlayerPostPoseSeconds = 0.0;
    double ActorRuntimeSeconds = 0.0;
};

bool UpdateWindowDemoPlaybackFrame(
    const Args& args, const Oot3dDemoHostInputState& input,
    Fast::Fast3dWindow& window, const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene, const LinkNativeLocomotionConfig& locomotionConfig,
    const NativeCameraConfig& cameraConfig, size_t baseActorVisualCount, double aspect, double dt,
    std::chrono::steady_clock::time_point now, const LinkInstance& resetLink, LinkInstance& link,
    LinkAnimation& animation, Camera& camera, IntroCutscenePlayback& introCutscene, TitleIntroPlayback& titleIntro,
    LinkMotionState& lastLinkMotion, WindowDemoPlaybackTiming& timing);
