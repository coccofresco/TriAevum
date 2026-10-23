#include "oot3d_top_screen_items_hint_consumer.h"

#include "oot3d_native_a32_memory.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <span>

namespace Oot3dNativeGame {
namespace {

constexpr std::uint32_t kPauseItemsState = 0x005066F8U;
constexpr std::uint32_t kPauseItemsControllerStateOffset = 0x34U;
constexpr std::uint32_t kPauseItemsSelectedModelOffset = 0xB0U;
constexpr std::uint32_t kGearCursorOwner = 0x00504480U;
constexpr float kMaximumHintVertexX = 390.0F;

void SetError(std::string *error, const char *message) {
  if (error != nullptr) {
    *error = message;
  }
}

bool ResolveSelectedModelPositions(const NativeA32Memory &memory,
                                   std::uint32_t model,
                                   std::uint32_t *positions,
                                   std::string *error) {
  std::uint32_t firstOwner = 0U;
  std::uint32_t streamOwner = 0U;
  std::uint32_t activeStream = 0U;
  if (!memory.ReadFast(model + 4U, &firstOwner)) {
    SetError(error, "cannot read the native Items selected-model stream owner");
    return false;
  }
  if (firstOwner == 0U || (firstOwner & 3U) != 0U) {
    return true;
  }
  if (!memory.ReadFast(firstOwner + 4U, &streamOwner)) {
    SetError(error, "cannot read the native Items selected-model stream owner");
    return false;
  }
  if (streamOwner == 0U || (streamOwner & 3U) != 0U) {
    return true;
  }
  if (!memory.ReadFast(streamOwner + 0x124U, &activeStream)) {
    SetError(error, "cannot read the native Items selected-model stream index");
    return false;
  }
  if (activeStream >= 4U) {
    return true;
  }
  std::uint32_t resolved = 0U;
  if (!memory.ReadFast(streamOwner + 0x1A0U + activeStream * 4U, &resolved)) {
    SetError(error,
             "cannot read the native Items selected-model position lane");
    return false;
  }
  if ((resolved & 3U) == 0U) {
    *positions = resolved;
  }
  return true;
}

struct CursorQuadBounds {
  bool Found = false;
  float FirstX = 0.0F;
  float MinimumX = 0.0F;
  float MaximumX = 0.0F;
  float MinimumY = 0.0F;
  float MaximumY = 0.0F;
};

// 3.0 0x005CB298 via 0x005CAE40(owner, 4): scans four quads, skipping a
// whole quad when its first vertex has the native hidden sentinel X >= 390.
bool ScanCursorQuads(const NativeA32Memory &memory, std::uint32_t owner,
                     CursorQuadBounds *bounds, std::string *error) {
  *bounds = {};
  std::uint32_t positions = 0U;
  if (!ResolveSelectedModelPositions(memory, owner, &positions, error)) {
    return false;
  }
  if (positions == 0U) {
    return true;
  }
  std::array<float, 4U * 4U * 3U> vertices{};
  if (!memory.ReadBytes(positions,
                        std::span<std::uint8_t>(
                            reinterpret_cast<std::uint8_t *>(vertices.data()),
                            sizeof(vertices)))) {
    SetError(error, "cannot read the native Items selected-model vertices");
    return false;
  }

  bool found = false;
  float minimumX = 0.0F;
  float maximumX = 0.0F;
  float minimumY = 0.0F;
  float maximumY = 0.0F;
  for (std::size_t vertex = 0U; vertex < 16U; ++vertex) {
    const float x = vertices[vertex * 3U];
    const float y = vertices[vertex * 3U + 1U];
    if (!std::isfinite(x) || !std::isfinite(y) ||
        !(vertices[(vertex / 4U) * 12U] < kMaximumHintVertexX)) {
      continue;
    }
    if (!found) {
      minimumX = maximumX = x;
      minimumY = maximumY = y;
      found = true;
      continue;
    }
    minimumX = std::min(minimumX, x);
    maximumX = std::max(maximumX, x);
    minimumY = std::min(minimumY, y);
    maximumY = std::max(maximumY, y);
  }
  if (!found || maximumX <= minimumX) {
    return true;
  }
  *bounds = {true, vertices[0], minimumX, maximumX, minimumY, maximumY};
  return true;
}

} // namespace

bool ReadTopScreenItemsHintCursorBounds(const NativeA32Memory &memory,
                                        TopScreenItemsHintCursorBounds *bounds,
                                        std::string *error) {
  if (bounds == nullptr) {
    SetError(error, "TopScreen Items hint bounds output is null");
    return false;
  }
  *bounds = {};
  if (error != nullptr) {
    error->clear();
  }

  std::uint32_t controllerState = 0U;
  std::uint32_t selectedModel = 0U;
  if (!memory.ReadFast(kPauseItemsState + kPauseItemsControllerStateOffset,
                       &controllerState) ||
      !memory.ReadFast(kPauseItemsState + kPauseItemsSelectedModelOffset,
                       &selectedModel)) {
    SetError(error, "cannot read the native Items hint owner");
    return false;
  }
  if (controllerState == 0U || selectedModel == 0U ||
      (selectedModel & 3U) != 0U) {
    return true;
  }

  CursorQuadBounds quads;
  if (!ScanCursorQuads(memory, selectedModel, &quads, error)) {
    return false;
  }
  if (!quads.Found) {
    return true;
  }

  // 0x005E3264 draws this group at projection 0.58 below X 256, else 0.6.
  // With -0.005 X scale these place native X at X+84 or X+80 on top.
  const float screenOffsetX = quads.FirstX < 256.0F ? 84.0F : 80.0F;
  *bounds = {true, (quads.MinimumX + quads.MaximumX) * 0.5F + screenOffsetX,
             quads.MinimumY, quads.MaximumY, quads.MaximumX - quads.MinimumX,
             screenOffsetX};
  return true;
}

bool ReadTopScreenGearCursorBounds(const NativeA32Memory &memory,
                                   TopScreenItemsHintCursorBounds *bounds,
                                   std::string *error) {
  if (bounds == nullptr) {
    SetError(error, "TopScreen Gear cursor bounds output is null");
    return false;
  }
  *bounds = {};
  if (error != nullptr) {
    error->clear();
  }
  std::uint32_t owner = 0U;
  if (!memory.ReadFast(kGearCursorOwner, &owner)) {
    SetError(error, "cannot read the native Gear cursor owner");
    return false;
  }
  if (owner == 0U) {
    return true;
  }
  CursorQuadBounds quads;
  if (!ScanCursorQuads(memory, owner, &quads, error)) {
    return false;
  }
  if (!quads.Found) {
    return true;
  }
  constexpr float screenOffsetX = 40.0F;
  *bounds = {true, (quads.MinimumX + quads.MaximumX) * 0.5F + screenOffsetX,
             quads.MinimumY, quads.MaximumY, quads.MaximumX - quads.MinimumX,
             screenOffsetX};
  return true;
}

bool ReadTopScreenGearHoldRingCenter(const NativeA32Memory &memory, std::uint32_t slot,
                                     float *centerX, float *centerY) {
  constexpr std::uint32_t kGearSlotLayout = 0x00504BD4U;
  if (centerX == nullptr || centerY == nullptr || slot > 0x1AU) {
    return false;
  }
  std::array<std::uint32_t, 4> words{};
  for (std::uint32_t lane = 0U; lane < words.size(); ++lane) {
    if (!memory.ReadFast(kGearSlotLayout + (lane * 27U + slot) * 4U, &words[lane])) {
      return false;
    }
  }
  const auto value = [&](std::uint32_t lane) { return std::bit_cast<float>(words[lane]); };
  const float x = value(0), y = value(1), width = value(2), height = value(3);
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height)) {
    return false;
  }
  *centerX = x + width + 12.133333F - 2.0F + 40.0F;
  *centerY = y + height * 0.5F;
  return true;
}

} // namespace Oot3dNativeGame
