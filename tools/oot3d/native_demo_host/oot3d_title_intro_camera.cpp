#include "oot3d_title_intro_camera.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "oot3d_demo_math.h"
#include "oot3d_title_intro_status_names.h"

extern "C" {
#include "oot3d/scene_cutscene_camera_runtime.h"
#include "oot3d/title_intro_opening_camera_runtime.h"
#include "oot3d/title_intro_opening_resolver_runtime.h"
}

namespace {

void ApplyTitleIntroCameraView(Camera& camera,
                               const Oot3dCutsceneCameraViewFrame& view,
                               const char* nativeBehavior) {
    camera.Position = ToVec3(view.eye);
    camera.Target = ToVec3(view.at);
    camera.Up = NativeCutsceneCameraUpForDemoView(ToVec3(view.up));
    if (Dot(camera.Up, camera.Up) <= 0.000001) {
        camera.Up = { 0.0, 1.0, 0.0 };
    }
    if (std::isfinite(view.fov) && view.fov > 0.0f) {
        camera.FovDegrees = view.fov;
    }
    LookAt(camera, camera.Target);
    camera.NativeCameraActive = false;
    camera.NativeBehavior = nativeBehavior;
    camera.NativeSet = kNativeCameraSetNone;
    camera.NativeMode = kNativeCameraModeNormal;
    camera.NativeFunction = kNativeCameraSetNone;
    camera.NativeSceneCameraSetting = kNativeCameraSetNone;
    camera.NativeSceneCameraApplied = true;
}

} // namespace

bool ApplyTitleIntroNativeCamera(TitleIntroPlayback& playback, double timelineFrame, Camera& camera) {
    playback.NativeCameraDecoded = false;
    playback.NativeCameraApplied = false;
    playback.CameraBlobSegmentSourceIndex = kOot3dInvalidSourceIndex;
    playback.CameraCommandRow = nullptr;
    playback.CameraBlobRow = nullptr;
    playback.CameraSegmentRow = nullptr;

    if (playback.OpeningOrchestrationRow == nullptr ||
        playback.OpeningOrchestrationIndex == OOT3D_TITLE_INTRO_OPENING_NO_ORCHESTRATION_INDEX) {
        playback.NativeCameraStatus = Oot3d_TitleIntroOpeningCameraRuntimeMissingOrchestrationStatus();
        return false;
    }

    Oot3dTitleIntroOpeningCameraSample sample{};
    const auto status = Oot3d_TitleIntroOpeningCameraRuntimeSample(
        playback.OpeningOrchestrationIndex,
        static_cast<float>(timelineFrame),
        nullptr,
        &sample);
    playback.NativeCameraDecoded = true;
    playback.CameraBlobRow = sample.cameraBlob;
    playback.CameraSegmentRow = sample.cameraSegment;
    playback.CameraBlobSegmentSourceIndex = sample.cameraBlobSegmentSourceIndex;
    if (status != OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_OK) {
        playback.NativeCameraStatus =
            std::string("opening_camera_runtime_") + TitleIntroOpeningCameraRuntimeStatusName(status);
        return false;
    }

    ApplyTitleIntroCameraView(camera, sample.view, Oot3d_TitleIntroOpeningCameraRuntimeBlobBehavior());
    playback.NativeCameraApplied = true;
    playback.NativeCameraStatus = Oot3d_TitleIntroOpeningCameraRuntimeAppliedStatus();
    return true;
}

bool ApplyTitleIntroInitialSceneCamera(TitleIntroPlayback& playback, double sceneFrame, Camera& camera) {
    playback.InitialSceneCameraDecoded = false;
    playback.InitialSceneCameraApplied = false;
    playback.InitialSceneCameraRow = nullptr;
    playback.InitialSceneCutsceneFrame = 0.0;
    playback.QdbCameraSuppressedByInitialSceneCamera = false;

    if (playback.OpeningOrchestrationRow == nullptr) {
        playback.InitialSceneCameraStatus =
            Oot3d_TitleIntroOpeningCameraRuntimeMissingInitialSceneOrchestrationStatus();
        return false;
    }
    if (playback.InitialSceneCutsceneRow == nullptr) {
        playback.InitialSceneCameraStatus =
            Oot3d_TitleIntroOpeningCameraRuntimeMissingInitialSceneCutsceneStatus();
        return false;
    }
    if (playback.InitialSceneCutsceneRow->cutsceneSourceIndex !=
            playback.OpeningOrchestrationRow->cutsceneSourceIndex ||
        playback.InitialSceneCutsceneRow->setupIndex != playback.OpeningOrchestrationRow->setupIndex ||
        Oot3d_TitleIntroOpeningResolverScenePathMatches(playback.InitialSceneCutsceneRow->scenePath,
                                                        playback.OpeningOrchestrationRow->scenePath) == 0) {
        playback.InitialSceneCameraStatus =
            Oot3d_TitleIntroOpeningCameraRuntimeInitialSceneCutsceneMismatchStatus();
        return false;
    }
    if (sceneFrame > static_cast<double>(playback.InitialSceneCutsceneRow->cameraFrameEnd)) {
        playback.InitialSceneCameraStatus =
            Oot3d_TitleIntroOpeningCameraRuntimeInitialSceneWindowCompleteStatus();
        return false;
    }

    const double clampedFrame = std::clamp(
        sceneFrame,
        static_cast<double>(playback.InitialSceneCutsceneRow->cameraFrameStart + 1),
        static_cast<double>(std::max(playback.InitialSceneCutsceneRow->cameraFrameStart + 1,
                                     playback.InitialSceneCutsceneRow->cameraFrameEnd - 1)));
    playback.InitialSceneCutsceneFrame = clampedFrame;
    playback.InitialSceneCameraRow = Oot3d_CutsceneIntroCameraFindTimelineRow(
        playback.InitialSceneCutsceneRow->cutsceneSourceIndex,
        static_cast<int32_t>(clampedFrame));
    if (playback.InitialSceneCameraRow == nullptr) {
        playback.InitialSceneCameraStatus =
            Oot3d_TitleIntroOpeningCameraRuntimeInitialSceneNoActiveRowStatus();
        return false;
    }

    Oot3dCutsceneCameraViewFrame view{};
    const auto status = Oot3d_CutsceneIntroCameraBuildView(
        playback.InitialSceneCutsceneRow->cutsceneSourceIndex,
        static_cast<float>(clampedFrame),
        nullptr,
        &view);
    playback.InitialSceneCameraDecoded = true;
    if (status != OOT3D_CUTSCENE_INTRO_CAMERA_OK) {
        playback.InitialSceneCameraStatus =
            std::string("runtime_") + TitleIntroSceneCameraStatusName(status);
        return false;
    }

    ApplyTitleIntroCameraView(camera, view, Oot3d_TitleIntroOpeningCameraRuntimeInitialSceneBehavior());
    playback.InitialSceneCameraApplied = true;
    playback.QdbCameraSuppressedByInitialSceneCamera = true;
    playback.InitialSceneCameraStatus =
        Oot3d_TitleIntroOpeningCameraRuntimeInitialSceneAppliedStatus();
    return true;
}

bool ApplyTitleIntroOpeningFrameRuntimeCamera(TitleIntroPlayback& playback, Camera& camera) {
    playback.OpeningFrameRuntimeCameraUsedForRender = false;
    const bool useGenericSnapshot = playback.GenericCutsceneRuntimeRequested;
    const auto& genericCamera = playback.GenericCutsceneSnapshot.camera;
    if (!playback.OpeningFrameRuntimeStepValid ||
        (useGenericSnapshot
             ? (playback.GenericCutsceneSnapshot.valid == 0 || genericCamera.valid == 0 || genericCamera.active == 0)
             : playback.OpeningFrameRuntimeStep.cameraViewAvailable == 0)) {
        return false;
    }

    const auto& view = useGenericSnapshot ? genericCamera.view : playback.OpeningFrameRuntimeStep.camera.view;
    ApplyTitleIntroCameraView(camera, view, Oot3d_TitleIntroOpeningCameraRuntimeOpeningFrameBehavior());

    playback.InitialSceneCameraDecoded = true;
    playback.InitialSceneCameraApplied = true;
    playback.QdbCameraSuppressedByInitialSceneCamera = true;
    playback.InitialSceneCutsceneFrame = static_cast<double>(playback.OpeningFrameRuntimeStep.frame);
    playback.InitialSceneCameraRow = Oot3d_CutsceneIntroCameraFindTimelineRow(
        playback.InitialSceneCutsceneSourceIndex,
        playback.OpeningFrameRuntimeStep.frame);
    playback.InitialSceneCameraStatus = Oot3d_TitleIntroOpeningCameraRuntimeOpeningFrameAppliedStatus();
    playback.CameraCommandRow = nullptr;
    playback.CameraBlobRow =
        useGenericSnapshot ? genericCamera.cameraBlob : playback.OpeningFrameRuntimeStep.camera.cameraBlob;
    playback.CameraSegmentRow =
        useGenericSnapshot ? genericCamera.cameraSegment : playback.OpeningFrameRuntimeStep.camera.cameraSegment;
    playback.CameraBlobSegmentSourceIndex = useGenericSnapshot
                                                ? genericCamera.cameraBlobSegmentSourceIndex
                                                : playback.OpeningFrameRuntimeStep.camera.cameraBlobSegmentSourceIndex;
    playback.NativeCameraDecoded = playback.CameraBlobRow != nullptr && playback.CameraSegmentRow != nullptr;
    playback.NativeCameraApplied = false;
    playback.NativeCameraStatus = Oot3d_TitleIntroOpeningCameraRuntimeSupersededByOpeningFrameStatus();
    playback.OpeningFrameRuntimeCameraUsedForRender = true;
    return true;
}
