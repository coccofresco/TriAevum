#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Oot3dNativeGame {

// SingleScreen 3.0 `rom:/actor/zelda_link_ext.zar`: the Roll Dash animations
// that stand in for native entries of Link's animation archive. The table is
// the dash part of the payload's 0x006094DC (name, native animation index);
// its N64 landing roll entries are not ported.
struct TopScreenLinkExtAnimation {
  const char *Name;
  std::uint16_t NativeIndex;
};

inline constexpr std::array<TopScreenLinkExtAnimation, 10> kTopScreenLinkExtAnimations{{
    {"nml_dash_free", 0x06CU},
    {"nml_dash", 0x06DU},
    {"nml_heavy_dash_free", 0x09AU},
    {"nml_heavy_dash", 0x09BU},
    {"nml_damage_dash_free", 0x0C4U},
    {"ft_dash_long", 0x127U},
    {"ft_dash", 0x128U},
    {"ft_heavy_dash_long", 0x16DU},
    {"ft_damage_dash_long", 0x175U},
    {"ft_damage_dash", 0x176U},
}};

// Guest image of the selected animations, placed at a host-reserved guest
// region (the 3.0 payload allocates the same bytes from the game heap):
//   +0x00 magic, +0x04 content hash, +0x08/+0x0C/+0x10 the archive, mask
//   and original entries earlier builds patched (restored from old states),
//   +0x80 slots [age][16] (adult 0, child 1; slot word = CSAB bytes),
//   +0x100 CSAB bytes.
// A slot's address stands in for a native archive entry, whose first word
// is likewise the CSAB pointer.
struct TopScreenLinkExtImage {
  static constexpr std::uint32_t kMagic = 0x584C5354U; // "TSLX"
  static constexpr std::uint32_t kPatchedArchive = 0x08U;
  static constexpr std::uint32_t kPatchedMask = 0x0CU;
  static constexpr std::uint32_t kOriginals = 0x10U;
  static constexpr std::uint32_t kSlots = 0x80U;
  static constexpr std::uint32_t kData = 0x100U;

  std::vector<std::uint8_t> Bytes;
  std::uint32_t ContentHash = 0;
  // slot [age][i] != 0: the archive held animation i for that age (a child
  // animation the archive lacks reuses the adult one, as 3.0 does).
  std::array<std::array<bool, kTopScreenLinkExtAnimations.size()>, 2> Present{};

  [[nodiscard]] static std::uint32_t SlotAddress(std::uint32_t base, std::uint32_t age,
                                                 std::size_t index) noexcept {
    return base + kSlots + (age * 16U + static_cast<std::uint32_t>(index)) * 4U;
  }
};

// Parses a ZAR v1 archive with 3.0's rules (0x005D8BA8): csab entries named
// adult/<name>.csab or child/<name>.csab whose name is in the table. Returns
// false with `error` when the archive is malformed or holds no animation.
bool BuildTopScreenLinkExtImage(std::span<const std::uint8_t> archive, std::uint32_t base,
                                TopScreenLinkExtImage *image, std::string *error);

} // namespace Oot3dNativeGame
