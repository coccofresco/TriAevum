#pragma once

#include <filesystem>
#include <string_view>

#include "oot3d_demo_host_types.h"
#include "oot3d_title_intro_runtime_types.h"
#include "three_ds_recomp/oot3d/Oot3dNativeDemoScene.h"

std::filesystem::path ResolveTitleIntroRomfsRoot(
    const Args& args,
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene);

TitleIntroPlayback InitialTitleIntroPlayback(
    const Args& args,
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene);

int ActiveSceneSetupOverrideForArgs(const Args& args);

std::string_view ActiveSceneSetupSourceForArgs(const Args& args);

TitleIntroPlayback InitializeTitleIntroPlaybackFromNativeSources(
    bool enabled,
    bool qdbIndexOverride,
    uint16_t qdbIndexOverrideValue,
    double startFrame,
    const std::filesystem::path& romfsRoot,
    double fallbackCmbToSceneScale,
    const Oot3dSceneCutsceneProgramSnapshot* cutsceneProgram = nullptr);
