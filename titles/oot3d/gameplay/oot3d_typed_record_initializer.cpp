#include "oot3d_typed_record_initializer.h"

#include <array>
#include <cstdint>
#include <limits>
#include <span>

namespace Oot3dNativeGame {
namespace {

constexpr std::uint32_t kSavedFrameSize = 2U * sizeof(std::uint32_t);
constexpr std::uint32_t kTailWordOffset = 0x48U;
constexpr std::uint32_t kLastTailWordOffset = 0x4CU;

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

Oot3dTypedRecordInitializerResult
DispatchMemzero(oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
                oot3d::recomp::a32::ExecutionResult *result,
                std::uint32_t *blocksConsumed) {
  const std::uint32_t record = state.r[0];
  if (state.r[13] < kSavedFrameSize ||
      record > std::numeric_limits<std::uint32_t>::max() -
                   kOot3dRecordInitializedPrefixSize) {
    return Oot3dTypedRecordInitializerResult::InvalidState;
  }

  const std::uint32_t frameAddress = state.r[13] - kSavedFrameSize;
  if (!memory.IsWritable(frameAddress, kSavedFrameSize) ||
      !memory.IsWritable(record, kOot3dRecordInitializedPrefixSize)) {
    return Oot3dTypedRecordInitializerResult::WriteFailure;
  }

  const std::array savedFrame{state.r[4], state.r[14]};
  if (!memory.WriteBytes(
          frameAddress,
          std::span<const std::uint8_t>(
              reinterpret_cast<const std::uint8_t *>(savedFrame.data()),
              sizeof(savedFrame))) ||
      !memory.WriteFast(record + kTailWordOffset, 0U) ||
      !memory.WriteFast(record + kLastTailWordOffset, 0U)) {
    return Oot3dTypedRecordInitializerResult::WriteFailure;
  }

  state.r[4] = record;
  state.r[13] = frameAddress;
  state.r[0] = record;
  state.r[1] = kOot3dRecordInitializerMemzeroSize;
  state.r[14] = kOot3dRecordInitializerMemzeroReturn;
  CompleteBranch(kOot3dRecordInitializerMemzeroReturn - 0x04U,
                 kOot3dRuntimeMemzeroEntry, state, result, blocksConsumed);
  return Oot3dTypedRecordInitializerResult::MemzeroDispatched;
}

Oot3dTypedRecordInitializerResult
ReturnFromMemzero(oot3d::recomp::a32::GuestState &state,
                  NativeA32Memory &memory,
                  oot3d::recomp::a32::ExecutionResult *result,
                  std::uint32_t *blocksConsumed) {
  if (state.r[13] >
      std::numeric_limits<std::uint32_t>::max() - kSavedFrameSize) {
    return Oot3dTypedRecordInitializerResult::InvalidState;
  }

  std::array<std::uint32_t, 2> savedFrame{};
  if (!memory.ReadBytes(state.r[13],
                        std::span<std::uint8_t>(
                            reinterpret_cast<std::uint8_t *>(savedFrame.data()),
                            sizeof(savedFrame)))) {
    return Oot3dTypedRecordInitializerResult::ReadFailure;
  }

  const std::uint32_t record = state.r[4];
  state.r[0] = record;
  state.r[4] = savedFrame[0];
  state.r[13] += kSavedFrameSize;
  CompleteBranch(kOot3dRecordInitializerMemzeroReturn + 0x04U, savedFrame[1],
                 state, result, blocksConsumed);
  return Oot3dTypedRecordInitializerResult::InitReturned;
}

} // namespace

Oot3dTypedRecordInitializerResult ExecuteOot3dTypedRecordInitializer(
    std::uint32_t pc, oot3d::recomp::a32::GuestState &state,
    NativeA32Memory &memory, oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed) {
  if (result == nullptr) {
    return Oot3dTypedRecordInitializerResult::NotHandled;
  }

  switch (pc) {
  case kOot3dRecordInitializerEntry:
    return DispatchMemzero(state, memory, result, blocksConsumed);
  case kOot3dRecordInitializerMemzeroReturn:
    return ReturnFromMemzero(state, memory, result, blocksConsumed);
  default:
    return Oot3dTypedRecordInitializerResult::NotHandled;
  }
}

} // namespace Oot3dNativeGame
