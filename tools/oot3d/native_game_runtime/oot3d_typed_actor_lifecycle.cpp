#include "oot3d_typed_actor_lifecycle.h"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace Oot3dNativeGame {
namespace {

constexpr std::uint32_t kActorDestroyFrameSize = 6U * sizeof(std::uint32_t);
constexpr std::uint32_t kActorDestroyAllocatorLiteral = 0x002D64F0U;
constexpr std::uint32_t kAllocatorDestroyVtableOffset = 0x10U;
constexpr std::uint32_t kOwnedResourceDestroyVtableOffset = 0x04U;
constexpr std::uint32_t kOwnedModelSlotCount = 6U;
constexpr std::uint32_t kActorUpdateAllPlayStackOffset = 0x3CU;
constexpr std::uint32_t kConditionFlagsMask =
    oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagZ |
    oot3d::recomp::a32::kFlagC | oot3d::recomp::a32::kFlagV;

enum class PendingWriteWidth : std::uint8_t {
  Byte,
  Word,
};

struct PendingWrite {
  std::uint32_t Address = 0U;
  std::uint32_t Value = 0U;
  PendingWriteWidth Width = PendingWriteWidth::Word;
};

struct LifecyclePlan {
  oot3d::recomp::a32::GuestState State;
  oot3d::recomp::a32::ExecutionResult Execution;
  std::vector<PendingWrite> Writes;
  std::optional<std::array<std::uint32_t, 6>> SavedFrame;
  Oot3dTypedActorLifecycleResult Success =
      Oot3dTypedActorLifecycleResult::NotHandled;
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

void Compare(LifecyclePlan &plan, std::uint32_t left, std::uint32_t right) {
  plan.State.cpsr = (plan.State.cpsr & ~kConditionFlagsMask) |
                    SubtractConditionFlags(left, right);
}

void CompleteBranch(LifecyclePlan &plan, std::uint32_t source,
                    std::uint32_t target,
                    Oot3dTypedActorLifecycleResult success) {
  plan.State.r[15] = target;
  plan.Execution = {
      oot3d::recomp::a32::ExitKind::Branch,
      target,
      oot3d::recomp::a32::FallbackReason::None,
      source,
  };
  plan.Success = success;
}

bool QueueWord(LifecyclePlan &plan, NativeA32Memory &memory,
               std::uint32_t address, std::uint32_t value) {
  if (!memory.IsWritable(address, sizeof(value))) {
    return false;
  }
  plan.Writes.push_back({address, value, PendingWriteWidth::Word});
  return true;
}

bool QueueByte(LifecyclePlan &plan, NativeA32Memory &memory,
               std::uint32_t address, std::uint8_t value) {
  if (!memory.IsWritable(address, sizeof(value))) {
    return false;
  }
  plan.Writes.push_back({address, value, PendingWriteWidth::Byte});
  return true;
}

bool CommitPlan(LifecyclePlan &plan, oot3d::recomp::a32::GuestState &state,
                NativeA32Memory &memory,
                oot3d::recomp::a32::ExecutionResult *result,
                std::uint32_t *blocksConsumed) {
  for (const auto &write : plan.Writes) {
    const bool written =
        write.Width == PendingWriteWidth::Byte
            ? memory.WriteFast(write.Address,
                               static_cast<std::uint8_t>(write.Value))
            : memory.WriteFast(write.Address, write.Value);
    if (!written) {
      return false;
    }
  }
  state = plan.State;
  *result = plan.Execution;
  if (blocksConsumed != nullptr) {
    *blocksConsumed = 1U;
  }
  return true;
}

Oot3dTypedActorLifecycleResult FinishActorDestroy(LifecyclePlan &plan,
                                                  NativeA32Memory &memory) {
  std::uint32_t lastOwnedAddress = 0U;
  std::uint32_t destroyStateAddress = 0U;
  if (!CheckedAddress(plan.State.r[6], kOot3dActorLastOwnedModelOffset,
                      &lastOwnedAddress) ||
      !CheckedAddress(plan.State.r[6], kOot3dActorDestroyStateOffset,
                      &destroyStateAddress)) {
    return Oot3dTypedActorLifecycleResult::InvalidState;
  }
  if (!QueueWord(plan, memory, lastOwnedAddress, plan.State.r[7]) ||
      !QueueByte(plan, memory, destroyStateAddress, 2U)) {
    return Oot3dTypedActorLifecycleResult::WriteFailure;
  }

  std::array<std::uint32_t, 6> saved{};
  if (plan.SavedFrame.has_value()) {
    saved = *plan.SavedFrame;
  } else {
    for (std::uint32_t index = 0U; index < saved.size(); ++index) {
      std::uint32_t address = 0U;
      if (!CheckedAddress(plan.State.r[13], index * sizeof(std::uint32_t),
                          &address)) {
        return Oot3dTypedActorLifecycleResult::InvalidState;
      }
      if (!memory.ReadFast(address, &saved[index])) {
        return Oot3dTypedActorLifecycleResult::ReadFailure;
      }
    }
  }
  if (plan.State.r[13] >
      std::numeric_limits<std::uint32_t>::max() - kActorDestroyFrameSize) {
    return Oot3dTypedActorLifecycleResult::InvalidState;
  }

  plan.State.r[0] = 2U;
  plan.State.r[4] = saved[0];
  plan.State.r[5] = saved[1];
  plan.State.r[6] = saved[2];
  plan.State.r[7] = saved[3];
  plan.State.r[8] = saved[4];
  plan.State.r[13] += kActorDestroyFrameSize;
  CompleteBranch(plan, kOot3dActorDestroyLastOwnedSlotReturn + 0x0CU, saved[5],
                 Oot3dTypedActorLifecycleResult::DestroyReturned);
  return plan.Success;
}

Oot3dTypedActorLifecycleResult
DispatchLastOwnedResource(LifecyclePlan &plan, NativeA32Memory &memory) {
  std::uint32_t resourceAddress = 0U;
  if (!CheckedAddress(plan.State.r[6], kOot3dActorLastOwnedModelOffset,
                      &resourceAddress)) {
    return Oot3dTypedActorLifecycleResult::InvalidState;
  }

  std::uint32_t resource = 0U;
  if (!memory.ReadFast(resourceAddress, &resource)) {
    return Oot3dTypedActorLifecycleResult::ReadFailure;
  }
  plan.State.r[0] = resource;
  Compare(plan, resource, 0U);
  if (resource == 0U) {
    return FinishActorDestroy(plan, memory);
  }

  std::uint32_t vtable = 0U;
  std::uint32_t callback = 0U;
  std::uint32_t callbackAddress = 0U;
  if (!memory.ReadFast(resource, &vtable) ||
      !CheckedAddress(vtable, kOwnedResourceDestroyVtableOffset,
                      &callbackAddress) ||
      !memory.ReadFast(callbackAddress, &callback)) {
    return Oot3dTypedActorLifecycleResult::ReadFailure;
  }
  plan.State.r[1] = callback;
  plan.State.r[14] = kOot3dActorDestroyLastOwnedSlotReturn;
  CompleteBranch(plan, kOot3dActorDestroyLastOwnedSlotReturn - 0x04U, callback,
                 Oot3dTypedActorLifecycleResult::ResourceCallbackDispatched);
  return plan.Success;
}

Oot3dTypedActorLifecycleResult
DispatchOwnedModelSlots(LifecyclePlan &plan, NativeA32Memory &memory) {
  while (plan.State.r[4] < kOwnedModelSlotCount) {
    std::uint32_t slotBase = 0U;
    std::uint32_t slotAddress = 0U;
    if (!CheckedAddress(plan.State.r[6], plan.State.r[4] * 4U, &slotBase) ||
        !CheckedAddress(slotBase, kOot3dActorOwnedModelSlotsOffset,
                        &slotAddress)) {
      return Oot3dTypedActorLifecycleResult::InvalidState;
    }
    plan.State.r[5] = slotBase;

    std::uint32_t resource = 0U;
    if (!memory.ReadFast(slotAddress, &resource)) {
      return Oot3dTypedActorLifecycleResult::ReadFailure;
    }
    plan.State.r[0] = resource;
    Compare(plan, resource, 0U);
    if (resource != 0U) {
      std::uint32_t vtable = 0U;
      std::uint32_t callback = 0U;
      std::uint32_t callbackAddress = 0U;
      if (!memory.ReadFast(resource, &vtable) ||
          !CheckedAddress(vtable, kOwnedResourceDestroyVtableOffset,
                          &callbackAddress) ||
          !memory.ReadFast(callbackAddress, &callback)) {
        return Oot3dTypedActorLifecycleResult::ReadFailure;
      }
      plan.State.r[1] = callback;
      plan.State.r[14] = kOot3dActorDestroyOwnedSlotReturn;
      CompleteBranch(
          plan, kOot3dActorDestroyOwnedSlotReturn - 0x04U, callback,
          Oot3dTypedActorLifecycleResult::ResourceCallbackDispatched);
      return plan.Success;
    }

    ++plan.State.r[4];
    Compare(plan, plan.State.r[4], kOwnedModelSlotCount);
    if (!QueueWord(plan, memory, slotAddress, plan.State.r[7])) {
      return Oot3dTypedActorLifecycleResult::WriteFailure;
    }
  }
  return DispatchLastOwnedResource(plan, memory);
}

Oot3dTypedActorLifecycleResult
DispatchModelContextOrSlots(LifecyclePlan &plan, NativeA32Memory &memory) {
  std::uint32_t modelContextAddress = 0U;
  if (!CheckedAddress(plan.State.r[6], kOot3dActorModelContextOffset,
                      &modelContextAddress)) {
    return Oot3dTypedActorLifecycleResult::InvalidState;
  }

  std::uint32_t modelContext = 0U;
  if (!memory.ReadFast(modelContextAddress, &modelContext)) {
    return Oot3dTypedActorLifecycleResult::ReadFailure;
  }
  plan.State.r[0] = modelContext;
  Compare(plan, modelContext, 0U);
  if (modelContext != 0U) {
    std::uint32_t allocatorGlobalAddress = 0U;
    std::uint32_t allocator = 0U;
    std::uint32_t allocatorVtable = 0U;
    std::uint32_t callbackAddress = 0U;
    std::uint32_t callback = 0U;
    if (!memory.ReadFast(kActorDestroyAllocatorLiteral,
                         &allocatorGlobalAddress) ||
        !memory.ReadFast(allocatorGlobalAddress, &allocator) ||
        !memory.ReadFast(allocator, &allocatorVtable) ||
        !CheckedAddress(allocatorVtable, kAllocatorDestroyVtableOffset,
                        &callbackAddress) ||
        !memory.ReadFast(callbackAddress, &callback)) {
      return Oot3dTypedActorLifecycleResult::ReadFailure;
    }
    plan.State.r[1] = modelContext;
    plan.State.r[2] = allocator;
    plan.State.r[0] = allocator;
    plan.State.r[3] = callback;
    plan.State.r[14] = kOot3dActorDestroyModelContextReturn;
    CompleteBranch(plan, kOot3dActorDestroyModelContextReturn - 0x04U, callback,
                   Oot3dTypedActorLifecycleResult::ResourceCallbackDispatched);
    return plan.Success;
  }

  plan.State.r[4] = 0U;
  return DispatchOwnedModelSlots(plan, memory);
}

Oot3dTypedActorLifecycleResult BeginActorDestroy(LifecyclePlan &plan,
                                                 NativeA32Memory &memory) {
  const std::uint32_t actor = plan.State.r[0];
  if (actor == 0U || plan.State.r[13] < kActorDestroyFrameSize) {
    return Oot3dTypedActorLifecycleResult::InvalidState;
  }

  std::uint32_t callbackAddress = 0U;
  std::uint32_t callback = 0U;
  if (!CheckedAddress(actor, kOot3dActorDestroyOffset, &callbackAddress)) {
    return Oot3dTypedActorLifecycleResult::InvalidState;
  }
  if (!memory.ReadFast(callbackAddress, &callback)) {
    return Oot3dTypedActorLifecycleResult::ReadFailure;
  }

  const std::uint32_t frameAddress = plan.State.r[13] - kActorDestroyFrameSize;
  if (!memory.IsWritable(frameAddress, kActorDestroyFrameSize)) {
    return Oot3dTypedActorLifecycleResult::WriteFailure;
  }
  plan.SavedFrame = std::array<std::uint32_t, 6>{
      plan.State.r[4], plan.State.r[5], plan.State.r[6],
      plan.State.r[7], plan.State.r[8], plan.State.r[14],
  };
  for (std::uint32_t index = 0U; index < plan.SavedFrame->size(); ++index) {
    if (!QueueWord(plan, memory, frameAddress + index * sizeof(std::uint32_t),
                   (*plan.SavedFrame)[index])) {
      return Oot3dTypedActorLifecycleResult::WriteFailure;
    }
  }

  plan.State.r[13] = frameAddress;
  plan.State.r[6] = actor;
  plan.State.r[7] = 0U;
  plan.State.r[2] = callback;
  Compare(plan, callback, 0U);
  if (callback != 0U) {
    plan.State.r[0] = actor;
    plan.State.r[14] = kOot3dActorDestroyCallbackReturn;
    CompleteBranch(plan, kOot3dActorDestroyCallbackReturn - 0x04U, callback,
                   Oot3dTypedActorLifecycleResult::DestroyCallbackDispatched);
    return plan.Success;
  }
  return DispatchModelContextOrSlots(plan, memory);
}

Oot3dTypedActorLifecycleResult
ContinueActorDestroyCallback(LifecyclePlan &plan, NativeA32Memory &memory) {
  if (plan.State.r[6] == 0U) {
    return Oot3dTypedActorLifecycleResult::InvalidState;
  }
  std::uint32_t destroyAddress = 0U;
  if (!CheckedAddress(plan.State.r[6], kOot3dActorDestroyOffset,
                      &destroyAddress)) {
    return Oot3dTypedActorLifecycleResult::InvalidState;
  }
  if (!QueueWord(plan, memory, destroyAddress, plan.State.r[7])) {
    return Oot3dTypedActorLifecycleResult::WriteFailure;
  }
  return DispatchModelContextOrSlots(plan, memory);
}

Oot3dTypedActorLifecycleResult
ContinueModelContextDestroy(LifecyclePlan &plan, NativeA32Memory &memory) {
  if (plan.State.r[6] == 0U) {
    return Oot3dTypedActorLifecycleResult::InvalidState;
  }
  std::uint32_t modelContextAddress = 0U;
  if (!CheckedAddress(plan.State.r[6], kOot3dActorModelContextOffset,
                      &modelContextAddress)) {
    return Oot3dTypedActorLifecycleResult::InvalidState;
  }
  if (!QueueWord(plan, memory, modelContextAddress, plan.State.r[7])) {
    return Oot3dTypedActorLifecycleResult::WriteFailure;
  }
  plan.State.r[4] = 0U;
  return DispatchOwnedModelSlots(plan, memory);
}

Oot3dTypedActorLifecycleResult
ContinueOwnedModelSlotDestroy(LifecyclePlan &plan, NativeA32Memory &memory) {
  if (plan.State.r[6] == 0U || plan.State.r[4] >= kOwnedModelSlotCount) {
    return Oot3dTypedActorLifecycleResult::InvalidState;
  }
  std::uint32_t expectedSlotBase = 0U;
  std::uint32_t slotAddress = 0U;
  if (!CheckedAddress(plan.State.r[6], plan.State.r[4] * 4U,
                      &expectedSlotBase) ||
      plan.State.r[5] != expectedSlotBase ||
      !CheckedAddress(expectedSlotBase, kOot3dActorOwnedModelSlotsOffset,
                      &slotAddress)) {
    return Oot3dTypedActorLifecycleResult::InvalidState;
  }
  if (!QueueWord(plan, memory, slotAddress, plan.State.r[7])) {
    return Oot3dTypedActorLifecycleResult::WriteFailure;
  }

  ++plan.State.r[4];
  Compare(plan, plan.State.r[4], kOwnedModelSlotCount);
  if (plan.State.r[4] < kOwnedModelSlotCount) {
    return DispatchOwnedModelSlots(plan, memory);
  }
  return DispatchLastOwnedResource(plan, memory);
}

Oot3dTypedActorLifecycleResult DispatchActorUpdateAllCallback(
    LifecyclePlan &plan, NativeA32Memory &memory, std::uint32_t callbackOffset,
    std::uint32_t returnAddress, Oot3dTypedActorLifecycleResult success) {
  const std::uint32_t actor = plan.State.r[4];
  std::uint32_t callbackAddress = 0U;
  std::uint32_t playAddress = 0U;
  std::uint32_t callback = 0U;
  std::uint32_t play = 0U;
  if (actor == 0U || !CheckedAddress(actor, callbackOffset, &callbackAddress) ||
      !CheckedAddress(plan.State.r[13], kActorUpdateAllPlayStackOffset,
                      &playAddress)) {
    return Oot3dTypedActorLifecycleResult::InvalidState;
  }
  if (!memory.ReadFast(callbackAddress, &callback) ||
      !memory.ReadFast(playAddress, &play)) {
    return Oot3dTypedActorLifecycleResult::ReadFailure;
  }

  plan.State.r[2] = callback;
  plan.State.r[1] = play;
  plan.State.r[0] = actor;
  plan.State.r[14] = returnAddress;
  CompleteBranch(plan, returnAddress - 0x04U, callback, success);
  return plan.Success;
}

Oot3dTypedActorLifecycleResult
ContinueActorInitCallback(LifecyclePlan &plan, NativeA32Memory &memory) {
  std::uint32_t initAddress = 0U;
  if (plan.State.r[4] == 0U ||
      !CheckedAddress(plan.State.r[4], kOot3dActorInitOffset, &initAddress)) {
    return Oot3dTypedActorLifecycleResult::InvalidState;
  }
  if (!QueueWord(plan, memory, initAddress, plan.State.r[10])) {
    return Oot3dTypedActorLifecycleResult::WriteFailure;
  }
  CompleteBranch(plan, kOot3dActorUpdateAllInitCallbackReturn,
                 kOot3dActorUpdateAllInitContinue,
                 Oot3dTypedActorLifecycleResult::InitCallbackReturned);
  return plan.Success;
}

} // namespace

Oot3dTypedActorLifecycleResult ExecuteOot3dTypedActorLifecycle(
    std::uint32_t pc, oot3d::recomp::a32::GuestState &state,
    NativeA32Memory &memory, oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed) {
  if (result == nullptr) {
    return Oot3dTypedActorLifecycleResult::NotHandled;
  }

  LifecyclePlan plan;
  plan.State = state;
  Oot3dTypedActorLifecycleResult outcome =
      Oot3dTypedActorLifecycleResult::NotHandled;
  switch (pc) {
  case kOot3dActorDestroyEntry:
    outcome = BeginActorDestroy(plan, memory);
    break;
  case kOot3dActorDestroyCallbackReturn:
    outcome = ContinueActorDestroyCallback(plan, memory);
    break;
  case kOot3dActorDestroyModelContextReturn:
    outcome = ContinueModelContextDestroy(plan, memory);
    break;
  case kOot3dActorDestroyOwnedSlotReturn:
    outcome = ContinueOwnedModelSlotDestroy(plan, memory);
    break;
  case kOot3dActorDestroyLastOwnedSlotReturn:
    outcome = FinishActorDestroy(plan, memory);
    break;
  case kOot3dActorUpdateAllInitCallbackEntry:
    outcome = DispatchActorUpdateAllCallback(
        plan, memory, kOot3dActorInitOffset,
        kOot3dActorUpdateAllInitCallbackReturn,
        Oot3dTypedActorLifecycleResult::InitCallbackDispatched);
    break;
  case kOot3dActorUpdateAllInitCallbackReturn:
    outcome = ContinueActorInitCallback(plan, memory);
    break;
  case kOot3dActorUpdateAllUpdateCallbackEntry:
    outcome = DispatchActorUpdateAllCallback(
        plan, memory, kOot3dActorUpdateOffset,
        kOot3dActorUpdateAllUpdateCallbackReturn,
        Oot3dTypedActorLifecycleResult::UpdateCallbackDispatched);
    break;
  default:
    return Oot3dTypedActorLifecycleResult::NotHandled;
  }

  switch (outcome) {
  case Oot3dTypedActorLifecycleResult::InitCallbackDispatched:
  case Oot3dTypedActorLifecycleResult::InitCallbackReturned:
  case Oot3dTypedActorLifecycleResult::UpdateCallbackDispatched:
  case Oot3dTypedActorLifecycleResult::DestroyCallbackDispatched:
  case Oot3dTypedActorLifecycleResult::ResourceCallbackDispatched:
  case Oot3dTypedActorLifecycleResult::DestroyReturned:
    if (!CommitPlan(plan, state, memory, result, blocksConsumed)) {
      return Oot3dTypedActorLifecycleResult::WriteFailure;
    }
    return outcome;
  default:
    return outcome;
  }
}

} // namespace Oot3dNativeGame
