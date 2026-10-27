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

oot3d::ui::UiPrimitive AtlasCrop(std::uint32_t owner, std::uint32_t source, float x, float y, float width,
                                 float height, oot3d::ui::UiRect destination) {
  auto primitive = Label(owner, source, kTopScreen30UiAtlasSemantic, destination, 1.0F);
  primitive.uv = {x / 512.0F, y / 256.0F, width / 512.0F, height / 256.0F};
  return primitive;
}

void TestPauseAndOcarinaIdentify() {
  // The L / R tab hints (0x005D4A0C glyph quads 2 / 3, 37 x 28): the
  // letter's ink, not the arrow; a held hint (dimmed) keeps its dimming.
  auto left = AtlasCrop(0x005D4A0CU, 2U, 462.0F, 108.0F, 37.0F, 28.0F, {100.0F, 200.0F, 37.0F, 28.0F});
  left.color = {0.55F, 0.55F, 0.55F, 1.0F};
  const auto l = IdentifyTopScreenHudLabel(left);
  Require(l && l->Button == TopScreenHudButton::L && Near(l->CenterX, 124.25F) && Near(l->CenterY, 213.35F) &&
              Near(l->Height, 15.5F) && Near(l->Color.red, 0.55F) && !l->AlignRight && l->KeepLabel,
          "the L tab hint's letter");
  // The hint's key and arrow stay; the glyph follows it, over the letter.
  std::vector<oot3d::ui::UiPrimitive> tabs{left};
  const auto tabStats = ReplaceTopScreenHudButtonLabels(
      tabs, std::nullopt, [](const TopScreenHudLabelSlot &, std::vector<oot3d::ui::UiPrimitive> &output) {
        oot3d::ui::UiPrimitive glyph;
        glyph.texture.semantic_name = "glyph";
        output.push_back(glyph);
        return true;
      });
  Require(tabStats.Replaced == 1U && tabs.size() == 2U && tabs[0].source_quad == 2U &&
              tabs[0].texture.semantic_name == kTopScreen30UiAtlasSemantic && tabs[1].texture.semantic_name == "glyph" &&
              tabs[1].owner_address == 0x005D4A0CU,
          "the tab hint kept under its glyph");
  const auto r = IdentifyTopScreenHudLabel(
      AtlasCrop(0x005D4A0CU, 3U, 240.0F, 117.0F, 37.0F, 28.0F, {300.0F, 200.0F, 37.0F, 28.0F}));
  Require(r && r->Button == TopScreenHudButton::R && Near(r->CenterX, 313.25F), "the R tab hint's letter");
  // Their backings (quads 0, 1) are not letters.
  auto backing = AtlasCrop(0x005D4A0CU, 0U, 401.0F, 196.0F, 18.0F, 18.0F, {65.5F, 206.0F, 41.0F, 42.0F});
  backing.role = oot3d::ui::UiPrimitiveRole::PauseBackground;
  Require(!IdentifyTopScreenHudLabel(backing), "a tab backing is no letter");

  // The item hint's SELECT word (quad 0) ends where the word's ink does; its
  // "?" (quad 1) stays.
  auto select = AtlasCrop(0x005E0E6CU, 0U, 277.0F, 222.0F, 64.0F, 16.0F, {150.0F, 100.0F, 64.0F, 16.0F});
  select.role = oot3d::ui::UiPrimitiveRole::PauseText;
  select.color.alpha = 0.6F;
  const auto s = IdentifyTopScreenHudLabel(select);
  Require(s && s->Button == TopScreenHudButton::Select && s->AlignRight && Near(s->CenterX, 213.0F) &&
              Near(s->CenterY, 109.0F) && Near(s->Height, 14.0F) && Near(s->Color.alpha, 0.6F),
          "the SELECT word");
  auto question = AtlasCrop(0x005E0E6CU, 1U, 344.0F, 222.0F, 16.0F, 16.0F, {217.0F, 98.0F, 20.0F, 20.0F});
  question.role = oot3d::ui::UiPrimitiveRole::PauseText;
  Require(!IdentifyTopScreenHudLabel(question), "the ? stays");

  // The legends' discs, by their crop whoever draws them; other crops of
  // the atlas (the D-pad, a font glyph, the A ring) are not.
  const std::array<std::pair<float, TopScreenHudButton>, 5> discs{{{46.0F, TopScreenHudButton::A},
                                                                   {81.0F, TopScreenHudButton::B},
                                                                   {116.0F, TopScreenHudButton::X},
                                                                   {151.0F, TopScreenHudButton::Y},
                                                                   {186.0F, TopScreenHudButton::Zl}}};
  for (const auto owner : {0x005E9A58U, 0x005E9C3CU, 0x005E9348U, 0x005E34A4U}) {
    for (const auto &[x, button] : discs) {
      auto disc = AtlasCrop(owner, 40U, x, 89.0F, 32.0F, 32.0F, {20.0F, 30.0F, 16.0F, 16.0F});
      disc.role = oot3d::ui::UiPrimitiveRole::PauseText;
      const auto slot = IdentifyTopScreenHudLabel(disc);
      Require(slot && slot->Button == button && Near(slot->CenterX, 28.0F) && Near(slot->CenterY, 38.0F) &&
                  Near(slot->Height, 13.0F),
              "a legend's button disc");
    }
  }
  for (const auto &crop : {std::array<float, 4>{385.0F, 108.0F, 29.0F, 29.0F},
                           std::array<float, 4>{462.0F, 139.0F, 24.0F, 25.0F},
                           std::array<float, 4>{97.0F, 3.0F, 48.0F, 48.0F}}) {
    auto other = AtlasCrop(0x005E9C3CU, 7U, crop[0], crop[1], crop[2], crop[3], {20.0F, 30.0F, 16.0F, 16.0F});
    other.role = oot3d::ui::UiPrimitiveRole::PauseText;
    Require(!IdentifyTopScreenHudLabel(other), "an atlas crop that is not a button disc");
  }
  auto otherTexture = AtlasCrop(0x005E9C3CU, 7U, 116.0F, 89.0F, 32.0F, 32.0F, {20.0F, 30.0F, 16.0F, 16.0F});
  otherTexture.texture.semantic_name = "oot3d/native/pause_shared/item_icons";
  Require(!IdentifyTopScreenHudLabel(otherTexture), "the disc's crop of another texture");

  // The song box's notes: their native type picks the button; the bands,
  // the song markers and a note without its type are not notes.
  const std::array<TopScreenHudButton, 5> noteButtons{TopScreenHudButton::L, TopScreenHudButton::R,
                                                      TopScreenHudButton::Y, TopScreenHudButton::X,
                                                      TopScreenHudButton::A};
  for (std::uint32_t type = 0; type < 5U; ++type) {
    Require(TopScreenOcarinaNoteButton(type) == noteButtons[type], "a note type's button");
    auto note = Label(0x005D2078U, 6U + type, "oot3d/native/pause_shared/ocarina_page",
                      {122.0F + 24.0F * type, 190.0F, 16.0F, 16.0F}, 0.85F);
    note.descriptor_address = kTopScreenOcarinaNoteAtlasTable + 4U * type;
    note.layer = 20U;
    const auto slot = IdentifyTopScreenHudLabel(note);
    Require(slot && slot->Button == noteButtons[type] && Near(slot->CenterX, 130.0F + 24.0F * type) &&
                Near(slot->CenterY, 198.0F) && Near(slot->Height, 15.0F) && slot->Layer == 20U &&
                Near(slot->Color.alpha, 0.85F),
            "a song box note");
  }
  Require(!TopScreenOcarinaNoteButton(5U), "no sixth note");
  auto band = Label(0x005D2078U, 2U, "oot3d/native/pause_shared/ocarina_page", {40.0F, 168.0F, 320.0F, 40.0F});
  band.descriptor_address = kTopScreenOcarinaNoteAtlasTable;
  auto untyped = Label(0x005D2078U, 7U, "oot3d/native/pause_shared/ocarina_page", {146.0F, 190.0F, 16.0F, 16.0F});
  auto marker = Label(0x005D2078U, 14U, "oot3d/native/pause_shared/ocarina_page", {308.0F, 24.0F, 24.0F, 28.0F});
  marker.descriptor_address = kTopScreenOcarinaNoteAtlasTable;
  for (const auto &primitive : {band, untyped, marker})
    Require(!IdentifyTopScreenHudLabel(primitive), "a song box quad that is not a note");
}

void TestStaffNotes() {
  // A message context with three drawn notes (X, A, Y), a hidden note, a
  // text element and a note of another window's models; read without
  // writing.
  NativeA32Memory memory;
  std::string error;
  Require(memory.MapRegion({"staff-context", 0x005C0000U, 0x4000U, true, false, {}}, &error) &&
              memory.MapRegion({"staff-heap", 0x08000000U, 0x40000U, true, false, {}}, &error),
          "map the message context");
  constexpr std::uint32_t context = kTopScreenMessageContext;
  Require(memory.Write8(context + 0x0CU, 1U) && memory.Write8(context + 0xF38U, 6U) &&
              memory.Write8(context + 0x0DU, 1U) && memory.Write8(context + 0x0EU, 1U) &&
              memory.Write8(context + 0x48U, 1U),
          "open the window");
  const auto writeFloats = [&](std::uint32_t address, const std::vector<float> &values) {
    std::vector<std::uint8_t> bytes(values.size() * 4U);
    std::memcpy(bytes.data(), values.data(), bytes.size());
    Require(memory.WriteBytes(address, bytes), "seed floats");
  };
  // The text and note models, each with its visible byte at +0x390.
  for (std::uint32_t model = 0x6AU; model <= 0x9AU; ++model) {
    const std::uint32_t state = 0x08010000U + model * 0x400U;
    Require(memory.Write32(context + 0x168U + model * 4U, state) && memory.Write8(state + 0x390U, 1U),
            "seed a model");
  }
  struct Element {
    std::uint32_t Index, Model, Models;
    bool Visible;
    float X, Y, Scale, Alpha;
  };
  const std::vector<Element> elements{
      {150U, 0x99U, context + 0x168U, true, 130.0F, 202.0F, 1.0F, 1.0F},  // X
      {151U, 0x9AU, context + 0x168U, true, 154.0F, 193.0F, 1.0F, 0.5F},  // A, fading
      {152U, 0x98U, context + 0x168U, true, 178.0F, 207.0F, 1.25F, 1.0F}, // Y, larger
      {153U, 0x96U, context + 0x168U, false, 202.0F, 193.0F, 1.0F, 1.0F}, // hidden
      {100U, 0x6EU, context + 0x168U, true, 124.0F, 171.0F, 1.0F, 1.0F},  // text
      {154U, 0x97U, 0x005C3000U, true, 226.0F, 193.0F, 1.0F, 1.0F},       // other models
  };
  for (const auto &element : elements) {
    const std::uint32_t address = 0x08000000U + element.Index * 0xD0U;
    const std::uint32_t renderer = 0x08020000U + element.Index * 0x100U;
    Require(memory.Write32(context + 0x568U + element.Index * 4U, address) &&
                memory.Write32(address + 0x04U, element.Models) && memory.Write32(address + 0x64U, renderer) &&
                memory.Write32(address + 0x78U, element.Model) &&
                memory.Write8(address + 0x6DU, element.Visible ? 1U : 0U) && memory.Write32(address + 0xB0U, 4U),
            "seed an element");
    writeFloats(renderer + 0x0CU, {element.Scale, 0, 0, element.X, 0, element.Scale, 0, element.Y, 0, 0, 1, -0.01F});
    writeFloats(renderer + 0xF0U, {1.0F, 0.98F, 0.89F, element.Alpha});
  }
  std::vector<std::uint8_t> before(0x40000U), after(0x40000U), contextBefore(0x4000U), contextAfter(0x4000U);
  Require(memory.ReadBytes(0x08000000U, before) && memory.ReadBytes(0x005C0000U, contextBefore), "snapshot");
  std::vector<TopScreenHudLabelSlot> notes;
  Require(ReadTopScreenOcarinaStaffNotes(memory, &notes, &error), "read the staff");
  Require(notes.size() == 3U, "the three drawn notes only");
  Require(notes[0].Button == TopScreenHudButton::X && Near(notes[0].CenterX, 130.0F) &&
              Near(notes[0].CenterY, 202.0F) && Near(notes[0].Height, 15.5F) && Near(notes[0].Color.alpha, 1.0F) &&
              notes[0].Owner == 0x0042CBCCU && notes[0].Descriptor == context,
          "the X note");
  Require(notes[1].Button == TopScreenHudButton::A && Near(notes[1].Color.alpha, 0.5F), "the fading A note");
  Require(notes[2].Button == TopScreenHudButton::Y && Near(notes[2].Height, 15.5F * 1.25F), "the larger Y note");
  Require(memory.ReadBytes(0x08000000U, after) && memory.ReadBytes(0x005C0000U, contextAfter) && before == after &&
              contextBefore == contextAfter,
          "reading the staff changed guest memory");
  // The window's own gates: a state that draws nothing, elements not drawn.
  for (const std::uint8_t state : {0U, 1U, 14U, 15U}) {
    Require(memory.Write8(context + 0xF38U, state), "state");
    Require(ReadTopScreenOcarinaStaffNotes(memory, &notes, &error) && notes.empty(), "a window state without notes");
  }
  Require(memory.Write8(context + 0xF38U, 6U) && memory.Write8(context + 0x48U, 0U), "hide the elements");
  Require(ReadTopScreenOcarinaStaffNotes(memory, &notes, &error) && notes.empty(), "elements not drawn");
  // Unmapped context: an error, never a guess.
  NativeA32Memory empty;
  Require(!ReadTopScreenOcarinaStaffNotes(empty, &notes, &error), "an unmapped message context");

  // The staff's glyphs go last, over the native notes, with their identity.
  std::vector<oot3d::ui::UiPrimitive> list{Label(0x005D2078U, 2U, "band", {40.0F, 168.0F, 320.0F, 40.0F})};
  TopScreenHudLabelSlot overlay;
  overlay.Button = TopScreenHudButton::X;
  overlay.CenterX = 130.0F;
  overlay.CenterY = 202.0F;
  overlay.Height = 15.5F;
  overlay.Layer = 24U;
  overlay.Owner = 0x0042CBCCU;
  overlay.Descriptor = context;
  const auto stats = ReplaceTopScreenHudButtonLabels(
      list, std::nullopt,
      [](const TopScreenHudLabelSlot &slot, std::vector<oot3d::ui::UiPrimitive> &output) {
        oot3d::ui::UiPrimitive glyph;
        glyph.texture.semantic_name = "glyph";
        glyph.layer = slot.Layer;
        output.push_back(glyph);
        return true;
      },
      {overlay});
  Require(stats.Overlays == 1U && list.size() == 2U && list[1].texture.semantic_name == "glyph" &&
              list[1].owner_address == 0x0042CBCCU && list[1].descriptor_address == context && list[1].layer == 24U,
          "the staff glyph over the native note");
}

void TestItemsPageLabels() {
  // The Items page's label model: X, Y, I, II quads (source UVs in PICA's
  // bottom-origin V), drawn positions in lane 1; read without writing.
  NativeA32Memory memory;
  std::string error;
  Require(memory.MapRegion({"items-state", 0x00500000U, 0x10000U, true, false, {}}, &error) &&
              memory.MapRegion({"items-heap", 0x08000000U, 0x10000U, true, false, {}}, &error),
          "map the Items page");
  constexpr std::uint32_t model = 0x08000100U, first = 0x08000200U, owner = 0x08000300U, sources = 0x08000304U,
                          uvs = 0x08001000U, lane1 = 0x08002000U;
  Require(memory.Write32(0x005066F8U + 0x34U, 2U) && memory.Write32(0x005066F8U + 0x30U, model) &&
              memory.Write32(model + 4U, first) && memory.Write32(first + 4U, owner) &&
              memory.Write32(owner, sources) && memory.Write32(sources + 4U, uvs) &&
              memory.Write32(sources + 8U, 0x08003000U) &&
              memory.Write32(owner + 0x10U, 16U) && memory.Write32(owner + 0x124U, 1U) &&
              memory.Write32(owner + 0x1A0U + 4U, lane1),
          "seed the label model");
  const auto writeFloats = [&](std::uint32_t address, const std::vector<float> &values) {
    std::vector<std::uint8_t> bytes(values.size() * 4U);
    std::memcpy(bytes.data(), values.data(), bytes.size());
    Require(memory.WriteBytes(address, bytes), "seed floats");
  };
  struct Quad {
    float U, V, W, H, X, Y;
  };
  const std::array<Quad, 4> quads{{{67, 51, 11, 11, 270, 67}, // X
                                   {83, 51, 11, 11, 260, 119}, // Y
                                   {63, 34, 18, 13, 267, 8},   // I
                                   {80, 34, 18, 13, 268, 192}}}; // II
  for (std::uint32_t index = 0; index < quads.size(); ++index) {
    const auto &q = quads[index];
    const float u0 = q.U / 128.0F, u1 = (q.U + q.W) / 128.0F;
    const float vTop = 1.0F - q.V / 128.0F, vBottom = 1.0F - (q.V + q.H) / 128.0F;
    writeFloats(uvs + index * 0x20U, {u0, vTop, u1, vTop, u0, vBottom, u1, vBottom});
    writeFloats(lane1 + index * 0x30U,
                {q.X, q.Y, 0, q.X + q.W, q.Y, 0, q.X, q.Y + q.H, 0, q.X + q.W, q.Y + q.H, 0});
  }
  std::vector<std::uint8_t> before(0x10000U), after(0x10000U);
  Require(memory.ReadBytes(0x08000000U, before), "snapshot");
  std::vector<TopScreenHudLabelSlot> labels;
  Require(ReadTopScreenItemsPageButtonLabels(memory, &labels, &error) && labels.size() == 4U, "X, Y, I, II");
  Require(labels[0].Button == TopScreenHudButton::X && Near(labels[0].CenterX, 355.5F) &&
              Near(labels[0].CenterY, 72.5F) && Near(labels[0].Height, std::hypot(11.0F, 11.0F)) &&
              labels[0].Owner == 0x005066F8U && labels[0].Descriptor == model,
          "the X label on the upper screen");
  Require(labels[1].Button == TopScreenHudButton::Y && Near(labels[1].CenterX, 345.5F) &&
              Near(labels[1].CenterY, 124.5F),
          "the Y label");
  // SingleScreen's I and II slots hold the ZR and ZL items.
  Require(labels[2].Button == TopScreenHudButton::Zr && Near(labels[2].CenterX, 356.0F) &&
              Near(labels[2].CenterY, 14.5F) && Near(labels[2].Height, std::hypot(18.0F, 13.0F)),
          "the I label is ZR's");
  Require(labels[3].Button == TopScreenHudButton::Zl && Near(labels[3].CenterY, 198.5F), "the II label is ZL's");
  Require(memory.ReadBytes(0x08000000U, after) && before == after, "reading the labels changed guest memory");
  // Opening, closed, or drawing none of its models: the native letters stay.
  for (const std::uint32_t state : {1U, 13U, 0U}) {
    Require(memory.Write32(0x005066F8U + 0x34U, state), "page state");
    Require(ReadTopScreenItemsPageButtonLabels(memory, &labels, &error) && labels.empty(), "a page not at rest");
  }
  Require(memory.Write32(0x005066F8U + 0x34U, 2U) && memory.Write32(0x005066F8U + 0x60U, 1U), "suppress");
  Require(ReadTopScreenItemsPageButtonLabels(memory, &labels, &error) && labels.empty(), "models not drawn");
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
  TestPauseAndOcarinaIdentify();
  TestStaffNotes();
  TestItemsPageLabels();
  TestSetting();
}
