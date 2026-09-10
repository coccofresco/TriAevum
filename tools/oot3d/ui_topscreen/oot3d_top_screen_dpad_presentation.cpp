#include "oot3d_top_screen_dpad_presentation.h"
#include "oot3d_top_screen_items_hint.h"

#include "oot3d_native_a32_memory.h"
#include "oot3d_ui/ui_contract_types.h"
#include "oot3d_ui/ui_pause_atlas_regions.h"

#include <algorithm>
#include <bit>
#include <optional>

namespace Oot3dNativeGame {
namespace {
using oot3d::ui::ItemId;
// TopScreen 2.1.1 FUN_005D4A8C, tables 005E1840 / 005E1830.
constexpr std::array<float, 4> kCenterX{30, 29, 9, 51};
constexpr std::array<float, 4> kCenterY{45, 90, 67, 67};
constexpr float kIconSize = 14.0F; // literal 005D7D14
constexpr std::uint32_t kSave = 0x00587958U;

std::optional<ItemId> ResolveItem(TopScreenDpadAction action,
                                  const TopScreenDpadPresentationState &s) {
  const auto equipped = [&](unsigned shift) {
    return (s.EquippedEquipment >> shift) & 15U;
  };
  const auto group = [&](unsigned shift, unsigned mask,
                         unsigned base) -> std::optional<ItemId> {
    const auto value = equipped(shift);
    if (s.Child || value < 1 || value > 3 ||
        std::popcount((s.OwnedEquipment >> shift) & mask) < 2)
      return {};
    return static_cast<ItemId>(base + value - 1);
  };
  switch (action) {
  case TopScreenDpadAction::ItemZr:
    return static_cast<ItemId>(s.ItemZr);
  case TopScreenDpadAction::ItemZl:
    return static_cast<ItemId>(s.ItemZl);
  case TopScreenDpadAction::Ocarina:
    return static_cast<ItemId>(s.Ocarina);
  case TopScreenDpadAction::Boomerang:
    if (s.Child && s.Boomerang)
      return ItemId::ITEM_BOOMERANG;
    break;
  case TopScreenDpadAction::Slingshot:
    if (s.Child && s.Slingshot)
      return ItemId::ITEM_SLINGSHOT;
    break;
  case TopScreenDpadAction::IronBoots:
    if (!s.Child &&
        ((s.OwnedEquipment & 0x2000) || equipped(12) == 2 || s.ItemZr == 0x45))
      return ItemId::ITEM_BOOTS_IRON;
    break;
  case TopScreenDpadAction::HoverBoots:
    if (!s.Child &&
        ((s.OwnedEquipment & 0x4000) || equipped(12) == 3 || s.ItemZl == 0x46))
      return ItemId::ITEM_BOOTS_HOVER;
    break;
  case TopScreenDpadAction::SwordToggle:
    if (!s.Child && (s.OwnedEquipment & 6) == 6 && equipped(0) >= 1 &&
        equipped(0) <= 3)
      return static_cast<ItemId>(0x3A + equipped(0));
    break;
  case TopScreenDpadAction::AllBootsToggle:
    if (!s.Child &&
        ((s.OwnedEquipment & 0x6000) || equipped(12) == 2 ||
         equipped(12) == 3 || s.ItemZr == 0x45 || s.ItemZl == 0x46) &&
        equipped(12) >= 1 && equipped(12) <= 3)
      return static_cast<ItemId>(0x43 + equipped(12));
    break;
  case TopScreenDpadAction::TunicToggle:
    return group(8, 7, 0x41);
  case TopScreenDpadAction::ShieldToggle:
    return group(4, 6, 0x3E);
  default:
    break;
  }
  return {};
}
} // namespace

bool ReadTopScreenDpadPresentationState(NativeA32Memory &memory,
                                        TopScreenDpadPresentationState *state) {
  if (!state)
    return false;
  TopScreenDpadPresentationState result;
  std::uint32_t age = 0;
  if (!memory.Read32(kSave + 4, &age) ||
      !memory.Read16(kSave + 0xB6, &result.OwnedEquipment) ||
      !memory.Read16(kSave + 0x8A, &result.EquippedEquipment) ||
      !memory.Read8(kSave + 0x83, &result.ItemZr) ||
      !memory.Read8(kSave + 0x84, &result.ItemZl))
    return false;
  result.Child = age != 0;
  const auto owns = [&](std::uint8_t item, bool *owned) {
    std::uint8_t slot = 0, stored = 0xFF;
    if (!memory.Read8(0x0053CC44U + item, &slot))
      return false;
    if (slot <= 0x17 && !memory.Read8(kSave + 0x8C + slot, &stored))
      return false;
    *owned = slot <= 0x17 && stored == item;
    return true;
  };
  bool fairy = false, time = false;
  if (!owns(7, &fairy) || !owns(8, &time) || !owns(0x0E, &result.Boomerang) ||
      !owns(6, &result.Slingshot))
    return false;
  if (fairy)
    result.Ocarina = 7;
  else if (time)
    result.Ocarina = 8;
  *state = result;
  return true;
}

std::size_t AppendTopScreenDpadPresentation(
    const TopScreenUiConfig &config,
    const TopScreenDpadPresentationState &state,
    const TopScreenAuxiliaryTouchInputs &viewState, float alpha,
    const oot3d::ui::UiTextureIdentity &itemIcons,
    const oot3d::ui::UiTextureIdentity &pauseTopPage,
    std::vector<oot3d::ui::UiPrimitive> &output) {
  if (!config.RenderHud || !config.RenderDpadIcons || !(alpha > 0))
    return 0;
  const auto start = output.size();
  const auto &actions = state.Child ? config.ChildDpad : config.AdultDpad;
  for (std::size_t direction = 0; direction < actions.size(); ++direction) {
    oot3d::ui::UiPrimitive p;
    p.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
    p.role = oot3d::ui::UiPrimitiveRole::ActionButton;
    p.owner_address = 0x005D4A8CU;
    p.source_quad = static_cast<std::uint32_t>(direction);
    p.layer = 13;
    p.color = {1, 1, 1, std::clamp(alpha, 0.0F, 1.0F)};
    p.destination = {kCenterX[direction] - kIconSize / 2,
                     kCenterY[direction] - kIconSize / 2, kIconSize, kIconSize};
    if (actions[direction] == TopScreenDpadAction::View) {
      const auto q =
          BuildTopScreenAuxiliaryTouchGeometry(viewState, {}, alpha).Quads[4];
      p.texture = pauseTopPage;
      // Retain native view/gyro/telescope variants and move their anchor only.
      p.destination = {q.Position.X + kCenterX[direction] - kCenterX[0],
                       q.Position.Y + kCenterY[direction] - kCenterY[0],
                       q.Size.X, q.Size.Y};
      p.uv = {q.AtlasOrigin.X / 512, q.AtlasOrigin.Y / 256, q.AtlasSize.X / 512,
              q.AtlasSize.Y / 256};
    } else if (actions[direction] == TopScreenDpadAction::MinimapToggle) {
      // FUN_005D8AB8: map glyph is not an inventory ItemId.
      p.texture = itemIcons;
      p.uv = {420.0F / 512, 380.0F / 512, 42.0F / 512, 42.0F / 512};
    } else {
      const auto item = ResolveItem(actions[direction], state);
      const auto *region =
          item ? oot3d::ui::Oot3dItemIconAtlasRegion(*item) : nullptr;
      if (!region || !region->IsDrawable())
        continue;
      p.texture = itemIcons;
      p.uv = {region->rect.x / 512.0F, region->rect.y / 512.0F,
              region->rect.width / 512.0F, region->rect.height / 512.0F};
    }
    if (!p.texture.semantic_name.empty())
      output.push_back(std::move(p));
  }
  // 005D818C..005D81FC: the original adds cycle marks only for boots/sword.
  // Rect 005E1894 belongs to custom_menu, not either native item atlas.
  for (std::size_t direction = 0; direction < actions.size(); ++direction) {
    const auto action = actions[direction];
    if ((action != TopScreenDpadAction::SwordToggle &&
         action != TopScreenDpadAction::AllBootsToggle) ||
        !ResolveItem(action, state))
      continue;
    oot3d::ui::UiPrimitive p;
    p.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
    p.role = oot3d::ui::UiPrimitiveRole::ActionButton;
    p.owner_address = 0x005D4A8CU;
    p.source_quad = static_cast<std::uint32_t>(4 + direction);
    p.layer = 14;
    p.texture.semantic_name = kTopScreen211MenuAtlasSemantic;
    p.color = {1, 1, 1, std::clamp(alpha, 0.0F, 1.0F)};
    const float shift = action == TopScreenDpadAction::SwordToggle && direction < 2 ? 2.0F : 0.0F;
    p.destination = {kCenterX[direction] - 7.0F + shift,
                     kCenterY[direction] - 8.0F, 14.0F, 14.0F};
    p.uv = {458.0F / 512, 2.0F / 512, 40.0F / 512, 40.0F / 512};
    output.push_back(std::move(p));
  }
  return output.size() - start;
}
} // namespace Oot3dNativeGame
