#include "oot3d_rumble_events.h"

#include "recomp/a32_runtime.h"

#include <bit>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>

namespace {

using Oot3dNativeGame::Oot3dRumbleEvent;
using Oot3dNativeGame::Oot3dRumbleEventKind;
using Oot3dNativeGame::Oot3dRumbleEventTracker;
using Oot3dNativeGame::Oot3dRumbleSample;
using ThreeDsRecomp::Input::RumbleStrength;

void Expect(bool condition, const std::string &role) {
  if (!condition) throw std::runtime_error("rumble events: " + role);
}

class ByteBus final : public oot3d::recomp::a32::MemoryBus {
 public:
  bool Read32(uint32_t address, uint32_t *value) override { return ReadN(address, value); }
  bool Write32(uint32_t address, uint32_t value) override { return WriteN(address, value); }
  bool Read16(uint32_t address, uint16_t *value) override { return ReadN(address, value); }
  bool Read8(uint32_t address, uint8_t *value) override { return ReadN(address, value); }
  uint64_t Writes = 0;
  std::map<uint32_t, uint8_t> Bytes;

  template <class T> void Set(uint32_t address, T value) {
    uint8_t raw[sizeof(T)];
    std::memcpy(raw, &value, sizeof(T));
    for (size_t i = 0; i < sizeof(T); ++i) Bytes[address + static_cast<uint32_t>(i)] = raw[i];
  }

 private:
  template <class T> bool ReadN(uint32_t address, T *value) {
    uint8_t raw[sizeof(T)];
    for (size_t i = 0; i < sizeof(T); ++i) {
      const auto found = Bytes.find(address + static_cast<uint32_t>(i));
      if (found == Bytes.end()) return false;
      raw[i] = found->second;
    }
    std::memcpy(value, raw, sizeof(T));
    return true;
  }
  template <class T> bool WriteN(uint32_t address, T value) {
    ++Writes;
    Set(address, value);
    return true;
  }
};

Oot3dRumbleSample Sample(int16_t health, bool grounded, float velocityY, uint32_t player = 0x30000000U,
                         int16_t scene = 0x55) {
  Oot3dRumbleSample sample;
  sample.Gameplay = true;
  sample.PlayState = 0x08000000U;
  sample.Player = player;
  sample.Scene = scene;
  sample.Health = health;
  sample.BgCheckFlags = grounded ? Oot3dNativeGame::kOot3dBgCheckGround : 0U;
  sample.VelocityY = velocityY;
  return sample;
}

void TestDamage() {
  Oot3dRumbleEventTracker tracker;
  Expect(tracker.Observe(Sample(48, true, 0.0F)).empty(), "baseline produced an event");
  Expect(tracker.Observe(Sample(48, true, 0.0F)).empty(), "unchanged health produced an event");
  const auto hit = tracker.Observe(Sample(40, true, 0.0F));
  Expect(hit.size() == 1U && hit[0].Kind == Oot3dRumbleEventKind::Damage && hit[0].Amount == 8.0F,
         "half a heart lost is one damage event of 8");
  Expect(tracker.Observe(Sample(40, true, 0.0F)).empty(), "damage repeated on the next sample");
  Expect(tracker.Observe(Sample(56, true, 0.0F)).empty(), "healing produced an event");
  // A different scene, Player actor or a pause outside gameplay is a new
  // baseline: a lower health there (a file or savestate load) is no damage.
  Expect(tracker.Observe(Sample(16, true, 0.0F, 0x30000000U, 0x56)).empty(), "scene change was damage");
  Expect(tracker.Observe(Sample(8, true, 0.0F, 0x30001000U, 0x56)).empty(), "new Player actor was damage");
  Oot3dRumbleSample paused;
  Expect(tracker.Observe(paused).empty(), "non-gameplay sample produced an event");
  Expect(tracker.Observe(Sample(4, true, 0.0F, 0x30001000U, 0x56)).empty(), "resume after non-gameplay was damage");
  Expect(tracker.Observe(Sample(0, true, 0.0F, 0x30001000U, 0x56)).size() == 1U, "lethal damage missed");
}

void TestLanding() {
  Oot3dRumbleEventTracker tracker;
  (void)tracker.Observe(Sample(48, true, 0.0F));
  // A small hop: never faster than the threshold.
  (void)tracker.Observe(Sample(48, false, 4.0F));
  (void)tracker.Observe(Sample(48, false, -6.0F));
  Expect(tracker.Observe(Sample(48, true, 0.0F)).empty(), "a small hop landed hard");
  // A long fall: the fastest speed of the airborne stretch counts, even when
  // the landing sample's velocity is already reset.
  (void)tracker.Observe(Sample(48, false, -5.0F));
  (void)tracker.Observe(Sample(48, false, -14.0F));
  (void)tracker.Observe(Sample(48, false, -14.0F));
  const auto landed = tracker.Observe(Sample(48, true, 0.0F));
  Expect(landed.size() == 1U && landed[0].Kind == Oot3dRumbleEventKind::HardLanding && landed[0].Amount == 14.0F,
         "a fall at 14 is one hard landing of 14");
  Expect(tracker.Observe(Sample(48, true, 0.0F)).empty(), "landing repeated while grounded");
  // Landing in water is not hard.
  (void)tracker.Observe(Sample(48, false, -18.0F));
  auto water = Sample(48, false, -18.0F);
  water.BgCheckFlags = Oot3dNativeGame::kOot3dBgCheckWater;
  Expect(tracker.Observe(water).empty(), "water entry was an event");
  Expect(tracker.Observe(Sample(48, true, 0.0F)).empty(), "landing after water was hard");
  // A fall that hurts: damage and landing in the same sample.
  (void)tracker.Observe(Sample(48, false, -20.0F));
  const auto both = tracker.Observe(Sample(32, true, 0.0F));
  Expect(both.size() == 2U, "a hurting fall is damage and a hard landing");
  // Falling while the scene changes (a void out) is no landing.
  (void)tracker.Observe(Sample(32, false, -20.0F));
  Expect(tracker.Observe(Sample(32, true, 0.0F, 0x30000000U, 0x60)).empty(), "landing across a scene change");
}

void TestPulsesAndStrength() {
  using ThreeDsRecomp::Input::ResolveRumble;
  const auto halfHeart = Oot3dNativeGame::Oot3dRumblePulse({Oot3dRumbleEventKind::Damage, 8.0F});
  const auto twoHearts = Oot3dNativeGame::Oot3dRumblePulse({Oot3dRumbleEventKind::Damage, 32.0F});
  const auto bigHit = Oot3dNativeGame::Oot3dRumblePulse({Oot3dRumbleEventKind::Damage, 160.0F});
  Expect(halfHeart.Intensity == 0.625F && halfHeart.DurationMilliseconds == 190U, "half-heart pulse");
  Expect(twoHearts.Intensity == 1.0F && bigHit.Intensity == 1.0F && bigHit.DurationMilliseconds == 400U,
         "damage pulse bounds");
  const auto soft = Oot3dNativeGame::Oot3dRumblePulse({Oot3dRumbleEventKind::HardLanding, 8.0F});
  const auto terminal = Oot3dNativeGame::Oot3dRumblePulse({Oot3dRumbleEventKind::HardLanding, 20.0F});
  const auto beyond = Oot3dNativeGame::Oot3dRumblePulse({Oot3dRumbleEventKind::HardLanding, 50.0F});
  Expect(soft.Intensity == 0.3F && terminal.Intensity > 0.89F && terminal.Intensity <= 0.9F &&
             beyond.Intensity == terminal.Intensity && beyond.DurationMilliseconds == 200U,
         "landing pulse bounds");
  Expect(Oot3dNativeGame::Oot3dRumblePulse({Oot3dRumbleEventKind::Damage, 0.0F}).DurationMilliseconds == 0U,
         "no damage has no pulse");
  // Input's strengths: Off plays nothing, Light is below Strong.
  Expect(!ResolveRumble(halfHeart, RumbleStrength::Off).Play, "Off played");
  const auto light = ResolveRumble(halfHeart, RumbleStrength::Light);
  const auto strong = ResolveRumble(halfHeart, RumbleStrength::Strong);
  Expect(light.Play && strong.Play && light.LowFrequency < strong.LowFrequency &&
             strong.LowFrequency == 40959U && strong.HighFrequency == 20480U &&
             strong.DurationMilliseconds == 190U,
         "Light/Strong motor values");
}

void TestReadSample() {
  using namespace Oot3dNativeGame;
  ByteBus bus;
  constexpr uint32_t play = 0x08100000U, player = 0x08200000U;
  bus.Set<uint32_t>(kOot3dRumbleSaveContext + kOot3dRumbleSaveGameMode, 0U);
  bus.Set<uint16_t>(kOot3dRumbleSaveContext + kOot3dRumbleSaveHealth, 48U);
  bus.Set<uint16_t>(play + kOot3dRumblePlayScene, 0x55U);
  bus.Set<uint32_t>(play + kOot3dRumblePlayPlayerHead, player);
  bus.Set<uint16_t>(player + kOot3dRumbleActorId, 0U);
  bus.Set<uint32_t>(player + kOot3dRumbleActorVelocityY, std::bit_cast<uint32_t>(-9.5F));
  bus.Set<uint16_t>(player + kOot3dRumbleActorBgCheckFlags, 0x0004U);
  Oot3dRumbleSample sample;
  Expect(ReadOot3dRumbleSample(bus, play, &sample) && sample.Gameplay && sample.Health == 48 &&
             sample.Player == player && sample.Scene == 0x55 && sample.VelocityY == -9.5F &&
             sample.BgCheckFlags == 0x0004U,
         "sample fields");
  Expect(bus.Writes == 0U, "reading wrote guest memory");
  Expect(!ReadOot3dRumbleSample(bus, 0U, &sample) && !sample.Gameplay, "no PlayState read as gameplay");
  bus.Set<uint32_t>(kOot3dRumbleSaveContext + kOot3dRumbleSaveGameMode, 1U);
  Expect(!ReadOot3dRumbleSample(bus, play, &sample) && !sample.Gameplay, "title demo read as gameplay");
  bus.Set<uint32_t>(kOot3dRumbleSaveContext + kOot3dRumbleSaveGameMode, 0U);
  bus.Set<uint16_t>(player + kOot3dRumbleActorId, 0x10U);
  Expect(!ReadOot3dRumbleSample(bus, play, &sample), "another actor read as the Player");
}

} // namespace

void RunOot3dRumbleEventTests() {
  TestDamage();
  TestLanding();
  TestPulsesAndStrength();
  TestReadSample();
}
