#pragma once

#include "oot3d_demo_host_types.h"
#include "oot3d_intro_cutscene_runtime_types.h"

IntroCutscenePlayback InitialIntroCutscenePlayback(const Args& args);

bool ApplyIntroCutsceneStepToCamera(const Oot3dCutsceneIntroRuntimeStep& step, Camera& camera);
bool ApplyCutsceneFrameSnapshotToCamera(
    const Oot3dSceneCutsceneFrameSnapshot& snapshot, Camera& camera);

void StepIntroCutscenePlayback(
    IntroCutscenePlayback& playback, Camera& camera, int32_t targetFrame = -1);
