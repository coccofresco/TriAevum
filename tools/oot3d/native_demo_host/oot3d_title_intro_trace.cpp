#include "oot3d_title_intro_trace.h"

#include <algorithm>
#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "oot3d_title_intro_diagnostics.h"
#include "oot3d_title_intro_opening_frame_runtime.h"
#include "oot3d_title_intro_opening_resolver.h"
#include "oot3d_title_intro_status_names.h"

extern "C" {
#include "oot3d/title_intro_opening_frame_runtime.h"
#include "oot3d/title_intro_source_table.h"
}

nlohmann::json BuildTitleIntroOpeningFrameTrace(const Args& args) {
    Oot3dTitleIntroOpeningFrameRuntimeState state{};
    Oot3dTitleIntroOpeningFrameRuntimeStep step{};
    uint16_t orchestrationIndex = OOT3D_TITLE_INTRO_OPENING_NO_ORCHESTRATION_INDEX;
    std::string orchestrationStatus;
    std::string initialCutsceneStatus;
    const auto* orchestration =
        ResolveTitleIntroOpeningOrchestrationFromNativeTable(orchestrationIndex, orchestrationStatus);
    const auto* initialCutscene = FindTitleIntroInitialSceneCutsceneRowForOpeningOrchestration(
        orchestration,
        initialCutsceneStatus);
    const uint16_t initialCutsceneSourceIndex =
        initialCutscene != nullptr ? initialCutscene->cutsceneSourceIndex : kOot3dInvalidSourceIndex;
    uint16_t resolvedQdbIndex = OOT3D_TITLE_INTRO_STATE37_QDB_ANY;
    std::string qdbStatus;
    const auto* resolvedQdb = ResolveTitleIntroOpeningQdbRow(
        orchestration,
        args.TitleIntroQdbIndexOverride,
        args.TitleIntroQdbIndex,
        resolvedQdbIndex,
        qdbStatus);
    const auto initStatus =
        orchestration != nullptr
            ? Oot3d_TitleIntroOpeningFrameRuntimeInit(&state, orchestrationIndex, 0u)
            : OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_MISSING_ORCHESTRATION;
    const uint32_t endFrame = std::min<uint32_t>(
        args.TitleIntroTraceEndFrame,
        state.nativeEndFrame > 0 ? static_cast<uint32_t>(state.nativeEndFrame) : args.TitleIntroTraceEndFrame);
    nlohmann::json frames = nlohmann::json::array();
    uint32_t sampledFrameCount = 0;
    uint32_t directFrameCount = 0;
    uint32_t state37FrameCount = 0;
    uint32_t state37AppliedCount = 0;
    uint32_t modeOpenCount = 0;
    uint32_t modeResetCount = 0;
    uint32_t firstDirectFrame = 0;
    uint32_t firstState37Frame = 0;
    uint32_t firstState37AppliedFrame = 0;
    uint32_t firstModeResetFrame = 0;
    Oot3dTitleIntroOpeningFrameRuntimeStatus stepStatus =
        OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_UNINITIALIZED;

    if (initStatus == OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_OK) {
        for (uint32_t frame = 1; frame <= endFrame; ++frame) {
            stepStatus = Oot3d_TitleIntroOpeningFrameRuntimeStep(&state, nullptr, &step);
            if (frame >= args.TitleIntroTraceStartFrame) {
                frames.push_back(TitleIntroOpeningFrameTraceStepToJson(state, step, stepStatus));
                sampledFrameCount++;
            }
            if (step.directRecordLayoutActive != 0u) {
                if (firstDirectFrame == 0) {
                    firstDirectFrame = frame;
                }
                directFrameCount++;
            }
            if (step.cutsceneRuntime.activeState37RowCount != 0u) {
                if (firstState37Frame == 0) {
                    firstState37Frame = frame;
                }
                state37FrameCount++;
            }
            if (step.cutsceneRuntime.state37PlaybackApplied != 0u) {
                if (firstState37AppliedFrame == 0) {
                    firstState37AppliedFrame = frame;
                }
                state37AppliedCount++;
            }
            if (step.directModeOpenApplied != 0u && step.directModeOpenEvent.modeOpened != 0u) {
                modeOpenCount++;
            }
            if (step.directModeResetApplied != 0u && step.directModeResetEvent.modeClosed != 0u) {
                if (firstModeResetFrame == 0) {
                    firstModeResetFrame = frame;
                }
                modeResetCount++;
            }
            if (stepStatus != OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_OK) {
                break;
            }
        }
    }

    return {
        { "format", "oot3d_title_intro_opening_frame_trace_v1" },
        { "basis",
          "Headless OOT3D title-intro opening frame runtime trace; advances decoded orchestration, camera, player-action motion, direct-state record/feed playback, and EnMag logo update/draw rows without render-loop timing." },
        { "qdb_index", resolvedQdbIndex },
        { "qdb_status", qdbStatus },
        { "qdb_index_override_applied", args.TitleIntroQdbIndexOverride },
        { "qdb_row", TitleIntroQdbRowToJson(resolvedQdb) },
        { "requested_start_frame", args.TitleIntroTraceStartFrame },
        { "requested_end_frame", args.TitleIntroTraceEndFrame },
        { "sampled_frame_count", sampledFrameCount },
        { "init_status", TitleIntroOpeningFrameRuntimeStatusName(initStatus) },
        { "init_status_value", initStatus },
        { "final_step_status", TitleIntroOpeningFrameRuntimeStatusName(stepStatus) },
        { "final_step_status_value", stepStatus },
        { "native_end_frame", state.nativeEndFrame },
        { "initial_scene_cutscene_source_index", initialCutsceneSourceIndex },
        { "initial_scene_cutscene_status", initialCutsceneStatus },
        { "orchestration_index", orchestrationIndex },
        { "orchestration_status", orchestrationStatus },
        { "orchestration", TitleIntroOpeningOrchestrationToJson(orchestration) },
        { "logo_input_bound", state.logoInputBound != 0 },
        { "logo_input_source",
          Oot3d_TitleIntroOpeningFrameRuntimeLogoInputSourceStatus(state.logoInputBound) },
        { "logo_input_orchestration_index", orchestrationIndex },
        { "logo_input_cutscene_source_index", state.logoInputCutsceneSourceIndex },
        { "summary",
          {
              { "direct_frame_count", directFrameCount },
              { "state37_frame_count", state37FrameCount },
              { "state37_applied_count", state37AppliedCount },
              { "mode_open_count", modeOpenCount },
              { "mode_reset_count", modeResetCount },
              { "first_direct_frame", firstDirectFrame == 0 ? nlohmann::json(nullptr) : nlohmann::json(firstDirectFrame) },
              { "first_state37_frame",
                firstState37Frame == 0 ? nlohmann::json(nullptr) : nlohmann::json(firstState37Frame) },
              { "first_state37_applied_frame",
                firstState37AppliedFrame == 0 ? nlohmann::json(nullptr)
                                               : nlohmann::json(firstState37AppliedFrame) },
              { "first_mode_reset_frame",
                firstModeResetFrame == 0 ? nlohmann::json(nullptr) : nlohmann::json(firstModeResetFrame) },
          } },
        { "native_title_gate_capture_owner_runtime_decoded", TitleIntroGateCaptureOwnerRuntimeDecoded() },
        { "gate_capture_owner_runtime_probe", TitleIntroGateCaptureOwnerRuntimeProbeToJson() },
        { "frames", frames },
    };
}
