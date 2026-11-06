#include "mm3d_scene_environment.h"
#include "triaevum_title_adapter.h"
#include <algorithm>
#include <bit>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using namespace Oot3dNativeGame;
int checks = 0;
void Check(bool condition, const char* message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
// Sparse memory: unmapped reads fail rather than manufacture zero. No guest
// Write methods exist; Put is exclusively fixture setup. Widths are checked.
struct Memory {
  std::map<uint32_t, uint8_t> Bytes;
  std::vector<std::pair<uint32_t, uint8_t>> Reads;
  template<class T> bool Read(uint32_t address, T* value) {
    Reads.emplace_back(address, uint8_t(sizeof(T)));
    *value = 0;
    for (uint32_t i = 0; i < sizeof(T); ++i) {
      const auto it = Bytes.find(address + i);
      if (it == Bytes.end()) return false;
      *value |= T(uint32_t(it->second) << (i * 8U));
    }
    return true;
  }
  bool Read8(uint32_t a, uint8_t* v) { return Read(a, v); }
  bool Read16(uint32_t a, uint16_t* v) { return Read(a, v); }
  bool Read32(uint32_t a, uint32_t* v) { return Read(a, v); }
  void Put(uint32_t address, uint32_t value, uint32_t width = 4) {
    for (uint32_t i = 0; i < width; ++i) Bytes[address+i] = uint8_t(value >> (i*8U));
  }
  void Float(uint32_t address, float value) { Put(address, std::bit_cast<uint32_t>(value)); }
};
constexpr uint32_t play = 0x10000000U, player = 0x10020000U;
Memory Fixture() {
  Memory m;
  // Independent literal donor-ABI fixture, with decoys at old OoT offsets.
  m.Put(0x00656B5CU, play + 0x1F8U);
  m.Put(play + 0x13DU, 4, 1);
  m.Put(play + 0x4A8U, play);
  m.Put(play + 0x148U, 0x2A, 2);
  m.Put(play + 0x138U, 100);
  m.Put(0x007751E4U, 1, 1);
  m.Put(0x007751E5U, 0xFF, 1); // Read32 instead of Read8 would not be boolean.
  m.Put(0x007751ECU, 2);
  m.Put(0x007751F8U, 0xD234, 2);
  m.Put(play + 0x20DCU, player);
  m.Put(play + 0x20D8U, 0x12345678U); // list start is not its head pointer
  m.Put(player, 0, 2);
  m.Put(player + 2U, 2, 1);
  m.Put(player + 0x140U, 0x00123450U);
  m.Put(player + 0x144U, 0); // first person/temporarily hidden still has a pose
  m.Float(player + 0x24U, 10.0F); m.Float(player + 0x28U, 20.0F); m.Float(player + 0x2CU, 30.0F);
  m.Put(player + 0xC2U, 0x4000, 2);
  m.Put(player + 0xBEU, 0xC000, 2);
  m.Put(play + 0x104U, 0x55, 2);
  m.Put(0x00587968U, 0);
  return m;
}
bool ReadAt(const Memory& m, uint32_t address, uint8_t width) {
  return std::find(m.Reads.begin(), m.Reads.end(), std::pair(address,width)) != m.Reads.end();
}
void TestReadContract() {
  auto m=Fixture();
  const auto c=CaptureMm3dSceneEnvironment(m);
  Check(c.State.has_value(), "valid MM3D fixture rejected");
  const auto& s=*c.State;
  Check(s.SceneId==0x2A && s.ContextIdentity==play && s.GuestFrame==100, "wrong scene/context/frame");
  Check(s.Night==true && s.Day==2 && s.Clock==0xD234, "wrong MM3D clock fields");
  Check(s.Player && s.Player->Position==std::array<float,3>{10,20,30} && s.Player->Yaw==90,
        "wrong player head, position or heading layout");
  Check(ReadAt(m,0x7751E4U,1) && !ReadAt(m,0x7751E4U,4), "night flag must be a byte");
  Check(!ReadAt(m,0x587968U,4) && !ReadAt(m,play+0x104U,2) && !ReadAt(m,player+0xBEU,2),
        "adapter read an OoT field");
  Check(ReadAt(m,play+0x20DCU,4) && !ReadAt(m,play+0x20D8U,4), "list start mistaken for first actor");
  Check(c.ReadCount==m.Reads.size() && c.ReadCount<=c.Reads.size(), "incomplete proof read receipt");
  for (const auto& [raw,expected] : std::array<std::pair<uint16_t,float>,4>{{{0,0},{0x4000,90},{0x8000,-180},{0xC000,-90}}}) {
    m.Put(player+0xC2U,raw,2);
    const auto pose=CaptureMm3dSceneEnvironment(m);
    Check(pose.State && pose.State->Player && pose.State->Player->Yaw==expected, "yaw binary-angle conversion");
  }
  m.Put(0x7751E4U,0,1);
  Check(CaptureMm3dSceneEnvironment(m).State->Night==false, "day byte mistaken for unknown");
  m.Put(0x7751E4U,2,1);
  Check(!CaptureMm3dSceneEnvironment(m).State->Night, "invalid night byte became true");
  m.Bytes.erase(0x7751E4U);
  auto unknown=CaptureMm3dSceneEnvironment(m);
  Check(unknown.State && !unknown.State->Night, "failed night read became day");
  Check(std::any_of(unknown.Reads.begin(),unknown.Reads.begin()+unknown.ReadCount,
        [](const auto& r){return r.Address==0x7751E4U && !r.Available;}), "failed read receipt lost availability");
}
void TestRecordedGameplay() {
  // Actual walk4/game.err.log:146 (host1232), code SHA6eec27b1... .
  // These literals are independent of layout constants/Fixture(). This is
  // a regression receipt for observed daytime behavior, not proof of night.
  struct Read { uint32_t Address, Width, Value; };
  constexpr Read observed[] = {
    {0x00656B5CU, 4U, 0x08045D38U},
    {0x08045C7DU, 1U, 0x00000004U},
    {0x08045FE8U, 4U, 0x08045B40U},
    {0x08045C88U, 2U, 0x0000006FU},
    {0x08045C78U, 4U, 0x000000FAU},
    {0x007751E4U, 1U, 0x00000000U},
    {0x007751ECU, 4U, 0x00000001U},
    {0x007751F8U, 2U, 0x0000420EU},
    {0x08047C1CU, 4U, 0x090A87E0U},
    {0x090A87E0U, 2U, 0x00000000U},
    {0x090A87E2U, 1U, 0x00000002U},
    {0x090A8920U, 4U, 0x001F4E1CU},
    {0x090A8924U, 4U, 0x001F4E0CU},
    {0x090A8804U, 4U, 0xC362CB8EU},
    {0x090A8808U, 4U, 0x42C80000U},
    {0x090A880CU, 4U, 0xC4D56FE9U},
    {0x090A88A2U, 2U, 0x00002E97U},
  };
  Memory m;
  for (const auto& r : observed) m.Put(r.Address,r.Value,r.Width);
  const auto current=CaptureMm3dSceneEnvironment(m);
  Check(current.State && current.State->SceneId==0x6FU &&
        current.State->ContextIdentity==0x08045B40U && current.State->GuestFrame==250U,
        "recorded MM3D scene and owner rejected");
  Check(current.State->Night==false && current.State->Day==1 && current.State->Clock==16910U,
        "recorded daytime fields changed");
  Check(current.State->Player && current.Player==0x090A87E0U &&
        current.State->Player->Position==std::array<float,3>{-226.795135498046875F,100.0F,-1707.4971923828125F} &&
        current.State->Player->Yaw==65.5169677734375F,
        "recorded player or heading changed");
  Check(Mm3dSceneEnvironmentStillCurrent(m,*current.State), "recorded owner failed lifetime check");
  // Next observed native sample, line147 (host1234). The previous facing
  // predicts this displacement; the current angle already reflects turning.
  m.Put(0x090A8804U,0xC35DCA40U); // X=-221.7900390625
  m.Put(0x090A880CU,0xC4D526FAU); // Z=-1705.218017578125
  m.Put(0x090A88A2U,11356U,2);
  m.Put(0x08045C78U,251U);
  const auto next=CaptureMm3dSceneEnvironment(m);
  Check(next.State && next.State->Player && next.State->GuestFrame==251 &&
        next.State->Player->Yaw==62.38037109375F,
        "new native sample retained a stale heading");
  const auto& a=current.State->Player->Position;
  const auto& b=next.State->Player->Position;
  const double direction=std::atan2(double(b[0])-a[0],double(b[2])-a[2])*180.0/3.141592653589793;
  Check(std::abs(direction-current.State->Player->Yaw)<0.001 &&
        std::abs(direction-next.State->Player->Yaw)>3.0,
        "recorded movement axes changed or heading was shifted to prior displacement");
}
void TestRecordedNight() {
  // Actual TRIAEVUM_MM3D_SCENE_TRACE receipt, host frame 0 of
  // sct_day1_night.oot3dsav (South Clock Town, day 1, 20:06), code SHA
  // 6eec27b1..., recorded 2026-11-06 (parity PM0 run ref_auth_sct_day1_night).
  // Literals are independent of layout constants/Fixture().
  struct Read { uint32_t Address, Width, Value; };
  constexpr Read observed[] = {
    {0x00656B5CU, 4U, 0x08045D38U},
    {0x08045C7DU, 1U, 0x00000004U},
    {0x08045FE8U, 4U, 0x08045B40U},
    {0x08045C88U, 2U, 0x0000006FU},
    {0x08045C78U, 4U, 0x000001B5U},
    {0x007751E4U, 1U, 0x00000001U},
    {0x007751ECU, 4U, 0x00000001U},
    {0x007751F8U, 2U, 0x0000D650U},
    {0x08047C1CU, 4U, 0x090A87E0U},
    {0x090A87E0U, 2U, 0x00000000U},
    {0x090A87E2U, 1U, 0x00000002U},
    {0x090A8920U, 4U, 0x001F4E1CU},
    {0x090A8924U, 4U, 0x001F4E0CU},
    {0x090A8804U, 4U, 0xC32C6563U},
    {0x090A8808U, 4U, 0x42C80000U},
    {0x090A880CU, 4U, 0xC4C8602BU},
    {0x090A88A2U, 2U, 0x0000E8F1U},
  };
  Memory m;
  for (const auto& r : observed) m.Put(r.Address,r.Value,r.Width);
  const auto night=CaptureMm3dSceneEnvironment(m);
  Check(night.State && night.State->SceneId==0x6FU && night.State->ContextIdentity==0x08045B40U &&
        night.State->GuestFrame==437U, "recorded night scene and owner rejected");
  Check(night.State->Night==true && night.State->Day==1 && night.State->Clock==0xD650U,
        "recorded night byte, day or clock not read as night");
  Check(night.State->Player.has_value(), "recorded night player rejected");
  Check(Mm3dSceneEnvironmentStillCurrent(m,*night.State), "recorded night owner failed lifetime check");
  // The same sample with the byte at its daytime value is day.
  m.Put(0x007751E4U,0U,1);
  Check(CaptureMm3dSceneEnvironment(m).State->Night==false, "night byte 0 read as night");
}
void TestRejections() {
  for (uint32_t view : {0U,0x0072F5D0U,0x1F0U,0xFFFFFFFCU,play+0x1F9U}) {
    auto m=Fixture();m.Put(0x656B5CU,view);
    Check(!CaptureMm3dSceneEnvironment(m).State, "invalid/default View produced a scene");
    Check(m.Reads.size()==1, "invalid View dereferenced a derived play pointer");
  }
  for (uint32_t type : {0U,3U,6U,255U}) {
    auto m=Fixture();m.Put(play+0x13DU,type,1);
    Check(!CaptureMm3dSceneEnvironment(m).State, "non-Play state accepted");
    Check(!ReadAt(m,0x7751E4U,1), "non-Play state read save data");
  }
  auto m=Fixture();m.Put(play+0x4A8U,play+4U);
  Check(!CaptureMm3dSceneEnvironment(m).State, "camera context did not own View");
  m=Fixture();m.Put(play+0x148U,0xFFFF,2);
  Check(!CaptureMm3dSceneEnvironment(m).State, "invalid signed scene accepted");
  m=Fixture();m.Bytes.erase(play+0x138U);
  Check(!CaptureMm3dSceneEnvironment(m).State, "unreadable frame accepted");
  m=Fixture();m.Put(play+0x20DCU,0);
  Check(CaptureMm3dSceneEnvironment(m).State && !CaptureMm3dSceneEnvironment(m).State->Player,
        "absent player should not discard a valid cutscene scene");
  m=Fixture();m.Put(player,0x18,2);
  Check(!CaptureMm3dSceneEnvironment(m).State->Player, "non-player actor accepted");
  m=Fixture();m.Put(player+2U,1,1);
  Check(!CaptureMm3dSceneEnvironment(m).State->Player, "wrong actor category accepted");
  m=Fixture();m.Put(player+0x140U,0);
  Check(!CaptureMm3dSceneEnvironment(m).State->Player, "dead actor accepted");
  m=Fixture();m.Float(player+0x24U,std::numeric_limits<float>::quiet_NaN());
  Check(!CaptureMm3dSceneEnvironment(m).State->Player, "NaN pose accepted");
  m=Fixture();m.Bytes.erase(player+0xC2U);
  Check(!CaptureMm3dSceneEnvironment(m).State->Player, "missing heading fabricated zero yaw");
}
void TestLifetimeAndStorage() {
  auto m=Fixture();auto state=*CaptureMm3dSceneEnvironment(m).State;
  Check(Mm3dSceneEnvironmentStillCurrent(m,state), "valid context failed lifetime check");
  m.Put(0x656B5CU,0);
  Check(!Mm3dSceneEnvironmentStillCurrent(m,state), "missing View kept menu profile");
  m=Fixture();m.Put(play+0x13DU,6,1);
  Check(!Mm3dSceneEnvironmentStillCurrent(m,state), "menu state kept profile");
  m=Fixture();m.Put(play+0x4A8U,0);
  Check(!Mm3dSceneEnvironmentStillCurrent(m,state), "dead camera owner kept profile");
  NativeSceneEnvironmentContinuity tracker;
  tracker.Observe(state);const auto first=tracker.Epoch();
  ++state.GuestFrame;state.Night=false;tracker.Observe(state);
  Check(tracker.Epoch()==first, "normal clock/frame change broke world continuity");
  ++state.SceneId;tracker.Observe(state);const auto second=tracker.Epoch();
  --state.SceneId;tracker.Observe(state);
  Check(second>first && tracker.Epoch()>second, "A-B-A reused an old world identity");
  const auto third=tracker.Epoch();--state.GuestFrame;tracker.Observe(state);
  Check(tracker.Epoch()>third, "frame rollback did not reset world");
  const auto fourth=tracker.Epoch();tracker.Reset();tracker.Observe(state);
  Check(tracker.Epoch()>fourth, "explicit restore to same context/frame reused epoch");
  tracker.Observe(std::nullopt);const auto absent=tracker.Epoch();tracker.Observe(std::nullopt);
  Check(!tracker.State() && tracker.Epoch()==absent, "absent state was retained or repeatedly reset");
  const auto oot=TitleSceneEnvironmentDirectory("config/settings.json",TitleAdapter::Oot3dEur);
  const auto mm=TitleSceneEnvironmentDirectory("config/settings.json",TitleAdapter::Mm3d);
  Check(oot==std::filesystem::path("config/environments"), "OoT legacy namespace moved");
  Check(mm==std::filesystem::path("config/environments/mm3d") && oot!=mm, "MM3D profiles share OoT namespace");
}
} // namespace
int main() try {
  TestReadContract();TestRecordedGameplay();TestRecordedNight();TestRejections();TestLifetimeAndStorage();
  std::printf("MM3D scene environment guards passed: %d checks\n",checks);
  return 0;
} catch(const std::exception& error) {
  std::fprintf(stderr,"MM3D scene environment guards FAILED: %s\n",error.what());return 1;
}
