#pragma once

extern "C" {
#include "oot3d/scene_cutscene_intro_camera_timeline.h"
#include "oot3d/title_intro_opening_actor_runtime.h"
#include "oot3d/title_intro_opening_camera_runtime.h"
#include "oot3d/title_intro_opening_frame_runtime.h"
#include "oot3d/title_intro_opening_logo_runtime.h"
#include "oot3d/title_intro_playback_runtime.h"
#include "oot3d/title_intro_record_c_runtime.h"
#include "oot3d/title_intro_source_selector_feed_runtime.h"
#include "oot3d/title_intro_source_selector_runtime.h"
}

const char* TitleIntroSceneCameraStatusName(Oot3dCutsceneIntroCameraStatus status);
const char* TitleIntroOpeningFrameRuntimeStatusName(Oot3dTitleIntroOpeningFrameRuntimeStatus status);
const char* TitleIntroPlaybackStatusName(Oot3dTitleIntroPlaybackStatus status);
const char* TitleIntroRecordCStatusName(Oot3dTitleIntroRecordCStatus status);
const char* TitleIntroSourceSelectorStatusName(Oot3dTitleIntroSourceSelectorStatus status);
const char* TitleIntroSourceSelectorFeedStatusName(Oot3dTitleIntroSourceSelectorFeedStatus status);
const char* TitleIntroOpeningCameraRuntimeStatusName(Oot3dTitleIntroOpeningCameraRuntimeStatus status);
const char* TitleIntroOpeningActorSampleStatusName(Oot3dTitleIntroOpeningActorSampleStatus status);
const char* TitleIntroOpeningLogoRuntimeStatusName(Oot3dTitleIntroOpeningLogoRuntimeStatus status);
