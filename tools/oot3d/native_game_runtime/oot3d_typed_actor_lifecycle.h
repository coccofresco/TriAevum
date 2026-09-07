#pragma once

#include "oot3d_native_a32_memory.h"
#include "recomp/a32_runtime.h"

#include <cstdint>

namespace Oot3dNativeGame {

inline constexpr std::uint32_t kOot3dActorDestroyEntry = 0x002D644CU;
inline constexpr std::uint32_t kOot3dActorDestroyCallbackReturn = 0x002D646CU;
inline constexpr std::uint32_t kOot3dActorDestroyModelContextReturn =
    0x002D649CU;
inline constexpr std::uint32_t kOot3dActorDestroyOwnedSlotReturn = 0x002D64BCU;
inline constexpr std::uint32_t kOot3dActorDestroyLastOwnedSlotReturn =
    0x002D64E0U;

inline constexpr std::uint32_t kOot3dActorUpdateAllInitCallbackEntry =
    0x004615D8U;
inline constexpr std::uint32_t kOot3dActorUpdateAllInitCallbackReturn =
    0x004615E8U;
inline constexpr std::uint32_t kOot3dActorUpdateAllInitContinue = 0x00461698U;
inline constexpr std::uint32_t kOot3dActorUpdateAllUpdateCallbackEntry =
    0x004617A4U;
inline constexpr std::uint32_t kOot3dActorUpdateAllUpdateCallbackReturn =
    0x004617B4U;

inline constexpr std::uint32_t kOot3dActorInitOffset = 0x134U;
inline constexpr std::uint32_t kOot3dActorDestroyOffset = 0x138U;
inline constexpr std::uint32_t kOot3dActorUpdateOffset = 0x13CU;
inline constexpr std::uint32_t kOot3dActorModelContextOffset = 0x178U;
inline constexpr std::uint32_t kOot3dActorOwnedModelSlotsOffset = 0x17CU;
inline constexpr std::uint32_t kOot3dActorLastOwnedModelOffset = 0x194U;
inline constexpr std::uint32_t kOot3dActorDestroyStateOffset = 0x198U;

enum class Oot3dTypedActorLifecycleResult : std::uint8_t {
  NotHandled,
  InitCallbackDispatched,
  InitCallbackReturned,
  UpdateCallbackDispatched,
  DestroyCallbackDispatched,
  ResourceCallbackDispatched,
  DestroyReturned,
  InvalidState,
  ReadFailure,
  WriteFailure,
};

Oot3dTypedActorLifecycleResult ExecuteOot3dTypedActorLifecycle(
    std::uint32_t pc, oot3d::recomp::a32::GuestState &state,
    NativeA32Memory &memory, oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed = nullptr);

} // namespace Oot3dNativeGame
