#include "oot3d_top_screen_gameplay_actions.h"

#include <array>
#include <cstddef>

namespace Oot3dNativeGame {
namespace {

constexpr std::uint32_t
EquipmentShift(oot3d::ui::EquipmentType category) noexcept {
  return static_cast<std::uint32_t>(category) * 4U;
}

std::uint8_t EquippedValue(const TopScreenEquipmentActionState &state,
                           oot3d::ui::EquipmentType category) noexcept {
  return static_cast<std::uint8_t>(
      (state.EquippedEquipment >> EquipmentShift(category)) & 0x0FU);
}

std::uint8_t OwnedValues(const TopScreenEquipmentActionState &state,
                         oot3d::ui::EquipmentType category) noexcept {
  return static_cast<std::uint8_t>(
      (state.OwnedEquipment >> EquipmentShift(category)) & 0x0FU);
}

bool OwnsValue(std::uint8_t ownedValues, std::uint8_t value) noexcept {
  return value != 0U && value <= 4U &&
         (ownedValues & (1U << (value - 1U))) != 0U;
}

std::optional<std::uint8_t>
NextOwnedValue(std::uint8_t current, std::uint8_t ownedValues,
               std::uint8_t allowedValues) noexcept {
  const std::uint8_t candidates = ownedValues & allowedValues;
  if (candidates == 0U) {
    return std::nullopt;
  }
  for (std::uint8_t distance = 1U; distance <= 3U; ++distance) {
    const std::uint8_t candidate =
        static_cast<std::uint8_t>(((current + distance - 1U) % 3U) + 1U);
    if (OwnsValue(candidates, candidate)) {
      return candidate;
    }
  }
  return std::nullopt;
}

TopScreenEquipmentActionPlan BuildChange(
    oot3d::ui::EquipmentType category, std::uint8_t value,
    std::optional<std::uint8_t> swordButtonItem = std::nullopt) noexcept {
  TopScreenEquipmentActionPlan result;
  result.Consumed = true;
  result.ChangeEquipment = oot3d::ui::ChangeEquipmentRequest{
      category, static_cast<std::int32_t>(value)};
  result.SwordButtonItem = swordButtonItem;
  result.RefreshPlayerEquipment = true;
  return result;
}

void ResetDirectItem(TopScreenDirectItemRuntime &runtime) noexcept {
  runtime = {};
}

std::uint8_t
DirectItemAction(std::uint8_t itemId,
                 const TopScreenDirectItemActionState &state) noexcept {
  if (itemId == kTopScreenBoomerangItemId) {
    return state.BoomerangItemAction;
  }
  if (itemId == kTopScreenSlingshotItemId) {
    return state.SlingshotItemAction;
  }
  return 0U;
}

} // namespace

bool HasTopScreenEquipmentActionInput(
    const TopScreenDpadActionState &actions) noexcept {
  constexpr std::array kEquipmentActions{
      TopScreenDpadAction::IronBoots,   TopScreenDpadAction::HoverBoots,
      TopScreenDpadAction::SwordToggle, TopScreenDpadAction::AllBootsToggle,
      TopScreenDpadAction::TunicToggle, TopScreenDpadAction::ShieldToggle};
  for (const auto action : kEquipmentActions) {
    if (actions.WasPressed(action)) {
      return true;
    }
  }
  return false;
}

bool HasTopScreenDirectItemActionInput(
    const TopScreenDpadActionState &actions) noexcept {
  return actions.IsHeld(TopScreenDpadAction::Boomerang) ||
         actions.IsHeld(TopScreenDpadAction::Slingshot);
}

TopScreenDirectItemActionPlan
ResolveTopScreenDirectItemAction(const TopScreenDpadActionState &actions,
                                 const TopScreenDirectItemActionState &state,
                                 TopScreenDirectItemRuntime &runtime) noexcept {
  TopScreenDirectItemActionPlan plan;
  const auto clearTransient = [&]() {
    if (state.CurrentTransientItemAction != 0U || runtime.ActiveItemId != 0U) {
      plan.TransientItemAction = 0U;
    }
    ResetDirectItem(runtime);
  };

  if (!state.Eligible || state.OrdinaryItemActivated) {
    clearTransient();
    return plan;
  }

  std::uint8_t requestedItem = 0U;
  if (actions.IsHeld(TopScreenDpadAction::Boomerang) &&
      state.BoomerangAvailable) {
    requestedItem = kTopScreenBoomerangItemId;
  } else if (actions.IsHeld(TopScreenDpadAction::Slingshot) &&
             state.SlingshotAvailable) {
    requestedItem = kTopScreenSlingshotItemId;
  }

  if (requestedItem != 0U) {
    const auto itemAction = DirectItemAction(requestedItem, state);
    if (itemAction == 0U) {
      clearTransient();
      return plan;
    }
    if (runtime.ActiveItemId == 0U) {
      runtime.ActiveFrames = 0U;
      runtime.PlayerObservedAction = false;
    }
    runtime.ActiveItemId = requestedItem;
    if (runtime.ActiveFrames != 0xFFFFU) {
      ++runtime.ActiveFrames;
    }
    if (state.PlayerHeldItemAction == itemAction) {
      runtime.PlayerObservedAction = true;
    }
    plan.Consumed = true;
    plan.TransientItemAction = itemAction;
    plan.TriggerPlayerItemUse = true;
    return plan;
  }

  if (runtime.ActiveItemId == 0U) {
    if (state.CurrentTransientItemAction != 0U) {
      plan.TransientItemAction = 0U;
    }
    return plan;
  }

  const auto activeAction = DirectItemAction(runtime.ActiveItemId, state);
  if (runtime.ActiveFrames != 0xFFFFU) {
    ++runtime.ActiveFrames;
  }
  if (activeAction != 0U && state.PlayerHeldItemAction == activeAction) {
    runtime.PlayerObservedAction = true;
  } else if (runtime.PlayerObservedAction || runtime.ActiveFrames > 30U) {
    plan.TransientItemAction = 0U;
    ResetDirectItem(runtime);
  }
  return plan;
}

TopScreenEquipmentActionPlan ResolveTopScreenEquipmentAction(
    const TopScreenDpadActionState &actions,
    const TopScreenEquipmentActionState &state) noexcept {
  using oot3d::ui::EquipmentType;
  if (!state.Eligible || !HasTopScreenEquipmentActionInput(actions)) {
    return {};
  }

  if (actions.WasPressed(TopScreenDpadAction::SwordToggle)) {
    const auto current = EquippedValue(state, EquipmentType::EQUIP_TYPE_SWORD);
    const auto owned = OwnedValues(state, EquipmentType::EQUIP_TYPE_SWORD);
    if ((current == 2U || current == 3U) && OwnsValue(owned, 2U) &&
        OwnsValue(owned, 3U)) {
      const std::uint8_t target = current == 2U ? 3U : 2U;
      return BuildChange(EquipmentType::EQUIP_TYPE_SWORD, target,
                         static_cast<std::uint8_t>(0x3AU + target));
    }
    return {};
  }

  const auto currentBoots =
      EquippedValue(state, EquipmentType::EQUIP_TYPE_BOOTS);
  const auto ownedBoots = OwnedValues(state, EquipmentType::EQUIP_TYPE_BOOTS);
  if (actions.WasPressed(TopScreenDpadAction::IronBoots)) {
    if (OwnsValue(ownedBoots, 2U)) {
      return BuildChange(EquipmentType::EQUIP_TYPE_BOOTS,
                         currentBoots == 2U ? 1U : 2U);
    }
    return {};
  }
  if (actions.WasPressed(TopScreenDpadAction::HoverBoots)) {
    if (OwnsValue(ownedBoots, 3U)) {
      return BuildChange(EquipmentType::EQUIP_TYPE_BOOTS,
                         currentBoots == 3U ? 1U : 3U);
    }
    return {};
  }
  if (actions.WasPressed(TopScreenDpadAction::AllBootsToggle)) {
    // Kokiri Boots are the unequipped/base value and are always a valid
    // destination in the original 2.1.1 cycle.
    const auto next = NextOwnedValue(
        currentBoots, static_cast<std::uint8_t>(ownedBoots | 1U), 0x07U);
    if (next.has_value() && *next != currentBoots) {
      return BuildChange(EquipmentType::EQUIP_TYPE_BOOTS, *next);
    }
    return {};
  }

  if (actions.WasPressed(TopScreenDpadAction::TunicToggle)) {
    const auto current = EquippedValue(state, EquipmentType::EQUIP_TYPE_TUNIC);
    const auto next = NextOwnedValue(
        current, OwnedValues(state, EquipmentType::EQUIP_TYPE_TUNIC), 0x07U);
    if (next.has_value() && *next != current) {
      return BuildChange(EquipmentType::EQUIP_TYPE_TUNIC, *next);
    }
    return {};
  }
  if (actions.WasPressed(TopScreenDpadAction::ShieldToggle)) {
    const auto current = EquippedValue(state, EquipmentType::EQUIP_TYPE_SHIELD);
    const auto next = NextOwnedValue(
        current, OwnedValues(state, EquipmentType::EQUIP_TYPE_SHIELD), 0x06U);
    if (next.has_value() && *next != current) {
      return BuildChange(EquipmentType::EQUIP_TYPE_SHIELD, *next);
    }
  }
  return {};
}

} // namespace Oot3dNativeGame
