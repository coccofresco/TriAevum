#pragma once

#include <cstddef>

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

struct WindowDemoState {
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene Scene;
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene RenderScene;
    LinkNativeLocomotionConfig LocomotionConfig;
    NativeCameraConfig CameraConfig;
    LinkInstance Link;
    LinkInstance ResetLink;
    LinkAnimation Animation;
    Camera CameraState;
    IntroCutscenePlayback IntroCutscene;
    TitleIntroPlayback TitleIntro;
    size_t BaseActorVisualCount = 0;
};

WindowDemoState InitializeWindowDemoState(const Args& args);
