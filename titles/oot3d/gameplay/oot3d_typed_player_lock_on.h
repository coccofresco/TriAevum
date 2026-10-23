#pragma once

#include "oot3d_native_a32_memory.h"
#include "recomp/a32_runtime.h"

#include <cstdint>

namespace Oot3dNativeGame {

inline constexpr std::uint32_t kOot3dPlayerReleaseLockOnEntry = 0x00334354U;
inline constexpr std::uint32_t kOot3dPlayerLockOnActorOffset = 0x16F8U;
inline constexpr std::uint32_t kOot3dPlayerLockOnStateFlagsOffset = 0x1714U;
inline constexpr std::uint32_t kOot3dPlayerLockOnStateFlag = 0x00002000U;

enum class Oot3dTypedPlayerLockOnResult : std::uint8_t {
  NotHandled,
  Released,
  ReadFailure,
  WriteFailure,
};

Oot3dTypedPlayerLockOnResult ExecuteOot3dTypedPlayerLockOn(
    std::uint32_t pc, oot3d::recomp::a32::GuestState &state,
    NativeA32Memory &memory, oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed = nullptr);

} // namespace Oot3dNativeGame
