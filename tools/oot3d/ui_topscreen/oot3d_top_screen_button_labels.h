#pragma once

// TopScreen button labels: the letters that name the console's buttons on
// the HUD (A, B, X, Y, ZL, ZR), on the ocarina's notes (L, R, X, Y, A) and in
// the pause pages' hints (L, R, SELECT, the A / B / X / Y discs) give way to
// the glyph of the input the player has bound to that button
// (TopScreenUiConfig::HudButtonLabels).
//
// The labels are identified by their native owners, not by where they are
// drawn, so a UI texture pack that redraws their art changes nothing here:
//   - X, Y and the horseback B: TopScreen 3.0's hub labels (owner 0x005C9940,
//     touch quads 34, 35 and 36 on layer 13, custom_ui01 rects 10, 11, 9);
//   - ZR and ZL: its touch labels (owner 0x005C9940, quads 0 and 1 on layer
//     13, custom_ui01 rects 15 and 14);
//   - B: the native PauseTouchButton's B letter (owner 0x005D4A8C, copy of
//     source quad 34, pause_top_page);
//   - A: the letter baked into the native A ring (HUD atlas texels 78..89 x
//     1..11 of 256), drawn natively by the Quest render buffer; the ring quad
//     is read after its transform and the letter's box mapped through it;
//   - the pause pages' L / R tab hints: SingleScreen 3.0 0x005D4A0C's glyph
//     quads 2 and 3 (custom_ui01 rects 20 "<-L" and 21 "R->"; the letter's
//     ink, not the arrow);
//   - the item hint's SELECT word: 0x005E0E6C quad 0 (custom_ui01 rect
//     (277, 222, 64, 16)); its "?" stays;
//   - the legends' A / B / X / Y / ZL discs (View Model, its controls, the
//     D-pad picker, the shortcut hold): custom_ui01's button rects at
//     (46 | 81 | 116 | 151 | 186, 89, 32, 32), the 0x0060A2D4 / 0x0060A728
//     records, whichever owner draws them;
//   - the ocarina song box's notes: 0x005D2078 quads 6..13, each carrying its
//     native note type as the address of its entry in the note atlas table
//     (0x0050A1E0 + 4 x type);
//   - the native message staff's notes (the notes played, a song taught or
//     played back): the message context's draw elements (0x005C1878 + 0x568)
//     whose model is one of the five note models (0x96 + note type), read
//     after their transform (ReadTopScreenOcarinaStaffNotes).
// Note types are the N64 ones (0 A, 1 C-down, 2 C-right, 3 C-left, 4 C-up),
// played on the 3DS with L, R, Y, X and A.
// A glyph takes the label's place in its list (its layer and order: the
// item icons drawn after a label still cover its glyph), the label's ink
// box, its colour and its alpha, so it scales with the HUD size and fades
// and hides with the HUD. The A glyph and the staff's notes are drawn over
// the native art they cover.

#include "oot3d_top_screen_mod_profile.h"
#include "oot3d_ui/ui_primitives.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Oot3dNativeGame {

class NativeA32Memory;

enum class TopScreenHudButton : std::uint8_t { A, B, X, Y, Zl, Zr, L, R, Select, Count };
inline constexpr std::size_t kTopScreenHudButtonCount = static_cast<std::size_t>(TopScreenHudButton::Count);

// Where a label's glyph goes, in HUD canvas units.
struct TopScreenHudLabelSlot {
  TopScreenHudButton Button = TopScreenHudButton::Count;
  float CenterX = 0.0F;
  float CenterY = 0.0F;
  // The letter's height (the glyph's height).
  float Height = 0.0F;
  oot3d::ui::UiColor Color{};
  std::uint32_t Layer = 13U;
  // The glyph ends at CenterX instead of being centred on it (a word's
  // glyph keeps its place beside what follows the word).
  bool AlignRight = false;
  // The label's art is more than its letter (the tab hints' key and arrow):
  // it stays, and the glyph is drawn right after it, over the letter.
  bool KeepLabel = false;
  // A native letter's owner and descriptor (the glyph's identity, for the
  // diagnostics), for the glyphs drawn over native art.
  std::uint32_t Owner = 0U;
  std::uint32_t Descriptor = 0U;
};

// The A ring letters read by the Quest materializations since the last label
// record: how many ran, the first one's letter and the latest one's.
struct TopScreenActionLetterReads {
  std::uint64_t Count = 0U;
  std::optional<TopScreenHudLabelSlot> First;
  std::optional<TopScreenHudLabelSlot> Last;
};

// The 3DS button that plays a native ocarina note type (0..4), if any.
[[nodiscard]] std::optional<TopScreenHudButton> TopScreenOcarinaNoteButton(std::uint32_t noteType) noexcept;

// The song box's note quads carry their native note type as the address of
// its entry in the note atlas table (oot3d_top_screen_ocarina.cpp).
inline constexpr std::uint32_t kTopScreenOcarinaNoteAtlasTable = 0x0050A1E0U;

// The label a HUD primitive draws, if it is one of the button labels.
[[nodiscard]] std::optional<TopScreenHudLabelSlot>
IdentifyTopScreenHudLabel(const oot3d::ui::UiPrimitive &primitive) noexcept;

// The A ring's letter in a Quest render buffer quad (final positions and
// host UVs, as the A copies use them); nothing when the quad's UVs do not
// hold the letter. Exposed for tests.
[[nodiscard]] std::optional<TopScreenHudLabelSlot>
TopScreenActionLetterInQuad(const std::array<TopScreenVec2, 4> &positions,
                            const oot3d::ui::UiRect &uv, const oot3d::ui::UiColor &color) noexcept;

// Reads the A ring from the Quest render buffer (after its transform, at the
// materialized hook) without changing it.
bool ReadTopScreenActionLetter(NativeA32Memory &memory, std::uint32_t renderBufferAddress,
                               const TopScreenUiConfig &config,
                               std::optional<TopScreenHudLabelSlot> *letter,
                               std::string *error = nullptr);

// The native message staff's notes as drawn this frame (read only), in draw
// order: nothing while the message window does not draw them. Exposed for
// tests with any message context address.
inline constexpr std::uint32_t kTopScreenMessageContext = 0x005C1878U;
bool ReadTopScreenOcarinaStaffNotes(NativeA32Memory &memory, std::vector<TopScreenHudLabelSlot> *notes,
                                    std::string *error = nullptr,
                                    std::uint32_t messageContext = kTopScreenMessageContext);

// The native Items page's slot labels as drawn (read only): the label model
// of its state (0x005066F8 + 0x30), quads whose UVs are the X / Y / I / II
// letters of the pause parts atlas, at their drawn positions on the upper
// screen, with the page at rest (state 2). I and II are SingleScreen's ZR and
// ZL item slots (ResolveTopScreenItemQuery: ZR is Item I, ZL Item II; the
// Vanilla UI pack draws "ZR" / "ZL" there), so they take ZR's and ZL's glyphs.
bool ReadTopScreenItemsPageButtonLabels(NativeA32Memory &memory, std::vector<TopScreenHudLabelSlot> *labels,
                                        std::string *error = nullptr);

// The Items label model's state address (0x005066F8 + 0x30).
inline constexpr std::uint32_t kTopScreenItemsLabelModel = 0x00506728U;
// The button a label quad names, by its UV crop of the pause parts atlas (X,
// Y, ZR for I, ZL for II); nothing for any other quad.
[[nodiscard]] std::optional<TopScreenHudButton> TopScreenItemsLabelButton(const std::array<float, 8> &picaUvs) noexcept;
// The label model's streams: source UVs (0x20 a quad) and colours (0x40 a
// quad, RGBA floats per vertex, read when its draw is decoded) and the
// positions drawn this frame (0x30 a quad). Quads 0 when there is no model.
struct TopScreenItemsLabelModel {
  std::uint32_t Uvs = 0U;
  std::uint32_t Colors = 0U;
  std::uint32_t Positions = 0U;
  std::uint32_t Quads = 0U;
};
bool ResolveTopScreenItemsLabelModel(NativeA32Memory &memory, std::uint32_t model, TopScreenItemsLabelModel *streams,
                                     std::string *error = nullptr);

// Appends the glyph for a slot; false keeps the label (no binding).
using TopScreenHudGlyphAppender =
    std::function<bool(const TopScreenHudLabelSlot &, std::vector<oot3d::ui::UiPrimitive> &)>;

struct TopScreenHudLabelStats {
  std::uint32_t Labels = 0U;
  std::uint32_t Replaced = 0U;
  bool ActionLetter = false;
  std::uint32_t Overlays = 0U;
};

// Replaces each label in place with its glyph, then adds the glyphs of the
// native letters drawn under the list (the A ring's letter, the staff's
// notes) at its end. The caller calls this only with the setting on and a
// family that has glyphs; without it the list is never touched.
TopScreenHudLabelStats ReplaceTopScreenHudButtonLabels(
    std::vector<oot3d::ui::UiPrimitive> &hud,
    const std::optional<TopScreenHudLabelSlot> &actionLetter,
    const TopScreenHudGlyphAppender &appendGlyph,
    const std::vector<TopScreenHudLabelSlot> &nativeOverlays = {});

} // namespace Oot3dNativeGame
