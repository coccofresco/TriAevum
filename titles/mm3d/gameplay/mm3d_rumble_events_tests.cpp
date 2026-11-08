// MM3D rumble sample -> shared Oot3dRumbleEventTracker -> pulse, from a
// sparse literal memory fixture (no process, ROM or pad). Decoys sit at the
// OoT3D/N64 offsets so a reader using them fails these checks.
#include "mm3d_rumble_events.h"
#include <bit>
#include <cstdio>
#include <map>
#include <stdexcept>

namespace {
using namespace Oot3dNativeGame;
int checks = 0;
void Check(bool ok, const char* why) { ++checks; if (!ok) throw std::runtime_error(why); }
struct Memory {
  std::map<uint32_t, uint8_t> Bytes;
  template<class T> bool Read(uint32_t a, T* v) {
    *v = 0;
    for (uint32_t i = 0; i < sizeof(T); ++i) {
      auto it = Bytes.find(a + i); if (it == Bytes.end()) return false;
      *v |= T(uint32_t(it->second) << (i * 8U));
    }
    return true;
  }
  bool Read8(uint32_t a, uint8_t* v) { return Read(a, v); }
  bool Read16(uint32_t a, uint16_t* v) { return Read(a, v); }
  bool Read32(uint32_t a, uint32_t* v) { return Read(a, v); }
  void Put(uint32_t a, uint32_t v, uint32_t w = 4) { for (uint32_t i = 0; i < w; ++i) Bytes[a + i] = uint8_t(v >> (i * 8U)); }
  void Float(uint32_t a, float v) { Put(a, std::bit_cast<uint32_t>(v)); }
};
constexpr uint32_t play = 0x10000000U, player = 0x10020000U;
Memory Fixture() {
  Memory m;
  m.Put(0x00656B5CU, play + 0x1F8U);      // current View -> Play
  m.Put(play + 0x13DU, 4, 1);             // Play state type
  m.Put(play + 0x4A8U, play);             // main camera backpointer
  m.Put(play + 0x148U, 0x6F, 2);          // scene
  m.Put(play + 0x138U, 100);
  m.Put(play + 0x20DCU, player);          // Player list head
  m.Put(player, 0, 2); m.Put(player + 2U, 2, 1); m.Put(player + 0x140U, 0x00123450U);
  m.Float(player + 0x24U, 1); m.Float(player + 0x28U, 2); m.Float(player + 0x2CU, 3);
  m.Put(player + 0xC2U, 0, 2);
  m.Put(0x00775314U, 0x30, 2);            // three hearts
  m.Float(player + 0x68U, 0.0F);          // velocity.y
  m.Put(player + 0x94U, 1, 2);            // grounded
  // Decoys: OoT3D/N64 offsets that must not be used.
  m.Float(player + 0x64U, -30.0F);        // velocity.x here, OoT3D velocity.y
  m.Put(player + 0x90U, 1, 2);            // N64 MM bgCheckFlags: always grounded
  m.Put(0x00587958U + 0x44U, 0x30, 2);    // OoT3D health
  return m;
}
std::vector<Oot3dRumbleEvent> Step(Memory& m, Oot3dRumbleEventTracker& t) {
  Oot3dRumbleSample s;
  Check(ReadMm3dRumbleSample(m, &s) && s.Gameplay, "live MM3D play sample read");
  return t.Observe(s);
}
void TestDamage() {
  auto m = Fixture(); Oot3dRumbleEventTracker t;
  Check(Step(m, t).empty(), "first sample is only a baseline");
  m.Put(0x00775314U, 0x20, 2);
  const auto e = Step(m, t);
  Check(e.size() == 1 && e[0].Kind == Oot3dRumbleEventKind::Damage && e[0].Amount == 16.0F, "one heart of MM3D damage");
  const auto pulse = Oot3dRumblePulse(e[0]);
  Check(pulse.Intensity == 0.75F && pulse.DurationMilliseconds == 230, "same pulse as OoT3D for one heart");
  m.Put(0x00775314U, 0x30, 2);
  Check(Step(m, t).empty(), "healing is not an event");
}
void TestHardLanding() {
  auto m = Fixture(); Oot3dRumbleEventTracker t;
  (void)Step(m, t);
  m.Put(player + 0x94U, 0, 2); m.Float(player + 0x68U, -6.0F); Check(Step(m, t).empty(), "airborne");
  m.Float(player + 0x68U, -14.0F); Check(Step(m, t).empty(), "falling faster");
  m.Put(player + 0x94U, 1, 2); m.Float(player + 0x68U, 0.0F);
  const auto e = Step(m, t);
  Check(e.size() == 1 && e[0].Kind == Oot3dRumbleEventKind::HardLanding && e[0].Amount == 14.0F,
        "MM3D hard landing from Actor velocity.y 0x68 and flags 0x94");
  m.Put(player + 0x94U, 0, 2); m.Float(player + 0x68U, -5.0F); (void)Step(m, t);
  m.Put(player + 0x94U, 1, 2); Check(Step(m, t).empty(), "a short hop is not a hard landing");
  m.Put(player + 0x94U, 0, 2); m.Float(player + 0x68U, -20.0F); (void)Step(m, t);
  m.Put(player + 0x94U, 0x20, 2); Check(Step(m, t).empty(), "landing in water is not a hard landing");
}
void TestBaselines() {
  auto m = Fixture(); Oot3dRumbleEventTracker t; (void)Step(m, t);
  m.Put(play + 0x148U, 0x70, 2); m.Put(0x00775314U, 0x10, 2);
  Check(Step(m, t).empty(), "a scene change resets the baseline");
  const uint32_t other = 0x10030000U;
  for (uint32_t a = 0; a < 0x1F8U; ++a) if (m.Bytes.count(player + a)) m.Bytes[other + a] = m.Bytes[player + a];
  m.Put(play + 0x20DCU, other); m.Put(0x00775314U, 0x08, 2);
  Check(Step(m, t).empty(), "a new Player actor (form change, respawn) resets the baseline");
  auto cut = Fixture(); cut.Put(play + 0x13DU, 3, 1);
  Oot3dRumbleSample s; Check(!ReadMm3dRumbleSample(cut, &s) && !s.Gameplay, "outside Play nothing is sampled");
  auto noHealth = Fixture(); noHealth.Bytes.erase(0x00775314U);
  Check(!ReadMm3dRumbleSample(noHealth, &s) && !s.Gameplay, "failed health read fails closed");
  Check(!ReadMm3dRumbleSample(cut, nullptr), "null sample refused");
}
} // namespace
int main() {
  try { TestDamage(); TestHardLanding(); TestBaselines(); }
  catch (const std::exception& e) { std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
  std::printf("mm3d_rumble_events_tests: %d checks passed\n", checks);
  return 0;
}
