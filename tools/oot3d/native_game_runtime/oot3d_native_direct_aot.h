#pragma once

#include "oot3d_native_whole_aot_runtime.h"
#include "triaevum_title_aot_abi.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace Oot3dNativeGame {

class NativeA32Memory;

std::span<const uint32_t> Oot3dWholeAotEntryPoints() noexcept;

bool Oot3dWholeAotPluginV2Available() noexcept;
bool Oot3dWholeAotPluginExecutionActive() noexcept;
[[noreturn]] void Oot3dWholeAotPluginObservableExit(
    uint32_t pc, const oot3d::recomp::a32::GuestState &state);

bool ExecuteOot3dWholeAotFunction(
    uint32_t pc, oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result, Oot3dWholeAotStats *stats,
    Oot3dWholeAotExternalCall externalCall, uint32_t blockBudget,
    uint32_t *blocksConsumed, oot3d::recomp::a32::BlockEntryCallback blockEntry,
    void *blockEntryUser, const uint32_t *blockEntryPcs,
    size_t blockEntryPcCount, const Oot3dAotBlockEntryFilter *blockEntryFilter,
    bool skipFirstBlockEntry, uint32_t stopPc);

} // namespace Oot3dNativeGame
