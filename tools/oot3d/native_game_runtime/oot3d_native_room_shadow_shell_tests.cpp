#include "oot3d_interior_sun_prebuild.h"
#include "oot3d_native_a32_lighting.h"
#include "oot3d_native_pica_composition.h"
#include "oot3d_native_room_shadow_shell.h"
#include "oot3d_typed_gameplay_bridge.h"
#include "recomp/a32_runtime.h"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <regex>
#include <stdexcept>
#include <thread>

namespace {
using namespace Oot3dNativeGame;
using namespace Fast::Renderer3ds;
void Check(bool ok, const char *what) {
  if (!ok)
    throw std::runtime_error(what);
}
void Put(std::vector<uint8_t> &b, size_t o, uint32_t value, size_t n = 4) {
  for (size_t i = 0; i < n; ++i)
    b.at(o + i) = uint8_t(value >> (i * 8));
}
void Float(std::vector<uint8_t> &b, size_t o, float value) {
  Put(b, o, std::bit_cast<uint32_t>(value));
}
void Magic(std::vector<uint8_t> &b, size_t o, std::string_view value) {
  std::copy(value.begin(), value.end(), b.begin() + o);
}
std::vector<uint8_t> Cmb(uint16_t indexType = 0x1403) {
  std::vector<uint8_t> b(0x800);
  Magic(b, 0, "cmb ");
  Put(b, 4, uint32_t(b.size()));
  Put(b, 8, 6);
  Put(b, 0x20, 8);
  Put(b, 0x24, 0x50);
  Put(b, 0x28, 0x100);
  Put(b, 0x30, 0x280);
  Put(b, 0x38, 0x500);
  Put(b, 0x3c, 0x600);
  Magic(b, 0x50, "skl ");
  Put(b, 0x58, 2);
  for (size_t i = 0; i < 2; ++i) {
    size_t o = 0x60 + i * 0x28;
    Put(b, o, uint32_t(i), 2);
    Put(b, o + 2, i ? 0 : 0xffff, 2);
    Float(b, o + 4, 1);
    Float(b, o + 8, 1);
    Float(b, o + 12, 1);
  }
  Float(b, 0x60 + 28, 10);
  Float(b, 0x88 + 32, 20);
  Magic(b, 0x100, "mats");
  Put(b, 0x108, 1);
  b[0x10c + 4] = 1;
  Magic(b, 0x280, "sklm");
  Put(b, 0x288, 0x20);
  Put(b, 0x28c, 0x40);
  Magic(b, 0x2a0, "mshs");
  Put(b, 0x2a8, 1);
  Magic(b, 0x2c0, "shp ");
  Put(b, 0x2c8, 1);
  Put(b, 0x2d0, 0x20, 2);
  Magic(b, 0x2e0, "sepd");
  Put(b, 0x2e8, 1, 2);
  Put(b, 0x2ea, 1, 2);
  Float(b, 0x2e0 + 0x28, 1);
  Put(b, 0x2e0 + 0x2c, 0x1406, 2);
  Put(b, 0x2e0 + 0x108, 0x120, 2);
  Magic(b, 0x400, "prms");
  Put(b, 0x40e, 1, 2);
  Put(b, 0x410, 0x20);
  Put(b, 0x414, 0x28);
  Put(b, 0x420, 1, 2);
  Magic(b, 0x428, "prm ");
  Put(b, 0x430, 1);
  Put(b, 0x438, indexType, 2);
  Put(b, 0x43c, 3, 2);
  Put(b, 0x43e, 1, 2);
  Magic(b, 0x500, "vatr");
  Put(b, 0x50c, 36);
  Put(b, 0x510, 0x50);
  Float(b, 0x55c, 1);
  Float(b, 0x570, 1);
  b[0x600] = 255; // first index is in 16-bit units even for U8/U32
  const size_t n = indexType == 0x1401 ? 1 : indexType == 0x1403 ? 2 : 4;
  for (size_t i = 0; i < 3; ++i)
    Put(b, 0x602 + i * n, uint32_t(i), n);
  return b;
}
struct Memory : oot3d::recomp::a32::MemoryBus {
  std::vector<uint8_t> Bytes;
  uint32_t Reads = 0;
  uint32_t Base = 0;
  explicit Memory(size_t size) : Bytes(size) {}
  bool Read32(uint32_t at, uint32_t *value) override {
    ++Reads;
    if (at < Base || uint64_t(at - Base) + 4 > Bytes.size())
      return false;
    at -= Base;
    *value = 0;
    for (size_t i = 0; i < 4; ++i)
      *value |= uint32_t(Bytes[at + i]) << (i * 8);
    return true;
  }
  bool Write32(uint32_t, uint32_t) override { return false; }
};
void GroupHeaderTests() {
  Memory m(0x1000);
  constexpr uint32_t room=0x100, scene=0x500;
  Put(m.Bytes,room,0x12); Put(m.Bytes,room+8,0x10); Put(m.Bytes,room+16,0x14);
  Put(m.Bytes,scene,0x11); m.Bytes[scene+4]=1; Put(m.Bytes,scene+8,0x14);
  m.Bytes[room+12]=m.Bytes[room+13]=0xFF;
  const auto classify=[&](uint32_t setup=0) {
    return ClassifyNativeEnvironmentGroup(ReadNativeSunHeader(room,setup,m),ReadNativeSunHeader(scene,setup,m));
  };
  m.Bytes[room+14]=10;
  Check(classify()==SceneEnvironmentGroup::TimeFlows,"authored flowing clock group");
  for (uint8_t speed : {uint8_t(0),uint8_t(255)}) {
    m.Bytes[room+14]=speed;
    Check(classify()==SceneEnvironmentGroup::TimeAtEntry,"0/FF held clock group");
  }
  m.Bytes[room+12]=11; m.Bytes[room+13]=0;
  Check(classify()==SceneEnvironmentGroup::FixedTime,"fixed room hour group");
  m.Bytes[room+5]=1;
  Check(classify()==SceneEnvironmentGroup::Indoor,"interior wins over fixed hour");
  m.Bytes[room+5]=0; m.Bytes[scene+6]=1; m.Bytes[scene+4]=0;
  Check(classify()==SceneEnvironmentGroup::Indoor,"authored no-sky interior group");
  m.Bytes[scene+4]=1; m.Bytes[room+12]=0xFF;
  Check(classify()==SceneEnvironmentGroup::TimeAtEntry,"partial FF does not set time");
  m.Bytes[room+12]=24;
  Check(!classify(),"invalid fixed hour must remain unknown");
  Check(!ClassifyNativeEnvironmentGroup({},ReadNativeSunHeader(scene,0,m)),"unknown room got group");
  Check(!ClassifyNativeEnvironmentGroup(ReadNativeSunHeader(room,0,m),{}),"unknown scene got group");
  // Alternative setup points at an indoor room with its own time command.
  Put(m.Bytes,room,0x0118); Put(m.Bytes,room+4,0x80); Put(m.Bytes,room+0x80,0x100);
  Put(m.Bytes,room+0x100,0x12); m.Bytes[room+0x105]=1; Put(m.Bytes,room+0x108,0x14);
  Check(classify(1)==SceneEnvironmentGroup::Indoor,"group ignored selected setup");
  Put(m.Bytes,room+0x108,0x18); m.Bytes[room+0x109]=1; Put(m.Bytes,room+0x10c,0x80);
  Check(!classify(1),"cyclic selected group header accepted");
  // Capture uses authored selected headers, not mutable env flags, and never
  // falls back to the previous room while the current room is uninstalled.
  Memory live(0x600000);
  constexpr uint32_t play=0x1000, liveRoom=0x8000, liveScene=0x9000;
  Put(live.Bytes,0x588E40,0); Put(live.Bytes,play+0x110,liveScene);
  Put(live.Bytes,play+0x4C30,0); Put(live.Bytes,play+0x4FFC,liveRoom);
  Put(live.Bytes,liveScene,0x11); live.Bytes[liveScene+4]=1; Put(live.Bytes,liveScene+8,0x14);
  Put(live.Bytes,liveRoom,0x12); Put(live.Bytes,liveRoom+8,0x10);
  live.Bytes[liveRoom+12]=live.Bytes[liveRoom+13]=255; live.Bytes[liveRoom+14]=10;
  Put(live.Bytes,liveRoom+16,0x14);
  uint64_t identity=0;
  Check(CaptureNativeEnvironmentGroup(play,live,&identity)==SceneEnvironmentGroup::TimeFlows &&
        identity==(uint64_t(liveScene)<<8),"captured group/resource identity incorrect");
  live.Bytes[play+0x31B0]=1; live.Bytes[play+0x3237]=5;
  Check(CaptureNativeEnvironmentGroup(play,live)==SceneEnvironmentGroup::TimeFlows,"mutable environment flags changed group");
  Put(live.Bytes,play+0x4FFC,0); Put(live.Bytes,play+0x500C,0); Put(live.Bytes,play+0x53D8,liveRoom);
  Check(!CaptureNativeEnvironmentGroup(play,live),"previous room used while current room loads");
  Check(!CaptureNativeEnvironmentGroup(UINT32_MAX,live),"overflowing group capture accepted");

}
void HeaderTests() {
  Memory m(0x1000);
  const uint32_t room = 0x100, scene = 0x500;
  Put(m.Bytes, room, 0x12);
  Put(m.Bytes, room + 8, 0x14);
  Put(m.Bytes, scene, 0x11);
  Put(m.Bytes, scene + 8, 0x14);
  for (auto sun : {0, 1})
    for (auto mode : {0, 1, 2})
      for (auto sky : {0, 1, 4, 5, 6, 8, 0x1d, 99}) {
        m.Bytes[room + 5] = uint8_t(sun);
        m.Bytes[scene + 6] = uint8_t(mode);
        m.Bytes[scene + 4] = uint8_t(sky);
        Check(NativeRoomIsInterior(ReadNativeSunHeader(room, 0, m),
                                   ReadNativeSunHeader(scene, 0, m)) ==
                  (sun || (mode == 1 && sky == 0)),
              "authored sun/sky/mode classification");
      }
  Check(!NativeRoomIsInterior({}, ReadNativeSunHeader(scene, 0, m)),
        "unknown room must be outdoor");
  m.Bytes[room + 5] = 0;
  Check(!NativeRoomIsInterior(ReadNativeSunHeader(room, 0, m), {}),
        "unknown scene must be outdoor");
  Put(m.Bytes, room, 0x0318);
  Put(m.Bytes, room + 4, 0x80);
  Put(m.Bytes, room + 8, 0x12);
  Put(m.Bytes, room + 16, 0x14);
  Put(m.Bytes, room + 0x84, 0x100);
  Put(m.Bytes, room + 0x100, 0x12);
  m.Bytes[room + 0x105] = 1;
  Put(m.Bytes, room + 0x108, 0x14);
  Check(!NativeRoomIsInterior(ReadNativeSunHeader(room, 0, m), {}),
        "base setup classification");
  Check(!NativeRoomIsInterior(ReadNativeSunHeader(room, 1, m), {}),
        "null alternate must use base");
  auto alternate = ReadNativeSunHeader(room, 2, m);
  Check(alternate && alternate->CommandOffset == 0x100 &&
            NativeRoomIsInterior(alternate, {}),
        "selected setup ignored");
  Check(ReadNativeSunHeader(room, 3, m)->CommandOffset == 0x100,
        "setup 3 fallback ignored");
  Check(NativeRoomSunFingerprint(1, 0) != NativeRoomSunFingerprint(1, 0x100),
        "alternate geometry cache alias");
  Put(m.Bytes, room + 0x100, 0x0318);
  Put(m.Bytes, room + 0x104, 0x80);
  Check(!ReadNativeSunHeader(room, 2, m), "alternate cycle accepted");
  Check(!ReadNativeSunHeader(room, 17, m), "unbounded setup accepted");
  auto a = std::vector<std::vector<uint8_t>>{Cmb()}, b = a;
  b[0][0x550] ^= 1;
  Check(NativeRoomSunContentFingerprint(1, a) !=
            NativeRoomSunContentFingerprint(1, b),
        "different CMB content aliases");
}
void SelectedMeshTests() {
  Memory m(0x2400);
  constexpr uint32_t base = 0x100;
  Put(m.Bytes, base, 0x118);
  Put(m.Bytes, base + 4, 0x80);
  Put(m.Bytes, base + 8, 0x0a);
  Put(m.Bytes, base + 12, 0x90);
  Put(m.Bytes, base + 16, 0x14);
  Put(m.Bytes, base + 0x80, 0x40);
  Put(m.Bytes, base + 0x40, 0x0a);
  Put(m.Bytes, base + 0x44, 0xb0);
  Put(m.Bytes, base + 0x48, 0x14);
  m.Bytes[base + 0x91] = 1;
  Put(m.Bytes, base + 0x94, 0xa0);
  Put(m.Bytes, base + 0xa0, 0x400);
  m.Bytes[base + 0xb1] = 1;
  Put(m.Bytes, base + 0xb4, 0xc0);
  Put(m.Bytes, base + 0xc0, 0x1000);
  auto a = Cmb(), b = a;
  Float(b, 0x60 + 28, 30);
  std::copy(a.begin(), a.end(), m.Bytes.begin() + base + 0x400);
  std::copy(b.begin(), b.end(), m.Bytes.begin() + base + 0x1000);
  NativeLoadedRoom room{
      0, NativeRoomFingerprint(std::span(m.Bytes).subspan(base, 128)), base};
  const auto first = CaptureNativeRoomShadowCmbs(
      room, m, NativeRoomShadowSource::File, nullptr, 0);
  const auto second = CaptureNativeRoomShadowCmbs(
      room, m, NativeRoomShadowSource::File, nullptr, 1);
  Check(first && second && first->front() == a && second->front() == b,
        "selected mesh command ignored");
  Put(m.Bytes, base + 0xb4, base + 0xc0);
  const auto installed = CaptureNativeRoomShadowCmbs(
      room, m, NativeRoomShadowSource::Installed, nullptr, 1);
  Check(installed && installed->front() == b,
        "selected installed mesh relocation failed");
}
void OfflineTests() {
  const auto root =
      std::filesystem::temp_directory_path() /
      ("interior-sun-test-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root / "romfs" / "scene");
  const auto write = [](const auto &path, const std::vector<uint8_t> &bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    Check(bool(out), "fixture write failed");
  };
  std::vector<uint8_t> room(0x1000), scene(0x100);
  Put(room, 0, 0x0149535a);
  Put(scene, 0, 0x0149535a);
  Put(room, 0x10, 0x12);
  room[0x15] = 0;
  Put(room, 0x18, 0x0a);
  Put(room, 0x1c, 0x80);
  Put(room, 0x20, 0x10); room[0x24]=room[0x25]=255; room[0x26]=10;
  Put(room, 0x28, 0x14);
  room[0x91] = 1;
  Put(room, 0x94, 0x90);
  Put(room, 0xa0, 0x100);
  const auto cmb = Cmb();
  std::copy(cmb.begin(), cmb.end(), room.begin() + 0x110);
  Put(scene, 0x10, 0x318);
  Put(scene, 0x14, 0x80);
  Put(scene, 0x18, 0x11);
  Put(scene, 0x20, 0x14); // base mode 0: outdoor
  Put(scene, 0x94, 0x40); // setup 2; null setup 3 falls back to 2
  Put(scene, 0x50, 0x11);
  scene[0x56] = 1;
  Put(scene, 0x58, 0x14);
  std::vector<std::pair<std::string, std::vector<uint8_t>>> files{
      {"test_0_info.zsi", room},
      {"test_info.zsi", scene},
      {"test_0_dd_info.zsi", room},
      {"test_dd_info.zsi", scene}};
  for (const auto &[name, bytes] : files)
    write(root / "romfs" / "scene" / name, bytes);
  const auto first = PrebuildInteriorSun(root / "romfs", root / "cache");
  Check(first["rooms"] == 2 && first["built"] == 2 && first["fallback"] == 0,
        "standard/MQ offline inventory failed");
  Check(first["entries"].size() == 8 &&
            first["entries"][0]["interior"] == false &&
            first["entries"][2]["interior"] == true &&
            first["entries"][3]["interior"] == true,
        "offline classification differs");
  const auto groups=ListNativeEnvironmentGroups(root/"romfs");
  Check(groups["entries"].size()==8 && groups["entries"][0]["firstRoomGroup"]=="outdoor-time-flows" &&
        groups["entries"][2]["firstRoomGroup"]=="indoor" && groups["entries"][3]["firstRoomGroup"]=="indoor",
        "offline selected-setup group inventory wrong");
  const auto second = PrebuildInteriorSun(root / "romfs", root / "cache");
  Check(second["built"] == 0 && second["hits"] == 2,
        "offline cache not reused");
  // An exported mask table gives a fresh cache the same bytes, without
  // building the interior; the exterior welds locally. A damaged table fails.
  (void)PrebuildInteriorSun(root / "romfs", root / "export-cache", {},
                            root / "masks.bin");
  const auto assembled = PrebuildInteriorSun(
      root / "romfs", root / "table-cache", root / "masks.bin");
  Check(assembled["built"] == 1 && assembled["precomputed"] == 1 &&
            assembled["precomputed_entries"] == 1 &&
            assembled["entries"][2]["status"] == "precomputed",
        "mask table not applied");
  for (const auto &entry : assembled["entries"]) {
    const auto read = [](const std::filesystem::path &path) {
      std::ifstream in(path, std::ios::binary);
      return std::vector<char>(std::istreambuf_iterator<char>(in), {});
    };
    const auto relative = entry["path"].get<std::string>();
    Check(read(root / "table-cache" / relative) ==
              read(root / "cache" / relative),
          "mask table cache differs from a built cache");
  }
  {
    std::fstream table(root / "masks.bin",
                       std::ios::binary | std::ios::in | std::ios::out);
    table.seekp(16);
    table.put('\x5a');
  }
  bool damaged = false;
  try {
    (void)PrebuildInteriorSun(root / "romfs", root / "damaged-cache",
                              root / "masks.bin");
  } catch (const std::exception &) {
    damaged = true;
  }
  Check(damaged, "damaged mask table accepted");
  // Minimal Level-3 image with root and scene directory. Also wrap it in IVFC.
  std::vector<uint8_t> dirs(60);
  Put(dirs, 20, 0);
  Put(dirs, 24 + 20, 10);
  for (size_t k = 0; k < 5; ++k)
    dirs[48 + k * 2] = uint8_t("scene"[k]);
  std::vector<uint8_t> table, payload;
  for (const auto &[name, bytes] : files) {
    const auto p = table.size();
    table.resize(p + ((32 + name.size() * 2 + 3) & ~size_t(3)));
    Put(table, p, 24);
    Put(table, p + 8, uint32_t(payload.size()));
    Put(table, p + 16, uint32_t(bytes.size()));
    Put(table, p + 28, uint32_t(name.size() * 2));
    for (size_t k = 0; k < name.size(); ++k)
      table[p + 32 + k * 2] = uint8_t(name[k]);
    payload.insert(payload.end(), bytes.begin(), bytes.end());
  }
  std::vector<uint8_t> image(40);
  Put(image, 0, 40);
  Put(image, 12, 40);
  Put(image, 16, uint32_t(dirs.size()));
  Put(image, 28, uint32_t(40 + dirs.size()));
  Put(image, 32, uint32_t(table.size()));
  Put(image, 36, uint32_t(40 + dirs.size() + table.size()));
  image.insert(image.end(), dirs.begin(), dirs.end());
  image.insert(image.end(), table.begin(), table.end());
  image.insert(image.end(), payload.begin(), payload.end());
  for (bool ivfc : {false, true}) {
    auto data = image;
    if (ivfc) {
      data.insert(data.begin(), 0x1000, 0);
      Put(data, 0, 0x43465649);
    }
    write(root / "romfs.bin", data);
    Check(ListNativeEnvironmentGroups(root/"romfs.bin")==groups,"directory/image group inventory differs");
    const auto fromImage =
        PrebuildInteriorSun(root / "romfs.bin", root / "cache");
    Check(fromImage["rooms"] == 2 && fromImage["hits"] == 2 &&
              fromImage["entries"][0]["fingerprint"] ==
                  second["entries"][0]["fingerprint"],
          "RomFS directory/image results differ");
  }
  // Reject corrupt input extents without starting a guest or publishing a
  // receipt.
  Put(image, 36, 0xfffffff0);
  write(root / "bad.bin", image);
  bool rejected = false;
  try {
    (void)PrebuildInteriorSun(root / "bad.bin", root / "cache");
  } catch (const std::exception &) {
    rejected = true;
  }
  Check(rejected, "invalid RomFS bounds accepted");
  std::filesystem::remove_all(root);
}
void UnitTests() {
  for (auto kind : {0x1401, 0x1403, 0x1405}) {
    auto b = Cmb(uint16_t(kind));
    auto mesh = ParseNativeRoomShadowCmb(b);
    Check(mesh && mesh->Complete && mesh->Triangles.size() == 1,
          "rigid indexed triangle missing");
    Check(mesh->Triangles[0].Positions[0] == ShadowVector{10, 20, 0},
          "parent bind chain not applied");
    Check(mesh->Triangles[0].Positions[1] == ShadowVector{11, 20, 0},
          "index start units wrong");
    Float(b, 0x88 + 4, 2);
    Float(b, 0x88 + 12, 4);
    Float(b, 0x88 + 24, 1.57079632679F);
    mesh = ParseNativeRoomShadowCmb(b);
    Check(bool(mesh), "TRS model rejected");
    Check(std::abs(mesh->Triangles[0].Positions[1][1] - 22) < .001F &&
              mesh->Triangles[0].Positions[2][2] == 4,
          "Rz * Ry * Rx * S transform wrong");
  }
  auto b = Cmb();
  Put(b, 0x60 + 2, 1, 2);
  Check(!ParseNativeRoomShadowCmb(b), "bone cycle accepted");
  b = Cmb();
  Put(b, 0x88 + 2, 3, 2);
  Check(!ParseNativeRoomShadowCmb(b), "invalid parent accepted");
  b = Cmb();
  Float(b, 0x550, std::numeric_limits<float>::quiet_NaN());
  Check(!ParseNativeRoomShadowCmb(b), "NaN accepted");
  b = Cmb();
  Put(b, 0x2e0 + 0x24, 4096);
  Check(!ParseNativeRoomShadowCmb(b), "invalid position stream accepted");
  b = Cmb();
  Put(b, 0x43e, 0xffff, 2);
  Check(!ParseNativeRoomShadowCmb(b), "invalid index range accepted");
  b = Cmb();
  Put(b, 0x40c, 1, 2);
  auto mesh = ParseNativeRoomShadowCmb(b);
  Check(mesh && !mesh->Complete && mesh->Triangles.empty(),
        "skinned geometry replaced");
  b = Cmb();
  b[0x10c + 0x130] = 1;
  mesh = ParseNativeRoomShadowCmb(b);
  Check(mesh && mesh->Triangles.empty(), "alpha geometry cached");
  b = Cmb();
  Put(b, 0x10c + 0x138, 1);
  mesh = ParseNativeRoomShadowCmb(b);
  Check(mesh && mesh->Triangles.empty(), "blended geometry cached");
  b = Cmb();
  b[0x110] = 0;
  mesh = ParseNativeRoomShadowCmb(b);
  Check(mesh && !mesh->Triangles[0].CullBack, "two-sided flag lost");
  b = Cmb();
  b.resize(0x700);
  Check(!ParseNativeRoomShadowCmb(b), "truncation accepted");
  // Real room command/table layout, with no guest file length available.
  Memory memory(0x2000);
  const uint32_t base = 0x100;
  Put(memory.Bytes, base, 0x0a);
  Put(memory.Bytes, base + 4, 0x80);
  Put(memory.Bytes, base + 8, 0x12);
  Put(memory.Bytes, base + 16, 0x14);
  memory.Bytes[base + 0x80] = 2;
  memory.Bytes[base + 0x81] = 1;
  Put(memory.Bytes, base + 0x84, 0x90);
  Put(memory.Bytes, base + 0x98, 0x100);
  b = Cmb();
  std::copy(b.begin(), b.end(), memory.Bytes.begin() + base + 0x100);
  NativeLoadedRoom room{
      0, NativeRoomFingerprint(std::span(memory.Bytes).subspan(base, 128)),
      base};
  auto captured =
      CaptureNativeRoomShadowCmbs(room, memory, NativeRoomShadowSource::File);
  Check(captured && captured->size() == 1 && captured->front() == b,
        "bounded room capture failed");
  // The installed ZSI mesh header has an absolute entries pointer, even
  // though its command argument and model-entry offsets stay relative.
  Put(memory.Bytes, base + 0x84, base + 0x90);
  captured = CaptureNativeRoomShadowCmbs(room, memory);
  Check(captured && captured->size() == 1 && captured->front() == b,
        "installed room's relocated entry table was rebased twice");
  Put(memory.Bytes, base + 0x84, base - 4);
  std::string error;
  Check(!CaptureNativeRoomShadowCmbs(
            room, memory, NativeRoomShadowSource::Installed, &error) &&
            !error.empty(),
        "out-of-room relocated pointer accepted or unreported");
  Put(memory.Bytes, base + 0x84, base + 0x90);
  const auto cacheFingerprint = NativeRoomSunContentFingerprint(
      room.Fingerprint, *CaptureNativeRoomShadowCmbs(room, memory));
  NativeRoomShadowShells cache;
  auto reads = memory.Reads;
  Check(!cache.Capture(false, 123, std::span(&room, 1), memory) &&
            memory.Reads == reads,
        "disabled shell cache accessed guest");
  auto scene = cache.Capture(true, 123, std::span(&room, 1), memory);
  Check(scene && scene->Rooms.size() == 1, "room snapshot not published");
  const auto until =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!scene->Ready() && std::chrono::steady_clock::now() < until) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    scene = cache.Capture(true, 123, std::span(&room, 1), memory);
  }
  Check(scene->Ready() && scene->Rooms[0].Fingerprint == cacheFingerprint,
        "worker did not publish mesh");
  reads = memory.Reads;
  Check(cache.Capture(true, 123, std::span(&room, 1), memory) == scene &&
            memory.Reads <= reads + 40,
        "unchanged room recopied or republished");
  Check(!cache.Capture(true, 123, {}, memory),
        "unloaded room snapshot retained");
  // Exercise the real lighting-probe publication boundary as well as the
  // cache: toggling the flag after a room is already loaded must publish it.
  memory.Bytes.resize(0x590000);
  constexpr uint32_t play = 0x1000;
  Put(memory.Bytes, play + 0x4ffc, base);
  memory.Bytes[play + 0x500c] = 0xff;
  NativeA32LightingProbe lighting;
  lighting.SetStaticEmitters({});
  lighting.CaptureSceneLights(play, memory);
  Check(!lighting.CaptureRenderer(0, memory).RoomShadows,
        "disabled probe published shells");
  lighting.SetRoomShadowShellsEnabled(true);
  auto publication = lighting.CaptureRenderer(0, memory);
  const auto readyUntil =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  do {
    lighting.CaptureSceneLights(play, memory);
    publication = lighting.CaptureRenderer(0, memory);
    if (publication.RoomShadows && publication.RoomShadows->Ready())
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  } while (std::chrono::steady_clock::now() < readyUntil);
  Check(publication.RoomShadows && publication.RoomShadows->Ready() &&
            publication.RoomShadows->Rooms.front().Fingerprint ==
                cacheFingerprint,
        "enabled lighting probe did not publish the installed room");
  // The same room geometry in a different authored scene class cannot reuse
  // its old masks, even when live sun/moon flags disagree with the header.
  constexpr uint32_t sceneHeader = 0xc00;
  Put(memory.Bytes, play + 0x110, sceneHeader);
  Put(memory.Bytes, sceneHeader, 0x11);
  Put(memory.Bytes, sceneHeader + 8, 0x14);
  memory.Bytes[sceneHeader + 6] = 1; // selected scene: no sky + indoor mode
  memory.Bytes[play + 0x31a6] =
      0; // live mutable flag deliberately says otherwise
  const auto classUntil =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  do {
    lighting.CaptureSceneLights(play, memory);
    publication = lighting.CaptureRenderer(0, memory);
    if (publication.RoomShadows && publication.RoomShadows->Ready())
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  } while (std::chrono::steady_clock::now() < classUntil);
  Check(publication.RoomShadows && publication.RoomShadows->Ready() &&
            publication.RoomShadows->Rooms[0].Interior &&
            publication.RoomShadows->Rooms[0].Mesh->Interior,
        "authored class change reused outdoor snapshot");
  memory.Bytes[sceneHeader + 6] = 0;
  lighting.SetRoomShadowShellsEnabled(false);
  lighting.CaptureSceneLights(play, memory);
  Check(!lighting.CaptureRenderer(0, memory).RoomShadows,
        "disabled probe retained shell publication");
  // Exercise the gameplay hook -> CMB packet span -> frontend draw boundary.
  // In particular the ready worker snapshot must replace an earlier pending
  // snapshot even when the geometry and command-list addresses stay fixed.
  Oot3dNativePicaCompositionTracker tracker;
  tracker.SetRoomShadowShellsEnabled(true);
  constexpr uint32_t packet = 0xb00, commands = 0x2000;
  Put(memory.Bytes, packet + 0x10, 8);
  Put(memory.Bytes, 0x54cc4c, commands);
  const auto captureDraw = [&]() {
    oot3d::recomp::a32::GuestState state{};
    state.r[0] = play;
    tracker.ObserveBlockEntry(kOot3dGameplayDrawEntry, state, memory);
    state.r[0] = 0;
    state.r[1] = 0;
    state.r[14] = 0x002FADDC;
    tracker.ObserveBlockEntry(kOot3dCmbRendererSubmitDrawHandleEntry, state,
                              memory);
    state.r[0] = packet;
    tracker.ObserveBlockEntry(kOot3dMeshCommandPacketSubmitEntry, state,
                              memory);
    tracker.ObserveBlockEntry(0x002FADDC, state, memory);
    std::vector<Oot3dPicaCommandListCompositionSpan> spans;
    Check(tracker.TakeCommandListCompositionSpans(commands, 8, spans) &&
              spans.size() == 1,
          "CMB packet lost its command span");
    auto frontend = std::make_unique<Oot3dNativePicaFrontend>();
    Check(frontend->SetNextCommandListCompositionSpans(commands, 8, spans),
          "frontend rejected room span");
    Oot3dGspCommandPacket submit{};
    submit.Control = 1;
    submit.Parameters[0] = commands;
    submit.Parameters[1] = 8;
    const std::array<uint32_t, 2> words{1, 0x000f022e};
    Check(frontend->SubmitGspCommand(submit, words) &&
              frontend->PendingDrawPackets().size() == 1,
          "frontend lost room draw");
    const auto &draw = frontend->PendingDrawPackets().front();
    Check(draw.CompositionDomain == Oot3dPicaCompositionDomain::Scene &&
              draw.Composition.Layer ==
                  Oot3dPicaCompositionLayer::OpaqueWorld &&
              draw.Composition.Provenance ==
                  Oot3dPicaCompositionProvenance::NativeCmbDrawPass &&
              !draw.Composition.NativeActorGeometry &&
              draw.LightBinding.RoomShadows ==
                  spans.front().LightBinding.RoomShadows,
          "room ownership or shell snapshot lost between native hook and "
          "frontend");
    return draw.LightBinding.RoomShadows;
  };
  std::shared_ptr<const RoomShadowShellScene> frontendScene;
  const auto frontendReadyUntil =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  do {
    frontendScene = captureDraw();
    if (frontendScene && frontendScene->Ready())
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  } while (std::chrono::steady_clock::now() < frontendReadyUntil);
  Check(frontendScene && frontendScene->Ready() &&
            frontendScene->Rooms.front().Fingerprint == cacheFingerprint,
        "worker readiness never reached frontend draw");
  Check(captureDraw() == frontendScene,
        "unchanged frontend room snapshot was not shared");
  tracker.SetRoomShadowShellsEnabled(false);
  Check(!captureDraw(), "frontend retained a disabled room snapshot");
  Put(memory.Bytes, base + 0x98, 0xffffff00);
  Check(!CaptureNativeRoomShadowCmbs(room, memory),
        "unbounded room pointer accepted");
}
// Optional read-only ROM validation; never starts the title or writes assets.
void RomCheck(const std::filesystem::path &sceneDirectory) {
  size_t rooms = 0, models = 0, triangles = 0, incomplete = 0;
  for (const auto &file : std::filesystem::directory_iterator(sceneDirectory)) {
    const auto name = file.path().filename().string();
    if (!name.ends_with("_info.zsi"))
      continue;
    std::ifstream in(file.path(), std::ios::binary | std::ios::ate);
    const auto length = in.tellg();
    if (length < 0)
      continue;
    Memory memory(size_t(length) + 0x1000);
    in.seekg(0);
    in.read(reinterpret_cast<char *>(memory.Bytes.data() + 0x100), length);
    if (!in)
      throw std::runtime_error("ROM read failed");
    const uint32_t base = 0x110;
    NativeLoadedRoom room{
        0, NativeRoomFingerprint(std::span(memory.Bytes).subspan(base, 128)),
        base};
    // Scene (as opposed to room) files have no mesh command.
    auto captured =
        CaptureNativeRoomShadowCmbs(room, memory, NativeRoomShadowSource::File);
    if (!captured)
      continue;
    ++rooms;
    for (const auto &bytes : *captured) {
      auto geometry = ParseNativeRoomShadowCmb(bytes);
      if (!geometry)
        throw std::runtime_error("ROM CMB rejected: " + name);
      ++models;
      triangles += geometry->Triangles.size();
      incomplete += !geometry->Complete;
    }
  }
  Check(rooms > 0, "ROM room validation found no models");
  std::printf(
      "ROM parser: %zu rooms, %zu models, %zu triangles, %zu incomplete\n",
      rooms, models, triangles, incomplete);
}
// Fresh synchronous builds from raw RomFS: excludes I/O, parsing and any disk
// cache; includes welding, voxel closure/flood, sector masks and encoding.
void BenchmarkRoom(const std::filesystem::path &file) {
  std::ifstream in(file, std::ios::binary | std::ios::ate);
  const auto length = in.tellg();
  Check(length >= 0x90 && length <= 64 * 1024 * 1024,
        "invalid benchmark room file");
  Memory memory(size_t(length) + 0x1000);
  in.seekg(0);
  in.read(reinterpret_cast<char *>(memory.Bytes.data() + 0x100), length);
  Check(bool(in), "benchmark room read failed");
  constexpr uint32_t base = 0x110;
  const NativeLoadedRoom room{
      0, NativeRoomFingerprint(std::span(memory.Bytes).subspan(base, 128)),
      base};
  const auto captured =
      CaptureNativeRoomShadowCmbs(room, memory, NativeRoomShadowSource::File);
  Check(bool(captured), "benchmark room capture failed");
  std::vector<RoomShadowTriangle> triangles;
  for (const auto &bytes : *captured) {
    const auto geometry = ParseNativeRoomShadowCmb(bytes);
    Check(geometry && geometry->Complete, "benchmark room geometry incomplete");
    triangles.insert(triangles.end(), geometry->Triangles.begin(),
                     geometry->Triangles.end());
  }
  Check(!triangles.empty(), "benchmark room has no triangles");
  const auto name = file.filename().string();
  std::smatch match;
  std::regex expression(R"(^(.+)_([0-9]+)(_dd)?_info\.zsi$)");
  Check(std::regex_match(name, match, expression), "benchmark room filename");
  std::ifstream sceneIn(file.parent_path() /
                            (match[1].str() + match[3].str() + "_info.zsi"),
                        std::ios::binary);
  std::vector<uint8_t> sceneBytes{std::istreambuf_iterator<char>(sceneIn), {}};
  Memory sceneMemory(sceneBytes.size() + 0x100);
  std::copy(sceneBytes.begin(), sceneBytes.end(),
            sceneMemory.Bytes.begin() + 0x100);
  const bool interior =
      NativeRoomIsInterior(ReadNativeSunHeader(base, 0, memory),
                           ReadNativeSunHeader(base, 0, sceneMemory));
  std::array<double, 3> milliseconds{};
  RoomInteriorSunStats stats;
  uint64_t version = 0;
  size_t marked = 0;
  for (size_t run = 0; run < milliseconds.size(); ++run) {
    const auto start = std::chrono::steady_clock::now();
    const auto mesh = BuildRoomShadowShell(room.Fingerprint, triangles, 512, {},
                                           interior, &stats);
    milliseconds[run] = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - start)
                            .count();
    Check(mesh && mesh->ShellMasks.size() == triangles.size(),
          "benchmark room build failed");
    Check(run == 0 || version == mesh->ContentVersion,
          "benchmark room build is nondeterministic");
    version = mesh->ContentVersion;
    marked = std::count_if(mesh->ShellMasks.begin(), mesh->ShellMasks.end(),
                           [](uint64_t mask) { return mask != 0; });
  }
  std::sort(milliseconds.begin(), milliseconds.end());
  std::printf(
      "room benchmark file=%s fingerprint=%016llx version=%u triangles=%zu "
      "marked=%zu runs=3 "
      "builder_ms_min=%.3f builder_ms_median=%.3f builder_ms_max=%.3f\n",
      file.filename().string().c_str(),
      static_cast<unsigned long long>(room.Fingerprint),
      kRoomShadowShellVersion, triangles.size(), marked, milliseconds.front(),
      milliseconds[1], milliseconds.back());
  std::printf("voxels=%llu peak_grid_bytes=%llu closing=%u interior=%llu "
              "samples=%llu rays=%llu\n",
              stats.Voxels, stats.PeakWorkingBytes, stats.ClosingSteps,
              stats.InteriorVoxels, stats.VoxelSamples, stats.RaySteps);
}
// Developer validation only: read the approved v2 geometry, rebuild using the
// production v3 builder, and compare all 64 bits with the trial result.
void CompareTrial(const std::filesystem::path &file) {
  std::ifstream in(file, std::ios::binary);
  std::vector<uint8_t> b{std::istreambuf_iterator<char>(in), {}};
  auto get = [&](size_t at, size_t n) {
    uint64_t v = 0;
    for (size_t k = 0; k < n; ++k)
      v |= uint64_t(b.at(at + k)) << (8 * k);
    return v;
  };
  Check(get(0, 4) == 0x48535352 && get(4, 4) == 2,
        "expected approved v2 cache");
  const auto fp = get(8, 8), vertices = get(24, 4), count = get(28, 4);
  Check(b.size() == 32 + vertices * 12 + count * 20 + 8, "trial size invalid");
  std::vector<RoomShadowTriangle> triangles(count);
  const size_t indices = 32 + vertices * 12, masks = indices + count * 12;
  for (size_t t = 0; t < count; ++t)
    for (size_t j = 0; j < 3; ++j)
      for (size_t k = 0; k < 3; ++k)
        triangles[t].Positions[j][k] = std::bit_cast<float>(uint32_t(
            get(32 + get(indices + (t * 3 + j) * 4, 4) * 12 + k * 4, 4)));
  RoomInteriorSunStats stats;
  const auto start = std::chrono::steady_clock::now();
  const auto mesh = BuildRoomShadowShell(fp, triangles, 512, {}, true, &stats);
  Check(bool(mesh), "trial build failed");
  const double ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - start)
                        .count();
  size_t differing = 0;
  for (size_t t = 0; t < count; ++t)
    differing += std::popcount(mesh->ShellMasks[t] ^ get(masks + t * 8, 8));
  std::printf(
      "trial=%s triangles=%zu differing_bits=%zu total_bits=%zu agreement=%.6f "
      "ms=%.3f voxels=%llu closing=%u interior=%llu peak_grid_bytes=%llu\n",
      file.filename().string().c_str(), size_t(count), differing,
      size_t(count * 64), 100.0 * (1 - double(differing) / (count * 64)), ms,
      stats.Voxels, stats.ClosingSteps, stats.InteriorVoxels,
      stats.PeakWorkingBytes);
}
// Optional offline capture: little-endian guest base followed by installed
// room bytes. No ROM data is committed; this exercises a real saved room.
void GuestRoomCheck(const std::filesystem::path &file) {
  std::ifstream in(file, std::ios::binary | std::ios::ate);
  const auto size = in.tellg();
  Check(size > 132 && size <= 64 * 1024 * 1024, "invalid guest room fixture");
  Memory memory(size_t(size) - 4);
  in.seekg(0);
  std::array<uint8_t, 4> base{};
  in.read(reinterpret_cast<char *>(base.data()), 4);
  for (size_t i = 0; i < 4; ++i)
    memory.Base |= uint32_t(base[i]) << (8 * i);
  in.read(reinterpret_cast<char *>(memory.Bytes.data()), memory.Bytes.size());
  Check(bool(in), "guest room read failed");
  NativeLoadedRoom room{
      0, NativeRoomFingerprint(std::span(memory.Bytes).first(128)),
      memory.Base};
  auto captured = CaptureNativeRoomShadowCmbs(room, memory);
  Check(bool(captured), "installed savestate room rejected");
  NativeRoomShadowShells cache;
  auto scene = cache.Capture(true, 0, std::span(&room, 1), memory);
  const auto until =
      std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (!scene->Ready() && std::chrono::steady_clock::now() < until) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    scene = cache.Capture(true, 0, std::span(&room, 1), memory);
  }
  Check(scene->Ready() && !scene->Rooms.front().Mesh->Indices.empty(),
        "saved room worker did not produce a mesh");
  std::printf("installed room: fingerprint=%016llx triangles=%zu ready=1\n",
              static_cast<unsigned long long>(room.Fingerprint),
              scene->Rooms.front().Mesh->ShellMasks.size());
}
} // namespace
int main(int argc, char **argv) try {
  if (argc == 3 && std::string_view(argv[1]) == "--compare-trial") {
    CompareTrial(argv[2]);
    return 0;
  }
  HeaderTests();
  GroupHeaderTests();
  SelectedMeshTests();
  UnitTests();
  OfflineTests();
  if (argc == 2)
    RomCheck(argv[1]);
  if (argc == 3 && std::string_view(argv[1]) == "--guest-room")
    GuestRoomCheck(argv[2]);
  if (argc == 3 && std::string_view(argv[1]) == "--benchmark-room")
    BenchmarkRoom(argv[2]);
  std::puts("native room shadow shell tests passed");
  return 0;
} catch (const std::exception &e) {
  std::fprintf(stderr, "FAILED: %s\n", e.what());
  return 1;
}
