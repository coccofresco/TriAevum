// TopScreen HUD button labels: which primitives are labels (by their native
// owners, whatever art a pack gives them), where each glyph goes, the A ring's
// letter read from the Quest render buffer without changing it, and an HUD
// list left untouched when nothing replaces a label.
#include "oot3d_native_a32_memory.h"
#include "oot3d_ui/ui_contract_types.h"
#include "oot3d_top_screen_button_labels.h"
#include "oot3d_top_screen_items_shortcuts.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace Oot3dNativeGame;

void Require(bool value, const char *message) {
  if (!value) {
    std::cerr << "button labels: " << message << '\n';
    std::exit(1);
  }
}

bool Near(float a, float b) { return std::abs(a - b) < 1e-3F; }

oot3d::ui::UiPrimitive Label(std::uint32_t owner, std::uint32_t source, std::string_view texture,
                             oot3d::ui::UiRect destination, float alpha = 0.9F) {
  oot3d::ui::UiPrimitive primitive;
  primitive.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
  primitive.role = oot3d::ui::UiPrimitiveRole::TouchControl;
  primitive.owner_address = owner;
  primitive.source_quad = source;
  primitive.texture.semantic_name = std::string(texture);
  primitive.destination = destination;
  primitive.color = {1.0F, 1.0F, 1.0F, alpha};
  primitive.layer = 13U;
  primitive.visible = alpha > 0.0F;
  return primitive;
}

constexpr std::string_view kPauseTopPage = "oot3d/native/pause_shared/pause_top_page";

void TestIdentify() {
  const std::string_view atlas = kTopScreen30UiAtlasSemantic;
  struct Case {
    oot3d::ui::UiPrimitive Primitive;
    TopScreenHudButton Button;
    float X, Y, Height;
  };
  // Destinations as the HUD composes them (16 px labels at HUD 1.0, the B
  // letter 11 px), glyphs 12 px; a scaled HUD scales the slot with them.
  const std::vector<Case> cases{
      {Label(0x005C9940U, 34U, atlas, {334.0F, 7.0F, 16.0F, 16.0F}), TopScreenHudButton::X, 341.5F, 15.5F, 12.0F},
      {Label(0x005C9940U, 35U, atlas, {310.0F, 31.0F, 16.0F, 16.0F}), TopScreenHudButton::Y, 317.5F, 39.5F, 12.0F},
      {Label(0x005C9940U, 36U, atlas, {223.0F, 11.0F, 16.0F, 16.0F}), TopScreenHudButton::B, 230.5F, 19.5F, 12.0F},
      {Label(0x005C9940U, 0U, atlas, {360.0F, 32.0F, 16.0F, 16.0F}), TopScreenHudButton::Zr, 367.5F, 40.5F, 12.0F},
      {Label(0x005C9940U, 1U, atlas, {336.0F, 56.0F, 11.2F, 11.2F}), TopScreenHudButton::Zl, 341.25F, 61.95F, 8.4F},
      {Label(0x005D4A8CU, 34U, kPauseTopPage, {225.0F, 13.0F, 11.0F, 11.0F}), TopScreenHudButton::B, 230.5F, 18.5F,
       12.0F},
  };
  for (const auto &item : cases) {
    const auto slot = IdentifyTopScreenHudLabel(item.Primitive);
    Require(slot && slot->Button == item.Button && Near(slot->CenterX, item.X) && Near(slot->CenterY, item.Y) &&
                Near(slot->Height, item.Height) && slot->Layer == 13U && Near(slot->Color.alpha, 0.9F),
            "a label's slot");
  }
  // Not labels: the rings (layer 12), the view eye (38), B's item (copy 35),
  // the D-pad cross, other owners and roles.
  auto ring = Label(0x005C9940U, 0U, atlas, {338.0F, 5.0F, 32.0F, 32.0F});
  ring.layer = 12U;
  auto item = Label(0x005C9940U, 1U, "oot3d/native/pause_shared/item_icons", {365.0F, 32.0F, 25.0F, 25.0F});
  auto action = Label(0x005C9940U, 5U, atlas, {2.0F, 60.0F, 14.0F, 14.0F});
  action.role = oot3d::ui::UiPrimitiveRole::ActionButton;
  for (const auto &primitive : {ring, item, action, Label(0x005C9940U, 38U, atlas, {27.0F, 40.0F, 16.0F, 10.0F}),
                                Label(0x005D4A8CU, 35U, kPauseTopPage, {234.0F, 12.0F, 26.0F, 27.0F}),
                                Label(0x005D4A8CU, 26U, kPauseTopPage, {8.0F, 215.0F, 18.0F, 18.0F}),
                                Label(0x00123456U, 34U, atlas, {334.0F, 7.0F, 16.0F, 16.0F})})
    Require(!IdentifyTopScreenHudLabel(primitive).has_value(), "a primitive that is not a label");
  // A label hidden this frame has a slot with no alpha.
  const auto hidden = IdentifyTopScreenHudLabel(Label(0x005C9940U, 34U, atlas, {334.0F, 7.0F, 16.0F, 16.0F}, 0.0F));
  Require(hidden && hidden->Color.alpha == 0.0F, "a hidden label keeps no alpha");
}

void TestActionLetter() {
  // The A ring as the Quest render buffer draws it: HUD atlas texels 60..101 x
  // 0..41 on a 32 px quad at (272, 8).
  const std::array<TopScreenVec2, 4> positions{
      TopScreenVec2{272.0F, 8.0F}, {304.0F, 8.0F}, {272.0F, 40.0F}, {304.0F, 40.0F}};
  const oot3d::ui::UiRect uv{60.0F / 256.0F, 0.0F, 41.0F / 256.0F, 41.0F / 256.0F};
  const auto letter = TopScreenActionLetterInQuad(positions, uv, {1.0F, 1.0F, 1.0F, 0.75F});
  const float scale = 32.0F / 41.0F;
  Require(letter && letter->Button == TopScreenHudButton::A && Near(letter->CenterX, 272.0F + 23.5F * scale) &&
              Near(letter->CenterY, 8.0F + 6.5F * scale) && Near(letter->Height, 12.0F * scale * 1.3F) &&
              Near(letter->Color.alpha, 0.75F),
          "the A letter in its ring");
  // The action word's quad (another part of the atlas) holds no letter.
  Require(!TopScreenActionLetterInQuad(positions, {0.0F, 100.0F / 256.0F, 80.0F / 256.0F, 20.0F / 256.0F},
                                       {1.0F, 1.0F, 1.0F, 1.0F}),
          "a word quad has no A letter");

  // Read from guest memory: only the ring near the A group, nothing written.
  NativeA32Memory memory;
  std::string error;
  Require(memory.MapRegion({"button-labels-quest", 0x08000000U, 0x1000U, true, false, {}}, &error), "map");
  constexpr std::uint32_t buffer = 0x08000000U, positionsAddress = 0x08000100U, uvsAddress = 0x08000300U,
                          colorsAddress = 0x08000400U, translationsAddress = 0x08000600U;
  TopScreenUiConfig config;
  config.HudScale = 1.0F;
  const auto group = ResolveTopScreenActionGroup(config);
  Require(memory.Write32(buffer, 2U) && memory.Write32(buffer + 0x10U, positionsAddress) &&
              memory.Write32(buffer + 0x14U, uvsAddress) && memory.Write32(buffer + 0x18U, colorsAddress) &&
              memory.Write32(buffer + 0x1CU, translationsAddress),
          "seed the render buffer");
  const auto writeFloats = [&](std::uint32_t address, const std::vector<float> &values) {
    std::vector<std::uint8_t> bytes(values.size() * 4U);
    std::memcpy(bytes.data(), values.data(), bytes.size());
    Require(memory.WriteBytes(address, bytes), "seed floats");
  };
  // Quad 0: a ring far from A (another button); quad 1: the A ring.
  const float left = group.X - 16.0F, top = group.Y - 16.0F;
  writeFloats(positionsAddress, {10, 10, 0, 42, 10, 0, 10, 42, 0, 42, 42, 0});
  writeFloats(positionsAddress + 0x30U, {left, top, 0, left + 32, top, 0, left, top + 32, 0, left + 32, top + 32, 0});
  // PICA V is bottom-origin: v 1 at the top.
  const float u0 = 60.0F / 256.0F, u1 = 101.0F / 256.0F, vTop = 1.0F, vBottom = 1.0F - 41.0F / 256.0F;
  for (std::uint32_t quad = 0; quad < 2U; ++quad) {
    writeFloats(uvsAddress + quad * 0x20U, {u0, vTop, u1, vTop, u0, vBottom, u1, vBottom});
    writeFloats(colorsAddress + quad * 0x40U, {1, 1, 1, 0.5F, 1, 1, 1, 0.5F, 1, 1, 1, 0.5F, 1, 1, 1, 0.5F});
    writeFloats(translationsAddress + quad * 8U, {0.0F, 0.0F});
  }
  std::vector<std::uint8_t> before(0x1000U), after(0x1000U);
  Require(memory.ReadBytes(0x08000000U, before), "snapshot");
  std::optional<TopScreenHudLabelSlot> read;
  Require(ReadTopScreenActionLetter(memory, buffer, config, &read, &error) && read &&
              Near(read->CenterX, left + 23.5F * scale) && Near(read->CenterY, top + 6.5F * scale) &&
              Near(read->Color.alpha, 0.5F),
          "the A ring read from the render buffer");
  Require(memory.ReadBytes(0x08000000U, after) && before == after, "reading the A ring changed guest memory");
  Require(memory.Write32(buffer, 0U), "empty buffer");
  Require(ReadTopScreenActionLetter(memory, buffer, config, &read, &error) && !read, "no quads, no letter");
}

void TestReplace() {
  const std::string_view atlas = kTopScreen30UiAtlasSemantic;
  oot3d::ui::UiPrimitive heart;
  heart.role = oot3d::ui::UiPrimitiveRole::Heart;
  heart.destination = {12.0F, 7.0F, 9.0F, 9.0F};
  heart.layer = 10U;
  auto icon = Label(0x005C9940U, 3U, "oot3d/native/pause_shared/item_icons", {339.0F, 8.0F, 25.0F, 25.0F}, 1.0F);
  icon.role = oot3d::ui::UiPrimitiveRole::ActionButton;
  const std::vector<oot3d::ui::UiPrimitive> hud{
      heart, Label(0x005C9940U, 34U, atlas, {334.0F, 7.0F, 16.0F, 16.0F}), icon,
      Label(0x005C9940U, 0U, atlas, {360.0F, 32.0F, 16.0F, 16.0F}),
      Label(0x005D4A8CU, 34U, kPauseTopPage, {225.0F, 13.0F, 11.0F, 11.0F}, 1.0F)};
  const auto marker = [](const TopScreenHudLabelSlot &slot, std::vector<oot3d::ui::UiPrimitive> &output) {
    oot3d::ui::UiPrimitive glyph;
    glyph.texture.semantic_name = "glyph";
    glyph.destination = {slot.CenterX, slot.CenterY, slot.Height, slot.Height};
    glyph.layer = slot.Layer;
    glyph.color.alpha = slot.Color.alpha;
    output.push_back(glyph);
    output.push_back(glyph);
    return slot.Button != TopScreenHudButton::Zr; // ZR unbound: its label stays
  };

  // Every label replaced in place; the order (and so what covers what) kept.
  auto replaced = hud;
  TopScreenHudLabelSlot letter;
  letter.Button = TopScreenHudButton::A;
  letter.Color.alpha = 0.8F;
  const auto stats = ReplaceTopScreenHudButtonLabels(replaced, letter, marker);
  Require(stats.Labels == 3U && stats.Replaced == 3U && stats.ActionLetter, "replacement counts");
  Require(replaced.size() == 9U, "replaced list size");
  Require(replaced[0].role == oot3d::ui::UiPrimitiveRole::Heart, "the heart first");
  Require(replaced[1].texture.semantic_name == "glyph" && replaced[2].texture.semantic_name == "glyph" &&
              replaced[1].owner_address == 0x005C9940U && replaced[1].source_quad == 34U,
          "X's glyph where its label was, with the label's identity");
  Require(replaced[3].role == oot3d::ui::UiPrimitiveRole::ActionButton, "the item icon still covers X's glyph");
  Require(replaced[4].texture.semantic_name == atlas && replaced[4].source_quad == 0U, "ZR keeps its label");
  Require(replaced[5].owner_address == 0x005D4A8CU && replaced[6].texture.semantic_name == "glyph", "B's glyph");
  Require(replaced[7].owner_address == 0x0042B884U && Near(replaced[8].color.alpha, 0.8F),
          "the A glyph last, over the native ring");

  // Nothing bound: the list comes back as it was, primitive for primitive.
  auto kept = hud;
  const auto none = ReplaceTopScreenHudButtonLabels(kept, std::nullopt,
                                                    [](const TopScreenHudLabelSlot &, auto &) { return false; });
  Require(none.Replaced == 0U && !none.ActionLetter && kept.size() == hud.size(), "an unbound HUD");
  for (std::size_t index = 0; index < hud.size(); ++index)
    Require(std::memcmp(&kept[index].destination, &hud[index].destination, sizeof(oot3d::ui::UiRect)) == 0 &&
                kept[index].texture.semantic_name == hud[index].texture.semantic_name &&
                kept[index].source_quad == hud[index].source_quad && kept[index].owner_address == hud[index].owner_address,
            "an unbound HUD changed");
}

void TestSetting() {
  TopScreenUiConfig config;
  Require(config.HudButtonLabels == TopScreenHudButtonLabels::Controller, "Controller is the default");
  std::string text, error;
  Require(SerializeTopScreenUiConfigText(config, &text, &error) && text.find("hud_button_labels") == std::string::npos,
          "the default is not written (older builds reject the key)");
  config.HudButtonLabels = TopScreenHudButtonLabels::Original;
  TopScreenUiConfig parsed;
  Require(SerializeTopScreenUiConfigText(config, &text, &error) &&
              text.find("hud_button_labels") != std::string::npos &&
              ParseTopScreenUiConfigText(text, &parsed, &error) && parsed == config,
          "Original round-trips");
  Require(ParseTopScreenUiConfigText(R"({"schema":"oot3d_topscreen_ui_v2","hud_button_labels":"controller"})", &parsed,
                                     &error) &&
              parsed.HudButtonLabels == TopScreenHudButtonLabels::Controller,
          "controller parses");
  Require(!ParseTopScreenUiConfigText(R"({"schema":"oot3d_topscreen_ui_v2","hud_button_labels":"xbox"})", &parsed,
                                      &error),
          "an unknown label mode is refused");
}

} // namespace

void RunTopScreenButtonLabelsTests() {
  TestIdentify();
  TestActionLetter();
  TestReplace();
  TestSetting();
}
