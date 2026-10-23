#include "fast/oot3d/grass_persistent_placement_cache.h"
#include "fast/oot3d/grass_overlay_mask.h"
#include <algorithm>
#include <atomic>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <unordered_set>
#include <type_traits>

namespace Fast::Oot3d {
namespace {
constexpr uint64_t kMaximumFileBytes = 512ULL * 1024 * 1024;
constexpr uint64_t kMagic = 0x3145484341435247ULL;
// Loads share the directory; writes and eviction (which removes files) are
// exclusive, so a file is never removed while another worker reads it.
std::shared_mutex diskMutex;
struct Hash {
    uint64_t Value = 14695981039346656037ULL;
    // Local non-cryptographic checksum, deliberately word-at-a-time rather
    // than serial byte FNV: large baked streams must be cheaper to verify than
    // to regenerate. memcpy permits unaligned input; the file is ABI/endian tagged.
    void Bytes(const void* data, size_t count) {
        const auto* p = static_cast<const uint8_t*>(data);
        size_t i = 0;
        for (; count - i >= 8; i += 8) {
            uint64_t word;
            std::memcpy(&word, p + i, 8);
            Value = (Value ^ word) * 1099511628211ULL;
        }
        for (; i < count; ++i)
            Value = (Value ^ p[i]) * 1099511628211ULL;
    }
    template <class T> void Add(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        Bytes(&value, sizeof(value));
    }
    template <class T> void Vector(const std::vector<T>& values) {
        Add(uint64_t(values.size()));
        Bytes(values.data(), values.size() * sizeof(T));
    }
};
// Local ABI format: exact payload bits preserve deterministic output. Explicit
// sizes + endian tag + algorithm version reject incompatible builds.
struct Header {
    uint64_t Magic = kMagic;
    uint32_t Version = kGrassPlacementDiskVersion;
    uint32_t Endian = 0x01020304;
    GrassPlacementDiskKey Key;
    uint64_t Bytes = 0, Checksum = 0;
    std::array<uint32_t, 13> Sizes{};
    std::array<uint64_t, 13> Counts{};
    std::array<uint32_t, 3> MidrangeStats{};
};
// Large sequential reads straight into the destination: a stream buffer
// costs about as much as the disk cache read itself for these sizes.
class UnbufferedFile {
  public:
    explicit UnbufferedFile(const std::filesystem::path& path) {
#ifdef _WIN32
        mFile = _wfopen(path.c_str(), L"rb");
#else
        mFile = std::fopen(path.c_str(), "rb");
#endif
        if (mFile != nullptr)
            std::setvbuf(mFile, nullptr, _IONBF, 0);
        mGood = mFile != nullptr;
    }
    ~UnbufferedFile() {
        if (mFile != nullptr)
            std::fclose(mFile);
    }
    UnbufferedFile(const UnbufferedFile&) = delete;
    UnbufferedFile& operator=(const UnbufferedFile&) = delete;
    void Read(void* data, size_t count) {
        if (mGood && count != 0U)
            mGood = std::fread(data, 1, count, mFile) == count;
    }
    [[nodiscard]] bool Good() const noexcept { return mGood; }

  private:
    std::FILE* mFile = nullptr;
    bool mGood = false;
};
template <class P, class F> void Vectors(P& p, F f) {
    f(p.Anchors);
    f(p.CullingAnchors);
    f(p.Clusters);
    f(p.VisibilityClusterOrder);
    f(p.VisibilityNodes);
    f(p.Midrange.Members);
    f(p.Midrange.Groups);
    f(p.Midrange.GroupForRoot);
    f(p.Midrange.GroupOrder);
    f(p.Midrange.Nodes);
    f(p.ShadowPoints);
    f(p.ShadowReferences);
    f(p.ShadowSurfaces);
}
template <class A> bool Finite(const A& a) {
    return std::all_of(a.begin(), a.end(), [](float x) { return std::isfinite(x); });
}
bool Range(uint64_t first, uint64_t count, size_t size) {
    return first <= size && count <= size - first;
}
bool Validate(const GrassWorldPlacement& p) {
    const auto n = p.Anchors.size();
    if (p.CullingAnchors.size() != n || (!p.ShadowReferences.empty() && p.ShadowReferences.size() != n))
        return false;
    for (const auto& a : p.Anchors)
        if (!Finite(a.BaseHeight) || !Finite(a.HalfWidthPhase))
            return false;
    for (const auto& a : p.CullingAnchors)
        if (!Finite(a.Position) || !std::isfinite(a.StableVisibility))
            return false;
    for (const auto& c : p.Clusters)
        if (!Range(c.FirstAnchor, c.AnchorCount, n) || !Finite(c.Center) || !std::isfinite(c.Radius) || c.Radius < 0 ||
            !std::isfinite(c.MaximumBladeHeight) || !std::isfinite(c.MinimumStableVisibility))
            return false;
    if (p.VisibilityClusterOrder.size() != p.Clusters.size())
        return false;
    for (auto i : p.VisibilityClusterOrder)
        if (i >= p.Clusters.size())
            return false;
    for (size_t i = 0; i < p.VisibilityNodes.size(); ++i) {
        const auto& v = p.VisibilityNodes[i];
        if (v.Escape <= i || v.Escape > p.VisibilityNodes.size() ||
            !Range(v.FirstCluster, v.ClusterCount, p.VisibilityClusterOrder.size()) || !Finite(v.Center) ||
            !std::isfinite(v.Radius) || v.Radius < 0 || !std::isfinite(v.MaximumBladeHeight) ||
            !std::isfinite(v.MinimumStableVisibility))
            return false;
    }
    const auto& m = p.Midrange;
    if (!m.Groups.empty() &&
        (m.Members.size() != n || m.GroupForRoot.size() != n || m.GroupOrder.size() != m.Groups.size()))
        return false;
    for (auto i : m.Members)
        if (i >= n)
            return false;
    for (auto i : m.GroupForRoot)
        if (i >= m.Groups.size())
            return false;
    for (auto i : m.GroupOrder)
        if (i >= m.Groups.size())
            return false;
    for (const auto& g : m.Groups)
        if (!Range(g.FirstMember, g.MemberCount, m.Members.size()) || g.MemberCount == 0 ||
            g.MemberCount > kGrassMidrangeClusterCapacity || !Finite(g.Center) || !Finite(g.RepresentativePosition) ||
            !std::isfinite(g.RootRadius) || g.RootRadius < 0 || !std::isfinite(g.MaximumBladeRadius) ||
            !std::isfinite(g.StableVisibility))
            return false;
    for (size_t i = 0; i < m.Nodes.size(); ++i) {
        const auto& v = m.Nodes[i];
        if (v.Escape <= i || v.Escape > m.Nodes.size() || !Range(v.First, v.Count, m.GroupOrder.size()) ||
            !Finite(v.Center) || !std::isfinite(v.Radius) || v.Radius < 0 || !std::isfinite(v.MaximumBladeRadius) ||
            !std::isfinite(v.MinimumStableVisibility))
            return false;
    }
    for (const auto& s : p.ShadowSurfaces) {
        for (const auto& v : s.Position)
            if (!Finite(v))
                return false;
        if (!Finite(s.Normal))
            return false;
    }
    for (const auto& s : p.ShadowPoints)
        if (!Finite(s.Position) || !std::isfinite(s.Normal[0]) || !std::isfinite(s.Normal[1]) ||
            !std::isfinite(s.Normal[2]) || std::bit_cast<uint32_t>(s.Normal[3]) >= p.ShadowSurfaces.size())
            return false;
    for (const auto& r : p.ShadowReferences)
        if (r.First >= p.ShadowPoints.size() || r.Second >= p.ShadowPoints.size() || !std::isfinite(r.Weight) ||
            r.Weight < 0 || r.Weight > 1)
            return false;
    return true;
}
} // namespace
GrassPlacementDiskLoadTiming& GrassLastPlacementDiskLoadTiming() noexcept {
    thread_local GrassPlacementDiskLoadTiming timing;
    return timing;
}
GrassPlacementDiskKey GrassPlacementPersistentKey(const GrassAsyncPlacementRequest& r) {
    Hash content, settings;
    content.Add(kGrassPlacementDiskVersion);
    // GeometryId participates in the original random seed; retain it exactly.
    content.Add(r.PlacementKey.GeometryId);
    content.Add(r.TextureHash);
    content.Add(r.MapperSlot);
    if (r.Vertices)
        content.Vector(*r.Vertices);
    if (r.Indices)
        content.Vector(*r.Indices);
    if (r.Mask) {
        content.Add(r.Mask->Width);
        content.Add(r.Mask->Height);
        content.Add(r.Mask->Channel);
        content.Vector(r.Mask->Samples);
    }
    content.Add(r.ColorSource.ContentVersion());
    content.Add(r.LazyMaskTexture);
    content.Add(r.LazyColorTexture);
    if (r.Overlays) {
        // Only overlays that can remove grass shape the placement.
        uint64_t previous = 0, draw = 0, covering = 0;
        std::unordered_set<const GrassOverlayAlpha*> materials;
        for (const auto& t : r.Overlays->Triangles) {
            if (t.Alpha && !GrassOverlayCanCover(*t.Alpha))
                continue;
            ++covering;
            if (t.Draw != previous) {
                previous = t.Draw;
                ++draw;
            }
            content.Add(draw);
            for (const auto& v : t.Vertices) {
                content.Add(v.Position);
                content.Add(v.Uv);
                content.Add(v.Alpha);
            }
            if (t.Alpha) {
                content.Add(t.Alpha->Version);
                if (materials.insert(t.Alpha.get()).second) {
                    const auto mask = [&](const auto& scalar) {
                        content.Add(bool(scalar));
                        if (scalar) {
                            content.Add(scalar->Width);
                            content.Add(scalar->Height);
                            content.Add(scalar->Channel);
                            content.Vector(scalar->Samples);
                        }
                    };
                    for (const auto& channel : t.Alpha->Texture)
                        mask(channel);
                    mask(t.Alpha->CustomMask);
                }
            }
        }
        content.Add(covering);
    }
    settings.Add(GrassPlacementRuleVersion(r.Rule, r.Generation));
    settings.Add(r.ModelToWorld);
    settings.Add(r.TransformBakedIntoVertices);
    settings.Add(r.MaterialWrapS);
    settings.Add(r.MaterialWrapT);
    settings.Add(r.Budget);
    settings.Add(r.ClusterSize);
    settings.Add(r.NormalOffset);
    settings.Add(r.HeightScale);
    settings.Add(r.MidrangeCellExtent);
    settings.Add(r.MidrangeAdaptive);
    settings.Add(r.MidrangeAdaptiveCapacity);
    settings.Add(r.ShadowGrid);
    // Only camera-independent placements are admitted to disk.
    return { content.Value, settings.Value };
}
std::filesystem::path GrassPersistentPlacementCache::Path(GrassPlacementDiskKey key) const {
    if (mDirectory.empty())
        return {};
    char name[64];
    std::snprintf(name, sizeof(name), "%016llx-%016llx.grass", (unsigned long long)key.Content,
                  (unsigned long long)key.Settings);
    return mDirectory / name;
}
std::shared_ptr<GrassWorldPlacement> GrassPersistentPlacementCache::Load(GrassPlacementDiskKey key,
                                                                         bool* rejected) const {
    if (rejected)
        *rejected = false;
    if (mDirectory.empty())
        return {};
    auto& timing = GrassLastPlacementDiskLoadTiming();
    timing = {};
    using Clock = std::chrono::steady_clock;
    const auto since = [](Clock::time_point start) {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    };
    const auto waitStart = Clock::now();
    std::shared_lock lock(diskMutex);
    timing.MutexWaitMilliseconds = since(waitStart);
    try {
        const auto readStart = Clock::now();
        const auto path = Path(key);
        std::error_code ec;
        const auto size = std::filesystem::file_size(path, ec);
        if (ec)
            return {};
        if (rejected)
            *rejected = true;
        if (size < sizeof(Header) || size > kMaximumFileBytes)
            return {};
        UnbufferedFile in(path);
        Header h{};
        in.Read(&h, sizeof(h));
        if (!in.Good() || h.Magic != kMagic || h.Version != kGrassPlacementDiskVersion || h.Endian != 0x01020304 ||
            h.Key != key || h.Bytes != size - sizeof(Header))
            return {};
        auto p = std::make_shared<GrassWorldPlacement>();
        size_t i = 0;
        uint64_t bytes = 0;
        bool valid = true;
        Vectors(*p, [&](auto& v) {
            using T = typename std::decay_t<decltype(v)>::value_type;
            if (h.Sizes[i] != sizeof(T) || h.Counts[i] > (h.Bytes - bytes) / sizeof(T))
                valid = false;
            if (valid)
                bytes += h.Counts[i] * sizeof(T);
            ++i;
        });
        if (!valid || bytes != h.Bytes)
            return {};
        Hash hash;
        hash.Add(h.Sizes);
        hash.Add(h.Counts);
        hash.Add(h.MidrangeStats);
        i = 0;
        Vectors(*p, [&](auto& v) {
            v.resize(size_t(h.Counts[i++]));
            const auto n = v.size() * sizeof(v[0]);
            // Hash each chunk while it is still in cache; chunks are whole
            // words, so the checksum equals one pass over the vector.
            auto* data = reinterpret_cast<uint8_t*>(v.data());
            constexpr size_t kChunk = size_t(8) << 20U;
            for (size_t offset = 0; offset < n && in.Good(); offset += kChunk) {
                const auto count = std::min(kChunk, n - offset);
                in.Read(data + offset, count);
                hash.Bytes(data + offset, count);
            }
        });
        p->Midrange.LargeGroupCount = h.MidrangeStats[0];
        p->Midrange.MaximumMemberCount = h.MidrangeStats[1];
        p->Midrange.CapacityLimitedRanges = h.MidrangeStats[2];
        timing.ReadMilliseconds = since(readStart);
        timing.Bytes = size;
        const auto validateStart = Clock::now();
        const bool accepted = in.Good() && hash.Value == h.Checksum && Validate(*p);
        timing.ValidateMilliseconds = since(validateStart);
        if (!accepted)
            return {};
        std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(), ec);
        if (rejected)
            *rejected = false;
        return p;
    } catch (const std::exception&) { return {}; }
}
bool GrassPersistentPlacementCache::Store(GrassPlacementDiskKey key, const GrassWorldPlacement& p) const {
    if (mDirectory.empty() || mEntryLimit == 0 || !Validate(p))
        return false;
    std::scoped_lock lock(diskMutex);
    try {
        Header h{};
        h.Key = key;
        h.MidrangeStats = { p.Midrange.LargeGroupCount, p.Midrange.MaximumMemberCount,
                            p.Midrange.CapacityLimitedRanges };
        size_t i = 0;
        Vectors(p, [&](const auto& v) {
            h.Sizes[i] = sizeof(v[0]);
            h.Counts[i++] = v.size();
            h.Bytes += v.size() * sizeof(v[0]);
        });
        if (h.Bytes > kMaximumFileBytes - sizeof(h) || h.Bytes + sizeof(h) > mByteLimit)
            return false;
        Hash hash;
        hash.Add(h.Sizes);
        hash.Add(h.Counts);
        hash.Add(h.MidrangeStats);
        Vectors(p, [&](const auto& v) { hash.Bytes(v.data(), v.size() * sizeof(v[0])); });
        h.Checksum = hash.Value;
        std::filesystem::create_directories(mDirectory);
        const auto path = Path(key);
        auto temporary = path;
        static std::atomic<uint64_t> serial = 0;
#ifdef _WIN32
        const auto process = _getpid();
#else
        const auto process = getpid();
#endif
        temporary += "." + std::to_string(process) + "." + std::to_string(++serial) + ".tmp";
        struct Cleanup {
            std::filesystem::path Path;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove(Path, ec);
            }
        } cleanup{ temporary };
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(&h), sizeof(h));
            Vectors(
                p, [&](const auto& v) { out.write(reinterpret_cast<const char*>(v.data()), v.size() * sizeof(v[0])); });
            out.flush();
            if (!out)
                return false;
        }
        std::error_code ec;
        std::filesystem::rename(temporary, path, ec);
        if (ec) { // Windows rename does not replace a previous (possibly corrupt) entry.
            std::filesystem::remove(path, ec);
            ec.clear();
            std::filesystem::rename(temporary, path, ec);
            if (ec) {
                std::filesystem::remove(temporary, ec);
                return false;
            }
        }
        struct File {
            std::filesystem::path Path;
            uint64_t Bytes;
            std::filesystem::file_time_type Time;
        };
        std::vector<File> files;
        uint64_t total = 0;
        for (const auto& file : std::filesystem::directory_iterator(mDirectory)) {
            if (!file.is_regular_file() || file.path().extension() != ".grass")
                continue;
            const auto size = file.file_size();
            files.push_back({ file.path(), size, file.last_write_time() });
            total += size;
        }
        std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.Time < b.Time; });
        size_t count = files.size();
        for (const auto& file : files) {
            if (total <= mByteLimit && count <= mEntryLimit)
                break;
            if (file.Path == path)
                continue;
            if (std::filesystem::remove(file.Path, ec)) {
                total -= file.Bytes;
                --count;
            }
        }
        return true;
    } catch (const std::exception&) { return false; }
}
} // namespace Fast::Oot3d
