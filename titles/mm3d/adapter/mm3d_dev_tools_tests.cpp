#include "mm3d_dev_tools.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <map>
#include <set>

using namespace Oot3dNativeGame;
using namespace Oot3dNativeGame::Mm3dDevAbi;
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
    Put(Common + 0x36A0, uint16_t{0xFFFF});
    Put(Common + 0x14, int32_t{1});
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
    Check(abi.Apply(g, Fake::P, 0xD600, nullptr, kDevWarpKeepAll, &error), "scene transaction accepted");
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
    Check(!abi.Apply(g, Fake::P, 0xD600, nullptr, kDevWarpKeepAll, &error), "partial failed write rejected");
    Check(g.bytes == before, "all preparation bytes rolled back");
    Check(std::find(g.writes.begin(), g.writes.end(), Fake::P + 0xC529) == g.writes.end(),
          "no trigger on preparation failure");
    g.writes.clear();
    g.bytes.erase(Fake::P + 0xC535);
    Check(!abi.Apply(g, Fake::P, 0xD600, nullptr, kDevWarpKeepAll, &error) && g.writes.empty(),
          "preflight denial causes zero writes");
  }
  {
    Fake g;
    Mm3dDevTools unproven{Mm3dDevProof{}};
    std::string error;
    Check(!unproven.Apply(g, Fake::P, 0xD600, nullptr, kDevWarpKeepAll, &error) && g.writes.empty(),
          "unproven ABI never writes");
    DevWarpPoint point;
    point.EntranceId = 0xD600;
    Check(!abi.Apply(g, Fake::P, 0xD600, &point, kDevWarpKeepAll, &error) && g.writes.empty(),
          "unproven -8 respawn never writes");
    Check(!abi.Apply(g, Fake::P, 0xD600, nullptr, DevWarpSelection{-1, 0x0000, -1, -1}, &error) && g.writes.empty(),
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
  // Arrival options: each gate off refuses with zero writes.
  {
    const Mm3dDevProof all{true, true, false, true, true, true};
    for (size_t axis = 0; axis < 3; ++axis) {
      Fake g;
      Mm3dDevProof proof = all;
      (axis == kMm3dAxisDay ? proof.Day : axis == kMm3dAxisTime ? proof.TimeOfDay : proof.PlayerForm) = false;
      Mm3dDevTools gated(proof);
      DevWarpSelection arrival = kDevWarpKeepAll;
      arrival[axis] = axis == kMm3dAxisDay ? 2 : axis == kMm3dAxisTime ? 0x8000 : FormDeku;
      std::string error;
      Check(!gated.Axes()[axis].Proven, "gated axis is reported unproven");
      Check(!gated.Apply(g, Fake::P, 0xD600, nullptr, arrival, &error) && g.writes.empty() &&
                error.find("awaiting proof") != std::string::npos,
            "an unproven arrival option never writes");
      DevWarpRuntime runtime({}, std::make_shared<Mm3dDevTools>(proof));
      Check(!runtime.ArrivalError(arrival).empty(), "the runtime names the unproven option");
      Check(runtime.ArrivalError(kDevWarpPresetAll).empty() && runtime.ArrivalError(kDevWarpKeepAll).empty(),
            "Keep and Preset never need a proof");
    }
  }
  // Day, time and form join the transaction before the trigger.
  {
    Fake g;
    Mm3dDevTools proven({true, true, false, true, true, true});
    g.Put(Common + 0x08, uint8_t{0x16}); // Goron Mask equipped
    std::string error;
    Check(proven.Apply(g, Fake::P, 0xD600, nullptr, DevWarpSelection{2, 0xEAAB, FormZora, -1}, &error), error.c_str());
    Check(g.Get<int32_t>(Common + 0x14) == 2 && g.Get<uint16_t>(Common + 0x36A0) == 0xEAAB &&
              g.Get<uint8_t>(Common + 0x26) == FormZora && g.Get<uint8_t>(Common + 0x08) == 0,
          "day and form written, time staged in nextDayTime, the mask cleared");
    Check(g.Get<uint16_t>(Common + 0x20) == 0x4000,
          "the scene being left keeps its time (its En_Test4 must not see a dawn)");
    Check(g.writes.back() == Fake::P + 0xC529, "trigger still published last");
    const auto position = [&](uint32_t a) { return std::find(g.writes.begin(), g.writes.end(), a) - g.writes.begin(); };
    Check(position(Common + 0x14) < position(Fake::P + 0xC52E) && position(Common + 0x26) < position(Fake::P + 0xC52E),
          "arrival fields precede the transition fields");
    Check(g.Get<int32_t>(Common + 0x18) == 0, "eventDayCount untouched");
    Fake noon;
    Check(proven.Apply(noon, Fake::P, 0xD600, nullptr, DevWarpSelection{-1, 0x8000, -1, -1}, &error) &&
              noon.Get<uint16_t>(Common + 0x36A0) == 0x8001,
          "noon is staged one unit later (nextDayTime 0x8000 would advance the day)");
  }
  {
    Fake g;
    Mm3dDevTools proven({true, true, false, true, true, true});
    const auto before = g.bytes;
    g.failOnce = Fake::P + 0xC535;
    std::string error;
    Check(!proven.Apply(g, Fake::P, 0xD600, nullptr, DevWarpSelection{3, 0x0000, FormDeku, -1}, &error) &&
              g.bytes == before,
          "arrival fields roll back with the transition");
    g.Put(Common + 0x36A0, uint16_t{0x8000});
    g.writes.clear();
    Check(!proven.Apply(g, Fake::P, 0xD600, nullptr, DevWarpSelection{-1, 0x8000, -1, -1}, &error) && g.writes.empty(),
          "a staged nextDayTime refuses a time write");
    Check(proven.Apply(g, Fake::P, 0xD600, nullptr, DevWarpSelection{-1, -1, FormDeku, -1}, &error),
          "a staged nextDayTime does not block a form-only warp");
  }
  {
    Fake g;
    Mm3dDevTools proven({true, true, false, true, true, true});
    std::string error;
    Check(!proven.Apply(g, Fake::P, 0xD600, nullptr, DevWarpSelection{-1, -1, FormDeity, -1}, &error) &&
              g.writes.empty() && error.find("Fierce Deity") != std::string::npos,
          "Fierce Deity refused outside the boss lairs");
    Check(!proven.Apply(g, Fake::P, 0xD600, nullptr, DevWarpSelection{4, -1, -1, -1}, &error) && g.writes.empty(),
          "day 4 is not offered");
    Check(proven.Apply(g, Fake::P, 0x3800, nullptr, DevWarpSelection{-1, -1, FormDeity, -1}, &error),
          "Fierce Deity accepted in Odolwa's Lair");
  }
  // Arrival checks: day, the time window, the 06:00 night rule, form and mask.
  {
    Fake g;
    Mm3dDevTools proven({true, true, false, true, true, true});
    DevWarpObservation o;
    g.Put(Common + 0x14, int32_t{2});
    g.Put(Common + 0x20, uint16_t{0xEAAB + 0x100});
    g.Put(Common + 0x0C, uint8_t{1});
    g.Put(Common + 0x26, uint8_t{FormZora});
    Check(proven.ArrivalMismatch(g, o, DevWarpSelection{2, 0xEAAB, FormZora, -1}).empty(), "matching arrival");
    Check(proven.ArrivalMismatch(g, o, DevWarpSelection{3, -1, -1, -1}).starts_with("field=day"), "day mismatch");
    Check(proven.ArrivalMismatch(g, o, DevWarpSelection{-1, 0x8000, -1, -1}).starts_with("field=time"),
          "time outside the window");
    g.Put(Common + 0x0C, uint8_t{0});
    Check(proven.ArrivalMismatch(g, o, DevWarpSelection{-1, 0xEAAB, -1, -1}).starts_with("field=night"),
          "22:00 must be night");
    Check(NightAt(0x3FFF) && !NightAt(0x4000) && !NightAt(0xBFFF) && NightAt(0xC000),
          "MM3D night: before 06:00 or from 18:00");
    g.Put(Common + 0x08, uint8_t{0x17});
    Check(proven.ArrivalMismatch(g, o, DevWarpSelection{-1, -1, FormZora, -1}).starts_with("field=form"),
          "a mask left on is a form mismatch");
    Check(proven.ArrivalMismatch(g, o, kDevWarpKeepAll).empty(), "Keep checks nothing");
    o.PlayState = true;
    o.Arrival = {1, 0x8000, FormHuman, -1};
    Check(proven.Describe(o) == "Day 1 12:00, Human", proven.Describe(o).c_str());
  }
  // Clock values agree with the generator's.
  Check(Clock(6, 0) == 0x4001 && Clock(12, 0) == 0x8000 && Clock(22, 0) == 0xEAAB && Clock(0, 0) == 0 &&
            ClockText(0xEAAB) == "22:00" && ClockText(0x4001) == "06:00",
        "clock conversion");
  // Zones: every zone names a catalog row; presets are Keep or a valid value.
  {
    std::set<std::string> ids;
    for (const auto &zone : kMm3dDevZones) {
      Check(ids.insert(zone.Id).second, "zone ids are unique");
      Entrance(zone.Entrance);
      Check(zone.Preset[0] == -1 || (zone.Preset[0] >= 1 && zone.Preset[0] <= 3), "zone day preset");
      Check(zone.Preset[1] >= -1 && zone.Preset[1] <= 0xFFFF, "zone time preset");
      Check(zone.Preset[2] >= -1 && zone.Preset[2] <= 4 && zone.Preset[2] != FormDeity, "zone form preset");
      Check(zone.Preset[3] == -1, "zone has three axes");
    }
    Check(ids.size() >= 60 && ids.count("stock-pot-inn") && ids.count("south-clock-town-clock-tower-door"),
          "curated zones present");
  }
  // TRIAEVUM_DEV_WARP parsing.
  {
    const auto parse = [](std::string_view text) {
      return ParseMm3dDevWarpScript(text, kMm3dDevEntrances, kMm3dDevZones);
    };
    const auto throws = [&](std::string_view text) {
      try {
        parse(text);
      } catch (const std::runtime_error &) {
        return true;
      }
      return false;
    };
    auto plain = parse("mm3d:0xD600");
    Check(plain.Entrance == 0xD600 && plain.Zone.empty() && plain.Arrival == kDevWarpKeepAll,
          "a bare entrance keeps everything");
    auto full = parse("mm3d:0xD210:day=2:time=1200:form=deku");
    Check(full.Arrival == DevWarpSelection{2, 0x8000, FormDeku, -1}, "entrance with options");
    auto zone = parse("mm3d:zone=stock-pot-inn:time=2200");
    Check(zone.Zone == "stock-pot-inn" && zone.Arrival == DevWarpSelection{-2, 0xEAAB, -2, -2},
          "a zone takes its preset except the named options");
    Check(parse("mm3d:zone=odolwas-lair:form=deity").Arrival[2] == FormDeity, "deity alias");
    Check(parse("mm3d:zone=stock-pot-inn:day=keep").Arrival[0] == kDevWarpKeep, "keep overrides a preset");
    for (const char *bad : {"oot:1:2", "mm3d:", "mm3d:0xD60F", "mm3d:0x10000", "mm3d:zone=nowhere", "mm3d:0xD600:day=4",
                            "mm3d:0xD600:time=2500", "mm3d:0xD600:time=22:00", "mm3d:0xD600:form=fairy",
                            "mm3d:0xD600:night=1", "mm3d:12abc"})
      Check(throws(bad), bad);
  }
  // Unproven axes: a zone preset degrades to Keep, an explicit value is refused.
  {
    Fake g;
    DevWarpRuntime runtime({}, std::make_shared<Mm3dDevTools>(Mm3dDevProof{true, true, false}));
    std::string error;
    Check(runtime.Resolve(kDevWarpPresetAll, runtime.FindZone("stock-pot-inn")) == kDevWarpKeepAll,
          "unproven presets resolve to keep");
    Check(runtime.WarpToZone("stock-pot-inn", kDevWarpPresetAll, &error), error.c_str());
    runtime.Advance(g, 0, 1);
    Check(runtime.Status().Phase == DevWarpPhase::Requested && g.Get<uint16_t>(Fake::P + 0xC52E) == 0xBC00 &&
              g.Get<uint16_t>(Common + 0x20) == 0x4000,
          "a zone warps with keep-only arrival before the proofs");
    Fake h;
    DevWarpRuntime explicitTime({}, std::make_shared<Mm3dDevTools>(Mm3dDevProof{true, true, false}));
    Check(explicitTime.WarpToZone("stock-pot-inn", DevWarpSelection{-2, 0x8000, -2, -2}, &error), error.c_str());
    explicitTime.Advance(h, 0, 1);
    Check(explicitTime.Status().Phase == DevWarpPhase::Failed && h.writes.empty(),
          "an explicit unproven value is refused without writes");
  }
  // The shared runtime: zone warp, boot zone once, persistence beside SoH's keys.
  {
    const auto path = std::filesystem::temp_directory_path() / "mm3d_dev_tools_tests.json";
    std::filesystem::remove(path);
    auto title = std::make_shared<Mm3dDevTools>(Mm3dDevProof{true, true, false, true, true, true});
    {
      DevWarpRuntime runtime(path, title);
      std::string error;
      Check(!runtime.WarpToZone("nowhere", kDevWarpPresetAll, &error), "unknown zone refused");
      DevWarpArrivalOptions options;
      options.SelectedZone = "north-clock-town";
      options.BootZone = "north-clock-town";
      options.Arrival = {kDevWarpPreset, 0xEAAB, kDevWarpKeep, kDevWarpPreset};
      runtime.SetArrivalOptions(options);
      Check(runtime.StorageError().empty(), runtime.StorageError().c_str());
    }
    {
      std::ifstream stream(path);
      const auto document = nlohmann::json::parse(stream);
      Check(document.contains("WarpPoints") && document.contains("DebugWarpScreen"), "SoH keys kept");
      Check(document["Zones"]["Selected"] == "north-clock-town" && document["Zones"]["Boot"] == "north-clock-town",
            "zones persisted");
      Check(document["Arrival"]["day"] == kDevWarpPreset && document["Arrival"]["time"] == 0xEAAB &&
                document["Arrival"]["form"] == kDevWarpKeep,
            "arrival persisted by axis id");
    }
    Fake g;
    DevWarpRuntime runtime(path, title);
    Check(runtime.ArrivalOptions().BootZone == "north-clock-town" && runtime.ArrivalOptions().Arrival[1] == 0xEAAB,
          "persisted options reload");
    runtime.Advance(g, 0, 1);
    Check(runtime.Status().Phase == DevWarpPhase::Requested, "boot zone requested at first gameplay");
    Check(g.Get<uint16_t>(Fake::P + 0xC52E) == 0xD620 && g.Get<int32_t>(Common + 0x14) == 1 &&
              g.Get<uint16_t>(Common + 0x36A0) == 0xEAAB && g.Get<uint8_t>(Common + 0x26) == FormHuman,
          "boot zone: preset day, overridden time, kept form");
    // The native transition: busy, then North Clock Town with the player.
    g.Put(Fake::P + 0xC529, uint8_t{0});
    g.Put(Fake::P + 0xC85A, uint8_t{3});
    runtime.Advance(g, 0, 2);
    g.Put(Fake::P + 0xC85A, uint8_t{0});
    g.Put(Common + 0x3518, uint32_t{0xD620});
    g.Put(Fake::P + 0x148, int16_t{0x6E});
    g.Put(Common + 0x0C, uint8_t{1});
    // Play_Init: the staged time becomes the time; nextDayTime counts down.
    g.Put(Common + 0x20, g.Get<uint16_t>(Common + 0x36A0));
    g.Put(Common + 0x36A0, uint16_t{0xFFFD});
    runtime.Advance(g, 0, 3);
    Check(runtime.Status().Phase == DevWarpPhase::Arrived, runtime.Status().Message.c_str());
    for (uint64_t frame = 4; frame < 200; ++frame)
      runtime.Advance(g, 0, frame);
    Check(runtime.Status().Phase == DevWarpPhase::Arrived, "the boot zone is taken once");
    g.Put(Common + 0x36A0, uint16_t{0xFFFF}); // the sfx countdown ended
    runtime.SetBootPoint("anything");
    Check(runtime.ArrivalOptions().BootZone.empty(), "a boot point clears the boot zone");
    // An arrival whose time did not take is reported as failed.
    runtime.WarpTo(Entrance(0xD800), DevWarpSelection{-1, 0xEAAB, -1, -1});
    runtime.Advance(g, 0, 300);
    g.Put(Common + 0x20, uint16_t{0x8000});
    g.Put(Fake::P + 0xC529, uint8_t{0});
    g.Put(Fake::P + 0xC85A, uint8_t{3});
    runtime.Advance(g, 0, 301);
    g.Put(Fake::P + 0xC85A, uint8_t{0});
    g.Put(Common + 0x3518, uint32_t{0xD800});
    g.Put(Fake::P + 0x148, int16_t{0x6F});
    runtime.Advance(g, 0, 302);
    Check(runtime.Status().Phase == DevWarpPhase::Failed &&
              runtime.Status().Message.find("field=time") != std::string::npos,
          runtime.Status().Message.c_str());
    std::filesystem::remove(path);
  }
  {
    // Destinations the live sweep saw fault the title: disabled with the
    // reason, and refused before any write.
    Fake g;
    Mm3dDevTools proven({true, true, false, true, true, true});
    const auto &row = Entrance(0x3A10);
    std::vector<uint32_t> table(65536, UINT32_MAX);
    table[0x3A10] = uint32_t(row.Scene) | (uint32_t(row.Spawn) << 8);
    const auto availability = proven.Check(row, table);
    Check(availability.Known && !availability.Available && availability.Reason.find("svcBreak") != std::string::npos,
          "a refused destination is shown disabled with its fault");
    std::string error;
    Check(!proven.Apply(g, Fake::P, 0x3A10, nullptr, kDevWarpKeepAll, &error) && g.writes.empty() &&
              error.find("faults MM3D") != std::string::npos,
          "a refused destination is refused without writes");
    Check(std::size(kMm3dDevRefused) == 15, "the sweep's fifteen faulting destinations");
  }
  {
    // A boss lair's intro cutscene (no gameplay yet) still counts as arrived
    // at the requested entrance; another entrance or no player does not.
    DevWarpObservation o;
    o.Player = true;
    o.Gameplay = false;
    o.Entrance = 0x6600;
    Check(abi.Arrived(o, 0x6600, false), "arrival during the entrance cutscene");
    Check(!abi.Arrived(o, 0x6610, false), "another spawn is not the arrival");
    o.Player = false;
    Check(!abi.Arrived(o, 0x6600, false), "no player, no arrival");
  }
  std::cout << "MM3D developer ABI boundary checks passed\n";
}
