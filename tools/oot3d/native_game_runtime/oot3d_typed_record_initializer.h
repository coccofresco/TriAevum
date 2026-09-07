#pragma once

#include "oot3d_native_a32_memory.h"
#include "recomp/a32_runtime.h"

#include <cstdint>

namespace Oot3dNativeGame {

inline constexpr std::uint32_t kOot3dRecordInitializerEntry = 0x002FFA20U;
inline constexpr std::uint32_t kOot3dRecordInitializerMemzeroReturn =
    0x002FFA40U;
inline constexpr std::uint32_t kOot3dRuntimeMemzeroEntry = 0x00343280U;
inline constexpr std::uint32_t kOot3dRecordInitializedPrefixSize = 0x50U;
inline constexpr std::uint32_t kOot3dRecordInitializerMemzeroSize = 0x48U;

enum class Oot3dTypedRecordInitializerResult : std::uint8_t {
  NotHandled,
  MemzeroDispatched,
  InitReturned,
  InvalidState,
  ReadFailure,
  WriteFailure,
};

Oot3dTypedRecordInitializerResult ExecuteOot3dTypedRecordInitializer(
    std::uint32_t pc, oot3d::recomp::a32::GuestState &state,
    NativeA32Memory &memory, oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed = nullptr);

} // namespace Oot3dNativeGame
