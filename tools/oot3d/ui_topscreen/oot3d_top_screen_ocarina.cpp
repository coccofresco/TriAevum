#include "oot3d_top_screen_ocarina.h"
#include "oot3d_native_a32_memory.h"
#include "oot3d_ui/ui_contract_types.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <utility>

namespace Oot3dNativeGame {
namespace {
void SetError(std::string *error, const char *message) { if (error) *error = message; }
constexpr std::array<std::array<std::uint8_t, 3>, 12> kTopScreenOcarinaColors{{
    {{0xD8, 0x82, 0xE6}},
    {{0xF0, 0xC8, 0x3C}},
    {{0x3C, 0xC8, 0x46}},
    {{0xF0, 0x8C, 0x28}},
    {{0xD2, 0x96, 0x5A}},
    {{0x6E, 0xC8, 0xE6}},
    {{0xF0, 0x3C, 0x32}},
    {{0x96, 0x50, 0xDC}},
    {{0x64, 0xDC, 0x5A}},
    {{0x96, 0xA0, 0xBE}},
    {{0x46, 0x82, 0xF0}},
    {{0xF0, 0xE1, 0x46}},
}};
}

bool ReadTopScreenOcarinaState(NativeA32Memory &memory,
                              TopScreenOcarinaState *state) {
  if (!state) return false;
  *state = {};
  std::uint32_t scene = 0, transition = 0, column = 0, row = 0;
  std::uint8_t mode = 0;
  bool suppressed = true;
  if (!memory.Read32(0x005043E0U, &scene) ||
      !memory.Read32(0x005093F8U, &state->Page) ||
      !memory.Read32(0x0050941CU, &transition) ||
      !memory.Read32(0x005093FCU, &column) ||
      !memory.Read32(0x00509400U, &row)) return false;
  if (!scene) return true;
  if (!memory.Read8(scene + 0x100U, &mode) ||
      !ReadTopScreenOcarinaUiActive(memory, &suppressed)) return false;
  state->CursorSong = column < 4 && row < 3 ? column + row * 4 : 11;
  state->Active = !suppressed && state->Page != 0 && transition == 0 && mode == 3;
  return true;
}

void TopScreenOcarinaBrowser::Reset() noexcept {
  mSong = -2;
  mDirection = 0;
  mRepeat = 0;
}

void TopScreenOcarinaBrowser::Advance(const TopScreenOcarinaState &state,
                                    bool left, bool right,
                                    bool leftPressed, bool rightPressed) noexcept {
  // Original 0x005D3D30: immediate direction change, then 12/5 native updates.
  // Unlearned songs remain selectable and display the original unknown marker.
  if (state.Page != 12) { Reset(); return; }
  if (!state.Active) return;
  const int direction = right ? 1 : (left ? -1 : 0);
  bool step = direction != mDirection ||
              (direction > 0 ? rightPressed : direction < 0 && leftPressed);
  if (step || direction == 0) mRepeat = 0;
  else if (++mRepeat >= 12) {
    step = true;
    mRepeat = 7;
  }
  mDirection = direction;
  if (step && direction) {
    mSong = mSong < 0 ? (direction > 0 ? 0 : 11)
                     : (mSong + direction + 12) % 12;
  }
}

bool ReadTopScreenOcarinaGeometry(NativeA32Memory &memory,
                                   const TopScreenOcarinaBrowser &browser,
                                   TopScreenOcarinaGeometry *geometry,
                                   std::string *error) {
  if (geometry == nullptr) {
    SetError(error, "TopScreen ocarina geometry output is null");
    return false;
  }
  *geometry = {};

  constexpr std::uint32_t kOcarina = 0x005093E4U;
  constexpr std::uint32_t kPositionArray = kOcarina + 0x68U;
  constexpr std::uint32_t kSizeArray = kOcarina + 0x3C8U;
  constexpr std::uint32_t kAtlasSizeArray = kOcarina + 0x728U;
  constexpr std::uint32_t kAtlasOriginArray = kOcarina + 0xA88U;
  constexpr std::uint32_t kSongCountTable = 0x004D53C8U;
  constexpr std::uint32_t kSongTypeTable = 0x004D541CU;
  constexpr std::uint32_t kSongFlagIndexTable = 0x0050A3B0U;
  constexpr std::uint32_t kSongFlagMaskTable = 0x0053C9D4U;
  constexpr std::uint32_t kSongFlagState = 0x00587A14U;
  constexpr std::uint32_t kNoteAtlasX = 0x0050A1E0U;
  constexpr std::uint32_t kNotePositionY = 0x0050A1CCU;

  TopScreenOcarinaState state;
  if (!ReadTopScreenOcarinaState(memory, &state)) {
    SetError(error, "cannot read native ocarina owner state"); return false;
  }
  if (!state.Active) return true;
  const bool localSong = state.Page == 12 && browser.SelectedSong() >= 0;
  const std::uint32_t song = localSong ? browser.SelectedSong() : state.CursorSong;
  geometry->Song = static_cast<std::uint8_t>(song);
  const auto &rgb = kTopScreenOcarinaColors[song];
  const auto color = [&](float alpha) {
    return oot3d::ui::UiColor{static_cast<float>(rgb[0]) / 255.0F,
                              static_cast<float>(rgb[1]) / 255.0F,
                              static_cast<float>(rgb[2]) / 255.0F, alpha};
  };
  const bool songFocused = localSong || state.Page == 4U;
  geometry->SongSelected = songFocused;

  const auto readVec2 = [&](std::uint32_t address, TopScreenVec2 *value) {
    std::array<std::uint32_t, 2> words{};
    if (value == nullptr || !memory.Read32(address, &words[0]) ||
        !memory.Read32(address + 4U, &words[1])) {
      return false;
    }
    value->X = std::bit_cast<float>(words[0]);
    value->Y = std::bit_cast<float>(words[1]);
    return std::isfinite(value->X) && std::isfinite(value->Y);
  };

  for (std::uint32_t index = 0U; index < 6U; ++index) {
    constexpr std::array<std::uint32_t, 6> kBandSources{0, 1, 2, 35, 36, 37};
    const auto source = kBandSources[index];
    auto &quad = geometry->Quads[index];
    if (!readVec2(kPositionArray + source * 8U, &quad.Position) ||
        !readVec2(kSizeArray + source * 8U, &quad.Size) ||
        !readVec2(kAtlasOriginArray + source * 8U, &quad.AtlasOrigin) ||
        !readVec2(kAtlasSizeArray + source * 8U, &quad.AtlasSize)) {
      SetError(error, "cannot read native PauseOcarina quad arrays");
      return false;
    }
    quad.Position.X += 40.0F;
    quad.Position.Y += index < 3U ? 168.0F : -182.0F;
    if (index < 3U) {
      quad.Position.Y += 1.0F;
      quad.Size.Y -= 1.0F;
      quad.AtlasOrigin.Y += 1.0F;
      quad.AtlasSize.Y -= 1.0F;
    } else if (index == 3U) {
      quad.AtlasSize.X -= 1.0F;
    } else if (index == 5U) {
      quad.AtlasOrigin.X -= 1.0F;
      quad.AtlasSize.X += 1.0F;
    }
    quad.Color = {1, 1, 1, songFocused ? 0.68F : 0.30F};
    quad.Visible = quad.Size.X != 0.0F && quad.Size.Y != 0.0F;
  }

  std::uint32_t songFlags = 0U;
  std::uint32_t flagIndex = 0U;
  std::uint32_t flagMask = 0U;
  std::uint32_t noteCount = 0U;
  std::uint32_t noteTypes = 0U;
  if (!memory.Read32(kSongFlagState, &songFlags) ||
      !memory.Read32(kSongFlagIndexTable + song * 4U,
                     &flagIndex) || flagIndex > (UINT32_MAX - kSongFlagMaskTable) / 4U ||
      !memory.Read32(kSongFlagMaskTable + flagIndex * 4U, &flagMask) ||
      !memory.Read32(kSongCountTable + song * 4U, &noteCount) ||
      !memory.Read32(kSongTypeTable + song * 4U, &noteTypes)) {
    SetError(error, "cannot read native ocarina song tables");
    return false;
  }
  const bool notesEnabled =
      songFocused && (songFlags & flagMask) != 0U;
  geometry->SongLearned = (songFlags & flagMask) != 0U;
  // 0x005D2720..0x005D2734, the mod's own localized menu atlas.
  geometry->UnknownSong = {songFocused && !geometry->SongLearned,
      {173, 26}, {54, 24}, {406, 484}, {54, 24}, color(0.85F)};
  for (std::uint32_t note = 0U; note < 8U; ++note) {
    auto &quad = geometry->Quads[note + 6U];
    std::uint32_t noteType = 0U;
    const bool visible =
        notesEnabled && note < noteCount && noteTypes != 0U;
    if (visible && (noteTypes > UINT32_MAX - 32U ||
                    !memory.Read32(noteTypes + note * 4U, &noteType))) {
      SetError(error, "cannot read native ocarina note type");
      return false;
    }
    noteType = std::min<std::uint32_t>(noteType, 4U);
    std::uint32_t noteYWord = 0U;
    if (!memory.Read32(kNotePositionY + noteType * 4U, &noteYWord)) {
      SetError(error, "cannot read native ocarina note position");
      return false;
    }
    std::uint32_t noteAtlasWord = 0U;
    if (!memory.Read32(kNoteAtlasX + noteType * 4U, &noteAtlasWord)) {
      SetError(error, "cannot read native ocarina note atlas table");
      return false;
    }
    quad.Position.X = 122.0F + static_cast<float>(note) * 24.0F;
    quad.Position.Y = std::bit_cast<float>(noteYWord) + 168.0F;
    quad.Size = visible ? TopScreenVec2{16.0F, 16.0F} : TopScreenVec2{};
    quad.AtlasOrigin = {std::bit_cast<float>(noteAtlasWord), 160.0F};
    quad.AtlasSize = {16.0F, 16.0F};
    quad.Color = {1, 1, 1, 0.85F};
    quad.Visible = visible;
  }

  for (std::uint32_t index = 14U; index < 16U; ++index) {
    auto &quad = geometry->Quads[index];
    quad.Position = {index == 14U ? 308.0F : 68.0F, 24.0F};
    quad.Size =
        songFocused ? TopScreenVec2{24.0F, 28.0F} : TopScreenVec2{};
    quad.AtlasOrigin = {112.0F, 160.0F};
    quad.AtlasSize = {23.0F, 28.0F};
    quad.Color = color(0.68F);
    quad.Visible = songFocused;
  }
  geometry->Active = true;
  return true;
}

std::size_t AppendTopScreenOcarinaPresentation(
    const TopScreenOcarinaGeometry &geometry,
    const oot3d::ui::UiTextureIdentity &ocarinaPage,
    std::vector<oot3d::ui::UiPrimitive> &output,
    const oot3d::ui::UiTextureIdentity &menuTexture) {
  if (!geometry.Active || ocarinaPage.semantic_name.empty()) {
    return 0U;
  }
  const std::size_t startSize = output.size();
  for (std::size_t index = 0; index < geometry.Quads.size(); ++index) {
    const auto &quad = geometry.Quads[index];
    if (!quad.Visible || quad.Size.X == 0.0F || quad.Size.Y == 0.0F) {
      continue;
    }
    oot3d::ui::UiPrimitive primitive;
    primitive.subsystem = oot3d::ui::UiSubsystem::TouchControls;
    primitive.role = oot3d::ui::UiPrimitiveRole::TouchControl;
    primitive.owner_address = 0x005D2078U;
    primitive.source_quad = static_cast<std::uint32_t>(index);
    primitive.texture = ocarinaPage;
    primitive.destination = {quad.Position.X, quad.Position.Y, quad.Size.X,
                             quad.Size.Y};
    primitive.uv = {quad.AtlasOrigin.X / 512.0F,
                    quad.AtlasOrigin.Y / 256.0F,
                    quad.AtlasSize.X / 512.0F, quad.AtlasSize.Y / 256.0F};
    primitive.color = quad.Color;
    primitive.layer = 20U;
    output.push_back(std::move(primitive));
  }
  if (geometry.UnknownSong.Visible && !menuTexture.semantic_name.empty()) {
    const auto &quad = geometry.UnknownSong;
    oot3d::ui::UiPrimitive primitive;
    primitive.subsystem = oot3d::ui::UiSubsystem::TouchControls;
    primitive.role = oot3d::ui::UiPrimitiveRole::TouchControl;
    primitive.owner_address = 0x005D2078U;
    primitive.source_quad = 16;
    primitive.texture = menuTexture;
    primitive.destination = {quad.Position.X, quad.Position.Y, quad.Size.X, quad.Size.Y};
    primitive.uv = {quad.AtlasOrigin.X / 512, quad.AtlasOrigin.Y / 512,
                    quad.AtlasSize.X / 512, quad.AtlasSize.Y / 512};
    primitive.color = quad.Color;
    primitive.layer = 21;
    output.push_back(std::move(primitive));
  }
  return output.size() - startSize;
}

bool ReadTopScreenOcarinaSongMessage(NativeA32Memory &memory,
                                     const TopScreenOcarinaGeometry &geometry,
                                     std::uint32_t *message, std::string *error) {
  if (!message) return false;
  *message = 0;
  if (!geometry.Active || !geometry.SongSelected || !geometry.SongLearned) return true;
  std::uint32_t index = 0;
  if (geometry.Song >= 12 ||
      !memory.Read32(0x004D5480U + geometry.Song * 4U, &index) ||
      index > UINT32_MAX - 0x9ADU) {
    SetError(error, "cannot read native ocarina message table");
    return false;
  }
  *message = index + 0x9ADU;
  return true;
}

bool ReadTopScreenOcarinaTextPrimitives(
    NativeA32Memory &memory, std::uint32_t overlay,
    const TopScreenOcarinaGeometry &geometry,
    std::vector<oot3d::ui::UiPrimitive> &output, std::string *error) {
  std::vector<oot3d::ui::UiPrimitive> staged;
  if (geometry.Song >= kTopScreenOcarinaColors.size() ||
      !overlay || !memory.IsMapped(overlay, 0x4C)) {
    SetError(error, "invalid native ocarina text owner"); return false;
  }
  const auto readFloats = [&](std::uint32_t address, auto &values) {
    return memory.ReadBytes(address, std::span<std::uint8_t>(
        reinterpret_cast<std::uint8_t *>(values.data()), sizeof(values))) &&
        std::all_of(values.begin(), values.end(), [](float f) { return std::isfinite(f); });
  };
  for (std::uint32_t layer = 0; layer < 2; ++layer) {
    std::uint32_t model = 0, descriptor = 0, count = 0, flags = 0, buffer = 0;
    std::uint32_t texture = 0, surface = 0, buffers = 0;
    // 0x00348BE4 owns the packed vertex buffer: position, optional normal,
    // optional UV, optional color. The generated overlay is single-buffered.
    if (!memory.Read32(overlay + 8U + layer * 4U, &model) || !model || !memory.IsMapped(model, 0x1B8) ||
        !memory.Read32(model, &descriptor) || !descriptor || !memory.IsMapped(descriptor, 0x20) ||
        !memory.Read32(descriptor + 0xCU, &count) || count == 0 || count > 4096 || count % 4 ||
        !memory.Read32(descriptor + 0x1CU, &flags) || (flags & 0x118U) != 0 ||
        !memory.Read32(model + 0x128U, &buffers) || buffers != 1 ||
        !memory.Read32(model + 0x1A0U, &buffer) || !buffer ||
        !memory.Read32(overlay + layer * 4U, &texture) || !texture || !memory.IsMapped(texture, 0x54) ||
        !memory.Read32(texture + 0x4CU, &surface) || !surface) {
      SetError(error, "incompatible native generated-text model"); return false;
    }
    const std::uint32_t uvOffset = count * ((flags & 0x80U) ? 12U : 24U);
    const std::uint32_t colorOffset = uvOffset + count * 8U;
    const std::uint32_t totalBytes = colorOffset + count * 16U;
    if (buffer > UINT32_MAX - totalBytes || !memory.IsMapped(buffer, totalBytes)) {
      SetError(error, "native generated-text buffer is unmapped"); return false;
    }
    for (std::uint32_t vertex = 0; vertex < count; vertex += 4) {
      std::array<float, 12> positions{};
      std::array<float, 8> uvs{};
      std::array<float, 16> colors{};
      if (!readFloats(buffer + vertex * 12U, positions) ||
          !readFloats(buffer + uvOffset + vertex * 8U, uvs) ||
          !readFloats(buffer + colorOffset + vertex * 16U, colors)) {
        SetError(error, "invalid native generated-text vertices"); return false;
      }
      oot3d::ui::UiPrimitive primitive;
      primitive.subsystem = oot3d::ui::UiSubsystem::TouchControls;
      primitive.role = oot3d::ui::UiPrimitiveRole::PauseText;
      primitive.owner_address = 0x005D4A8CU;
      primitive.source_quad = vertex / 4;
      primitive.texture = {texture, surface, "oot3d/native/generated_text"};
      // Native 0x005D2078 shifts the rotated viewport by -40: +40 in host X.
      // The text builder emits TL, BL, TR, BR, unlike the ordinary UI quads.
      primitive.destination = {positions[0] + 40.0F, positions[1],
                               positions[6] - positions[0], positions[4] - positions[1]};
      primitive.uv = {uvs[0], 1.0F - uvs[1], uvs[4] - uvs[0], uvs[1] - uvs[3]};
      primitive.color = {colors[0], colors[1], colors[2], colors[3]};
      if (layer == 1) {
        const auto &rgb = kTopScreenOcarinaColors[geometry.Song];
        primitive.color.red = rgb[0] / 255.0F;
        primitive.color.green = rgb[1] / 255.0F;
        primitive.color.blue = rgb[2] / 255.0F;
      }
      primitive.layer = 22U + layer;
      staged.push_back(std::move(primitive));
    }
  }
  output.insert(output.end(), staged.begin(), staged.end());
  return true;
}


TopScreenOcarinaNavigationGeometry
BuildTopScreenOcarinaNavigationGeometry(std::int8_t direction) noexcept {
  TopScreenOcarinaNavigationGeometry geometry;
  const std::array<bool, 2> active{direction < 0, direction > 0};
  constexpr std::array<float, 2> kCentersX{50.0F, 350.0F};
  for (std::size_t index = 0; index < geometry.Quads.size(); ++index) {
    auto &quad = geometry.Quads[index];
    const bool left = index == 0U;
    quad.Visible = true;
    quad.Position = {kCentersX[index] - 13.0F,
                     204.0F - 13.0F + (active[index] ? 2.0F : 0.0F)};
    quad.Size = {26.0F, 26.0F};
    quad.AtlasOrigin = {left ? 512.0F : 486.0F, 230.0F};
    quad.AtlasSize = {left ? -26.0F : 26.0F, 26.0F};
    const float rgb = active[index] ? 1.0F : 0.6F;
    quad.Color = {rgb, rgb, rgb, 0.8F};
  }
  return geometry;
}

std::size_t AppendTopScreenOcarinaNavigationPresentation(
    const TopScreenOcarinaNavigationGeometry &geometry,
    const oot3d::ui::UiTextureIdentity &pauseTopPage,
    std::vector<oot3d::ui::UiPrimitive> &output) {
  if (pauseTopPage.semantic_name.empty()) {
    return 0U;
  }
  const std::size_t startSize = output.size();
  for (std::size_t index = 0; index < geometry.Quads.size(); ++index) {
    const auto &quad = geometry.Quads[index];
    if (!quad.Visible) {
      continue;
    }
    oot3d::ui::UiPrimitive primitive;
    primitive.subsystem = oot3d::ui::UiSubsystem::TouchControls;
    primitive.role = oot3d::ui::UiPrimitiveRole::TouchControl;
    primitive.owner_address = 0x005CBC54U;
    primitive.source_quad = static_cast<std::uint32_t>(index);
    primitive.texture = pauseTopPage;
    primitive.destination = {quad.Position.X, quad.Position.Y, quad.Size.X,
                             quad.Size.Y};
    primitive.uv = {quad.AtlasOrigin.X / 512.0F,
                    quad.AtlasOrigin.Y / 256.0F,
                    quad.AtlasSize.X / 512.0F, quad.AtlasSize.Y / 256.0F};
    primitive.color = quad.Color;
    primitive.layer = 21U;
    output.push_back(std::move(primitive));
  }
  return output.size() - startSize;
}

} // namespace Oot3dNativeGame
