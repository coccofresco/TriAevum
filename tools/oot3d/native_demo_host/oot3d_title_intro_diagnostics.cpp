#include "oot3d_title_intro_diagnostics.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>

#include "oot3d_title_intro_opening_frame_runtime.h"
#include "oot3d_title_intro_render_binding_diagnostics.h"
#include "oot3d_title_intro_status_names.h"

extern "C" {
#include "oot3d/scene_cutscene_camera_blob_table.h"
#include "oot3d/scene_cutscene_intro_runtime.h"
#include "oot3d/title_intro_opening_frame_runtime.h"
#include "oot3d/title_intro_source_table.h"
}

namespace {

struct TitleIntroDiagnosticVec3 {
    double X = 0.0;
    double Y = 0.0;
    double Z = 0.0;
};

TitleIntroDiagnosticVec3 ToVec3(const Oot3dCutsceneCameraVec3f& value) {
    return { value.x, value.y, value.z };
}

nlohmann::json Vec3ToJson(const TitleIntroDiagnosticVec3& value) {
    return { { "x", value.X }, { "y", value.Y }, { "z", value.Z } };
}

nlohmann::json DemoVec3ToJson(const ThreeDsRecomp::Oot3d::Oot3dDemoVec3& value) {
    return { { "x", value.X }, { "y", value.Y }, { "z", value.Z } };
}

nlohmann::json GenericCutsceneActorSnapshotsToJson(const TitleIntroPlayback& playback) {
    nlohmann::json actors = nlohmann::json::array();
    for (uint16_t i = 0; i < playback.GenericCutsceneSnapshot.actorCount; ++i) {
        const auto& actor = playback.GenericCutsceneSnapshot.actors[i];
        actors.push_back({
            { "binding_index", actor.actorBindingIndex },
            { "actor_kind", actor.actorKind },
            { "actor_role", actor.actorRole != nullptr ? actor.actorRole : "" },
            { "archive_path", actor.archivePath != nullptr ? actor.archivePath : "" },
            { "cmb_name", actor.cmbName != nullptr ? actor.cmbName : "" },
            { "base_csab_name", actor.baseCsabName != nullptr ? actor.baseCsabName : "" },
            { "resource_valid", actor.resourceValid != 0 },
            { "animation_valid", actor.animationValid != 0 },
            { "animation_kind", actor.animationKind },
            { "animation_index", actor.animationIndex },
            { "animation_csab_name",
              actor.animationCsabName != nullptr ? actor.animationCsabName : "" },
            { "animation_role", actor.animationRole != nullptr ? actor.animationRole : "" },
            { "animation_play_speed_scale", actor.animationPlaySpeedScale },
            { "animation_native_play_speed", actor.animationNativePlaySpeed },
            { "animation_frame_delta", actor.animationFrameDelta },
            { "native_speed", actor.nativeSpeed },
            { "animation_frame_valid", actor.animationFrameValid != 0 },
            { "animation_frame", actor.animationFrame },
            { "animation_change_mode", actor.animationChangeMode },
            { "animation_update_mode", actor.animationUpdateMode },
            { "animation_completion_transitioned",
              actor.animationCompletionTransitioned != 0 },
            { "animation_completion_looped", actor.animationCompletionLooped != 0 },
            { "animation_morph_active", actor.animationMorphActive != 0 },
            { "animation_morph_source_index", actor.animationMorphSourceIndex },
            { "animation_morph_source_frame", actor.animationMorphSourceFrame },
            { "animation_morph_weight", actor.animationMorphWeight },
            { "animation_morph_rate", actor.animationMorphRate },
            { "animation_morph_frames", actor.animationMorphFrames },
            { "animation_morph_source_csab_name",
              actor.animationMorphSourceCsabName != nullptr
                  ? actor.animationMorphSourceCsabName
                  : "" },
            { "animation_morph_source",
              actor.animationMorphSource != nullptr ? actor.animationMorphSource : "" },
            { "animation_source", actor.animationSource != nullptr ? actor.animationSource : "" },
        });
    }
    return actors;
}

nlohmann::json PairedMountAnimationToJson(
    const Oot3dTitleIntroOpeningPairedMountAnimationSample& animation) {
    return {
        { "valid", animation.valid != 0 },
        { "frame_valid", animation.frameValid != 0 },
        { "completion_transitioned", animation.completionTransitioned != 0 },
        { "completion_looped", animation.completionLooped != 0 },
        { "action_id", animation.actionId },
        { "animation_index", animation.animationIndex },
        { "change_mode", animation.changeMode },
        { "update_mode", animation.updateMode },
        { "frame", animation.frame },
        { "play_speed", animation.playSpeed },
        { "csab_name", animation.csabName != nullptr ? animation.csabName : "" },
        { "morph_active", animation.morphActive != 0 },
        { "morph_source_animation_index", animation.morphSourceAnimationIndex },
        { "morph_source_frame", animation.morphSourceFrame },
        { "morph_weight", animation.morphWeight },
        { "morph_rate", animation.morphRate },
        { "morph_frames", animation.morphFrames },
        { "morph_source_csab_name",
          animation.morphSourceCsabName != nullptr ? animation.morphSourceCsabName : "" },
        { "morph_source", animation.morphSource != nullptr ? animation.morphSource : "" },
        { "motion_role", animation.motionRole != nullptr ? animation.motionRole : "" },
        { "source", animation.source != nullptr ? animation.source : "" },
    };
}

} // namespace


nlohmann::json IntroCutsceneNativeRowToJson(const Oot3dSceneCutsceneNativeSourceRow* row) {
    if (row == nullptr) {
        return nullptr;
    }
    return {
        { "cutscene_source_index", row->cutsceneSourceIndex },
        { "scene_id", row->sceneId },
        { "setup_index", row->setupIndex },
        { "native_decoded", row->nativeDecoded != 0 },
        { "native_header_offset", row->nativeHeaderOffset },
        { "native_command_count", row->nativeCommandCount },
        { "native_end_frame", row->nativeEndFrame },
        { "scene_path", row->scenePath },
        { "setup_symbol", row->setupSymbol },
        { "payload_symbol", row->payloadSymbol },
    };
}

nlohmann::json IntroCutsceneCameraRowToJson(const Oot3dSceneCutsceneIntroCameraTimelineRow* row) {
    if (row == nullptr) {
        return nullptr;
    }
    return {
        { "timeline_source_index", row->introCameraTimelineSourceIndex },
        { "cutscene_source_index", row->cutsceneSourceIndex },
        { "native_command_source_index", row->nativeCommandSourceIndex },
        { "camera_blob_source_index", row->cameraBlobSourceIndex },
        { "camera_blob_segment_source_index", row->cameraBlobSegmentSourceIndex },
        { "start_frame", row->startFrame },
        { "end_frame", row->endFrame },
        { "sample_frame", row->sampleFrame },
        { "scene_path", row->scenePath },
        { "strt_label", row->strtLabel },
        { "strt_curve_role", row->strtCurveRole },
    };
}

nlohmann::json IntroCutscenePlayerActionRowToJson(const Oot3dSceneCutscenePlayerActionRow* row) {
    if (row == nullptr) {
        return nullptr;
    }
    nlohmann::json rawWords = nlohmann::json::array();
    for (size_t wordIndex = 0; wordIndex < 12; wordIndex++) {
        rawWords.push_back(row->rawWords[wordIndex]);
    }
    return {
        { "player_action_source_index", row->playerActionSourceIndex },
        { "cutscene_source_index", row->cutsceneSourceIndex },
        { "native_command_source_index", row->nativeCommandSourceIndex },
        { "setup_index", row->setupIndex },
        { "local_command_index", row->localCommandIndex },
        { "entry_index", row->entryIndex },
        { "command_offset", row->commandOffset },
        { "entry_offset", row->entryOffset },
        { "action_id", row->actionId },
        { "start_frame", row->startFrame },
        { "end_frame", row->endFrame },
        { "duration_frames", row->durationFrames },
        { "unk_06", row->unk06 },
        { "active_window_start_exclusive_end_inclusive", row->activeWindowStartExclusiveEndInclusive != 0 },
        { "scene_path", row->scenePath },
        { "action_name", row->actionName },
        { "runtime_semantic", row->runtimeSemantic },
        { "raw_words", rawWords },
        { "raw_words_text", row->rawWordsText },
    };
}

nlohmann::json IntroCutsceneDirectPlayerEventRowToJson(const Oot3dSceneCutsceneDirectPlayerEventRow* row) {
    if (row == nullptr) {
        return nullptr;
    }
    nlohmann::json rawWords = nlohmann::json::array();
    for (size_t wordIndex = 0; wordIndex < 4; wordIndex++) {
        rawWords.push_back(row->rawWords[wordIndex]);
    }
    return {
        { "direct_player_event_source_index", row->directPlayerEventSourceIndex },
        { "cutscene_source_index", row->cutsceneSourceIndex },
        { "native_command_source_index", row->nativeCommandSourceIndex },
        { "setup_index", row->setupIndex },
        { "local_command_index", row->localCommandIndex },
        { "command_offset", row->commandOffset },
        { "payload_offset", row->payloadOffset },
        { "action_id", row->actionId },
        { "start_frame", row->startFrame },
        { "unk_0c", row->unk0C },
        { "unk_0e", row->unk0E },
        { "normal_start_frame_gate_confirmed", row->normalStartFrameGateConfirmed != 0 },
        { "dispatch_resolved", row->dispatchResolved != 0 },
        { "transition_request_index", row->transitionRequestIndex },
        { "transition_request_trigger", row->transitionRequestTrigger },
        { "transition_request_effect", row->transitionRequestEffect },
        { "player_runtime_field8_value", row->playerRuntimeField8Value },
        { "player_runtime_field8_value_resolved", row->playerRuntimeField8ValueResolved != 0 },
        { "player_byte_write_offset", row->playerByteWriteOffset },
        { "player_byte_write_value", row->playerByteWriteValue },
        { "player_byte_write_resolved", row->playerByteWriteResolved != 0 },
        { "item_give_id", row->itemGiveId },
        { "item_give_resolved", row->itemGiveResolved != 0 },
        { "scene_path", row->scenePath },
        { "action_name", row->actionName },
        { "runtime_semantic", row->runtimeSemantic },
        { "transition_request_role", row->transitionRequestRole },
        { "player_runtime_field8_expression", row->playerRuntimeField8Expression },
        { "dispatch_evidence", row->dispatchEvidence },
        { "raw_words", rawWords },
        { "raw_words_text", row->rawWordsText },
        { "dynamic_dispatch_resolved", row->dynamicDispatchResolved != 0 },
        { "dynamic_dispatch_variant_ref_start", row->dynamicDispatchVariantRefStart },
        { "dynamic_dispatch_variant_ref_count", row->dynamicDispatchVariantRefCount },
        { "dynamic_dispatch_state_address", row->dynamicDispatchStateAddress },
        { "dynamic_dispatch_initial_state", row->dynamicDispatchInitialState },
        { "dynamic_dispatch_state_expression", row->dynamicDispatchStateExpression },
    };
}

nlohmann::json IntroCutsceneDirectPlayerEventDynamicVariantToJson(
    const Oot3dSceneCutsceneDirectPlayerEventDynamicVariantRow* row
) {
    if (row == nullptr) {
        return nullptr;
    }
    return {
        { "dynamic_dispatch_variant_ref_index", row->dynamicDispatchVariantRefIndex },
        { "direct_player_event_source_index", row->directPlayerEventSourceIndex },
        { "selector_value", row->selectorValue },
        { "selector_incremented", row->selectorIncremented != 0 },
        { "selector_reset_to_zero", row->selectorResetToZero != 0 },
        { "transition_request_index", row->transitionRequestIndex },
        { "transition_request_trigger", row->transitionRequestTrigger },
        { "transition_request_effect", row->transitionRequestEffect },
        { "player_runtime_field8_value", row->playerRuntimeField8Value },
        { "player_runtime_field8_value_resolved", row->playerRuntimeField8ValueResolved != 0 },
        { "player_runtime_field8_expression", row->playerRuntimeField8Expression },
        { "dispatch_evidence", row->dispatchEvidence },
    };
}

const char* Oot3dGlobalEntranceValidationStatusName(Oot3dGlobalEntranceValidationStatus status) {
    switch (status) {
        case OOT3D_GLOBAL_ENTRANCE_OUTSIDE_CODE_BIN_IMAGE:
            return "outside_code_bin_image";
        case OOT3D_GLOBAL_ENTRANCE_OUTSIDE_INFERRED_TABLE_EXTENT:
            return "outside_inferred_table_extent";
        case OOT3D_GLOBAL_ENTRANCE_DECODED_SCENE_ID_UNLABELED:
            return "decoded_scene_id_unlabeled";
        case OOT3D_GLOBAL_ENTRANCE_SECONDARY_LABEL_NO_NATIVE_STEM_MATCH:
            return "secondary_label_no_native_stem_match";
        case OOT3D_GLOBAL_ENTRANCE_NATIVE_SCENE_CANDIDATE_LOCAL_ENTRANCE_UNCOVERED:
            return "native_scene_candidate_local_entrance_uncovered";
        case OOT3D_GLOBAL_ENTRANCE_NATIVE_SCENE_CANDIDATE_LOCAL_ENTRANCE_COVERED:
            return "native_scene_candidate_local_entrance_covered";
        case OOT3D_GLOBAL_ENTRANCE_NATIVE_SCENE_CANDIDATE_SPAWN_RESOLVED:
            return "native_scene_candidate_spawn_resolved";
        case OOT3D_GLOBAL_ENTRANCE_NATIVE_KOKIRI_SLOT5_ENTRYPOINT_CONFIRMED:
            return "native_kokiri_slot5_entrypoint_confirmed";
        default:
            return "unknown";
    }
}

nlohmann::json Oot3dGlobalEntranceRowToJson(const Oot3dGlobalEntranceRow* row) {
    if (row == nullptr) {
        return nullptr;
    }
    return {
        { "entrance_index", row->entranceIndex },
        { "reference_flags", row->referenceFlags },
        { "referenced_by_direct_exit", (row->referenceFlags & OOT3D_GLOBAL_ENTRANCE_REF_DIRECT_EXIT) != 0 },
        { "referenced_by_high_remap_exit", (row->referenceFlags & OOT3D_GLOBAL_ENTRANCE_REF_HIGH_REMAP_EXIT) != 0 },
        { "referenced_by_kokiri_slot5_fixture", (row->referenceFlags & OOT3D_GLOBAL_ENTRANCE_REF_KOKIRI_SLOT5_FIXTURE) != 0 },
        { "referenced_by_cutscene_direct_player_event", (row->referenceFlags & OOT3D_GLOBAL_ENTRANCE_REF_CUTSCENE_DIRECT_PLAYER_EVENT) != 0 },
        { "scene_id", row->sceneId },
        { "local_entrance_index", row->localEntranceIndex },
        { "field", row->field },
        { "secondary_scene_stem", row->secondarySceneStem },
        { "secondary_scene_enum", row->secondarySceneEnum },
        { "native_scene_path_candidate", row->nativeScenePathCandidate },
        { "native_scene_resource_zsi_path", row->nativeSceneResourceZsiPath },
        { "native_scene_source_basename", row->nativeSceneSourceBasename },
        { "native_scene_index_symbol", row->nativeSceneIndexSymbol },
        { "validation_status", Oot3dGlobalEntranceValidationStatusName(row->validationStatus) },
    };
}

const char* Oot3dCutsceneTransitionHandoffStatusName(Oot3dCutsceneTransitionHandoffStatus status) {
    switch (status) {
        case OOT3D_CUTSCENE_TRANSITION_HANDOFF_UNRESOLVED_GLOBAL_ENTRANCE:
            return "unresolved_global_entrance";
        case OOT3D_CUTSCENE_TRANSITION_HANDOFF_NO_NATIVE_SCENE_MATCHES:
            return "no_native_scene_matches";
        case OOT3D_CUTSCENE_TRANSITION_HANDOFF_NO_NATIVE_CUTSCENE_CANDIDATES:
            return "no_native_cutscene_candidates";
        case OOT3D_CUTSCENE_TRANSITION_HANDOFF_UNIQUE_NATIVE_CUTSCENE_CANDIDATE:
            return "unique_native_cutscene_candidate";
        case OOT3D_CUTSCENE_TRANSITION_HANDOFF_REQUIRES_SCENE_LAYER_OFFSET:
            return "requires_scene_layer_offset";
        case OOT3D_CUTSCENE_TRANSITION_HANDOFF_AMBIGUOUS_NATIVE_CUTSCENE_CANDIDATES:
            return "ambiguous_native_cutscene_candidates";
        default:
            return "unknown";
    }
}

nlohmann::json Oot3dCutsceneTransitionEffectiveEntranceRowToJson(
    const Oot3dSceneCutsceneTransitionEffectiveEntranceRow* row
) {
    if (row == nullptr) {
        return nullptr;
    }
    return {
        { "effective_entrance_source_index", row->effectiveEntranceSourceIndex },
        { "handoff_source_index", row->handoffSourceIndex },
        { "direct_player_event_source_index", row->directPlayerEventSourceIndex },
        { "transition_request_index", row->transitionRequestIndex },
        { "scene_layer_offset", row->sceneLayerOffset },
        { "effective_entrance_index", row->effectiveEntranceIndex },
        { "decoded_from_code_bin", row->decodedFromCodeBin != 0 },
        { "scene_id", row->sceneId },
        { "local_entrance_index", row->localEntranceIndex },
        { "field", row->field },
        { "raw_hex", row->rawHex },
        { "native_scene_path_candidate", row->nativeScenePathCandidate },
        { "native_scene_index_symbol", row->nativeSceneIndexSymbol },
        { "native_match_count", row->nativeMatchCount },
        { "cutscene_candidate_count", row->cutsceneCandidateCount },
        { "decode_status", row->decodeStatus },
    };
}

nlohmann::json Oot3dCutsceneTransitionHandoffCandidateRowToJson(
    const Oot3dSceneCutsceneTransitionHandoffCandidateRow* row
) {
    if (row == nullptr) {
        return nullptr;
    }
    return {
        { "candidate_source_index", row->candidateSourceIndex },
        { "handoff_source_index", row->handoffSourceIndex },
        { "direct_player_event_source_index", row->directPlayerEventSourceIndex },
        { "transition_request_index", row->transitionRequestIndex },
        { "scene_id", row->sceneId },
        { "local_entrance_index", row->localEntranceIndex },
        { "field", row->field },
        { "scene_path", row->scenePath },
        { "setup_index", row->setupIndex },
        { "cutscene_source_index", row->cutsceneSourceIndex },
        { "has_cutscene_source",
          row->cutsceneSourceIndex != OOT3D_SCENE_CUTSCENE_TRANSITION_HANDOFF_NO_CUTSCENE_SOURCE_INDEX },
        { "native_end_frame", row->nativeEndFrame },
        { "native_command_count", row->nativeCommandCount },
        { "spawn_resolved", row->spawnResolved != 0 },
        { "entrance_spawn", row->entranceSpawn },
        { "entrance_room", row->entranceRoom },
        { "spawn_actor_name", row->spawnActorName },
        { "spawn_pos", { row->spawnPos[0], row->spawnPos[1], row->spawnPos[2] } },
        { "spawn_rot", { row->spawnRot[0], row->spawnRot[1], row->spawnRot[2] } },
        { "spawn_params", row->spawnParams },
    };
}

nlohmann::json Oot3dCutsceneTransitionHandoffRowToJson(
    const Oot3dSceneCutsceneTransitionHandoffRow* row
) {
    if (row == nullptr) {
        return nullptr;
    }

    nlohmann::json candidates = nlohmann::json::array();
    const uint32_t firstIndex = row->candidateFirstIndex;
    const uint32_t endIndex = firstIndex + row->candidateCount;
    for (uint32_t candidateIndex = firstIndex;
         candidateIndex < endIndex &&
         candidateIndex < oot3d_scene_cutscene_transition_handoff_candidate_row_count;
         candidateIndex++) {
        candidates.push_back(
            Oot3dCutsceneTransitionHandoffCandidateRowToJson(
                &oot3d_scene_cutscene_transition_handoff_candidate_rows[candidateIndex]
            )
        );
    }

    nlohmann::json effectiveEntrances = nlohmann::json::array();
    const uint32_t firstEffectiveIndex = row->effectiveEntranceFirstIndex;
    const uint32_t endEffectiveIndex = firstEffectiveIndex + row->effectiveEntranceCount;
    for (uint32_t effectiveIndex = firstEffectiveIndex;
         effectiveIndex < endEffectiveIndex &&
         effectiveIndex < oot3d_scene_cutscene_transition_effective_entrance_row_count;
         effectiveIndex++) {
        effectiveEntrances.push_back(
            Oot3dCutsceneTransitionEffectiveEntranceRowToJson(
                &oot3d_scene_cutscene_transition_effective_entrance_rows[effectiveIndex]
            )
        );
    }

    return {
        { "handoff_source_index", row->handoffSourceIndex },
        { "direct_player_event_source_index", row->directPlayerEventSourceIndex },
        { "source_cutscene_source_index", row->sourceCutsceneSourceIndex },
        { "source_scene_path", row->sourceScenePath },
        { "source_setup_index", row->sourceSetupIndex },
        { "action_id", row->actionId },
        { "start_frame", row->startFrame },
        { "transition_request_index", row->transitionRequestIndex },
        { "transition_request_trigger", row->transitionRequestTrigger },
        { "transition_request_effect", row->transitionRequestEffect },
        { "global_entrance_resolved", row->globalEntranceResolved != 0 },
        { "scene_id", row->sceneId },
        { "local_entrance_index", row->localEntranceIndex },
        { "field", row->field },
        { "native_scene_path_candidate", row->nativeScenePathCandidate },
        { "native_scene_index_symbol", row->nativeSceneIndexSymbol },
        { "native_match_count", row->nativeMatchCount },
        { "candidate_first_index", row->candidateFirstIndex },
        { "candidate_count", row->candidateCount },
        { "cutscene_candidate_count", row->cutsceneCandidateCount },
        { "requires_scene_layer_offset", row->requiresSceneLayerOffset != 0 },
        { "effective_entrance_first_index", row->effectiveEntranceFirstIndex },
        { "effective_entrance_count", row->effectiveEntranceCount },
        { "effective_entrance_expression", Oot3d_CutsceneTransitionHandoffEffectiveEntranceExpression() },
        { "global_entrance_index_source_address",
          OOT3D_SCENE_CUTSCENE_TRANSITION_HANDOFF_GLOBAL_ENTRANCE_INDEX_SOURCE_ADDRESS },
        { "global_entrance_table_address",
          OOT3D_SCENE_CUTSCENE_TRANSITION_HANDOFF_GLOBAL_ENTRANCE_TABLE_ADDRESS },
        { "scene_layer_offset_context_address",
          OOT3D_SCENE_CUTSCENE_TRANSITION_HANDOFF_SCENE_LAYER_OFFSET_CONTEXT_ADDRESS },
        { "scene_layer_offset_context_field_offset",
          OOT3D_SCENE_CUTSCENE_TRANSITION_HANDOFF_SCENE_LAYER_OFFSET_CONTEXT_FIELD_OFFSET },
        { "scene_layer_offset_source_address",
          OOT3D_SCENE_CUTSCENE_TRANSITION_HANDOFF_SCENE_LAYER_OFFSET_SOURCE_ADDRESS },
        { "status", Oot3dCutsceneTransitionHandoffStatusName(row->status) },
        { "effective_entrances", effectiveEntrances },
        { "candidates", candidates },
    };
}

nlohmann::json IntroCutsceneStepToJson(const Oot3dCutsceneIntroRuntimeStep& step) {
    return {
        { "frame", step.frame },
        { "native_cutscene", IntroCutsceneNativeRowToJson(step.nativeCutscene) },
        { "camera_row", IntroCutsceneCameraRowToJson(step.cameraRow) },
        { "active_player_action", IntroCutscenePlayerActionRowToJson(step.activePlayerAction) },
        { "start_player_action", IntroCutscenePlayerActionRowToJson(step.startPlayerAction) },
        { "start_direct_player_event", IntroCutsceneDirectPlayerEventRowToJson(step.startDirectPlayerEvent) },
        { "start_direct_player_event_dynamic_variant",
          IntroCutsceneDirectPlayerEventDynamicVariantToJson(step.startDirectPlayerEventDynamicVariant) },
        { "camera_status", IntroCutsceneCameraStatusName(step.cameraStatus) },
        { "view",
          { { "eye", Vec3ToJson(ToVec3(step.view.eye)) },
            { "at", Vec3ToJson(ToVec3(step.view.at)) },
            { "up", Vec3ToJson(ToVec3(step.view.up)) },
            { "fov", step.view.fov },
            { "view_dist_d0", step.view.viewDistD0 },
            { "roll", step.view.roll },
            { "has_perturbation", step.view.hasPerturbation != 0 },
            { "perturbation_magnitude", step.view.perturbationMagnitude } } },
        { "active_player_action_count", step.activePlayerActionCount },
        { "start_trigger_player_action_count", step.startTriggerPlayerActionCount },
        { "start_direct_player_event_count", step.startDirectPlayerEventCount },
        { "start_lighting_count", step.startLightingCount },
        { "active_misc_action_count", step.activeMiscActionCount },
        { "start_trigger_action_count", step.startTriggerActionCount },
        { "first_frame_reset", step.firstFrameReset != 0 },
        { "request_cutscene_end", step.requestCutsceneEnd != 0 },
        { "request_camera_data_index_0", step.requestCameraDataIndex0 != 0 },
        { "transition_counter_small_ramp_active", step.transitionCounterSmallRampActive != 0 },
        { "environment_flag_3271_to_10", step.environmentFlag3271To10 != 0 },
        { "environment_flag_3_set", step.environmentFlag3Set != 0 },
        { "environment_flag_4_set", step.environmentFlag4Set != 0 },
        { "environment_light_setting_applied", step.environmentLightSettingApplied != 0 },
        { "environment_light_setting_resolved", step.environmentLightSettingResolved != 0 },
        { "environment_light_setting_raw_index", step.environmentLightSettingRawIndex },
        { "environment_light_setting_target", step.environmentLightSettingTarget },
        { "environment_light_setting_start_frame", step.environmentLightSettingStartFrame },
        { "environment_light_setting_play_target_offset", step.environmentLightSettingPlayTargetOffset },
        { "environment_light_setting_play_blend_weight_offset",
          step.environmentLightSettingPlayBlendWeightOffset },
        { "environment_light_setting_row", SceneCutsceneLightingRowToJson(step.startLighting) },
        { "transition_counter_timed_sfx_active", step.transitionCounterTimedSfxActive != 0 },
        { "camera_quake_start_requested", step.cameraQuakeStartRequested != 0 },
        { "camera_quake_stop_requested", step.cameraQuakeStopRequested != 0 },
        { "direct_player_event_dispatch_applied", step.directPlayerEventDispatchApplied != 0 },
        { "direct_player_event_dynamic_variant_applied", step.directPlayerEventDynamicVariantApplied != 0 },
        { "direct_player_event_dynamic_state_before", step.directPlayerEventDynamicStateBefore },
        { "direct_player_event_dynamic_state_after", step.directPlayerEventDynamicStateAfter },
        { "direct_player_event_dynamic_state_address", step.directPlayerEventDynamicStateAddress },
        { "request_direct_player_event_end_state", step.requestDirectPlayerEventEndState != 0 },
        { "transition_request_applied", step.transitionRequestApplied != 0 },
        { "transition_request_index", step.transitionRequestIndex },
        { "transition_request_trigger", step.transitionRequestTrigger },
        { "transition_request_effect", step.transitionRequestEffect },
        { "transition_request_entrance_resolved", step.transitionRequestEntranceResolved != 0 },
        { "transition_request_entrance", Oot3dGlobalEntranceRowToJson(step.transitionRequestEntrance) },
        { "transition_request_handoff_resolved", step.transitionRequestHandoffResolved != 0 },
        { "transition_request_handoff", Oot3dCutsceneTransitionHandoffRowToJson(step.transitionRequestHandoff) },
        { "transition_request_cutscene_index_resolved", step.transitionRequestCutsceneIndexResolved != 0 },
        { "transition_request_cutscene_index_value", step.transitionRequestCutsceneIndexValue },
        { "transition_request_setup_index_resolved", step.transitionRequestSetupIndexResolved != 0 },
        { "transition_request_setup_index", step.transitionRequestSetupIndex },
        { "transition_request_cutscene_resolved", step.transitionRequestCutsceneResolved != 0 },
        { "transition_request_cutscene", IntroCutsceneNativeRowToJson(step.transitionRequestCutscene) },
        { "player_runtime_field8_value", step.playerRuntimeField8Value },
        { "player_runtime_field8_value_resolved", step.playerRuntimeField8ValueResolved != 0 },
        { "player_byte_write_offset", step.playerByteWriteOffset },
        { "player_byte_write_value", step.playerByteWriteValue },
        { "player_byte_write_resolved", step.playerByteWriteResolved != 0 },
        { "item_give_id", step.itemGiveId },
        { "item_give_resolved", step.itemGiveResolved != 0 },
        { "set_time_start_count", step.startSetTimeCount },
        { "set_time_applied", step.setTimeApplied != 0 },
        { "set_time_row", SceneCutsceneSetTimeRowToJson(step.startSetTime) },
        { "time_resolved", step.timeResolved != 0 },
        { "day_time", step.dayTime },
        { "skybox_time", step.skyboxTime },
        { "time_start_frame", step.timeStartFrame },
        { "time_hour", step.timeHour },
        { "time_minute", step.timeMinute },
        { "native_time_progression_applied", step.nativeTimeProgressionApplied != 0 },
        { "native_time_source_function", OOT3D_CUTSCENE_INTRO_NATIVE_ENV_TIME_UPDATE_FUNCTION },
        { "native_time_rate_source_address", OOT3D_CUTSCENE_INTRO_NATIVE_ENV_TIME_RATE_SOURCE_ADDRESS },
        { "native_time_rate_init_function", OOT3D_CUTSCENE_INTRO_NATIVE_ENV_TIME_RATE_INIT_FUNCTION },
        { "native_time_rate", step.nativeTimeRate },
        { "native_time_game_speed", step.nativeTimeGameSpeed },
        { "native_time_base_increment", step.nativeTimeBaseIncrement },
        { "native_time_delta", step.nativeTimeDelta },
        { "native_time_night_flag_before", step.nativeTimeNightFlagBefore != 0 },
        { "native_time_night_flag_after", step.nativeTimeNightFlagAfter != 0 },
        { "native_time_output_mirror_applied", step.nativeTimeOutputMirrorApplied != 0 },
        { "native_time_day_time_before", step.nativeTimeDayTimeBefore },
        { "native_time_day_time_after", step.nativeTimeDayTimeAfter },
        { "native_time_skybox_time_before", step.nativeTimeSkyboxTimeBefore },
        { "native_time_skybox_time_after", step.nativeTimeSkyboxTimeAfter },
        { "light_mode_resolved", step.lightModeResolved != 0 },
        { "light_mode_current", step.lightModeCurrent },
        { "light_mode_target", step.lightModeTarget },
        { "light_mode_blend_active", step.lightModeBlendActive != 0 },
        { "light_mode_blend_start_applied", step.lightModeBlendStartApplied != 0 },
        { "light_mode_blend_remaining", step.lightModeBlendRemaining },
        { "light_mode_blend_duration", step.lightModeBlendDuration },
        { "light_mode_blend_weight", step.lightModeBlendWeight },
        { "light_mode_start_frame", step.lightModeStartFrame },
        { "light_mode_source_action_id", step.lightModeSourceActionId },
        { "light_mode_action", SceneCutsceneMiscActionRowToJson(step.lightModeActionRow) },
        { "color_addends_resolved", step.colorAddendsResolved != 0 },
        { "color_addend_ramp_active", step.colorAddendRampActive != 0 },
        { "color_addend_source_action_id", step.colorAddendSourceActionId },
        { "color_addend_action", SceneCutsceneMiscActionRowToJson(step.colorAddendActionRow) },
        { "ambient_color_addends",
          { step.ambientColorAddends[0], step.ambientColorAddends[1],
            step.ambientColorAddends[2] } },
        { "light_color_addends",
          { step.lightColorAddends[0], step.lightColorAddends[1],
            step.lightColorAddends[2] } },
        { "fog_color_addends",
          { step.fogColorAddends[0], step.fogColorAddends[1],
            step.fogColorAddends[2] } },
    };
}

nlohmann::json IntroCutscenePlaybackToJson(const IntroCutscenePlayback& playback) {
    return {
        { "enabled", playback.Enabled },
        { "initialized", playback.Initialized },
        { "camera_applied", playback.CameraApplied },
        { "cutscene_source_index", playback.CutsceneSourceIndex },
        { "step_count", playback.StepCount },
        { "init_status", IntroCutsceneRuntimeStatusName(playback.InitStatus) },
        { "last_status", IntroCutsceneRuntimeStatusName(playback.LastStatus) },
        { "state",
          { { "frame", playback.State.frame },
            { "native_end_frame", playback.State.nativeEndFrame },
            { "cutscene_end_counter", playback.State.cutsceneEndCounter },
            { "transition_counter_53f0", playback.State.transitionCounter53F0 },
            { "complete", playback.State.complete != 0 },
            { "cutscene_state", playback.State.cutsceneState },
            { "camera_data_index", playback.State.cameraDataIndex },
            { "env_flag_3", playback.State.envFlag3 },
            { "env_flag_4", playback.State.envFlag4 },
            { "env_flag_3271", playback.State.envFlag3271 },
            { "environment_light_setting_resolved", playback.State.environmentLightSettingResolved != 0 },
            { "environment_light_setting_raw_index", playback.State.environmentLightSettingRawIndex },
            { "environment_light_setting_target", playback.State.environmentLightSettingTarget },
            { "environment_light_setting_start_frame", playback.State.environmentLightSettingStartFrame },
            { "environment_light_setting_play_target_offset",
              playback.State.environmentLightSettingPlayTargetOffset },
            { "environment_light_setting_play_blend_weight_offset",
              playback.State.environmentLightSettingPlayBlendWeightOffset },
            { "environment_light_setting_row",
              SceneCutsceneLightingRowToJson(playback.State.environmentLightSettingRow) },
            { "first_frame_reset_applied", playback.State.firstFrameResetApplied != 0 },
            { "direct_player_event_dispatched", playback.State.directPlayerEventDispatched != 0 },
            { "transition_request_pending", playback.State.transitionRequestPending != 0 },
            { "transition_request_index", playback.State.transitionRequestIndex },
            { "transition_request_trigger", playback.State.transitionRequestTrigger },
            { "transition_request_effect", playback.State.transitionRequestEffect },
            { "transition_request_entrance_resolved", playback.State.transitionRequestEntranceResolved != 0 },
            { "transition_request_entrance", Oot3dGlobalEntranceRowToJson(playback.State.transitionRequestEntrance) },
            { "transition_request_handoff_resolved", playback.State.transitionRequestHandoffResolved != 0 },
            { "transition_request_handoff", Oot3dCutsceneTransitionHandoffRowToJson(playback.State.transitionRequestHandoff) },
            { "transition_request_cutscene_index_resolved", playback.State.transitionRequestCutsceneIndexResolved != 0 },
            { "transition_request_cutscene_index_value", playback.State.transitionRequestCutsceneIndexValue },
            { "transition_request_setup_index_resolved", playback.State.transitionRequestSetupIndexResolved != 0 },
            { "transition_request_setup_index", playback.State.transitionRequestSetupIndex },
            { "transition_request_cutscene_resolved", playback.State.transitionRequestCutsceneResolved != 0 },
            { "transition_request_cutscene", IntroCutsceneNativeRowToJson(playback.State.transitionRequestCutscene) },
            { "player_runtime_field8_value", playback.State.playerRuntimeField8Value },
            { "player_runtime_field8_value_resolved", playback.State.playerRuntimeField8ValueResolved != 0 },
            { "player_byte_write_offset", playback.State.playerByteWriteOffset },
            { "player_byte_write_value", playback.State.playerByteWriteValue },
            { "player_byte_write_resolved", playback.State.playerByteWriteResolved != 0 },
            { "item_give_id", playback.State.itemGiveId },
            { "item_give_resolved", playback.State.itemGiveResolved != 0 },
            { "time_resolved", playback.State.timeResolved != 0 },
            { "day_time", playback.State.dayTime },
            { "skybox_time", playback.State.skyboxTime },
            { "time_start_frame", playback.State.timeStartFrame },
            { "time_hour", playback.State.timeHour },
            { "time_minute", playback.State.timeMinute },
            { "native_time_progression_resolved", playback.State.nativeTimeProgressionResolved != 0 },
            { "native_time_source_function", OOT3D_CUTSCENE_INTRO_NATIVE_ENV_TIME_UPDATE_FUNCTION },
            { "native_time_rate_source_address", OOT3D_CUTSCENE_INTRO_NATIVE_ENV_TIME_RATE_SOURCE_ADDRESS },
            { "native_time_rate_init_function", OOT3D_CUTSCENE_INTRO_NATIVE_ENV_TIME_RATE_INIT_FUNCTION },
            { "native_time_rate", playback.State.nativeTimeRate },
            { "native_time_game_speed", playback.State.nativeTimeGameSpeed },
            { "native_time_base_increment", playback.State.nativeTimeBaseIncrement },
            { "native_time_delta", playback.State.nativeTimeDelta },
            { "native_time_night_flag", playback.State.nativeTimeNightFlag != 0 },
            { "native_time_output_mirror_applied", playback.State.nativeTimeOutputMirrorApplied != 0 },
            { "native_time_previous_day_time", playback.State.nativeTimePreviousDayTime },
            { "native_time_previous_skybox_time", playback.State.nativeTimePreviousSkyboxTime },
            { "light_mode_resolved", playback.State.lightModeResolved != 0 },
            { "light_mode_current", playback.State.lightModeCurrent },
            { "light_mode_target", playback.State.lightModeTarget },
            { "light_mode_blend_active", playback.State.lightModeBlendActive != 0 },
            { "light_mode_blend_remaining", playback.State.lightModeBlendRemaining },
            { "light_mode_blend_duration", playback.State.lightModeBlendDuration },
            { "light_mode_start_frame", playback.State.lightModeStartFrame },
            { "light_mode_source_action_id", playback.State.lightModeSourceActionId },
            { "light_mode_action", SceneCutsceneMiscActionRowToJson(playback.State.lightModeActionRow) },
            { "color_addends_resolved", playback.State.colorAddendsResolved != 0 },
            { "color_addend_ramp_active", playback.State.colorAddendRampActive != 0 },
            { "color_addend_source_action_id", playback.State.colorAddendSourceActionId },
            { "color_addend_action",
              SceneCutsceneMiscActionRowToJson(playback.State.colorAddendActionRow) },
            { "ambient_color_addends",
              { playback.State.ambientColorAddends[0], playback.State.ambientColorAddends[1],
                playback.State.ambientColorAddends[2] } },
            { "light_color_addends",
              { playback.State.lightColorAddends[0], playback.State.lightColorAddends[1],
                playback.State.lightColorAddends[2] } },
            { "fog_color_addends",
              { playback.State.fogColorAddends[0], playback.State.fogColorAddends[1],
                playback.State.fogColorAddends[2] } },
            { "set_time_row", SceneCutsceneSetTimeRowToJson(playback.State.setTimeRow) } } },
        { "last_step", IntroCutsceneStepToJson(playback.LastStep) },
        { "basis", kOot3dIntroCutsceneRuntimeBasis },
    };
}


nlohmann::json TitleIntroQdbRowToJson(const Oot3dTitleIntroQdbSourceRow* row) {
    if (row == nullptr) {
        return nullptr;
    }
    return {
        { "qdb_index", row->qdbIndex },
        { "asset_index", row->assetIndex },
        { "embedded_index", row->embeddedIndex },
        { "is_epona_title_demo_candidate", row->isEponaTitleDemoCandidate != 0 },
        { "size", row->size },
        { "command_count", row->commandCount },
        { "end_frame", row->endFrame },
        { "decoded_size", row->decodedSize },
        { "archive_path", row->archivePath },
        { "embedded_name", row->embeddedName },
        { "command_ids", row->commandIds },
    };
}

nlohmann::json TitleIntroCueRowToJson(const Oot3dTitleIntroActorCueRow* row) {
    if (row == nullptr) {
        return nullptr;
    }
    return {
        { "actor_cue_index", row->actorCueIndex },
        { "qdb_index", row->qdbIndex },
        { "qdb_command_index", row->qdbCommandIndex },
        { "cue_command_id", row->cueCommandId },
        { "cue_index", row->cueIndex },
        { "cue_id", row->cueId },
        { "start_frame", row->startFrame },
        { "end_frame", row->endFrame },
        { "rotation_s16", { { "x", row->rotX }, { "y", row->rotY }, { "z", row->rotZ } } },
        { "start_position", { { "x", row->startX }, { "y", row->startY }, { "z", row->startZ } } },
        { "end_position", { { "x", row->endX }, { "y", row->endY }, { "z", row->endZ } } },
        { "normal", { { "x", row->normalX }, { "y", row->normalY }, { "z", row->normalZ } } },
        { "archive_path", row->archivePath },
        { "embedded_name", row->embeddedName },
    };
}

nlohmann::json TitleIntroLinkBoyPlayerActionRowToJson(const Oot3dTitleIntroLinkBoyPlayerActionRow& row);

nlohmann::json TitleIntroLinkBoyPlayerActionRowToJson(const Oot3dTitleIntroLinkBoyPlayerActionRow* row) {
    if (row == nullptr) {
        return nullptr;
    }
    return TitleIntroLinkBoyPlayerActionRowToJson(*row);
}

nlohmann::json TitleIntroPlayerActionTransformToJson(
    const Oot3dTitleIntroPlayerActionTransform& transform) {
    return {
        { "action_id", transform.actionId },
        { "start_frame", transform.startFrame },
        { "end_frame", transform.endFrame },
        { "duration_frames", transform.durationFrames },
        { "rotation_s16",
          { { "x", transform.rotX }, { "y", transform.rotY }, { "z", transform.rotZ } } },
        { "start_position",
          { { "x", transform.startX }, { "y", transform.startY }, { "z", transform.startZ } } },
        { "end_position",
          { { "x", transform.endX }, { "y", transform.endY }, { "z", transform.endZ } } },
        { "tail_word9_f32", transform.tailWord9F32 },
        { "tail_word10_f32", transform.tailWord10F32 },
        { "tail_word11_f32", transform.tailWord11F32 },
    };
}

nlohmann::json TitleIntroOpeningPlayerMotionRowToJson(
    const Oot3dTitleIntroOpeningPlayerMotionRow* row) {
    if (row == nullptr) {
        return nullptr;
    }
    auto rawWords = nlohmann::json::array();
    for (uint32_t i = 0; i < OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_RECORD_WORD_COUNT; ++i) {
        rawWords.push_back(row->rawWords[i]);
    }
    return {
        { "motion_ref_index", row->motionRefIndex },
        { "orchestration_index", row->orchestrationIndex },
        { "player_action_ref_index", row->playerActionRefIndex },
        { "player_action_source_index", row->playerActionSourceIndex },
        { "native_command_source_index", row->nativeCommandSourceIndex },
        { "local_command_index", row->localCommandIndex },
        { "entry_index", row->entryIndex },
        { "action_id", row->actionId },
        { "action_id_hex", row->actionIdHex },
        { "start_frame", row->startFrame },
        { "end_frame", row->endFrame },
        { "duration_frames", row->durationFrames },
        { "direct_state_case_present", row->directStateCasePresent != 0 },
        { "not_direct_state_case", row->notDirectStateCase != 0 },
        { "has_quantized_motion_vector", row->hasQuantizedMotionVector != 0 },
        { "native_heading_s16", row->nativeHeadingS16 },
        { "native_speed", row->clampedSpeed },
        { "raw_words", rawWords },
        { "raw_words_text", row->rawWordsText },
    };
}

nlohmann::json TitleIntroCueSampleToJson(const TitleIntroCueSample& sample) {
    return {
        { "valid", sample.Valid },
        { "active", sample.Active },
        { "frame", sample.Frame },
        { "interpolation", sample.Interpolation },
        { "status", sample.Status },
        { "position", DemoVec3ToJson(sample.Position) },
        { "rotation_s16", DemoVec3ToJson(sample.Rotation) },
        { "row", TitleIntroCueRowToJson(sample.Row) },
        { "link_boy_player_action_row", TitleIntroLinkBoyPlayerActionRowToJson(sample.LinkBoyPlayerActionRow) },
        { "opening_player_motion_row", TitleIntroOpeningPlayerMotionRowToJson(sample.OpeningPlayerMotionRow) },
        { "opening_player_motion_transform",
          sample.HasOpeningPlayerMotionTransform
              ? TitleIntroPlayerActionTransformToJson(sample.OpeningPlayerMotionTransform)
              : nlohmann::json(nullptr) },
    };
}

nlohmann::json TitleIntroCameraCommandToJson(const Oot3dTitleIntroQdbCommandRow* row) {
    if (row == nullptr) {
        return nullptr;
    }
    return {
        { "qdb_command_index", row->qdbCommandIndex },
        { "asset_index", row->assetIndex },
        { "embedded_index", row->embeddedIndex },
        { "local_command_index", row->localCommandIndex },
        { "command_offset", row->commandOffset },
        { "command_id", row->commandId },
        { "total_size", row->totalSize },
        { "archive_path", row->archivePath },
        { "embedded_name", row->embeddedName },
        { "command_name", row->commandName },
        { "category", row->category },
    };
}

nlohmann::json TitleIntroCameraBlobToJson(const Oot3dSceneCutsceneCameraBlobRow* row) {
    if (row == nullptr) {
        return nullptr;
    }
    return {
        { "camera_blob_source_index", row->cameraBlobSourceIndex },
        { "native_command_source_index", row->nativeCommandSourceIndex },
        { "cutscene_source_index", row->cutsceneSourceIndex },
        { "scene_path", row->scenePath },
        { "local_command_index", row->localCommandIndex },
        { "command_offset", row->commandOffset },
        { "command_blob_size", row->commandBlobSize },
        { "segment_ref_start", row->segmentRefStart },
        { "segment_ref_count", row->segmentRefCount },
        { "payload_symbol", row->payloadSymbol },
    };
}

nlohmann::json TitleIntroCameraSegmentToJson(const Oot3dSceneCutsceneCameraBlobSegmentRow* row) {
    if (row == nullptr) {
        return nullptr;
    }
    return {
        { "camera_blob_segment_source_index", row->cameraBlobSegmentSourceIndex },
        { "camera_blob_source_index", row->cameraBlobSourceIndex },
        { "segment_index", row->segmentIndex },
        { "start_frame", row->startFrame },
        { "end_frame", row->endFrame },
        { "mads_type", row->madsType },
        { "mads_offset", row->madsOffset },
        { "cmad_offset", row->cmadOffset },
        { "scene_path", row->scenePath },
    };
}

nlohmann::json TitleIntroActorScaleRowToJson(const Oot3dTitleIntroActorScaleRow* row) {
    if (row == nullptr) {
        return nullptr;
    }
    return {
        { "scale_index", row->scaleIndex },
        { "actor_role", row->actorRole },
        { "actor_name", row->actorName },
        { "archive_role", row->archiveRole },
        { "init_function", row->initFunction },
        { "update_function", row->updateFunction },
        { "draw_function", row->drawFunction },
        { "actor_set_scale_callsite", row->actorSetScaleCallsite },
        { "actor_set_scale_function", row->actorSetScaleFunction },
        { "scale_literal_address", row->scaleLiteralAddress },
        { "actor_scale", row->actorScale },
        { "gravity_literal_address", row->gravityLiteralAddress },
        { "gravity", row->gravity },
        { "shadow_y_offset_literal_address", row->shadowYOffsetLiteralAddress },
        { "shadow_y_offset", row->shadowYOffset },
        { "shadow_scale_literal_address", row->shadowScaleLiteralAddress },
        { "shadow_scale", row->shadowScale },
        { "focus_y_offset_literal_address", row->focusYOffsetLiteralAddress },
        { "focus_y_offset", row->focusYOffset },
        { "oot3d_basis", row->oot3dBasis },
        { "n64_reference", row->n64Reference },
        { "decode_status", row->decodeStatus },
    };
}

nlohmann::json TitleIntroActorAnimationRowToJson(const Oot3dTitleIntroActorAnimationRow* row) {
    if (row == nullptr) {
        return nullptr;
    }
    return {
        { "animation_index", row->animationIndex },
        { "actor_role", row->actorRole },
        { "actor_name", row->actorName },
        { "asset_role", row->assetRole },
        { "archive_path", row->archivePath },
        { "cmb_type_index", row->cmbTypeIndex },
        { "cmb_file_index", row->cmbFileIndex },
        { "cmb_name", row->cmbName },
        { "animation_table_pointer_literal_address", row->animationTablePointerLiteralAddress },
        { "animation_table_outer_runtime_address", row->animationTableOuterRuntimeAddress },
        { "animation_table_outer_index", row->animationTableOuterIndex },
        { "init_animation_table_runtime_address", row->initAnimationTableRuntimeAddress },
        { "init_animation_slot", row->initAnimationSlot },
        { "init_csab_type_index", row->initCsabTypeIndex },
        { "init_csab_file_index", row->initCsabFileIndex },
        { "init_csab_name", row->initCsabName },
        { "title_visual_csab_type_index", row->titleVisualCsabTypeIndex },
        { "title_visual_csab_file_index", row->titleVisualCsabFileIndex },
        { "title_visual_csab_name", row->titleVisualCsabName },
        { "actor_init_function", row->actorInitFunction },
        { "actor_update_function", row->actorUpdateFunction },
        { "actor_draw_function", row->actorDrawFunction },
        { "zar_get_cmb_by_index_callsite", row->zarGetCmbByIndexCallsite },
        { "animation_play_once_callsite", row->animationPlayOnceCallsite },
        { "title_cue_semantic_reference_function", row->titleCueSemanticReferenceFunction },
        { "title_cue_role", row->titleCueRole },
        { "oot3d_basis", row->oot3dBasis },
        { "n64_reference", row->n64Reference },
        { "decode_status", row->decodeStatus },
    };
}

nlohmann::json TitleIntroActorMotionAnimationRowToJson(const Oot3dTitleIntroActorMotionAnimationRow* row) {
    if (row == nullptr) {
        return nullptr;
    }
    return {
        { "motion_index", row->motionIndex },
        { "actor_role", row->actorRole },
        { "actor_name", row->actorName },
        { "asset_role", row->assetRole },
        { "archive_path", row->archivePath },
        { "horse_type_index", row->horseTypeIndex },
        { "action_field_offset", row->actionFieldOffset },
        { "speed_field_offset", row->speedFieldOffset },
        { "animation_index_field_offset", row->animationIndexFieldOffset },
        { "horse_type_field_offset", row->horseTypeFieldOffset },
        { "skel_anime_field_offset", row->skelAnimeFieldOffset },
        { "source_function", row->sourceFunction },
        { "table_pointer_literal_address", row->tablePointerLiteralAddress },
        { "animation_table_outer_runtime_address", row->animationTableOuterRuntimeAddress },
        { "animation_table_runtime_address", row->animationTableRuntimeAddress },
        { "zero_speed_literal_address", row->zeroSpeedLiteralAddress },
        { "zero_speed", row->zeroSpeed },
        { "walk_threshold_literal_address", row->walkThresholdLiteralAddress },
        { "walk_threshold", row->walkThreshold },
        { "fast_threshold_literal_address", row->fastThresholdLiteralAddress },
        { "fast_threshold", row->fastThreshold },
        { "idle_play_speed_scale_literal_address", row->idlePlaySpeedScaleLiteralAddress },
        { "idle_play_speed_scale", row->idlePlaySpeedScale },
        { "walk_play_speed_scale_literal_address", row->walkPlaySpeedScaleLiteralAddress },
        { "walk_play_speed_scale", row->walkPlaySpeedScale },
        { "trot_play_speed_scale_literal_address", row->trotPlaySpeedScaleLiteralAddress },
        { "trot_play_speed_scale", row->trotPlaySpeedScale },
        { "fast_play_speed_scale_literal_address", row->fastPlaySpeedScaleLiteralAddress },
        { "fast_play_speed_scale", row->fastPlaySpeedScale },
        { "audio_id_literal_address", row->audioIdLiteralAddress },
        { "audio_id", row->audioId },
        { "audio_freq_pointer_address", row->audioFreqPointerAddress },
        { "audio_freq_runtime_address", row->audioFreqRuntimeAddress },
        { "audio_vol_pointer_address", row->audioVolPointerAddress },
        { "audio_vol_runtime_address", row->audioVolRuntimeAddress },
        { "animation_change_same_state_callsite", row->animationChangeSameStateCallsite },
        { "animation_change_changed_state_callsite", row->animationChangeChangedStateCallsite },
        { "idle_animation_index", row->idleAnimationIndex },
        { "idle_csab_type_index", row->idleCsabTypeIndex },
        { "idle_csab_file_index", row->idleCsabFileIndex },
        { "idle_csab_name", row->idleCsabName },
        { "walk_animation_index", row->walkAnimationIndex },
        { "walk_csab_type_index", row->walkCsabTypeIndex },
        { "walk_csab_file_index", row->walkCsabFileIndex },
        { "walk_csab_name", row->walkCsabName },
        { "trot_animation_index", row->trotAnimationIndex },
        { "trot_csab_type_index", row->trotCsabTypeIndex },
        { "trot_csab_file_index", row->trotCsabFileIndex },
        { "trot_csab_name", row->trotCsabName },
        { "fast_animation_index", row->fastAnimationIndex },
        { "fast_csab_type_index", row->fastCsabTypeIndex },
        { "fast_csab_file_index", row->fastCsabFileIndex },
        { "fast_csab_name", row->fastCsabName },
        { "oot3d_basis", row->oot3dBasis },
        { "n64_reference", row->n64Reference },
        { "decode_status", row->decodeStatus },
    };
}

nlohmann::json TitleIntroHorseStateRouteRowToJson(const Oot3dTitleIntroHorseStateRouteRow& row) {
    return {
        { "state_route_index", row.stateRouteIndex },
        { "route_role", row.routeRole },
        { "actor_role", row.actorRole },
        { "actor_name", row.actorName },
        { "asset_role", row.assetRole },
        { "archive_path", row.archivePath },
        { "source_function", row.sourceFunction },
        { "source_export", row.sourceExport },
        { "horse_type_index", row.horseTypeIndex },
        { "action_field_offset", row.actionFieldOffset },
        { "speed_field_offset", row.speedFieldOffset },
        { "animation_index_field_offset", row.animationIndexFieldOffset },
        { "horse_type_field_offset", row.horseTypeFieldOffset },
        { "skel_anime_field_offset", row.skelAnimeFieldOffset },
        { "follow_timer_field_offset", row.followTimerFieldOffset },
        { "action_value", row.actionValue },
        { "animation_indices", row.animationIndices },
        { "table_pointer_literal_address", row.tablePointerLiteralAddress },
        { "animation_table_outer_runtime_address", row.animationTableOuterRuntimeAddress },
        { "animation_table_runtime_address", row.animationTableRuntimeAddress },
        { "primary_literal_addresses", row.primaryLiteralAddresses },
        { "primary_literal_values", row.primaryLiteralValues },
        { "forced_speed", row.forcedSpeed },
        { "update_arg", row.updateArg },
        { "gallop_control_block_runtime_address", row.gallopControlBlockRuntimeAddress },
        { "gallop_play_speed_scale", row.gallopPlaySpeedScale },
        { "gallop_play_speed_switch", row.gallopPlaySpeedSwitch },
        { "gallop_play_speed_min", row.gallopPlaySpeedMin },
        { "gallop_play_speed_max", row.gallopPlaySpeedMax },
        { "gallop_brake_input_magnitude", row.gallopBrakeInputMagnitude },
        { "gallop_downshift_speed", row.gallopDownshiftSpeed },
        { "gallop_fast_animation_index", row.gallopFastAnimationIndex },
        { "gallop_fast_csab_type_index", row.gallopFastCsabTypeIndex },
        { "gallop_fast_csab_file_index", row.gallopFastCsabFileIndex },
        { "gallop_fast_csab_name", row.gallopFastCsabName },
        { "gallop_carrot_animation_index", row.gallopCarrotAnimationIndex },
        { "gallop_carrot_csab_type_index", row.gallopCarrotCsabTypeIndex },
        { "gallop_carrot_csab_file_index", row.gallopCarrotCsabFileIndex },
        { "gallop_carrot_csab_name", row.gallopCarrotCsabName },
        { "resolved_csabs", row.resolvedCsabs },
        { "oot3d_basis", row.oot3dBasis },
        { "n64_reference", row.n64Reference },
        { "unresolved_followup", row.unresolvedFollowup },
        { "decode_status", row.decodeStatus },
    };
}

nlohmann::json TitleIntroHorseStateRouteRowsToJson() {
    auto rows = nlohmann::json::array();
    for (uint32_t i = 0; i < gOot3dTitleIntroHorseStateRouteRowCount; ++i) {
        rows.push_back(TitleIntroHorseStateRouteRowToJson(gOot3dTitleIntroHorseStateRouteRows[i]));
    }
    return rows;
}

nlohmann::json TitleIntroLinkChildStateRouteRowToJson(const Oot3dTitleIntroLinkChildStateRouteRow& row) {
    return {
        { "state_route_index", row.stateRouteIndex },
        { "route_role", row.routeRole },
        { "actor_role", row.actorRole },
        { "actor_name", row.actorName },
        { "asset_role", row.assetRole },
        { "archive_path", row.archivePath },
        { "update_function", row.updateFunction },
        { "init_function", row.initFunction },
        { "draw_function", row.drawFunction },
        { "action_field_offset", row.actionFieldOffset },
        { "animation_index_field_offset", row.animationIndexFieldOffset },
        { "speed_field_offset", row.speedFieldOffset },
        { "skel_anime_field_offset", row.skelAnimeFieldOffset },
        { "action_table_pointer_literal_address", row.actionTablePointerLiteralAddress },
        { "action_table_runtime_address", row.actionTableRuntimeAddress },
        { "action_slot", row.actionSlot },
        { "handler_function", row.handlerFunction },
        { "animation_table_pointer_literal_address", row.animationTablePointerLiteralAddress },
        { "animation_table_runtime_address", row.animationTableRuntimeAddress },
        { "expected_animation_indices", row.expectedAnimationIndices },
        { "resolved_csabs", row.resolvedCsabs },
        { "expected_speed_values", row.expectedSpeedValues },
        { "n64_reference_action", row.n64ReferenceAction },
        { "oot3d_basis", row.oot3dBasis },
        { "n64_reference", row.n64Reference },
        { "unresolved_followup", row.unresolvedFollowup },
        { "decode_status", row.decodeStatus },
    };
}

nlohmann::json TitleIntroLinkChildStateRouteRowsToJson() {
    auto rows = nlohmann::json::array();
    for (uint32_t i = 0; i < gOot3dTitleIntroLinkChildStateRouteRowCount; ++i) {
        rows.push_back(TitleIntroLinkChildStateRouteRowToJson(gOot3dTitleIntroLinkChildStateRouteRows[i]));
    }
    return rows;
}

nlohmann::json TitleIntroLinkBoyPlayerActionRowToJson(const Oot3dTitleIntroLinkBoyPlayerActionRow& row) {
    return {
        { "player_action_index", row.playerActionIndex },
        { "actor_cue_index", row.actorCueIndex },
        { "actor_role", row.actorRole },
        { "actor_name", row.actorName },
        { "asset_role", row.assetRole },
        { "archive_path", row.archivePath },
        { "cmb_name", row.cmbName },
        { "title_visual_csab_name", row.titleVisualCsabName },
        { "qdb_index", row.qdbIndex },
        { "qdb_command_index", row.qdbCommandIndex },
        { "qdb_archive_path", row.qdbArchivePath },
        { "qdb_embedded_name", row.qdbEmbeddedName },
        { "cue_command_id", row.cueCommandId },
        { "cue_index", row.cueIndex },
        { "cue_id", row.cueId },
        { "cue_role", row.cueRole },
        { "start_frame", row.startFrame },
        { "end_frame", row.endFrame },
        { "duration_frames", row.durationFrames },
        { "rot_x", row.rotX },
        { "rot_y", row.rotY },
        { "rot_z", row.rotZ },
        { "start_x", row.startX },
        { "start_y", row.startY },
        { "start_z", row.startZ },
        { "end_x", row.endX },
        { "end_y", row.endY },
        { "end_z", row.endZ },
        { "delta_x", row.deltaX },
        { "delta_y", row.deltaY },
        { "delta_z", row.deltaZ },
        { "normal_x", row.normalX },
        { "normal_y", row.normalY },
        { "normal_z", row.normalZ },
        { "oot3d_basis", row.oot3dBasis },
        { "n64_reference", row.n64Reference },
        { "unresolved_followup", row.unresolvedFollowup },
        { "decode_status", row.decodeStatus },
    };
}

nlohmann::json TitleIntroLinkBoyPlayerActionRowsToJson() {
    auto rows = nlohmann::json::array();
    for (uint32_t i = 0; i < gOot3dTitleIntroLinkBoyPlayerActionRowCount; ++i) {
        rows.push_back(TitleIntroLinkBoyPlayerActionRowToJson(gOot3dTitleIntroLinkBoyPlayerActionRows[i]));
    }
    return rows;
}

nlohmann::json TitleIntroNativeActorMotionClipsToJson(const TitleIntroNativeActor& actor) {
    auto clips = nlohmann::json::array();
    for (const auto& clip : actor.MotionClips) {
        clips.push_back({
            { "loaded", clip.Loaded },
            { "role", clip.Role },
            { "csab_name", clip.CsabName },
            { "animation_index", clip.AnimationIndex },
            { "play_speed_scale", clip.PlaySpeedScale },
            { "frame_count", clip.Metadata.FrameCount },
            { "animated_bone_count", clip.Metadata.AnimatedBoneCount },
            { "source", clip.Source },
        });
    }
    return clips;
}

nlohmann::json TitleIntroOpeningActorBindingToJson(
    const Oot3dTitleIntroOpeningActorBindingRow* row);

nlohmann::json TitleIntroNativeActorToJson(const TitleIntroNativeActor& actor) {
    return {
        { "loaded", actor.Loaded },
        { "role", actor.Role },
        { "opening_actor_binding_status", actor.OpeningActorBindingStatus },
        { "opening_actor_binding", TitleIntroOpeningActorBindingToJson(actor.OpeningActorBindingRow) },
        { "archive_path", actor.ArchivePath.string() },
        { "cmb_name", actor.CmbName },
        { "csab_name", actor.CsabName },
        { "status", actor.Status },
        { "scale", actor.Scale },
        { "native_actor_scale", actor.NativeActorScale },
        { "native_actor_scale_base", actor.NativeActorScaleBase },
        { "scale_source", actor.ScaleSource },
        { "scale_status", actor.ScaleStatus },
        { "scale_row", TitleIntroActorScaleRowToJson(actor.ScaleRow) },
        { "animation_row", TitleIntroActorAnimationRowToJson(actor.AnimationRow) },
        { "motion_animation_row", TitleIntroActorMotionAnimationRowToJson(actor.MotionAnimationRow) },
        { "motion_clip_status", actor.MotionClipStatus },
        { "motion_clips", TitleIntroNativeActorMotionClipsToJson(actor) },
        { "resource_visibility_status", actor.ResourceVisibilityStatus },
        { "native_visible_resource_count", actor.NativeVisibleResourceCount },
        { "native_resource_visibility", actor.NativeResourceVisibility },
        { "render_batch_count", actor.BaseRenderModel.Batches.size() },
        { "render_triangle_count", actor.BaseRenderModel.TriangleCount() },
        { "cmb_bone_count", actor.Model.Skeleton.Bones.size() },
        { "cmb_mesh_count", actor.Model.Meshes.size() },
        { "cmb_material_count", actor.Model.Materials.size() },
        { "csab_frame_count", actor.Csab.FrameCount },
        { "csab_animated_bone_count", actor.Csab.AnimatedBoneCount },
    };
}

nlohmann::json TitleIntroActorInitRowToJson(const Oot3dTitleIntroActorInitSourceRow* row) {
    if (row == nullptr) {
        return nullptr;
    }
    return {
        { "actor_init_index", row->actorInitIndex },
        { "actor_init_file_offset", row->actorInitFileOffset },
        { "actor_id", row->actorId },
        { "actor_category", row->actorCategory },
        { "flags", row->flags },
        { "object_id", row->objectId },
        { "instance_size", row->instanceSize },
        { "init_function", row->initFunction },
        { "destroy_function", row->destroyFunction },
        { "update_function", row->updateFunction },
        { "draw_function", row->drawFunction },
        { "actor_name", row->actorName },
        { "object_name", row->objectName },
        { "decode_status", row->decodeStatus },
        { "basis", row->basis },
    };
}

nlohmann::json TitleIntroLogoComponentRowToJson(const Oot3dTitleIntroLogoComponentRow* row) {
    if (row == nullptr) {
        return nullptr;
    }
    return {
        { "component_index", row->componentIndex },
        { "asset_index", row->assetIndex },
        { "handle_field_offset", row->handleFieldOffset },
        { "alpha_field_offset", row->alphaFieldOffset },
        { "cmb_index_jpeu", row->cmbIndexJpeu },
        { "csab_index_jpeu", row->csabIndexJpeu },
        { "cmb_index_us", row->cmbIndexUs },
        { "csab_index_us", row->csabIndexUs },
        { "cmab_index", row->cmabIndex },
        { "material_animation_runtime_owner_field_offset",
          row->materialAnimationRuntimeOwnerFieldOffset },
        { "material_animation_runtime_loop_field_offset",
          row->materialAnimationRuntimeLoopFieldOffset },
        { "material_animation_runtime_loop_override_valid",
          row->materialAnimationRuntimeLoopOverrideValid },
        { "material_animation_runtime_loop_mode", row->materialAnimationRuntimeLoopMode },
        { "material_animation_runtime_init_function", row->materialAnimationRuntimeInitFunction },
        { "material_animation_runtime_step_function", row->materialAnimationRuntimeStepFunction },
        { "component_role", row->componentRole },
        { "archive_path", row->archivePath },
        { "cmb_name_jpeu", row->cmbNameJpeu },
        { "csab_name_jpeu", row->csabNameJpeu },
        { "cmb_name_us", row->cmbNameUs },
        { "csab_name_us", row->csabNameUs },
        { "cmab_name", row->cmabName },
        { "material_animation_binding_basis", row->materialAnimationBindingBasis },
        { "binding_basis", row->bindingBasis },
    };
}

nlohmann::json TitleIntroLogoDrawRowToJson(const Oot3dTitleIntroLogoDrawRow* row) {
    if (row == nullptr) {
        return nullptr;
    }
    const nlohmann::json matrix = {
        { row->matrix00, row->matrix01, row->matrix02, row->matrix03 },
        { row->matrix10, row->matrix11, row->matrix12, row->matrix13 },
        { row->matrix20, row->matrix21, row->matrix22, row->matrix23 },
        { row->matrix30, row->matrix31, row->matrix32, row->matrix33 },
    };
    return {
        { "draw_index", row->drawIndex },
        { "submit_order", row->submitOrder },
        { "component_index", row->componentIndex },
        { "component_role", row->componentRole },
        { "handle_field_offset", row->handleFieldOffset },
        { "alpha_field_offset", row->alphaFieldOffset },
        { "effect_alpha_field_offset", row->effectAlphaFieldOffset },
        { "draw_function", row->drawFunction },
        { "color_pointer_address", row->colorPointerAddress },
        { "color_runtime_address", row->colorRuntimeAddress },
        { "light_block_pointer_address", row->lightBlockPointerAddress },
        { "light_block_runtime_address", row->lightBlockRuntimeAddress },
        { "renderer_flag_pointer_address", row->rendererFlagPointerAddress },
        { "renderer_flag_runtime_address", row->rendererFlagRuntimeAddress },
        { "render_context_pointer_address", row->renderContextPointerAddress },
        { "render_context_runtime_address", row->renderContextRuntimeAddress },
        { "alternate_render_context_pointer_address", row->alternateRenderContextPointerAddress },
        { "alternate_render_context_runtime_address", row->alternateRenderContextRuntimeAddress },
        { "material_handle_function", row->materialHandleFunction },
        { "material_slot_select_function", row->materialSlotSelectFunction },
        { "material_color_apply_function", row->materialColorApplyFunction },
        { "matrix_copy_function", row->matrixCopyFunction },
        { "submit_function", row->submitFunction },
        { "light_config_reset_function", row->lightConfigResetFunction },
        { "light_config_apply_function", row->lightConfigApplyFunction },
        { "light_vector_apply_function", row->lightVectorApplyFunction },
        { "alpha_scale", row->alphaScale },
        { "base_translate_z", row->baseTranslateZ },
        { "small_depth_offset", row->smallDepthOffset },
        { "copyright_translate_y", row->copyrightTranslateY },
        { "effect_vector_double_scale", row->effectVectorDoubleScale },
        { "effect_vector_half_scale", row->effectVectorHalfScale },
        { "effect_vector_bias", row->effectVectorBias },
        { "base_color", { row->baseColorR, row->baseColorG, row->baseColorB, row->baseColorA } },
        { "matrix_row_major_4x4", matrix },
        { "matrix_role", row->matrixRole },
        { "matrix_source_addresses", row->matrixSourceAddresses },
        { "light_block_row_major", row->lightBlockRowMajor },
        { "visibility_condition", row->visibilityCondition },
        { "draw_basis", row->drawBasis },
        { "decode_status", row->decodeStatus },
    };
}

nlohmann::json TitleIntroLogoDrawContextRowToJson(const Oot3dTitleIntroLogoDrawContextRow* row) {
    if (row == nullptr) {
        return nullptr;
    }
    return {
        { "context_index", row->contextIndex },
        { "context_role", row->contextRole },
        { "draw_function", row->drawFunction },
        { "submit_function", row->submitFunction },
        { "renderer_flag_pointer_address", row->rendererFlagPointerAddress },
        { "renderer_flag_runtime_address", row->rendererFlagRuntimeAddress },
        { "render_context_pointer_address", row->renderContextPointerAddress },
        { "render_context_runtime_address", row->renderContextRuntimeAddress },
        { "alternate_render_context_pointer_address", row->alternateRenderContextPointerAddress },
        { "alternate_render_context_runtime_address", row->alternateRenderContextRuntimeAddress },
        { "global_context_runtime_address", row->globalContextRuntimeAddress },
        { "submit_manager_runtime_address", row->submitManagerRuntimeAddress },
        { "global_context_submit_manager_offset", row->globalContextSubmitManagerOffset },
        { "submit_manager_constructor_function", row->submitManagerConstructorFunction },
        { "submit_manager_vtable_address", row->submitManagerVtableAddress },
        { "submit_manager_storage_size", row->submitManagerStorageSize },
        { "small_queue_count_offset", row->smallQueueCountOffset },
        { "small_queue_storage_offset", row->smallQueueStorageOffset },
        { "small_queue_capacity", row->smallQueueCapacity },
        { "small_queue_record_stride", row->smallQueueRecordStride },
        { "small_queue_record_state_byte_offset", row->smallQueueRecordStateByteOffset },
        { "small_queue_record_state_byte_value", row->smallQueueRecordStateByteValue },
        { "small_queue_record_write_function", row->smallQueueRecordWriteFunction },
        { "small_queue_drain_function", row->smallQueueDrainFunction },
        { "pass0_drain_function", row->pass0DrainFunction },
        { "pass1_drain_function", row->pass1DrainFunction },
        { "lazy_guard_function", row->lazyGuardFunction },
        { "lazy_init_function", row->lazyInitFunction },
        { "draw_handle_vtable_submit_slot_offset", row->drawHandleVtableSubmitSlotOffset },
        { "oot3d_basis", row->oot3dBasis },
        { "n64_reference", row->n64Reference },
        { "decode_status", row->decodeStatus },
    };
}

nlohmann::json TitleIntroLogoUpdateRowToJson(const Oot3dTitleIntroLogoUpdateRow* row) {
    if (row == nullptr) {
        return nullptr;
    }
    return {
        { "update_index", row->updateIndex },
        { "phase_role", row->phaseRole },
        { "state", row->state },
        { "substate", row->substate },
        { "next_state", row->nextState },
        { "next_substate", row->nextSubstate },
        { "timer_field_offset", row->timerFieldOffset },
        { "timer_initial_value", row->timerInitialValue },
        { "flag_id", row->flagId },
        { "literal_address", row->literalAddress },
        { "literal_value", row->literalValue },
        { "max_alpha", row->maxAlpha },
        { "min_alpha", row->minAlpha },
        { "main_alpha_step", row->mainAlphaStep },
        { "title_text_effect_alpha_step", row->titleTextEffectAlphaStep },
        { "copyright_alpha_clamp", row->copyrightAlphaClamp },
        { "copyright_alpha_step_default", row->copyrightAlphaStepDefault },
        { "fade_out_alpha_step_default", row->fadeOutAlphaStepDefault },
        { "transition_copyright_alpha_step", row->transitionCopyrightAlphaStep },
        { "transition_fade_out_alpha_step", row->transitionFadeOutAlphaStep },
        { "init_function", row->initFunction },
        { "update_function", row->updateFunction },
        { "flags_get_env_function", row->flagsGetEnvFunction },
        { "get_csab_by_index_function", row->getCsabByIndexFunction },
        { "set_csab_function", row->setCsabFunction },
        { "set_csab_frame_function", row->setCsabFrameFunction },
        { "set_title_anim_state_function", row->setTitleAnimStateFunction },
        { "audio_cutscene_flag_function", row->audioCutsceneFlagFunction },
        { "audio_play_sound_function", row->audioPlaySoundFunction },
        { "alpha_field_offsets", row->alphaFieldOffsets },
        { "alpha_operation", row->alphaOperation },
        { "alpha_delta_source", row->alphaDeltaSource },
        { "oot3d_basis", row->oot3dBasis },
        { "n64_reference", row->n64Reference },
        { "decode_status", row->decodeStatus },
    };
}

nlohmann::json TitleIntroLogoComponentRuntimeToJson(const TitleIntroLogoComponentRuntime& component) {
    return {
        { "loaded", component.Loaded },
        { "variant", component.Variant },
        { "archive_path", component.ArchivePath.string() },
        { "cmb_name", component.CmbName },
        { "csab_name", component.CsabName },
        { "status", component.Status },
        { "cmb_bone_count", component.Model.Skeleton.Bones.size() },
        { "cmb_mesh_count", component.Model.Meshes.size() },
        { "cmb_material_count", component.Model.Materials.size() },
        { "csab_frame_count", component.Csab.FrameCount },
        { "csab_animated_bone_count", component.Csab.AnimatedBoneCount },
        { "material_animation_count", component.MaterialAnimations.size() },
        { "material_animation_names", component.MaterialAnimationNames },
        { "material_animation_runtime_loop_override_resolved",
          component.MaterialAnimationRuntimeLoopOverrideResolved },
        { "material_animation_runtime_loop_mode", component.MaterialAnimationRuntimeLoopMode },
        { "material_animation_runtime_loop_field_offset",
          component.MaterialAnimationRuntimeLoopFieldOffset },
        { "material_animation_runtime_loop_source", component.MaterialAnimationRuntimeLoopSource },
        { "bind_world_transform_count", component.BindWorldTransforms.size() },
        { "source_row", TitleIntroLogoComponentRowToJson(component.Row) },
    };
}

nlohmann::json TitleIntroLogoAlphaStateToJson(const TitleIntroLogoAlphaState& state) {
    return {
        { "decoded", state.Decoded },
        { "simulated", state.Simulated },
        { "used_for_render", state.UsedForRender },
        { "native_rows_used", state.NativeRowsUsed },
        { "input_frame", state.InputFrame },
        { "update_ticks", state.UpdateTicks },
        { "state", state.State },
        { "substate", state.Substate },
        { "delay_timer", state.DelayTimer },
        { "timer", state.Timer },
        { "title_text_alpha", state.TitleTextAlpha },
        { "main_logo_alpha", state.MainLogoAlpha },
        { "copyright_alpha", state.CopyrightAlpha },
        { "effect_alpha", state.EffectAlpha },
        { "copyright_alpha_step", state.CopyrightAlphaStep },
        { "fade_out_alpha_step", state.FadeOutAlphaStep },
        { "env3_triggered", state.Env3Triggered },
        { "env4_triggered", state.Env4Triggered },
        { "display_reached", state.DisplayReached },
        { "frame_boundary_trace_pending", state.FrameBoundaryTracePending },
        { "env3_schedule_source", state.Env3ScheduleSource },
        { "status", state.Status },
        { "basis",
          state.Simulated
              ? kOot3dTitleIntroLogoAlphaSimulationBasis
              : "OOT3D title-intro opening frame runtime EnMag logo state; generated from code.bin-backed update rows and used by the render draw snapshot" },
        { "applied_phase_roles", state.AppliedPhaseRoles },
    };
}

nlohmann::json TitleIntroLogoRuntimeToJson(const TitleIntroLogoRuntime& runtime) {
    nlohmann::json components = nlohmann::json::array();
    for (const auto& component : runtime.Components) {
        components.push_back(TitleIntroLogoComponentRuntimeToJson(component));
    }
    nlohmann::json drawRows = nlohmann::json::array();
    for (const auto* row : runtime.DrawRows) {
        drawRows.push_back(TitleIntroLogoDrawRowToJson(row));
    }
    nlohmann::json drawContextRows = nlohmann::json::array();
    for (const auto* row : runtime.DrawContextRows) {
        drawContextRows.push_back(TitleIntroLogoDrawContextRowToJson(row));
    }
    nlohmann::json updateRows = nlohmann::json::array();
    for (const auto* row : runtime.UpdateRows) {
        updateRows.push_back(TitleIntroLogoUpdateRowToJson(row));
    }
    nlohmann::json alphaReferenceSamples = nlohmann::json::array();
    for (const auto& sample : runtime.AlphaReferenceSamples) {
        alphaReferenceSamples.push_back(TitleIntroLogoAlphaStateToJson(sample));
    }
    return {
        { "decoded", runtime.Decoded },
        { "loaded", runtime.Loaded },
        { "status", runtime.Status },
        { "opening_actor_binding_status", runtime.OpeningActorBindingStatus },
        { "opening_actor_binding", TitleIntroOpeningActorBindingToJson(runtime.OpeningActorBindingRow) },
        { "variant", runtime.Variant },
        { "variant_status", runtime.VariantStatus },
        { "archive_path", runtime.ArchivePath.string() },
        { "component_count", runtime.Components.size() },
        { "draw_route_decoded", runtime.DrawRouteDecoded },
        { "draw_row_count", runtime.DrawRows.size() },
        { "draw_status", runtime.DrawStatus },
        { "draw_context_decoded", runtime.DrawContextDecoded },
        { "draw_context_row_count", runtime.DrawContextRows.size() },
        { "draw_context_status", runtime.DrawContextStatus },
        { "update_route_decoded", runtime.UpdateRouteDecoded },
        { "update_row_count", runtime.UpdateRows.size() },
        { "update_status", runtime.UpdateStatus },
        { "alpha_state", TitleIntroLogoAlphaStateToJson(runtime.AlphaState) },
        { "alpha_reference_samples", alphaReferenceSamples },
        { "alpha_reference_sample_count", alphaReferenceSamples.size() },
        { "binding_basis", kOot3dTitleIntroLogoBindingBasis },
        { "draw_projection_basis", kOot3dTitleIntroLogoDrawProjectionBasis },
        { "draw_render_binding_basis", kOot3dTitleIntroLogoDrawRenderBindingBasis },
        { "update_basis", kOot3dTitleIntroLogoUpdateBasis },
        { "actor_init_row", TitleIntroActorInitRowToJson(runtime.ActorInitRow) },
        { "components", components },
        { "draw_rows", drawRows },
        { "draw_context_rows", drawContextRows },
        { "update_rows", updateRows },
    };
}

nlohmann::json TitleIntroOpeningOrchestrationToJson(
    const Oot3dTitleIntroOpeningOrchestrationRow* row) {
    if (row == nullptr) {
        return nullptr;
    }
    return {
        { "orchestration_index", row->orchestrationIndex },
        { "scene_id", row->sceneId },
        { "setup_index", row->setupIndex },
        { "cutscene_source_index", row->cutsceneSourceIndex },
        { "native_header_offset", row->nativeHeaderOffset },
        { "native_end_frame", row->nativeEndFrame },
        { "native_command_ref_start", row->nativeCommandRefStart },
        { "native_command_ref_count", row->nativeCommandRefCount },
        { "player_action_ref_start", row->playerActionRefStart },
        { "player_action_ref_count", row->playerActionRefCount },
        { "player_action_max_end_frame", row->playerActionMaxEndFrame },
        { "camera_blob_source_index", row->cameraBlobSourceIndex },
        { "camera_blob_segment_source_index", row->cameraBlobSegmentSourceIndex },
        { "camera_cmad_record_ref_start", row->cameraCmadRecordRefStart },
        { "camera_cmad_record_ref_count", row->cameraCmadRecordRefCount },
        { "camera_curve_ref_start", row->cameraCurveRefStart },
        { "camera_curve_ref_count", row->cameraCurveRefCount },
        { "slot6_eye_distance_to_emulator", row->slot6EyeDistanceToEmulator },
        { "link_actor_scale_index", row->linkActorScaleIndex },
        { "epona_actor_scale_index", row->eponaActorScaleIndex },
        { "link_actor_animation_index", row->linkActorAnimationIndex },
        { "epona_actor_animation_index", row->eponaActorAnimationIndex },
        { "horse_motion_animation_index", row->horseMotionAnimationIndex },
        { "title_logo_actor_init_index", row->titleLogoActorInitIndex },
        { "title_logo_component_ref_start", row->titleLogoComponentRefStart },
        { "title_logo_component_ref_count", row->titleLogoComponentRefCount },
        { "title_logo_draw_ref_start", row->titleLogoDrawRefStart },
        { "title_logo_draw_ref_count", row->titleLogoDrawRefCount },
        { "title_logo_update_ref_start", row->titleLogoUpdateRefStart },
        { "title_logo_update_ref_count", row->titleLogoUpdateRefCount },
        { "role", row->role },
        { "scene_path", row->scenePath },
        { "slot6_trace_path", row->slot6TracePath },
        { "basis", row->basis },
        { "unresolved", row->unresolved },
    };
}

nlohmann::json TitleIntroOpeningCameraSampleToJson(const Oot3dTitleIntroOpeningCameraSample& sample) {
    return {
        { "frame", sample.frame },
        { "orchestration_index", sample.orchestrationIndex },
        { "camera_blob_source_index", sample.cameraBlobSourceIndex },
        { "camera_blob_segment_source_index", sample.cameraBlobSegmentSourceIndex },
        { "intro_camera_timeline_source_index", sample.introCameraTimelineSourceIndex },
        { "segment_start_frame", sample.segmentStartFrame },
        { "segment_end_frame", sample.segmentEndFrame },
        { "has_slot6_reference", sample.hasSlot6Reference != 0 },
        { "slot6_reference_distance", sample.slot6ReferenceDistance },
        { "slot6_eye_distance_to_emulator", sample.slot6EyeDistanceToEmulator },
        { "cs_params",
          {
              { "cs_param_80", sample.csParams.csParam80 },
              { "cs_param_84", sample.csParams.csParam84 },
              { "cs_param_88", sample.csParams.csParam88 },
              { "cs_param_8c", sample.csParams.csParam8C },
              { "cs_param_90", sample.csParams.csParam90 },
              { "cs_param_94", sample.csParams.csParam94 },
              { "cs_param_144", sample.csParams.csParam144 },
              { "cs_param_d0", sample.csParams.csParamD0 },
              { "cs_param_1a2", sample.csParams.csParam1A2 },
          } },
        { "view",
          {
              { "eye", Vec3ToJson(ToVec3(sample.view.eye)) },
              { "at", Vec3ToJson(ToVec3(sample.view.at)) },
              { "up", Vec3ToJson(ToVec3(sample.view.up)) },
              { "eye_to_at",
                {
                    { "r", sample.view.eyeToAt.r },
                    { "pitch", sample.view.eyeToAt.pitch },
                    { "yaw", sample.view.eyeToAt.yaw },
                } },
              { "fov", sample.view.fov },
              { "view_dist_d0", sample.view.viewDistD0 },
              { "perturbation_magnitude", sample.view.perturbationMagnitude },
              { "roll", sample.view.roll },
              { "has_perturbation", sample.view.hasPerturbation != 0 },
          } },
    };
}

nlohmann::json TitleIntroOpeningPlayerMotionResultToJson(
    const Oot3dTitleIntroPlayerActionMotionResult& motion) {
    return {
        { "vector_x_f32", motion.vectorXF32 },
        { "vector_z_f32", motion.vectorZF32 },
        { "vector_x_s32_vcvt", motion.vectorXS32Vcvt },
        { "vector_z_s32_vcvt", motion.vectorZS32Vcvt },
        { "vector_x_quantized_f32", motion.vectorXQuantizedF32 },
        { "vector_z_quantized_f32", motion.vectorZQuantizedF32 },
        { "unclamped_speed", motion.unclampedSpeed },
        { "clamped_speed", motion.clampedSpeed },
        { "speed_clamped", motion.speedClamped != 0 },
        { "heading_computed", motion.headingComputed != 0 },
        { "atan2_arg0_z", motion.atan2Arg0Z },
        { "atan2_arg1_neg_x", motion.atan2Arg1NegX },
        { "heading", motion.heading },
    };
}

nlohmann::json TitleIntroOpeningActorBindingToJson(
    const Oot3dTitleIntroOpeningActorBindingRow* row) {
    if (row == nullptr) {
        return nullptr;
    }
    return {
        { "actor_binding_index", row->actorBindingIndex },
        { "orchestration_index", row->orchestrationIndex },
        { "actor_kind", row->actorKind },
        { "source_scale_index", row->sourceScaleIndex },
        { "source_animation_index", row->sourceAnimationIndex },
        { "source_motion_animation_index", row->sourceMotionAnimationIndex },
        { "source_actor_init_index", row->sourceActorInitIndex },
        { "paired_actor_binding_index", row->pairedActorBindingIndex },
        { "required_asset_ref_index", row->requiredAssetRefIndex },
        { "actor_init_function", row->actorInitFunction },
        { "actor_update_function", row->actorUpdateFunction },
        { "actor_draw_function", row->actorDrawFunction },
        { "actor_scale", row->actorScale },
        { "gravity", row->gravity },
        { "shadow_scale", row->shadowScale },
        { "focus_y_offset", row->focusYOffset },
        { "actor_role", row->actorRole },
        { "actor_name", row->actorName },
        { "archive_path", row->archivePath },
        { "cmb_name", row->cmbName },
        { "init_csab_name", row->initCsabName },
        { "title_visual_csab_name", row->titleVisualCsabName },
        { "runtime_binding_role", row->runtimeBindingRole },
        { "native_basis", row->nativeBasis },
        { "unresolved", row->unresolved },
    };
}

nlohmann::json TitleIntroOpeningActorMotionCueToJson(
    const Oot3dTitleIntroOpeningActorMotionCueRow* row) {
    if (row == nullptr) {
        return nullptr;
    }
    return {
        { "timeline_index", row->timelineIndex },
        { "orchestration_index", row->orchestrationIndex },
        { "actor_binding_index", row->actorBindingIndex },
        { "paired_mount_actor_binding_index", row->pairedMountActorBindingIndex },
        { "motion_ref_index", row->motionRefIndex },
        { "player_action_ref_index", row->playerActionRefIndex },
        { "player_action_source_index", row->playerActionSourceIndex },
        { "native_command_source_index", row->nativeCommandSourceIndex },
        { "local_command_index", row->localCommandIndex },
        { "entry_index", row->entryIndex },
        { "action_id", row->actionId },
        { "start_frame", row->startFrame },
        { "end_frame", row->endFrame },
        { "duration_frames", row->durationFrames },
        { "direct_state_case_present", row->directStateCasePresent != 0 },
        { "not_direct_state_case", row->notDirectStateCase != 0 },
        { "has_quantized_motion_vector", row->hasQuantizedMotionVector != 0 },
        { "speed_clamped", row->speedClamped != 0 },
        { "start_exclusive_end_inclusive", row->startExclusiveEndInclusive != 0 },
        { "direct_record_layout_index", row->directRecordLayoutIndex },
        { "direct_state_id", row->directStateId },
        { "direct_state_switch_function", row->directStateSwitchFunction },
        { "direct_state_switch_wrapper_function", row->directStateSwitchWrapperFunction },
        { "direct_state_setter_function", row->directStateSetterFunction },
        { "direct_state_handler_function", row->directStateHandlerFunction },
        { "native_speed", row->nativeSpeed },
        { "native_heading", row->nativeHeading },
        { "action_id_hex", row->actionIdHex },
        { "direct_state_id_hex", row->directStateIdHex },
        { "direct_state_features", row->directStateFeatures },
        { "direct_state_calls", row->directStateCalls },
        { "direct_state_outgoing_targets", row->directStateOutgoingTargets },
        { "player_action_consumer_status", row->playerActionConsumerStatus },
        { "direct_state_evidence", row->directStateEvidence },
        { "source_semantic", row->sourceSemantic },
        { "unresolved", row->unresolved },
    };
}

nlohmann::json TitleIntroOpeningActorVisualTransformToJson(
    const Oot3dTitleIntroOpeningActorVisualTransformSample& transform) {
    const char* sourceKind = "none";
    if (transform.sourceKind == OOT3D_TITLE_INTRO_OPENING_ACTOR_VISUAL_TRANSFORM_LINK_PLAYER_ACTION) {
        sourceKind = "link_player_action";
    } else if (transform.sourceKind == OOT3D_TITLE_INTRO_OPENING_ACTOR_VISUAL_TRANSFORM_PAIRED_MOUNT_CUE) {
        sourceKind = "paired_mount_cue";
    } else if (transform.sourceKind ==
               OOT3D_TITLE_INTRO_OPENING_ACTOR_VISUAL_TRANSFORM_PAIRED_MOUNT_PLAYER_ACTION) {
        sourceKind = "paired_mount_player_action";
    }

    return {
        { "valid", transform.valid != 0 },
        { "active", transform.active != 0 },
        { "source_kind", sourceKind },
        { "source_kind_value", transform.sourceKind },
        { "actor_binding_index", transform.actorBindingIndex },
        { "frame", transform.frame },
        { "interpolation", transform.interpolation },
        { "position", { { "x", transform.positionX }, { "y", transform.positionY }, { "z", transform.positionZ } } },
        { "rotation_s16", { { "x", transform.rotX }, { "y", transform.rotY }, { "z", transform.rotZ } } },
        { "motion_row", TitleIntroOpeningPlayerMotionRowToJson(transform.motionRow) },
        { "actor_cue_row", TitleIntroCueRowToJson(transform.actorCueRow) },
    };
}

nlohmann::json TitleIntroOpeningActorDirectRecordLayoutToJson(
    const Oot3dTitleIntroOpeningActorDirectRecordLayoutRow* row) {
    if (row == nullptr) {
        return nullptr;
    }
    return {
        { "direct_record_layout_index", row->directRecordLayoutIndex },
        { "entry_direct_state_id", row->entryDirectStateId },
        { "consumer_direct_state_id", row->consumerDirectStateId },
        { "completion_direct_state_id", row->completionDirectStateId },
        { "branch_direct_state_id", row->branchDirectStateId },
        { "entry_mode_change_function", row->entryModeChangeFunction },
        { "entry_mode_byte", row->entryModeByte },
        { "entry_global_gate_function", row->entryGlobalGateFunction },
        { "entry_global_gate_byte", row->entryGlobalGateByte },
        { "state37_handler_function", row->state37HandlerFunction },
        { "record_pointer_getter_function", row->recordPointerGetterFunction },
        { "record_pointer_literal_pool_address", row->recordPointerLiteralPoolAddress },
        { "record_context_address", row->recordContextAddress },
        { "record_table_address", row->recordTableAddress },
        { "record_pointer_address", row->recordPointerAddress },
        { "record_context_offset", row->recordContextOffset },
        { "record_stride", row->recordStride },
        { "record_byte0_offset", row->recordByte0Offset },
        { "record_byte1_offset", row->recordByte1Offset },
        { "record_byte2_offset", row->recordByte2Offset },
        { "record_byte0_role", row->recordByte0Role },
        { "record_byte1_role", row->recordByte1Role },
        { "record_byte2_role", row->recordByte2Role },
        { "record_byte0_source", row->recordByte0Source },
        { "record_byte1_source", row->recordByte1Source },
        { "record_byte2_source", row->recordByte2Source },
        { "record_apply_function", row->recordApplyFunction },
        { "record_byte_table_literal_pool_address", row->recordByteTableLiteralPoolAddress },
        { "record_byte_table_address", row->recordByteTableAddress },
        { "first_change_latch_address", row->firstChangeLatchAddress },
        { "first_change_init_context_address", row->firstChangeInitContextAddress },
        { "record_change_notify_context_address", row->recordChangeNotifyContextAddress },
        { "record_apply_side_effect_index_limit", row->recordApplySideEffectIndexLimit },
        { "state37_apply_current_callsite", row->state37ApplyCurrentCallsite },
        { "state37_apply_terminator_callsite", row->state37ApplyTerminatorCallsite },
        { "state37_player_record_pointer_offset", row->state37PlayerRecordPointerOffset },
        { "state37_player_last_record_value_offset", row->state37PlayerLastRecordValueOffset },
        { "state37_cursor_source", row->state37CursorSource },
        { "record_pointer_status", row->recordPointerStatus },
        { "record_apply_status", row->recordApplyStatus },
        { "record_c_runtime_status", row->recordCRuntimeStatus },
        { "state37_playback_runtime_status", row->state37PlaybackRuntimeStatus },
        { "source_evidence", row->sourceEvidence },
        { "unresolved", row->unresolved },
    };
}

nlohmann::json TitleIntroRecordByteApplyEventToJson(const Oot3dTitleIntroRecordByteApplyEvent& event) {
    return {
        { "changed", event.changed != 0 },
        { "old_value", event.oldValue },
        { "new_value", event.newValue },
        { "index", event.index },
        { "first_change_init_requested", event.firstChangeInitRequested != 0 },
        { "notify_requested", event.notifyRequested != 0 },
        { "stored_after_notify", event.storedAfterNotify != 0 },
    };
}

nlohmann::json TitleIntroDispatchEventToJson(const Oot3dTitleIntroDispatchEvent& event) {
    return {
        { "dispatched", event.dispatched != 0 },
        { "code", event.code },
        { "scale_bits", event.scaleBits },
        { "payload_rebind_requested", event.payloadRebindRequested != 0 },
        { "payload_clear_requested", event.payloadClearRequested != 0 },
        { "payload_handle_b8_written", event.payloadHandleB8Written != 0 },
        { "payload_activated", event.payloadActivated != 0 },
        { "payload_descriptor", event.payloadDescriptor },
        { "payload_value", event.payloadValue },
        { "payload_arg", event.payloadArg },
    };
}

nlohmann::json TitleIntroSourceSelectorEventToJson(const Oot3dTitleIntroSourceSelectorEvent& event) {
    return {
        { "dirty_countdown_short_circuit", event.dirtyCountdownShortCircuit != 0 },
        { "stable_gate_short_circuit", event.stableGateShortCircuit != 0 },
        { "masked_gate_word", event.maskedGateWord },
        { "newly_active_bits", event.newlyActiveBits },
        { "active_selector_word", event.activeSelectorWord },
        { "selected", event.selected != 0 },
        { "selected_bit", event.selectedBit },
        { "selected_slot", event.selectedSlot },
        { "selected_source_byte", event.selectedSourceByte },
        { "high_adjust_applied", event.highAdjustApplied != 0 },
        { "low_adjust_applied", event.lowAdjustApplied != 0 },
        { "mode2_vector_path", event.mode2VectorPath != 0 },
        { "normal_vector_path", event.normalVectorPath != 0 },
        { "vector_lookup_index", event.vectorLookupIndex },
        { "vector_dispatch_value", event.vectorDispatchValue },
        { "vector_scale_bits", event.vectorScaleBits },
        { "dispatch_slot_changed", event.dispatchSlotChanged != 0 },
        { "slot_clear_requested", event.slotClearRequested != 0 },
        { "payload_rebind_skipped", event.payloadRebindSkipped != 0 },
        { "dispatch_scale_event", TitleIntroDispatchEventToJson(event.dispatchScaleEvent) },
        { "dispatch_payload_event", TitleIntroDispatchEventToJson(event.dispatchPayloadEvent) },
        { "dispatch_slot_event", TitleIntroDispatchEventToJson(event.dispatchSlotEvent) },
        { "dispatch_vector_event", TitleIntroDispatchEventToJson(event.dispatchVectorEvent) },
        { "dispatch_vector_reset_event", TitleIntroDispatchEventToJson(event.dispatchVectorResetEvent) },
    };
}

nlohmann::json TitleIntroSourceSelectorFeedEventToJson(const Oot3dTitleIntroSourceSelectorFeedEvent& event) {
    return {
        { "captured_gate_and_vector", event.capturedGateAndVector != 0 },
        { "captured_gate_word", event.capturedGateWord },
        { "captured_vector_x", event.capturedVectorX },
        { "captured_vector_y", event.capturedVectorY },
        { "gate_changed", event.gateChanged != 0 },
        { "gate_opened", event.gateOpened != 0 },
        { "gate_closed", event.gateClosed != 0 },
        { "gate_latched_from_saved", event.gateLatchedFromSaved != 0 },
        { "vectors_clamped", event.vectorsClamped != 0 },
        { "clamped_vector_x", event.clampedVectorX },
        { "clamped_vector_y", event.clampedVectorY },
        { "selector_applied", event.selectorApplied != 0 },
        { "selector_status", TitleIntroSourceSelectorStatusName(event.selectorStatus) },
        { "selector_status_value", event.selectorStatus },
        { "selector_event", TitleIntroSourceSelectorEventToJson(event.selectorEvent) },
        { "mode_changed", event.modeChanged != 0 },
        { "mode_opened", event.modeOpened != 0 },
        { "mode_closed", event.modeClosed != 0 },
        { "mode_close_commit_requested", event.modeCloseCommitRequested != 0 },
        { "current_progress_time_snapshotted", event.currentProgressTimeSnapshotted != 0 },
        { "time_counter_before", event.timeCounterBefore },
        { "time_counter_after", event.timeCounterAfter },
        { "frame_gate_rolled_from_saved", event.frameGateRolledFromSaved != 0 },
        { "runtime_request_initialized", event.runtimeRequestInitialized != 0 },
        { "runtime_request_advance_applied", event.runtimeRequestAdvanceApplied != 0 },
        { "runtime_request_completed", event.runtimeRequestCompleted != 0 },
        { "native_sequence_bound", event.nativeSequenceBound != 0 },
        { "sequence_advance_applied", event.sequenceAdvanceApplied != 0 },
        { "sequence_row_loaded", event.sequenceRowLoaded != 0 },
        { "record_a_composed", event.recordAComposed != 0 },
        { "record_b_composed", event.recordBComposed != 0 },
        { "record_a_bytes", { event.recordABytes[0], event.recordABytes[1], event.recordABytes[2] } },
        { "record_b_bytes", { event.recordBBytes[0], event.recordBBytes[1], event.recordBBytes[2] } },
        { "record_b_dispatch_event", TitleIntroDispatchEventToJson(event.recordBDispatchEvent) },
        { "record_b_payload_dispatch_event", TitleIntroDispatchEventToJson(event.recordBPayloadDispatchEvent) },
        { "record_b_scale_dispatch_event", TitleIntroDispatchEventToJson(event.recordBScaleDispatchEvent) },
        { "record_b_slot_dispatch_event", TitleIntroDispatchEventToJson(event.recordBSlotDispatchEvent) },
        { "record_b_lookup_dispatch_event", TitleIntroDispatchEventToJson(event.recordBLookupDispatchEvent) },
        { "record_b_vector_reset_dispatch_event", TitleIntroDispatchEventToJson(event.recordBVectorResetDispatchEvent) },
    };
}

nlohmann::json TitleIntroRecordCFrameEventToJson(const Oot3dTitleIntroRecordCFrameEvent& event) {
    return {
        { "progression_checked", event.progressionChecked != 0 },
        { "progression_gate_passed", event.progressionGatePassed != 0 },
        { "stable_previous_sample", event.stablePreviousSample != 0 },
        { "current_slot_missing", event.currentSlotMissing != 0 },
        { "record_byte0_written", event.recordByte0Written != 0 },
        { "record_byte0_value", event.recordByte0Value },
        { "cursor_incremented", event.cursorIncremented != 0 },
        { "cursor_wrapped_to_one", event.cursorWrappedToOne != 0 },
        { "mode_ff_reset_to_zero", event.modeFFResetToZero != 0 },
        { "record_c_before", { event.recordCBefore[0], event.recordCBefore[1], event.recordCBefore[2] } },
        { "record_c_after", { event.recordCAfter[0], event.recordCAfter[1], event.recordCAfter[2] } },
        { "history_commit",
          {
              { "requested", event.historyCommit.requested != 0 },
              { "forced", event.historyCommit.forced != 0 },
              { "mode1_buffer_selected", event.historyCommit.mode1BufferSelected != 0 },
              { "previous_slot", event.historyCommit.previousSlot },
              { "duration_delta", event.historyCommit.durationDelta },
              { "pending_index_before", event.historyCommit.pendingIndexBefore },
              { "pending_index_after", event.historyCommit.pendingIndexAfter },
              { "mode2_pattern_scan_requested", event.historyCommit.mode2PatternScanRequested != 0 },
              { "pattern_matched", event.historyCommit.patternMatched != 0 },
              { "matched_pattern_index", event.historyCommit.matchedPatternIndex },
              { "global_pattern_flag_set", event.historyCommit.globalPatternFlagSet != 0 },
              { "active_clear_requested", event.historyCommit.activeClearRequested != 0 },
              { "mode_cleared", event.historyCommit.modeCleared != 0 },
          } },
    };
}

nlohmann::json TitleIntroState37EventToJson(const Oot3dTitleIntroState37Event& event) {
    return {
        { "record_byte0", event.recordByte0 },
        { "record_mode_byte1", event.recordModeByte1 },
        { "record_cursor_byte2", event.recordCursorByte2 },
        { "record_ready_for_cursor", event.recordReadyForCursor != 0 },
        { "player_byte300_written", event.playerByte300Written != 0 },
        { "player_byte300_value", event.playerByte300Value },
        { "cursor_before", event.cursorBefore },
        { "cursor_after", event.cursorAfter },
        { "cursor_incremented", event.cursorIncremented != 0 },
        { "current_apply", TitleIntroRecordByteApplyEventToJson(event.currentApply) },
        { "terminator_apply", TitleIntroRecordByteApplyEventToJson(event.terminatorApply) },
        { "external_advance_gate",
          {
              { "message_state_matched", event.externalAdvanceGate.messageStateMatched != 0 },
              { "actor_gate5_matched", event.externalAdvanceGate.actorGate5Matched != 0 },
              { "player_flag_mask_passed", event.externalAdvanceGate.playerFlagMaskPassed != 0 },
              { "fallback_byte_passed", event.externalAdvanceGate.fallbackBytePassed != 0 },
              { "passed", event.externalAdvanceGate.passed != 0 },
          } },
        { "request_player_timer_2d3", event.requestPlayerTimer2D3 != 0 },
        { "request_mode_reset_to_zero", event.requestModeResetToZero != 0 },
        { "request_state38", event.requestState38 != 0 },
        { "request_state39", event.requestState39 != 0 },
        { "blocked_on_external_advance_gate", event.blockedOnExternalAdvanceGate != 0 },
    };
}

nlohmann::json TitleIntroCutsceneRuntimeStepToJson(const Oot3dTitleIntroCutsceneRuntimeStep& step) {
    return {
        { "frame", step.frame },
        { "qdb_index_filtered", step.qdbIndexFiltered != 0 },
        { "qdb_index", step.qdbIndex },
        { "source_selector_feed_applied", step.sourceSelectorFeedApplied != 0 },
        { "source_selector_feed_status", TitleIntroSourceSelectorFeedStatusName(step.sourceSelectorFeedStatus) },
        { "source_selector_feed_status_value", step.sourceSelectorFeedStatus },
        { "source_selector_feed_event", TitleIntroSourceSelectorFeedEventToJson(step.sourceSelectorFeedEvent) },
        { "source_selector_applied", step.sourceSelectorApplied != 0 },
        { "source_selector_status", TitleIntroSourceSelectorStatusName(step.sourceSelectorStatus) },
        { "source_selector_status_value", step.sourceSelectorStatus },
        { "source_selector_event", TitleIntroSourceSelectorEventToJson(step.sourceSelectorEvent) },
        { "record_c_produced", step.recordCProduced != 0 },
        { "produced_record_c", { step.producedRecordC[0], step.producedRecordC[1], step.producedRecordC[2] } },
        { "record_c_status", TitleIntroRecordCStatusName(step.recordCStatus) },
        { "record_c_status_value", step.recordCStatus },
        { "record_c_event", TitleIntroRecordCFrameEventToJson(step.recordCEvent) },
        { "active_state37_row_count", step.activeState37RowCount },
        { "first_active_state37_row", TitleIntroLinkBoyPlayerActionRowToJson(step.firstActiveState37Row) },
        { "state37_playback_applied", step.state37PlaybackApplied != 0 },
        { "state37_status", TitleIntroPlaybackStatusName(step.state37Status) },
        { "state37_status_value", step.state37Status },
        { "state37_event", TitleIntroState37EventToJson(step.state37Event) },
    };
}

nlohmann::json SceneCutsceneSetTimeRowToJson(const Oot3dSceneCutsceneSetTimeRow* row) {
    if (row == nullptr) {
        return nullptr;
    }
    nlohmann::json rawWords = nlohmann::json::array();
    for (size_t wordIndex = 0; wordIndex < 3; ++wordIndex) {
        rawWords.push_back(row->rawWords[wordIndex]);
    }
    return {
        { "set_time_source_index", row->setTimeSourceIndex },
        { "cutscene_source_index", row->cutsceneSourceIndex },
        { "native_command_source_index", row->nativeCommandSourceIndex },
        { "setup_index", row->setupIndex },
        { "local_command_index", row->localCommandIndex },
        { "entry_index", row->entryIndex },
        { "command_offset", row->commandOffset },
        { "entry_offset", row->entryOffset },
        { "unk_00", row->unk00 },
        { "start_frame", row->startFrame },
        { "end_frame", row->endFrame },
        { "hour", row->hour },
        { "minute", row->minute },
        { "minute_plus_one", row->minutePlusOne },
        { "unused", row->unused },
        { "day_time", row->dayTime },
        { "skybox_time", row->skyboxTime },
        { "scene_path", row->scenePath },
        { "runtime_semantic", row->runtimeSemantic },
        { "raw_words", rawWords },
        { "raw_words_text", row->rawWordsText },
    };
}

nlohmann::json SceneCutsceneLightingRowToJson(const Oot3dSceneCutsceneLightingRow* row) {
    if (row == nullptr) {
        return nullptr;
    }
    return {
        { "lighting_source_index", row->lightingSourceIndex },
        { "native_command_source_index", row->nativeCommandSourceIndex },
        { "cutscene_source_index", row->cutsceneSourceIndex },
        { "setup_index", row->setupIndex },
        { "local_command_index", row->localCommandIndex },
        { "entry_index", row->entryIndex },
        { "command_offset", row->commandOffset },
        { "entry_offset", row->entryOffset },
        { "raw_light_setting_index", row->rawLightSettingIndex },
        { "target_light_setting", row->targetLightSetting },
        { "target_light_setting_resolved", row->targetLightSettingResolved != 0 },
        { "target_light_setting_is_sentinel", row->targetLightSettingIsSentinel != 0 },
        { "play_target_light_setting_offset", row->playTargetLightSettingOffset },
        { "play_blend_weight_offset", row->playBlendWeightOffset },
        { "start_frame", row->startFrame },
        { "end_frame", row->endFrame },
        { "start_frame_trigger_confirmed", row->startFrameTriggerConfirmed != 0 },
        { "scene_path", row->scenePath },
        { "runtime_semantic", row->runtimeSemantic },
        { "raw_words_text", row->rawWordsText },
    };
}

nlohmann::json SceneCutsceneMiscActionRowToJson(const Oot3dSceneCutsceneMiscActionRow* row) {
    if (row == nullptr) {
        return nullptr;
    }
    return {
        { "misc_action_source_index", row->miscActionSourceIndex },
        { "native_command_source_index", row->nativeCommandSourceIndex },
        { "cutscene_source_index", row->cutsceneSourceIndex },
        { "setup_index", row->setupIndex },
        { "local_command_index", row->localCommandIndex },
        { "entry_index", row->entryIndex },
        { "command_offset", row->commandOffset },
        { "entry_offset", row->entryOffset },
        { "action_id", row->actionId },
        { "start_frame", row->startFrame },
        { "end_frame", row->endFrame },
        { "duration_frames", row->durationFrames },
        { "start_frame_trigger_confirmed", row->startFrameTriggerConfirmed != 0 },
        { "active_window_inclusive_start_exclusive_end",
          row->activeWindowInclusiveStartExclusiveEnd != 0 },
        { "scene_path", row->scenePath },
        { "action_name", row->actionName },
        { "runtime_semantic", row->runtimeSemantic },
        { "raw_words_text", row->rawWordsText },
    };
}

nlohmann::json TitleIntroOpeningFrameCutsceneStateToJson(
    const Oot3dTitleIntroOpeningFrameRuntimeState& state) {
    const auto& runtime = state.cutsceneRuntime;
    return {
        { "record_byte_table",
          {
              { "byte_count", runtime.recordByteTable.byteCount },
              { "first_change_latch", runtime.recordByteTable.firstChangeLatch },
              { "first_change_init_count", runtime.recordByteTable.firstChangeInitCount },
              { "notify_count", runtime.recordByteTable.notifyCount },
              { "write_count", runtime.recordByteTable.writeCount },
          } },
        { "record_c_context",
          {
              { "current_slot_or_id", runtime.recordCContext.currentSlotOrId },
              { "record_source_byte", runtime.recordCContext.recordSourceByte },
              { "current_x", runtime.recordCContext.currentX },
              { "current_y", runtime.recordCContext.currentY },
              { "current_z", runtime.recordCContext.currentZ },
              { "mode_byte", runtime.recordCContext.modeByte },
              { "pending_history_index", runtime.recordCContext.pendingHistoryIndex },
              { "previous_slot", runtime.recordCContext.previousSlot },
              { "cursor_count", runtime.recordCContext.cursorCount },
              { "last_progress_time", runtime.recordCContext.lastProgressTime },
              { "current_progress_time", runtime.recordCContext.currentProgressTime },
          } },
        { "native_record_table",
          {
              { "record_a", { runtime.nativeRecordTable.recordA.byte0, runtime.nativeRecordTable.recordA.byte1,
                                runtime.nativeRecordTable.recordA.byte2 } },
              { "record_b", { runtime.nativeRecordTable.recordB.byte0, runtime.nativeRecordTable.recordB.byte1,
                                runtime.nativeRecordTable.recordB.byte2 } },
              { "record_c", { runtime.nativeRecordTable.recordC.byte0, runtime.nativeRecordTable.recordC.byte1,
                                runtime.nativeRecordTable.recordC.byte2 } },
          } },
        { "source_selector_feed",
          {
              { "active_flag", runtime.sourceSelectorFeed.activeFlag != 0 },
              { "time_counter", runtime.sourceSelectorFeed.timeCounter },
              { "countdown_override", runtime.sourceSelectorFeed.countdownOverride },
              { "request_active_mask", runtime.sourceSelectorFeed.requestActiveMask },
              { "record_b_lookup_source", runtime.sourceSelectorFeed.recordBLookupSource },
          } },
        { "native_input_context",
          {
              { "context_address", OOT3D_TITLE_INTRO_FEED_NATIVE_INPUT_CONTEXT_ADDRESS },
              { "mode_offset", OOT3D_TITLE_INTRO_FEED_NATIVE_INPUT_MODE_OFFSET },
              { "analog_source_offset", OOT3D_TITLE_INTRO_FEED_NATIVE_INPUT_ANALOG_SOURCE_OFFSET },
              { "global_mask_offset", OOT3D_TITLE_INTRO_FEED_NATIVE_INPUT_GLOBAL_MASK_OFFSET },
              { "capture_enable_offset", OOT3D_TITLE_INTRO_FEED_NATIVE_INPUT_CAPTURE_ENABLE_OFFSET },
              { "mode_discriminator", state.nativeInputContext.modeDiscriminator },
              { "analog_source_value", state.nativeInputContext.analogSourceValue },
              { "global_input_mask", state.nativeInputContext.globalInputMask },
              { "lane_input_mask", state.nativeInputContext.laneInputMask },
              { "gate_capture_enable", state.nativeInputContext.gateCaptureEnable },
              { "clear_primary_bit", state.nativeInputContext.clearPrimaryBit != 0 },
              { "analog_vector_x", state.nativeInputContext.analogVectorX },
              { "analog_vector_y", state.nativeInputContext.analogVectorY },
          } },
        { "state37",
          {
              { "cursor", runtime.state37.cursor },
              { "player_byte300", runtime.state37.playerByte300 },
              { "player_timer_2d3", runtime.state37.playerTimer2D3 },
              { "state_change_requested", runtime.state37.stateChangeRequested != 0 },
              { "next_direct_state", runtime.state37.nextDirectState },
              { "step_count", runtime.state37.stepCount },
          } },
        { "dispatch",
          {
              { "dispatch_context", state.dispatchState.dispatchContext },
              { "dispatch_gate_byte5", state.dispatchState.dispatchGateByte5 },
              { "dispatch_handle_present", state.dispatchState.dispatchHandlePresent != 0 },
              { "payload_allocator_available", state.dispatchState.payloadAllocatorAvailable != 0 },
              { "last_dispatch_code", state.dispatchState.lastDispatchCode },
              { "dispatch_count", state.dispatchState.dispatchCount },
              { "payload_bound", state.dispatchState.payloadBound != 0 },
          } },
        { "logo_input",
          {
              { "owned_by_frame_runtime", true },
              { "bound", state.logoInputBound != 0 },
              { "cutscene_source_index", state.logoInputCutsceneSourceIndex },
              { "cutscene_frame", state.logoInputCutsceneRuntime.frame },
              { "env_flag_3", state.logoInputEnvFlag3 != 0 },
              { "env_flag_4", state.logoInputEnvFlag4 != 0 },
              { "init_status", IntroCutsceneRuntimeStatusName(state.logoInputInitStatus) },
              { "init_status_value", state.logoInputInitStatus },
              { "step_status", IntroCutsceneRuntimeStatusName(state.logoInputStepStatus) },
              { "step_status_value", state.logoInputStepStatus },
              { "native_time_progression_resolved",
                state.logoInputCutsceneRuntime.nativeTimeProgressionResolved != 0 },
              { "native_time_rate", state.logoInputCutsceneRuntime.nativeTimeRate },
              { "native_time_game_speed", state.logoInputCutsceneRuntime.nativeTimeGameSpeed },
              { "native_time_base_increment", state.logoInputCutsceneRuntime.nativeTimeBaseIncrement },
              { "native_time_delta", state.logoInputCutsceneRuntime.nativeTimeDelta },
              { "native_time_night_flag", state.logoInputCutsceneRuntime.nativeTimeNightFlag != 0 },
              { "native_time_output_mirror_applied",
                state.logoInputCutsceneRuntime.nativeTimeOutputMirrorApplied != 0 },
          } },
        { "set_time",
          {
              { "resolved", state.timeResolved != 0 },
              { "day_time", state.dayTime },
              { "skybox_time", state.skyboxTime },
              { "time_start_frame", state.timeStartFrame },
              { "hour", state.timeHour },
              { "minute", state.timeMinute },
              { "source_row", SceneCutsceneSetTimeRowToJson(state.setTimeRow) },
          } },
    };
}

nlohmann::json TitleIntroGateCaptureEventToJson(const Oot3dTitleIntroGateCaptureEvent& event) {
    return {
        { "applied", event.applied != 0 },
        { "scene_mode_active", event.sceneModeActive != 0 },
        { "gate_capture_enabled", event.gateCaptureEnabled != 0 },
        { "analog_source_enabled", event.analogSourceEnabled != 0 },
        { "clear_primary_bit_applied", event.clearPrimaryBitApplied != 0 },
        { "analog_high_group_applied", event.analogHighGroupApplied != 0 },
        { "analog_low_group_applied", event.analogLowGroupApplied != 0 },
        { "analog_direct_vector_applied", event.analogDirectVectorApplied != 0 },
        { "input_mask", event.inputMask },
        { "analog_input_mask", event.analogInputMask },
        { "captured_gate_word", event.capturedGateWord },
        { "captured_vector_x", event.capturedVectorX },
        { "captured_vector_y", event.capturedVectorY },
    };
}

nlohmann::json TitleIntroNativeInputCaptureEventToJson(const Oot3dTitleIntroNativeInputCaptureEvent& event) {
    return {
        { "applied", event.applied != 0 },
        { "analog_source_enabled", event.analogSourceEnabled != 0 },
        { "global_mask_used", event.globalMaskUsed != 0 },
        { "lane_mask_used", event.laneMaskUsed != 0 },
        { "native_input_context_address", event.nativeInputContextAddress },
        { "mode_discriminator", event.modeDiscriminator },
        { "analog_source_value", event.analogSourceValue },
        { "global_input_mask", event.globalInputMask },
        { "lane_input_mask", event.laneInputMask },
        { "gate_capture_enable", event.gateCaptureEnable },
        { "scene_input_mode", event.sceneInputMode },
        { "gate_capture", TitleIntroGateCaptureEventToJson(event.gateCaptureEvent) },
    };
}

nlohmann::json TitleIntroGateCaptureOwnerRuntimeProbeToJson() {
    Oot3dTitleIntroSourceSelectorFeedState state{};
    Oot3dTitleIntroGateCaptureEvent event{};
    Oot3dTitleIntroGateCaptureInput input{};
    Oot3dTitleIntroNativeInputContext nativeContext{};
    Oot3dTitleIntroNativeInputCaptureEvent nativeEvent{};
    Oot3dTitleIntroSourceSelectorFeedStatus status;
    Oot3dTitleIntroSourceSelectorFeedStatus nativeStatus;

    Oot3d_TitleIntroSourceSelectorFeedInit(&state);
    input.sceneInputMode = OOT3D_TITLE_INTRO_FEED_CAPTURE_OWNER_SCENE_MODE_ACTIVE;
    input.gateCaptureEnabled = 1u;
    input.analogSourceEnabled = 1u;
    input.inputMask = OOT3D_TITLE_INTRO_FEED_CAPTURE_INPUT_MASK_PRIMARY |
                      OOT3D_TITLE_INTRO_FEED_CAPTURE_INPUT_MASK_SLOT11 |
                      OOT3D_TITLE_INTRO_FEED_CAPTURE_INPUT_MASK_SLOT9 |
                      OOT3D_TITLE_INTRO_FEED_CAPTURE_INPUT_MASK_SLOT2 |
                      OOT3D_TITLE_INTRO_FEED_CAPTURE_INPUT_MASK_SLOT5;
    input.analogInputMask =
                      OOT3D_TITLE_INTRO_FEED_CAPTURE_INPUT_MASK_ANALOG_HIGH;
    input.analogVectorX = -12;
    input.analogVectorY = 23;
    status = Oot3d_TitleIntroSourceSelectorFeedCaptureFromNativeInput(&state, &input, &event);

    Oot3d_TitleIntroSourceSelectorFeedInit(&state);
    nativeContext.modeDiscriminator = 3u;
    nativeContext.analogSourceValue = 1u;
    nativeContext.globalInputMask = input.inputMask;
    nativeContext.laneInputMask = input.analogInputMask;
    nativeContext.gateCaptureEnable = 1u;
    nativeContext.analogVectorX = input.analogVectorX;
    nativeContext.analogVectorY = input.analogVectorY;
    nativeStatus = Oot3d_TitleIntroSourceSelectorFeedCaptureFromNativeInputContext(
        &state,
        &nativeContext,
        &nativeEvent
    );

    return {
        { "capture_owner_entry", OOT3D_TITLE_INTRO_FEED_CAPTURE_OWNER_ENTRY },
        { "capture_helper_entry", OOT3D_TITLE_INTRO_FEED_CAPTURE_ENTRY },
        { "native_input_context_address", OOT3D_TITLE_INTRO_FEED_NATIVE_INPUT_CONTEXT_ADDRESS },
        { "status", TitleIntroSourceSelectorFeedStatusName(status) },
        { "status_value", status },
        { "event", TitleIntroGateCaptureEventToJson(event) },
        { "native_context_status", TitleIntroSourceSelectorFeedStatusName(nativeStatus) },
        { "native_context_status_value", nativeStatus },
        { "native_context_event", TitleIntroNativeInputCaptureEventToJson(nativeEvent) },
        { "feed_gate_saved_word_source", state.gateSavedWordSource },
        { "feed_latched_vector_x", state.latchedVectorX },
        { "feed_latched_vector_y", state.latchedVectorY },
    };
}

bool TitleIntroGateCaptureOwnerRuntimeDecoded() {
    Oot3dTitleIntroSourceSelectorFeedState state{};
    Oot3dTitleIntroGateCaptureEvent event{};
    Oot3dTitleIntroGateCaptureInput input{};
    Oot3dTitleIntroNativeInputContext nativeContext{};
    Oot3dTitleIntroNativeInputCaptureEvent nativeEvent{};
    Oot3dTitleIntroSourceSelectorFeedStatus status;

    Oot3d_TitleIntroSourceSelectorFeedInit(&state);
    input.sceneInputMode = OOT3D_TITLE_INTRO_FEED_CAPTURE_OWNER_SCENE_MODE_ACTIVE;
    input.gateCaptureEnabled = 1u;
    input.analogSourceEnabled = 1u;
    input.inputMask = OOT3D_TITLE_INTRO_FEED_CAPTURE_INPUT_MASK_PRIMARY |
                      OOT3D_TITLE_INTRO_FEED_CAPTURE_INPUT_MASK_SLOT11 |
                      OOT3D_TITLE_INTRO_FEED_CAPTURE_INPUT_MASK_SLOT9 |
                      OOT3D_TITLE_INTRO_FEED_CAPTURE_INPUT_MASK_SLOT2 |
                      OOT3D_TITLE_INTRO_FEED_CAPTURE_INPUT_MASK_SLOT5;
    input.analogInputMask =
                      OOT3D_TITLE_INTRO_FEED_CAPTURE_INPUT_MASK_ANALOG_HIGH;
    input.analogVectorX = -12;
    input.analogVectorY = 23;
    status = Oot3d_TitleIntroSourceSelectorFeedCaptureFromNativeInput(&state, &input, &event);
    if (status != OOT3D_TITLE_INTRO_SOURCE_SELECTOR_FEED_OK ||
        state.gateSavedWordSource !=
            (OOT3D_TITLE_INTRO_SOURCE_SELECTOR_STABLE_BITS_MASK |
             OOT3D_TITLE_INTRO_SOURCE_SELECTOR_ADJUST_HIGH) ||
        state.latchedVectorX != OOT3D_TITLE_INTRO_FEED_VECTOR_CLAMP_MAX ||
        state.latchedVectorY != 23 ||
        event.analogHighGroupApplied == 0u) {
        return false;
    }

    input.inputMask = OOT3D_TITLE_INTRO_FEED_CAPTURE_INPUT_MASK_PRIMARY;
    input.analogInputMask = OOT3D_TITLE_INTRO_FEED_CAPTURE_INPUT_MASK_ANALOG_LOW;
    input.clearPrimaryBit = 1u;
    input.analogVectorX = 7;
    input.analogVectorY = -9;
    status = Oot3d_TitleIntroSourceSelectorFeedCaptureFromNativeInput(&state, &input, &event);
    return status == OOT3D_TITLE_INTRO_SOURCE_SELECTOR_FEED_OK &&
           state.gateSavedWordSource == OOT3D_TITLE_INTRO_SOURCE_SELECTOR_ADJUST_LOW &&
           state.latchedVectorX == OOT3D_TITLE_INTRO_FEED_VECTOR_CLAMP_MIN &&
           state.latchedVectorY == -9 &&
           event.clearPrimaryBitApplied != 0u &&
           event.analogLowGroupApplied != 0u &&
           ([&]() {
               Oot3d_TitleIntroSourceSelectorFeedInit(&state);
               nativeContext.modeDiscriminator = 3u;
               nativeContext.analogSourceValue = 1u;
               nativeContext.globalInputMask = OOT3D_TITLE_INTRO_FEED_CAPTURE_INPUT_MASK_PRIMARY |
                                               OOT3D_TITLE_INTRO_FEED_CAPTURE_INPUT_MASK_SLOT11 |
                                               OOT3D_TITLE_INTRO_FEED_CAPTURE_INPUT_MASK_SLOT9 |
                                               OOT3D_TITLE_INTRO_FEED_CAPTURE_INPUT_MASK_SLOT2 |
                                               OOT3D_TITLE_INTRO_FEED_CAPTURE_INPUT_MASK_SLOT5;
               nativeContext.laneInputMask = OOT3D_TITLE_INTRO_FEED_CAPTURE_INPUT_MASK_ANALOG_HIGH;
               nativeContext.gateCaptureEnable = 1u;
               nativeContext.analogVectorX = -12;
               nativeContext.analogVectorY = 23;
               status = Oot3d_TitleIntroSourceSelectorFeedCaptureFromNativeInputContext(
                   &state,
                   &nativeContext,
                   &nativeEvent
               );
               return status == OOT3D_TITLE_INTRO_SOURCE_SELECTOR_FEED_OK &&
                      nativeEvent.globalMaskUsed != 0u &&
                      nativeEvent.laneMaskUsed == 0u &&
                      state.gateSavedWordSource ==
                          (OOT3D_TITLE_INTRO_SOURCE_SELECTOR_STABLE_BITS_MASK |
                           OOT3D_TITLE_INTRO_SOURCE_SELECTOR_ADJUST_HIGH) &&
                      state.latchedVectorX == OOT3D_TITLE_INTRO_FEED_VECTOR_CLAMP_MAX &&
                      state.latchedVectorY == 23;
           })();
}

bool TitleIntroOpeningActorDirectRecordLayoutDecoded() {
    return Oot3d_TitleIntroOpeningActorRuntimeIsDirectRecordLayoutDecoded() != 0u;
}

bool TitleIntroOpeningActorMotionConsumerSplitDecoded() {
    return Oot3d_TitleIntroOpeningActorRuntimeIsMotionConsumerSplitDecoded() != 0u;
}

nlohmann::json TitleIntroOpeningActorDirectRecordLayoutRowsToJson() {
    nlohmann::json rows = nlohmann::json::array();
    for (uint32_t i = 0; i < gOot3dTitleIntroOpeningActorDirectRecordLayoutRowCount; ++i) {
        rows.push_back(TitleIntroOpeningActorDirectRecordLayoutToJson(
            &gOot3dTitleIntroOpeningActorDirectRecordLayoutRows[i]));
    }
    return rows;
}

nlohmann::json TitleIntroOpeningActorMotionCueRowsToJson() {
    nlohmann::json rows = nlohmann::json::array();
    for (uint32_t i = 0; i < gOot3dTitleIntroOpeningActorMotionCueRowCount; ++i) {
        rows.push_back(TitleIntroOpeningActorMotionCueToJson(&gOot3dTitleIntroOpeningActorMotionCueRows[i]));
    }
    return rows;
}

nlohmann::json TitleIntroOpeningActorMotionSampleToJson(
    const Oot3dTitleIntroOpeningActorMotionSample& sample) {
    return {
        { "frame", sample.frame },
        { "active", sample.active != 0 },
        { "timeline_index", sample.timelineIndex },
        { "actor_binding_index", sample.actorBindingIndex },
        { "paired_mount_actor_binding_index", sample.pairedMountActorBindingIndex },
        { "motion_ref_index", sample.motionRefIndex },
        { "player_action_ref_index", sample.playerActionRefIndex },
        { "player_action_source_index", sample.playerActionSourceIndex },
        { "action_id", sample.actionId },
        { "native_speed", sample.nativeSpeed },
        { "native_heading", sample.nativeHeading },
        { "actor_binding", TitleIntroOpeningActorBindingToJson(sample.actorBinding) },
        { "paired_mount_binding", TitleIntroOpeningActorBindingToJson(sample.pairedMountBinding) },
        { "cue", TitleIntroOpeningActorMotionCueToJson(sample.cue) },
        { "opening_player_motion_row", TitleIntroOpeningPlayerMotionRowToJson(sample.motionRow) },
        { "opening_player_motion_transform",
          TitleIntroPlayerActionTransformToJson(sample.playerActionTransform) },
        { "player_action_source_row", TitleIntroLinkBoyPlayerActionRowToJson(sample.playerActionRow) },
        { "paired_mount_cue_row", TitleIntroCueRowToJson(sample.pairedMountCueRow) },
        { "link_visual_transform", TitleIntroOpeningActorVisualTransformToJson(sample.linkVisualTransform) },
        { "paired_mount_visual_transform",
          TitleIntroOpeningActorVisualTransformToJson(sample.pairedMountVisualTransform) },
        { "paired_mount_animation", PairedMountAnimationToJson(sample.pairedMountAnimation) },
        { "motion", TitleIntroOpeningPlayerMotionResultToJson(sample.motion) },
    };
}

nlohmann::json TitleIntroOpeningLogoStateToJson(const Oot3dTitleIntroOpeningLogoState& state) {
    return {
        { "state", state.state },
        { "substate", state.substate },
        { "delay_timer", state.delayTimer },
        { "timer", state.timer },
        { "pending_transition", state.pendingTransition },
        { "copyright_alpha_step", state.copyrightAlphaStep },
        { "fade_out_alpha_step", state.fadeOutAlphaStep },
        { "selected_main_csab_index", state.selectedMainCsabIndex },
        { "main_csab_bound", state.mainCsabBound != 0 },
        { "title_anim_state_set", state.titleAnimStateSet != 0 },
        { "transition_requested", state.transitionRequested != 0 },
        { "title_text_alpha", state.titleTextAlpha },
        { "main_logo_alpha", state.mainLogoAlpha },
        { "copyright_alpha", state.copyrightAlpha },
        { "effect_alpha", state.effectAlpha },
    };
}

nlohmann::json TitleIntroOpeningLogoStepResultToJson(
    const Oot3dTitleIntroOpeningLogoStepResult& result) {
    return {
        { "applied", result.applied != 0 },
        { "delay_timer_decremented", result.delayTimerDecremented != 0 },
        { "state_changed", result.stateChanged != 0 },
        { "bound_main_csab", result.boundMainCsab != 0 },
        { "title_anim_state_set", result.titleAnimStateSet != 0 },
        { "transition_requested", result.transitionRequested != 0 },
        { "update_runtime_index", result.updateRuntimeIndex },
        { "source_update_index", result.sourceUpdateIndex },
        { "source_update_row", TitleIntroLogoUpdateRowToJson(result.sourceUpdateRow) },
    };
}

nlohmann::json TitleIntroOpeningLogoDrawComponentStateToJson(
    const Oot3dTitleIntroOpeningLogoDrawComponentState& draw) {
    nlohmann::json matrix = nlohmann::json::array();
    for (size_t rowIndex = 0; rowIndex < 4; ++rowIndex) {
        nlohmann::json matrixRow = nlohmann::json::array();
        for (size_t columnIndex = 0; columnIndex < 4; ++columnIndex) {
            matrixRow.push_back(draw.matrix[rowIndex * 4 + columnIndex]);
        }
        matrix.push_back(matrixRow);
    }
    return {
        { "draw_index", draw.drawIndex },
        { "component_index", draw.componentIndex },
        { "handle_field_offset", draw.handleFieldOffset },
        { "alpha_field_offset", draw.alphaFieldOffset },
        { "effect_alpha_field_offset", draw.effectAlphaFieldOffset },
        { "visible", draw.visible != 0 },
        { "alpha", draw.alpha },
        { "alpha_normalized", draw.alphaNormalized },
        { "effect_alpha", draw.effectAlpha },
        { "effect_alpha_normalized", draw.effectAlphaNormalized },
        { "color", { draw.colorR, draw.colorG, draw.colorB, draw.colorA } },
        { "matrix_row_major_4x4", matrix },
        { "source_draw_row", TitleIntroLogoDrawRowToJson(draw.sourceDrawRow) },
        { "source_component_row", TitleIntroLogoComponentRowToJson(draw.sourceComponentRow) },
    };
}

nlohmann::json TitleIntroOpeningLogoDrawSnapshotToJson(
    const Oot3dTitleIntroOpeningLogoDrawSnapshot& snapshot) {
    nlohmann::json draws = nlohmann::json::array();
    for (uint16_t i = 0; i < snapshot.drawCount; ++i) {
        draws.push_back(TitleIntroOpeningLogoDrawComponentStateToJson(snapshot.draws[i]));
    }
    return {
        { "orchestration_index", snapshot.orchestrationIndex },
        { "draw_count", snapshot.drawCount },
        { "draws", draws },
    };
}

nlohmann::json TitleIntroOpeningFrameRuntimeToJson(const TitleIntroPlayback& playback) {
    Oot3dTitleIntroOpeningFrameRuntimeState state{};
    Oot3dTitleIntroOpeningFrameRuntimeStep step{};
    const bool forceRisingButtonLogoState = false;
    const bool runtimeSampleFromRenderPlayback = playback.OpeningFrameRuntimeInitialized;
    auto initStatus = playback.OpeningFrameRuntimeInitStatus;
    auto stepStatus = playback.OpeningFrameRuntimeStepStatus;
    int32_t requestedFrame = TitleIntroOpeningFrameRuntimeSampleFrame(playback);
    int32_t sampleFrame = 0;
    if (runtimeSampleFromRenderPlayback) {
        state = playback.OpeningFrameRuntimeState;
        step = playback.OpeningFrameRuntimeStep;
        sampleFrame = step.frame > 0 ? step.frame : std::clamp(requestedFrame, 1, std::max(1, state.nativeEndFrame));
    } else {
        if (playback.OpeningOrchestrationRow == nullptr ||
            playback.OpeningOrchestrationIndex == OOT3D_TITLE_INTRO_OPENING_NO_ORCHESTRATION_INDEX) {
            initStatus = OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_MISSING_ORCHESTRATION;
        } else {
            initStatus = Oot3d_TitleIntroOpeningFrameRuntimeInit(
                &state,
                playback.OpeningOrchestrationIndex,
                forceRisingButtonLogoState ? 1u : 0u);
        }
        if (initStatus == OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_OK) {
            sampleFrame = std::clamp(requestedFrame, 1, std::max(1, state.nativeEndFrame));
            stepStatus = OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_UNINITIALIZED;
            for (int32_t frame = 0; frame < sampleFrame; ++frame) {
                stepStatus = Oot3d_TitleIntroOpeningFrameRuntimeStep(&state, nullptr, &step);
                if (stepStatus != OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_OK) {
                    break;
                }
            }
        }
    }
    const bool anyRenderBinding = playback.OpeningFrameRuntimeCameraUsedForRender ||
                                  playback.OpeningFrameRuntimeActorMotionUsedForRender ||
                                  playback.OpeningFrameRuntimeLogoDrawUsedForRender;
    const std::string renderBindingStatus = TitleIntroOpeningFrameRenderBindingStatus(playback);
    const bool logoInputBound = runtimeSampleFromRenderPlayback
                                    ? playback.OpeningFrameRuntimeLogoInputBound
                                    : state.logoInputBound != 0;
    const uint16_t logoInputOrchestrationIndex =
        runtimeSampleFromRenderPlayback ? playback.OpeningFrameRuntimeLogoInputOrchestrationIndex
                                        : playback.OpeningOrchestrationIndex;
    const uint16_t logoInputCutsceneSourceIndex =
        runtimeSampleFromRenderPlayback ? playback.OpeningFrameRuntimeLogoInputCutsceneSourceIndex
                                        : state.logoInputCutsceneSourceIndex;
    const int32_t logoInputCutsceneFrame =
        runtimeSampleFromRenderPlayback ? playback.OpeningFrameRuntimeLogoInputCutsceneFrame
                                        : state.logoInputCutsceneRuntime.frame;
    const uint8_t logoInputEnvFlag3 =
        runtimeSampleFromRenderPlayback ? playback.OpeningFrameRuntimeLogoInputEnvFlag3
                                        : state.logoInputEnvFlag3;
    const uint8_t logoInputEnvFlag4 =
        runtimeSampleFromRenderPlayback ? playback.OpeningFrameRuntimeLogoInputEnvFlag4
                                        : state.logoInputEnvFlag4;
    const Oot3dCutsceneIntroRuntimeStatus logoInputInitStatus =
        runtimeSampleFromRenderPlayback ? playback.OpeningFrameRuntimeLogoInputInitStatus
                                        : state.logoInputInitStatus;
    const Oot3dCutsceneIntroRuntimeStatus logoInputStepStatus =
        runtimeSampleFromRenderPlayback ? playback.OpeningFrameRuntimeLogoInputStepStatus
                                        : state.logoInputStepStatus;
    const std::string logoInputSourceStatus =
        runtimeSampleFromRenderPlayback ? playback.OpeningFrameRuntimeLogoInputSourceStatus
                                        : Oot3d_TitleIntroOpeningFrameRuntimeLogoInputSourceStatus(
                                              state.logoInputBound);
    nlohmann::json out = {
        { "decoded", initStatus == OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_OK },
        { "used_for_render", anyRenderBinding },
        { "camera_used_for_render", playback.OpeningFrameRuntimeCameraUsedForRender },
        { "actor_motion_used_for_render", playback.OpeningFrameRuntimeActorMotionUsedForRender },
        { "link_actor_transform_used_for_render", playback.OpeningFrameRuntimeLinkActorTransformUsedForRender },
        { "epona_actor_transform_used_for_render", playback.OpeningFrameRuntimeEponaActorTransformUsedForRender },
        { "mounted_link_attachment_error_valid",
          playback.OpeningFrameRuntimeMountedLinkAttachmentErrorValid },
        { "mounted_link_attachment_error_length",
          playback.OpeningFrameRuntimeMountedLinkAttachmentErrorLength },
        { "link_actor_suppressed_by_paired_mount_draw",
          playback.OpeningFrameRuntimeLinkActorSuppressedByPairedMountDraw },
        { "link_actor_draw_status", playback.OpeningFrameRuntimeLinkActorDrawStatus },
        { "link_cue_resolved_from_opening_frame_runtime", playback.LinkCueResolvedFromOpeningFrameRuntime },
        { "epona_cue_resolved_from_opening_frame_runtime", playback.EponaCueResolvedFromOpeningFrameRuntime },
        { "qdb_actor_cue_fallback_used_for_render", playback.QdbActorCueFallbackUsedForRender },
        { "logo_draw_used_for_render", playback.OpeningFrameRuntimeLogoDrawUsedForRender },
        { "logo_draw_resolved_component_count", playback.OpeningFrameRuntimeLogoDrawResolvedComponentCount },
        { "logo_draw_visible_component_count", playback.OpeningFrameRuntimeLogoDrawVisibleComponentCount },
        { "logo_draw_submitted_visual_count", playback.OpeningFrameRuntimeLogoDrawSubmittedVisualCount },
        { "logo_draw_skipped_invisible_component_count",
          playback.OpeningFrameRuntimeLogoDrawSkippedInvisibleComponentCount },
        { "logo_draw_material_color_slot", kOot3dTitleIntroLogoMaterialColorSlot },
        { "logo_draw_material_color_override_batch_count",
          playback.OpeningFrameRuntimeLogoDrawMaterialColorOverrideBatchCount },
        { "logo_draw_render_status", playback.OpeningFrameRuntimeLogoDrawRenderStatus },
        { "logo_draw_material_color_apply_supported", true },
        { "logo_draw_material_color_apply_pending",
          playback.OpeningFrameRuntimeLogoDrawVisibleComponentCount > 0 &&
              playback.OpeningFrameRuntimeLogoDrawMaterialColorOverrideBatchCount == 0 },
        { "logo_draw_render_binding_basis", kOot3dTitleIntroLogoDrawRenderBindingBasis },
        { "render_binding_status", renderBindingStatus },
        { "runtime_sample_source",
          runtimeSampleFromRenderPlayback ? "render_playback_state" : "json_diagnostic_resample" },
        { "basis", "OOT3D title-intro opening frame runtime compiled from code.bin/source-table evidence: orchestration, CMAD camera, player-action motion, direct-state record/feed playback, and EnMag logo update/draw rows" },
        { "orchestration_index", playback.OpeningOrchestrationIndex },
        { "orchestration_status", playback.OpeningOrchestrationStatus },
        { "orchestration", TitleIntroOpeningOrchestrationToJson(playback.OpeningOrchestrationRow) },
        { "force_rising_button_logo_state", forceRisingButtonLogoState },
        { "logo_input_source", logoInputSourceStatus },
        { "logo_input_bound", logoInputBound },
        { "logo_input_orchestration_index", logoInputOrchestrationIndex },
        { "logo_input_transition_handoff_applied", step.logoInputTransitionHandoffApplied != 0 },
        { "logo_input_previous_cutscene_source_index", step.logoInputPreviousCutsceneSourceIndex },
        { "logo_input_cutscene_source_index", logoInputCutsceneSourceIndex },
        { "logo_input_cutscene_frame", logoInputCutsceneFrame },
        { "logo_input_env_flag_3", logoInputEnvFlag3 != 0 },
        { "logo_input_env_flag_4", logoInputEnvFlag4 != 0 },
        { "logo_input_init_status", IntroCutsceneRuntimeStatusName(logoInputInitStatus) },
        { "logo_input_step_status", IntroCutsceneRuntimeStatusName(logoInputStepStatus) },
        { "init_status", TitleIntroOpeningFrameRuntimeStatusName(initStatus) },
        { "init_status_value", initStatus },
    };
    if (initStatus != OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_OK) {
        return out;
    }

    out["requested_frame"] = requestedFrame;
    out["sample_frame"] = sampleFrame;
    out["native_end_frame"] = state.nativeEndFrame;
    out["camera_segment_start_frame"] = state.cameraSegmentStartFrame;
    out["camera_segment_end_frame"] = state.cameraSegmentEndFrame;
    out["state_after_step"] = {
        { "frame", state.frame },
        { "complete", state.complete != 0 },
        { "time_resolved", state.timeResolved != 0 },
        { "day_time", state.dayTime },
        { "skybox_time", state.skyboxTime },
        { "time_start_frame", state.timeStartFrame },
        { "time_hour", state.timeHour },
        { "time_minute", state.timeMinute },
        { "set_time_row", SceneCutsceneSetTimeRowToJson(state.setTimeRow) },
        { "native_time_progression_resolved",
          state.logoInputCutsceneRuntime.nativeTimeProgressionResolved != 0 },
        { "native_time_source_function", OOT3D_CUTSCENE_INTRO_NATIVE_ENV_TIME_UPDATE_FUNCTION },
        { "native_time_rate_source_address", OOT3D_CUTSCENE_INTRO_NATIVE_ENV_TIME_RATE_SOURCE_ADDRESS },
        { "native_time_rate_init_function", OOT3D_CUTSCENE_INTRO_NATIVE_ENV_TIME_RATE_INIT_FUNCTION },
        { "native_time_rate", state.logoInputCutsceneRuntime.nativeTimeRate },
        { "native_time_game_speed", state.logoInputCutsceneRuntime.nativeTimeGameSpeed },
        { "native_time_base_increment", state.logoInputCutsceneRuntime.nativeTimeBaseIncrement },
        { "native_time_delta", state.logoInputCutsceneRuntime.nativeTimeDelta },
        { "native_time_night_flag", state.logoInputCutsceneRuntime.nativeTimeNightFlag != 0 },
        { "native_time_output_mirror_applied",
          state.logoInputCutsceneRuntime.nativeTimeOutputMirrorApplied != 0 },
        { "light_mode_resolved", state.lightModeResolved != 0 },
        { "light_mode_current", state.lightModeCurrent },
        { "light_mode_target", state.lightModeTarget },
        { "light_mode_blend_active", state.lightModeBlendActive != 0 },
        { "light_mode_blend_remaining", state.lightModeBlendRemaining },
        { "light_mode_blend_duration", state.lightModeBlendDuration },
        { "light_mode_blend_weight", state.lightModeBlendWeight },
        { "light_mode_start_frame", state.lightModeStartFrame },
        { "light_mode_source_action_id", state.lightModeSourceActionId },
        { "light_mode_action", SceneCutsceneMiscActionRowToJson(state.lightModeActionRow) },
        { "color_addends_resolved", state.colorAddendsResolved != 0 },
        { "color_addend_ramp_active", state.colorAddendRampActive != 0 },
        { "color_addend_source_action_id", state.colorAddendSourceActionId },
        { "color_addend_action", SceneCutsceneMiscActionRowToJson(state.colorAddendActionRow) },
        { "ambient_color_addends",
          { state.ambientColorAddends[0], state.ambientColorAddends[1],
            state.ambientColorAddends[2] } },
        { "light_color_addends",
          { state.lightColorAddends[0], state.lightColorAddends[1],
            state.lightColorAddends[2] } },
        { "fog_color_addends",
          { state.fogColorAddends[0], state.fogColorAddends[1],
            state.fogColorAddends[2] } },
        { "environment_light_setting_resolved", state.environmentLightSettingResolved != 0 },
        { "environment_light_setting_raw_index", state.environmentLightSettingRawIndex },
        { "environment_light_setting_target", state.environmentLightSettingTarget },
        { "environment_light_setting_start_frame", state.environmentLightSettingStartFrame },
        { "environment_light_setting_play_target_offset", state.environmentLightSettingPlayTargetOffset },
        { "environment_light_setting_play_blend_weight_offset",
          state.environmentLightSettingPlayBlendWeightOffset },
        { "environment_light_setting_row",
          SceneCutsceneLightingRowToJson(state.environmentLightSettingRow) },
        { "logo", TitleIntroOpeningLogoStateToJson(state.logoState) },
        { "cutscene", TitleIntroOpeningFrameCutsceneStateToJson(state) },
    };
    out["step_status"] = TitleIntroOpeningFrameRuntimeStatusName(stepStatus);
    out["step_status_value"] = stepStatus;
    if (stepStatus == OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_OK ||
        stepStatus == OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_COMPLETE) {
        out["step"] = {
            { "orchestration_index", step.orchestrationIndex },
            { "frame", step.frame },
            { "native_end_frame", step.nativeEndFrame },
            { "camera_segment_start_frame", step.cameraSegmentStartFrame },
            { "camera_segment_end_frame", step.cameraSegmentEndFrame },
            { "camera_segment_active", step.cameraSegmentActive != 0 },
            { "camera_view_available", step.cameraViewAvailable != 0 },
            { "camera_view_held", step.cameraViewHeld != 0 },
            { "complete", step.complete != 0 },
            { "orchestration", TitleIntroOpeningOrchestrationToJson(step.orchestration) },
            { "camera_status", TitleIntroOpeningCameraRuntimeStatusName(step.cameraStatus) },
            { "camera_status_value", step.cameraStatus },
            { "camera", TitleIntroOpeningCameraSampleToJson(step.camera) },
            { "actor_motion_status", TitleIntroOpeningActorSampleStatusName(step.actorMotionStatus) },
            { "actor_motion_status_value", step.actorMotionStatus },
            { "actor_motion_active", step.actorMotionActive != 0 },
            { "actor_motion", TitleIntroOpeningActorMotionSampleToJson(step.actorMotion) },
            { "cutscene_qdb_index_bound", step.cutsceneQdbIndexBound != 0 },
            { "cutscene_qdb_index", step.cutsceneQdbIndex },
            { "direct_record_layout_active", step.directRecordLayoutActive != 0 },
            { "direct_record_layout", TitleIntroOpeningActorDirectRecordLayoutToJson(step.directRecordLayout) },
            { "direct_native_input_capture_applied", step.directNativeInputCaptureApplied != 0 },
            { "direct_native_input_capture_status",
              TitleIntroSourceSelectorFeedStatusName(step.directNativeInputCaptureStatus) },
            { "direct_native_input_capture_status_value", step.directNativeInputCaptureStatus },
            { "direct_native_input_capture_event",
              TitleIntroNativeInputCaptureEventToJson(step.directNativeInputCaptureEvent) },
            { "direct_mode_open_applied", step.directModeOpenApplied != 0 },
            { "direct_mode_open_status", TitleIntroSourceSelectorFeedStatusName(step.directModeOpenStatus) },
            { "direct_mode_open_status_value", step.directModeOpenStatus },
            { "direct_mode_open_event", TitleIntroSourceSelectorFeedEventToJson(step.directModeOpenEvent) },
            { "direct_global_gate_open_applied", step.directGlobalGateOpenApplied != 0 },
            { "direct_global_gate_open_status", TitleIntroSourceSelectorFeedStatusName(step.directGlobalGateOpenStatus) },
            { "direct_global_gate_open_status_value", step.directGlobalGateOpenStatus },
            { "direct_global_gate_open_event",
              TitleIntroSourceSelectorFeedEventToJson(step.directGlobalGateOpenEvent) },
            { "direct_mode_reset_applied", step.directModeResetApplied != 0 },
            { "direct_mode_reset_status", TitleIntroSourceSelectorFeedStatusName(step.directModeResetStatus) },
            { "direct_mode_reset_status_value", step.directModeResetStatus },
            { "direct_mode_reset_event", TitleIntroSourceSelectorFeedEventToJson(step.directModeResetEvent) },
            { "cutscene_runtime_applied", step.cutsceneRuntimeApplied != 0 },
            { "cutscene_runtime_status", TitleIntroPlaybackStatusName(step.cutsceneRuntimeStatus) },
            { "cutscene_runtime_status_value", step.cutsceneRuntimeStatus },
            { "cutscene_runtime", TitleIntroCutsceneRuntimeStepToJson(step.cutsceneRuntime) },
            { "set_time_start_count", step.startSetTimeCount },
            { "set_time_applied", step.setTimeApplied != 0 },
            { "set_time_row", SceneCutsceneSetTimeRowToJson(step.startSetTime) },
            { "time_resolved", step.timeResolved != 0 },
            { "day_time", step.dayTime },
            { "skybox_time", step.skyboxTime },
            { "time_start_frame", step.timeStartFrame },
            { "time_hour", step.timeHour },
            { "time_minute", step.timeMinute },
            { "native_time_progression_applied",
              step.logoInputCutsceneStep.nativeTimeProgressionApplied != 0 },
            { "native_time_rate", step.logoInputCutsceneStep.nativeTimeRate },
            { "native_time_game_speed", step.logoInputCutsceneStep.nativeTimeGameSpeed },
            { "native_time_base_increment", step.logoInputCutsceneStep.nativeTimeBaseIncrement },
            { "native_time_delta", step.logoInputCutsceneStep.nativeTimeDelta },
            { "native_time_night_flag_before",
              step.logoInputCutsceneStep.nativeTimeNightFlagBefore != 0 },
            { "native_time_night_flag_after",
              step.logoInputCutsceneStep.nativeTimeNightFlagAfter != 0 },
            { "native_time_output_mirror_applied",
              step.logoInputCutsceneStep.nativeTimeOutputMirrorApplied != 0 },
            { "native_time_day_time_before", step.logoInputCutsceneStep.nativeTimeDayTimeBefore },
            { "native_time_day_time_after", step.logoInputCutsceneStep.nativeTimeDayTimeAfter },
            { "native_time_skybox_time_before",
              step.logoInputCutsceneStep.nativeTimeSkyboxTimeBefore },
            { "native_time_skybox_time_after",
              step.logoInputCutsceneStep.nativeTimeSkyboxTimeAfter },
            { "light_mode_resolved", step.lightModeResolved != 0 },
            { "light_mode_current", step.lightModeCurrent },
            { "light_mode_target", step.lightModeTarget },
            { "light_mode_blend_active", step.lightModeBlendActive != 0 },
            { "light_mode_blend_start_applied", step.lightModeBlendStartApplied != 0 },
            { "light_mode_blend_remaining", step.lightModeBlendRemaining },
            { "light_mode_blend_duration", step.lightModeBlendDuration },
            { "light_mode_blend_weight", step.lightModeBlendWeight },
            { "light_mode_start_frame", step.lightModeStartFrame },
            { "light_mode_source_action_id", step.lightModeSourceActionId },
            { "light_mode_action", SceneCutsceneMiscActionRowToJson(step.lightModeActionRow) },
            { "color_addends_resolved", step.colorAddendsResolved != 0 },
            { "color_addend_ramp_active", step.colorAddendRampActive != 0 },
            { "color_addend_source_action_id", step.colorAddendSourceActionId },
            { "color_addend_action", SceneCutsceneMiscActionRowToJson(step.colorAddendActionRow) },
            { "ambient_color_addends",
              { step.ambientColorAddends[0], step.ambientColorAddends[1],
                step.ambientColorAddends[2] } },
            { "light_color_addends",
              { step.lightColorAddends[0], step.lightColorAddends[1],
                step.lightColorAddends[2] } },
            { "fog_color_addends",
              { step.fogColorAddends[0], step.fogColorAddends[1],
                step.fogColorAddends[2] } },
            { "environment_light_setting_applied", step.environmentLightSettingApplied != 0 },
            { "environment_light_setting_resolved", step.environmentLightSettingResolved != 0 },
            { "environment_light_setting_raw_index", step.environmentLightSettingRawIndex },
            { "environment_light_setting_target", step.environmentLightSettingTarget },
            { "environment_light_setting_start_frame", step.environmentLightSettingStartFrame },
            { "environment_light_setting_play_target_offset", step.environmentLightSettingPlayTargetOffset },
            { "environment_light_setting_play_blend_weight_offset",
              step.environmentLightSettingPlayBlendWeightOffset },
            { "environment_light_setting_row",
              SceneCutsceneLightingRowToJson(step.environmentLightSettingRow) },
            { "logo_input",
              {
                  { "owned_by_frame_runtime", true },
                  { "bound", step.logoInputBound != 0 },
                  { "transition_handoff_applied", step.logoInputTransitionHandoffApplied != 0 },
                  { "previous_cutscene_source_index", step.logoInputPreviousCutsceneSourceIndex },
                  { "cutscene_source_index", step.logoInputCutsceneSourceIndex },
                  { "cutscene_frame", step.logoInputCutsceneFrame },
                  { "env_flag_3", step.logoInputEnvFlag3 != 0 },
                  { "env_flag_4", step.logoInputEnvFlag4 != 0 },
                  { "input_env_flag_3", step.logoInput.envFlag3 != 0 },
                  { "input_env_flag_4", step.logoInput.envFlag4 != 0 },
                  { "input_button_pressed", step.logoInput.buttonPressed != 0 },
                  { "init_status", IntroCutsceneRuntimeStatusName(step.logoInputInitStatus) },
                  { "init_status_value", step.logoInputInitStatus },
                  { "step_status", IntroCutsceneRuntimeStatusName(step.logoInputStepStatus) },
                  { "step_status_value", step.logoInputStepStatus },
                  { "cutscene_step", IntroCutsceneStepToJson(step.logoInputCutsceneStep) },
              } },
            { "logo_step_status", TitleIntroOpeningLogoRuntimeStatusName(step.logoStepStatus) },
            { "logo_step_status_value", step.logoStepStatus },
            { "logo_step_applied", step.logoStepApplied != 0 },
            { "logo_step", TitleIntroOpeningLogoStepResultToJson(step.logoStep) },
            { "logo_draw_status", TitleIntroOpeningLogoRuntimeStatusName(step.logoDrawStatus) },
            { "logo_draw_status_value", step.logoDrawStatus },
            { "logo_draw", TitleIntroOpeningLogoDrawSnapshotToJson(step.logoDraw) },
        };
    }
    return out;
}

nlohmann::json TitleIntroOpeningFrameTraceStepToJson(
    const Oot3dTitleIntroOpeningFrameRuntimeState& state,
    const Oot3dTitleIntroOpeningFrameRuntimeStep& step,
    Oot3dTitleIntroOpeningFrameRuntimeStatus stepStatus) {
    const auto& runtime = step.cutsceneRuntime;
    nlohmann::json actorMotion = {
        { "active", step.actorMotionActive != 0 },
        { "status", TitleIntroOpeningActorSampleStatusName(step.actorMotionStatus) },
        { "status_value", step.actorMotionStatus },
    };
    if (step.actorMotionActive != 0) {
        actorMotion["timeline_index"] = step.actorMotion.timelineIndex;
        actorMotion["action_id"] = step.actorMotion.actionId;
        actorMotion["native_speed"] = step.actorMotion.nativeSpeed;
        actorMotion["native_heading"] = step.actorMotion.nativeHeading;
        actorMotion["player_action_source_index"] = step.actorMotion.playerActionSourceIndex;
        actorMotion["motion_ref_index"] = step.actorMotion.motionRefIndex;
        actorMotion["opening_player_motion_row"] =
            TitleIntroOpeningPlayerMotionRowToJson(step.actorMotion.motionRow);
        actorMotion["opening_player_motion_transform"] =
            TitleIntroPlayerActionTransformToJson(step.actorMotion.playerActionTransform);
        actorMotion["link_visual_transform"] =
            TitleIntroOpeningActorVisualTransformToJson(step.actorMotion.linkVisualTransform);
        actorMotion["paired_mount_visual_transform"] =
            TitleIntroOpeningActorVisualTransformToJson(step.actorMotion.pairedMountVisualTransform);
        actorMotion["paired_mount_animation"] =
            PairedMountAnimationToJson(step.actorMotion.pairedMountAnimation);
        actorMotion["direct_record_layout_index"] =
            step.actorMotion.cue != nullptr ? nlohmann::json(step.actorMotion.cue->directRecordLayoutIndex)
                                            : nlohmann::json(nullptr);
        actorMotion["direct_state_id"] =
            step.actorMotion.cue != nullptr ? nlohmann::json(step.actorMotion.cue->directStateId)
                                            : nlohmann::json(nullptr);
        actorMotion["cue_start_frame"] =
            step.actorMotion.cue != nullptr ? nlohmann::json(step.actorMotion.cue->startFrame)
                                            : nlohmann::json(nullptr);
        actorMotion["cue_end_frame"] =
            step.actorMotion.cue != nullptr ? nlohmann::json(step.actorMotion.cue->endFrame)
                                            : nlohmann::json(nullptr);
        actorMotion["player_action_index"] =
            step.actorMotion.playerActionRow != nullptr
                ? nlohmann::json(step.actorMotion.playerActionRow->playerActionIndex)
                : nlohmann::json(nullptr);
        actorMotion["player_action_cue_id"] =
            step.actorMotion.playerActionRow != nullptr ? nlohmann::json(step.actorMotion.playerActionRow->cueId)
                                                        : nlohmann::json(nullptr);
        actorMotion["qdb_index"] =
            step.actorMotion.playerActionRow != nullptr ? nlohmann::json(step.actorMotion.playerActionRow->qdbIndex)
                                                        : nlohmann::json(nullptr);
    }

    nlohmann::json camera = {
        { "segment_active", step.cameraSegmentActive != 0 },
        { "view_available", step.cameraViewAvailable != 0 },
        { "view_held", step.cameraViewHeld != 0 },
        { "status", TitleIntroOpeningCameraRuntimeStatusName(step.cameraStatus) },
        { "status_value", step.cameraStatus },
    };
    if (step.cameraSegmentActive != 0 &&
        step.cameraStatus == OOT3D_TITLE_INTRO_OPENING_CAMERA_RUNTIME_OK) {
        camera["eye"] = Vec3ToJson(ToVec3(step.camera.view.eye));
        camera["at"] = Vec3ToJson(ToVec3(step.camera.view.at));
        camera["up"] = Vec3ToJson(ToVec3(step.camera.view.up));
        camera["fov"] = step.camera.view.fov;
        camera["camera_blob_segment_source_index"] = step.camera.cameraBlobSegmentSourceIndex;
    }

    return {
        { "frame", step.frame },
        { "step_status", TitleIntroOpeningFrameRuntimeStatusName(stepStatus) },
        { "step_status_value", stepStatus },
        { "complete", step.complete != 0 },
        { "environment",
          { { "time_resolved", step.timeResolved != 0 },
            { "day_time", step.dayTime },
            { "skybox_time", step.skyboxTime },
            { "time_start_frame", step.timeStartFrame },
            { "set_time_applied", step.setTimeApplied != 0 },
            { "set_time_row", SceneCutsceneSetTimeRowToJson(step.startSetTime) },
            { "light_mode_resolved", step.lightModeResolved != 0 },
            { "light_mode_current", step.lightModeCurrent },
            { "light_mode_target", step.lightModeTarget },
            { "light_mode_blend_active", step.lightModeBlendActive != 0 },
            { "light_mode_blend_start_applied", step.lightModeBlendStartApplied != 0 },
            { "light_mode_blend_remaining", step.lightModeBlendRemaining },
            { "light_mode_blend_duration", step.lightModeBlendDuration },
            { "light_mode_blend_weight", step.lightModeBlendWeight },
            { "light_mode_start_frame", step.lightModeStartFrame },
            { "light_mode_source_action_id", step.lightModeSourceActionId },
            { "light_mode_action", SceneCutsceneMiscActionRowToJson(step.lightModeActionRow) },
            { "color_addends_resolved", step.colorAddendsResolved != 0 },
            { "color_addend_ramp_active", step.colorAddendRampActive != 0 },
            { "color_addend_source_action_id", step.colorAddendSourceActionId },
            { "color_addend_action", SceneCutsceneMiscActionRowToJson(step.colorAddendActionRow) },
            { "ambient_color_addends",
              { step.ambientColorAddends[0], step.ambientColorAddends[1],
                step.ambientColorAddends[2] } },
            { "light_color_addends",
              { step.lightColorAddends[0], step.lightColorAddends[1],
                step.lightColorAddends[2] } },
            { "fog_color_addends",
              { step.fogColorAddends[0], step.fogColorAddends[1],
                step.fogColorAddends[2] } },
            { "environment_light_setting_applied", step.environmentLightSettingApplied != 0 },
            { "environment_light_setting_resolved", step.environmentLightSettingResolved != 0 },
            { "environment_light_setting_raw_index", step.environmentLightSettingRawIndex },
            { "environment_light_setting_target", step.environmentLightSettingTarget },
            { "environment_light_setting_start_frame", step.environmentLightSettingStartFrame },
            { "environment_light_setting_play_target_offset", step.environmentLightSettingPlayTargetOffset },
            { "environment_light_setting_play_blend_weight_offset",
              step.environmentLightSettingPlayBlendWeightOffset },
            { "environment_light_setting_row",
              SceneCutsceneLightingRowToJson(step.environmentLightSettingRow) } } },
        { "camera", camera },
        { "actor_motion", actorMotion },
        { "direct",
          {
              { "qdb_index_bound", step.cutsceneQdbIndexBound != 0 },
              { "qdb_index", step.cutsceneQdbIndex },
              { "record_layout_active", step.directRecordLayoutActive != 0 },
              { "record_layout_index",
                step.directRecordLayout != nullptr ? nlohmann::json(step.directRecordLayout->directRecordLayoutIndex)
                                                   : nlohmann::json(nullptr) },
              { "entry_direct_state_id",
                step.directRecordLayout != nullptr ? nlohmann::json(step.directRecordLayout->entryDirectStateId)
                                                   : nlohmann::json(nullptr) },
              { "consumer_direct_state_id",
                step.directRecordLayout != nullptr ? nlohmann::json(step.directRecordLayout->consumerDirectStateId)
                                                   : nlohmann::json(nullptr) },
              { "native_input_capture_applied", step.directNativeInputCaptureApplied != 0 },
              { "native_input_capture_status",
                TitleIntroSourceSelectorFeedStatusName(step.directNativeInputCaptureStatus) },
              { "native_input_capture",
                TitleIntroNativeInputCaptureEventToJson(step.directNativeInputCaptureEvent) },
              { "mode_open_applied", step.directModeOpenApplied != 0 },
              { "mode_open_status", TitleIntroSourceSelectorFeedStatusName(step.directModeOpenStatus) },
              { "mode_open_changed", step.directModeOpenEvent.modeChanged != 0 },
              { "mode_opened", step.directModeOpenEvent.modeOpened != 0 },
              { "global_gate_open_applied", step.directGlobalGateOpenApplied != 0 },
              { "global_gate_open_status", TitleIntroSourceSelectorFeedStatusName(step.directGlobalGateOpenStatus) },
              { "global_gate_changed", step.directGlobalGateOpenEvent.gateChanged != 0 },
              { "global_gate_opened", step.directGlobalGateOpenEvent.gateOpened != 0 },
              { "global_gate_byte",
                step.directRecordLayout != nullptr ? nlohmann::json(step.directRecordLayout->entryGlobalGateByte)
                                                   : nlohmann::json(nullptr) },
              { "mode_reset_applied", step.directModeResetApplied != 0 },
              { "mode_reset_status", TitleIntroSourceSelectorFeedStatusName(step.directModeResetStatus) },
              { "mode_reset_changed", step.directModeResetEvent.modeChanged != 0 },
              { "mode_closed", step.directModeResetEvent.modeClosed != 0 },
          } },
        { "cutscene",
          {
              { "runtime_applied", step.cutsceneRuntimeApplied != 0 },
              { "runtime_status", TitleIntroPlaybackStatusName(step.cutsceneRuntimeStatus) },
              { "qdb_index_filtered", runtime.qdbIndexFiltered != 0 },
              { "qdb_index", runtime.qdbIndex },
              { "feed_applied", runtime.sourceSelectorFeedApplied != 0 },
              { "feed_status", TitleIntroSourceSelectorFeedStatusName(runtime.sourceSelectorFeedStatus) },
              { "record_c_produced", runtime.recordCProduced != 0 },
              { "record_c_status", TitleIntroRecordCStatusName(runtime.recordCStatus) },
              { "record_c", { runtime.producedRecordC[0], runtime.producedRecordC[1], runtime.producedRecordC[2] } },
              { "record_c_progression_gate_passed", runtime.recordCEvent.progressionGatePassed != 0 },
              { "record_c_cursor_incremented", runtime.recordCEvent.cursorIncremented != 0 },
              { "active_state37_row_count", runtime.activeState37RowCount },
              { "first_active_state37_player_action_index",
                runtime.firstActiveState37Row != nullptr
                    ? nlohmann::json(runtime.firstActiveState37Row->playerActionIndex)
                    : nlohmann::json(nullptr) },
              { "first_active_state37_start_frame",
                runtime.firstActiveState37Row != nullptr ? nlohmann::json(runtime.firstActiveState37Row->startFrame)
                                                         : nlohmann::json(nullptr) },
              { "first_active_state37_end_frame",
                runtime.firstActiveState37Row != nullptr ? nlohmann::json(runtime.firstActiveState37Row->endFrame)
                                                         : nlohmann::json(nullptr) },
              { "state37_applied", runtime.state37PlaybackApplied != 0 },
              { "state37_status", TitleIntroPlaybackStatusName(runtime.state37Status) },
              { "state37_record_ready", runtime.state37Event.recordReadyForCursor != 0 },
              { "state37_cursor_before", runtime.state37Event.cursorBefore },
              { "state37_cursor_after", runtime.state37Event.cursorAfter },
              { "state37_request_state38", runtime.state37Event.requestState38 != 0 },
              { "state37_request_state39", runtime.state37Event.requestState39 != 0 },
              { "state37_request_mode_reset_to_zero", runtime.state37Event.requestModeResetToZero != 0 },
              { "state37_blocked_on_external_advance_gate",
                runtime.state37Event.blockedOnExternalAdvanceGate != 0 },
          } },
        { "set_time",
          {
              { "start_count", step.startSetTimeCount },
              { "applied", step.setTimeApplied != 0 },
              { "row", SceneCutsceneSetTimeRowToJson(step.startSetTime) },
              { "time_resolved", step.timeResolved != 0 },
              { "day_time", step.dayTime },
              { "skybox_time", step.skyboxTime },
              { "time_start_frame", step.timeStartFrame },
              { "hour", step.timeHour },
              { "minute", step.timeMinute },
              { "native_time_progression_applied",
                step.logoInputCutsceneStep.nativeTimeProgressionApplied != 0 },
              { "native_time_rate", step.logoInputCutsceneStep.nativeTimeRate },
              { "native_time_game_speed", step.logoInputCutsceneStep.nativeTimeGameSpeed },
              { "native_time_base_increment", step.logoInputCutsceneStep.nativeTimeBaseIncrement },
              { "native_time_delta", step.logoInputCutsceneStep.nativeTimeDelta },
              { "native_time_night_flag_before",
                step.logoInputCutsceneStep.nativeTimeNightFlagBefore != 0 },
              { "native_time_night_flag_after",
                step.logoInputCutsceneStep.nativeTimeNightFlagAfter != 0 },
              { "native_time_day_time_before", step.logoInputCutsceneStep.nativeTimeDayTimeBefore },
              { "native_time_day_time_after", step.logoInputCutsceneStep.nativeTimeDayTimeAfter },
          } },
        { "logo_input",
          {
              { "owned_by_frame_runtime", true },
              { "bound", step.logoInputBound != 0 },
              { "cutscene_source_index", step.logoInputCutsceneSourceIndex },
              { "cutscene_frame", step.logoInputCutsceneFrame },
              { "env_flag_3", step.logoInputEnvFlag3 != 0 },
              { "env_flag_4", step.logoInputEnvFlag4 != 0 },
              { "input_env_flag_3", step.logoInput.envFlag3 != 0 },
              { "input_env_flag_4", step.logoInput.envFlag4 != 0 },
              { "input_button_pressed", step.logoInput.buttonPressed != 0 },
              { "init_status", IntroCutsceneRuntimeStatusName(step.logoInputInitStatus) },
              { "init_status_value", step.logoInputInitStatus },
              { "step_status", IntroCutsceneRuntimeStatusName(step.logoInputStepStatus) },
              { "step_status_value", step.logoInputStepStatus },
          } },
        { "state_after",
          {
              { "record_c_context_mode_byte", state.cutsceneRuntime.recordCContext.modeByte },
              { "record_c_context_cursor_count", state.cutsceneRuntime.recordCContext.cursorCount },
              { "record_c_context_current_slot_or_id", state.cutsceneRuntime.recordCContext.currentSlotOrId },
              { "record_c_context_current_progress_time", state.cutsceneRuntime.recordCContext.currentProgressTime },
              { "record_c_context_last_progress_time", state.cutsceneRuntime.recordCContext.lastProgressTime },
              { "feed_active", state.cutsceneRuntime.sourceSelectorFeed.activeFlag != 0 },
              { "feed_time_counter", state.cutsceneRuntime.sourceSelectorFeed.timeCounter },
              { "feed_gate_saved_word_source", state.cutsceneRuntime.sourceSelectorFeed.gateSavedWordSource },
              { "feed_latched_vector_x", state.cutsceneRuntime.sourceSelectorFeed.latchedVectorX },
              { "feed_latched_vector_y", state.cutsceneRuntime.sourceSelectorFeed.latchedVectorY },
              { "native_input_context_mode", state.nativeInputContext.modeDiscriminator },
              { "native_input_context_global_mask", state.nativeInputContext.globalInputMask },
              { "native_input_context_lane_mask", state.nativeInputContext.laneInputMask },
              { "native_input_context_capture_enable", state.nativeInputContext.gateCaptureEnable },
              { "record_table_c",
                { state.cutsceneRuntime.nativeRecordTable.recordC.byte0,
                  state.cutsceneRuntime.nativeRecordTable.recordC.byte1,
                  state.cutsceneRuntime.nativeRecordTable.recordC.byte2 } },
              { "state37_cursor", state.cutsceneRuntime.state37.cursor },
              { "state37_step_count", state.cutsceneRuntime.state37.stepCount },
              { "state37_state_change_requested", state.cutsceneRuntime.state37.stateChangeRequested != 0 },
              { "state37_next_direct_state", state.cutsceneRuntime.state37.nextDirectState },
              { "time_resolved", state.timeResolved != 0 },
              { "day_time", state.dayTime },
              { "skybox_time", state.skyboxTime },
              { "time_start_frame", state.timeStartFrame },
              { "time_hour", state.timeHour },
              { "time_minute", state.timeMinute },
              { "set_time_row", SceneCutsceneSetTimeRowToJson(state.setTimeRow) },
              { "native_time_progression_resolved",
                state.logoInputCutsceneRuntime.nativeTimeProgressionResolved != 0 },
              { "native_time_rate", state.logoInputCutsceneRuntime.nativeTimeRate },
              { "native_time_game_speed", state.logoInputCutsceneRuntime.nativeTimeGameSpeed },
              { "native_time_base_increment", state.logoInputCutsceneRuntime.nativeTimeBaseIncrement },
              { "native_time_delta", state.logoInputCutsceneRuntime.nativeTimeDelta },
              { "native_time_night_flag", state.logoInputCutsceneRuntime.nativeTimeNightFlag != 0 },
              { "color_addends_resolved", state.colorAddendsResolved != 0 },
              { "color_addend_ramp_active", state.colorAddendRampActive != 0 },
              { "color_addend_source_action_id", state.colorAddendSourceActionId },
              { "ambient_color_addends",
                { state.ambientColorAddends[0], state.ambientColorAddends[1],
                  state.ambientColorAddends[2] } },
              { "light_color_addends",
                { state.lightColorAddends[0], state.lightColorAddends[1],
                  state.lightColorAddends[2] } },
              { "fog_color_addends",
                { state.fogColorAddends[0], state.fogColorAddends[1],
                  state.fogColorAddends[2] } },
              { "logo_input_cutscene_frame", state.logoInputCutsceneRuntime.frame },
              { "logo_input_env_flag_3", state.logoInputEnvFlag3 != 0 },
              { "logo_input_env_flag_4", state.logoInputEnvFlag4 != 0 },
          } },
        { "logo",
          {
              { "step_status", TitleIntroOpeningLogoRuntimeStatusName(step.logoStepStatus) },
              { "step_applied", step.logoStepApplied != 0 },
              { "draw_status", TitleIntroOpeningLogoRuntimeStatusName(step.logoDrawStatus) },
              { "draw_count", step.logoDraw.drawCount },
              { "state", TitleIntroOpeningLogoStateToJson(state.logoState) },
          } },
    };
}

nlohmann::json TitleIntroSceneCutsceneRowToJson(const Oot3dSceneCutsceneIntroCameraCutsceneRow* row) {
    if (row == nullptr) {
        return nullptr;
    }
    return {
        { "cutscene_source_index", row->cutsceneSourceIndex },
        { "camera_timeline_ref_start", row->cameraTimelineRefStart },
        { "camera_timeline_ref_count", row->cameraTimelineRefCount },
        { "misc_action_count", row->miscActionCount },
        { "setup_index", row->setupIndex },
        { "camera_frame_start", row->cameraFrameStart },
        { "camera_frame_end", row->cameraFrameEnd },
        { "scene_path", row->scenePath },
    };
}

nlohmann::json TitleIntroSceneCameraRowToJson(const Oot3dSceneCutsceneIntroCameraTimelineRow* row) {
    if (row == nullptr) {
        return nullptr;
    }
    return {
        { "timeline_source_index", row->introCameraTimelineSourceIndex },
        { "cutscene_source_index", row->cutsceneSourceIndex },
        { "native_command_source_index", row->nativeCommandSourceIndex },
        { "camera_blob_source_index", row->cameraBlobSourceIndex },
        { "camera_blob_segment_source_index", row->cameraBlobSegmentSourceIndex },
        { "scene_id", row->sceneId },
        { "setup_index", row->setupIndex },
        { "local_command_index", row->localCommandIndex },
        { "segment_index", row->segmentIndex },
        { "start_frame", row->startFrame },
        { "end_frame", row->endFrame },
        { "sample_frame", row->sampleFrame },
        { "scene_path", row->scenePath },
        { "strt_label", row->strtLabel },
        { "strt_curve_role", row->strtCurveRole },
    };
}

nlohmann::json TitleIntroPlaybackToJson(const TitleIntroPlayback& playback) {
    const double performanceDivisor = playback.Performance.SampleCount > 0
                                          ? static_cast<double>(playback.Performance.SampleCount)
                                          : 1.0;
    nlohmann::json horseDustContacts = nlohmann::json::array();
    for (const auto& contact : playback.HorseDustEmitterProfile.Contacts) {
        horseDustContacts.push_back({
            { "flag", contact.Flag },
            { "skeleton_node_index", contact.SkeletonNodeIndex },
            { "frame_min_exclusive", contact.FrameMinExclusive },
            { "frame_max_exclusive", contact.FrameMaxExclusive },
            { "position_jitter", contact.PositionJitter },
        });
    }
    nlohmann::json horseDustParticleStats = nullptr;
    if (!playback.NativeEffects.Particles.empty()) {
        int16_t minScale = std::numeric_limits<int16_t>::max();
        int16_t maxScale = std::numeric_limits<int16_t>::min();
        int16_t minRemainingLife = std::numeric_limits<int16_t>::max();
        int16_t maxRemainingLife = std::numeric_limits<int16_t>::min();
        uint16_t minTextureFrame = std::numeric_limits<uint16_t>::max();
        uint16_t maxTextureFrame = 0;
        uint16_t combinedFlags = 0;
        int64_t scaleSum = 0;
        for (const auto& particle : playback.NativeEffects.Particles) {
            minScale = std::min(minScale, particle.Scale);
            maxScale = std::max(maxScale, particle.Scale);
            minRemainingLife = std::min(minRemainingLife, particle.RemainingLife);
            maxRemainingLife = std::max(maxRemainingLife, particle.RemainingLife);
            minTextureFrame = std::min(minTextureFrame, particle.TextureFrame);
            maxTextureFrame = std::max(maxTextureFrame, particle.TextureFrame);
            combinedFlags = static_cast<uint16_t>(combinedFlags | particle.Flags);
            scaleSum += particle.Scale;
        }
        horseDustParticleStats = {
            { "min_scale", minScale },
            { "max_scale", maxScale },
            { "average_scale",
              static_cast<double>(scaleSum) /
                  static_cast<double>(playback.NativeEffects.Particles.size()) },
            { "min_remaining_life", minRemainingLife },
            { "max_remaining_life", maxRemainingLife },
            { "min_texture_frame", minTextureFrame },
            { "max_texture_frame", maxTextureFrame },
            { "combined_flags", combinedFlags },
        };
    }
    return {
        { "enabled", playback.Enabled },
        { "initialized", playback.Initialized },
        { "status", playback.Status },
        { "qdb_index", playback.QdbIndex },
        { "qdb_status", playback.QdbStatus },
        { "qdb_index_override_applied", playback.QdbIndexOverrideApplied },
        { "frame", playback.Frame },
        { "timeline_frame", TitleIntroTimelineFrame(playback) },
        { "frames_per_second", playback.FramesPerSecond },
        { "romfs_root", playback.RomfsRoot.string() },
        { "opening_orchestration_index", playback.OpeningOrchestrationIndex },
        { "opening_orchestration_status", playback.OpeningOrchestrationStatus },
        { "opening_orchestration", TitleIntroOpeningOrchestrationToJson(playback.OpeningOrchestrationRow) },
        { "generic_cutscene_runtime",
          {
              { "requested", playback.GenericCutsceneRuntimeRequested },
              { "program_status",
                Oot3d_SceneCutsceneFrameRuntimeStatusName(
                    playback.GenericCutsceneProgramStatus) },
              { "program_valid", playback.GenericCutsceneProgram.valid != 0 },
              { "program_actor_resource_count",
                playback.GenericCutsceneProgram.actorResourceCount },
              { "scene_id", playback.GenericCutsceneKey.sceneId },
              { "setup_index", playback.GenericCutsceneKey.setupIndex },
              { "cutscene_source_index", playback.GenericCutsceneKey.cutsceneSourceIndex },
              { "adapter_kind",
                Oot3d_SceneCutsceneFrameAdapterKindName(playback.GenericCutsceneState.adapter.kind) },
              { "adapter_index", playback.GenericCutsceneState.adapter.adapterIndex },
              { "initialized", playback.GenericCutsceneState.initialized != 0 },
              { "snapshot_valid", playback.GenericCutsceneSnapshot.valid != 0 },
              { "snapshot_frame", playback.GenericCutsceneSnapshot.frame },
              { "camera_valid", playback.GenericCutsceneSnapshot.camera.valid != 0 },
              { "camera_active", playback.GenericCutsceneSnapshot.camera.active != 0 },
              { "camera_held", playback.GenericCutsceneSnapshot.camera.held != 0 },
              { "actor_count", playback.GenericCutsceneSnapshot.actorCount },
              { "actors", GenericCutsceneActorSnapshotsToJson(playback) },
              { "environment_time_resolved",
                playback.GenericCutsceneSnapshot.environment.timeResolved != 0 },
              { "environment_light_mode_resolved",
                playback.GenericCutsceneSnapshot.environment.lightModeResolved != 0 },
              { "environment_light_setting_resolved",
                playback.GenericCutsceneSnapshot.environment.lightSettingResolved != 0 },
              { "overlay_valid", playback.GenericCutsceneSnapshot.overlayValid != 0 },
              { "overlay_draw_count", playback.GenericCutsceneSnapshot.overlayDrawCount },
              { "init_status",
                Oot3d_SceneCutsceneFrameRuntimeStatusName(playback.GenericCutsceneInitStatus) },
              { "step_status",
                Oot3d_SceneCutsceneFrameRuntimeStatusName(playback.GenericCutsceneStepStatus) },
          } },
        { "qdb_row", TitleIntroQdbRowToJson(playback.QdbRow) },
        { "link_actor", TitleIntroNativeActorToJson(playback.LinkActor) },
        { "epona_actor", TitleIntroNativeActorToJson(playback.EponaActor) },
        { "link_cue", TitleIntroCueSampleToJson(playback.LinkCue) },
        { "epona_cue", TitleIntroCueSampleToJson(playback.EponaCue) },
        { "native_effect_ss",
          {
              { "initialized", playback.NativeEffectSsInitialized },
              { "status", playback.NativeEffectSsStatus },
              { "active_particle_count", playback.NativeEffects.Particles.size() },
              { "active_particle_stats", horseDustParticleStats },
              { "horse_dust_spawn_count", playback.NativeHorseDustSpawnCount },
              { "last_update_tick", playback.NativeEffects.LastUpdateTick },
              { "render_environment_resolved",
                playback.NativeEffectSsRenderEnvironment.Resolved },
              { "render_environment_source",
                playback.NativeEffectSsRenderEnvironment.SourceStatus },
              { "render_environment_scene_color_scale",
                playback.NativeEffectSsRenderEnvironment.SceneColorScale },
              { "render_environment_ambient_color",
                {
                    { "r", playback.NativeEffectSsRenderEnvironment.AmbientColor.R },
                    { "g", playback.NativeEffectSsRenderEnvironment.AmbientColor.G },
                    { "b", playback.NativeEffectSsRenderEnvironment.AmbientColor.B },
                    { "a", playback.NativeEffectSsRenderEnvironment.AmbientColor.A },
                } },
              { "render_environment_global_color_multiplier",
                {
                    { "x", playback.NativeEffectSsRenderEnvironment.GlobalColorMultiplier.X },
                    { "y", playback.NativeEffectSsRenderEnvironment.GlobalColorMultiplier.Y },
                    { "z", playback.NativeEffectSsRenderEnvironment.GlobalColorMultiplier.Z },
                } },
              { "effect_type", playback.NativeEffects.DustProfile.EffectType },
              { "effect_descriptor_address", playback.NativeEffects.DustProfile.DescriptorAddress },
              { "texture_loaded", playback.NativeEffects.DustProfile.TextureLoaded },
              { "texture_name", playback.NativeEffects.DustProfile.TextureName },
              { "texture_ctxb_type_local_index",
                playback.NativeEffects.DustProfile.CtxbTypeLocalIndex },
              { "texture_object_id", playback.NativeEffects.DustProfile.ObjectId },
              { "texture_archive_rom_path",
                playback.NativeEffects.DustProfile.ObjectArchiveRomPath },
              { "texture_archive",
                playback.NativeEffects.DustProfile.ObjectArchivePath.string() },
              { "atlas_columns", playback.NativeEffects.DustProfile.AtlasColumns },
              { "atlas_rows", playback.NativeEffects.DustProfile.AtlasRows },
              { "texture_frame_count",
                playback.NativeEffects.DustProfile.TextureFrameCount },
              { "global_update_rate",
                playback.NativeEffects.DustProfile.GlobalUpdateRate },
              { "update_rate_scale",
                playback.NativeEffects.DustProfile.UpdateRateScale },
              { "render_scale",
                playback.NativeEffects.DustProfile.RenderScale },
              { "sprite_template_position_address",
                playback.NativeEffects.DustProfile.SpriteTemplatePositionAddress },
              { "sprite_template_half_extent",
                playback.NativeEffects.DustProfile.SpriteTemplateHalfExtent },
              { "horse_emitter_decoded", playback.HorseDustEmitterProfile.Decoded },
              { "horse_emitter_status", playback.HorseDustEmitterProfile.SourceStatus },
              { "horse_dust_spawn_profile",
                {
                    { "decoded", playback.HorseDustEmitterProfile.DustSpawn.Decoded },
                    { "source", playback.HorseDustEmitterProfile.DustSpawn.SourceStatus },
                    { "flags", playback.HorseDustEmitterProfile.DustSpawn.Flags },
                    { "scale_base", playback.HorseDustEmitterProfile.DustSpawn.ScaleBase },
                    { "scale_random_range",
                      playback.HorseDustEmitterProfile.DustSpawn.ScaleRandomRange },
                    { "scale_step_base",
                      playback.HorseDustEmitterProfile.DustSpawn.ScaleStepBase },
                    { "scale_step_random_range",
                      playback.HorseDustEmitterProfile.DustSpawn.ScaleStepRandomRange },
                    { "duration_base", playback.HorseDustEmitterProfile.DustSpawn.DurationBase },
                    { "duration_random_range",
                      playback.HorseDustEmitterProfile.DustSpawn.DurationRandomRange },
                    { "duration_to_life_scale",
                      playback.HorseDustEmitterProfile.DustSpawn.DurationToLifeScale },
                    { "duration_to_life_bias",
                      playback.HorseDustEmitterProfile.DustSpawn.DurationToLifeBias },
                } },
              { "horse_gallop_animation_indices",
                playback.HorseDustEmitterProfile.GallopAnimationIndices },
              { "horse_contacts", horseDustContacts },
              { "horse_hoof_local_offset",
                {
                    { "x", playback.HorseDustEmitterProfile.HoofLocalOffset.X },
                    { "y", playback.HorseDustEmitterProfile.HoofLocalOffset.Y },
                    { "z", playback.HorseDustEmitterProfile.HoofLocalOffset.Z },
                } },
          } },
        { "link_cue_resolved_from_opening_frame_runtime", playback.LinkCueResolvedFromOpeningFrameRuntime },
        { "epona_cue_resolved_from_opening_frame_runtime", playback.EponaCueResolvedFromOpeningFrameRuntime },
        { "qdb_actor_cue_fallback_used_for_render", playback.QdbActorCueFallbackUsedForRender },
        { "added_actor_visual_count", playback.AddedActorVisualCount },
        { "link_actor_suppressed_by_paired_mount_draw",
          playback.OpeningFrameRuntimeLinkActorSuppressedByPairedMountDraw },
        { "link_actor_draw_status", playback.OpeningFrameRuntimeLinkActorDrawStatus },
        { "mounted_link_attachment_error_valid",
          playback.OpeningFrameRuntimeMountedLinkAttachmentErrorValid },
        { "mounted_link_attachment_error_length",
          playback.OpeningFrameRuntimeMountedLinkAttachmentErrorLength },
        { "suppressed_scene_actor_visual_count", playback.SuppressedSceneActorVisualCount },
        { "runtime_environment_time_used_for_render", playback.RuntimeEnvironmentTimeUsedForRender },
        { "runtime_environment_render_rebuilt", playback.RuntimeEnvironmentRenderRebuilt },
        { "runtime_environment_rendered_day_time", playback.RuntimeEnvironmentRenderedDayTime },
        { "runtime_environment_rendered_skybox_time", playback.RuntimeEnvironmentRenderedSkyboxTime },
        { "runtime_environment_rendered_time_start_frame", playback.RuntimeEnvironmentRenderedTimeStartFrame },
        { "runtime_environment_light_mode_used_for_render",
          playback.RuntimeEnvironmentLightModeUsedForRender },
        { "runtime_environment_rendered_light_mode_current",
          playback.RuntimeEnvironmentRenderedLightModeCurrent },
        { "runtime_environment_rendered_light_mode_target",
          playback.RuntimeEnvironmentRenderedLightModeTarget },
        { "runtime_environment_rendered_light_mode_blend_active",
          playback.RuntimeEnvironmentRenderedLightModeBlendActive },
        { "runtime_environment_rendered_light_mode_blend_weight",
          playback.RuntimeEnvironmentRenderedLightModeBlendWeight },
        { "runtime_environment_rendered_light_mode_blend_remaining",
          playback.RuntimeEnvironmentRenderedLightModeBlendRemaining },
        { "runtime_environment_rendered_light_mode_blend_duration",
          playback.RuntimeEnvironmentRenderedLightModeBlendDuration },
        { "runtime_environment_rendered_light_mode_start_frame",
          playback.RuntimeEnvironmentRenderedLightModeStartFrame },
        { "runtime_environment_light_setting_used_for_render",
          playback.RuntimeEnvironmentLightSettingUsedForRender },
        { "runtime_environment_rendered_light_setting_raw_index",
          playback.RuntimeEnvironmentRenderedLightSettingRawIndex },
        { "runtime_environment_rendered_light_setting_target",
          playback.RuntimeEnvironmentRenderedLightSettingTarget },
        { "runtime_environment_rendered_light_setting_setup_index",
          playback.RuntimeEnvironmentRenderedLightSettingSetupIndex },
        { "runtime_environment_rendered_light_setting_start_frame",
          playback.RuntimeEnvironmentRenderedLightSettingStartFrame },
        { "runtime_environment_color_addends_used_for_render",
          playback.RuntimeEnvironmentColorAddendsUsedForRender },
        { "runtime_environment_rendered_ambient_color_addends",
          playback.RuntimeEnvironmentRenderedAmbientColorAddends },
        { "runtime_environment_rendered_light_color_addends",
          playback.RuntimeEnvironmentRenderedLightColorAddends },
        { "runtime_environment_rendered_fog_color_addends",
          playback.RuntimeEnvironmentRenderedFogColorAddends },
        { "runtime_environment_rendered_color_addend_source_action_id",
          playback.RuntimeEnvironmentRenderedColorAddendSourceActionId },
        { "runtime_environment_rendered_color_addend_ramp_active",
          playback.RuntimeEnvironmentRenderedColorAddendRampActive },
        { "runtime_environment_material_animation_frame_used_for_render",
          playback.RuntimeEnvironmentMaterialAnimationFrameUsedForRender },
        { "runtime_environment_rendered_material_animation_frame",
          playback.RuntimeEnvironmentRenderedMaterialAnimationFrame },
        { "runtime_environment_material_animation_frame_source",
          playback.RuntimeEnvironmentMaterialAnimationFrameSource },
        { "actor_source_mode",
          "opening_frame_runtime_link_boy_player_action_and_paired_epona_mount_actor_cue_only" },
        { "diagnostic_camera_applied", playback.DiagnosticCameraApplied },
        { "initial_scene_camera_basis", kOot3dTitleIntroInitialSceneCameraBasis },
        { "initial_scene_cutscene_source_index", playback.InitialSceneCutsceneSourceIndex },
        { "initial_scene_cutscene_frame", playback.InitialSceneCutsceneFrame },
        { "initial_scene_camera_decoded", playback.InitialSceneCameraDecoded },
        { "initial_scene_camera_applied", playback.InitialSceneCameraApplied },
        { "initial_scene_camera_status", playback.InitialSceneCameraStatus },
        { "initial_scene_cutscene_row", TitleIntroSceneCutsceneRowToJson(playback.InitialSceneCutsceneRow) },
        { "initial_scene_camera_row", TitleIntroSceneCameraRowToJson(playback.InitialSceneCameraRow) },
        { "qdb_camera_suppressed_by_initial_scene_camera", playback.QdbCameraSuppressedByInitialSceneCamera },
        { "native_camera_decoded", playback.NativeCameraDecoded },
        { "native_camera_applied", playback.NativeCameraApplied },
        { "native_camera_status", playback.NativeCameraStatus },
        { "camera_blob_segment_source_index", playback.CameraBlobSegmentSourceIndex },
        { "camera_command", TitleIntroCameraCommandToJson(playback.CameraCommandRow) },
        { "camera_blob", TitleIntroCameraBlobToJson(playback.CameraBlobRow) },
        { "camera_segment", TitleIntroCameraSegmentToJson(playback.CameraSegmentRow) },
        { "title_logo_runtime_decoded", playback.LogoRuntime.Decoded },
        { "title_logo_draw_route_decoded", playback.LogoRuntime.DrawRouteDecoded },
        { "title_logo_draw_context_decoded", playback.LogoRuntime.DrawContextDecoded },
        { "title_logo_update_route_decoded", playback.LogoRuntime.UpdateRouteDecoded },
        { "title_logo_alpha_state_simulated", playback.LogoRuntime.AlphaState.Simulated },
        { "title_logo_alpha_state_used_for_render", playback.LogoRuntime.AlphaState.UsedForRender },
        { "native_title_actor_scales_decoded",
          Oot3d_TitleIntroOpeningActorRuntimeAreTitleActorScalesDecoded(
              playback.LinkActor.ScaleRow,
              playback.EponaActor.ScaleRow) != 0u },
        { "native_title_actor_animation_rows_decoded",
          Oot3d_TitleIntroOpeningActorRuntimeIsTitleActorAnimationRowDecoded(
              playback.LinkActor.AnimationRow) != 0u &&
              Oot3d_TitleIntroOpeningActorRuntimeIsTitleActorAnimationRowDecoded(
                  playback.EponaActor.AnimationRow) != 0u },
        { "native_title_epona_motion_animation_route_decoded",
          Oot3d_TitleIntroOpeningActorRuntimeIsEponaMotionAnimationRouteDecoded(
              playback.EponaActor.MotionAnimationRow,
              playback.EponaActor.CsabName.c_str()) != 0u },
        { "native_title_horse_state_route_decoded",
          Oot3d_TitleIntroOpeningActorRuntimeIsHorseStateRouteTableDecoded() != 0u },
        { "native_title_skel_anime_timing_decoded",
          gOot3dTitleIntroSkelAnimeTimingRowCount >= 2u &&
              Oot3d_TitleIntroSourceFindSkelAnimeTimingRow(0u) != nullptr &&
              Oot3d_TitleIntroSourceFindSkelAnimeTimingRow(2u) != nullptr },
        { "native_title_mounted_player_animation_frame_decoded",
          gOot3dTitleIntroMountedPlayerAnimationFrameRowCount == 1u &&
              Oot3d_TitleIntroSourceFindMountedPlayerAnimationFrameRow(7u) != nullptr },
        { "native_title_link_boy_player_action_decoded",
          Oot3d_TitleIntroOpeningActorRuntimeIsAdultLinkPlayerActionTableDecoded() != 0u },
        { "native_title_opening_actor_motion_consumer_split_decoded",
          TitleIntroOpeningActorMotionConsumerSplitDecoded() },
        { "native_title_opening_actor_direct_record_layout_decoded",
          TitleIntroOpeningActorDirectRecordLayoutDecoded() },
        { "native_title_gate_capture_owner_runtime_decoded",
          TitleIntroGateCaptureOwnerRuntimeDecoded() },
        { "native_title_link_boy_actor_symbol_route_decoded",
          Oot3d_TitleIntroOpeningActorRuntimeIsLinkChildStateRouteTableDecoded() != 0u },
        { "native_title_link_child_state_route_decoded",
          Oot3d_TitleIntroOpeningActorRuntimeIsLinkChildStateRouteTableDecoded() != 0u },
        { "horse_state_route_rows", TitleIntroHorseStateRouteRowsToJson() },
        { "gate_capture_owner_runtime_probe", TitleIntroGateCaptureOwnerRuntimeProbeToJson() },
        { "link_boy_player_action_rows", TitleIntroLinkBoyPlayerActionRowsToJson() },
        { "opening_actor_direct_record_layout_rows", TitleIntroOpeningActorDirectRecordLayoutRowsToJson() },
        { "opening_actor_motion_cue_rows", TitleIntroOpeningActorMotionCueRowsToJson() },
        { "link_boy_actor_symbol_route_rows", TitleIntroLinkChildStateRouteRowsToJson() },
        { "link_child_state_route_rows", TitleIntroLinkChildStateRouteRowsToJson() },
        { "title_logo_runtime", TitleIntroLogoRuntimeToJson(playback.LogoRuntime) },
        { "opening_frame_runtime", TitleIntroOpeningFrameRuntimeToJson(playback) },
        { "performance",
          {
              { "sample_count", playback.Performance.SampleCount },
              { "opening_frame_runtime_average_ms",
                playback.Performance.OpeningFrameRuntimeSeconds * 1000.0 / performanceDivisor },
              { "runtime_environment_average_ms",
                playback.Performance.RuntimeEnvironmentSeconds * 1000.0 / performanceDivisor },
              { "runtime_state_average_ms",
                playback.Performance.RuntimeStateSeconds * 1000.0 / performanceDivisor },
              { "epona_visual_average_ms",
                playback.Performance.EponaVisualSeconds * 1000.0 / performanceDivisor },
              { "link_visual_average_ms",
                playback.Performance.LinkVisualSeconds * 1000.0 / performanceDivisor },
              { "camera_average_ms",
                playback.Performance.CameraSeconds * 1000.0 / performanceDivisor },
              { "actor_lighting_average_ms",
                playback.Performance.ActorLightingSeconds * 1000.0 / performanceDivisor },
              { "logo_draw_average_ms",
                playback.Performance.LogoDrawSeconds * 1000.0 / performanceDivisor },
              { "bounds_average_ms",
                playback.Performance.BoundsSeconds * 1000.0 / performanceDivisor },
              { "source", "steady_clock_title_intro_phase_accumulators" },
          } },
        { "basis", kOot3dTitleIntroRuntimeBasis },
        { "title_logo_binding_basis", kOot3dTitleIntroLogoBindingBasis },
        { "title_logo_update_basis", kOot3dTitleIntroLogoUpdateBasis },
        { "actor_animation_binding_basis", kOot3dTitleIntroActorAnimationBindingBasis },
        { "skel_anime_timing_basis", kOot3dTitleIntroSkelAnimeTimingBasis },
        { "horse_state_route_basis", kOot3dTitleIntroHorseStateRouteBasis },
        { "link_boy_player_action_basis", kOot3dTitleIntroLinkBoyPlayerActionBasis },
        { "link_boy_actor_symbol_route_basis", kOot3dTitleIntroLinkChildStateRouteBasis },
        { "link_child_state_route_basis", kOot3dTitleIntroLinkChildStateRouteBasis },
        { "actor_scale_basis", kOot3dTitleIntroActorScaleBasis },
    };
}
