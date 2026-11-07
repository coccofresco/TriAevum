#pragma once

#include "gameplay_rumble.h"
#include "../adapter/mm3d_hook_sites.h"
#include <array>
#include <bit>
#include <cmath>

namespace Oot3dNativeGame {
namespace Mm3dRumble {
// EUR v1.1, re-read against code.bin 6eec27b1...; MAP_6 G7.04.
// 2Ship mm/src/overlays/actors/ovl_player_actor/z_player.c func_80836F10:
// fallDistance > 200, except floors 6/9 and csAction != 0. The motor calls
// are absent in MM3D. Use that predicate at a ground edge, not a stale distance
// alone. This is a sampled analogue, not an exact fall-impact callback.
inline constexpr std::array<Mm3dHookSite, 6> Guards{{
    {"health read", 0x21E340, {0xE59F003C,0xE1D023BC,0xE1D013FA,0xE0822004}},
    {"player list", 0x20F748, {0xE1A04001,0xE1A07000,0xE2800A02,0xE59050DC}},
    {"velocity gravity", 0x356500, {0xED900A1A,0xEDD00A1D,0xEE300A20,0xED800A1A}},
    {"background flags", 0x204AB8, {0xE1D019B4,0xE3110C02,0x0A000007,0xE59F1544}},
    {"fall distance", 0x1E732C, {0xE5D4092C,0xE3500000,0x01D97DF2,0x0A000000}},
    {"fall floor", 0x1E7318, {0xE59FA144,0xE59A0230,0xE3500006,0x13500009}}
}};
inline constexpr uint32_t Common = 0x7751D8, ViewSlot = 0x656B5C;
inline bool Pointer(uint32_t p, uint32_t bytes) {
  return p >= 0x08000000 && !(p & 3U) && p < 0x40000000 && bytes <= 0x40000000-p;
}
template<class M> bool CodeMatches(M& m) {
  for (const auto& site : Guards)
    if (!Mm3dHookSiteMatches(site, [&](uint32_t a, uint32_t& w) { return m.Read32(a, &w); })) return false;
  uint32_t literal = 0;
  return m.Read32(0x1E7464, &literal) && literal == 0x68D59C;
}
struct Sample {
  ThreeDsRecomp::Input::GameplayRumbleSample Event;
  uint32_t Frame = 0;
  uint8_t Form = 0;
};
template<class M> Sample Read(M& m) {
  Sample out;
  uint32_t view=0, owner=0, player=0, mode=0, frame=0, velocity=0, calc=0, cutscene=0;
  uint16_t scene=0, health=0, id=1, flags=0, pause=0, camera=0, distance=0;
  uint8_t type=0, state=0, category=0, form=0, transition=0, csAction=0;
  if (!m.Read32(ViewSlot,&view) || view < 0x1F8 || !Pointer(view-0x1F8,0x11030)) return out;
  const auto play = view-0x1F8;
  if (!m.Read8(play+0x13D,&type) || type!=4 || !m.Read8(play+0x13C,&state) || state!=2 ||
      !m.Read32(play+0x4A8,&owner) || owner!=play ||
      !m.Read32(Common+0x1361C,&mode) || mode!=0 ||
      !m.Read32(Common+0x1C,&cutscene) || cutscene>=0xFFF0 ||
      !m.Read16(play+0xAAC,&pause) || pause!=0 || !m.Read16(play+0xA7A,&camera) || camera!=0 ||
      !m.Read8(play+0xC529,&transition) || transition!=0 ||
      !m.Read16(play+0x148,&scene) || scene>0x74 || !m.Read32(play+0x138,&frame) ||
      !m.Read32(play+0x20DC,&player) || !Pointer(player,0x12CB8) ||
      !m.Read16(player,&id) || id!=0 || !m.Read8(player+2,&category) || category!=2 ||
      !m.Read32(player+0x140,&calc) || calc!=0x1F9C70 ||
      !m.Read8(Common+0x26,&form) || form>4 ||
      !m.Read16(Common+0x13C,&health) || health>320 ||
      !m.Read32(player+0x68,&velocity) || !m.Read16(player+0x94,&flags) ||
      !m.Read16(player+0x11ED2,&distance) || !m.Read8(player+0x92C,&csAction)) return out;
  const float vy=std::bit_cast<float>(velocity);
  if (!std::isfinite(vy)) return out;
  uint32_t floor=0;
  if (!m.Read32(0x68D59C+0x230,&floor)) return out;
  out.Event = {true,play,player,static_cast<int16_t>(scene),static_cast<int16_t>(health),flags,vy};
  out.Event.AllowHardLanding = static_cast<int16_t>(distance)>200 && floor!=6 && floor!=9 && csAction==0;
  out.Frame=frame;
  out.Form=form;
  return out;
}
class Source {
 public:
  template<class M> void Verify(M& memory) { mVerified=CodeMatches(memory); Reset(); }
  void Reset() { mTracker.Reset(); mPrevious.reset(); }
  template<class M> std::vector<ThreeDsRecomp::Input::RumblePulse> Poll(M& memory, bool enabled) {
    if (!enabled || !mVerified) { Reset(); return {}; }
    const auto sample=Read(memory);
    if (mPrevious && (sample.Frame<mPrevious->Frame || sample.Form!=mPrevious->Form)) mTracker.Reset();
    mPrevious=sample;
    std::vector<ThreeDsRecomp::Input::RumblePulse> pulses;
    for (const auto& event : mTracker.Observe(sample.Event))
      pulses.push_back(ThreeDsRecomp::Input::GameplayRumblePulse(event));
    return pulses;
  }
  bool Verified() const { return mVerified; }
 private:
  bool mVerified=false;
  std::optional<Sample> mPrevious;
  ThreeDsRecomp::Input::GameplayRumbleEventTracker mTracker;
};
} // namespace Mm3dRumble
} // namespace Oot3dNativeGame
