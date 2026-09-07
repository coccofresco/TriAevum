#include "oot3d_typed_game_state.h"

#include <array>
#include <cstdint>
#include <span>

namespace Oot3dNativeGame {
namespace {

constexpr std::uint32_t kSavedFrameSize = 8U;

void CompleteBranch(std::uint32_t source, std::uint32_t target,
                    oot3d::recomp::a32::GuestState &state,
                    oot3d::recomp::a32::ExecutionResult *result,
                    std::uint32_t *blocksConsumed) {
  state.r[15] = target;
  *result = {
      oot3d::recomp::a32::ExitKind::Branch,
      target,
      oot3d::recomp::a32::FallbackReason::None,
      source,
  };
  if (blocksConsumed != nullptr) {
    *blocksConsumed = 1U;
  }
}

Oot3dTypedGameStateResult DispatchMain(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed) {
  const std::uint32_t gameState = state.r[0];
  const std::uint32_t frameAddress = state.r[13] - kSavedFrameSize;
  std::uint32_t main = 0U;
  if (!memory.ReadFast(gameState + kOot3dGameStateMainOffset, &main)) {
    return Oot3dTypedGameStateResult::ReadFailure;
  }
  if (!memory.IsWritable(frameAddress, kSavedFrameSize)) {
    return Oot3dTypedGameStateResult::WriteFailure;
  }

  const std::array savedFrame{state.r[4], state.r[14]};
  if (!memory.WriteBytes(
          frameAddress,
          std::span<const std::uint8_t>(
              reinterpret_cast<const std::uint8_t *>(savedFrame.data()),
              sizeof(savedFrame)))) {
    return Oot3dTypedGameStateResult::WriteFailure;
  }

  state.r[1] = main;
  state.r[4] = gameState;
  state.r[13] = frameAddress;
  state.r[14] = kOot3dGameStateUpdateMainReturn;
  CompleteBranch(kOot3dGameStateUpdateOwnerEntry, main, state, result,
                 blocksConsumed);
  return Oot3dTypedGameStateResult::MainDispatched;
}

Oot3dTypedGameStateResult ReturnFromMain(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed) {
  std::array<std::uint32_t, 2> savedFrame{};
  if (!memory.ReadBytes(
          state.r[13],
          std::span<std::uint8_t>(
              reinterpret_cast<std::uint8_t *>(savedFrame.data()),
              sizeof(savedFrame)))) {
    return Oot3dTypedGameStateResult::ReadFailure;
  }

  const std::uint32_t counterAddress =
      state.r[4] + kOot3dGameStateFrameCounterOffset;
  std::uint32_t frameCounter = 0U;
  if (!memory.ReadFast(counterAddress, &frameCounter)) {
    return Oot3dTypedGameStateResult::ReadFailure;
  }
  if (!memory.IsWritable(counterAddress, sizeof(frameCounter))) {
    return Oot3dTypedGameStateResult::WriteFailure;
  }

  ++frameCounter;
  if (!memory.WriteFast(counterAddress, frameCounter)) {
    return Oot3dTypedGameStateResult::WriteFailure;
  }

  state.r[0] = frameCounter;
  state.r[4] = savedFrame[0];
  state.r[13] += kSavedFrameSize;
  CompleteBranch(kOot3dGameStateUpdateMainReturn, savedFrame[1], state, result,
                 blocksConsumed);
  return Oot3dTypedGameStateResult::UpdateReturned;
}

} // namespace

Oot3dTypedGameStateResult ExecuteOot3dTypedGameState(
    std::uint32_t pc, oot3d::recomp::a32::GuestState &state,
    NativeA32Memory &memory, oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed) {
  if (result == nullptr) {
    return Oot3dTypedGameStateResult::NotHandled;
  }
  switch (pc) {
  case kOot3dGameStateUpdateOwnerEntry:
    return DispatchMain(state, memory, result, blocksConsumed);
  case kOot3dGameStateUpdateMainReturn:
    return ReturnFromMain(state, memory, result, blocksConsumed);
  default:
    return Oot3dTypedGameStateResult::NotHandled;
  }
}

} // namespace Oot3dNativeGame
