#include "oot3d_title_intro_status_names.h"

const char* TitleIntroSceneCameraStatusName(Oot3dCutsceneIntroCameraStatus status) {
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

const char* TitleIntroOpeningFrameRuntimeStatusName(Oot3dTitleIntroOpeningFrameRuntimeStatus status) {
    switch (status) {
        case OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_OK:
            return "ok";
        case OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_NULL_STATE:
            return "null_state";
        case OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_NULL_OUTPUT:
            return "null_output";
        case OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_UNINITIALIZED:
            return "uninitialized";
        case OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_MISSING_ORCHESTRATION:
            return "missing_orchestration";
        case OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_CAMERA_ERROR:
            return "camera_error";
        case OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_ACTOR_ERROR:
            return "actor_error";
        case OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_CUTSCENE_ERROR:
            return "cutscene_error";
        case OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_LOGO_ERROR:
            return "logo_error";
        case OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_COMPLETE:
            return "complete";
    }
    return "unknown";
}

const char* TitleIntroPlaybackStatusName(Oot3dTitleIntroPlaybackStatus status) {
    switch (status) {
        case OOT3D_TITLE_INTRO_PLAYBACK_OK:
            return "ok";
        case OOT3D_TITLE_INTRO_PLAYBACK_NULL_STATE:
            return "null_state";
        case OOT3D_TITLE_INTRO_PLAYBACK_NULL_TABLE:
            return "null_table";
        case OOT3D_TITLE_INTRO_PLAYBACK_NULL_OUTPUT:
            return "null_output";
        case OOT3D_TITLE_INTRO_PLAYBACK_INDEX_OUT_OF_RANGE:
            return "index_out_of_range";
        case OOT3D_TITLE_INTRO_PLAYBACK_NULL_PAYLOAD:
            return "null_payload";
        case OOT3D_TITLE_INTRO_PLAYBACK_NULL_RECORD:
            return "null_record";
        case OOT3D_TITLE_INTRO_PLAYBACK_UNINITIALIZED:
            return "uninitialized";
    }
    return "unknown";
}

const char* TitleIntroRecordCStatusName(Oot3dTitleIntroRecordCStatus status) {
    switch (status) {
        case OOT3D_TITLE_INTRO_RECORD_C_OK:
            return "ok";
        case OOT3D_TITLE_INTRO_RECORD_C_NULL_CONTEXT:
            return "null_context";
        case OOT3D_TITLE_INTRO_RECORD_C_NULL_TABLE:
            return "null_table";
        case OOT3D_TITLE_INTRO_RECORD_C_NULL_OUTPUT:
            return "null_output";
    }
    return "unknown";
}

const char* TitleIntroSourceSelectorStatusName(Oot3dTitleIntroSourceSelectorStatus status) {
    switch (status) {
        case OOT3D_TITLE_INTRO_SOURCE_SELECTOR_OK:
            return "ok";
        case OOT3D_TITLE_INTRO_SOURCE_SELECTOR_NULL_STATE:
            return "null_state";
        case OOT3D_TITLE_INTRO_SOURCE_SELECTOR_NULL_RECORD_CONTEXT:
            return "null_record_context";
    }
    return "unknown";
}

const char* TitleIntroSourceSelectorFeedStatusName(Oot3dTitleIntroSourceSelectorFeedStatus status) {
    switch (status) {
        case OOT3D_TITLE_INTRO_SOURCE_SELECTOR_FEED_OK:
            return "ok";
        case OOT3D_TITLE_INTRO_SOURCE_SELECTOR_FEED_NULL_STATE:
            return "null_state";
        case OOT3D_TITLE_INTRO_SOURCE_SELECTOR_FEED_NULL_SELECTOR:
            return "null_selector";
        case OOT3D_TITLE_INTRO_SOURCE_SELECTOR_FEED_NULL_RECORD_CONTEXT:
            return "null_record_context";
        case OOT3D_TITLE_INTRO_SOURCE_SELECTOR_FEED_NULL_RECORD_TABLE:
            return "null_record_table";
        case OOT3D_TITLE_INTRO_SOURCE_SELECTOR_FEED_NULL_SEQUENCE_SOURCE:
            return "null_sequence_source";
        case OOT3D_TITLE_INTRO_SOURCE_SELECTOR_FEED_NULL_CAPTURE_INPUT:
            return "null_capture_input";
        case OOT3D_TITLE_INTRO_SOURCE_SELECTOR_FEED_SELECTOR_APPLY_FAILED:
            return "selector_apply_failed";
    }
    return "unknown";
}

const char* TitleIntroOpeningCameraRuntimeStatusName(Oot3dTitleIntroOpeningCameraRuntimeStatus status) {
    switch (status) {
        case OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_OK:
            return "ok";
        case OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_NULL_OUTPUT:
            return "null_output";
        case OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_MISSING_ORCHESTRATION:
            return "missing_orchestration";
        case OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_NO_ACTIVE_SEGMENT:
            return "no_active_segment";
        case OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_APPLY_FAILED:
            return "apply_failed";
        case OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_PROJECT_FAILED:
            return "project_failed";
    }
    return "unknown";
}

const char* TitleIntroOpeningActorSampleStatusName(Oot3dTitleIntroOpeningActorSampleStatus status) {
    switch (status) {
        case OOT3D_TITLE_INTRO_OPENING_ACTOR_SAMPLE_OK:
            return "ok";
        case OOT3D_TITLE_INTRO_OPENING_ACTOR_SAMPLE_NULL_OUTPUT:
            return "null_output";
        case OOT3D_TITLE_INTRO_OPENING_ACTOR_SAMPLE_NO_ACTIVE_CUE:
            return "no_active_cue";
        case OOT3D_TITLE_INTRO_OPENING_ACTOR_SAMPLE_MOTION_ERROR:
            return "motion_error";
    }
    return "unknown";
}

const char* TitleIntroOpeningLogoRuntimeStatusName(Oot3dTitleIntroOpeningLogoRuntimeStatus status) {
    switch (status) {
        case OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_OK:
            return "ok";
        case OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_NULL_OUTPUT:
            return "null_output";
        case OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_NULL_STATE:
            return "null_state";
        case OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_MISSING_ORCHESTRATION:
            return "missing_orchestration";
        case OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_INDEX_OUT_OF_RANGE:
            return "index_out_of_range";
        case OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_NO_MATCHING_STEP:
            return "no_matching_step";
    }
    return "unknown";
}
