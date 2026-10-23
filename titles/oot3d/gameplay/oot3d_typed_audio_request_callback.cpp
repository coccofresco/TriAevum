#include "oot3d_typed_audio_request_callback.h"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace Oot3dNativeGame {
namespace {

constexpr std::uint32_t kSavedFrameSize = 4U * sizeof(std::uint32_t);
constexpr std::uint32_t kRequestStateLiteral = 0x00465368U;
constexpr std::uint32_t kRequestPendingOffset = 0x04U;
constexpr std::uint32_t kRequestedSfxIdOffset = 0x28U;
constexpr std::uint32_t kObjectSfxIdOffset = 0x9CU;
constexpr std::uint32_t kAcceptedStatusMaximum = 12U;
constexpr std::uint32_t kConditionFlagsMask =
    oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagZ |
    oot3d::recomp::a32::kFlagC | oot3d::recomp::a32::kFlagV;

struct PendingByteWrite {
  std::uint32_t Address = 0U;
  std::uint8_t Value = 0U;
};

struct AudioRequestCallbackPlan {
  oot3d::recomp::a32::GuestState State;
  oot3d::recomp::a32::ExecutionResult Execution;
  std::optional<std::array<std::uint32_t, 4>> SavedFrame;
  std::uint32_t SavedFrameAddress = 0U;
  std::optional<PendingByteWrite> ByteWrite;
  Oot3dTypedAudioRequestCallbackResult Success =
      Oot3dTypedAudioRequestCallbackResult::NotHandled;
};

bool CheckedAddress(std::uint32_t base, std::uint32_t offset,
                    std::uint32_t *address) {
  if (address == nullptr ||
      base > std::numeric_limits<std::uint32_t>::max() - offset) {
    return false;
  }
  *address = base + offset;
  return true;
}

std::uint32_t SubtractConditionFlags(std::uint32_t left, std::uint32_t right) {
  const std::uint32_t value = left - right;
  std::uint32_t flags = 0U;
  if ((value & oot3d::recomp::a32::kFlagN) != 0U) {
    flags |= oot3d::recomp::a32::kFlagN;
  }
  if (value == 0U) {
    flags |= oot3d::recomp::a32::kFlagZ;
  }
  if (left >= right) {
    flags |= oot3d::recomp::a32::kFlagC;
  }
  if ((((left ^ right) & (left ^ value)) & oot3d::recomp::a32::kFlagN) != 0U) {
    flags |= oot3d::recomp::a32::kFlagV;
  }
  return flags;
}

void Compare(AudioRequestCallbackPlan &plan, std::uint32_t left,
             std::uint32_t right) {
  plan.State.cpsr = (plan.State.cpsr & ~kConditionFlagsMask) |
                    SubtractConditionFlags(left, right);
}

void CompleteBranch(AudioRequestCallbackPlan &plan, std::uint32_t source,
                    std::uint32_t target,
                    Oot3dTypedAudioRequestCallbackResult success) {
  plan.State.r[15] = target;
  plan.Execution = {
      oot3d::recomp::a32::ExitKind::Branch,
      target,
      oot3d::recomp::a32::FallbackReason::None,
      source,
  };
  plan.Success = success;
}

bool QueuePendingClear(AudioRequestCallbackPlan &plan, NativeA32Memory &memory,
                       std::uint32_t requestState) {
  std::uint32_t pendingAddress = 0U;
  if (!CheckedAddress(requestState, kRequestPendingOffset, &pendingAddress)) {
    return false;
  }
  if (!memory.IsWritable(pendingAddress, sizeof(std::uint8_t))) {
    return false;
  }
  plan.ByteWrite = PendingByteWrite{pendingAddress, 0U};
  return true;
}

bool CommitPlan(AudioRequestCallbackPlan &plan,
                oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
                oot3d::recomp::a32::ExecutionResult *result,
                std::uint32_t *blocksConsumed) {
  if (plan.SavedFrame.has_value() &&
      !memory.WriteBytes(
          plan.SavedFrameAddress,
          std::span<const std::uint8_t>(
              reinterpret_cast<const std::uint8_t *>(plan.SavedFrame->data()),
              sizeof(*plan.SavedFrame)))) {
    return false;
  }
  if (plan.ByteWrite.has_value() &&
      !memory.WriteFast(plan.ByteWrite->Address, plan.ByteWrite->Value)) {
    return false;
  }

  state = plan.State;
  *result = plan.Execution;
  if (blocksConsumed != nullptr) {
    *blocksConsumed = 1U;
  }
  return true;
}

Oot3dTypedAudioRequestCallbackResult
DispatchReferenceCleanup(AudioRequestCallbackPlan &plan) {
  plan.State.r[0] = plan.State.r[13];
  plan.State.r[14] = kOot3dAudioRequestFlag100ReferenceCleanupReturn;
  CompleteBranch(
      plan, kOot3dAudioRequestFlag100ReferenceCleanupReturn - 0x04U,
      kOot3dRendererObjectRefClearEntry,
      Oot3dTypedAudioRequestCallbackResult::ReferenceCleanupDispatched);
  return plan.Success;
}

Oot3dTypedAudioRequestCallbackResult
BeginCallback(AudioRequestCallbackPlan &plan, NativeA32Memory &memory) {
  if (plan.State.r[13] < kSavedFrameSize) {
    return Oot3dTypedAudioRequestCallbackResult::InvalidState;
  }

  const std::uint32_t frameAddress = plan.State.r[13] - kSavedFrameSize;
  if (!memory.IsWritable(frameAddress, kSavedFrameSize)) {
    return Oot3dTypedAudioRequestCallbackResult::WriteFailure;
  }

  plan.SavedFrame = std::array{plan.State.r[3], plan.State.r[4],
                               plan.State.r[5], plan.State.r[14]};
  plan.SavedFrameAddress = frameAddress;
  plan.State.r[4] = plan.State.r[0];
  plan.State.r[1] = plan.State.r[0];
  plan.State.r[13] = frameAddress;
  plan.State.r[0] = frameAddress;
  plan.State.r[14] = kOot3dAudioRequestFlag100ReferenceAcquireReturn;
  CompleteBranch(
      plan, kOot3dAudioRequestFlag100ReferenceAcquireReturn - 0x04U,
      kOot3dRendererObjectRefAssignEntry,
      Oot3dTypedAudioRequestCallbackResult::ReferenceAcquireDispatched);
  return plan.Success;
}

Oot3dTypedAudioRequestCallbackResult
ContinueAfterReferenceAcquire(AudioRequestCallbackPlan &plan,
                              NativeA32Memory &memory) {
  const std::uint32_t descriptor = plan.State.r[4];
  std::uint32_t object = 0U;
  std::uint32_t requestState = 0U;
  if (!memory.ReadFast(descriptor, &object) ||
      !memory.ReadFast(kRequestStateLiteral, &requestState)) {
    return Oot3dTypedAudioRequestCallbackResult::ReadFailure;
  }

  std::uint32_t requestedSfxIdAddress = 0U;
  if (!CheckedAddress(requestState, kRequestedSfxIdOffset,
                      &requestedSfxIdAddress)) {
    return Oot3dTypedAudioRequestCallbackResult::InvalidState;
  }

  std::uint32_t objectSfxId = std::numeric_limits<std::uint32_t>::max();
  if (object != 0U) {
    std::uint32_t objectSfxIdAddress = 0U;
    if (!CheckedAddress(object, kObjectSfxIdOffset, &objectSfxIdAddress)) {
      return Oot3dTypedAudioRequestCallbackResult::InvalidState;
    }
    if (!memory.ReadFast(objectSfxIdAddress, &objectSfxId)) {
      return Oot3dTypedAudioRequestCallbackResult::ReadFailure;
    }
  }

  std::uint32_t requestedSfxId = 0U;
  if (!memory.ReadFast(requestedSfxIdAddress, &requestedSfxId)) {
    return Oot3dTypedAudioRequestCallbackResult::ReadFailure;
  }

  plan.State.r[0] = object;
  plan.State.r[4] = requestState;
  Compare(plan, object, 0U);
  plan.State.r[0] = objectSfxId;
  plan.State.r[1] = requestedSfxId;
  Compare(plan, objectSfxId, requestedSfxId);
  if (objectSfxId != requestedSfxId) {
    return DispatchReferenceCleanup(plan);
  }

  std::uint32_t acquiredHandle = 0U;
  if (!memory.ReadFast(plan.State.r[13], &acquiredHandle)) {
    return Oot3dTypedAudioRequestCallbackResult::ReadFailure;
  }
  plan.State.r[0] = acquiredHandle;
  Compare(plan, acquiredHandle, 0U);
  if (acquiredHandle == 0U) {
    if (!QueuePendingClear(plan, memory, requestState)) {
      return Oot3dTypedAudioRequestCallbackResult::WriteFailure;
    }
    return DispatchReferenceCleanup(plan);
  }

  plan.State.r[14] = kOot3dAudioRequestFlag100StatusQueryReturn;
  CompleteBranch(plan, kOot3dAudioRequestFlag100StatusQueryReturn - 0x04U,
                 kOot3dAudioRequestStatusQueryEntry,
                 Oot3dTypedAudioRequestCallbackResult::StatusQueryDispatched);
  return plan.Success;
}

Oot3dTypedAudioRequestCallbackResult
ContinueAfterStatusQuery(AudioRequestCallbackPlan &plan,
                         NativeA32Memory &memory) {
  const std::uint32_t status = plan.State.r[0];
  Compare(plan, status, kAcceptedStatusMaximum);
  if (status <= kAcceptedStatusMaximum &&
      !QueuePendingClear(plan, memory, plan.State.r[4])) {
    return Oot3dTypedAudioRequestCallbackResult::WriteFailure;
  }
  return DispatchReferenceCleanup(plan);
}

Oot3dTypedAudioRequestCallbackResult
ReturnAfterReferenceCleanup(AudioRequestCallbackPlan &plan,
                            NativeA32Memory &memory) {
  if (plan.State.r[13] >
      std::numeric_limits<std::uint32_t>::max() - kSavedFrameSize) {
    return Oot3dTypedAudioRequestCallbackResult::InvalidState;
  }

  std::array<std::uint32_t, 4> savedFrame{};
  if (!memory.ReadBytes(plan.State.r[13],
                        std::span<std::uint8_t>(
                            reinterpret_cast<std::uint8_t *>(savedFrame.data()),
                            sizeof(savedFrame)))) {
    return Oot3dTypedAudioRequestCallbackResult::ReadFailure;
  }

  plan.State.r[3] = savedFrame[0];
  plan.State.r[4] = savedFrame[1];
  plan.State.r[5] = savedFrame[2];
  plan.State.r[13] += kSavedFrameSize;
  CompleteBranch(plan, kOot3dAudioRequestFlag100ReferenceCleanupReturn,
                 savedFrame[3],
                 Oot3dTypedAudioRequestCallbackResult::CallbackReturned);
  return plan.Success;
}

} // namespace

Oot3dTypedAudioRequestCallbackResult ExecuteOot3dTypedAudioRequestCallback(
    std::uint32_t pc, oot3d::recomp::a32::GuestState &state,
    NativeA32Memory &memory, oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed) {
  if (result == nullptr) {
    return Oot3dTypedAudioRequestCallbackResult::NotHandled;
  }

  AudioRequestCallbackPlan plan;
  plan.State = state;
  Oot3dTypedAudioRequestCallbackResult outcome =
      Oot3dTypedAudioRequestCallbackResult::NotHandled;
  switch (pc) {
  case kOot3dAudioRequestFlag100CallbackEntry:
    outcome = BeginCallback(plan, memory);
    break;
  case kOot3dAudioRequestFlag100ReferenceAcquireReturn:
    outcome = ContinueAfterReferenceAcquire(plan, memory);
    break;
  case kOot3dAudioRequestFlag100StatusQueryReturn:
    outcome = ContinueAfterStatusQuery(plan, memory);
    break;
  case kOot3dAudioRequestFlag100ReferenceCleanupReturn:
    outcome = ReturnAfterReferenceCleanup(plan, memory);
    break;
  default:
    return Oot3dTypedAudioRequestCallbackResult::NotHandled;
  }

  switch (outcome) {
  case Oot3dTypedAudioRequestCallbackResult::ReferenceAcquireDispatched:
  case Oot3dTypedAudioRequestCallbackResult::StatusQueryDispatched:
  case Oot3dTypedAudioRequestCallbackResult::ReferenceCleanupDispatched:
  case Oot3dTypedAudioRequestCallbackResult::CallbackReturned:
    if (!CommitPlan(plan, state, memory, result, blocksConsumed)) {
      return Oot3dTypedAudioRequestCallbackResult::WriteFailure;
    }
    return outcome;
  default:
    return outcome;
  }
}

} // namespace Oot3dNativeGame
