#include "oot3d_native_room_shadow_shell.h"
#include "recomp/a32_runtime.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

namespace Oot3dNativeGame {
namespace {
using namespace Fast::Renderer3ds;
constexpr size_t kMaximumCmbBytes = 64U * 1024U * 1024U;
constexpr size_t kMaximumModels = 64;
struct Reader {
  std::span<const uint8_t> Data;
  void Range(size_t at, size_t n) const {
    if (at > Data.size() || n > Data.size() - at)
      throw std::runtime_error("CMB out of bounds");
  }
  uint8_t U8(size_t at) const {
    Range(at, 1);
    return Data[at];
  }
  uint16_t U16(size_t at) const {
    Range(at, 2);
    return uint16_t(Data[at]) | (uint16_t(Data[at + 1]) << 8);
  }
  int16_t S16(size_t at) const { return std::bit_cast<int16_t>(U16(at)); }
  uint32_t U32(size_t at) const {
    return uint32_t(U16(at)) | (uint32_t(U16(at + 2)) << 16);
  }
  float Float(size_t at) const {
    const auto v = std::bit_cast<float>(U32(at));
    if (!std::isfinite(v))
      throw std::runtime_error("CMB non-finite value");
    return v;
  }
  void Magic(size_t at, std::string_view magic) const {
    Range(at, magic.size());
    if (!std::equal(magic.begin(), magic.end(), Data.begin() + at))
      throw std::runtime_error("CMB chunk missing");
  }
  size_t Chunk(size_t at, std::string_view magic) const {
    const size_t p = U32(at);
    Magic(p, magic);
    return p;
  }
};
using Matrix = std::array<std::array<float, 4>, 3>;
std::vector<Matrix> Bones(const Reader &r, size_t skl) {
  const size_t count = r.U32(skl + 8);
  if (count > 4096)
    throw std::runtime_error("CMB too many bones");
  r.Range(skl + 0x10, count * 0x28);
  std::vector<Matrix> local(count), world(count);
  std::vector<int16_t> parents(count);
  std::vector<uint8_t> state(count);
  for (size_t i = 0; i < count; ++i) {
    const size_t at = skl + 0x10 + i * 0x28;
    parents[i] = r.S16(at + 2);
    if (parents[i] >= int(count))
      throw std::runtime_error("CMB bone parent out of range");
    const float sx = r.Float(at + 4), sy = r.Float(at + 8),
                sz = r.Float(at + 12);
    const float rx = r.Float(at + 16), ry = r.Float(at + 20),
                rz = r.Float(at + 24);
    const float cx = std::cos(rx), snx = std::sin(rx), cy = std::cos(ry),
                sny = std::sin(ry), cz = std::cos(rz), snz = std::sin(rz);
    local[i] = {{{cz * cy * sx, (cz * sny * snx - snz * cx) * sy,
                  (cz * sny * cx + snz * snx) * sz, r.Float(at + 28)},
                 {snz * cy * sx, (snz * sny * snx + cz * cx) * sy,
                  (snz * sny * cx - cz * snx) * sz, r.Float(at + 32)},
                 {-sny * sx, cy * snx * sy, cy * cx * sz, r.Float(at + 36)}}};
  }
  for (size_t i = 0; i < count; ++i) {
    std::vector<size_t> chain;
    int current = int(i);
    while (current >= 0 && state[current] != 2) {
      if (state[current] == 1)
        throw std::runtime_error("CMB bone cycle");
      state[current] = 1;
      chain.push_back(size_t(current));
      current = parents[current];
    }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
      const size_t b = *it;
      const int parent = parents[b];
      if (parent < 0)
        world[b] = local[b];
      else
        for (size_t row = 0; row < 3; ++row)
          for (size_t col = 0; col < 4; ++col) {
            float v = col == 3 ? world[parent][row][3] : 0;
            for (size_t k = 0; k < 3; ++k)
              v += world[parent][row][k] * local[b][k][col];
            if (!std::isfinite(v))
              throw std::runtime_error("CMB bone transform overflow");
            world[b][row][col] = v;
          }
      state[b] = 2;
    }
  }
  return world;
}
size_t ScalarSize(uint16_t kind) {
  switch (kind) {
  case 0x1400:
  case 0x1401:
    return 1;
  case 0x1402:
  case 0x1403:
    return 2;
  case 0x1404:
  case 0x1405:
  case 0x1406:
    return 4;
  default:
    throw std::runtime_error("CMB scalar type");
  }
}
float Scalar(const Reader &r, size_t at, uint16_t kind) {
  switch (kind) {
  case 0x1400:
    return float(std::bit_cast<int8_t>(r.U8(at)));
  case 0x1401:
    return float(r.U8(at));
  case 0x1402:
    return float(r.S16(at));
  case 0x1403:
    return float(r.U16(at));
  case 0x1404:
    return float(std::bit_cast<int32_t>(r.U32(at)));
  case 0x1405:
    return float(r.U32(at));
  case 0x1406:
    return r.Float(at);
  default:
    throw std::runtime_error("CMB scalar type");
  }
}
bool ReadGuest(oot3d::recomp::a32::MemoryBus &memory, uint32_t address,
               std::span<uint8_t> bytes) {
  if (uint64_t(address) + bytes.size() > uint64_t(UINT32_MAX) + 1)
    return false;
  size_t i = 0;
  for (; i + 4 <= bytes.size(); i += 4) {
    uint32_t v = 0;
    if (!memory.Read32(address + uint32_t(i), &v))
      return false;
    for (size_t k = 0; k < 4; ++k)
      bytes[i + k] = uint8_t(v >> (k * 8));
  }
  for (; i < bytes.size(); ++i)
    if (!memory.Read8(address + uint32_t(i), &bytes[i]))
      return false;
  return true;
}
} // namespace

std::optional<NativeRoomShadowGeometry>
ParseNativeRoomShadowCmb(std::span<const uint8_t> bytes) {
  try {
    Reader r{bytes};
    r.Magic(0, "cmb ");
    const size_t length = r.U32(4);
    if (length < 0x44 || length > bytes.size() || length > kMaximumCmbBytes ||
        r.U32(8) != 6)
      return {};
    r.Data = bytes.first(length);
    const size_t skl = r.Chunk(0x24, "skl "), mats = r.Chunk(0x28, "mats"),
                 sklm = r.Chunk(0x30, "sklm"), vatr = r.Chunk(0x38, "vatr");
    const size_t indices = r.U32(0x3c), indexBytes = size_t(r.U32(0x20)) * 2;
    r.Range(indices, indexBytes);
    const size_t mshs = sklm + r.U32(sklm + 8), shp = sklm + r.U32(sklm + 12);
    r.Magic(mshs, "mshs");
    r.Magic(shp, "shp ");
    const size_t meshCount = r.U32(mshs + 8), shapeCount = r.U32(shp + 8),
                 materialCount = r.U32(mats + 8);
    if (meshCount > 65536 || shapeCount > 65536 || materialCount > 256)
      return {};
    r.Range(mshs + 0x10, meshCount * 4);
    r.Range(shp + 0x10, shapeCount * 2);
    r.Range(mats + 12, materialCount * 0x15c);
    const auto bones = Bones(r, skl);
    NativeRoomShadowGeometry result;
    const size_t positionLength = r.U32(vatr + 12),
                 positionStart = vatr + r.U32(vatr + 16);
    r.Range(positionStart, positionLength);
    for (size_t m = 0; m < meshCount; ++m) {
      const size_t shape = r.U16(mshs + 0x10 + m * 4),
                   material = r.U8(mshs + 0x12 + m * 4);
      if (shape >= shapeCount || material >= materialCount)
        return {};
      const size_t mat = mats + 12 + material * 0x15c;
      // Alpha-test and transparent materials stay on the native path.
      if (r.U8(mat + 0x130) != 0 || r.U32(mat + 0x138) != 0)
        continue;
      const size_t s = shp + r.U16(shp + 0x10 + shape * 2);
      r.Magic(s, "sepd");
      const size_t primitiveCount = r.U16(s + 8);
      r.Range(s + 0x108, primitiveCount * 2);
      const bool fixed = (r.U16(s + 10) & 1) == 0 ||
                         (r.U16(s + 0x106) & 1) != 0 || r.U16(s + 0x2e) != 0;
      const size_t offset = r.U32(s + 0x24);
      const float scale = r.Float(s + 0x28);
      const uint16_t kind = r.U16(s + 0x2c);
      const ShadowVector constant{r.Float(s + 0x30), r.Float(s + 0x34),
                                  r.Float(s + 0x38)};
      const size_t scalar = ScalarSize(kind), stride = scalar * 3;
      for (size_t p = 0; p < primitiveCount; ++p) {
        const size_t prms = s + r.U16(s + 0x108 + p * 2);
        r.Magic(prms, "prms");
        const size_t prm = prms + r.U32(prms + 0x14);
        r.Magic(prm, "prm ");
        if (!r.U32(prm + 8))
          continue;
        if (r.U16(prms + 12) != 0 || r.U16(prms + 14) != 1) {
          result.Complete = false;
          continue;
        }
        if (r.U32(prm + 12) != 0) {
          result.Complete = false;
          continue;
        }
        const size_t bone = r.U16(prms + r.U32(prms + 0x10));
        if (bone >= bones.size())
          return {};
        const uint16_t indexType = r.U16(prm + 16);
        if (indexType != 0x1401 && indexType != 0x1403 && indexType != 0x1405)
          return {};
        const size_t indexSize = ScalarSize(indexType), count = r.U16(prm + 20),
                     first = size_t(r.U16(prm + 22)) * 2;
        if (first > indexBytes || count * indexSize > indexBytes - first ||
            count % 3 != 0)
          return {};
        for (size_t i = 0; i < count; i += 3) {
          if (result.Triangles.size() == kMaximumRoomShadowTriangles)
            return {};
          RoomShadowTriangle triangle{};
          triangle.CullBack = r.U8(mat + 4) != 0;
          for (size_t corner = 0; corner < 3; ++corner) {
            const size_t at = indices + first + (i + corner) * indexSize;
            const size_t index = indexSize == 1   ? r.U8(at)
                                 : indexSize == 2 ? r.U16(at)
                                                  : r.U32(at);
            ShadowVector position = constant;
            if (!fixed) {
              if (offset > positionLength ||
                  (index + 1) * stride > positionLength - offset)
                return {};
              for (size_t k = 0; k < 3; ++k)
                position[k] =
                    Scalar(r,
                           positionStart + offset + index * stride + k * scalar,
                           kind) *
                    scale;
            }
            for (size_t row = 0; row < 3; ++row) {
              const auto &b = bones[bone][row];
              const float v = b[0] * position[0] + b[1] * position[1] +
                              b[2] * position[2] + b[3];
              if (!std::isfinite(v) || std::abs(v) > 1.e8F)
                return {};
              triangle.Positions[corner][row] = v;
            }
          }
          result.Triangles.push_back(triangle);
        }
      }
    }
    return result;
  } catch (const std::exception &) {
    return {};
  }
}

std::optional<NativeSunHeader>
ReadNativeSunHeader(uint32_t base, uint32_t setup,
                    oot3d::recomp::a32::MemoryBus &memory) {
  if (!base || base > UINT32_MAX - kMaximumCmbBytes || setup > 16)
    return {};
  NativeSunHeader result;
  uint32_t at = base;
  std::set<uint32_t> visited;
  for (uint32_t n = 0; n < 256; ++n, at += 8) {
    if (at < base || at - base > kMaximumCmbBytes - 8 ||
        !visited.insert(at).second)
      return {};
    std::array<uint8_t, 8> command{};
    if (!ReadGuest(memory, at, command))
      return {};
    const Reader r{command};
    const auto arg = r.U32(4);
    switch (command[0]) {
    case 0x14:
      return result;
    case 0x0a:
      result.MeshOffset = arg;
      break;
    case 0x11:
      result.Skybox = command[4];
      result.EnvironmentLightMode = command[6];
      break;
    case 0x12:
      result.SunMoonDisabled = command[5];
      break;
    case 0x10:
      result.TimeHour = command[4];
      result.TimeMinute = command[5];
      result.TimeSpeed = command[6];
      break;
    case 0x18:
      if (setup) {
        // EUR handler 00378ff8: null setup 3 falls back to setup 2;
        // other null alternatives continue the base header.
        if (setup > command[1] || command[1] > 16 ||
            arg > kMaximumCmbBytes - 64)
          return {};
        uint32_t offset = 0;
        if (!memory.Read32(base + arg + (setup - 1) * 4, &offset))
          return {};
        if (!offset && setup == 3 && !memory.Read32(base + arg + 4, &offset))
          return {};
        if (offset) {
          if (offset > kMaximumCmbBytes - 8 || (offset & 7))
            return {};
          at = base + offset - 8;
          result.CommandOffset = offset;
        }
      }
      break;
    default:
      break;
    }
  }
  return {};
}
bool NativeRoomIsInterior(const std::optional<NativeSunHeader> &room,
                          const std::optional<NativeSunHeader> &scene) {
  if (!room || !room->SunMoonDisabled)
    return false;
  if (*room->SunMoonDisabled)
    return true;
  if (!scene || !scene->EnvironmentLightMode)
    return false;
  if (*scene->EnvironmentLightMode == 0)
    return false;
  if (*scene->EnvironmentLightMode != 1 || !scene->Skybox)
    return false;
  if (*scene->Skybox == 0)
    return true;
  // Authored sky family 1,4,5,0x1d,6,8 and unknown values are both outdoor.
  return false;
}
std::optional<SceneEnvironmentGroup> ClassifyNativeEnvironmentGroup(
    const std::optional<NativeSunHeader>& room, const std::optional<NativeSunHeader>& scene) {
  if (!room || !scene || !room->SunMoonDisabled || !scene->EnvironmentLightMode ||
      !scene->Skybox) return {};
  if (*scene->EnvironmentLightMode > 1) return {};
  if (NativeRoomIsInterior(room, scene)) return SceneEnvironmentGroup::Indoor;
  if (!room->TimeHour || !room->TimeMinute || !room->TimeSpeed) return {};
  const auto hour = *room->TimeHour, minute = *room->TimeMinute;
  if ((hour != 0xFF && hour > 23) || (minute != 0xFF && minute > 59)) return {};
  // EUR command 10 handler 00217A5C sets time only when BOTH are non-FF.
  if (hour != 0xFF && minute != 0xFF) return SceneEnvironmentGroup::FixedTime;
  return *room->TimeSpeed == 0 || *room->TimeSpeed == 0xFF
      ? SceneEnvironmentGroup::TimeAtEntry : SceneEnvironmentGroup::TimeFlows;
}
std::optional<SceneEnvironmentGroup> CaptureNativeEnvironmentGroup(
    uint32_t play, oot3d::recomp::a32::MemoryBus& memory, uint64_t* authoredScene) {
  if (authoredScene) *authoredScene = 0;
  uint32_t setup = 0, scene = 0;
  if (!play || play > UINT32_MAX - 0x53DC || !memory.Read32(0x588E40, &setup) ||
      setup > 16 || !memory.Read32(play + 0x110, &scene)) return {};
  if (authoredScene && scene) *authoredScene = (uint64_t(scene) << 8) | setup;
  uint8_t room = 0xFF;
  uint32_t commands = 0;
  // Never latch a previous-room resource while the new current room loads.
  if (!memory.Read8(play + 0x4C30, &room) || room >= 0x80 ||
      !memory.Read32(play + 0x4FFC, &commands) || !commands) return {};
  return ClassifyNativeEnvironmentGroup(ReadNativeSunHeader(commands, setup, memory),
                                       ReadNativeSunHeader(scene, setup, memory));
}
uint64_t NativeRoomSunFingerprint(uint64_t fingerprint, uint32_t offset) {
  if (!offset)
    return fingerprint;
  std::array<uint8_t, 13> key{};
  for (size_t k = 0; k < 8; ++k)
    key[k] = uint8_t(fingerprint >> (8 * k));
  for (size_t k = 0; k < 4; ++k)
    key[8 + k] = uint8_t(offset >> (8 * k));
  key[12] = 3;
  return NativeRoomFingerprint(key);
}

uint64_t
NativeRoomSunContentFingerprint(uint64_t fingerprint,
                                std::span<const std::vector<uint8_t>> cmbs) {
  uint64_t hash = fingerprint;
  for (const auto &bytes : cmbs) {
    for (size_t k = 0; k < 8; ++k)
      hash = (hash ^ uint8_t(uint64_t(bytes.size()) >> (8 * k))) *
             1099511628211ULL;
    for (auto byte : bytes)
      hash = (hash ^ byte) * 1099511628211ULL;
  }
  return hash;
}

std::optional<std::vector<std::vector<uint8_t>>> CaptureNativeRoomShadowCmbs(
    const NativeLoadedRoom &room, oot3d::recomp::a32::MemoryBus &memory,
    NativeRoomShadowSource source, std::string *error, uint32_t setup) {
  if (error)
    *error = "unreadable or malformed room data";
  const uint32_t base = room.CommandsAddress;
  if (base < 0x10 || base > UINT32_MAX - kMaximumCmbBytes)
    return {};
  const auto relative = [&](uint32_t offset, size_t bytes) {
    if (offset > kMaximumCmbBytes || bytes > kMaximumCmbBytes - offset)
      throw std::runtime_error("ZSI relative pointer out of range");
    return base + offset;
  };
  try {
    std::array<uint8_t, 128> fingerprint{};
    if (!ReadGuest(memory, base, fingerprint) ||
        NativeRoomFingerprint(fingerprint) != room.Fingerprint)
      return {};
    const auto header = ReadNativeSunHeader(base, setup, memory);
    if (!header || !header->MeshOffset)
      return {};
    const uint32_t mesh = relative(*header->MeshOffset, 8);
    uint8_t type = 0, count = 0;
    uint32_t entries = 0;
    if (!memory.Read8(mesh, &type) || !memory.Read8(mesh + 1, &count) ||
        !memory.Read32(mesh + 4, &entries) || count == 0 ||
        count > kMaximumModels || (type != 0 && type != 2))
      return {};
    if (source == NativeRoomShadowSource::Installed) {
      if (entries < base)
        throw std::runtime_error("relocated mesh entries precede the room");
      entries = relative(entries - base, size_t(count) * (type == 2 ? 16 : 8));
    } else {
      entries = relative(entries, size_t(count) * (type == 2 ? 16 : 8));
    }
    std::set<uint32_t> addresses;
    for (uint32_t i = 0; i < count; ++i)
      for (uint32_t pass = 0; pass < 2; ++pass) {
        uint32_t offset = 0;
        if (!memory.Read32(entries + i * (type == 2 ? 16 : 8) +
                               (type == 2 ? 8 : 0) + pass * 4,
                           &offset))
          return {};
        if (offset)
          addresses.insert(relative(offset, 12));
      }
    if (addresses.empty() || addresses.size() > kMaximumModels)
      return {};
    std::vector<std::vector<uint8_t>> result;
    size_t total = 0;
    for (auto address : addresses) {
      std::array<uint8_t, 12> header{};
      if (!ReadGuest(memory, address, header))
        return {};
      Reader r{header};
      r.Magic(0, "cmb ");
      const size_t size = r.U32(4);
      if (size < 0x44 || size > kMaximumCmbBytes - total || r.U32(8) != 6)
        return {};
      (void)relative(address - base, size);
      total += size;
      auto &bytes = result.emplace_back(size);
      if (!ReadGuest(memory, address, bytes))
        return {};
    }
    if (error)
      error->clear();
    return result;
  } catch (const std::exception &exception) {
    if (error)
      *error = exception.what();
    return {};
  }
}

struct NativeRoomShadowShells::Impl {
  struct Key {
    uint64_t Fingerprint = 0;
    bool Interior = false;
    uint32_t Commands = 0;
    uint64_t Scope = 0;
    auto operator<=>(const Key &) const = default;
  };
  struct Entry {
    Key RequestKey;
    uint64_t Fingerprint = 0;
    int Room = -1;
    bool Interior = false;
    std::vector<std::vector<uint8_t>> Cmbs;
    std::shared_ptr<const RoomShadowShell> Mesh;
  };
  std::filesystem::path Directory;
  std::mutex Mutex;
  std::condition_variable_any Wake;
  std::map<Key, std::shared_ptr<Entry>> Entries;
  std::deque<std::shared_ptr<Entry>> Queue;
  std::shared_ptr<const RoomShadowShellScene> Published;
  // Declared last: joins before the state it accesses is destroyed.
  std::jthread Worker;
  Impl() {
    if (const char *path = std::getenv("TRIAEVUM_RENDERER_CACHE_DIR"))
      Directory = path;
    Worker = std::jthread([this](std::stop_token stop) {
      while (!stop.stop_requested()) {
        std::shared_ptr<Entry> entry;
        {
          std::unique_lock lock(Mutex);
          Wake.wait(lock, stop, [&] { return !Queue.empty(); });
          if (stop.stop_requested())
            break;
          entry = Queue.front();
          Queue.pop_front();
        }
        try {
          const auto started = std::chrono::steady_clock::now();
          auto mesh = LoadRoomShadowShell(Directory, entry->Fingerprint,
                                          entry->Interior);
          std::fprintf(stderr,
                       "TRIAEVUM_ROOM_SHADOW_SHELL event=cache_%s "
                       "fingerprint=%016llx room=%d persistence=%d\n",
                       mesh ? "hit" : "miss",
                       static_cast<unsigned long long>(entry->Fingerprint),
                       entry->Room, !Directory.empty());
          if (!mesh) {
            std::vector<RoomShadowTriangle> triangles;
            bool complete = true;
            for (const auto &bytes : entry->Cmbs) {
              auto parsed = ParseNativeRoomShadowCmb(bytes);
              if (!parsed || !parsed->Complete ||
                  triangles.size() + parsed->Triangles.size() >
                      kMaximumRoomShadowTriangles) {
                complete = false;
                break;
              }
              triangles.insert(triangles.end(), parsed->Triangles.begin(),
                               parsed->Triangles.end());
            }
            if (complete)
              mesh = BuildRoomShadowShell(entry->Fingerprint, triangles,
                                          kRoomShadowRasterSize, stop,
                                          entry->Interior);
            const double ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - started)
                                  .count();
            std::fprintf(stderr,
                         "TRIAEVUM_ROOM_SHADOW_SHELL event=build_%s "
                         "fingerprint=%016llx room=%d triangles=%zu ms=%.3f\n",
                         mesh                    ? "done"
                         : stop.stop_requested() ? "cancelled"
                                                 : "failed",
                         static_cast<unsigned long long>(entry->Fingerprint),
                         entry->Room,
                         mesh ? mesh->ShellMasks.size() : triangles.size(), ms);
            if (mesh && !stop.stop_requested() && !Directory.empty()) {
              std::string error;
              const bool saved = SaveRoomShadowShell(Directory, *mesh, &error);
              std::fprintf(
                  stderr,
                  "TRIAEVUM_ROOM_SHADOW_SHELL event=cache_write_%s "
                  "fingerprint=%016llx room=%d path=\"%s\" error=\"%s\"\n",
                  saved ? "done" : "failed",
                  static_cast<unsigned long long>(entry->Fingerprint),
                  entry->Room,
                  RoomShadowShellPath(Directory, entry->Fingerprint,
                                      entry->Interior)
                      .string()
                      .c_str(),
                  error.c_str());
            }
          }
          std::lock_guard lock(Mutex);
          entry->Mesh = std::move(mesh);
          entry->Cmbs.clear();
        } catch (const std::exception &exception) {
          std::fprintf(stderr,
                       "TRIAEVUM_ROOM_SHADOW_SHELL event=worker_failed "
                       "fingerprint=%016llx room=%d error=\"%s\"\n",
                       static_cast<unsigned long long>(entry->Fingerprint),
                       entry->Room, exception.what());
          std::lock_guard lock(Mutex);
          entry->Cmbs.clear();
        }
      }
    });
  }
};
NativeRoomShadowShells::NativeRoomShadowShells() = default;
NativeRoomShadowShells::~NativeRoomShadowShells() = default;
std::shared_ptr<const RoomShadowShellScene> NativeRoomShadowShells::Capture(
    bool enabled, uint64_t scope, std::span<const NativeLoadedRoom> rooms,
    oot3d::recomp::a32::MemoryBus &memory, uint32_t playState) {
  if (!enabled || rooms.size() > 2)
    return {};
  if (!mImpl && rooms.empty())
    return {};
  if (!mImpl)
    mImpl = std::make_unique<Impl>();
  uint32_t setup = 0, sceneBase = 0;
  bool setupKnown = !playState;
  if (playState && playState <= UINT32_MAX - 0x110) {
    // EUR selected setup (SaveContext+4e8) and authored scene resource base.
    setupKnown = memory.Read32(0x588e40, &setup) && setup <= 16;
    (void)memory.Read32(playState + 0x110, &sceneBase);
  }
  const auto sceneHeader =
      setupKnown ? ReadNativeSunHeader(sceneBase, setup, memory) : std::nullopt;
  struct Request {
    const NativeLoadedRoom *Room;
    Impl::Key Key;
    bool Valid;
  };
  std::vector<Request> requests;
  for (const auto &room : rooms) {
    const auto header =
        setupKnown ? ReadNativeSunHeader(room.CommandsAddress, setup, memory)
                   : std::nullopt;
    requests.push_back(
        {&room,
         {NativeRoomSunFingerprint(room.Fingerprint,
                                   header ? header->CommandOffset : 0),
          NativeRoomIsInterior(header, sceneHeader), room.CommandsAddress,
          scope},
         header.has_value()});
  }
  // Retain only installed room/setup/class combinations, including transitions.
  {
    std::lock_guard lock(mImpl->Mutex);
    const auto loaded = [&](const Impl::Key &key) {
      return std::any_of(requests.begin(), requests.end(),
                         [&](const auto &r) { return r.Key == key; });
    };
    std::erase_if(mImpl->Entries,
                  [&](const auto &pair) { return !loaded(pair.first); });
    std::erase_if(mImpl->Queue,
                  [&](const auto &e) { return !loaded(e->RequestKey); });
  }
  if (rooms.empty()) {
    mImpl->Published.reset();
    return {};
  }
  std::array<RoomShadowShellInstance, 2> current;
  size_t count = 0;
  std::set<Impl::Key> seen;
  for (const auto &request : requests) {
    const auto &room = *request.Room;
    if (!seen.insert(request.Key).second)
      continue;
    if (!request.Valid) {
      current[count++] = {request.Key.Fingerprint, {}, false};
      continue; // Retry unreadable metadata next frame, without pinning
                // failure.
    }
    std::shared_ptr<Impl::Entry> entry;
    bool added = false;
    {
      std::lock_guard lock(mImpl->Mutex);
      auto [it, inserted] = mImpl->Entries.try_emplace(request.Key);
      if (inserted) {
        it->second = std::make_shared<Impl::Entry>();
        it->second->Fingerprint = request.Key.Fingerprint;
        it->second->RequestKey = request.Key;
        it->second->Interior = request.Key.Interior;
        it->second->Room = room.Number;
      }
      entry = it->second;
      added = inserted;
    }
    if (added) {
      std::fprintf(stderr,
                   "TRIAEVUM_ROOM_SHADOW_SHELL event=request "
                   "fingerprint=%016llx room=%d commands=%08x scope=%llu\n",
                   static_cast<unsigned long long>(room.Fingerprint),
                   room.Number, room.CommandsAddress,
                   static_cast<unsigned long long>(scope));
      std::string error;
      auto cmbs = request.Valid
                      ? CaptureNativeRoomShadowCmbs(
                            room, memory, NativeRoomShadowSource::Installed,
                            &error, setup)
                      : std::nullopt;
      if (cmbs) {
        std::lock_guard lock(mImpl->Mutex);
        entry->Fingerprint =
            NativeRoomSunContentFingerprint(request.Key.Fingerprint, *cmbs);
        entry->Cmbs = std::move(*cmbs);
        mImpl->Queue.push_back(entry);
        mImpl->Wake.notify_one();
      } else {
        std::fprintf(stderr,
                     "TRIAEVUM_ROOM_SHADOW_SHELL event=capture_failed "
                     "fingerprint=%016llx room=%d error=\"%s\"\n",
                     static_cast<unsigned long long>(room.Fingerprint),
                     room.Number, error.c_str());
      }
    }
    {
      std::lock_guard lock(mImpl->Mutex);
      current[count++] = {entry->Fingerprint, entry->Mesh, entry->Interior};
    }
  }
  if (mImpl->Published && mImpl->Published->ScopeIdentity == scope &&
      mImpl->Published->Rooms.size() == count &&
      std::equal(current.begin(), current.begin() + count,
                 mImpl->Published->Rooms.begin()))
    return mImpl->Published;
  auto scene = std::make_shared<RoomShadowShellScene>();
  scene->ScopeIdentity = scope;
  scene->Rooms.assign(current.begin(), current.begin() + count);
  mImpl->Published = std::move(scene);
  return mImpl->Published;
}
} // namespace Oot3dNativeGame
