#pragma once

#include "oot3d_native_whole_aot_runtime.h"
#include "triaevum_title_aot_abi.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace Oot3dNativeGame {

class NativeA32Memory;

std::span<const uint32_t> Oot3dWholeAotEntryPoints() noexcept;

bool Oot3dWholeAotPluginV2Available() noexcept;
bool Oot3dWholeAotPluginObservesMemoryWrites() noexcept;
bool Oot3dWholeAotPluginExecutionActive() noexcept;
// The loaded title module honours resumable exit requests (F07 option 2).
bool Oot3dWholeAotPluginExitRequestSupported() noexcept;
[[noreturn]] void Oot3dWholeAotPluginObservableExit(
    uint32_t pc, const oot3d::recomp::a32::GuestState &state);

// Observable exits leave compiled code by throwing through every generated
// frame (title-module ABI v2). The report counts them per guest PC and times
// each from the request until the title module's Execute returns: the unwind
// a non-throwing exit would remove.
struct Oot3dWholeAotObservableExitReport {
  uint64_t Exits = 0;
  uint64_t UnwindNanoseconds = 0;
  std::vector<std::pair<uint32_t, uint64_t>> ExitsByPc;
};
Oot3dWholeAotObservableExitReport Oot3dWholeAotPluginObservableExitReport();

bool ExecuteOot3dWholeAotFunction(
    uint32_t pc, oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result, Oot3dWholeAotStats *stats,
    Oot3dWholeAotExternalCall externalCall, uint32_t blockBudget,
    uint32_t *blocksConsumed, oot3d::recomp::a32::BlockEntryCallback blockEntry,
    void *blockEntryUser, const uint32_t *blockEntryPcs,
    size_t blockEntryPcCount, const Oot3dAotBlockEntryFilter *blockEntryFilter,
    bool skipFirstBlockEntry, uint32_t stopPc);

} // namespace Oot3dNativeGame
