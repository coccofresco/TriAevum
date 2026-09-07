#include "oot3d_top_screen_items_hint.h"

#include "oot3d_ui/ui_backend.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

[[noreturn]] void Fail(std::string_view message) {
  std::cerr << "oot3d_top_screen_items_hint_tests: " << message << '\n';
  std::exit(1);
}

void Require(bool condition, std::string_view message) {
  if (!condition) {
    Fail(message);
  }
}

bool Near(float left, float right, float epsilon = 0.0001F) {
  return std::abs(left - right) <= epsilon;
}

} // namespace

int main() {
  using namespace Oot3dNativeGame;

  const TopScreenItemsHintCursorBounds cursor{true, 160.0F, 50.0F, 90.0F,
                                              40.0F};
  TopScreenItemsHintRuntimeState state;
  AdvanceTopScreenItemsHintState(state, cursor);
  for (std::uint32_t frame = 0U; frame < 89U; ++frame) {
    AdvanceTopScreenItemsHintState(state, cursor);
  }
  Require(state.StableFrames == 89U &&
              !BuildTopScreenItemsHintGeometry(state, true).Visible,
          "the official 90-update Items hint delay changed");

  AdvanceTopScreenItemsHintState(state, cursor);
  auto geometry = BuildTopScreenItemsHintGeometry(state, true);
  Require(state.StableFrames == 90U && geometry.Visible &&
              Near(geometry.Quads[0].Alpha, 0.0F),
          "the Items hint did not begin its native fade at update 90");
  for (std::uint32_t frame = 0U; frame < 12U; ++frame) {
    AdvanceTopScreenItemsHintState(state, cursor);
  }
  geometry = BuildTopScreenItemsHintGeometry(state, true);
  Require(state.StableFrames == 102U && geometry.Visible &&
              geometry.Quads[0].Alpha >= 0.73F &&
              geometry.Quads[0].Alpha <= 1.0F &&
              Near(geometry.Quads[0].Alpha, geometry.Quads[1].Alpha) &&
              Near(geometry.Quads[0].X, 136.7F) &&
              Near(geometry.Quads[0].Y, 90.0F) &&
              Near(geometry.Quads[0].Width, 32.0F) &&
              Near(geometry.Quads[0].Height, 8.0F) &&
              Near(geometry.Quads[1].X, 171.7F) &&
              Near(geometry.Quads[1].Y, 88.2F) &&
              Near(geometry.Quads[1].Width, 11.6F) &&
              Near(geometry.Quads[1].Height, 11.6F),
          "the recovered Items hint geometry differs from payload 0x005D2078");

  oot3d::ui::UiTextureIdentity menuAtlas;
  menuAtlas.semantic_name = kTopScreen211MenuAtlasSemantic;
  std::vector<oot3d::ui::UiPrimitive> primitives;
  Require(
      AppendTopScreenItemsHintPresentation(geometry, menuAtlas, primitives) ==
              2U &&
          primitives.size() == 2U &&
          primitives[0].subsystem == oot3d::ui::UiSubsystem::Items &&
          primitives[0].texture.semantic_name ==
              kTopScreen211MenuAtlasSemantic &&
          Near(primitives[0].destination.x, 170.875F) &&
          Near(primitives[0].destination.width, 40.0F) &&
          Near(primitives[0].uv.x, 434.0F / 512.0F) &&
          Near(primitives[0].uv.y, 410.0F / 512.0F) &&
          Near(primitives[1].uv.x, 492.0F / 512.0F) &&
          Near(primitives[1].uv.y, 104.0F / 512.0F),
      "the Items hint was not presented through generic 400x240 primitives");

  AdvanceTopScreenItemsHintState(state,
                                 {true, cursor.CenterX + 0.6F, cursor.MinimumY,
                                  cursor.MaximumY, cursor.Width});
  Require(state.StableFrames == 0U,
          "cursor movement above the native half-pixel threshold was ignored");
  AdvanceTopScreenItemsHintState(state, {});
  Require(!state.Cursor.Active && !state.Tracking && state.StableFrames == 0U,
          "leaving the Items owner did not reset the hint tracker");
  Require(!BuildTopScreenItemsHintGeometry(state, true).Visible &&
              !BuildTopScreenItemsHintGeometry(state, false).Visible,
          "an inactive or disabled Items hint remained visible");

  std::cout << "oot3d_top_screen_items_hint_tests: ok\n";
  return 0;
}
