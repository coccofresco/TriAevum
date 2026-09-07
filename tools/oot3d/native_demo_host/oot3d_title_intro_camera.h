#pragma once

#include "oot3d_demo_host_types.h"
#include "oot3d_title_intro_runtime_types.h"

bool ApplyTitleIntroNativeCamera(TitleIntroPlayback& playback, double timelineFrame, Camera& camera);
bool ApplyTitleIntroInitialSceneCamera(TitleIntroPlayback& playback, double sceneFrame, Camera& camera);
bool ApplyTitleIntroOpeningFrameRuntimeCamera(TitleIntroPlayback& playback, Camera& camera);
