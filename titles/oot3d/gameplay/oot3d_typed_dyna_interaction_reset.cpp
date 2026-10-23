#include "oot3d_typed_dyna_interaction_reset.h"

#include <array>
#include <cstdint>
#include <span>

namespace Oot3dNativeGame {
namespace {

constexpr std::uint32_t kSavedFrameSize = 2U * sizeof(std::uint32_t);
constexpr std::uint16_t kDynaSlotActive = 0x0001U;
constexpr std::uint16_t kDynaSlotDeleted = 0x0002U;
constexpr std::uint32_t kOwnerReturnInstruction = 0x0047AFACU;
constexpr std::uint32_t kInteractResetReturnInstruction = 0x00483CA4U;
constexpr std::uint32_t kConditionFlagsMask =
    oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagZ |
    oot3d::recomp::a32::kFlagC | oot3d::recomp::a32::kFlagV;

struct DynaInteractionResetPlan {
  oot3d::recomp::a32::GuestState State;
  std::array<std::uint32_t, 2> SavedFrame{};
  std::uint32_t SavedFrameAddress = 0U;
  std::uint32_t InteractFlagsAddress = 0U;
  bool ResetInteraction = false;
};

void SetEqualConditionFlags(oot3d::recomp::a32::GuestState &state) noexcept {
  state.cpsr = (state.cpsr & ~kConditionFlagsMask) |
               oot3d::recomp::a32::kFlagZ | oot3d::recomp::a32::kFlagC;
}

Oot3dTypedDynaInteractionResetResult
BuildPlan(const oot3d::recomp::a32::GuestState &incoming,
          NativeA32Memory &memory, DynaInteractionResetPlan *plan) {
  if (plan == nullptr || incoming.r[13] < kSavedFrameSize) {
    return Oot3dTypedDynaInteractionResetResult::WriteFailure;
  }

  plan->State = incoming;
  plan->SavedFrameAddress = incoming.r[13] - kSavedFrameSize;
  plan->SavedFrame = {incoming.r[4], incoming.r[14]};
  if (!memory.IsWritable(plan->SavedFrameAddress, kSavedFrameSize)) {
    return Oot3dTypedDynaInteractionResetResult::WriteFailure;
  }

  const std::uint32_t play = incoming.r[0];
  const std::uint32_t dyna = incoming.r[1];
  const std::uint32_t actor = incoming.r[2];
  const std::uint32_t dynaActors = play + kOot3dPlayDynaActorContextOffset;

  plan->State.r[12] = play;
  plan->State.r[13] = plan->SavedFrameAddress;
  plan->State.r[14] = dynaActors;
  plan->State.r[0] = actor;
  plan->State.r[3] = 0U;

  for (; plan->State.r[3] < kOot3dDynaActorCount; ++plan->State.r[3]) {
    const std::uint32_t index = plan->State.r[3];
    const std::uint32_t deleteFlagsAddress =
        dyna + kOot3dDynaDeleteFlagsOffset + index * sizeof(std::uint16_t);
    std::uint16_t deleteFlags = 0U;
    if (!memory.ReadFast(deleteFlagsAddress, &deleteFlags)) {
      return Oot3dTypedDynaInteractionResetResult::ReadFailure;
    }
    plan->State.r[2] = deleteFlags;

    if ((deleteFlags & kDynaSlotActive) != 0U) {
      const std::uint32_t actorFlagsBase = dynaActors +
                                           kOot3dDynaActorFlagsOffset +
                                           index * sizeof(std::uint16_t);
      plan->State.r[2] = actorFlagsBase - 0x6CU;

      std::uint16_t actorFlags = 0U;
      if (!memory.ReadFast(actorFlagsBase, &actorFlags)) {
        return Oot3dTypedDynaInteractionResetResult::ReadFailure;
      }
      plan->State.r[12] = actorFlags;

      if ((actorFlags & kDynaSlotActive) == 0U ||
          (actorFlags & kDynaSlotDeleted) != 0U) {
        plan->State.r[12] = 0U;
      } else {
        const std::uint32_t actorEntry =
            dynaActors + index * kOot3dDynaActorEntrySize;
        plan->State.r[2] = actorEntry;
        if (!memory.ReadFast(actorEntry + kOot3dDynaActorEntryPointerOffset,
                             &plan->State.r[12])) {
          return Oot3dTypedDynaInteractionResetResult::ReadFailure;
        }
      }

      if (plan->State.r[12] != 0U && plan->State.r[12] == plan->State.r[0]) {
        plan->InteractFlagsAddress = actor + kOot3dDynaActorInteractFlagsOffset;
        if (!memory.IsWritable(plan->InteractFlagsAddress,
                               sizeof(std::uint8_t))) {
          return Oot3dTypedDynaInteractionResetResult::WriteFailure;
        }
        plan->ResetInteraction = true;
        break;
      }
    }
  }

  SetEqualConditionFlags(plan->State);
  plan->State.r[13] = incoming.r[13];
  plan->State.r[15] = incoming.r[14];
  if (plan->ResetInteraction) {
    plan->State.r[1] = 0U;
    plan->State.r[14] = incoming.r[14];
  }
  return plan->ResetInteraction
             ? Oot3dTypedDynaInteractionResetResult::InteractionReset
             : Oot3dTypedDynaInteractionResetResult::ActorNotRegistered;
}

bool CommitPlan(const DynaInteractionResetPlan &plan,
                oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
                oot3d::recomp::a32::ExecutionResult *result,
                std::uint32_t *blocksConsumed) {
  if (!memory.WriteBytes(
          plan.SavedFrameAddress,
          std::span<const std::uint8_t>(
              reinterpret_cast<const std::uint8_t *>(plan.SavedFrame.data()),
              sizeof(plan.SavedFrame)))) {
    return false;
  }
  if (plan.ResetInteraction &&
      !memory.WriteFast(plan.InteractFlagsAddress, std::uint8_t{0U})) {
    return false;
  }

  state = plan.State;
  const std::uint32_t source = plan.ResetInteraction
                                   ? kInteractResetReturnInstruction
                                   : kOwnerReturnInstruction;
  *result = {
      oot3d::recomp::a32::ExitKind::Branch,
      state.r[15],
      oot3d::recomp::a32::FallbackReason::None,
      source,
  };
  if (blocksConsumed != nullptr) {
    *blocksConsumed = 1U;
  }
  return true;
}

} // namespace

Oot3dTypedDynaInteractionResetResult ExecuteOot3dTypedDynaInteractionReset(
    std::uint32_t pc, oot3d::recomp::a32::GuestState &state,
    NativeA32Memory &memory, oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed) {
  if (pc != kOot3dDynaResetActorInteractionIfRegisteredEntry ||
      result == nullptr) {
    return Oot3dTypedDynaInteractionResetResult::NotHandled;
  }

  DynaInteractionResetPlan plan;
  const auto outcome = BuildPlan(state, memory, &plan);
  if (outcome != Oot3dTypedDynaInteractionResetResult::InteractionReset &&
      outcome != Oot3dTypedDynaInteractionResetResult::ActorNotRegistered) {
    return outcome;
  }
  if (!CommitPlan(plan, state, memory, result, blocksConsumed)) {
    return Oot3dTypedDynaInteractionResetResult::WriteFailure;
  }
  return outcome;
}

} // namespace Oot3dNativeGame
