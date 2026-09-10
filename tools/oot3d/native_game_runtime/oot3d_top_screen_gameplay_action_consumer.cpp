#include "oot3d_top_screen_gameplay_action_consumer.h"

#include "oot3d_native_a32_process.h"
#include "oot3d_top_screen_gameplay_actions.h"

#include "oot3d_ui/ui_native_actions.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

namespace Oot3dNativeGame {
namespace {

constexpr std::uint32_t kPauseRoot = 0x005043D4U;
constexpr std::uint32_t kPauseState = 0x0050AF68U;
constexpr std::uint32_t kSaveContext = 0x00587958U;
constexpr std::uint32_t kOwnedEquipment = 0x00587A0EU;
constexpr std::uint32_t kItemSlotTable = 0x0053CC00U;
constexpr std::uint32_t kInventoryItems = 0x005879E4U;
constexpr std::uint32_t kItemActionTable = 0x004DBD9AU;
constexpr std::uint32_t kPlayerSetEquipmentData = 0x0034913CU;
constexpr std::uint32_t kPlayerPlaySfx = 0x0036F59CU;
constexpr std::uint32_t kEquipmentChangeSfx = 0x01000036U;
constexpr std::uint32_t kBoomerangSlotOffset = 0x52U;
constexpr std::uint32_t kSlingshotSlotOffset = 0x4AU;

struct NativeGameplayActionSnapshot {
  TopScreenEquipmentActionState Equipment;
  TopScreenDirectItemActionState DirectItem;
  std::uint32_t PlayState = 0U;
  std::uint32_t Player = 0U;
};

void SetError(std::string *error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
}

bool ReadDirectItemAvailability(NativeA32Memory &memory,
                                std::uint32_t slotOffset,
                                std::uint8_t expectedItem, bool *available,
                                std::uint8_t *itemAction) {
  if (available == nullptr || itemAction == nullptr) {
    return false;
  }
  *available = false;
  *itemAction = 0U;
  std::uint8_t slot = 0U;
  if (!memory.Read8(kItemSlotTable + slotOffset, &slot) ||
      !memory.Read8(kItemActionTable + expectedItem, itemAction)) {
    return false;
  }
  if (slot > 0x17U) {
    return true;
  }
  std::uint8_t inventoryItem = 0U;
  if (!memory.Read8(kInventoryItems + slot, &inventoryItem)) {
    return false;
  }
  *available = inventoryItem == expectedItem;
  return true;
}

bool ReadActionState(NativeA32Memory &memory,
                     NativeGameplayActionSnapshot *snapshot,
                     std::string *error) {
  if (snapshot == nullptr) {
    SetError(error, "TopScreen gameplay action state output is null");
    return false;
  }
  *snapshot = {};

  std::uint32_t age = 0U;
  std::uint16_t health = 0U;
  std::uint16_t ownedEquipment = 0U;
  std::uint16_t equippedEquipment = 0U;
  std::uint32_t pauseState = 0U;
  if (!memory.Read32(kPauseRoot + 0x0CU, &snapshot->PlayState) ||
      !memory.Read32(kPauseState, &pauseState) ||
      !memory.Read32(kSaveContext + 0x04U, &age) ||
      !memory.Read16(kSaveContext + 0x44U, &health) ||
      !memory.Read16(kOwnedEquipment, &ownedEquipment) ||
      !memory.Read16(kSaveContext + 0x8AU, &equippedEquipment)) {
    SetError(error, "cannot read native TopScreen gameplay action state");
    return false;
  }
  snapshot->Equipment.OwnedEquipment = ownedEquipment;
  snapshot->Equipment.EquippedEquipment = equippedEquipment;
  if (snapshot->PlayState == 0U) {
    return true;
  }

  std::uint8_t stateType = 0U;
  std::uint8_t stateSubtype = 0U;
  if (!memory.Read8(snapshot->PlayState + 0x100U, &stateType) ||
      !memory.Read8(snapshot->PlayState + 0x101U, &stateSubtype) ||
      !memory.Read32(snapshot->PlayState + 0x20ACU, &snapshot->Player)) {
    SetError(error, "cannot read native TopScreen gameplay owner");
    return false;
  }
  if (snapshot->Player == 0U) {
    return true;
  }

  // PlayState is published before its embedded Player pointer is guaranteed
  // to be initialized.  A fast Linux host can observe that short boot window;
  // treat an unmapped pointer as "no active player" until the guest publishes
  // the real heap object instead of turning a host UI poll into a fatal error.
  constexpr std::uint32_t kPlayerActionStateSpan = 0x12BDU;
  if (!memory.IsMapped(snapshot->Player, kPlayerActionStateSpan)) {
    snapshot->Player = 0U;
    return true;
  }

  std::uint32_t heldActor = 0U;
  std::uint8_t cutsceneAction = 0U;
  std::uint8_t heldItemAction = 0U;
  std::uint8_t itemAction = 0U;
  std::uint8_t transientItemAction = 0U;
  if (!memory.Read32(snapshot->Player + 0x1224U, &heldActor) ||
      !memory.Read8(snapshot->Player + 0x12BCU, &cutsceneAction) ||
      !memory.Read8(snapshot->Player + 0x1A9U, &heldItemAction) ||
      !memory.Read8(snapshot->Player + 0x1ACU, &itemAction) ||
      !memory.Read8(kSaveContext + 0x89U, &transientItemAction)) {
    SetError(error, "cannot read native TopScreen Player action gates");
    return false;
  }
  snapshot->Equipment.Eligible =
      pauseState == 2U && age == 0U && health != 0U && stateType == 3U &&
      stateSubtype == 2U && heldActor == 0U && cutsceneAction == 0U &&
      heldItemAction == itemAction;
  snapshot->DirectItem.Eligible = pauseState == 2U && age != 0U &&
                                  health != 0U && stateType == 3U &&
                                  stateSubtype == 2U;
  snapshot->DirectItem.PlayerHeldItemAction = heldItemAction;
  snapshot->DirectItem.CurrentTransientItemAction = transientItemAction;
  if (!ReadDirectItemAvailability(memory, kBoomerangSlotOffset,
                                  kTopScreenBoomerangItemId,
                                  &snapshot->DirectItem.BoomerangAvailable,
                                  &snapshot->DirectItem.BoomerangItemAction) ||
      !ReadDirectItemAvailability(memory, kSlingshotSlotOffset,
                                  kTopScreenSlingshotItemId,
                                  &snapshot->DirectItem.SlingshotAvailable,
                                  &snapshot->DirectItem.SlingshotItemAction)) {
    SetError(error, "cannot decode native TopScreen direct-item ownership");
    return false;
  }
  return true;
}

bool InvokeChangeEquipment(NativeA32Process &process,
                           const oot3d::ui::ChangeEquipmentRequest &request,
                           std::uint32_t directCallReturnAddress,
                           std::string *error) {
  const auto *contract = oot3d::ui::FindOot3dNativeUiActionContract(
      oot3d::ui::UiNativeActionKind::ChangeEquipment);
  if (contract == nullptr ||
      contract->invocation !=
          oot3d::ui::UiNativeActionInvocation::BackendRequest) {
    SetError(error, "native ChangeEquipment UI action contract is unavailable");
    return false;
  }
  const std::array<std::uint32_t, 2> arguments{
      static_cast<std::uint32_t>(request.equipment_type),
      static_cast<std::uint32_t>(request.value)};
  return process.InvokeFunction(contract->guest_entry, arguments,
                                directCallReturnAddress, error);
}

} // namespace

bool PlayTopScreenUiSound(NativeA32Process &process, std::uint32_t sound,
                         std::uint32_t returnAddress, std::string *error) {
  // 005D42E0..005D4308: four register arguments and two stack arguments.
  // Gain/frequency and reverb are persistent native globals, not host floats.
  auto state = process.PrimaryThreadState();
  auto &memory = process.Memory();
  if (state.r[13] < 8U) return false;
  const auto stack = (state.r[13] - 8U) & ~7U;
  std::uint32_t savedGain = 0, savedReverb = 0;
  if (!memory.IsWritable(stack, 8U) ||
      !memory.Read32(stack, &savedGain) || !memory.Read32(stack + 4, &savedReverb) ||
      !memory.IsMapped(0x0054AC20U, 8U)) {
    SetError(error, "native TopScreen UI sound arguments are unavailable");
    return false;
  }
  memory.Write32(stack, 0x0054AC20U);
  memory.Write32(stack + 4, 0x0054AC24U);
  state.r[0] = sound;
  state.r[1] = 0;
  state.r[2] = 4;
  state.r[3] = 0x0054AC20U;
  state.r[13] = stack;
  const bool success = process.InvokeFunctionWithState(
      0x0037547CU, state, returnAddress, error);
  memory.Write32(stack, savedGain);
  memory.Write32(stack + 4, savedReverb);
  return success;
}

bool ReadTopScreenChildLink(const NativeA32Memory &memory, bool *childLink,
                            std::string *error) {
  if (childLink == nullptr) {
    SetError(error, "TopScreen Link age output is null");
    return false;
  }
  std::uint32_t age = 0U;
  if (!memory.ReadFast(kSaveContext + 0x04U, &age)) {
    SetError(error, "cannot read native TopScreen Link age");
    return false;
  }
  *childLink = age != 0U;
  return true;
}

bool ConsumeTopScreenGameplayActions(
    NativeA32Process &process, const TopScreenDpadActionState &actions,
    bool ordinaryItemActivated, std::uint32_t directCallReturnAddress,
    TopScreenGameplayActionConsumerRuntime *runtime,
    TopScreenGameplayActionConsumerStats *stats, std::string *error) {
  if (runtime == nullptr) {
    SetError(error, "TopScreen gameplay action runtime is null");
    return false;
  }

  TopScreenGameplayActionConsumerStats localStats;
  if (stats != nullptr) {
    localStats = *stats;
  }
  NativeGameplayActionSnapshot snapshot;
  if (!ReadActionState(process.Memory(), &snapshot, error)) {
    return false;
  }
  snapshot.DirectItem.OrdinaryItemActivated = ordinaryItemActivated;
  const bool attempted = HasTopScreenEquipmentActionInput(actions) ||
                         HasTopScreenDirectItemActionInput(actions) ||
                         runtime->DirectItem.ActiveItemId != 0U ||
                         snapshot.DirectItem.CurrentTransientItemAction != 0U;
  if (attempted) {
    ++localStats.Attempts;
  }
  if (snapshot.Equipment.Eligible || snapshot.DirectItem.Eligible) {
    ++localStats.Eligible;
  }
  const auto equipmentPlan =
      ResolveTopScreenEquipmentAction(actions, snapshot.Equipment);
  const auto directItemPlan = ResolveTopScreenDirectItemAction(
      actions, snapshot.DirectItem, runtime->DirectItem);

  auto &memory = process.Memory();
  std::uint32_t playerStateFlags2 = 0U;
  if (equipmentPlan.ChangeEquipment.has_value() &&
      (!memory.IsWritable(snapshot.Player + 0x172CU, sizeof(std::uint32_t)) ||
       !memory.IsWritable(snapshot.Player + 0x1714U, sizeof(std::uint32_t)) ||
       !memory.Read32(snapshot.Player + 0x1714U, &playerStateFlags2) ||
       (equipmentPlan.SwordButtonItem.has_value() &&
        !memory.IsWritable(kSaveContext + 0x80U, sizeof(std::uint8_t))))) {
    SetError(error, "cannot preflight native TopScreen equipment transaction");
    return false;
  }
  if ((directItemPlan.TransientItemAction.has_value() &&
       !memory.IsWritable(kSaveContext + 0x89U, sizeof(std::uint8_t))) ||
      (directItemPlan.TriggerPlayerItemUse &&
       (!memory.IsWritable(snapshot.Player + 0x29E4U, sizeof(std::uint32_t)) ||
        !memory.IsWritable(snapshot.Player + 0x29E8U,
                           sizeof(std::uint32_t))))) {
    SetError(error, "cannot preflight native TopScreen direct-item action");
    return false;
  }

  if (equipmentPlan.ChangeEquipment.has_value()) {
    if (!InvokeChangeEquipment(process, *equipmentPlan.ChangeEquipment,
                               directCallReturnAddress, error)) {
      return false;
    }
    ++localStats.EquipmentChanges;
    if (equipmentPlan.SwordButtonItem.has_value() &&
        !memory.Write8(kSaveContext + 0x80U, *equipmentPlan.SwordButtonItem)) {
      SetError(error, "cannot update native sword button item");
      return false;
    }
    if (!memory.Write32(snapshot.Player + 0x172CU, 0U)) {
      SetError(error, "cannot reset native Player equipment state");
      return false;
    }
    const std::array<std::uint32_t, 2> refreshArguments{snapshot.PlayState,
                                                        snapshot.Player};
    if (!process.InvokeFunction(kPlayerSetEquipmentData, refreshArguments,
                                directCallReturnAddress, error)) {
      return false;
    }
    const std::array<std::uint32_t, 2> soundArguments{snapshot.Player,
                                                      kEquipmentChangeSfx};
    if (!process.InvokeFunction(kPlayerPlaySfx, soundArguments,
                                directCallReturnAddress, error)) {
      return false;
    }
    if (!memory.Write32(snapshot.Player + 0x1714U, playerStateFlags2 | 8U)) {
      SetError(error, "cannot complete native TopScreen equipment refresh");
      return false;
    }
    ++localStats.PlayerRefreshes;
  }

  if (directItemPlan.TransientItemAction.has_value()) {
    if (!memory.Write8(kSaveContext + 0x89U,
                       *directItemPlan.TransientItemAction)) {
      SetError(error, "cannot update native TopScreen direct-item action");
      return false;
    }
    if (*directItemPlan.TransientItemAction == 0U) {
      ++localStats.DirectItemClears;
    } else {
      ++localStats.DirectItemAssignments;
    }
  }
  if (directItemPlan.TriggerPlayerItemUse) {
    if (!memory.Write32(snapshot.Player + 0x29E4U, 3U) ||
        !memory.Write32(snapshot.Player + 0x29E8U, 1U)) {
      SetError(error, "cannot trigger native TopScreen direct-item action");
      return false;
    }
    ++localStats.DirectItemTriggers;
  }
  if (stats != nullptr) {
    *stats = localStats;
  }
  return true;
}

} // namespace Oot3dNativeGame
