#pragma once

#include <cstdint>

extern "C" {
#include "oot3d/scene_cutscene_frame_runtime.h"
#include "oot3d/scene_cutscene_intro_runtime.h"
}

constexpr uint16_t kDefaultIntroCutsceneSourceIndex = 27;
constexpr const char* kOot3dIntroCutsceneRuntimeBasis =
    "decomp_support z_scene_cutscene_intro_runtime.c consumes OOT3D QDB cutscene, camera blob CMAD curves, STRT labels, CS_CMD_MISC, CS_CMD_SETTIME, and CS_CMD_SET_PLAYER_ACTION rows decoded from native ZSI sources";

struct IntroCutscenePlayback {
    bool Enabled = false;
    bool Initialized = false;
    bool CameraApplied = false;
    uint16_t CutsceneSourceIndex = kDefaultIntroCutsceneSourceIndex;
    uint32_t StepCount = 0;
    double StartFrame = 0.0;
    double FramesPerSecond = 30.0;
    bool GenericCutsceneRuntimeRequested = false;
    int32_t GenericSampledFrame = 0;
    Oot3dSceneCutsceneFrameKey GenericKey{};
    Oot3dSceneCutsceneFrameRuntimeStatus GenericProgramStatus =
        OOT3D_SCENE_CUTSCENE_FRAME_RUNTIME_UNINITIALIZED;
    Oot3dSceneCutsceneFrameRuntimeStatus GenericInitStatus =
        OOT3D_SCENE_CUTSCENE_FRAME_RUNTIME_UNINITIALIZED;
    Oot3dSceneCutsceneFrameRuntimeStatus GenericStepStatus =
        OOT3D_SCENE_CUTSCENE_FRAME_RUNTIME_UNINITIALIZED;
    Oot3dSceneCutsceneProgramSnapshot GenericProgram{};
    Oot3dSceneCutsceneFrameRuntimeState GenericState{};
    Oot3dSceneCutsceneFrameSnapshot GenericSnapshot{};
    Oot3dCutsceneIntroRuntimeStatus InitStatus = OOT3D_CUTSCENE_INTRO_RUNTIME_OK;
    Oot3dCutsceneIntroRuntimeStatus LastStatus = OOT3D_CUTSCENE_INTRO_RUNTIME_OK;
    Oot3dCutsceneIntroRuntimeState State{};
    Oot3dCutsceneIntroRuntimeStep LastStep{};
};
