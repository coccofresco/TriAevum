#pragma once

#include <nlohmann/json.hpp>

#include "oot3d_intro_cutscene_status_names.h"
#include "oot3d_intro_cutscene_runtime_types.h"
#include "oot3d_title_intro_runtime_types.h"

extern "C" {
#include "oot3d/scene_cutscene_intro_runtime.h"
}

nlohmann::json SceneCutsceneLightingRowToJson(const Oot3dSceneCutsceneLightingRow* row);
nlohmann::json SceneCutsceneMiscActionRowToJson(const Oot3dSceneCutsceneMiscActionRow* row);
nlohmann::json SceneCutsceneSetTimeRowToJson(const Oot3dSceneCutsceneSetTimeRow* row);
nlohmann::json IntroCutsceneStepToJson(const Oot3dCutsceneIntroRuntimeStep& step);
nlohmann::json IntroCutscenePlaybackToJson(const IntroCutscenePlayback& playback);
nlohmann::json TitleIntroQdbRowToJson(const Oot3dTitleIntroQdbSourceRow* row);
nlohmann::json TitleIntroOpeningOrchestrationToJson(
    const Oot3dTitleIntroOpeningOrchestrationRow* row);
nlohmann::json TitleIntroOpeningFrameTraceStepToJson(
    const Oot3dTitleIntroOpeningFrameRuntimeState& state,
    const Oot3dTitleIntroOpeningFrameRuntimeStep& step,
    Oot3dTitleIntroOpeningFrameRuntimeStatus stepStatus);
bool TitleIntroGateCaptureOwnerRuntimeDecoded();
nlohmann::json TitleIntroGateCaptureOwnerRuntimeProbeToJson();
nlohmann::json TitleIntroPlaybackToJson(const TitleIntroPlayback& playback);
