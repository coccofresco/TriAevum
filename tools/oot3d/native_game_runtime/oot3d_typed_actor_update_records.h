#pragma once

#include "oot3d_native_a32_memory.h"
#include "recomp/a32_runtime.h"

#include <cstdint>

namespace Oot3dNativeGame {

inline constexpr std::uint32_t
    kOot3dActorUpdateRecordInitializeDefaultsEntry = 0x0047C938U;
inline constexpr std::uint32_t kOot3dActorUpdateRecordDefaultWordLiteral =
    0x0047C960U;
inline constexpr std::uint32_t kOot3dActorUpdateRecordClearHalfwordsEntry =
    0x0047CCDCU;

enum class Oot3dTypedActorUpdateRecordResult : std::uint8_t {
  NotHandled,
  InitializedDefaults,
  ClearedHalfwords,
  ReadFailure,
  WriteFailure,
};

Oot3dTypedActorUpdateRecordResult ExecuteOot3dTypedActorUpdateRecord(
    std::uint32_t pc, oot3d::recomp::a32::GuestState &state,
    NativeA32Memory &memory, oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed = nullptr);

} // namespace Oot3dNativeGame
