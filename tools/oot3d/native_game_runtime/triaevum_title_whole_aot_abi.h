#pragma once

#include "oot3d_native_whole_aot_runtime.h"

#include <cstddef>
#include <cstdint>

namespace Oot3dNativeGame {

class NativeA32Memory;

inline constexpr uint32_t kOot3dWholeAotPluginAbiV2 = 2U;

using Oot3dWholeAotExecuteFunctionV2 = bool (*)(
    uint32_t pc, oot3d::recomp::a32::GuestState &state,
    NativeA32Memory &memory, oot3d::recomp::a32::ExecutionResult *result,
    Oot3dWholeAotStats *stats, Oot3dWholeAotExternalCall externalCall,
    uint32_t blockBudget, uint32_t *blocksConsumed,
    oot3d::recomp::a32::BlockEntryCallback blockEntry, void *blockEntryUser,
    const uint32_t *blockEntryPcs, size_t blockEntryPcCount,
    const Oot3dAotBlockEntryFilter *blockEntryFilter, bool skipFirstBlockEntry,
    uint32_t stopPc);

using Oot3dWholeAotExecutionActiveV2 = bool (*)() noexcept;
using Oot3dWholeAotObservableExitV2 = void (*)(
    uint32_t pc, const oot3d::recomp::a32::GuestState *state);

struct Oot3dWholeAotProgramV2 final {
  uint32_t AbiVersion = 0U;
  uint32_t StructSize = 0U;
  const uint32_t *EntryPoints = nullptr;
  size_t EntryPointCount = 0U;
  Oot3dWholeAotExecuteFunctionV2 Execute = nullptr;
  Oot3dWholeAotExecutionActiveV2 ExecutionActive = nullptr;
  Oot3dWholeAotObservableExitV2 ObservableExit = nullptr;
};

static_assert(sizeof(Oot3dWholeAotProgramV2) == 48U);

extern "C" const Oot3dWholeAotProgramV2 *
triaevum_title_whole_aot_query(uint32_t requestedAbi) noexcept;

} // namespace Oot3dNativeGame
