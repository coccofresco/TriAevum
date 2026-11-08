#pragma once

#include <array>
#include <cstdint>

namespace Oot3dNativeGame {
// Original EUR v1.1 image. Meaning and independent verification:
// W/analysis/hooks/MAP_7.md G8.01 (Play_Update and its transition consumer).
// Data-proof windows are not observers and must not be installed as hooks.
struct Mm3dHookSite {
  const char* Name;
  uint32_t Pc;
  std::array<uint32_t, 4> Words;
};
inline constexpr Mm3dHookSite kMm3dPlayUpdateSite{
    "Play_Update", 0x0015F4FC, {0xE92D4FF0, 0xE1A04000, 0xE2800028, 0xED2D8B02}};
inline constexpr Mm3dHookSite kMm3dTransitionSite{
    "Play_UpdateTransition", 0x00176AFC, {0xE92D4FF0, 0xE1A04000, 0xE2800B33, 0xE2847CC5}};
inline constexpr Mm3dHookSite kMm3dCameraUpdateSite{
    "Camera_Update", 0x00180B68, {0xE92D4FF0, 0xE2815C01, 0xE1A04001, 0xE1A0A000}};

template<class ReadWord>
bool Mm3dHookSiteMatches(const Mm3dHookSite& site, ReadWord&& read) {
  for (uint32_t i = 0; i < site.Words.size(); ++i) {
    uint32_t word = 0;
    if (!read(site.Pc + i * 4, word) || word != site.Words[i]) return false;
  }
  return true;
}
} // namespace Oot3dNativeGame
