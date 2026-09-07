#include "oot3d_intro_cutscene_status_names.h"

const char* IntroCutsceneRuntimeStatusName(Oot3dCutsceneIntroRuntimeStatus status) {
    switch (status) {
        case OOT3D_CUTSCENE_INTRO_RUNTIME_OK:
            return "ok";
        case OOT3D_CUTSCENE_INTRO_RUNTIME_NULL_STATE:
            return "null_state";
        case OOT3D_CUTSCENE_INTRO_RUNTIME_NULL_OUTPUT:
            return "null_output";
        case OOT3D_CUTSCENE_INTRO_RUNTIME_UNKNOWN_CUTSCENE:
            return "unknown_cutscene";
        case OOT3D_CUTSCENE_INTRO_RUNTIME_UNINITIALIZED:
            return "uninitialized";
        case OOT3D_CUTSCENE_INTRO_RUNTIME_COMPLETE:
            return "complete";
    }
    return "unknown";
}

const char* IntroCutsceneCameraStatusName(Oot3dCutsceneIntroCameraStatus status) {
    switch (status) {
        case OOT3D_CUTSCENE_INTRO_CAMERA_OK:
            return "ok";
        case OOT3D_CUTSCENE_INTRO_CAMERA_NULL_VIEW:
            return "null_view";
        case OOT3D_CUTSCENE_INTRO_CAMERA_NO_ACTIVE_TIMELINE_ROW:
            return "no_active_timeline_row";
        case OOT3D_CUTSCENE_INTRO_CAMERA_RUNTIME_FAILED:
            return "runtime_failed";
    }
    return "unknown";
}
