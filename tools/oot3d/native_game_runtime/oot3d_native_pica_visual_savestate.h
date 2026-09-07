#pragma once

#include "oot3d_native_pica_presentation_scheduler.h"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Oot3dNativeGame {

struct Oot3dPicaVisualReplayState {
    Oot3dPicaPresentationSchedulerState Scheduler;
    Oot3dPicaVisualContinuityTrackerState Continuity;
    std::optional<Oot3dPicaVisualFrame> PreviousFrame;
    std::optional<Oot3dPicaVisualFrame> LatestFrame;
    bool LatestTransitionContinuous = false;
    std::map<uint32_t, Oot3dPicaDisplayTransferSubmission>
        DisplayTransfersByOutput;
    uint64_t LastSelectedTopTransferCompletionId = 0;
    uint64_t LastSubmittedDrawId = 0;
};

bool EncodeOot3dPicaVisualReplayState(
    const Oot3dPicaVisualReplayState& state,
    std::vector<uint8_t>& output, std::string* error = nullptr);

bool DecodeOot3dPicaVisualReplayState(
    std::span<const uint8_t> bytes,
    Oot3dPicaVisualReplayState& state, std::string* error = nullptr);

} // namespace Oot3dNativeGame
