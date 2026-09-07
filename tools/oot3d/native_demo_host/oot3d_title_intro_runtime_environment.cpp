#include "oot3d_title_intro_runtime_environment.h"

#include <cmath>
#include <utility>

namespace {
ThreeDsRecomp::Oot3d::Oot3dNativeRuntimeEnvironmentInput GenericCutsceneRuntimeEnvironmentInput(
    const TitleIntroPlayback& playback) {
    ThreeDsRecomp::Oot3d::Oot3dNativeRuntimeEnvironmentInput input;
    if (!playback.OpeningFrameRuntimeStepValid || playback.GenericCutsceneSnapshot.valid == 0) {
        input.SourceStatus = Oot3d_TitleIntroOpeningFrameRuntimeStepNotValidStatus();
        input.LightModeSourceStatus = input.SourceStatus;
        return input;
    }

    const auto& environment = playback.GenericCutsceneSnapshot.environment;
    input.TimeResolved = environment.timeResolved != 0;
    input.DayTime = environment.dayTime;
    input.SkyboxTime = environment.skyboxTime;
    input.TimeStartFrame = environment.timeStartFrame;
    input.SourceKind = Oot3d_TitleIntroOpeningFrameRuntimeTimeSourceKind(
        &playback.OpeningFrameRuntimeState, &playback.OpeningFrameRuntimeStep);
    input.SourceStatus = Oot3d_TitleIntroOpeningFrameRuntimeTimeSourceStatus(
        &playback.OpeningFrameRuntimeState, &playback.OpeningFrameRuntimeStep);

    input.LightModeResolved = environment.lightModeResolved != 0;
    input.LightModeCurrent = environment.lightModeCurrent;
    input.LightModeTarget = environment.lightModeTarget;
    input.LightModeBlendActive = environment.lightModeBlendActive != 0;
    input.LightModeBlendRemaining = environment.lightModeBlendRemaining;
    input.LightModeBlendDuration = environment.lightModeBlendDuration;
    input.LightModeBlendWeight = environment.lightModeBlendWeight;
    input.LightModeStartFrame = environment.lightModeStartFrame;
    input.LightModeSourceActionId = environment.lightModeSourceActionId;
    input.LightModeSourceKind =
        Oot3d_TitleIntroOpeningFrameRuntimeLightModeSourceKind(&playback.OpeningFrameRuntimeState);
    input.LightModeSourceStatus =
        Oot3d_TitleIntroOpeningFrameRuntimeLightModeSourceStatus(&playback.OpeningFrameRuntimeState);

    input.LightSettingResolved = environment.lightSettingResolved != 0;
    input.LightSettingRawIndex = environment.lightSettingRawIndex;
    input.LightSettingTarget = environment.lightSettingTarget;
    input.LightSettingSetupIndex = environment.lightSettingSetupIndex != UINT16_MAX
                                       ? environment.lightSettingSetupIndex
                                       : -1;
    input.LightSettingStartFrame = environment.lightSettingStartFrame;
    input.LightSettingPlayTargetOffset = environment.lightSettingPlayTargetOffset;
    input.LightSettingPlayBlendWeightOffset = environment.lightSettingPlayBlendWeightOffset;
    input.LightSettingScenePath = environment.lightSettingScenePath != nullptr
                                      ? environment.lightSettingScenePath
                                      : "";
    input.LightSettingSourceKind =
        Oot3d_TitleIntroOpeningFrameRuntimeLightSettingSourceKind(&playback.OpeningFrameRuntimeState);
    input.LightSettingSourceStatus =
        Oot3d_TitleIntroOpeningFrameRuntimeLightSettingSourceStatus(&playback.OpeningFrameRuntimeState);

    input.ColorAddendsResolved = environment.colorAddendsResolved != 0;
    for (size_t i = 0; i < input.AmbientColorAddends.size(); ++i) {
        input.AmbientColorAddends[i] = environment.ambientColorAddends[i];
        input.LightColorAddends[i] = environment.lightColorAddends[i];
        input.FogColorAddends[i] = environment.fogColorAddends[i];
    }
    input.ColorAddendRampActive = environment.colorAddendRampActive != 0;
    input.ColorAddendSourceActionId = input.ColorAddendRampActive
                                         ? environment.colorAddendSourceActionId
                                         : -1;
    input.ColorAddendSourceKind =
        Oot3d_TitleIntroOpeningFrameRuntimeColorAddendSourceKind(&playback.OpeningFrameRuntimeState);
    input.ColorAddendSourceStatus =
        Oot3d_TitleIntroOpeningFrameRuntimeColorAddendSourceStatus(&playback.OpeningFrameRuntimeState);
    return input;
}
} // namespace

ThreeDsRecomp::Oot3d::Oot3dNativeRuntimeEnvironmentInput CutsceneFrameRuntimeEnvironmentInput(
    const Oot3dSceneCutsceneFrameSnapshot& snapshot) {
    ThreeDsRecomp::Oot3d::Oot3dNativeRuntimeEnvironmentInput input;
    if (snapshot.valid == 0) {
        input.SourceStatus = "scene_cutscene_frame_snapshot_not_valid";
        input.LightModeSourceStatus = input.SourceStatus;
        return input;
    }
    const auto& environment = snapshot.environment;
    input.TimeResolved = environment.timeResolved != 0;
    input.DayTime = environment.dayTime;
    input.SkyboxTime = environment.skyboxTime;
    input.TimeStartFrame = environment.timeStartFrame;
    input.SourceKind = "scene_cutscene_frame_snapshot_environment";
    input.SourceStatus = input.TimeResolved ? "resolved" : "time_not_resolved";
    input.LightModeResolved = environment.lightModeResolved != 0;
    input.LightModeCurrent = environment.lightModeCurrent;
    input.LightModeTarget = environment.lightModeTarget;
    input.LightModeBlendActive = environment.lightModeBlendActive != 0;
    input.LightModeBlendRemaining = environment.lightModeBlendRemaining;
    input.LightModeBlendDuration = environment.lightModeBlendDuration;
    input.LightModeBlendWeight = environment.lightModeBlendWeight;
    input.LightModeStartFrame = environment.lightModeStartFrame;
    input.LightModeSourceActionId = environment.lightModeSourceActionId;
    input.LightModeSourceKind = "scene_cutscene_frame_snapshot_environment";
    input.LightModeSourceStatus = input.LightModeResolved ? "resolved" : "light_mode_not_resolved";
    input.LightSettingResolved = environment.lightSettingResolved != 0;
    input.LightSettingRawIndex = environment.lightSettingRawIndex;
    input.LightSettingTarget = environment.lightSettingTarget;
    input.LightSettingSetupIndex = environment.lightSettingSetupIndex != UINT16_MAX
                                       ? environment.lightSettingSetupIndex
                                       : -1;
    input.LightSettingStartFrame = environment.lightSettingStartFrame;
    input.LightSettingPlayTargetOffset = environment.lightSettingPlayTargetOffset;
    input.LightSettingPlayBlendWeightOffset = environment.lightSettingPlayBlendWeightOffset;
    input.LightSettingScenePath = environment.lightSettingScenePath != nullptr
                                      ? environment.lightSettingScenePath
                                      : "";
    input.LightSettingSourceKind = "scene_cutscene_frame_snapshot_environment";
    input.LightSettingSourceStatus = input.LightSettingResolved ? "resolved" : "light_setting_not_resolved";
    input.ColorAddendsResolved = environment.colorAddendsResolved != 0;
    for (size_t i = 0; i < input.AmbientColorAddends.size(); ++i) {
        input.AmbientColorAddends[i] = environment.ambientColorAddends[i];
        input.LightColorAddends[i] = environment.lightColorAddends[i];
        input.FogColorAddends[i] = environment.fogColorAddends[i];
    }
    input.ColorAddendRampActive = environment.colorAddendRampActive != 0;
    input.ColorAddendSourceActionId = input.ColorAddendRampActive
                                         ? environment.colorAddendSourceActionId
                                         : -1;
    input.ColorAddendSourceKind = "scene_cutscene_frame_snapshot_environment";
    input.ColorAddendSourceStatus = input.ColorAddendsResolved ? "resolved" : "color_addends_not_resolved";
    return input;
}

ThreeDsRecomp::Oot3d::Oot3dNativeRuntimeEnvironmentInput TitleIntroRuntimeEnvironmentInput(
    const TitleIntroPlayback& playback) {
    if (playback.GenericCutsceneRuntimeRequested) {
        return GenericCutsceneRuntimeEnvironmentInput(playback);
    }
    ThreeDsRecomp::Oot3d::Oot3dNativeRuntimeEnvironmentInput input;
    if (!playback.OpeningFrameRuntimeStepValid) {
        input.SourceStatus = Oot3d_TitleIntroOpeningFrameRuntimeStepNotValidStatus();
        input.LightModeSourceStatus = Oot3d_TitleIntroOpeningFrameRuntimeStepNotValidStatus();
        return input;
    }
    if (playback.OpeningFrameRuntimeState.timeResolved == 0) {
        input.SourceStatus =
            Oot3d_TitleIntroOpeningFrameRuntimeTimeSourceStatus(
                &playback.OpeningFrameRuntimeState,
                &playback.OpeningFrameRuntimeStep);
    } else {
        input.TimeResolved = true;
        input.DayTime = playback.OpeningFrameRuntimeState.dayTime;
        input.SkyboxTime = playback.OpeningFrameRuntimeState.skyboxTime;
        input.TimeStartFrame = playback.OpeningFrameRuntimeState.timeStartFrame;
        input.SourceKind =
            Oot3d_TitleIntroOpeningFrameRuntimeTimeSourceKind(
                &playback.OpeningFrameRuntimeState,
                &playback.OpeningFrameRuntimeStep);
        input.SourceStatus =
            Oot3d_TitleIntroOpeningFrameRuntimeTimeSourceStatus(
                &playback.OpeningFrameRuntimeState,
                &playback.OpeningFrameRuntimeStep);
    }
    if (playback.OpeningFrameRuntimeState.lightModeResolved != 0) {
        input.LightModeResolved = true;
        input.LightModeCurrent = playback.OpeningFrameRuntimeState.lightModeCurrent;
        input.LightModeTarget = playback.OpeningFrameRuntimeState.lightModeTarget;
        input.LightModeBlendActive = playback.OpeningFrameRuntimeState.lightModeBlendActive != 0;
        input.LightModeBlendRemaining = playback.OpeningFrameRuntimeState.lightModeBlendRemaining;
        input.LightModeBlendDuration = playback.OpeningFrameRuntimeState.lightModeBlendDuration;
        input.LightModeBlendWeight = playback.OpeningFrameRuntimeState.lightModeBlendWeight;
        input.LightModeStartFrame = playback.OpeningFrameRuntimeState.lightModeStartFrame;
        input.LightModeSourceActionId = playback.OpeningFrameRuntimeState.lightModeSourceActionId;
        input.LightModeSourceKind =
            Oot3d_TitleIntroOpeningFrameRuntimeLightModeSourceKind(
                &playback.OpeningFrameRuntimeState);
        input.LightModeSourceStatus =
            Oot3d_TitleIntroOpeningFrameRuntimeLightModeSourceStatus(
                &playback.OpeningFrameRuntimeState);
    } else {
        input.LightModeSourceStatus =
            Oot3d_TitleIntroOpeningFrameRuntimeLightModeSourceStatus(
                &playback.OpeningFrameRuntimeState);
    }
    if (playback.OpeningFrameRuntimeState.environmentLightSettingResolved != 0) {
        input.LightSettingResolved = true;
        input.LightSettingRawIndex =
            playback.OpeningFrameRuntimeState.environmentLightSettingRawIndex;
        input.LightSettingTarget =
            playback.OpeningFrameRuntimeState.environmentLightSettingTarget;
        input.LightSettingSetupIndex =
            playback.OpeningFrameRuntimeState.environmentLightSettingRow != nullptr
                ? playback.OpeningFrameRuntimeState.environmentLightSettingRow->setupIndex
                : -1;
        input.LightSettingStartFrame =
            playback.OpeningFrameRuntimeState.environmentLightSettingStartFrame;
        input.LightSettingPlayTargetOffset =
            playback.OpeningFrameRuntimeState.environmentLightSettingPlayTargetOffset;
        input.LightSettingPlayBlendWeightOffset =
            playback.OpeningFrameRuntimeState.environmentLightSettingPlayBlendWeightOffset;
        input.LightSettingScenePath =
            playback.OpeningFrameRuntimeState.environmentLightSettingRow != nullptr &&
                    playback.OpeningFrameRuntimeState.environmentLightSettingRow->scenePath != nullptr
                ? playback.OpeningFrameRuntimeState.environmentLightSettingRow->scenePath
                : "";
        input.LightSettingSourceKind =
            Oot3d_TitleIntroOpeningFrameRuntimeLightSettingSourceKind(
                &playback.OpeningFrameRuntimeState);
        input.LightSettingSourceStatus =
            Oot3d_TitleIntroOpeningFrameRuntimeLightSettingSourceStatus(
                &playback.OpeningFrameRuntimeState);
    }
    if (playback.OpeningFrameRuntimeState.colorAddendsResolved != 0) {
        input.ColorAddendsResolved = true;
        for (size_t i = 0; i < input.AmbientColorAddends.size(); ++i) {
            input.AmbientColorAddends[i] =
                playback.OpeningFrameRuntimeState.ambientColorAddends[i];
            input.LightColorAddends[i] =
                playback.OpeningFrameRuntimeState.lightColorAddends[i];
            input.FogColorAddends[i] =
                playback.OpeningFrameRuntimeState.fogColorAddends[i];
        }
        input.ColorAddendRampActive =
            playback.OpeningFrameRuntimeState.colorAddendRampActive != 0;
        input.ColorAddendSourceActionId =
            input.ColorAddendRampActive
                ? playback.OpeningFrameRuntimeState.colorAddendSourceActionId
                : -1;
        input.ColorAddendSourceKind =
            Oot3d_TitleIntroOpeningFrameRuntimeColorAddendSourceKind(
                &playback.OpeningFrameRuntimeState);
        input.ColorAddendSourceStatus =
            Oot3d_TitleIntroOpeningFrameRuntimeColorAddendSourceStatus(
                &playback.OpeningFrameRuntimeState);
    }
    return input;
}

const ThreeDsRecomp::Oot3d::Oot3dNativeRuntimeEnvironmentInput* RuntimeEnvironmentInputPtr(
    const ThreeDsRecomp::Oot3d::Oot3dNativeRuntimeEnvironmentInput& input) {
    return input.TimeResolved || input.LightModeResolved || input.LightSettingResolved ||
                   input.ColorAddendsResolved
               ? &input
               : nullptr;
}

bool TitleIntroRuntimeEnvironmentNeedsRenderRebuild(
    const TitleIntroPlayback& playback,
    const ThreeDsRecomp::Oot3d::Oot3dNativeRuntimeEnvironmentInput& input,
    float materialAnimationFrame) {
    return playback.RuntimeEnvironmentTimeUsedForRender != input.TimeResolved ||
           playback.RuntimeEnvironmentLightModeUsedForRender != input.LightModeResolved ||
           playback.RuntimeEnvironmentLightSettingUsedForRender != input.LightSettingResolved ||
           playback.RuntimeEnvironmentColorAddendsUsedForRender != input.ColorAddendsResolved ||
           !playback.RuntimeEnvironmentMaterialAnimationFrameUsedForRender ||
           (input.TimeResolved &&
            (playback.RuntimeEnvironmentRenderedDayTime != input.DayTime ||
             playback.RuntimeEnvironmentRenderedSkyboxTime != input.SkyboxTime ||
             playback.RuntimeEnvironmentRenderedTimeStartFrame != input.TimeStartFrame)) ||
           (input.LightModeResolved &&
            (playback.RuntimeEnvironmentRenderedLightModeCurrent != input.LightModeCurrent ||
             playback.RuntimeEnvironmentRenderedLightModeTarget != input.LightModeTarget ||
             playback.RuntimeEnvironmentRenderedLightModeBlendActive != input.LightModeBlendActive ||
             std::fabs(playback.RuntimeEnvironmentRenderedLightModeBlendWeight -
                       input.LightModeBlendWeight) > 0.0001 ||
             playback.RuntimeEnvironmentRenderedLightModeBlendRemaining !=
                 input.LightModeBlendRemaining ||
             playback.RuntimeEnvironmentRenderedLightModeBlendDuration !=
                 input.LightModeBlendDuration ||
             playback.RuntimeEnvironmentRenderedLightModeStartFrame != input.LightModeStartFrame)) ||
           (input.LightSettingResolved &&
            (playback.RuntimeEnvironmentRenderedLightSettingRawIndex != input.LightSettingRawIndex ||
             playback.RuntimeEnvironmentRenderedLightSettingTarget != input.LightSettingTarget ||
             playback.RuntimeEnvironmentRenderedLightSettingSetupIndex != input.LightSettingSetupIndex ||
             playback.RuntimeEnvironmentRenderedLightSettingStartFrame != input.LightSettingStartFrame)) ||
           (input.ColorAddendsResolved &&
            (playback.RuntimeEnvironmentRenderedAmbientColorAddends != input.AmbientColorAddends ||
             playback.RuntimeEnvironmentRenderedLightColorAddends != input.LightColorAddends ||
             playback.RuntimeEnvironmentRenderedFogColorAddends != input.FogColorAddends ||
             playback.RuntimeEnvironmentRenderedColorAddendSourceActionId !=
                 input.ColorAddendSourceActionId ||
             playback.RuntimeEnvironmentRenderedColorAddendRampActive !=
                 input.ColorAddendRampActive));
}

void MarkTitleIntroRuntimeEnvironmentRendered(
    TitleIntroPlayback& playback,
    const ThreeDsRecomp::Oot3d::Oot3dNativeRuntimeEnvironmentInput& input,
    float materialAnimationFrame,
    std::string materialAnimationFrameSource,
    bool rebuilt) {
    playback.RuntimeEnvironmentRenderRebuilt = rebuilt;
    playback.RuntimeEnvironmentMaterialAnimationFrameUsedForRender = true;
    playback.RuntimeEnvironmentRenderedMaterialAnimationFrame = materialAnimationFrame;
    playback.RuntimeEnvironmentMaterialAnimationFrameSource = std::move(materialAnimationFrameSource);
    playback.RuntimeEnvironmentTimeUsedForRender = false;
    playback.RuntimeEnvironmentLightModeUsedForRender = false;
    playback.RuntimeEnvironmentLightSettingUsedForRender = false;
    playback.RuntimeEnvironmentColorAddendsUsedForRender = false;
    if (input.TimeResolved) {
        playback.RuntimeEnvironmentTimeUsedForRender = true;
        playback.RuntimeEnvironmentRenderedDayTime = input.DayTime;
        playback.RuntimeEnvironmentRenderedSkyboxTime = input.SkyboxTime;
        playback.RuntimeEnvironmentRenderedTimeStartFrame = input.TimeStartFrame;
    }
    if (input.LightModeResolved) {
        playback.RuntimeEnvironmentLightModeUsedForRender = true;
        playback.RuntimeEnvironmentRenderedLightModeCurrent = input.LightModeCurrent;
        playback.RuntimeEnvironmentRenderedLightModeTarget = input.LightModeTarget;
        playback.RuntimeEnvironmentRenderedLightModeBlendActive = input.LightModeBlendActive;
        playback.RuntimeEnvironmentRenderedLightModeBlendWeight = input.LightModeBlendWeight;
        playback.RuntimeEnvironmentRenderedLightModeBlendRemaining = input.LightModeBlendRemaining;
        playback.RuntimeEnvironmentRenderedLightModeBlendDuration = input.LightModeBlendDuration;
        playback.RuntimeEnvironmentRenderedLightModeStartFrame = input.LightModeStartFrame;
    }
    if (input.LightSettingResolved) {
        playback.RuntimeEnvironmentLightSettingUsedForRender = true;
        playback.RuntimeEnvironmentRenderedLightSettingRawIndex = input.LightSettingRawIndex;
        playback.RuntimeEnvironmentRenderedLightSettingTarget = input.LightSettingTarget;
        playback.RuntimeEnvironmentRenderedLightSettingSetupIndex = input.LightSettingSetupIndex;
        playback.RuntimeEnvironmentRenderedLightSettingStartFrame = input.LightSettingStartFrame;
    }
    if (input.ColorAddendsResolved) {
        playback.RuntimeEnvironmentColorAddendsUsedForRender = true;
        playback.RuntimeEnvironmentRenderedAmbientColorAddends = input.AmbientColorAddends;
        playback.RuntimeEnvironmentRenderedLightColorAddends = input.LightColorAddends;
        playback.RuntimeEnvironmentRenderedFogColorAddends = input.FogColorAddends;
        playback.RuntimeEnvironmentRenderedColorAddendSourceActionId =
            input.ColorAddendSourceActionId;
        playback.RuntimeEnvironmentRenderedColorAddendRampActive = input.ColorAddendRampActive;
    }
}
