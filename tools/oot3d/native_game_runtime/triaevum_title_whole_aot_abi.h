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
  // Optional tail, gated by StructSize. Generated code uses checked memory
  // helpers from its own support library, not the host executable's helpers.
  // Version 3 includes native per-page write generations and a new fast-page
  // stride. Recompile every shard and its support library; older modules are
  // rejected rather than allowed to bypass cache invalidation.
  uint32_t MemoryWriteObservationVersion = 0U;
  // Optional tail, gated by StructSize >= 64: capability bits of the module.
  // A module built before the tail reports StructSize 56 and has none.
  uint64_t Capabilities = 0U;
};

// The module's compiled blocks honour a block-entry callback's resumable exit
// request (kOot3dAotExitRequestPc, oot3d_native_whole_aot_runtime.h). Without
// it a product patch exit must leave the module by an observable exit.
inline constexpr uint64_t kOot3dWholeAotCapabilityExitRequest = 1U;
// The smallest program a module may report (modules built before the tail).
inline constexpr uint32_t kOot3dWholeAotProgramV2MinimumSize = 56U;

static_assert(offsetof(Oot3dWholeAotProgramV2, MemoryWriteObservationVersion) == 48U);
static_assert(offsetof(Oot3dWholeAotProgramV2, Capabilities) == 56U);
static_assert(sizeof(Oot3dWholeAotProgramV2) == 64U);

extern "C"
#ifdef TRIAEVUM_TITLE_AOT_EXPORT
TRIAEVUM_TITLE_AOT_EXPORT
#endif
const Oot3dWholeAotProgramV2 *
triaevum_title_whole_aot_query(uint32_t requestedAbi) noexcept;

} // namespace Oot3dNativeGame
