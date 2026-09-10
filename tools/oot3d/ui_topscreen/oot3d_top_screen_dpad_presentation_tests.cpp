#include "oot3d_top_screen_dpad_presentation.h"
#include "oot3d_ui/ui_pause_atlas_regions.h"

#include <cstdlib>
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
      require(result.size() == (visible ? 4 : 0),
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
    }
  }
  state.Child = false;
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
