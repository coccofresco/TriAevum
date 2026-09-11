#include "oot3d_top_screen_dpad_presentation.h"
#include "oot3d_ui/ui_pause_atlas_regions.h"

#include <cstdlib>
#include <cmath>
#include <iostream>

void RunTopScreenDpadPresentationTests() {
  using namespace Oot3dNativeGame;
  const auto require = [](bool value, const char *message) {
    if (!value) {
      std::cerr << message << '\n';
      std::exit(1);
    }
  };
  TopScreenUiConfig config;
  TopScreenDpadPresentationState state;
  state.OwnedEquipment = 0x7777;
  state.EquippedEquipment = 0x1122;
  state.Ocarina = 8;
  state.ItemZr = 0x0E;
  state.ItemZl = 6;
  state.Boomerang = state.Slingshot = true;
  oot3d::ui::UiTextureIdentity items, page;
  items.semantic_name = "native-items";
  page.semantic_name = "native-pause";
  const auto render = [&] {
    std::vector<oot3d::ui::UiPrimitive> result;
    AppendTopScreenDpadPresentation(config, state, {}, 1, items, page, result);
    return result;
  };
  require(render().size() == 4,
          "default adult D-pad must show view, ocarina and both boots");
  for (unsigned action = 0; action < 14; ++action) {
    for (bool child : {false, true}) {
      state.Child = child;
      config.AdultDpad.fill(static_cast<TopScreenDpadAction>(action));
      config.ChildDpad.fill(static_cast<TopScreenDpadAction>(action));
      auto result = render();
      const bool adultOnly = action == 3 || action == 4 || action == 7 ||
                             action == 8 || action == 12 || action == 13;
      const bool childOnly = action == 10 || action == 11;
      const bool visible =
          action != 0 && !(child && adultOnly) && !(!child && childOnly);
      const bool badges = visible && (action == 7 || action == 8);
      require(result.size() == (visible ? (badges ? 8 : 4) : 0),
              "D-pad action age/availability mismatch");
      for (std::size_t i = 0; i < result.size(); ++i) {
        require(result[i].source_quad == i &&
                    result[i].owner_address == 0x005D4A8C,
                "mapped icons need stable direction identity");
        require(result[i].uv.x >= 0 && result[i].uv.y >= 0 &&
                    result[i].uv.x + result[i].uv.width <= 1 &&
                    result[i].uv.y + result[i].uv.height <= 1,
                "mapped icon must fit its native atlas");
      }
      if (badges) {
        for (std::size_t direction = 0; direction < 4; ++direction) {
          const auto &icon = result[direction];
          const auto &badge = result[4 + direction];
          require(badge.texture.semantic_name != icon.texture.semantic_name &&
                      badge.uv.x == 458.0F / 512 && badge.uv.y == 2.0F / 512 &&
                      badge.uv.width == 40.0F / 512 && badge.uv.height == 40.0F / 512,
                  "cycle mark must use the original custom-menu atlas rect");
          require(badge.destination.x == icon.destination.x +
                      ((action == 7 && direction < 2) ? 2.0F : 0.0F) &&
                      badge.destination.y == icon.destination.y - 1.0F &&
                      badge.layer > icon.layer,
                  "cycle mark must retain native direction offsets and order");
        }
      }
    }
  }
  state.Child = false;
  config.AdultDpad = {TopScreenDpadAction::ItemZr,
                     TopScreenDpadAction::ItemZl,
                     TopScreenDpadAction::Ocarina,
                     TopScreenDpadAction::IronBoots};
  state.ItemOpacity = {0.5F, 0.25F, 0.0F};
  std::vector<oot3d::ui::UiPrimitive> dimmed;
  AppendTopScreenDpadPresentation(config, state, {}, 0.6F, items, page, dimmed);
  require(dimmed.size() == 4 &&
              std::abs(dimmed[0].color.alpha - 0.3F) < 0.0001F &&
              std::abs(dimmed[1].color.alpha - 0.15F) < 0.0001F &&
              dimmed[2].color.alpha == 0.0F &&
              std::abs(dimmed[3].color.alpha - 0.6F) < 0.0001F,
          "native item opacity must multiply HUD alpha once, not affect boots");
  state.ItemOpacity = {};
  config.AdultDpad.fill(TopScreenDpadAction::None);
  config.AdultDpad[0] = TopScreenDpadAction::TunicToggle;
  auto before = render();
  require(before.size() == 1, "None must not emit old default icons");
  state.EquippedEquipment = 0x1222;
  auto after = render();
  const auto *goron =
      oot3d::ui::Oot3dItemIconAtlasRegion(oot3d::ui::ItemId::ITEM_TUNIC_GORON);
  require(after.size() == 1 && after[0].uv.x == goron->rect.x / 512.0F &&
              before[0].uv.x != after[0].uv.x,
          "equipment icon must follow native equipped state");
  state.OwnedEquipment = 0x1111;
  require(render().empty(), "single owned tunic must not offer a cycle");
  config.AdultDpad[0] = TopScreenDpadAction::View;
  config.RenderDpadIcons = false;
  require(render().empty(),
          "D-pad option must suppress view as well as item icons");
  config.RenderDpadIcons = true;
  config.RenderHud = false;
  require(render().empty(), "HUD suppression must include mapped icons");
  std::cout << "TopScreen mapped D-pad presentation contracts passed\n";
}
