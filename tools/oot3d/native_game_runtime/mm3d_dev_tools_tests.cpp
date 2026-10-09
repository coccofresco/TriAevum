#include "mm3d_dev_tools.h"
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>

using namespace Oot3dNativeGame;
namespace {
void Check(bool value, const char *message) {
  if (!value) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}
class Fake final : public DevWarpGuest {
public:
  static constexpr uint32_t P = 0x08045B40, A = 0x09000000, Out = 0x0FFFFE00;
  std::map<uint32_t, uint8_t> bytes;
  std::vector<uint32_t> writes;
  uint32_t failOnce = 0;
  unsigned nativeCalls = 0;
  void Map(uint32_t address, uint32_t size) {
    for (uint32_t i = 0; i < size; ++i)
      bytes[address + i] = 0;
  }
  template <class T> void Put(uint32_t address, const T &value) {
    const auto *b = reinterpret_cast<const uint8_t *>(&value);
    for (size_t i = 0; i < sizeof(T); ++i)
      bytes[address + uint32_t(i)] = b[i];
  }
  template <class T> T Get(uint32_t address) {
    T v{};
    Check(Mm3dDevAbi::Read(*this, address, v), "fixture read");
    return v;
  }
  bool Read(uint32_t address, std::span<uint8_t> b) override {
    for (size_t i = 0; i < b.size(); ++i) {
      auto f = bytes.find(address + uint32_t(i));
      if (f == bytes.end())
        return false;
      b[i] = f->second;
    }
    return true;
  }
  bool Write(uint32_t address, std::span<const uint8_t> b) override {
    writes.push_back(address);
    for (size_t i = 0; i < b.size(); ++i) {
      auto f = bytes.find(address + uint32_t(i));
      if (f == bytes.end())
        return false;
      f->second = b[i];
      if (failOnce == address) {
        failOnce = 0;
        return false;
      }
    }
    return true;
  }
  bool RequestTransition(uint32_t, uint16_t, bool *, std::string *) override {
    ++nativeCalls;
    return false;
  }
  Fake() {
    using namespace Mm3dDevAbi;
    Map(P, 0x11030);
    Map(A, 0x200);
    Map(Common, 0x140F8);
    Map(Out, 6);
    Put(ViewSlot, P + 0x1F8);
    Put(P + 0x4A8, P);
    Put(P + 0x200, uint32_t{400});
    Put(P + 0x208, uint32_t{240});
    Put(P + 0x13C, uint8_t{2});
    Put(P + 0x13D, uint8_t{4});
    Put(P + 0x148, int16_t{0x6F});
    Put(P + 0x20DC, A);
    Put(A + 2, uint8_t{2});
    Put(A + 0x24, std::array<float, 3>{1, 2, 3});
    Put(Common + 0x26, uint8_t{4});
    Put(Common + 0x3518, uint32_t{0xD800});
    Put(Common + 0x20, uint16_t{0x4000});
    Put(Common + 0x369E, uint16_t{0xFFEF});
    Put(P + 0xA68, P + 0x408);
    Put(P + 0x408 + 0x154, uint16_t{7});
    Put(P + 0x408 + 0x156, uint16_t{1});
    Put(P + 0x408 + 0x50, std::array<float, 3>{0, 0, 100});
    Put(P + 0x408 + 0x5C, std::array<float, 3>{0, 0, 0});
    for (auto [pc, w] : CameraGuard)
      Put(pc, w);
    Put(0x15F4FC, uint32_t{0xE92D4FF0});
    Put(0x176AFC, uint32_t{0xE92D4FF0});
    Put(0x18E5BC, EntranceTable);
    std::map<unsigned, std::vector<const DevWarpEntrance *>> groups;
    for (const auto &e : kMm3dDevEntrances)
      groups[e.Entrance >> 9].push_back(&e);
    uint32_t list = 0x69F000, row = 0x69E000;
    for (const auto &[group, entries] : groups) {
      uint32_t count = 0;
      for (auto *e : entries)
        count = std::max(count, uint32_t((e->Entrance >> 4) & 31) + 1);
      Put(EntranceTable + 12 * group, count);
      Put(EntranceTable + 12 * group + 4, list);
      Map(list, 4 * count);
      for (auto *e : entries) {
        Put(list + 4 * ((e->Entrance >> 4) & 31), row);
        Put(row, uint32_t(e->Scene) | (uint32_t(e->Spawn) << 8));
        row += 4;
      }
      list += 4 * count;
    }
  }
};
const DevWarpEntrance &Entrance(uint16_t value) {
  for (const auto &e : kMm3dDevEntrances)
    if (e.Entrance == value)
      return e;
  std::abort();
}
} // namespace
int main() {
  using namespace Mm3dDevAbi;
  Mm3dDevTools abi({true, true, false});
  {
    Fake g;
    DevWarpObservation o;
    int16_t busy = 0;
    uint32_t player = 0;
    Check(abi.Observe(g, Fake::P, o, busy, player) && o.Gameplay && busy == -1, "idle zero entrance is not busy");
    g.Put(Fake::P + 0xC529, uint8_t{236});
    g.Put(Fake::P + 0xC85A, uint8_t{3});
    Check(abi.Observe(g, Fake::P, o, busy, player) && !o.Gameplay && busy == 0, "North fade-in is busy");
    Check(!abi.CameraOwnsInput(g), "fade does not own controls");
    g.Put(Fake::P + 0xC529, uint8_t{0});
    g.Put(Fake::P + 0xC85A, uint8_t{0});
    g.Put(Fake::P + 0xA7A, uint16_t{1});
    Check(!abi.CameraOwnsInput(g), "selector subcamera does not own controls");
    g.Put(Fake::P + 0xA7A, uint16_t{0});
    g.Put(Fake::P + 0xAAC, uint16_t{1});
    Check(!abi.CameraOwnsInput(g), "notebook does not own controls");
  }
  {
    Fake g;
    uint32_t value = 0;
    Check(Resolve(g, 0xD600, value) && (value & 255) == 0x6E, "encoded group maps North");
    Check(!Resolve(g, 0xD60F, value), "unknown layers rejected");
    Check(!Resolve(g, 0xE000, value), "group overflow rejected");
    std::string error;
    Check(abi.Apply(g, Fake::P, 0xD600, nullptr, {}, {}, &error), "scene transaction accepted");
    Check(g.Get<uint16_t>(Fake::P + 0xC52E) == 0xD600, "high-bit entrance preserved unsigned");
    Check(g.writes.back() == Fake::P + 0xC529 && g.Get<uint8_t>(Fake::P + 0xC529) == 20, "trigger is published last");
    Check(g.Get<uint16_t>(Common + 0x20) == 0x4000 && g.Get<uint8_t>(Common + 0x26) == 4, "time and form preserved");
    Check(g.nativeCalls == 0, "no OoT guest helper used");
  }
  {
    Fake g;
    uint32_t value = 0;
    constexpr uint32_t header = EntranceTable + 12 * 0x6B;
    const auto count = g.Get<uint32_t>(header), list = g.Get<uint32_t>(header + 4);
    g.Put(header, uint32_t{33});
    Check(!Resolve(g, 0xD600, value), "oversized spawn count rejected");
    g.Put(header, uint32_t{0});
    Check(!Resolve(g, 0xD600, value), "empty group rejected");
    g.Put(header, count);
    g.Put(header + 4, uint32_t{0xFFFFFFFC});
    Check(!Resolve(g, 0xD600, value), "wrapping pointer-list span rejected");
    g.Put(header + 4, list);
    g.Put(list, uint32_t{EntranceTable});
    Check(!Resolve(g, 0xD600, value), "row beyond entrance data rejected");
  }
  {
    Fake g;
    const auto before = g.bytes;
    g.failOnce = Fake::P + 0xC52E;
    std::string error;
    Check(!abi.Apply(g, Fake::P, 0xD600, nullptr, {}, {}, &error), "partial failed write rejected");
    Check(g.bytes == before, "all preparation bytes rolled back");
    Check(std::find(g.writes.begin(), g.writes.end(), Fake::P + 0xC529) == g.writes.end(),
          "no trigger on preparation failure");
    g.writes.clear();
    g.bytes.erase(Fake::P + 0xC535);
    Check(!abi.Apply(g, Fake::P, 0xD600, nullptr, {}, {}, &error) && g.writes.empty(),
          "preflight denial causes zero writes");
  }
  {
    Fake g;
    Mm3dDevTools unproven{Mm3dDevProof{}};
    std::string error;
    Check(!unproven.Apply(g, Fake::P, 0xD600, nullptr, {}, {}, &error) && g.writes.empty(),
          "unproven ABI never writes");
    DevWarpPoint point;
    point.EntranceId = 0xD600;
    Check(!abi.Apply(g, Fake::P, 0xD600, &point, {}, {}, &error) && g.writes.empty(),
          "unproven -8 respawn never writes");
    Check(!abi.Apply(g, Fake::P, 0xD600, nullptr, {}, true, &error) && g.writes.empty(),
          "unproven time mutation rejected");
  }
  {
    Fake g;
    DevFreeCamera camera;
    camera.SetPose({10, 20, 30}, 90, 0);
    Check(!abi.ApplyCamera(g, camera, Fake::Out, Fake::P + 0x5A0), "subcamera not replaced");
    Check(camera.AppliedCount() == 0 && g.writes.empty(), "declined camera has no effects");
    const auto before = g.bytes;
    g.failOnce = Fake::P + 0x22C;
    Check(!abi.ApplyCamera(g, camera, Fake::Out, Fake::P + 0x408) && g.bytes == before,
          "camera partial write rollback");
    Check(camera.AppliedCount() == 0, "failed camera not counted as applied");
    Check(abi.ApplyCamera(g, camera, Fake::Out, Fake::P + 0x408), "main camera applied");
    Check(g.Get<std::array<float, 3>>(Fake::P + 0x220) == std::array<float, 3>{10, 20, 30},
          "view eye uses requested pose");
    Check(g.Get<std::array<float, 3>>(Fake::P + 0x408 + 0x74) == std::array<float, 3>{10, 20, 30},
          "eyeNext follows pose");
    Check(g.Get<int16_t>(Fake::Out + 2) == 16384 && camera.AppliedCount() == 1, "sret yaw and success count");
  }
  {
    Fake g;
    auto title = std::make_shared<Mm3dDevTools>(Mm3dDevProof{true, true, false});
    DevWarpRuntime runtime({}, title);
    runtime.WarpTo(Entrance(0xD800), DevWarpAge::Child, {});
    runtime.Advance(g, 0, 1);
    Check(runtime.Status().Phase == DevWarpPhase::Requested, "shared state machine accepts MM3D");
    g.Put(Fake::P + 0xC529, uint8_t{0});
    runtime.Advance(g, 0, 2);
    Check(runtime.Status().Phase == DevWarpPhase::Requested, "same-scene request cannot arrive before an edge");
    g.Put(Fake::P + 0xC529, uint8_t{20});
    runtime.Advance(g, 0, 3);
    g.Put(Fake::P + 0xC529, uint8_t{0});
    runtime.Advance(g, 0, 4);
    Check(runtime.Status().Phase == DevWarpPhase::Arrived, "arrival after observed transition edge");
  }
  std::cout << "MM3D developer ABI boundary checks passed\n";
}
