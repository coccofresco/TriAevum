#pragma once

#include "oot3d_native_a32_memory.h"
#include "recomp/a32_runtime.h"

#include <cstdint>

namespace Oot3dNativeGame {

inline constexpr std::uint32_t kOot3dAudioRequestFlag100CallbackEntry =
    0x00465304U;
inline constexpr std::uint32_t kOot3dAudioRequestFlag100ReferenceAcquireReturn =
    0x00465318U;
inline constexpr std::uint32_t kOot3dAudioRequestFlag100StatusQueryReturn =
    0x00465348U;
inline constexpr std::uint32_t kOot3dAudioRequestFlag100ReferenceCleanupReturn =
    0x00465364U;

inline constexpr std::uint32_t kOot3dRendererObjectRefAssignEntry = 0x0030EE14U;
inline constexpr std::uint32_t kOot3dRendererObjectRefClearEntry = 0x0030EDE0U;
inline constexpr std::uint32_t kOot3dAudioRequestStatusQueryEntry = 0x00481580U;

enum class Oot3dTypedAudioRequestCallbackResult : std::uint8_t {
  NotHandled,
  ReferenceAcquireDispatched,
  StatusQueryDispatched,
  ReferenceCleanupDispatched,
  CallbackReturned,
  InvalidState,
  ReadFailure,
  WriteFailure,
};

Oot3dTypedAudioRequestCallbackResult ExecuteOot3dTypedAudioRequestCallback(
    std::uint32_t pc, oot3d::recomp::a32::GuestState &state,
    NativeA32Memory &memory, oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed = nullptr);

} // namespace Oot3dNativeGame
