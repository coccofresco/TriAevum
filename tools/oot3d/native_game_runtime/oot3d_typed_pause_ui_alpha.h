#pragma once

#include "oot3d_native_a32_memory.h"
#include "recomp/a32_runtime.h"

#include <cstdint>

namespace Oot3dNativeGame {

inline constexpr std::uint32_t kOot3dPauseUiUpdateDualAlphaEntry = 0x0047955CU;
inline constexpr std::uint32_t kOot3dPauseUiAlphaPauseStateReturn = 0x0047956CU;
inline constexpr std::uint32_t kOot3dPauseUiAlphaFadeOutStepReturn =
    0x004795ECU;
inline constexpr std::uint32_t kOot3dPauseUiAlphaFadeInStepReturn = 0x00479660U;

inline constexpr std::uint32_t kOot3dPauseContextGetStateEntry = 0x003695F8U;
inline constexpr std::uint32_t kOot3dPauseUiAlphaMathStepToSEntry = 0x00372AA8U;

inline constexpr std::uint32_t kOot3dPauseUiFadeTimerOffset = 0x10U;
inline constexpr std::uint32_t kOot3dPauseUiGateTimerOffset = 0x11U;
inline constexpr std::uint32_t kOot3dPauseUiPrimaryAlphaOffset = 0x12U;
inline constexpr std::uint32_t kOot3dPauseUiSecondaryAlphaOffset = 0x14U;
inline constexpr std::uint32_t kOot3dPauseUiAlphaLiteralPool = 0x004796A4U;

enum class Oot3dTypedPauseUiAlphaResult : std::uint8_t {
  NotHandled,
  PauseStateQueryDispatched,
  FirstAlphaStepDispatched,
  TailAlphaStepDispatched,
  UpdateReturned,
  InvalidState,
  ReadFailure,
  WriteFailure,
};

Oot3dTypedPauseUiAlphaResult ExecuteOot3dTypedPauseUiAlpha(
    std::uint32_t pc, oot3d::recomp::a32::GuestState &state,
    NativeA32Memory &memory, oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed = nullptr);

} // namespace Oot3dNativeGame
