#include "oot3d_intro_cutscene_playback.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include "oot3d_demo_math.h"
#include "oot3d_intro_cutscene_status_names.h"
#include "oot3d_title_intro_diagnostics.h"

IntroCutscenePlayback InitialIntroCutscenePlayback(const Args& args) {
    IntroCutscenePlayback playback;
    playback.Enabled = args.IntroCutscenePlayback;
    playback.CutsceneSourceIndex = args.IntroCutsceneSourceIndex;
    playback.StartFrame = args.CutscenePlayerRequest ? args.CutscenePlayerFrame : 0.0;
    if (!playback.Enabled) {
        return playback;
    }

    playback.GenericCutsceneRuntimeRequested =
        args.CutscenePlayerRequest &&
        args.CutscenePlayerAdapterKind == OOT3D_SCENE_CUTSCENE_FRAME_ADAPTER_SCENE_CUTSCENE;
    if (playback.GenericCutsceneRuntimeRequested) {
        playback.GenericKey.sceneId = static_cast<uint8_t>(args.CutscenePlayerSceneId);
        playback.GenericKey.setupIndex = args.CutscenePlayerSetupIndex;
        playback.GenericKey.cutsceneSourceIndex = args.CutscenePlayerSourceIndex;
        playback.GenericProgramStatus = Oot3d_SceneCutsceneFrameRuntimeBuildProgram(
            playback.GenericKey, &playback.GenericProgram);
        playback.GenericInitStatus = playback.GenericProgramStatus == OOT3D_SCENE_CUTSCENE_FRAME_RUNTIME_OK
                                         ? Oot3d_SceneCutsceneFrameRuntimeInit(
                                               &playback.GenericState, playback.GenericKey)
                                         : playback.GenericProgramStatus;
        playback.Initialized =
            playback.GenericInitStatus == OOT3D_SCENE_CUTSCENE_FRAME_RUNTIME_OK;
        playback.InitStatus = playback.GenericState.sceneCutsceneInitStatus;
        playback.State = playback.GenericState.sceneCutsceneState;
    } else {
        playback.InitStatus =
            Oot3d_CutsceneIntroRuntimeInit(&playback.State, playback.CutsceneSourceIndex);
        playback.Initialized = playback.InitStatus == OOT3D_CUTSCENE_INTRO_RUNTIME_OK;
    }
    if (!playback.Initialized) {
        throw std::runtime_error(
            "failed to initialize OOT3D intro cutscene runtime for source index " +
            std::to_string(playback.CutsceneSourceIndex) + ": " +
            IntroCutsceneRuntimeStatusName(playback.InitStatus));
    }
    return playback;
}

bool ApplyCutsceneFrameSnapshotToCamera(
    const Oot3dSceneCutsceneFrameSnapshot& snapshot, Camera& camera) {
    if (snapshot.valid == 0 || snapshot.camera.valid == 0 || snapshot.camera.active == 0) {
        return false;
    }
    camera.Position = ToVec3(snapshot.camera.view.eye);
    camera.Target = ToVec3(snapshot.camera.view.at);
    camera.Up = NativeCutsceneCameraUpForDemoView(ToVec3(snapshot.camera.view.up));
    if (Dot(camera.Up, camera.Up) <= 0.000001) {
        camera.Up = { 0.0, 1.0, 0.0 };
    }
    if (std::isfinite(snapshot.camera.view.fov) && snapshot.camera.view.fov > 0.0f) {
        camera.FovDegrees = snapshot.camera.view.fov;
    }
    LookAt(camera, camera.Target);
    camera.NativeCameraActive = false;
    camera.NativeBehavior = "scene_cutscene_frame_snapshot_camera";
    camera.NativeSet = kNativeCameraSetNone;
    camera.NativeMode = kNativeCameraModeNormal;
    camera.NativeFunction = kNativeCameraSetNone;
    camera.NativeSceneCameraApplied = true;
    return true;
}

bool ApplyIntroCutsceneStepToCamera(const Oot3dCutsceneIntroRuntimeStep& step, Camera& camera) {
    if (step.cameraStatus != OOT3D_CUTSCENE_INTRO_CAMERA_OK) {
        return false;
    }

    camera.Position = ToVec3(step.view.eye);
    camera.Target = ToVec3(step.view.at);
    camera.Up = NativeCutsceneCameraUpForDemoView(ToVec3(step.view.up));
    if (Dot(camera.Up, camera.Up) <= 0.000001) {
        camera.Up = { 0.0, 1.0, 0.0 };
    }
    if (std::isfinite(step.view.fov) && step.view.fov > 0.0f) {
        camera.FovDegrees = step.view.fov;
    }
    LookAt(camera, camera.Target);
    camera.NativeCameraActive = false;
    camera.NativeBehavior = "intro_cutscene_runtime_camera";
    camera.NativeSet = kNativeCameraSetNone;
    camera.NativeMode = kNativeCameraModeNormal;
    camera.NativeFunction = kNativeCameraSetNone;
    camera.NativeSceneCameraDataIndex = step.requestCameraDataIndex0 != 0 ? 0 : camera.NativeSceneCameraDataIndex;
    camera.NativeSceneCameraSetting = kNativeCameraSetNone;
    camera.NativeSceneCameraApplied = true;
    return true;
}

void StepIntroCutscenePlayback(IntroCutscenePlayback& playback, Camera& camera, int32_t targetFrame) {
    if (!playback.Enabled || !playback.Initialized) {
        return;
    }

    if (playback.GenericCutsceneRuntimeRequested) {
        const int32_t clampedTarget = std::clamp(
            targetFrame >= 0 ? targetFrame : playback.GenericSampledFrame + 1,
            1,
            std::max(1, playback.GenericState.nativeEndFrame));
        if (clampedTarget < playback.GenericSampledFrame) {
            playback.GenericInitStatus = Oot3d_SceneCutsceneFrameRuntimeInit(
                &playback.GenericState, playback.GenericKey);
            playback.GenericSampledFrame = 0;
        }
        while (playback.GenericSampledFrame < clampedTarget) {
            playback.GenericStepStatus = Oot3d_SceneCutsceneFrameRuntimeStep(
                &playback.GenericState, nullptr, &playback.GenericSnapshot);
            if (playback.GenericStepStatus != OOT3D_SCENE_CUTSCENE_FRAME_RUNTIME_OK &&
                playback.GenericStepStatus != OOT3D_SCENE_CUTSCENE_FRAME_RUNTIME_COMPLETE) {
                break;
            }
            playback.GenericSampledFrame = playback.GenericSnapshot.frame;
            ++playback.StepCount;
        }
        playback.State = playback.GenericState.sceneCutsceneState;
        playback.LastStep = playback.GenericSnapshot.sceneCutsceneStep;
        playback.LastStatus = playback.GenericState.sceneCutsceneStepStatus;
        playback.CameraApplied = ApplyCutsceneFrameSnapshotToCamera(
            playback.GenericSnapshot, camera);
        return;
    }

    playback.LastStatus =
        Oot3d_CutsceneIntroRuntimeStep(&playback.State, nullptr, &playback.LastStep);
    if (playback.LastStatus == OOT3D_CUTSCENE_INTRO_RUNTIME_OK ||
        playback.LastStatus == OOT3D_CUTSCENE_INTRO_RUNTIME_COMPLETE) {
        ++playback.StepCount;
    }
    playback.CameraApplied = ApplyIntroCutsceneStepToCamera(playback.LastStep, camera);
}
