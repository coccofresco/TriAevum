#pragma once

#include "oot3d_native_a32_memory.h"
#include "recomp/a32_runtime.h"

#include <cstdint>

namespace Oot3dNativeGame {

inline constexpr std::uint32_t
    kOot3dDynaResetActorInteractionIfRegisteredEntry = 0x0047AF24U;
inline constexpr std::uint32_t kOot3dDynaActorCount = 0x32U;
inline constexpr std::uint32_t kOot3dPlayDynaActorContextOffset = 0x0A98U;
inline constexpr std::uint32_t kOot3dDynaActorEntrySize = 0x6CU;
inline constexpr std::uint32_t kOot3dDynaActorEntryPointerOffset = 0x54U;
inline constexpr std::uint32_t kOot3dDynaActorFlagsOffset = 0x156CU;
inline constexpr std::uint32_t kOot3dDynaDeleteFlagsOffset = 0x151CU;
inline constexpr std::uint32_t kOot3dDynaActorInteractFlagsOffset = 0x1B8U;

enum class Oot3dTypedDynaInteractionResetResult : std::uint8_t {
  NotHandled,
  InteractionReset,
  ActorNotRegistered,
  ReadFailure,
  WriteFailure,
};

Oot3dTypedDynaInteractionResetResult ExecuteOot3dTypedDynaInteractionReset(
    std::uint32_t pc, oot3d::recomp::a32::GuestState &state,
    NativeA32Memory &memory, oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed = nullptr);

} // namespace Oot3dNativeGame
