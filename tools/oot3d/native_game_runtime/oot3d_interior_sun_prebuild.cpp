#include "oot3d_interior_sun_prebuild.h"
#include "oot3d_native_room_shadow_shell.h"
#include "recomp/a32_runtime.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <numeric>
#include <optional>
#include <regex>
#include <stdexcept>
#include <thread>

namespace Oot3dNativeGame {
namespace {
using Bytes = std::vector<uint8_t>;
constexpr uint64_t kMaxFile = 64ULL * 1024 * 1024;
uint32_t U32(std::span<const uint8_t> b, size_t at) {
  if (at > b.size() || b.size() - at < 4)
    throw std::runtime_error("truncated RomFS/ZSI metadata");
  return uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 |
         uint32_t(b[at + 2]) << 16 | uint32_t(b[at + 3]) << 24;
}
uint64_t U64(std::span<const uint8_t> b, size_t at) {
  return U32(b, at) | uint64_t(U32(b, at + 4)) << 32;
}
std::string Name(const Bytes &b, size_t at, size_t n) {
  if (n % 2 || at > b.size() || n > b.size() - at)
    throw std::runtime_error("invalid RomFS name extent");
  std::string s;
  for (size_t i = 0; i < n; i += 2) {
    if (b[at + i + 1] || b[at + i] < 32 || b[at + i] > 126)
      return {};
    s += char(b[at + i]);
  }
  if (s.find_first_of("/\\") != std::string::npos || s == "." || s == "..")
    return {};
  return s;
}
struct Romfs {
  struct File {
    std::filesystem::path Path;
    uint64_t Offset = 0, Size = 0;
  };
  std::map<std::string, File> Files;
  explicit Romfs(const std::filesystem::path &path) {
    if (std::filesystem::is_directory(path)) {
      for (const auto &f : std::filesystem::directory_iterator(path / "scene"))
        if (f.is_regular_file() &&
            f.path().filename().string().ends_with("_info.zsi"))
          Files.emplace(f.path().filename().string(),
                        File{f.path(), 0, f.file_size()});
    } else {
      const auto size = std::filesystem::file_size(path);
      std::ifstream stream(path, std::ios::binary);
      auto read = [&](uint64_t at, uint64_t n) {
        if (at > size || n > size - at || n > 16 * 1024 * 1024)
          throw std::runtime_error("RomFS metadata outside image");
        Bytes b(static_cast<size_t>(n));
        stream.seekg(std::streamoff(at));
        if (!stream.read(reinterpret_cast<char *>(b.data()),
                         std::streamsize(n)))
          throw std::runtime_error("RomFS read failed");
        return b;
      };
      const uint64_t base = U32(read(0, 4), 0) == 0x43465649 ? 0x1000 : 0;
      const auto h = read(base, 40);
      if (U32(h, 0) != 40)
        throw std::runtime_error("invalid RomFS Level-3 header");
      const auto dirs = read(base + U32(h, 12), U32(h, 16)),
                 files = read(base + U32(h, 28), U32(h, 32));
      const uint64_t data = base + U32(h, 36);
      std::vector<uint32_t> sceneDirs;
      for (size_t p = 0; p < dirs.size();) {
        const auto n = U32(dirs, p + 20);
        const auto name = Name(dirs, p + 24, n);
        if (name == "scene" && U32(dirs, p) == 0)
          sceneDirs.push_back(uint32_t(p));
        const uint64_t next = p + ((24ULL + n + 3) & ~3ULL);
        if (next > dirs.size())
          throw std::runtime_error("invalid RomFS directory record");
        p = size_t(next);
      }
      for (size_t p = 0; p < files.size();) {
        const auto n = U32(files, p + 28);
        const auto name = Name(files, p + 32, n);
        const auto offset = U64(files, p + 8), length = U64(files, p + 16);
        if (data > size || offset > size - data ||
            length > size - data - offset)
          throw std::runtime_error("RomFS file outside image");
        if (std::find(sceneDirs.begin(), sceneDirs.end(), U32(files, p)) !=
                sceneDirs.end() &&
            name.ends_with("_info.zsi"))
          if (!Files.emplace(name, File{path, data + offset, length}).second)
            throw std::runtime_error("duplicate RomFS scene file");
        const uint64_t next = p + ((32ULL + n + 3) & ~3ULL);
        if (next > files.size())
          throw std::runtime_error("invalid RomFS file record");
        p = size_t(next);
      }
    }
    if (Files.empty() || Files.size() > 4096)
      throw std::runtime_error("RomFS has no bounded scene inventory");
  }
  Bytes Read(const std::string &name) const {
    const auto found = Files.find(name);
    if (found == Files.end())
      return {};
    const auto &f = found->second;
    if (f.Size < 0x90 || f.Size > kMaxFile)
      throw std::runtime_error("invalid ZSI size: " + name);
    Bytes b(static_cast<size_t>(f.Size));
    std::ifstream in(f.Path, std::ios::binary);
    in.seekg(std::streamoff(f.Offset));
    if (!in.read(reinterpret_cast<char *>(b.data()),
                 std::streamsize(b.size())) ||
        U32(b, 0) != 0x0149535a)
      throw std::runtime_error("invalid ZSI: " + name);
    return b;
  }
};
struct FileMemory : oot3d::recomp::a32::MemoryBus {
  Bytes Data;
  explicit FileMemory(Bytes b) : Data(std::move(b)) {}
  bool Read32(uint32_t at, uint32_t *v) override {
    if (at > Data.size() || Data.size() - at < 4)
      return false;
    *v = U32(Data, at);
    return true;
  }
  bool Read8(uint32_t at, uint8_t *v) override {
    if (at >= Data.size())
      return false;
    *v = Data[at];
    return true;
  }
  bool Write32(uint32_t, uint32_t) override { return false; }
  uint32_t Setups() const {
    uint32_t setups = 0;
    for (size_t at = 0x10; at + 8 <= Data.size() && at < 0x110; at += 8) {
      if (Data[at] == 0x14)
        break;
      if (Data[at] == 0x18)
        setups = std::max(setups, std::min<uint32_t>(Data[at + 1], 16));
    }
    return setups;
  }
};
std::string Hex(uint64_t value) {
  char b[17];
  std::snprintf(b, sizeof(b), "%016llx",
                static_cast<unsigned long long>(value));
  return b;
}
// Interior-sun masks the release computed for identical room content, keyed
// by the content fingerprint; the table carries no geometry. Little-endian:
// "TISP", u32 format 1, u32 criterion version, u32 entry count; per entry u64
// fingerprint (strictly increasing), u32 triangles, u32 kind (0 masks, 1 the
// work/memory bound fallback), u64 first mask; the u64 masks; then a u64
// FNV-1a checksum of everything before it, with the cache's basis.
constexpr uint32_t kTableMagic = 0x50534954;
constexpr uint32_t kTableFormat = 1;
constexpr uint64_t kTableMaxBytes = 256ULL * 1024 * 1024;
constexpr const char *kBoundReason = "interior-sun work or memory bound exceeded";
uint64_t Checksum(std::span<const uint8_t> bytes) {
  uint64_t hash = 1469598103934665603ULL;
  for (const auto byte : bytes) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  return hash;
}
struct MaskTable {
  struct Entry {
    uint32_t Triangles = 0, Kind = 0;
    uint64_t First = 0;
  };
  std::map<uint64_t, Entry> Entries;
  std::vector<uint64_t> Masks;
};
MaskTable LoadMaskTable(const std::filesystem::path &path) {
  MaskTable table;
  if (path.empty())
    return table;
  const auto size = std::filesystem::file_size(path);
  if (size < 24 || size > kTableMaxBytes)
    throw std::runtime_error("invalid interior-sun table size");
  Bytes b(static_cast<size_t>(size));
  std::ifstream in(path, std::ios::binary);
  if (!in.read(reinterpret_cast<char *>(b.data()), std::streamsize(b.size())))
    throw std::runtime_error("interior-sun table read failed");
  if (U32(b, 0) != kTableMagic || U32(b, 4) != kTableFormat ||
      U32(b, 8) != Fast::Renderer3ds::kRoomShadowShellVersion)
    throw std::runtime_error("interior-sun table version differs");
  if (U64(b, b.size() - 8) != Checksum(std::span(b).first(b.size() - 8)))
    throw std::runtime_error("interior-sun table checksum differs");
  const uint64_t count = U32(b, 12), masksAt = 16 + count * 24;
  if (masksAt > b.size() - 8 || (b.size() - 8 - masksAt) % 8)
    throw std::runtime_error("invalid interior-sun table extent");
  uint64_t previous = 0, next = 0;
  for (uint64_t i = 0; i < count; ++i) {
    const size_t at = size_t(16 + i * 24);
    const uint64_t fingerprint = U64(b, at), first = U64(b, at + 16);
    const uint32_t triangles = U32(b, at + 8), kind = U32(b, at + 12);
    if ((i && fingerprint <= previous) || kind > 1 || first != next ||
        (kind == 1 && triangles) ||
        triangles > Fast::Renderer3ds::kMaximumRoomShadowTriangles)
      throw std::runtime_error("invalid interior-sun table entry");
    table.Entries.emplace(fingerprint, MaskTable::Entry{triangles, kind, first});
    previous = fingerprint;
    next += triangles;
  }
  if (next != (b.size() - 8 - masksAt) / 8)
    throw std::runtime_error("invalid interior-sun table mask count");
  table.Masks.resize(size_t(next));
  for (size_t k = 0; k < table.Masks.size(); ++k)
    table.Masks[k] = U64(b, size_t(masksAt + k * 8));
  return table;
}
void PutLe(Bytes &b, uint64_t value, size_t bytes) {
  for (size_t i = 0; i < bytes; ++i)
    b.push_back(uint8_t(value >> (8 * i)));
}
// One room content and class: its geometry, then its outcome.
struct RoomJob {
  uint64_t Fingerprint = 0;
  bool Interior = false;
  bool Complete = true;
  std::vector<Fast::Renderer3ds::RoomShadowTriangle> Triangles;
  std::string Status = "fallback", Error;
  std::shared_ptr<const Fast::Renderer3ds::RoomShadowShell> Mesh;
  Fast::Renderer3ds::RoomInteriorSunStats Stats;
  double Milliseconds = 0;
};
void RunRoomJob(RoomJob &job, const std::filesystem::path &cache,
                const MaskTable &table) {
  using namespace Fast::Renderer3ds;
  const auto started = std::chrono::steady_clock::now();
  if (!job.Complete) {
    job.Error = "unsupported or incomplete CMB geometry";
  } else if ((job.Mesh = LoadRoomShadowShell(cache, job.Fingerprint, job.Interior))) {
    job.Status = "hit";
  } else {
    const auto known = job.Interior ? table.Entries.find(job.Fingerprint)
                                    : table.Entries.end();
    if (known != table.Entries.end() && known->second.Kind == 1) {
      job.Error = kBoundReason;
    } else {
      if (known != table.Entries.end()) {
        job.Mesh = AssembleRoomShadowShell(
            job.Fingerprint, job.Triangles,
            std::span(table.Masks).subspan(size_t(known->second.First),
                                           known->second.Triangles),
            kRoomShadowRasterSize, true);
        if (job.Mesh)
          job.Status = "precomputed";
      }
      if (!job.Mesh) {
        job.Mesh = BuildRoomShadowShell(job.Fingerprint, job.Triangles,
                                        kRoomShadowRasterSize, {},
                                        job.Interior, &job.Stats);
        if (job.Mesh)
          job.Status = "built";
        else
          job.Error = kBoundReason;
      }
      if (job.Mesh) {
        std::string error;
        if (!SaveRoomShadowShell(cache, *job.Mesh, &error))
          throw std::runtime_error("interior-sun cache write failed: " + error);
      }
    }
  }
  job.Triangles = {};
  job.Milliseconds += std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - started)
                          .count();
}
void ExportMaskTable(const std::filesystem::path &path,
                     const std::vector<RoomJob> &jobs) {
  std::map<uint64_t, const RoomJob *> interior;
  for (const auto &job : jobs)
    if (job.Interior && (job.Mesh || job.Error == kBoundReason))
      interior.emplace(job.Fingerprint, &job);
  Bytes b;
  PutLe(b, kTableMagic, 4);
  PutLe(b, kTableFormat, 4);
  PutLe(b, Fast::Renderer3ds::kRoomShadowShellVersion, 4);
  PutLe(b, interior.size(), 4);
  uint64_t first = 0;
  for (const auto &[fingerprint, job] : interior) {
    const uint64_t triangles = job->Mesh ? job->Mesh->ShellMasks.size() : 0;
    PutLe(b, fingerprint, 8);
    PutLe(b, triangles, 4);
    PutLe(b, job->Mesh ? 0 : 1, 4);
    PutLe(b, first, 8);
    first += triangles;
  }
  for (const auto &[fingerprint, job] : interior)
    if (job->Mesh)
      for (const auto mask : job->Mesh->ShellMasks)
        PutLe(b, mask, 8);
  PutLe(b, Checksum(b), 8);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char *>(b.data()), std::streamsize(b.size()));
  out.close();
  if (!out)
    throw std::runtime_error("cannot write interior-sun table");
}
} // namespace
nlohmann::json
PrebuildInteriorSun(const std::filesystem::path &romfs,
                    const std::filesystem::path &cacheDirectory,
                    const std::filesystem::path &maskTable,
                    const std::filesystem::path &exportTable) {
  using namespace Fast::Renderer3ds;
  if (cacheDirectory.empty())
    throw std::runtime_error("renderer cache directory is required");
  const auto cache =
      std::filesystem::absolute(cacheDirectory).lexically_normal();
  const auto table = LoadMaskTable(maskTable);
  Romfs source(romfs);
  const std::regex roomName(R"(^(.+)_([0-9]+)(_dd)?_info\.zsi$)");
  // Every room and setup in inventory order; identical content and class
  // share one job, prepared once.
  struct Visit {
    std::string Room;
    uint32_t Setup = 0;
    bool Interior = false;
    uint64_t Fingerprint = 0;
    std::optional<size_t> Job;
    bool Reused = false;
    std::string Error;
  };
  std::vector<Visit> visits;
  std::vector<RoomJob> jobs;
  std::map<std::pair<uint64_t, bool>, size_t> jobOf;
  size_t roomCount = 0;
  for (const auto &[name, file] : source.Files) {
    std::smatch match;
    if (!std::regex_match(name, match, roomName))
      continue;
    ++roomCount;
    FileMemory room(source.Read(name));
    FileMemory scene(
        source.Read(match[1].str() + match[3].str() + "_info.zsi"));
    const uint32_t setups = std::max(room.Setups(), scene.Setups());
    NativeLoadedRoom loaded{
        0, NativeRoomFingerprint(std::span(room.Data).subspan(0x10, 128)),
        0x10};
    for (uint32_t setup = 0; setup <= setups; ++setup) {
      const auto started = std::chrono::steady_clock::now();
      const auto header = ReadNativeSunHeader(0x10, setup, room);
      Visit visit{name, setup,
                  NativeRoomIsInterior(header,
                                       ReadNativeSunHeader(0x10, setup, scene)),
                  NativeRoomSunFingerprint(loaded.Fingerprint,
                                           header ? header->CommandOffset : 0)};
      auto cmbs = CaptureNativeRoomShadowCmbs(
          loaded, room, NativeRoomShadowSource::File, &visit.Error, setup);
      if (cmbs) {
        visit.Fingerprint =
            NativeRoomSunContentFingerprint(visit.Fingerprint, *cmbs);
        const auto key = std::pair{visit.Fingerprint, visit.Interior};
        if (const auto found = jobOf.find(key); found != jobOf.end()) {
          visit.Job = found->second;
          visit.Reused = true;
        } else {
          RoomJob job{visit.Fingerprint, visit.Interior};
          for (const auto &bytes : *cmbs) {
            const auto geometry = ParseNativeRoomShadowCmb(bytes);
            if (!geometry || !geometry->Complete ||
                job.Triangles.size() + geometry->Triangles.size() >
                    kMaximumRoomShadowTriangles) {
              job.Complete = false;
              job.Triangles = {};
              break;
            }
            job.Triangles.insert(job.Triangles.end(),
                                 geometry->Triangles.begin(),
                                 geometry->Triangles.end());
          }
          job.Milliseconds = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();
          visit.Job = jobs.size();
          jobOf.emplace(key, jobs.size());
          jobs.push_back(std::move(job));
        }
      }
      visits.push_back(std::move(visit));
    }
  }
  if (!roomCount)
    throw std::runtime_error("RomFS contains no room files");
  // Rooms are independent: the largest first, over a bounded worker set
  // (each build's grid stays under its own memory bound).
  std::vector<size_t> order(jobs.size());
  std::iota(order.begin(), order.end(), size_t{0});
  std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    return jobs[a].Triangles.size() > jobs[b].Triangles.size();
  });
  std::atomic<size_t> next{0};
  std::mutex failureMutex;
  std::string failure;
  const unsigned threads = std::min<unsigned>(
      std::clamp(std::thread::hardware_concurrency() / 2U, 1U, 6U),
      unsigned(std::max<size_t>(jobs.size(), 1)));
  std::vector<std::thread> workers;
  for (unsigned t = 0; t < threads; ++t)
    workers.emplace_back([&] {
      for (size_t i; (i = next++) < order.size();) {
        try {
          RunRoomJob(jobs[order[i]], cache, table);
        } catch (const std::exception &exception) {
          std::lock_guard lock(failureMutex);
          if (failure.empty())
            failure = exception.what();
          next = order.size();
        }
      }
    });
  for (auto &worker : workers)
    worker.join();
  if (!failure.empty())
    throw std::runtime_error(failure);
  auto entries = nlohmann::json::array();
  size_t built = 0, hits = 0, precomputed = 0, fallback = 0;
  for (const auto &visit : visits) {
    auto entry = nlohmann::json{{"room", visit.Room},
                                {"setup", visit.Setup},
                                {"interior", visit.Interior},
                                {"status", "fallback"},
                                {"fingerprint", Hex(visit.Fingerprint)}};
    if (!visit.Job) {
      ++fallback;
      entry["reason"] = visit.Error;
      entries.push_back(std::move(entry));
      continue;
    }
    const auto &job = jobs[*visit.Job];
    entry["status"] = job.Status;
    entry["voxels"] = job.Stats.Voxels;
    entry["peak_grid_bytes"] = job.Stats.PeakWorkingBytes;
    entry["interior_voxels"] = job.Stats.InteriorVoxels;
    entry["closing_steps"] = job.Stats.ClosingSteps;
    entry["ms"] = job.Milliseconds;
    if (job.Mesh) {
      entry["path"] = RoomShadowShellPath({}, job.Fingerprint, job.Interior)
                          .generic_string();
      entry["triangles"] = job.Mesh->ShellMasks.size();
    } else {
      entry["reason"] = job.Error;
    }
    if (visit.Reused) {
      entry["reused_in_run"] = true;
    } else {
      built += job.Status == "built";
      hits += job.Status == "hit";
      precomputed += job.Status == "precomputed";
      fallback += !job.Mesh;
      std::fprintf(stderr,
                   "TRIAEVUM_INTERIOR_SUN room=%s setup=%u interior=%d "
                   "status=%s ms=%.3f voxels=%llu\n",
                   visit.Room.c_str(), visit.Setup, visit.Interior,
                   job.Status.c_str(), job.Milliseconds,
                   static_cast<unsigned long long>(job.Stats.Voxels));
    }
    entries.push_back(std::move(entry));
  }
  if (!exportTable.empty())
    ExportMaskTable(exportTable, jobs);
  return {{"format", "triaevum_interior_sun_v1"},
          {"criterion_version", kRoomShadowShellVersion},
          {"game_booted", false},
          {"cache_directory", cache.generic_string()},
          {"rooms", roomCount},
          {"built", built},
          {"hits", hits},
          {"precomputed", precomputed},
          {"precomputed_entries", table.Entries.size()},
          {"threads", threads},
          {"fallback", fallback},
          {"entries", std::move(entries)}};
}
nlohmann::json ListNativeEnvironmentGroups(const std::filesystem::path& romfs) {
  // Names/IDs only, from scene_lights.py's title table. Groups are exclusively
  // decoded from each resource's selected authored command lists below.
  static const std::map<std::string, uint16_t> sceneIds{
      {"ydan", 0},
      {"ddan", 1},
      {"bdan", 2},
      {"bmori1", 3},
      {"hidan", 4},
      {"mizusin", 5},
      {"jyasinzou", 6},
      {"hakadan", 7},
      {"hakadan_ch", 8},
      {"ice_doukutu", 9},
      {"ganon", 10},
      {"men", 11},
      {"gerudoway", 12},
      {"ganontika", 13},
      {"ganon_sonogo", 14},
      {"ganontikasonogo", 15},
      {"takaraya", 16},
      {"ydan_boss", 17},
      {"ddan_boss", 18},
      {"bdan_boss", 19},
      {"moriboss", 20},
      {"fire_bs", 21},
      {"mizusin_boss", 22},
      {"jyasinzou_boss", 23},
      {"hakadan_boss", 24},
      {"ganon_boss", 25},
      {"ganon_final", 26},
      {"entra_day", 27},
      {"entra_night", 28},
      {"entra_ruins", 29},
      {"market_alley", 30},
      {"market_alley_n", 31},
      {"market_day", 32},
      {"market_night", 33},
      {"market_ruins", 34},
      {"shrine", 35},
      {"shrine_n", 36},
      {"shrine_r", 37},
      {"k_home", 38},
      {"k_home3", 39},
      {"k_home4", 40},
      {"k_home5", 41},
      {"kakariko", 42},
      {"kakariko_home3", 43},
      {"shop", 44},
      {"kokiri", 45},
      {"shop_golon", 46},
      {"zoora", 47},
      {"shop_drag", 48},
      {"shop_alley", 49},
      {"shop_night", 50},
      {"shop_face", 51},
      {"link", 52},
      {"kakariko_impa", 53},
      {"stable", 54},
      {"labo", 55},
      {"hylia_labo", 56},
      {"tent", 57},
      {"hut", 58},
      {"daiyousei_izumi", 59},
      {"yousei_izumi_tate", 60},
      {"yousei_izumi_yoko", 61},
      {"kakusiana", 62},
      {"hakaana", 63},
      {"hakaana2", 64},
      {"hakaana_ouke", 65},
      {"syatekijyou", 66},
      {"tokinoma", 67},
      {"kenjyanoma", 68},
      {"hairal_niwa", 69},
      {"hairal_niwa_n", 70},
      {"hiral_demo", 71},
      {"hakasitarelay", 72},
      {"turibori", 73},
      {"nakaniwa", 74},
      {"bowling", 75},
      {"souko", 76},
      {"miharigoya", 77},
      {"mahouya", 78},
      {"ganon_demo", 79},
      {"kinsuta", 80},
      {"spot00", 81},
      {"spot01", 82},
      {"spot02", 83},
      {"spot03", 84},
      {"spot04", 85},
      {"spot05", 86},
      {"spot06", 87},
      {"spot07", 88},
      {"spot08", 89},
      {"spot09", 90},
      {"spot10", 91},
      {"spot11", 92},
      {"spot12", 93},
      {"spot13", 94},
      {"spot15", 95},
      {"spot16", 96},
      {"spot17", 97},
      {"spot18", 98},
      {"spot20", 99},
      {"ganon_tou", 100},
      {"spot99", 107}};
  Romfs source(romfs);
  const std::regex roomName(R"(^(.+)_([0-9]+)(_dd)?_info\.zsi$)");
  std::map<std::pair<std::string,uint32_t>, nlohmann::json> rows;
  for (const auto& [name, unused] : source.Files) {
    std::smatch match;
    if (!std::regex_match(name, match, roomName)) continue;
    const auto stem = match[1].str(), variant = match[3].str();
    FileMemory room(source.Read(name)), scene(source.Read(stem + variant + "_info.zsi"));
    for (uint32_t setup=0; setup<=std::max(room.Setups(),scene.Setups()); ++setup) {
      auto& row = rows[{stem+variant,setup}];
      if (row.is_null()) {
        row = {{"scene",stem},{"variant",variant.empty()?"standard":"master-quest"},{"setup",setup},
               {"policy","first-installed-room-per-visit"},{"rooms",nlohmann::json::array()}};
        if (const auto id=sceneIds.find(stem); id!=sceneIds.end()) row["sceneId"]=id->second;
      }
      const auto header=ReadNativeSunHeader(0x10,setup,room);
      const auto group=ClassifyNativeEnvironmentGroup(header,ReadNativeSunHeader(0x10,setup,scene));
      nlohmann::json item={{"room",std::stoul(match[2].str())},{"file",name},
          {"group",group?nlohmann::json(EnvironmentGroupId(*group)):nlohmann::json(nullptr)}};
      if (header) {
        if (header->TimeHour) item["hour"]=*header->TimeHour;
        if (header->TimeMinute) item["minute"]=*header->TimeMinute;
        if (header->TimeSpeed) item["speed"]=*header->TimeSpeed;
      }
      row["rooms"].push_back(std::move(item));
    }
  }
  auto entries=nlohmann::json::array();
  for (auto& [key,row] : rows) {
    auto& rooms=row["rooms"];
    std::sort(rooms.begin(),rooms.end(),[](const auto& a,const auto& b){ return a.at("room")<b.at("room"); });
    row["firstRoomGroup"]=rooms.front()["group"];
    row["mixed"]=std::any_of(rooms.begin(),rooms.end(),[&](const auto& r){return r.at("group")!=rooms.front()["group"];});
    entries.push_back(std::move(row));
  }
  return {{"schema","triaevum.environment-groups"},{"version",1},{"gameBooted",false},{"entries",entries}};
}
int InteriorSunToolMain(int argc, char **argv) {
  std::filesystem::path romfs, cache, manifest, masks, exportMasks;
  for (int i = 2; i < argc; i += 2) {
    if (i + 1 == argc)
      throw std::runtime_error("missing interior-sun tool argument");
    const std::string_view option = argv[i];
    auto *target = option == "--romfs"                      ? &romfs
                   : option == "--renderer-cache-directory" ? &cache
                   : option == "--manifest"                 ? &manifest
                   : option == "--mask-table"               ? &masks
                   : option == "--export-mask-table"        ? &exportMasks
                                                            : nullptr;
    if (!target || !target->empty())
      throw std::runtime_error(
          "unknown or repeated interior-sun tool argument");
    *target = argv[i + 1];
  }
  if (romfs.empty() || cache.empty() || manifest.empty())
    throw std::runtime_error("usage: --prebuild-interior-sun --romfs PATH "
                             "--renderer-cache-directory PATH --manifest PATH "
                             "[--mask-table PATH] [--export-mask-table PATH]");
  const auto result = PrebuildInteriorSun(romfs, cache, masks, exportMasks);
  std::ofstream out(manifest, std::ios::binary | std::ios::trunc);
  out << result.dump(2) << '\n';
  out.close();
  if (!out)
    throw std::runtime_error("cannot write interior-sun manifest");
  return 0;
}
} // namespace Oot3dNativeGame
