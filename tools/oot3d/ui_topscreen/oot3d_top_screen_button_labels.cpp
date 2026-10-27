// TopScreen HUD button labels replaced by the glyphs of the bound inputs.
#include "oot3d_top_screen_button_labels.h"

#include "oot3d_top_screen_items_shortcuts.h"
#include "oot3d_top_screen_mod_profile_internal.h"

namespace Oot3dNativeGame {
namespace {

constexpr std::uint32_t kHubOwner = 0x005C9940U;
constexpr std::uint32_t kPauseTouchButtonOwner = 0x005D4A8CU;
// The Quest render buffer's A copies carry this owner (the materialized hook).
constexpr std::uint32_t kQuestActionOwner = 0x0042B884U;
constexpr std::string_view kPauseTopPageSemantic = "oot3d/native/pause_shared/pause_top_page";

// custom_ui01's 16 x 16 label rects: the letters' ink (outline included)
// covers x 2..13, y 3..14 (one letter) or x 0..15, y 4..13 (ZL, ZR), on the
// native atlas and on the Vanilla UI pack alike; both centre on (7.5, 8.5).
// A glyph is a filled shape where a letter is strokes: one texel taller than
// the 11-texel letters reads as their equal.
constexpr float kHubLabelCenterX = 7.5F / 16.0F;
constexpr float kHubLabelCenterY = 8.5F / 16.0F;
constexpr float kHubLabelHeight = 12.0F / 16.0F;
// The native B letter fills its 11 x 11 crop.
constexpr float kNativeLetterHeight = 12.0F / 11.0F;

// The A ring's letter in the 256 x 256 HUD atlas (texels, outline included):
// x 77.5..89.5, y 1..12, on the native art and on the Vanilla UI pack.
constexpr float kActionLetterLeft = 77.5F / 256.0F;
constexpr float kActionLetterRight = 89.5F / 256.0F;
constexpr float kActionLetterTop = 1.0F / 256.0F;
constexpr float kActionLetterBottom = 12.0F / 256.0F;
// A disc that covers the letter's box: the box's diagonal over its longer
// side, about 1.3 for the near-square letter.
constexpr float kActionLetterCover = 1.3F;

TopScreenHudLabelSlot SlotIn(const oot3d::ui::UiPrimitive &primitive, TopScreenHudButton button, float centerX,
                             float centerY, float height) noexcept {
  const auto &destination = primitive.destination;
  TopScreenHudLabelSlot slot;
  slot.Button = button;
  slot.CenterX = destination.x + destination.width * centerX;
  slot.CenterY = destination.y + destination.height * centerY;
  slot.Height = destination.height * height;
  slot.Color = primitive.color;
  if (!primitive.visible) slot.Color.alpha = 0.0F;
  slot.Layer = primitive.layer;
  return slot;
}

} // namespace

std::optional<TopScreenHudLabelSlot>
IdentifyTopScreenHudLabel(const oot3d::ui::UiPrimitive &primitive) noexcept {
  if (primitive.role != oot3d::ui::UiPrimitiveRole::TouchControl || !(primitive.destination.width > 0.0F) ||
      !(primitive.destination.height > 0.0F))
    return std::nullopt;
  if (primitive.owner_address == kHubOwner && primitive.layer == 13U &&
      primitive.texture.semantic_name == kTopScreen30UiAtlasSemantic) {
    TopScreenHudButton button = TopScreenHudButton::Count;
    switch (primitive.source_quad) {
    case 0U: button = TopScreenHudButton::Zr; break;
    case 1U: button = TopScreenHudButton::Zl; break;
    case 34U: button = TopScreenHudButton::X; break;
    case 35U: button = TopScreenHudButton::Y; break;
    case 36U: button = TopScreenHudButton::B; break;
    default: return std::nullopt;
    }
    return SlotIn(primitive, button, kHubLabelCenterX, kHubLabelCenterY, kHubLabelHeight);
  }
  if (primitive.owner_address == kPauseTouchButtonOwner && primitive.source_quad == 34U &&
      primitive.texture.semantic_name == kPauseTopPageSemantic) {
    return SlotIn(primitive, TopScreenHudButton::B, 0.5F, 0.5F, kNativeLetterHeight);
  }
  return std::nullopt;
}

std::optional<TopScreenHudLabelSlot>
TopScreenActionLetterInQuad(const std::array<TopScreenVec2, 4> &positions, const oot3d::ui::UiRect &uv,
                            const oot3d::ui::UiColor &color) noexcept {
  if (uv.width == 0.0F || uv.height == 0.0F) return std::nullopt;
  const float uMinimum = std::min(uv.x, uv.x + uv.width);
  const float uMaximum = std::max(uv.x, uv.x + uv.width);
  const float vMinimum = std::min(uv.y, uv.y + uv.height);
  const float vMaximum = std::max(uv.y, uv.y + uv.height);
  if (kActionLetterLeft < uMinimum || kActionLetterRight > uMaximum || kActionLetterTop < vMinimum ||
      kActionLetterBottom > vMaximum)
    return std::nullopt;
  const float width = positions[1].X - positions[0].X;
  const float height = positions[2].Y - positions[0].Y;
  if (!(std::abs(width) > 0.0F) || !(std::abs(height) > 0.0F)) return std::nullopt;
  const auto x = [&](float u) { return positions[0].X + (u - uv.x) / uv.width * width; };
  const auto y = [&](float v) { return positions[0].Y + (v - uv.y) / uv.height * height; };
  TopScreenHudLabelSlot slot;
  slot.Button = TopScreenHudButton::A;
  slot.CenterX = (x(kActionLetterLeft) + x(kActionLetterRight)) * 0.5F;
  slot.CenterY = (y(kActionLetterTop) + y(kActionLetterBottom)) * 0.5F;
  const float letterWidth = std::abs(x(kActionLetterRight) - x(kActionLetterLeft));
  const float letterHeight = std::abs(y(kActionLetterBottom) - y(kActionLetterTop));
  slot.Height = std::max(letterWidth, letterHeight) * kActionLetterCover;
  slot.Color = color;
  slot.Layer = 13U;
  return slot;
}

bool ReadTopScreenActionLetter(NativeA32Memory &memory, std::uint32_t renderBufferAddress,
                               const TopScreenUiConfig &config, std::optional<TopScreenHudLabelSlot> *letter,
                               std::string *error) {
  if (letter == nullptr) {
    SetError(error, "missing TopScreen A letter output");
    return false;
  }
  letter->reset();
  // Render buffer: count +0, materialized positions +0x10, UVs +0x14,
  // colors +0x18, translations +0x1C (as the A copies read it).
  std::uint32_t count = 0U, positions = 0U, uvs = 0U, colors = 0U, translations = 0U;
  if (!memory.Read32(renderBufferAddress, &count) || !memory.Read32(renderBufferAddress + 0x10U, &positions) ||
      !memory.Read32(renderBufferAddress + 0x14U, &uvs) || !memory.Read32(renderBufferAddress + 0x18U, &colors) ||
      !memory.Read32(renderBufferAddress + 0x1CU, &translations)) {
    SetError(error, "cannot read the native Quest A render buffer");
    return false;
  }
  if (count == 0U || count > 8U || positions == 0U || uvs == 0U || colors == 0U || translations == 0U) return true;
  const auto group = ResolveTopScreenActionGroup(config);
  const float reach = group.Radius * 1.6F;
  for (std::uint32_t index = 0U; index < count; ++index) {
    std::array<TopScreenVec3, 4> quad{};
    std::array<float, 2> translation{};
    std::array<float, 8> quadUvs{};
    std::array<float, 16> color{};
    const auto bytes = [](auto &values) {
      return std::span<std::uint8_t>(reinterpret_cast<std::uint8_t *>(values.data()), sizeof(values));
    };
    if (!memory.ReadBytes(positions + index * 0x30U, bytes(quad)) ||
        !memory.ReadBytes(translations + index * 8U, bytes(translation)) ||
        !memory.ReadBytes(uvs + index * 0x20U, bytes(quadUvs)) ||
        !memory.ReadBytes(colors + index * 0x40U, bytes(color))) {
      SetError(error, "cannot read the native Quest A quads");
      return false;
    }
    std::array<TopScreenVec2, 4> placed{};
    float quadX = 0.0F, quadY = 0.0F;
    for (std::size_t vertex = 0U; vertex < quad.size(); ++vertex) {
      placed[vertex] = {quad[vertex].X + translation[0], quad[vertex].Y + translation[1]};
      quadX += placed[vertex].X * 0.25F;
      quadY += placed[vertex].Y * 0.25F;
    }
    if (std::abs(quadX - group.X) > reach || std::abs(quadY - group.Y) > reach) continue;
    if (auto found = TopScreenActionLetterInQuad(placed, NativePicaQuadUvToHost(quadUvs),
                                                 {color[0], color[1], color[2], color[3]})) {
      *letter = found;
      return true;
    }
  }
  return true;
}

TopScreenHudLabelStats ReplaceTopScreenHudButtonLabels(std::vector<oot3d::ui::UiPrimitive> &hud,
                                                       const std::optional<TopScreenHudLabelSlot> &actionLetter,
                                                       const TopScreenHudGlyphAppender &appendGlyph) {
  TopScreenHudLabelStats stats;
  if (!appendGlyph) return stats;
  std::vector<oot3d::ui::UiPrimitive> output;
  output.reserve(hud.size() + 16U);
  const auto append = [&](const TopScreenHudLabelSlot &slot, std::uint32_t owner, std::uint32_t descriptor,
                          std::uint32_t source) {
    const std::size_t start = output.size();
    if (!appendGlyph(slot, output)) {
      output.resize(start);
      return false;
    }
    // The glyph keeps the label's identity for the HUD's diagnostics.
    for (std::size_t index = start; index < output.size(); ++index) {
      output[index].owner_address = owner;
      output[index].descriptor_address = descriptor;
      output[index].source_quad = source;
    }
    ++stats.Replaced;
    return true;
  };
  for (auto &primitive : hud) {
    const auto slot = IdentifyTopScreenHudLabel(primitive);
    if (!slot) {
      output.push_back(std::move(primitive));
      continue;
    }
    ++stats.Labels;
    if (!append(*slot, primitive.owner_address, primitive.descriptor_address, primitive.source_quad))
      output.push_back(std::move(primitive));
  }
  if (actionLetter) stats.ActionLetter = append(*actionLetter, kQuestActionOwner, 0U, 0U);
  hud = std::move(output);
  return stats;
}

} // namespace Oot3dNativeGame
