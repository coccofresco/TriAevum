#include "oot3d_typed_player_lock_on.h"

#include <cstdint>

namespace Oot3dNativeGame {

Oot3dTypedPlayerLockOnResult ExecuteOot3dTypedPlayerLockOn(
    std::uint32_t pc, oot3d::recomp::a32::GuestState &state,
    NativeA32Memory &memory, oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed) {
  if (pc != kOot3dPlayerReleaseLockOnEntry || result == nullptr) {
    return Oot3dTypedPlayerLockOnResult::NotHandled;
  }

  // The target first advances r0 by 0x1000, then addresses both fields from
  // that adjusted value. Preserve the resulting caller-visible r0 value.
  const std::uint32_t adjustedPlayer = state.r[0] + 0x1000U;
  const std::uint32_t lockOnActorAddress = adjustedPlayer + 0x6F8U;
  const std::uint32_t stateFlagsAddress = adjustedPlayer + 0x714U;

  std::uint32_t stateFlags = 0U;
  if (!memory.ReadFast(stateFlagsAddress, &stateFlags)) {
    return Oot3dTypedPlayerLockOnResult::ReadFailure;
  }
  if (!memory.IsWritable(lockOnActorAddress, sizeof(std::uint32_t)) ||
      !memory.IsWritable(stateFlagsAddress, sizeof(std::uint32_t))) {
    return Oot3dTypedPlayerLockOnResult::WriteFailure;
  }

  stateFlags &= ~kOot3dPlayerLockOnStateFlag;
  if (!memory.WriteFast(lockOnActorAddress, std::uint32_t{0}) ||
      !memory.WriteFast(stateFlagsAddress, stateFlags)) {
    return Oot3dTypedPlayerLockOnResult::WriteFailure;
  }

  state.r[0] = adjustedPlayer;
  state.r[1] = stateFlags;
  state.r[15] = state.r[14];
  *result = {
      oot3d::recomp::a32::ExitKind::Branch,
      state.r[15],
      oot3d::recomp::a32::FallbackReason::None,
      kOot3dPlayerReleaseLockOnEntry,
  };
  if (blocksConsumed != nullptr) {
    *blocksConsumed = 1U;
  }
  return Oot3dTypedPlayerLockOnResult::Released;
}

} // namespace Oot3dNativeGame
