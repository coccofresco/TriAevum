#include "oot3d_top_screen_dpad_presentation.h"

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
    const oot3d::ui::UiTextureIdentity &uiAtlas,
    std::vector<oot3d::ui::UiPrimitive> &output) {
  if (!config.RenderHud || !(alpha > 0) ||
      (viewState.PauseState >= 12U && viewState.PauseState <= 19U) ||
      viewState.PauseState == 23U)
    return 0;
  const auto start = output.size();
  const auto &actions = state.Child ? config.ChildDpad : config.AdultDpad;
  const bool navi = viewState.HasPlayState && viewState.StateType == 3U &&
                    viewState.StateSubtype == 2U && viewState.PlayerHudMode != 0;
  const auto view =
      BuildTopScreenAuxiliaryTouchGeometry(viewState, {}, alpha).Quads[4];
  constexpr std::array<float, 4> kViewLaneOffsetX{1.0F, 2.0F, 0.0F, 0.0F};
  const auto firstView = std::find(actions.begin(), actions.end(), TopScreenDpadAction::View);
  std::uint32_t viewCount = 0U;
  const auto appendView = [&](std::size_t direction, bool laneCorrection) {
    if (!view.Visible || uiAtlas.semantic_name.empty()) return;
    oot3d::ui::UiPrimitive p;
    p.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
    p.role = oot3d::ui::UiPrimitiveRole::ActionButton;
    p.owner_address = 0x005D4A8CU;
    p.source_quad = 38U + viewCount++;
    p.layer = 13U;
    p.texture = uiAtlas;
    p.color = view.Color;
    p.destination = {
        view.Position.X + kCenterX[direction] - kCenterX[0] +
            (laneCorrection ? kViewLaneOffsetX[direction] : 0.0F),
        view.Position.Y + kCenterY[direction] - kCenterY[0],
        view.Size.X, view.Size.Y};
    p.uv = {view.AtlasOrigin.X / 512.0F, view.AtlasOrigin.Y / 256.0F,
            view.AtlasSize.X / 512.0F, view.AtlasSize.Y / 256.0F};
    output.push_back(std::move(p));
  };
  if (config.RenderDpadIcons && firstView != actions.end()) {
    for (std::size_t direction = 0; direction < actions.size(); ++direction)
      if (actions[direction] == TopScreenDpadAction::View) appendView(direction, true);
  } else if (navi) {
    // 005D77E0 / 005D9054: unmapped Navi is centered at (30,45).
    // With display disabled, use the FIRST mapped center, without +1/+2.
    const auto direction = firstView == actions.end()
                               ? 0U : static_cast<std::size_t>(firstView - actions.begin());
    appendView(direction, false);
  }
  // Unused 38..41 slots are parked at (500,300) with alpha 0 in the payload;
  // host presentation is rebuilt each frame, so simply omit those slots.
  if (!config.RenderDpadIcons) return output.size() - start;
  for (std::size_t direction = 0; direction < actions.size(); ++direction) {
    if (actions[direction] == TopScreenDpadAction::View) continue;
    oot3d::ui::UiPrimitive p;
    p.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
    p.role = oot3d::ui::UiPrimitiveRole::ActionButton;
    p.owner_address = 0x005D4A8CU;
    p.source_quad = static_cast<std::uint32_t>(direction);
    p.layer = 13;
    p.color = {1, 1, 1, std::clamp(alpha, 0.0F, 1.0F)};
    // 005D7D68 / 005D7C9C / 005D7C6C multiply native lane alpha once.
    if (actions[direction] == TopScreenDpadAction::ItemZr)
      p.color.alpha *= state.ItemOpacity.ItemZr;
    else if (actions[direction] == TopScreenDpadAction::ItemZl)
      p.color.alpha *= state.ItemOpacity.ItemZl;
    else if (actions[direction] == TopScreenDpadAction::Ocarina)
      p.color.alpha *= state.ItemOpacity.Ocarina;
    else
      p.color.alpha *= state.GroupAlpha;
    p.destination = {kCenterX[direction] - kIconSize / 2,
                     kCenterY[direction] - kIconSize / 2, kIconSize, kIconSize};
    if (actions[direction] == TopScreenDpadAction::MinimapToggle) {
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
  // 3.0 draws no HUD cycle marks; the mark keeps the 2.1.1 placement and
  // uses the 3.0 cycle badge (custom_ui01 rect 0x0060A718, 512x256).
  for (std::size_t direction = 0;
       !uiAtlas.semantic_name.empty() && direction < actions.size(); ++direction) {
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
    p.texture = uiAtlas;
    p.color = {1, 1, 1, std::clamp(alpha, 0.0F, 1.0F) * state.GroupAlpha};
    const float shift = action == TopScreenDpadAction::SwordToggle && direction < 2 ? 2.0F : 0.0F;
    p.destination = {kCenterX[direction] - 7.0F + shift,
                     kCenterY[direction] - 8.0F, 14.0F, 14.0F};
    p.uv = {320.0F / 512, 3.0F / 256, 40.0F / 512, 40.0F / 256};
    output.push_back(std::move(p));
  }
  return output.size() - start;
}
} // namespace Oot3dNativeGame
