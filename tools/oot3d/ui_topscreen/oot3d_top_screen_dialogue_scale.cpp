#include "oot3d_top_screen_dialogue_scale.h"

#include "oot3d_top_screen_config.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace Oot3dNativeGame {
namespace {

constexpr std::uint32_t kElementVisible = 0x6DU;
constexpr std::uint32_t kElementRenderer = 0x64U;
// The element's second renderer (the text's shadow), which 0x002F1280 draws
// before or after the first one while its byte +0x6E is set. The element's
// update (0x002F2D88..0x002F2E24) writes both world matrices alike.
constexpr std::uint32_t kElementSecondRenderer = 0x68U;
constexpr std::uint32_t kElementSecondVisible = 0x6EU;
constexpr std::uint32_t kRendererMatrix = 0x0CU;
// The X and Y rows of the 3 x 4 matrix: m00 m01 m02 tx, m10 m11 m12 ty.
constexpr std::size_t kMatrixWords = 8U;
// The pause pages that draw the item description under them (Gear, Items,
// Map, System): their states are 0 while they are closed.
constexpr std::array<std::uint32_t, 4> kPausePageStates{0x00504484U, 0x0050672CU, 0x00506CE8U, 0x0050A530U};

bool PausePageOpen(NativeA32Memory &memory) {
  for (const auto address : kPausePageStates) {
    std::uint32_t state = 0U;
    // An unreadable page is treated as open: never scale on a guess.
    if (!memory.Read32(address, &state) || state != 0U) return true;
  }
  return false;
}

bool ReadMatrix(NativeA32Memory &memory, std::uint32_t renderer, std::array<std::uint32_t, kMatrixWords> *words) {
  for (std::size_t index = 0; index < words->size(); ++index)
    if (!memory.Read32(renderer + kRendererMatrix + static_cast<std::uint32_t>(index * 4U), &(*words)[index]))
      return false;
  return std::all_of(words->begin(), words->end(),
                     [](std::uint32_t word) { return std::isfinite(std::bit_cast<float>(word)); });
}

// Each drawable element's renderers once (a renderer shared by two elements
// is transformed once): the first, with whether the element is visible, and
// the second while the element draws it.
template <typename Visit> bool ForEachRenderer(NativeA32Memory &memory, std::uint32_t window, Visit &&visit) {
  std::vector<std::uint32_t> seen;
  const auto once = [&](std::uint32_t renderer) {
    if (renderer == 0U || (renderer & 3U) != 0U || std::find(seen.begin(), seen.end(), renderer) != seen.end())
      return false;
    seen.push_back(renderer);
    return true;
  };
  for (std::uint32_t index = 0; index < kTopScreenDialogueElementCount; ++index) {
    std::uint32_t element = 0U, renderer = 0U, second = 0U;
    std::uint8_t visible = 0U, secondVisible = 0U;
    if (!memory.Read32(window + kTopScreenDialogueElements + index * 4U, &element)) return false;
    if (element == 0U) continue;
    if (!memory.Read32(element + kElementRenderer, &renderer) || !memory.Read8(element + kElementVisible, &visible) ||
        !memory.Read8(element + kElementSecondVisible, &secondVisible))
      return false;
    if (once(renderer)) visit(renderer, visible != 0U, true);
    if (secondVisible == 0U) continue;
    if (!memory.Read32(element + kElementSecondRenderer, &second)) return false;
    if (once(second)) visit(second, visible != 0U, false);
  }
  return true;
}

} // namespace

TopScreenDialogueTransform ResolveTopScreenDialogueTransform(NativeA32Memory &memory, std::uint32_t window,
                                                             std::uint8_t percent) {
  TopScreenDialogueTransform result;
  const auto clamped = std::clamp(percent, kTopScreenDialogueScaleMinimum, kTopScreenDialogueScaleMaximum);
  if (clamped >= kTopScreenDialogueScaleMaximum || window == 0U) return result;
  // Without its active byte (+4) the window returns before its elements.
  std::uint8_t active = 0U;
  if (!memory.Read8(window + 4U, &active) || active == 0U || PausePageOpen(memory)) return result;
  float top = std::numeric_limits<float>::max(), bottom = std::numeric_limits<float>::lowest();
  // The pivot is the first renderers' (a shadow is offset from its text).
  const bool read = ForEachRenderer(memory, window, [&](std::uint32_t renderer, bool visible, bool first) {
    std::array<std::uint32_t, kMatrixWords> words{};
    if (!visible || !first || !ReadMatrix(memory, renderer, &words)) return;
    const float y = std::bit_cast<float>(words[7]);
    top = std::min(top, y);
    bottom = std::max(bottom, y);
  });
  if (!read || top > bottom) return result;
  result.Active = true;
  result.Scale = static_cast<float>(clamped) / 100.0F;
  result.PivotY = (top + bottom) * 0.5F;
  return result;
}

bool BeginTopScreenDialogueScale(NativeA32Memory &memory, std::uint32_t window, std::uint8_t percent,
                                 TopScreenDialogueScaleState &state) {
  // A restore the last draw did not reach comes first.
  EndTopScreenDialogueScale(memory, state);
  const auto transform = ResolveTopScreenDialogueTransform(memory, window, percent);
  if (!transform.Active) return false;
  const float s = transform.Scale;
  (void)ForEachRenderer(memory, window, [&](std::uint32_t renderer, bool, bool) {
    TopScreenDialogueScaleState::Saved saved;
    saved.Address = renderer + kRendererMatrix;
    if (!ReadMatrix(memory, renderer, &saved.Words)) return;
    // p' = pivot + s (M p - pivot): the X and Y rows' linear part scales by
    // s, their translation moves toward the pivot. Z is the draw order.
    std::array<std::uint32_t, kMatrixWords> scaled{};
    for (std::size_t index = 0; index < kMatrixWords; ++index) {
      const float value = std::bit_cast<float>(saved.Words[index]);
      const float pivot = index == 3U ? transform.PivotX : transform.PivotY;
      scaled[index] = std::bit_cast<std::uint32_t>((index % 4U) == 3U ? pivot + (value - pivot) * s : value * s);
    }
    for (std::size_t index = 0; index < kMatrixWords; ++index)
      if (!memory.Write32(saved.Address + static_cast<std::uint32_t>(index * 4U), scaled[index])) {
        // Put back what was written; an element is never left half scaled.
        for (std::size_t undo = 0; undo < index; ++undo)
          (void)memory.Write32(saved.Address + static_cast<std::uint32_t>(undo * 4U), saved.Words[undo]);
        return;
      }
    state.Restore.push_back(saved);
  });
  return !state.Restore.empty();
}

void EndTopScreenDialogueScale(NativeA32Memory &memory, TopScreenDialogueScaleState &state) {
  // Reverse order: a renderer reached twice ends with its first saved words.
  for (auto saved = state.Restore.rbegin(); saved != state.Restore.rend(); ++saved)
    for (std::size_t index = 0; index < saved->Words.size(); ++index)
      (void)memory.Write32(saved->Address + static_cast<std::uint32_t>(index * 4U), saved->Words[index]);
  state.Restore.clear();
}

} // namespace Oot3dNativeGame
