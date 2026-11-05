// The HUD button labels' owners together: the controls owner's bindings and
// prompt family give each HUD button's glyph (every family, the keyboard's
// keys and mouse, unbound buttons), the TopScreen setting turns it off, and
// a composed HUD gets its glyphs in place of its letters.
#include "oot3d_hud_button_labels.h"
#include "oot3d_menu_font.h"
#include "oot3d_ui/ui_contract_types.h"
#include "oot3d_top_screen_items_shortcuts.h"

#include <cstdlib>
#include <iostream>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

using namespace Oot3dNativeGame;
using Menu::ButtonGlyph;
using Menu::ButtonGlyphFrame;
using Menu::ButtonGlyphIcon;

void Require(bool value, const char *message) {
  if (!value) {
    std::cerr << "oot3d_hud_button_labels_tests: " << message << '\n';
    std::exit(1);
  }
}

const std::optional<ButtonGlyph> &GlyphOf(const TopScreenHudButtonGlyphs &glyphs, TopScreenHudButton button) {
  return glyphs[static_cast<std::size_t>(button)];
}

void TestActive() {
  for (const auto set : {NativePromptSet::Xbox, NativePromptSet::PlayStation, NativePromptSet::Nintendo,
                         NativePromptSet::SteamDeck, NativePromptSet::Generic, NativePromptSet::Keyboard}) {
    Require(TopScreenHudButtonGlyphsActive(TopScreenHudButtonLabels::Controller, set), "Controller draws glyphs");
    Require(!TopScreenHudButtonGlyphsActive(TopScreenHudButtonLabels::Original, set), "Original leaves the HUD");
  }
  Require(!TopScreenHudButtonGlyphsActive(TopScreenHudButtonLabels::Controller, NativePromptSet::Touch),
          "touch keeps the letters");
}

void TestDefaults() {
  const auto controls = NativeControlDefaults();
  const auto button = [](const char *label) { return ButtonGlyph::Framed(ButtonGlyphFrame::Button, label); };
  const auto xbox = ResolveTopScreenHudButtonGlyphs(NativePromptSet::Xbox, controls);
  Require(GlyphOf(xbox, TopScreenHudButton::A) == button("A") && GlyphOf(xbox, TopScreenHudButton::B) == button("B") &&
              GlyphOf(xbox, TopScreenHudButton::X) == button("X") &&
              GlyphOf(xbox, TopScreenHudButton::Y) == button("Y") &&
              GlyphOf(xbox, TopScreenHudButton::Zl) == button("LT") &&
              GlyphOf(xbox, TopScreenHudButton::Zr) == button("RT"),
          "Xbox defaults");
  const auto ps = ResolveTopScreenHudButtonGlyphs(NativePromptSet::PlayStation, controls);
  Require(GlyphOf(ps, TopScreenHudButton::A) == ButtonGlyph::Symbol(ButtonGlyphIcon::PsCross) &&
              GlyphOf(ps, TopScreenHudButton::B) == ButtonGlyph::Symbol(ButtonGlyphIcon::PsCircle) &&
              GlyphOf(ps, TopScreenHudButton::X) == ButtonGlyph::Symbol(ButtonGlyphIcon::PsSquare) &&
              GlyphOf(ps, TopScreenHudButton::Y) == ButtonGlyph::Symbol(ButtonGlyphIcon::PsTriangle) &&
              GlyphOf(ps, TopScreenHudButton::Zl) == button("L2") && GlyphOf(ps, TopScreenHudButton::Zr) == button("R2"),
          "PlayStation defaults");
  const auto nintendo = ResolveTopScreenHudButtonGlyphs(NativePromptSet::Nintendo, controls);
  Require(GlyphOf(nintendo, TopScreenHudButton::Zl) == button("ZL") &&
              GlyphOf(nintendo, TopScreenHudButton::A) == button("A"),
          "Nintendo defaults");
  const auto deck = ResolveTopScreenHudButtonGlyphs(NativePromptSet::SteamDeck, controls);
  Require(GlyphOf(deck, TopScreenHudButton::Zr) == button("R2"), "Steam Deck defaults");
  const auto generic = ResolveTopScreenHudButtonGlyphs(NativePromptSet::Generic, controls);
  Require(GlyphOf(generic, TopScreenHudButton::B) == button("B") && GlyphOf(generic, TopScreenHudButton::Zr) == button("R2"),
          "Generic defaults: SDL's letters for the positions");
  // The keyboard and mouse defaults: space (and Z, and the left button) on
  // A, X on B, C on X, V on Y, 2 on ZL, 1 on ZR.
  const auto key = [](const char *label) { return ButtonGlyph::Framed(ButtonGlyphFrame::Key, label); };
  const auto keyboard = ResolveTopScreenHudButtonGlyphs(NativePromptSet::Keyboard, controls);
  Require(GlyphOf(keyboard, TopScreenHudButton::A) == ButtonGlyph::Symbol(ButtonGlyphIcon::KeySpace) &&
              GlyphOf(keyboard, TopScreenHudButton::B) == key("X") &&
              GlyphOf(keyboard, TopScreenHudButton::X) == key("C") &&
              GlyphOf(keyboard, TopScreenHudButton::Y) == key("V") &&
              GlyphOf(keyboard, TopScreenHudButton::Zl) == key("2") &&
              GlyphOf(keyboard, TopScreenHudButton::Zr) == key("1"),
          "keyboard defaults");
  const auto touch = ResolveTopScreenHudButtonGlyphs(NativePromptSet::Touch, controls);
  for (const auto &glyph : touch) Require(!glyph.has_value(), "touch has no glyphs");
}

void TestRebound() {
  auto controls = NativeControlDefaults();
  auto &a = controls.Bindings[static_cast<std::size_t>(NativeControlAction::A)];
  auto &zr = controls.Bindings[static_cast<std::size_t>(NativeControlAction::Zr)];
  auto &x = controls.Bindings[static_cast<std::size_t>(NativeControlAction::X)];
  // A on the right shoulder and the keyboard's E; ZR without a pad button;
  // X only on the mouse's forward button.
  a.Gamepad = NativeGamepadButton::RightShoulder;
  a.KeyboardPrimary = NativeKeyboardKey::E;
  zr.Gamepad = NativeGamepadButton::None;
  x.KeyboardPrimary = x.KeyboardSecondary = NativeKeyboardKey::None;
  x.Mouse = NativeMouseButton::Forward;
  const auto xbox = ResolveTopScreenHudButtonGlyphs(NativePromptSet::Xbox, controls);
  Require(GlyphOf(xbox, TopScreenHudButton::A) == ButtonGlyph::Framed(ButtonGlyphFrame::Button, "RB"),
          "a rebound pad button");
  Require(!GlyphOf(xbox, TopScreenHudButton::Zr).has_value(), "an unbound pad button keeps its label");
  const auto keyboard = ResolveTopScreenHudButtonGlyphs(NativePromptSet::Keyboard, controls);
  Require(GlyphOf(keyboard, TopScreenHudButton::A) == ButtonGlyph::Framed(ButtonGlyphFrame::Key, "E") &&
              GlyphOf(keyboard, TopScreenHudButton::X) == ButtonGlyph::Symbol(ButtonGlyphIcon::MouseForward),
          "a rebound key and a mouse-only button");
  controls.MouseEnabled = false;
  Require(!GlyphOf(ResolveTopScreenHudButtonGlyphs(NativePromptSet::Keyboard, controls), TopScreenHudButton::X),
          "a disabled mouse shows nothing: the label stays");
}

void TestApply() {
  oot3d::ui::UiPrimitive label;
  label.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
  label.role = oot3d::ui::UiPrimitiveRole::TouchControl;
  label.owner_address = 0x005C9940U;
  label.source_quad = 34U;
  label.texture.semantic_name = std::string(kTopScreen30UiAtlasSemantic);
  label.destination = {334.0F, 7.0F, 16.0F, 16.0F};
  label.color = {1.0F, 1.0F, 1.0F, 0.9F};
  label.layer = 13U;
  std::vector<oot3d::ui::UiPrimitive> hud{label};
  TopScreenHudLabelSlot letter;
  letter.Button = TopScreenHudButton::A;
  letter.CenterX = 288.0F;
  letter.CenterY = 13.0F;
  letter.Height = 12.0F;
  letter.Color = {1.0F, 1.0F, 1.0F, 1.0F};
  const auto glyphs = ResolveTopScreenHudButtonGlyphs(NativePromptSet::Keyboard, NativeControlDefaults());
  // 900p: 3.75 framebuffer pixels a canvas unit, an 11-unit glyph is 41 px.
  const auto stats = ApplyTopScreenHudButtonGlyphs(hud, letter, glyphs, 3.75F);
  Require(stats.Labels == 1U && stats.Replaced == 2U && stats.ActionLetter, "label and A replaced");
  std::size_t frames = 0, letters = 0;
  for (const auto &primitive : hud) {
    Require(primitive.texture.semantic_name != kTopScreen30UiAtlasSemantic, "the X label is gone");
    if (primitive.texture.semantic_name == Menu::ButtonGlyphTextureName(64U)) ++frames;
    if (primitive.texture.semantic_name == Menu::kHeadingFontTexture) ++letters;
    Require(primitive.layer == 13U, "glyphs keep the label's layer");
  }
  // C's square keycap (its two caps + 1 letter) then the space bar's
  // symbol keycap (one quad, no letters).
  Require(frames == 3U && letters == 1U, "keycaps from the 64-texel atlas with their labels");
}

void TestNativeLabelFrames() {
  // One record per guest update with draws; a frame's glyphs come from the
  // update before the one that decoded its last draw (the update that built
  // it), never from the latest one.
  TopScreenNativeLabelFrames frames;
  Require(frames.ForFrame(10U) == nullptr, "no record, no glyphs");
  for (std::uint64_t update = 1U; update <= 4U; ++update) {
    TopScreenNativeLabelRecord record;
    record.ThroughDraw = update * 100U;
    TopScreenHudLabelSlot note;
    note.CenterX = static_cast<float>(update);
    record.StaffNotes.push_back(note);
    frames.Record(record);
  }
  const auto *frame = frames.ForFrame(250U);
  Require(frame != nullptr && frame->ThroughDraw == 200U && frame->StaffNotes.front().CenterX == 2.0F,
          "the frame decoded in update 3 shows update 2's letters");
  Require(frames.Decoding(250U) != nullptr && frames.Decoding(250U)->ThroughDraw == 300U,
          "the update that decoded the frame");
  Require(frames.ForFrame(80U) == nullptr, "a frame older than the history shows none");
  Require(frames.ForFrame(900U) == nullptr, "a frame not decoded yet shows none");
  // Draw ids restart (a savestate load): the history restarts with them.
  TopScreenNativeLabelRecord restart;
  restart.ThroughDraw = 5U;
  frames.Record(restart);
  Require(frames.Size() == 1U && frames.ForFrame(5U) == nullptr, "a restarted history");
}

TopScreenHudLabelSlot LetterOfUpdate(int update) {
  TopScreenHudLabelSlot slot;
  slot.Button = TopScreenHudButton::A;
  slot.CenterX = static_cast<float>(update);
  return slot;
}

// Records with these Quest materialization counts, paired in order; the
// letters each record ends with (0 = none), materializations numbered from 1.
std::vector<float> PairedLetters(std::initializer_list<std::uint64_t> counts, TopScreenActionLetterPairing &pairing) {
  TopScreenNativeLabelFrames frames;
  int update = 0;
  std::uint64_t draw = 0U;
  for (const std::uint64_t count : counts) {
    TopScreenActionLetterReads reads;
    reads.Count = count;
    if (count >= 1U) reads.First = LetterOfUpdate(++update);
    for (std::uint64_t extra = 1U; extra < count; ++extra) ++update;
    if (count >= 1U) reads.Last = LetterOfUpdate(update);
    auto pair = pairing.Next(reads);
    if (pair.FillsPrevious) frames.SetLatestActionLetter(std::move(pair.Previous));
    TopScreenNativeLabelRecord record;
    record.ThroughDraw = draw += 100U;
    record.ActionLetter = std::move(pair.Current);
    frames.Record(std::move(record));
  }
  std::vector<float> letters;
  for (std::uint64_t through = 100U; through <= draw; through += 100U) {
    const auto *record = frames.Decoding(through);
    letters.push_back(record != nullptr && record->ActionLetter ? record->ActionLetter->CenterX : 0.0F);
  }
  return letters;
}

void TestActionLetterPairing() {
  using Letters = std::vector<float>;
  // One materialization per record (Kokiri): each record keeps its own.
  {
    TopScreenActionLetterPairing pairing;
    Require(PairedLetters({1U, 1U, 1U}, pairing) == Letters({1.0F, 2.0F, 3.0F}), "one per record, unchanged");
  }
  // Hyrule Field, measured: every third update's record is taken before its
  // materialization, which lands in the next record (0 then 2). Every record
  // gets a letter, in order, each used once.
  {
    TopScreenActionLetterPairing pairing;
    Require(PairedLetters({1U, 0U, 2U, 1U, 0U, 2U, 1U}, pairing) ==
                Letters({1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F, 7.0F}),
            "a late materialization fills the record waiting for it");
  }
  // The other order (2 then 0): the surplus read is carried.
  {
    TopScreenActionLetterPairing pairing;
    Require(PairedLetters({1U, 1U, 2U, 0U, 1U}, pairing) == Letters({1.0F, 2.0F, 3.0F, 4.0F, 5.0F}),
            "an early materialization is carried to its record");
  }
  // The HUD stops materializing: no stale letter, a record waits one record
  // only and a carried letter lives one record only.
  {
    TopScreenActionLetterPairing pairing;
    Require(PairedLetters({1U, 0U, 0U, 1U}, pairing) == Letters({1.0F, 0.0F, 2.0F, 0.0F}),
            "a record waits for the next materialization only");
  }
  {
    TopScreenActionLetterPairing pairing;
    Require(PairedLetters({2U, 0U, 0U}, pairing) == Letters({1.0F, 2.0F, 0.0F}), "carried for one record");
  }
  // A reset (labels off, a restarted history) drops what waits or is carried.
  {
    TopScreenActionLetterPairing pairing;
    (void)PairedLetters({2U}, pairing);
    pairing.Reset();
    Require(PairedLetters({0U}, pairing) == Letters({0.0F}), "a reset drops the carried letter");
  }
  // A materialization that read no letter (the ring hidden) pairs as nothing.
  {
    TopScreenActionLetterPairing pairing;
    TopScreenActionLetterReads hidden;
    hidden.Count = 1U;
    const auto pair = pairing.Next(hidden);
    Require(!pair.FillsPrevious && !pair.Current, "a hidden ring has no letter");
  }
}

} // namespace

int main() {
  TestActive();
  TestDefaults();
  TestRebound();
  TestApply();
  TestNativeLabelFrames();
  TestActionLetterPairing();
  std::cout << "oot3d_hud_button_labels_tests: ok\n";
  return EXIT_SUCCESS;
}
