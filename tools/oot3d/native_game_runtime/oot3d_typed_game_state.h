#pragma once

#include "oot3d_native_a32_memory.h"
#include "recomp/a32_runtime.h"

#include <cstdint>

namespace Oot3dNativeGame {

inline constexpr std::uint32_t kOot3dGameStateUpdateOwnerEntry = 0x00417014U;
inline constexpr std::uint32_t kOot3dGameStateUpdateMainReturn = 0x00417024U;
inline constexpr std::uint32_t kOot3dGameStateMainOffset = 0x04U;
inline constexpr std::uint32_t kOot3dGameStateFrameCounterOffset = 0xF8U;

enum class Oot3dTypedGameStateResult : std::uint8_t {
  NotHandled,
  MainDispatched,
  UpdateReturned,
  ReadFailure,
  WriteFailure,
};

Oot3dTypedGameStateResult ExecuteOot3dTypedGameState(
    std::uint32_t pc, oot3d::recomp::a32::GuestState &state,
    NativeA32Memory &memory, oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed = nullptr);

} // namespace Oot3dNativeGame
