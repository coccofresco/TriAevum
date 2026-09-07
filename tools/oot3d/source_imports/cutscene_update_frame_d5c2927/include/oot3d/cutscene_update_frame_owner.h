#pragma once

#include <cstddef>
#include <cstdint>

namespace Oot3dSourceCutsceneUpdateFrame {

struct CutsceneUpdateFrameLiterals {
    uint32_t SchedulerStateAddress = 0U;
    uint32_t SchedulerThreshold = 0U;
    uint32_t BackendClockTicksOffset = 0U;
    uint32_t TickScaleBits = 0U;
    uint32_t FrameRateBits = 0U;
};

const CutsceneUpdateFrameLiterals& ActiveLiterals();
void* ResolveGuestMemory(uint32_t address, size_t size);

int32_t QueryBackendClock();
void CommitBackendClock(uint32_t clockStateAddress);
int32_t ConvertBackendTicksToFrame(
    int32_t ticks, uint32_t tickScaleBits, uint32_t frameRateBits);
void Cutscene_ProcessCommands(
    uint32_t playAddress, uint32_t cutsceneContextAddress,
    uint32_t cutsceneDataAddress);

void Cutscene_UpdateFrame(
    uint32_t playAddress, uint32_t cutsceneContextAddress);

} // namespace Oot3dSourceCutsceneUpdateFrame
