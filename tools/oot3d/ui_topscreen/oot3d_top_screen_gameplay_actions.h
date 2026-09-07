#pragma once

#include "oot3d_top_screen_controls.h"

#include "oot3d_ui/ui_native_actions.h"

#include <cstdint>
#include <optional>

namespace Oot3dNativeGame {

struct TopScreenEquipmentActionState {
  bool Eligible = false;
  std::uint16_t OwnedEquipment = 0U;
  std::uint16_t EquippedEquipment = 0U;
};

struct TopScreenEquipmentActionPlan {
  bool Consumed = false;
  std::optional<oot3d::ui::ChangeEquipmentRequest> ChangeEquipment;
  std::optional<std::uint8_t> SwordButtonItem;
  bool RefreshPlayerEquipment = false;
};

inline constexpr std::uint8_t kTopScreenSlingshotItemId = 0x06U;
inline constexpr std::uint8_t kTopScreenBoomerangItemId = 0x0EU;

struct TopScreenDirectItemRuntime {
  std::uint8_t ActiveItemId = 0U;
  std::uint16_t ActiveFrames = 0U;
  bool PlayerObservedAction = false;
};

struct TopScreenDirectItemActionState {
  bool Eligible = false;
  bool OrdinaryItemActivated = false;
  bool BoomerangAvailable = false;
  bool SlingshotAvailable = false;
  std::uint8_t BoomerangItemAction = 0U;
  std::uint8_t SlingshotItemAction = 0U;
  std::uint8_t PlayerHeldItemAction = 0U;
  std::uint8_t CurrentTransientItemAction = 0U;
};

struct TopScreenDirectItemActionPlan {
  bool Consumed = false;
  std::optional<std::uint8_t> TransientItemAction;
  bool TriggerPlayerItemUse = false;
};

// Resolves the equipment shortcuts recovered from TopScreen 2.1.1 into the
// native UI action contract. Guest memory and native calls remain application
// responsibilities and are deliberately absent from this planner.
TopScreenEquipmentActionPlan ResolveTopScreenEquipmentAction(
    const TopScreenDpadActionState &actions,
    const TopScreenEquipmentActionState &state) noexcept;

bool HasTopScreenEquipmentActionInput(
    const TopScreenDpadActionState &actions) noexcept;

bool HasTopScreenDirectItemActionInput(
    const TopScreenDpadActionState &actions) noexcept;

// Reproduces the host-side lifetime of the 2.1.1 direct Boomerang/Slingshot
// assignment. The application consumer supplies native inventory/action
// evidence and applies the returned writes; no guest address enters here.
TopScreenDirectItemActionPlan
ResolveTopScreenDirectItemAction(const TopScreenDpadActionState &actions,
                                 const TopScreenDirectItemActionState &state,
                                 TopScreenDirectItemRuntime &runtime) noexcept;

} // namespace Oot3dNativeGame
