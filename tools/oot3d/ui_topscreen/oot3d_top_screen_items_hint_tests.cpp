#include "oot3d_top_screen_items_hint.h"
#include "oot3d_top_screen_dpad_picker.h"

#include "oot3d_ui/ui_backend.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
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
          "the recovered Items hint geometry differs from 3.0 0x005E0E6C");

  oot3d::ui::UiTextureIdentity hintAtlas;
  hintAtlas.semantic_name = kTopScreen30UiAtlasSemantic;
  std::vector<oot3d::ui::UiPrimitive> primitives;
  Require(
      AppendTopScreenItemsHintPresentation(geometry, hintAtlas, primitives) ==
              2U &&
          primitives.size() == 2U &&
          primitives[0].subsystem == oot3d::ui::UiSubsystem::Items &&
          primitives[0].texture.semantic_name == kTopScreen30UiAtlasSemantic &&
          Near(primitives[0].destination.x, 136.7F) &&
          Near(primitives[0].destination.width, 32.0F) &&
          Near(primitives[0].uv.x, 277.0F / 512.0F) &&
          Near(primitives[0].uv.y, 222.0F / 256.0F) &&
          Near(primitives[0].uv.width, 64.0F / 512.0F) &&
          Near(primitives[0].uv.height, 16.0F / 256.0F) &&
          Near(primitives[1].uv.x, 344.0F / 512.0F) &&
          Near(primitives[1].uv.y, 222.0F / 256.0F),
      "the Items hint does not use the custom_ui01 SELECT and ? rects");
  primitives.clear();
  Require(AppendTopScreenItemsHintPresentation(geometry, {}, primitives) == 0U,
          "the Items hint drew without its atlas");

  // 0x005E2A30 / 0x005E2A34 clamp native X to [-80, 318]: canvas [0, 398]
  // at projection 0.6, [4, 402] at 0.58.
  auto right = state;
  right.Cursor.CenterX = 390.0F;
  const auto rightGeometry = BuildTopScreenItemsHintGeometry(right, true);
  Require(Near(rightGeometry.Quads[1].X + rightGeometry.Quads[1].Width, 398.0F),
          "right-column hint still clamps to the old 320-wide page");
  auto left = state;
  left.Cursor.CenterX = 10.0F;
  left.Cursor.ScreenOffsetX = 84.0F;
  Require(Near(BuildTopScreenItemsHintGeometry(left, true).Quads[0].X, 4.0F),
          "the left clamp ignores the 0.58 grid projection");
  auto gridRight = right;
  gridRight.Cursor.ScreenOffsetX = 84.0F;
  gridRight.Cursor.CenterX = 420.0F;
  const auto gridRightQuad = BuildTopScreenItemsHintGeometry(gridRight, true).Quads[1];
  Require(Near(gridRightQuad.X + gridRightQuad.Width, 402.0F),
          "the right clamp ignores the 0.58 grid projection");

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


  TopScreenItemsShortcutState shortcuts;
  TopScreenItemsShortcutDrawState shortcutDraw;
  shortcutDraw.Active = true;
  shortcutDraw.Ocarina = 8;
  shortcutDraw.BootsOwned = true;
  shortcutDraw.Boots = 0x45;
  oot3d::ui::UiTextureIdentity nativeIcons, uiAtlas;
  nativeIcons.semantic_name = "oot3d/native/items";
  uiAtlas.semantic_name = kTopScreen30UiAtlasSemantic;
  primitives.clear();
  Require(AppendTopScreenItemsShortcutPresentation(shortcuts, shortcutDraw, nativeIcons, uiAtlas, primitives) == 5,
          "shortcut icons or the boots cycle badge are missing");
  Require(Near(primitives[0].destination.x, 47) && Near(primitives[0].destination.y, 7) &&
              Near(primitives[0].destination.width, 42) && Near(primitives[0].uv.x, 420.5F / 512) &&
              Near(primitives[0].uv.width, 41.0F / 512), "Minimap glyph differs from 0x005E2DF4");
  Require(primitives[1].texture.semantic_name == kTopScreen30UiAtlasSemantic &&
              Near(primitives[1].destination.x, 48) && Near(primitives[1].uv.x, 452.0F / 512) &&
              Near(primitives[1].uv.y, 73.0F / 256) &&
              Near(primitives[1].destination.height, 42.0F * 30 / 47),
          "View icon uses the wrong atlas, aspect, or 256-high UV normalization");
  Require(Near(primitives[2].destination.y, 103) && Near(primitives[2].uv.x, 336.0F / 512) &&
              Near(primitives[3].uv.x, 378.0F / 512) && Near(primitives[3].uv.y, 210.0F / 512) &&
              primitives[4].texture.semantic_name == kTopScreen30UiAtlasSemantic &&
              Near(primitives[4].uv.x, 320.0F / 512) && Near(primitives[4].uv.y, 3.0F / 256),
          "native Ocarina/boots atlas or cycle badge differs from recovered rectangles");
  shortcuts.SelectedRow = 2;
  primitives.clear();
  AppendTopScreenItemsShortcutPresentation(shortcuts, shortcutDraw, nativeIcons, uiAtlas, primitives);
  Require(Near(primitives[2].destination.x, 42) && Near(primitives[2].destination.y, 98) &&
              Near(primitives[2].destination.width, 52), "selected shortcut did not grow around its center");
  shortcuts.Order = {2, 0, 3, 1};
  shortcutDraw.Child = true;
  primitives.clear();
  AppendTopScreenItemsShortcutPresentation(shortcuts, shortcutDraw, nativeIcons, uiAtlas, primitives);
  Require(Near(primitives[0].uv.x, 336.0F / 512) && Near(primitives[2].color.red, 0.3F) &&
              Near(primitives[3].color.red, 0.3F) && Near(primitives[4].uv.x, 452.0F / 512),
          "permutation or child boots visibility/availability was lost");
  shortcutDraw.Ocarina = 0xFF;
  shortcutDraw.BootsOwned = false;
  primitives.clear();
  Require(AppendTopScreenItemsShortcutPresentation(shortcuts, shortcutDraw, nativeIcons, uiAtlas, primitives) == 2,
          "unowned Ocarina or incomplete boots pair remained visible");
  shortcutDraw.Dimmed = true;
  primitives.clear();
  AppendTopScreenItemsShortcutPresentation(shortcuts, shortcutDraw, nativeIcons, uiAtlas, primitives);
  Require(Near(primitives[0].color.red, 0.3F) && Near(primitives[1].color.red, 0.3F),
          "native assignment state did not dim the shortcut column");
  Require(Near(primitives[0].color.alpha, 1.0F) && Near(primitives[1].color.alpha, 1.0F),
          "dimmed shortcuts became translucent instead of darkening RGB");
  shortcuts.MovingRow = 1;
  shortcuts.PulseFrame = 8;
  primitives.clear();
  AppendTopScreenItemsShortcutPresentation(shortcuts, shortcutDraw, nativeIcons, uiAtlas, primitives);
  Require(primitives.size() == 3 && primitives[1].source_quad == 9 &&
              Near(primitives[1].color.red, 1.0F) && Near(primitives[1].color.alpha, 0.5F),
          "reorder highlight did not pulse over the dimmed icon");
  shortcutDraw.Active = false;
  primitives.clear();
  Require(AppendTopScreenItemsShortcutPresentation(shortcuts, shortcutDraw, nativeIcons, uiAtlas, primitives) == 0,
          "inactive Items emitted shortcut icons");


  TopScreenDpadPickerState picker;
  TopScreenDpadPickerDrawState pickerDraw;
  pickerDraw.Items[2] = 8;
  pickerDraw.Items[8] = 0x45;
  OpenTopScreenDpadPicker(picker, TopScreenDpadAction::MinimapToggle, true,
                         {TopScreenDpadAction::View, TopScreenDpadAction::Ocarina,
                          TopScreenDpadAction::AllBootsToggle, TopScreenDpadAction::None}, 1);
  TopScreenDpadPickerHoldCursor holdCursor;
  const auto presentPicker = [&] {
    primitives.clear();
    return AppendTopScreenDpadPickerPresentation(picker, pickerDraw, nativeIcons, uiAtlas, primitives,
                                                &holdCursor);
  };
  const auto findPicker = [&](unsigned source) -> const oot3d::ui::UiPrimitive & {
    for (const auto &p : primitives) if (p.source_quad == source) return p;
    Fail("expected picker primitive missing");
  };
  Require(presentPicker() == 0, "picker appeared before its opening fade");
  picker.Frame = 4;
  presentPicker();
  Require(Near(findPicker(0).destination.width, 400) && Near(findPicker(0).color.alpha, 0.5F) &&
              Near(findPicker(1).destination.x, 127.85F) && Near(findPicker(1).destination.y, 40.45F),
          "picker opening scale/dimmer/panel placement differs from 0x005E9348");
  picker.Phase = TopScreenDpadPickerPhase::Choosing;
  picker.Frame = 0;
  presentPicker();
  Require(Near(findPicker(1).destination.x, 122) && Near(findPicker(1).destination.y, 34) &&
              Near(findPicker(5).destination.width, 132) && Near(findPicker(5).destination.height, 160) &&
              Near(findPicker(1).uv.x, 375.0F / 512) && Near(findPicker(9).uv.y, 64.0F / 256),
          "picker nine-slice frame differs from 0x005E70A4");
  Require(Near(findPicker(10).destination.x, 171) && Near(findPicker(10).destination.y, 81) &&
              Near(findPicker(10).destination.width, 58) && Near(findPicker(10).uv.x, 385.0F / 512) &&
              Near(findPicker(10).uv.y, 108.0F / 256) && Near(findPicker(10).uv.width, 29.0F / 512),
          "picker D-pad crop/projection differs from 0x005D1DCC/0x005E517C");
  Require(Near(findPicker(20).destination.x, 187.4F) &&
              Near(findPicker(21).destination.y, 146) && Near(findPicker(22).destination.x, 136) &&
              Near(findPicker(122).uv.x, 320.0F / 512) &&
              Near(findPicker(30).destination.x, 185) && Near(findPicker(30).destination.y, 95),
          "picker current mappings/centre action/cycle badge are misplaced");
  Require(Near(findPicker(40).uv.x, 81.0F / 512) && Near(findPicker(41).uv.x, 116.0F / 512) &&
              findPicker(40).texture.semantic_name == kTopScreen30UiAtlasSemantic,
          "picker Back/Clear controls do not use the native atlas");
  picker.ClearHoldFrames = 2;
  presentPicker();
  Require(holdCursor.Count == 26 && Near(findPicker(90).destination.width, 13) &&
              Near(findPicker(90).destination.x + 6.5F, holdCursor.CenterX) &&
              Near(findPicker(90).destination.y + 6.5F, holdCursor.CenterY),
          "clear-hold cursor is missing or detached from its native X glyph");
  picker.ClearHoldFrames = -1;
  const auto twoRows = presentPicker();
  Require(holdCursor.Count == 0, "released X retained the clear-hold cursor");
  const float twoRowLeft = findPicker(40).destination.x;
  const float twoRowButton = findPicker(40).destination.width;
  // 0x005D289C: a viewable item adds [Y] View Model, refitted to the panel.
  pickerDraw.ViewModel = true;
  Require(presentPicker() == twoRows + 10 && Near(findPicker(42).uv.x, 151.0F / 512) &&
              Near(findPicker(42).uv.y, 89.0F / 256) && findPicker(40).destination.x < twoRowLeft &&
              findPicker(42).destination.width < twoRowButton,
          "picker legend lacks the [Y] View Model row of 0x005E962C");
  float legendRight = 0;
  for (const auto &p : primitives)
    if (p.layer == 36) legendRight = std::max(legendRight, p.destination.x + p.destination.width);
  Require(legendRight <= 278 && twoRowLeft > 122, "three-row picker legend overflows the panel");
  pickerDraw.ViewModel = false;

  // 0x005E9A58: [X] at 0.85 and View Model at 0.62 below the Gear cursor.
  constexpr float unit = 0.422836F;
  const float gearButton = 0.85F * unit * 19 * 1.55F;
  const float gearTotal = gearButton + 2 + (10 + 157) * 0.62F * unit;
  const auto gearLegend = [&](float nativeX, std::uint32_t fade, std::uint32_t pulse) {
    primitives.clear();
    const TopScreenItemsHintCursorBounds gear{true, nativeX + 40, 50, 90, 40, 40};
    return AppendTopScreenGearViewModelLegendPresentation(gear, fade, pulse, uiAtlas, primitives);
  };
  Require(gearLegend(160, 12, 0) == 10 && Near(primitives[0].uv.x, 116.0F / 512) &&
              Near(primitives[0].destination.width, gearButton) &&
              Near(primitives[0].destination.x, 200 - gearTotal / 2 + 1) &&
              Near(primitives[0].destination.y + gearButton / 2, 99) &&
              Near(primitives[1].uv.x, 300.0F / 512) &&
              Near(primitives[1].destination.y, 91 + 1.9365458F + 2 + 3 * 0.62F * unit) &&
              Near(primitives[0].color.alpha, 0.73F) && Near(primitives[9].color.alpha, 0.73F) &&
              primitives[0].layer == 15 && primitives[0].texture.semantic_name == kTopScreen30UiAtlasSemantic,
          "Gear legend glyphs, line or projection differ from 0x005E9A58");
  Require(gearLegend(0, 12, 45) == 10 && Near(primitives[0].destination.x, 43) &&
              Near(primitives[0].color.alpha, 1),
          "Gear legend ignores the native X 2 clamp or the pulse peak");
  Require(gearLegend(330, 6, 60) == 10 && Near(primitives[0].destination.x, 318 - gearTotal + 41) &&
              Near(primitives[0].color.alpha, (0.73F + (2 - 60.0F / 45) * 0.27F) * 0.5F),
          "Gear legend ignores the native X 318 clamp or its 12-update fade");
  Require(gearLegend(160, 0, 0) == 0 && primitives.empty(), "Gear legend drew before its delay");
  primitives.clear();
  Require(AppendTopScreenGearViewModelLegendPresentation({}, 12, 0, uiAtlas, primitives) == 0,
          "Gear legend drew without cursor bounds");

  // 0x005E9C3C: the View Model control line on a centred 24-high panel.
  primitives.clear();
  Require(AppendTopScreenViewModelControlsPresentation(uiAtlas, primitives) == 9 + 4 + 20,
          "View Model controls lost a panel slice, button or glyph");
  Require(Near(primitives[0].destination.x + primitives[2].destination.x + primitives[2].destination.width, 400) &&
              Near(primitives[0].destination.y, 210) &&
              Near(primitives[6].destination.y + primitives[6].destination.height, 234) &&
              Near(primitives[0].uv.x, 375.0F / 512) && primitives[0].layer == 31,
          "View Model control panel differs from 0x005E70A4 at 160-centred Y 210");
  const float controlButton = 0.75F * unit * 19 * 1.55F;
  unsigned buttons = 0;
  for (const auto &p : primitives) {
    if (p.layer != 36 || !Near(p.destination.width, controlButton)) continue;
    Require(Near(p.destination.y + controlButton / 2, 222), "View Model control button is off its line");
    ++buttons;
  }
  Require(buttons == 4 && Near(primitives[9].uv.x, 116.0F / 512) && Near(primitives[16].uv.x, 151.0F / 512) &&
              Near(primitives[24].uv.x, 385.0F / 512) && Near(primitives[24].uv.y, 108.0F / 256) &&
              Near(primitives[9].destination.x, primitives[0].destination.x + 17),
          "View Model controls lost [X] Zoom In / [Y] Zoom Out or the panel padding");
  primitives.clear();
  Require(AppendTopScreenViewModelControlsPresentation({}, primitives) == 0,
          "View Model controls drew without the custom atlas");
  picker.Phase = TopScreenDpadPickerPhase::Confirming;
  picker.Direction = 3;
  picker.Frame = 6;
  picker.Assignments[3] = picker.Action;
  presentPicker();
  Require(Near(findPicker(23).destination.width, 35) &&
              Near(findPicker(31).destination.x, 223.25F) && Near(findPicker(31).color.alpha, 0.75F),
          "confirmed direction does not grow or receive the easing/fading action copy");
  picker.Phase = TopScreenDpadPickerPhase::Closing;
  picker.Frame = 3;
  presentPicker();
  Require(Near(findPicker(0).color.alpha, 0.5F), "picker did not fade on close");
  picker = {};
  Require(presentPicker() == 0 && holdCursor.Count == 0, "closed picker emits stale primitives");

  // 0x005CCA90 / 0x005CBD64: a clockwise annulus, 24 dark quads followed
  // by complete/partial yellow quads. UV corners keep the radial texture.
  const auto ring = BuildTopScreenDpadPickerHoldCursor(200, 192, 0.25F);
  Require(ring.Count == 30 && Near(ring.TextureAlphaScale, 2) &&
              Near(ring.Quads[0].Vertices[0].X, 200) &&
              Near(ring.Quads[0].Vertices[0].Y, 179.866667F) &&
              Near(ring.Quads[0].Vertices[2].Y, 185.533333F) &&
              Near(ring.Quads[29].Vertices[1].X, 212.133333F) &&
              Near(ring.Quads[29].Vertices[1].Y, 192),
          "quarter hold ring radii, winding or angular extent changed");
  Require(Near(ring.Quads[0].Vertices[0].U, 423.0F / 512) &&
              Near(ring.Quads[0].Vertices[0].V, 189.5F / 256) &&
              Near(ring.Quads[0].Vertices[3].U, 428.0F / 512) &&
              Near(ring.Quads[0].Vertices[3].V, 192.5F / 256) &&
              Near(ring.Quads[0].Color.red, 0) && Near(ring.Quads[0].Color.alpha, 1) &&
              Near(ring.Quads[24].Color.red, 1) && Near(ring.Quads[24].Color.green, 0.86F) &&
              Near(ring.Quads[24].Color.blue, 0.12F) && Near(ring.Quads[24].Color.alpha, 1),
          "hold ring native texture coordinates or background/fill colors changed");
  const auto partialRing = BuildTopScreenDpadPickerHoldCursor(0, 0, 1.0F / 48);
  Require(partialRing.Count == 25 && Near(partialRing.Quads[24].Vertices[1].X, 1.583333F) &&
              Near(partialRing.Quads[24].Vertices[1].Y, -12.029581F),
          "hold ring lost its fractional segment or native 16-bit angle rounding");
  Require(BuildTopScreenDpadPickerHoldCursor(0, 0, -1).Count == 24 &&
              BuildTopScreenDpadPickerHoldCursor(0, 0, 0.5F).Count == 36 &&
              BuildTopScreenDpadPickerHoldCursor(0, 0, 2).Count == 48 &&
              BuildTopScreenDpadPickerHoldCursor(0, 0, std::numeric_limits<float>::quiet_NaN()).Count == 0,
          "hold ring boundaries produce invalid or excessive geometry");

  // The A ring follows native cursor bounds, independently of row order.
  for (int row = 0; row < 4; ++row) {
    TopScreenItemsShortcutState held;
    held.SelectedRow = row;
    held.Order = {3, 2, 1, 0};
    TopScreenItemsShortcutInput input;
    input.Active = true;
    input.Row = static_cast<unsigned>(row);
    input.Available.fill(true);
    input.Pressed = input.Held = 1;
    AdvanceTopScreenItemsShortcuts(held, input);
    const TopScreenItemsHintCursorBounds bounds{true, 68, row * 48.0F + 2,
                                                row * 48.0F + 54, 52};
    Require(BuildTopScreenItemsShortcutHoldCursor(held, bounds).Count == 0,
            "A ring appeared on the initial press");
    input.Pressed = 0;
    constexpr std::array<unsigned, 15> counts{0, 0, 28, 29, 31, 32, 34, 36, 37, 39, 40, 42, 44, 45, 47};
    for (unsigned frame = 1; frame < 15; ++frame) {
      AdvanceTopScreenItemsShortcuts(held, input);
      const auto aRing = BuildTopScreenItemsShortcutHoldCursor(held, bounds);
      Require(aRing.Count == counts[frame], "A-hold ring did not follow the 15-update interval");
      primitives.clear();
      const auto glyphCount = AppendTopScreenItemsShortcutHoldPresentation(aRing, uiAtlas, primitives);
      Require(glyphCount == (frame > 1 ? 1U : 0U), "A glyph outlived or preceded its ring");
      if (frame > 1) {
        Require(Near(aRing.CenterX, 104.133333F) && Near(aRing.CenterY, row * 48.0F + 28) &&
                    Near(aRing.TextureAlphaScale, 2) &&
                    Near(primitives[0].destination.x + 6.5F, aRing.CenterX) &&
                    Near(primitives[0].destination.y + 6.5F, aRing.CenterY) &&
                    Near(primitives[0].destination.width, 13) &&
                    Near(primitives[0].uv.x, 46.0F / 512) && Near(primitives[0].uv.y, 89.0F / 256) &&
                    Near(primitives[0].uv.width, 32.0F / 512) &&
                    Near(primitives[0].uv.height, 32.0F / 256) &&
                    primitives[0].texture.semantic_name == kTopScreen30UiAtlasSemantic &&
                    primitives[0].layer == 38 && !primitives[0].texture_alpha_only &&
                    Near(primitives[0].color.alpha, 1),
                "A ring/normal-modulate A glyph lost native placement, atlas or material");
      }
    }
    const auto action = AdvanceTopScreenItemsShortcuts(held, input);
    Require(action.Action == ResolveTopScreenItemsShortcutAction(held.Order, row) &&
                BuildTopScreenItemsShortcutHoldCursor(held, bounds).Count == 0,
            "A ring survived the picker-open update or changed the reordered action");
  }
  TopScreenItemsShortcutState held;
  held.SelectedRow = 0;
  held.HoldFrames = 7;
  TopScreenItemsHintCursorBounds bounds{true, 68, 2, 54, 52};
  const auto heldRing = BuildTopScreenItemsShortcutHoldCursor(held, bounds);
  primitives.clear();
  Require(AppendTopScreenItemsShortcutHoldPresentation(heldRing, {}, primitives) == 0 &&
              primitives.empty(), "missing custom atlas emitted an A glyph");
  bounds.CenterX += 9;
  bounds.Width = 40;
  Require(Near(BuildTopScreenItemsShortcutHoldCursor(held, bounds).CenterX, 107.133333F),
          "A ring used a fixed row position instead of live cursor bounds");
  for (int invalid = 0; invalid < 9; ++invalid) {
    auto state = held;
    auto cursor = bounds;
    switch (invalid) {
    case 0: state.SelectedRow = -1; break;
    case 1: state.SelectedRow = 4; break;
    case 2: state.MovingRow = 0; break;
    case 3: state.SwapFrame = 2; break;
    case 4: cursor.Active = false; break;
    case 5: cursor.Width = 0; break;
    case 6: cursor.MaximumY = cursor.MinimumY; break;
    case 7: cursor.CenterX = std::numeric_limits<float>::quiet_NaN(); break;
    case 8: cursor.MinimumY = std::numeric_limits<float>::infinity(); break;
    }
    Require(BuildTopScreenItemsShortcutHoldCursor(state, cursor).Count == 0,
            "inactive/reordering/invalid cursor emitted an A ring");
  }
  TopScreenItemsShortcutInput release;
  release.Active = true;
  AdvanceTopScreenItemsShortcuts(held, release);
  Require(held.MovingRow == 0 && BuildTopScreenItemsShortcutHoldCursor(held, bounds).Count == 0,
          "releasing A retained the ring or prevented tap reorder");

  std::cout << "oot3d_top_screen_items_hint_tests: ok\n";
  return 0;
}
