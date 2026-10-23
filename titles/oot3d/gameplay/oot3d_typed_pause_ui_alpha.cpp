#include "oot3d_typed_pause_ui_alpha.h"

#include "oot3d_native_a32_vfp_ops.h"
#include "recomp/a32_vfp_scalar.h"

#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace Oot3dNativeGame {
namespace {

constexpr std::uint32_t kSavedFrameSize = 6U * sizeof(std::uint32_t);
constexpr std::uint32_t kUpdateRateOffset = 0x110U;
constexpr std::uint32_t kBiasLiteral = kOot3dPauseUiAlphaLiteralPool + 0x04U;
constexpr std::uint32_t kFadeOutPrimaryFactorLiteral =
    kOot3dPauseUiAlphaLiteralPool + 0x08U;
constexpr std::uint32_t kFadeOutSecondaryFactorLiteral =
    kOot3dPauseUiAlphaLiteralPool + 0x0CU;
constexpr std::uint32_t kFadeInPrimaryFactorLiteral =
    kOot3dPauseUiAlphaLiteralPool + 0x10U;
constexpr std::uint32_t kFadeInSecondaryFactorLiteral =
    kOot3dPauseUiAlphaLiteralPool + 0x14U;
constexpr std::uint32_t kFadeOutPrimaryCall = 0x004795E8U;
constexpr std::uint32_t kFadeOutTailCall = 0x00479624U;
constexpr std::uint32_t kFadeInPrimaryCall = 0x0047965CU;
constexpr std::uint32_t kFadeInTailCall = 0x00479698U;
constexpr std::uint32_t kReturnInstruction = 0x004796A0U;
constexpr std::uint32_t kConditionFlagsMask =
    oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagZ |
    oot3d::recomp::a32::kFlagC | oot3d::recomp::a32::kFlagV;

struct PendingByteWrite {
  std::uint32_t Address = 0U;
  std::uint8_t Value = 0U;
};

struct PauseUiAlphaPlan {
  oot3d::recomp::a32::GuestState State;
  oot3d::recomp::a32::ExecutionResult Execution;
  std::optional<std::array<std::uint32_t, 6>> SavedFrameWrite;
  std::uint32_t SavedFrameAddress = 0U;
  std::array<PendingByteWrite, 2> ByteWrites{};
  std::size_t ByteWriteCount = 0U;
  Oot3dTypedPauseUiAlphaResult Success =
      Oot3dTypedPauseUiAlphaResult::NotHandled;
};

bool CheckedAddress(std::uint32_t base, std::uint32_t offset,
                    std::uint32_t *address) noexcept {
  if (address == nullptr ||
      base > std::numeric_limits<std::uint32_t>::max() - offset) {
    return false;
  }
  *address = base + offset;
  return true;
}

std::uint32_t SubtractConditionFlags(std::uint32_t left,
                                     std::uint32_t right) noexcept {
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

void Compare(PauseUiAlphaPlan &plan, std::uint32_t left,
             std::uint32_t right) noexcept {
  plan.State.cpsr = (plan.State.cpsr & ~kConditionFlagsMask) |
                    SubtractConditionFlags(left, right);
}

void AndByteFlags(PauseUiAlphaPlan &plan, std::uint32_t value) noexcept {
  plan.State.cpsr &= ~(oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagZ);
  if ((value & oot3d::recomp::a32::kFlagN) != 0U) {
    plan.State.cpsr |= oot3d::recomp::a32::kFlagN;
  }
  if (value == 0U) {
    plan.State.cpsr |= oot3d::recomp::a32::kFlagZ;
  }
}

void CompleteBranch(PauseUiAlphaPlan &plan, std::uint32_t source,
                    std::uint32_t target,
                    Oot3dTypedPauseUiAlphaResult success) noexcept {
  plan.State.r[15] = target;
  plan.Execution = {
      oot3d::recomp::a32::ExitKind::Branch,
      target,
      oot3d::recomp::a32::FallbackReason::None,
      source,
  };
  plan.Success = success;
}

bool QueueByteWrite(PauseUiAlphaPlan &plan, NativeA32Memory &memory,
                    std::uint32_t address, std::uint8_t value) {
  if (plan.ByteWriteCount >= plan.ByteWrites.size() ||
      !memory.IsWritable(address, sizeof(value))) {
    return false;
  }
  plan.ByteWrites[plan.ByteWriteCount++] = {address, value};
  return true;
}

bool ReadSavedFrame(const PauseUiAlphaPlan &plan, NativeA32Memory &memory,
                    std::array<std::uint32_t, 6> *savedFrame) {
  if (savedFrame == nullptr ||
      plan.State.r[13] >
          std::numeric_limits<std::uint32_t>::max() - kSavedFrameSize) {
    return false;
  }
  return memory.ReadBytes(
      plan.State.r[13],
      std::span<std::uint8_t>(
          reinterpret_cast<std::uint8_t *>(savedFrame->data()),
          sizeof(*savedFrame)));
}

void RestoreSavedFrame(PauseUiAlphaPlan &plan,
                       const std::array<std::uint32_t, 6> &savedFrame,
                       bool restoreLinkRegister) noexcept {
  plan.State.vfp[16] = savedFrame[0];
  plan.State.vfp[17] = savedFrame[1];
  plan.State.r[4] = savedFrame[2];
  plan.State.r[5] = savedFrame[3];
  plan.State.r[6] = savedFrame[4];
  if (restoreLinkRegister) {
    plan.State.r[14] = savedFrame[5];
  }
  plan.State.r[13] += kSavedFrameSize;
}

bool CommitPlan(PauseUiAlphaPlan &plan, oot3d::recomp::a32::GuestState &state,
                NativeA32Memory &memory,
                oot3d::recomp::a32::ExecutionResult *result,
                std::uint32_t *blocksConsumed) {
  if (plan.SavedFrameWrite.has_value() &&
      !memory.WriteBytes(
          plan.SavedFrameAddress,
          std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(
                                            plan.SavedFrameWrite->data()),
                                        sizeof(*plan.SavedFrameWrite)))) {
    return false;
  }
  for (std::size_t index = 0U; index < plan.ByteWriteCount; ++index) {
    if (!memory.WriteFast(plan.ByteWrites[index].Address,
                          plan.ByteWrites[index].Value)) {
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

void CommitVfpResult(PauseUiAlphaPlan &plan, std::uint8_t lane,
                     oot3d::recomp::a32::VfpBinary32Result result) noexcept {
  plan.State.vfp[lane] = result.value;
  plan.State.fpscr |= result.exception_flags;
}

bool ReadUpdateRate(NativeA32Memory &memory, std::uint32_t pointerCell,
                    std::int16_t *updateRate) {
  std::uint32_t timeState = 0U;
  std::uint32_t updateRateAddress = 0U;
  std::uint16_t updateRateBits = 0U;
  if (updateRate == nullptr || !memory.ReadFast(pointerCell, &timeState) ||
      !CheckedAddress(timeState, kUpdateRateOffset, &updateRateAddress) ||
      !memory.ReadFast(updateRateAddress, &updateRateBits)) {
    return false;
  }
  *updateRate = std::bit_cast<std::int16_t>(updateRateBits);
  return true;
}

std::uint32_t SignExtendLowHalf(std::uint32_t value) noexcept {
  return std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(
      std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value))));
}

void BuildPrimaryStep(PauseUiAlphaPlan &plan, std::int16_t updateRate,
                      std::uint32_t factorBits) noexcept {
  plan.State.vfp[2] = factorBits;
  plan.State.vfp[0] =
      std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(updateRate));
  CommitVfpResult(plan, 1,
                  oot3d::recomp::a32::VfpBinary32FromSigned(plan.State.vfp[0],
                                                            plan.State.fpscr));
  plan.State.vfp[0] = plan.State.vfp[16];
  CommitVfpResult(plan, 0,
                  oot3d::recomp::a32::VfpBinary32MultiplyAccumulate(
                      plan.State.vfp[0], plan.State.vfp[1], plan.State.vfp[2],
                      plan.State.fpscr));
  CommitVfpResult(plan, 0,
                  oot3d::recomp::a32::VfpBinary32ToSigned(plan.State.vfp[0],
                                                          plan.State.fpscr));
  plan.State.r[0] = plan.State.vfp[0];
  plan.State.r[2] = SignExtendLowHalf(plan.State.r[0]);
}

void BuildSecondaryStep(PauseUiAlphaPlan &plan, std::int16_t updateRate,
                        std::uint32_t factorBits) noexcept {
  plan.State.vfp[1] = factorBits;
  plan.State.vfp[0] =
      std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(updateRate));
  CommitVfpResult(plan, 0,
                  oot3d::recomp::a32::VfpBinary32FromSigned(plan.State.vfp[0],
                                                            plan.State.fpscr));
  CommitVfpResult(plan, 16,
                  oot3d::recomp::a32::VfpBinary32MultiplyAccumulate(
                      plan.State.vfp[16], plan.State.vfp[0], plan.State.vfp[1],
                      plan.State.fpscr));
  CommitVfpResult(plan, 0,
                  oot3d::recomp::a32::VfpBinary32ToSigned(plan.State.vfp[16],
                                                          plan.State.fpscr));
}

Oot3dTypedPauseUiAlphaResult BeginUpdate(PauseUiAlphaPlan &plan,
                                         NativeA32Memory &memory) {
  if (plan.State.r[13] < kSavedFrameSize) {
    return Oot3dTypedPauseUiAlphaResult::InvalidState;
  }
  const std::uint32_t frameAddress = plan.State.r[13] - kSavedFrameSize;
  if (!memory.IsWritable(frameAddress, kSavedFrameSize)) {
    return Oot3dTypedPauseUiAlphaResult::WriteFailure;
  }

  plan.SavedFrameWrite =
      std::array{plan.State.vfp[16], plan.State.vfp[17], plan.State.r[4],
                 plan.State.r[5],    plan.State.r[6],    plan.State.r[14]};
  plan.SavedFrameAddress = frameAddress;
  plan.State.r[4] = plan.State.r[1];
  plan.State.r[13] = frameAddress;
  plan.State.r[14] = kOot3dPauseUiAlphaPauseStateReturn;
  CompleteBranch(plan, kOot3dPauseUiAlphaPauseStateReturn - 0x04U,
                 kOot3dPauseContextGetStateEntry,
                 Oot3dTypedPauseUiAlphaResult::PauseStateQueryDispatched);
  return plan.Success;
}

Oot3dTypedPauseUiAlphaResult ReturnFromUpdate(PauseUiAlphaPlan &plan,
                                              NativeA32Memory &memory) {
  std::array<std::uint32_t, 6> savedFrame{};
  if (!ReadSavedFrame(plan, memory, &savedFrame)) {
    return Oot3dTypedPauseUiAlphaResult::ReadFailure;
  }
  RestoreSavedFrame(plan, savedFrame, false);
  CompleteBranch(plan, kReturnInstruction, savedFrame[5],
                 Oot3dTypedPauseUiAlphaResult::UpdateReturned);
  return plan.Success;
}

Oot3dTypedPauseUiAlphaResult ContinueAfterPauseState(PauseUiAlphaPlan &plan,
                                                     NativeA32Memory &memory) {
  Compare(plan, plan.State.r[0], 0U);
  if (plan.State.r[0] != 0U) {
    return ReturnFromUpdate(plan, memory);
  }

  std::uint32_t gateTimerAddress = 0U;
  std::uint8_t gateTimer = 0U;
  if (!CheckedAddress(plan.State.r[4], kOot3dPauseUiGateTimerOffset,
                      &gateTimerAddress) ||
      !memory.ReadFast(gateTimerAddress, &gateTimer)) {
    return Oot3dTypedPauseUiAlphaResult::ReadFailure;
  }
  plan.State.r[0] = gateTimer;
  Compare(plan, plan.State.r[0], 0U);
  if (gateTimer != 0U) {
    gateTimer = static_cast<std::uint8_t>(gateTimer - 1U);
    plan.State.r[0] = gateTimer;
    AndByteFlags(plan, plan.State.r[0]);
    if (!QueueByteWrite(plan, memory, gateTimerAddress, gateTimer)) {
      return Oot3dTypedPauseUiAlphaResult::WriteFailure;
    }
    if (gateTimer != 0U) {
      return ReturnFromUpdate(plan, memory);
    }
  }

  std::uint32_t fadeTimerAddress = 0U;
  std::uint8_t fadeTimer = 0U;
  std::uint32_t updateRatePointerCell = 0U;
  std::uint32_t biasBits = 0U;
  if (!CheckedAddress(plan.State.r[4], kOot3dPauseUiFadeTimerOffset,
                      &fadeTimerAddress) ||
      !memory.ReadFast(fadeTimerAddress, &fadeTimer) ||
      !memory.ReadFast(kOot3dPauseUiAlphaLiteralPool, &updateRatePointerCell) ||
      !memory.ReadFast(kBiasLiteral, &biasBits)) {
    return Oot3dTypedPauseUiAlphaResult::ReadFailure;
  }

  plan.State.r[0] = fadeTimer;
  plan.State.r[5] = updateRatePointerCell;
  plan.State.vfp[16] = biasBits;
  Compare(plan, plan.State.r[0], 0U);

  bool fadeIn = false;
  if (fadeTimer != 0U) {
    fadeTimer = static_cast<std::uint8_t>(fadeTimer - 1U);
    plan.State.r[0] = fadeTimer;
    AndByteFlags(plan, plan.State.r[0]);
    if (!QueueByteWrite(plan, memory, fadeTimerAddress, fadeTimer)) {
      return Oot3dTypedPauseUiAlphaResult::WriteFailure;
    }
    fadeIn = fadeTimer != 0U;
  }

  const std::uint32_t factorAddress =
      fadeIn ? kFadeInPrimaryFactorLiteral : kFadeOutPrimaryFactorLiteral;
  std::uint32_t factorBits = 0U;
  std::int16_t updateRate = 0;
  std::uint32_t alphaAddress = 0U;
  if (!memory.ReadFast(factorAddress, &factorBits) ||
      !ReadUpdateRate(memory, plan.State.r[5], &updateRate) ||
      !CheckedAddress(plan.State.r[4], kOot3dPauseUiPrimaryAlphaOffset,
                      &alphaAddress)) {
    return Oot3dTypedPauseUiAlphaResult::ReadFailure;
  }

  plan.State.r[1] = fadeIn ? 255U : 0U;
  BuildPrimaryStep(plan, updateRate, factorBits);
  plan.State.r[0] = alphaAddress;
  plan.State.r[14] = fadeIn ? kOot3dPauseUiAlphaFadeInStepReturn
                            : kOot3dPauseUiAlphaFadeOutStepReturn;
  CompleteBranch(plan, fadeIn ? kFadeInPrimaryCall : kFadeOutPrimaryCall,
                 kOot3dPauseUiAlphaMathStepToSEntry,
                 Oot3dTypedPauseUiAlphaResult::FirstAlphaStepDispatched);
  return plan.Success;
}

Oot3dTypedPauseUiAlphaResult ContinueAfterPrimaryStep(PauseUiAlphaPlan &plan,
                                                      NativeA32Memory &memory,
                                                      bool fadeIn) {
  const std::uint32_t factorAddress =
      fadeIn ? kFadeInSecondaryFactorLiteral : kFadeOutSecondaryFactorLiteral;
  std::uint32_t factorBits = 0U;
  std::int16_t updateRate = 0;
  std::uint32_t alphaAddress = 0U;
  std::array<std::uint32_t, 6> savedFrame{};
  if (!memory.ReadFast(factorAddress, &factorBits) ||
      !ReadUpdateRate(memory, plan.State.r[5], &updateRate) ||
      !CheckedAddress(plan.State.r[4], kOot3dPauseUiSecondaryAlphaOffset,
                      &alphaAddress) ||
      !ReadSavedFrame(plan, memory, &savedFrame)) {
    return Oot3dTypedPauseUiAlphaResult::ReadFailure;
  }

  plan.State.r[1] = fadeIn ? 255U : 0U;
  BuildSecondaryStep(plan, updateRate, factorBits);
  plan.State.r[0] = plan.State.vfp[0];
  plan.State.r[2] = SignExtendLowHalf(plan.State.r[0]);
  plan.State.r[0] = alphaAddress;
  RestoreSavedFrame(plan, savedFrame, true);
  CompleteBranch(plan, fadeIn ? kFadeInTailCall : kFadeOutTailCall,
                 kOot3dPauseUiAlphaMathStepToSEntry,
                 Oot3dTypedPauseUiAlphaResult::TailAlphaStepDispatched);
  return plan.Success;
}

} // namespace

Oot3dTypedPauseUiAlphaResult ExecuteOot3dTypedPauseUiAlpha(
    std::uint32_t pc, oot3d::recomp::a32::GuestState &state,
    NativeA32Memory &memory, oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed) {
  if (result == nullptr) {
    return Oot3dTypedPauseUiAlphaResult::NotHandled;
  }

  PauseUiAlphaPlan plan;
  plan.State = state;
  Oot3dTypedPauseUiAlphaResult outcome =
      Oot3dTypedPauseUiAlphaResult::NotHandled;
  switch (pc) {
  case kOot3dPauseUiUpdateDualAlphaEntry:
    outcome = BeginUpdate(plan, memory);
    break;
  case kOot3dPauseUiAlphaPauseStateReturn:
    outcome = ContinueAfterPauseState(plan, memory);
    break;
  case kOot3dPauseUiAlphaFadeOutStepReturn:
    outcome = ContinueAfterPrimaryStep(plan, memory, false);
    break;
  case kOot3dPauseUiAlphaFadeInStepReturn:
    outcome = ContinueAfterPrimaryStep(plan, memory, true);
    break;
  default:
    return Oot3dTypedPauseUiAlphaResult::NotHandled;
  }

  switch (outcome) {
  case Oot3dTypedPauseUiAlphaResult::PauseStateQueryDispatched:
  case Oot3dTypedPauseUiAlphaResult::FirstAlphaStepDispatched:
  case Oot3dTypedPauseUiAlphaResult::TailAlphaStepDispatched:
  case Oot3dTypedPauseUiAlphaResult::UpdateReturned:
    if (!CommitPlan(plan, state, memory, result, blocksConsumed)) {
      return Oot3dTypedPauseUiAlphaResult::WriteFailure;
    }
    return outcome;
  default:
    return outcome;
  }
}

} // namespace Oot3dNativeGame
