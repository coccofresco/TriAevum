// TopScreen button labels (HUD, ocarina notes, pause hints) replaced by the
// glyphs of the bound inputs.
#include "oot3d_top_screen_button_labels.h"

#include "oot3d_top_screen_items_shortcuts.h"
#include "oot3d_top_screen_mod_profile_internal.h"

#include <algorithm>
#include <cmath>
#include <span>

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

// SingleScreen 3.0's L / R tab hints (0x005D4A0C) and its item hint
// (0x005E0E6C), custom_ui01 (512 x 256).
constexpr std::uint32_t kTabHintOwner = 0x005D4A0CU;
constexpr std::uint32_t kItemsHintOwner = 0x005E0E6CU;
constexpr float kUiAtlasWidth = 512.0F;
constexpr float kUiAtlasHeight = 256.0F;
// Rects 20 ("<-L", 462,108) and 21 ("R->", 240,117), 37 x 28: the letter's
// ink is x 20..28.5 (L) or 9..17.5 (R), y 7..19.7 of the crop, on the native
// atlas and on the HD atlas alike (the arrow is not the letter). The hint's
// key and arrow stay, so the glyph covers the letter: as tall as the ink
// box's diagonal (a disc covers the L's corners).
constexpr float kTabHintWidth = 37.0F;
constexpr float kTabHintHeight = 28.0F;
constexpr float kTabLetterCenterY = 13.35F;
constexpr float kTabLetterLeftCenterX = 24.25F;
constexpr float kTabLetterRightCenterX = 13.25F;
constexpr float kTabLetterHeight = 15.5F;
// The SELECT word (277,222,64,16): ink x 2..63, y 2..15.75; its glyph ends
// where the word does, beside the "?".
constexpr float kSelectWordX = 277.0F;
constexpr float kSelectWordY = 222.0F;
constexpr float kSelectWordWidth = 64.0F;
constexpr float kSelectWordHeight = 16.0F;
constexpr float kSelectInkRight = 63.0F;
constexpr float kSelectInkCenterY = 9.0F;
constexpr float kSelectInkHeight = 14.0F;
// The button discs (records 0x0060A2D4, 0x0060A728): 32 x 32 at y 89, the
// disc's ink x 3..29, y 3..29 (diameter 26).
struct ButtonDisc {
  float X;
  TopScreenHudButton Button;
};
constexpr std::array<ButtonDisc, 5> kButtonDiscs{{{46.0F, TopScreenHudButton::A},
                                                   {81.0F, TopScreenHudButton::B},
                                                   {116.0F, TopScreenHudButton::X},
                                                   {151.0F, TopScreenHudButton::Y},
                                                   {186.0F, TopScreenHudButton::Zl}}};
constexpr float kButtonDiscY = 89.0F;
constexpr float kButtonDiscSize = 32.0F;
constexpr float kButtonDiscInk = 26.0F / 32.0F;

// The ocarina song box (0x005D2078): notes are quads 6..13, 16 x 16; the
// note's disc covers all but its outermost texel.
constexpr std::uint32_t kOcarinaSongBoxOwner = 0x005D2078U;
constexpr float kOcarinaNoteInk = 15.0F / 16.0F;

// The message context's draw elements (0x0042CBCC draws them through
// 0x002F1280): 256 pointers at +0x568; the model table at +0x168.
constexpr std::uint32_t kMessageDrawOwner = 0x0042CBCCU;
constexpr std::uint32_t kMessageElements = 0x568U;
constexpr std::uint32_t kMessageElementCount = 0x100U;
constexpr std::uint32_t kMessageModels = 0x168U;
constexpr std::uint32_t kStaffNoteModel = 0x96U;
// A note's quad is 16 x 16 model units (-8..8); a glyph about as tall as
// its disc covers it, the native shadow stays beside it.
constexpr float kStaffNoteSize = 16.0F;
constexpr float kStaffNoteCover = 15.5F / 16.0F;

// The native Items page (state 0x005066F8): its controller state at +0x34,
// the model of the slot labels I, X, Y, II at +0x30. Those are drawn from the
// 128 x 128 pause parts atlas; X at texels (67, 51), Y at (83, 51), 11 x 11.
constexpr std::uint32_t kPauseItemsState = 0x005066F8U;
constexpr std::uint32_t kPauseItemsLabelModel = 0x30U;
constexpr std::uint32_t kPauseItemsController = 0x34U;
constexpr std::uint32_t kPauseItemsAtRest = 2U;
constexpr float kPausePartsAtlasSize = 128.0F;
constexpr float kItemsLabelLetterY = 51.0F;
constexpr float kItemsLabelLetterSize = 11.0F;
constexpr float kItemsLabelXLetterX = 67.0F;
constexpr float kItemsLabelYLetterX = 83.0F;
// The X fills its 11 x 11 crop: a disc covers its corners at 1.35 times.
constexpr float kItemsLabelCover = 1.35F;

// A texel rectangle of custom_ui01 for a primitive's UV (host, top-left).
bool UvAt(const oot3d::ui::UiRect &uv, float x, float y, float width, float height) noexcept {
  constexpr float kTolerance = 0.25F;
  return std::abs(uv.x * kUiAtlasWidth - x) < kTolerance && std::abs(uv.y * kUiAtlasHeight - y) < kTolerance &&
         std::abs(uv.width * kUiAtlasWidth - width) < kTolerance &&
         std::abs(uv.height * kUiAtlasHeight - height) < kTolerance;
}

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

std::optional<TopScreenHudButton> TopScreenOcarinaNoteButton(std::uint32_t noteType) noexcept {
  // 0x004D541C's sequences: Zelda's Lullaby 3 4 2 (X A Y), the Minuet
  // 0 4 3 2 (L A X Y), the Sun's Song 2 1 4 (Y R A).
  switch (noteType) {
  case 0U: return TopScreenHudButton::L;
  case 1U: return TopScreenHudButton::R;
  case 2U: return TopScreenHudButton::Y;
  case 3U: return TopScreenHudButton::X;
  case 4U: return TopScreenHudButton::A;
  default: return std::nullopt;
  }
}

std::optional<TopScreenHudLabelSlot>
IdentifyTopScreenHudLabel(const oot3d::ui::UiPrimitive &primitive) noexcept {
  if (!(primitive.destination.width > 0.0F) || !(primitive.destination.height > 0.0F)) return std::nullopt;
  const bool uiAtlas = primitive.texture.semantic_name == kTopScreen30UiAtlasSemantic;
  // The legends' button discs, whoever draws them (an upright crop).
  if (uiAtlas && !primitive.uv_quarter_turn && !primitive.corners) {
    for (const auto &disc : kButtonDiscs) {
      if (UvAt(primitive.uv, disc.X, kButtonDiscY, kButtonDiscSize, kButtonDiscSize))
        return SlotIn(primitive, disc.Button, 0.5F, 0.5F, kButtonDiscInk);
    }
  }
  if (uiAtlas && primitive.owner_address == kTabHintOwner &&
      primitive.role == oot3d::ui::UiPrimitiveRole::TouchControl &&
      (primitive.source_quad == 2U || primitive.source_quad == 3U)) {
    const bool left = primitive.source_quad == 2U;
    auto slot = SlotIn(primitive, left ? TopScreenHudButton::L : TopScreenHudButton::R,
                       (left ? kTabLetterLeftCenterX : kTabLetterRightCenterX) / kTabHintWidth,
                       kTabLetterCenterY / kTabHintHeight, kTabLetterHeight / kTabHintHeight);
    slot.KeepLabel = true;
    return slot;
  }
  if (uiAtlas && primitive.owner_address == kItemsHintOwner && primitive.source_quad == 0U &&
      UvAt(primitive.uv, kSelectWordX, kSelectWordY, kSelectWordWidth, kSelectWordHeight)) {
    auto slot = SlotIn(primitive, TopScreenHudButton::Select, kSelectInkRight / kSelectWordWidth,
                       kSelectInkCenterY / kSelectWordHeight, kSelectInkHeight / kSelectWordHeight);
    slot.AlignRight = true;
    return slot;
  }
  if (primitive.owner_address == kOcarinaSongBoxOwner && primitive.source_quad >= 6U &&
      primitive.source_quad < 14U && primitive.descriptor_address >= kTopScreenOcarinaNoteAtlasTable &&
      primitive.descriptor_address < kTopScreenOcarinaNoteAtlasTable + 5U * 4U &&
      (primitive.descriptor_address - kTopScreenOcarinaNoteAtlasTable) % 4U == 0U) {
    const auto button =
        TopScreenOcarinaNoteButton((primitive.descriptor_address - kTopScreenOcarinaNoteAtlasTable) / 4U);
    if (button) return SlotIn(primitive, *button, 0.5F, 0.5F, kOcarinaNoteInk);
  }
  if (primitive.role != oot3d::ui::UiPrimitiveRole::TouchControl) return std::nullopt;
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

bool ReadTopScreenOcarinaStaffNotes(NativeA32Memory &memory, std::vector<TopScreenHudLabelSlot> *notes,
                                    std::string *error, std::uint32_t messageContext) {
  if (notes == nullptr) {
    SetError(error, "missing TopScreen staff note output");
    return false;
  }
  notes->clear();
  // 0x0042CBCC's gates: the window is up (+0x0C), its state (+0xF38) is not
  // 0, 1, 14 or 15, it is shown (+0x0D, +0x0E) and its elements drawn (+0x48).
  std::uint8_t open = 0U, state = 0U, shown = 0U, visible = 0U, elements = 0U;
  if (!memory.Read8(messageContext + 0x0CU, &open) || !memory.Read8(messageContext + 0xF38U, &state) ||
      !memory.Read8(messageContext + 0x0DU, &shown) || !memory.Read8(messageContext + 0x0EU, &visible) ||
      !memory.Read8(messageContext + 0x48U, &elements)) {
    SetError(error, "cannot read the native message context");
    return false;
  }
  if (open == 0U || state == 0U || state == 1U || state == 0x0EU || state == 0x0FU || shown == 0U ||
      visible == 0U || elements == 0U)
    return true;
  for (std::uint32_t index = 0U; index < kMessageElementCount; ++index) {
    std::uint32_t element = 0U;
    if (!memory.Read32(messageContext + kMessageElements + index * 4U, &element)) {
      SetError(error, "cannot read the native message elements");
      return false;
    }
    if (element == 0U) continue;
    // 0x002F1280's gates: a model table and a renderer, the model's own
    // visibility (+0x390), the element's (+0x6D) and a drawable state.
    std::uint32_t models = 0U, renderer = 0U, model = 0U, drawable = 0U, modelState = 0U;
    std::uint8_t elementVisible = 0U, modelVisible = 0U;
    if (!memory.Read32(element + 0x04U, &models) || !memory.Read32(element + 0x64U, &renderer) ||
        !memory.Read32(element + 0x78U, &model) || !memory.Read8(element + 0x6DU, &elementVisible) ||
        !memory.Read32(element + 0xB0U, &drawable)) {
      SetError(error, "cannot read a native message element");
      return false;
    }
    // A note is an element of this window's model table whose model is one
    // of the five note models.
    if (models != messageContext + kMessageModels || renderer == 0U || model < kStaffNoteModel ||
        model > kStaffNoteModel + 4U || elementVisible == 0U || drawable <= 2U)
      continue;
    if (!memory.Read32(models + model * 4U, &modelState) || modelState == 0U ||
        !memory.Read8(modelState + 0x390U, &modelVisible)) {
      SetError(error, "cannot read a native message note model");
      return false;
    }
    if (modelVisible == 0U) continue;
    // The renderer's world matrix (3 x 4 at +0x0C, canvas units, Y down)
    // and its colour (RGBA floats at +0xF0), as the draw uses them.
    std::array<float, 12> matrix{};
    std::array<float, 4> color{};
    const auto bytes = [](auto &values) {
      return std::span<std::uint8_t>(reinterpret_cast<std::uint8_t *>(values.data()), sizeof(values));
    };
    if (!memory.ReadBytes(renderer + 0x0CU, bytes(matrix)) || !memory.ReadBytes(renderer + 0xF0U, bytes(color))) {
      SetError(error, "cannot read a native message note renderer");
      return false;
    }
    if (!std::all_of(matrix.begin(), matrix.end(), [](float value) { return std::isfinite(value); }) ||
        !std::all_of(color.begin(), color.end(), [](float value) { return std::isfinite(value); }))
      continue;
    const auto button = TopScreenOcarinaNoteButton(model - kStaffNoteModel);
    const float scale = std::max(std::hypot(matrix[0], matrix[4]), std::hypot(matrix[1], matrix[5]));
    if (!button || !(scale > 0.0F)) continue;
    TopScreenHudLabelSlot slot;
    slot.Button = *button;
    slot.CenterX = matrix[3];
    slot.CenterY = matrix[7];
    slot.Height = kStaffNoteSize * kStaffNoteCover * scale;
    slot.Color = {std::clamp(color[0], 0.0F, 1.0F), std::clamp(color[1], 0.0F, 1.0F),
                  std::clamp(color[2], 0.0F, 1.0F), std::clamp(color[3], 0.0F, 1.0F)};
    slot.Layer = 24U;
    slot.Owner = kMessageDrawOwner;
    slot.Descriptor = messageContext;
    notes->push_back(slot);
  }
  return true;
}

bool ReadTopScreenItemsPageButtonLabels(NativeA32Memory &memory, std::vector<TopScreenHudLabelSlot> *labels,
                                        std::string *error) {
  if (labels == nullptr) {
    SetError(error, "missing TopScreen Items label output");
    return false;
  }
  labels->clear();
  std::uint32_t controller = 0U, model = 0U, suppressed = 0U;
  if (!memory.Read32(kPauseItemsState + kPauseItemsController, &controller) ||
      !memory.Read32(kPauseItemsState + kPauseItemsLabelModel, &model) ||
      !memory.Read32(kPauseItemsState + 0x60U, &suppressed)) {
    SetError(error, "cannot read the native Items page");
    return false;
  }
  // 0x00434C80 draws the labels whenever the page is up, but while it opens
  // or closes (states 13, 1, ...) the page slides under a transform these
  // positions do not carry: the labels are covered only with the page at
  // rest (state 2); a transition shows the native letters.
  // +0x60 set: 0x00434C80 draws none of the page's models.
  if (controller != kPauseItemsAtRest || suppressed != 0U || model == 0U || (model & 3U) != 0U) return true;
  // The model's stream owner (model +4, +4): its source streams (+0:
  // positions, UVs, colours), its vertex count (+0x10) and the positions it
  // drew this frame (lane +0x124 of +0x1A0), as the Items cursor reads them.
  std::uint32_t first = 0U, owner = 0U, sources = 0U, uvs = 0U, vertices = 0U, lane = 0U, positions = 0U;
  if (!memory.Read32(model + 4U, &first) || first == 0U || (first & 3U) != 0U) return true;
  if (!memory.Read32(first + 4U, &owner) || owner == 0U || (owner & 3U) != 0U) return true;
  if (!memory.Read32(owner, &sources) || !memory.Read32(owner + 0x10U, &vertices) ||
      !memory.Read32(owner + 0x124U, &lane)) {
    SetError(error, "cannot read the native Items label model");
    return false;
  }
  if (sources == 0U || vertices == 0U || vertices > 64U || vertices % 4U != 0U || lane >= 4U) return true;
  if (!memory.Read32(sources + 4U, &uvs) || !memory.Read32(owner + 0x1A0U + lane * 4U, &positions)) {
    SetError(error, "cannot read the native Items label streams");
    return false;
  }
  if (uvs == 0U || positions == 0U) return true;
  const auto bytes = [](auto &values) {
    return std::span<std::uint8_t>(reinterpret_cast<std::uint8_t *>(values.data()), sizeof(values));
  };
  for (std::uint32_t quad = 0U; quad < vertices / 4U; ++quad) {
    std::array<float, 8> quadUvs{};
    std::array<float, 12> quadPositions{};
    if (!memory.ReadBytes(uvs + quad * 0x20U, bytes(quadUvs)) ||
        !memory.ReadBytes(positions + quad * 0x30U, bytes(quadPositions))) {
      SetError(error, "cannot read a native Items label quad");
      return false;
    }
    const auto uv = NativePicaQuadUvToHost(quadUvs);
    const auto at = [&](float x) {
      return std::abs(std::min(uv.x, uv.x + uv.width) * kPausePartsAtlasSize - x) < 0.25F &&
             std::abs(std::min(uv.y, uv.y + uv.height) * kPausePartsAtlasSize - kItemsLabelLetterY) < 0.25F &&
             std::abs(std::abs(uv.width) * kPausePartsAtlasSize - kItemsLabelLetterSize) < 0.25F &&
             std::abs(std::abs(uv.height) * kPausePartsAtlasSize - kItemsLabelLetterSize) < 0.25F;
    };
    TopScreenHudButton button = TopScreenHudButton::Count;
    if (at(kItemsLabelXLetterX)) button = TopScreenHudButton::X;
    else if (at(kItemsLabelYLetterX)) button = TopScreenHudButton::Y;
    else continue;
    if (!std::all_of(quadPositions.begin(), quadPositions.end(), [](float value) { return std::isfinite(value); }))
      continue;
    const float minimumX = std::min({quadPositions[0], quadPositions[3], quadPositions[6], quadPositions[9]});
    const float maximumX = std::max({quadPositions[0], quadPositions[3], quadPositions[6], quadPositions[9]});
    const float minimumY = std::min({quadPositions[1], quadPositions[4], quadPositions[7], quadPositions[10]});
    const float maximumY = std::max({quadPositions[1], quadPositions[4], quadPositions[7], quadPositions[10]});
    if (!(maximumX > minimumX) || !(maximumY > minimumY)) continue;
    // 0x005E3264's projections on the upper screen: native X + 84 below
    // X 256, else + 80 (as the Items cursor and its hint).
    const float offsetX = quadPositions[0] < 256.0F ? 84.0F : 80.0F;
    TopScreenHudLabelSlot slot;
    slot.Button = button;
    slot.CenterX = (minimumX + maximumX) * 0.5F + offsetX;
    slot.CenterY = (minimumY + maximumY) * 0.5F;
    slot.Height = std::max(maximumX - minimumX, maximumY - minimumY) * kItemsLabelCover;
    slot.Color = {1.0F, 1.0F, 1.0F, 1.0F};
    slot.Layer = 14U;
    slot.Owner = kPauseItemsState;
    slot.Descriptor = model;
    labels->push_back(slot);
  }
  return true;
}

TopScreenHudLabelStats ReplaceTopScreenHudButtonLabels(std::vector<oot3d::ui::UiPrimitive> &hud,
                                                       const std::optional<TopScreenHudLabelSlot> &actionLetter,
                                                       const TopScreenHudGlyphAppender &appendGlyph,
                                                       const std::vector<TopScreenHudLabelSlot> &nativeOverlays) {
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
    const auto owner = primitive.owner_address;
    const auto descriptor = primitive.descriptor_address;
    const auto source = primitive.source_quad;
    if (slot->KeepLabel) {
      output.push_back(std::move(primitive));
      (void)append(*slot, owner, descriptor, source);
    } else if (!append(*slot, owner, descriptor, source)) {
      output.push_back(std::move(primitive));
    }
  }
  if (actionLetter) stats.ActionLetter = append(*actionLetter, kQuestActionOwner, 0U, 0U);
  for (std::size_t index = 0U; index < nativeOverlays.size(); ++index) {
    const auto &overlay = nativeOverlays[index];
    if (append(overlay, overlay.Owner, overlay.Descriptor, static_cast<std::uint32_t>(index)))
      ++stats.Overlays;
  }
  hud = std::move(output);
  return stats;
}

} // namespace Oot3dNativeGame
