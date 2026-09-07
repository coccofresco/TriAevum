#include "oot3d_typed_actor_update_records.h"

#include <array>
#include <cstdint>

namespace Oot3dNativeGame {
namespace {

void CompleteReturn(std::uint32_t entry,
                    oot3d::recomp::a32::GuestState &state,
                    oot3d::recomp::a32::ExecutionResult *result,
                    std::uint32_t *blocksConsumed) {
  state.r[15] = state.r[14];
  *result = {
      oot3d::recomp::a32::ExitKind::Branch,
      state.r[15],
      oot3d::recomp::a32::FallbackReason::None,
      entry,
  };
  if (blocksConsumed != nullptr) {
    *blocksConsumed = 1U;
  }
}

Oot3dTypedActorUpdateRecordResult InitializeDefaults(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed) {
  const std::uint32_t record = state.r[0];
  std::uint32_t defaultWord = 0U;
  if (!memory.ReadFast(kOot3dActorUpdateRecordDefaultWordLiteral,
                       &defaultWord)) {
    return Oot3dTypedActorUpdateRecordResult::ReadFailure;
  }

  constexpr std::array<std::uint32_t, 4> kByteOffsets{
      0x18U, 0x19U, 0x1AU, 0x1BU};
  constexpr std::array<std::uint32_t, 3> kWordOffsets{0x0CU, 0x08U, 0x04U};
  for (const std::uint32_t offset : kByteOffsets) {
    if (!memory.IsWritable(record + offset, sizeof(std::uint8_t))) {
      return Oot3dTypedActorUpdateRecordResult::WriteFailure;
    }
  }
  for (const std::uint32_t offset : kWordOffsets) {
    if (!memory.IsWritable(record + offset, sizeof(std::uint32_t))) {
      return Oot3dTypedActorUpdateRecordResult::WriteFailure;
    }
  }

  for (const std::uint32_t offset : kByteOffsets) {
    if (!memory.WriteFast(record + offset, std::uint8_t{0})) {
      return Oot3dTypedActorUpdateRecordResult::WriteFailure;
    }
  }
  for (const std::uint32_t offset : kWordOffsets) {
    if (!memory.WriteFast(record + offset, defaultWord)) {
      return Oot3dTypedActorUpdateRecordResult::WriteFailure;
    }
  }

  state.r[1] = 0U;
  state.vfp[0] = defaultWord;
  CompleteReturn(kOot3dActorUpdateRecordInitializeDefaultsEntry, state, result,
                 blocksConsumed);
  return Oot3dTypedActorUpdateRecordResult::InitializedDefaults;
}

Oot3dTypedActorUpdateRecordResult ClearHalfwords(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed) {
  const std::uint32_t record = state.r[0];
  constexpr std::array<std::uint32_t, 2> kHalfwordOffsets{0x04U, 0x0CU};
  for (const std::uint32_t offset : kHalfwordOffsets) {
    if (!memory.IsWritable(record + offset, sizeof(std::uint16_t))) {
      return Oot3dTypedActorUpdateRecordResult::WriteFailure;
    }
  }
  for (const std::uint32_t offset : kHalfwordOffsets) {
    if (!memory.WriteFast(record + offset, std::uint16_t{0})) {
      return Oot3dTypedActorUpdateRecordResult::WriteFailure;
    }
  }

  state.r[1] = 0U;
  CompleteReturn(kOot3dActorUpdateRecordClearHalfwordsEntry, state, result,
                 blocksConsumed);
  return Oot3dTypedActorUpdateRecordResult::ClearedHalfwords;
}

} // namespace

Oot3dTypedActorUpdateRecordResult ExecuteOot3dTypedActorUpdateRecord(
    std::uint32_t pc, oot3d::recomp::a32::GuestState &state,
    NativeA32Memory &memory, oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed) {
  if (result == nullptr) {
    return Oot3dTypedActorUpdateRecordResult::NotHandled;
  }
  switch (pc) {
  case kOot3dActorUpdateRecordInitializeDefaultsEntry:
    return InitializeDefaults(state, memory, result, blocksConsumed);
  case kOot3dActorUpdateRecordClearHalfwordsEntry:
    return ClearHalfwords(state, memory, result, blocksConsumed);
  default:
    return Oot3dTypedActorUpdateRecordResult::NotHandled;
  }
}

} // namespace Oot3dNativeGame
