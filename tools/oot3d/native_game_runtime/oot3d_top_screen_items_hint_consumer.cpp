#include "oot3d_top_screen_items_hint_consumer.h"

#include "oot3d_native_a32_memory.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

namespace Oot3dNativeGame {
namespace {

constexpr std::uint32_t kPauseItemsState = 0x005066F8U;
constexpr std::uint32_t kPauseItemsControllerStateOffset = 0x34U;
constexpr std::uint32_t kPauseItemsSelectedModelOffset = 0xB0U;
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

  std::uint32_t positions = 0U;
  if (!ResolveSelectedModelPositions(memory, selectedModel, &positions,
                                     error)) {
    return false;
  }
  if (positions == 0U) {
    return true;
  }

  // FUN_005CA270 scans exactly four quads of four xyz vertices and ignores
  // the native hidden-vertex sentinel lane at x >= 390.
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
    if (!std::isfinite(x) || !std::isfinite(y) || x >= kMaximumHintVertexX) {
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

  *bounds = {true, (minimumX + maximumX) * 0.5F, minimumY, maximumY,
             maximumX - minimumX};
  return true;
}

} // namespace Oot3dNativeGame
