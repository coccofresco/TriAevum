#pragma once

#include "oot3d_top_screen_mod_profile.h"

namespace Oot3dNativeGame {

struct TopScreenOcarinaState {
  bool Active = false;
  std::uint32_t Page = 0;
  std::uint32_t CursorSong = 0;
};

// 2.1.1 owners 0x005C9ED8 and 0x005D2078. This is independent of pause.
bool ReadTopScreenOcarinaState(NativeA32Memory &memory,
                              TopScreenOcarinaState *state);

class TopScreenOcarinaBrowser {
public:
  void Reset() noexcept;
  void Advance(const TopScreenOcarinaState &state, bool left, bool right,
               bool leftPressed = false, bool rightPressed = false,
               bool togglePressed = false) noexcept;
  bool GuideHidden() const noexcept { return mGuideHidden; }
  std::uint32_t TakeGuideSound() noexcept {
    const auto sound = mGuideSound;
    mGuideSound = 0;
    return sound;
  }
  int SelectedSong() const noexcept { return mSong; }
  std::int8_t Direction() const noexcept { return static_cast<std::int8_t>(mDirection); }
private:
  int mSong = -2;
  int mDirection = 0;
  unsigned mRepeat = 0;
  bool mGuideHidden = false;
  std::uint32_t mGuideSound = 0;
};

struct TopScreenOcarinaGeometry {
  bool Active = false;
  bool SongSelected = false;
  bool SongLearned = false;
  std::uint8_t Song = 0;
  std::array<TopScreenTexturedQuad, 16> Quads{};
  TopScreenTexturedQuad UnknownSong{};
};

// Native message selection, not a host-font or translated-string table.
bool ReadTopScreenOcarinaSongMessage(NativeA32Memory &memory,
                                     const TopScreenOcarinaGeometry &geometry,
                                     std::uint32_t *message,
                                     std::string *error = nullptr);

// Imports the two generated glyph models (shadow, then colored foreground).
bool ReadTopScreenOcarinaTextPrimitives(
    NativeA32Memory &memory, std::uint32_t overlay,
    const TopScreenOcarinaGeometry &geometry,
    std::vector<oot3d::ui::UiPrimitive> &output,
    std::string *error = nullptr);

bool ReadTopScreenOcarinaGeometry(NativeA32Memory &memory,
                                 const TopScreenOcarinaBrowser &browser,
                                 TopScreenOcarinaGeometry *geometry,
                                 std::string *error = nullptr);
std::size_t AppendTopScreenOcarinaPresentation(
    const TopScreenOcarinaGeometry &geometry,
    const oot3d::ui::UiTextureIdentity &texture,
    std::vector<oot3d::ui::UiPrimitive> &output,
    const oot3d::ui::UiTextureIdentity &menuTexture = {});

struct TopScreenOcarinaNavigationGeometry {
  std::array<TopScreenTexturedQuad, 2> Quads{};
};

// Ocarina navigation arrows: 2.1.1 producers 0x005CBC54/0x005CA7B8.
// Direction is derived from the native OoT3D input frame: -1 left, +1 right.
TopScreenOcarinaNavigationGeometry
BuildTopScreenOcarinaNavigationGeometry(std::int8_t direction) noexcept;

std::size_t AppendTopScreenOcarinaNavigationPresentation(
    const TopScreenOcarinaNavigationGeometry &geometry,
    const oot3d::ui::UiTextureIdentity &pauseTopPage,
    std::vector<oot3d::ui::UiPrimitive> &output);

} // namespace Oot3dNativeGame
