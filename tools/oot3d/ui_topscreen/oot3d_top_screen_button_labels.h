#pragma once

// TopScreen HUD button labels: the letters that name the console's buttons
// on the HUD (A, B, X, Y, ZL, ZR) give way to the glyph of the input the
// player has bound to that button (TopScreenUiConfig::HudButtonLabels).
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
//     is read after its transform and the letter's box mapped through it.
// A glyph takes the label's place in the HUD list (its layer and order: the
// item icons drawn after a label still cover its glyph), the label's ink
// box, its colour and its alpha, so it scales with the HUD size and fades
// and hides with the HUD. The A glyph is drawn over the native ring and
// covers its letter.

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

enum class TopScreenHudButton : std::uint8_t { A, B, X, Y, Zl, Zr, Count };
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
};

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

// Appends the glyph for a slot; false keeps the label (no binding).
using TopScreenHudGlyphAppender =
    std::function<bool(const TopScreenHudLabelSlot &, std::vector<oot3d::ui::UiPrimitive> &)>;

struct TopScreenHudLabelStats {
  std::uint32_t Labels = 0U;
  std::uint32_t Replaced = 0U;
  bool ActionLetter = false;
};

// Replaces each label in place with its glyph and adds the A glyph at the
// end (over the native ring). The caller calls this only with the setting on
// and a family that has glyphs; without it the HUD list is never touched.
TopScreenHudLabelStats ReplaceTopScreenHudButtonLabels(
    std::vector<oot3d::ui::UiPrimitive> &hud,
    const std::optional<TopScreenHudLabelSlot> &actionLetter,
    const TopScreenHudGlyphAppender &appendGlyph);

} // namespace Oot3dNativeGame
