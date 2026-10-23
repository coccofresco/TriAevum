#include "fast/oot3d/grass_async_placement_builder.h"
#include "fast/oot3d/grass_overlay_mask.h"
#include "fast/oot3d/grass_persistent_placement_cache.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

namespace Fast::Oot3d {
namespace {

constexpr uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

template <typename Value> void HashValue(uint64_t& hash, const Value& value) noexcept {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    for (size_t index = 0U; index < sizeof(Value); ++index) {
        hash = (hash ^ bytes[index]) * kFnvPrime;
    }
}

} // namespace

uint64_t GrassPlacementSourceVersion(const GrassAsyncPlacementRequest& request) noexcept {
    uint64_t hash = kFnvOffset;
    HashValue(hash, request.PlacementKey.GeometryId);
    HashValue(hash, request.PlacementKey.ContentVersion);
    HashValue(hash, request.PlacementKey.TextureHash);
    HashValue(hash, request.PlacementKey.RuleId);
    HashValue(hash, request.SourceContentVersion);
    HashValue(hash, GrassPlacementRuleVersion(request.Rule, request.Generation));
    HashValue(hash, std::bit_cast<uint32_t>(request.ClusterSize));
    for (const float value : request.ModelToWorld) {
        HashValue(hash, std::bit_cast<uint32_t>(value));
    }
    HashValue(hash, request.TransformBakedIntoVertices);
    HashValue(hash, std::bit_cast<uint32_t>(request.NormalOffset));
    HashValue(hash, std::bit_cast<uint32_t>(request.HeightScale));
    if (request.MidrangeCellExtent != 0.0F)
        HashValue(hash, std::bit_cast<uint32_t>(request.MidrangeCellExtent));
    if (request.MidrangeAdaptive) {
        HashValue(hash, request.MidrangeAdaptive);
        HashValue(hash, request.MidrangeAdaptiveCapacity);
    }
    HashValue(hash, request.MaterialWrapS);
    HashValue(hash, request.MaterialWrapT);
    HashValue(hash, request.ColorSource.ContentVersion());
    HashValue(hash, request.LazyMaskTexture);
    HashValue(hash, request.LazyColorTexture);
    if (request.Overlays)
        HashValue(hash, request.Overlays->PlacementVersion);
    return hash == 0U ? 1U : hash;
}

namespace {
uint64_t TransformVersion(const GrassAsyncPlacementRequest& request, bool shadowGrid) noexcept {
    uint64_t hash = GrassPlacementSourceVersion(request);
    if (shadowGrid) HashValue(hash, shadowGrid);
    HashValue(hash, request.Budget);
    HashValue(hash, request.PlacementView.Enabled);
    if (request.PlacementView.Enabled) {
        for (float v : request.PlacementView.WorldToClip) HashValue(hash, std::bit_cast<uint32_t>(v));
        for (float v : request.PlacementView.Eye) HashValue(hash, std::bit_cast<uint32_t>(v));
        HashValue(hash, std::bit_cast<uint32_t>(request.PlacementView.DrawDistance));
        HashValue(hash, std::bit_cast<uint32_t>(request.PlacementView.FullDensityDistance));
        HashValue(hash, std::bit_cast<uint32_t>(request.PlacementView.FarDensity));
        HashValue(hash, std::bit_cast<uint32_t>(request.PlacementView.GuardDistance));
    }
    return hash == 0U ? 1U : hash;
}

struct BuildKey {
    GrassPlacementKey Placement;
    uint64_t WorldIdentity = 0U;
    uint64_t Transform = 0U;

    bool operator==(const BuildKey&) const = default;
};

struct BuildKeyHash {
    size_t operator()(const BuildKey& key) const noexcept {
        size_t hash = GrassPlacementKeyHash{}(key.Placement);
        hash ^= static_cast<size_t>(key.WorldIdentity) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
        hash ^= static_cast<size_t>(key.Transform) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
        return hash;
    }
};

bool Valid(const GrassAsyncPlacementRequest& request) {
    return request.PlacementKey.GeometryId != 0U && request.PlacementKey.ContentVersion != 0U &&
           request.SourceContentVersion != 0U && request.WorldIdentity != 0U && request.Vertices != nullptr &&
           !request.Vertices->empty() && request.Indices != nullptr && request.Indices->size() >= 3U &&
           ((request.Mask != nullptr && !request.Mask->Samples.empty()) || request.LazyMaskTexture != 0) &&
           request.Budget != 0U;
}

std::shared_ptr<const GrassWorldPlacement> Build(const GrassAsyncPlacementRequest& input) {
    auto request = input;
    if (request.LazyMaskTexture)
        request.Mask =
            GrassTextureSourceCache::Instance().AcquireMaskShared(request.LazyMaskTexture, request.Rule.Channel);
    if (request.LazyColorTexture)
        request.ColorSource = GrassTextureSourceCache::Instance().AcquireColorSource(request.LazyColorTexture);
    if (!request.Mask)
        throw std::runtime_error("grass source mask unavailable");
    request.Overlays = ResolveGrassOverlayTextures(request.Overlays);
    GrassSourceSurface surface;
    surface.GeometryId = request.PlacementKey.GeometryId;
    surface.ContentVersion = request.SourceContentVersion;
    surface.InstanceId = request.InstanceId;
    surface.TextureHash = request.TextureHash;
    surface.MapperSlot = request.MapperSlot;
    surface.MaterialWrapS = request.MaterialWrapS;
    surface.MaterialWrapT = request.MaterialWrapT;
    surface.ModelToWorld = request.ModelToWorld;
    surface.TransformBakedIntoVertices = request.TransformBakedIntoVertices;
    surface.Vertices = *request.Vertices;
    surface.Indices = *request.Indices;
    surface.PlacementView = request.PlacementView;
    surface.Overlays = request.Overlays;

    const bool diagnose = std::getenv("OOT3D_GRASS_DIAGNOSTICS") != nullptr;
    const auto started = diagnose ? std::chrono::steady_clock::now()
                                 : std::chrono::steady_clock::time_point{};
    auto anchors =
        GrassSurfaceExtractor::Extract(surface, request.Rule, request.Generation, *request.Mask, request.Budget);
    const auto extracted = diagnose ? std::chrono::steady_clock::now() : started;
    const auto bytesHash = [](const auto& values) {
        uint64_t hash = kFnvOffset;
        for (const auto& value : values) HashValue(hash, value);
        return hash;
    };
    const uint64_t anchorHash = diagnose ? bytesHash(anchors) : 0U;
    auto local = BuildGrassPlacementSet(std::move(anchors), request.ClusterSize);
    const auto clustered = diagnose ? std::chrono::steady_clock::now() : started;

    GrassWorldPlacementRequest world;
    world.ColorSource = request.ColorSource;
    world.ColorWrapS = ResolveGrassTextureWrap(request.Rule.Wrap, request.MaterialWrapS);
    world.ColorWrapT = ResolveGrassTextureWrap(request.Rule.Wrap, request.MaterialWrapT);
    world.Identity = request.WorldIdentity;
    world.ContentVersion = input.PlacementKey.ContentVersion ^ TransformVersion(input, input.ShadowGrid);
    if (world.ContentVersion == 0U) world.ContentVersion = 1U;
    world.FrameId = request.FrameId;
    world.Anchors = local.Anchors;
    world.Clusters = local.Clusters;
    world.ShadowGrid = request.ShadowGrid;
    world.SourceVertices = surface.Vertices;
    world.ModelToWorld = request.ModelToWorld;
    world.TransformBakedIntoVertices = request.TransformBakedIntoVertices;
    world.NormalOffset = request.NormalOffset;
    world.HeightScale = request.HeightScale;
    world.MidrangeCellExtent = request.MidrangeCellExtent;
    world.MidrangeAdaptive = request.MidrangeAdaptive;
    world.MidrangeAdaptiveCapacity = request.MidrangeAdaptiveCapacity;
    world.MidrangeMask = request.Mask.get();
    world.MidrangeMaskRule = &request.Rule;
    auto result = std::make_shared<GrassWorldPlacement>(BuildGrassWorldPlacement(world));
    result->ContentVersion = world.ContentVersion;
    if (diagnose) {
        const auto elapsed = [](auto begin, auto end) {
            return std::chrono::duration<double, std::milli>(end - begin).count();
        };
        std::fprintf(stderr,
            "[grass-build] source=%llx anchors=%zu clusters=%zu extract_ms=%.3f cluster_ms=%.3f world_ms=%.3f "
            "anchor_hash=%016llx world_hash=%016llx\n",
            static_cast<unsigned long long>(request.WorldIdentity), local.Anchors.size(), local.Clusters.size(),
            elapsed(started, extracted), elapsed(extracted, clustered),
            elapsed(clustered, std::chrono::steady_clock::now()), static_cast<unsigned long long>(anchorHash),
            static_cast<unsigned long long>(bytesHash(result->Anchors)));
    }
    return result;
}

} // namespace

void GrassAsyncPlacementResult::WaitUntilReady() {
    if (State != GrassAsyncPlacementState::Pending || !Completion.valid()) return;
    try {
        const auto complete = Completion.get();
        Placement = complete.Placement;
        BuildMilliseconds = complete.BuildMilliseconds;
        State = Placement ? GrassAsyncPlacementState::Ready : GrassAsyncPlacementState::Failed;
    } catch (const std::future_error&) {
        State = GrassAsyncPlacementState::Failed;
    }
    Completion = {};
}

struct GrassAsyncPlacementBuilder::Impl {
    enum class EntryState : uint8_t {
        Pending,
        Ready,
        Failed,
    };

    struct Entry {
        EntryState State = EntryState::Pending;
        uint64_t Generation = 0U;
        uint64_t LastUsedFrame = 0U;
        std::shared_ptr<const GrassWorldPlacement> Placement;
        double BuildMilliseconds = 0.0;
        std::shared_future<GrassPlacementCompletion> Completion;
        // A built placement reaches the disk only after it stayed in use for
        // the settle delay, so the intermediate states of an F1 edit (one per
        // slider step) are never written.
        std::optional<GrassPlacementDiskKey> PendingDiskKey;
        std::filesystem::path PendingDiskDirectory;
        std::chrono::steady_clock::time_point ReadyTime;
    };

    struct Job {
        BuildKey Key;
        uint64_t Generation = 0U;
        GrassAsyncPlacementRequest Request;
        std::filesystem::path Directory;
        std::shared_ptr<std::promise<GrassPlacementCompletion>> Completion;
        // A store job: no build, no completion.
        std::shared_ptr<const GrassWorldPlacement> Store;
        GrassPlacementDiskKey StoreKey;
    };

    Impl(size_t capacity, size_t workerCount, size_t byteCapacity)
        : Capacity(std::max<size_t>(capacity, 1U)), ByteCapacity(byteCapacity) {
        workerCount = std::max<size_t>(workerCount, 1U);
        Workers.reserve(workerCount);
        for (size_t index = 0U; index < workerCount; ++index) {
            Workers.emplace_back([this] { WorkerMain(); });
        }
    }

    ~Impl() {
        {
            std::scoped_lock lock(Mutex);
            Stopping = true;
            Jobs.clear();
        }
        WorkReady.notify_all();
        for (auto& worker : Workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    void WorkerMain() {
        for (;;) {
            Job job;
            {
                std::unique_lock lock(Mutex);
                WorkReady.wait(lock, [this] { return Stopping || !Jobs.empty(); });
                if (Stopping && Jobs.empty()) {
                    return;
                }
                job = std::move(Jobs.front());
                Jobs.pop_front();
                ++RunningJobs;
            }
            if (job.Store) {
                bool written = false;
                try {
                    written = GrassPersistentPlacementCache(job.Directory).Store(job.StoreKey, *job.Store);
                } catch (...) {}
                std::scoped_lock lock(Mutex);
                --RunningJobs;
                Statistics.DiskWrites += written;
                if (Jobs.empty() && RunningJobs == 0U)
                    Idle.notify_all();
                continue;
            }

            std::shared_ptr<const GrassWorldPlacement> placement;
            bool failed = false;
            bool diskHit = false, diskMiss = false, diskReject = false;
            const bool diskEnabled = !job.Directory.empty() && !job.Request.PlacementView.Enabled;
            GrassPlacementDiskKey diskKey;
            const auto begin = std::chrono::steady_clock::now();
            try {
                if (diskEnabled)
                    diskKey = GrassPlacementPersistentKey(job.Request);
                GrassPersistentPlacementCache disk(job.Directory);
                if (diskEnabled) {
                    auto cached = disk.Load(diskKey, &diskReject);
                    if (cached && std::any_of(cached->Anchors.begin(), cached->Anchors.end(), [&](const auto& anchor) {
                            const auto& ref = anchor.SurfaceReference;
                            return (ref[0] & 65535U) >= job.Request.Vertices->size() ||
                                   (ref[0] >> 16) >= job.Request.Vertices->size() ||
                                   (ref[1] & 65535U) >= job.Request.Vertices->size();
                        })) {
                        cached.reset();
                        diskReject = true;
                    }
                    if (cached) {
                        cached->Identity = job.Request.WorldIdentity;
                        cached->ContentVersion = job.Request.PlacementKey.ContentVersion ^
                                                 TransformVersion(job.Request, job.Request.ShadowGrid);
                        if (!cached->ContentVersion)
                            cached->ContentVersion = 1;
                        cached->LastUsedFrame = job.Request.FrameId;
                        placement = std::move(cached);
                        diskHit = true;
                    } else
                        diskMiss = true;
                }
                if (!placement)
                    placement = Build(job.Request);
            } catch (...) { failed = true; }
            const double elapsed =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();

            {
                std::scoped_lock lock(Mutex);
                --RunningJobs;
                job.Completion->set_value(job.Generation == Generation
                    ? GrassPlacementCompletion{placement, elapsed} : GrassPlacementCompletion{});
                const auto found = Entries.find(job.Key);
                if (job.Generation == Generation && found != Entries.end() &&
                    found->second.Generation == job.Generation && found->second.State == EntryState::Pending) {
                    found->second.State = failed ? EntryState::Failed : EntryState::Ready;
                    if (diskEnabled && !diskHit && placement) {
                        found->second.PendingDiskKey = diskKey;
                        found->second.PendingDiskDirectory = job.Directory;
                        found->second.ReadyTime = std::chrono::steady_clock::now();
                    }
                    found->second.Placement = std::move(placement);
                    found->second.BuildMilliseconds = elapsed;
                    Statistics.Builds += !diskHit;
                    Statistics.DiskHits += diskHit;
                    Statistics.DiskMisses += diskMiss;
                    Statistics.DiskRejects += diskReject;
                    Statistics.BuildMilliseconds += elapsed;
                    if (failed) {
                        ++Statistics.Failures;
                    }
                    EvictIfNeeded(job.Key);
                    RefreshCounts();
                }
                if (Jobs.empty() && RunningJobs == 0U) {
                    Idle.notify_all();
                }
            }
        }
    }

    void EvictIfNeeded(const BuildKey& protectedKey) {
        const auto bytes = [](const auto& entry) -> size_t {
            if (!entry.Placement) return 0;
            const auto& p=*entry.Placement;
            return p.Anchors.capacity()*sizeof(GrassWorldAnchor)+p.CullingAnchors.capacity()*sizeof(GrassWorldCullingAnchor)+
                p.Clusters.capacity()*sizeof(GrassWorldCluster)+p.VisibilityNodes.capacity()*sizeof(GrassClusterVisibilityNode)+
                p.VisibilityClusterOrder.capacity()*sizeof(uint32_t)+
                p.ShadowPoints.capacity()*sizeof(GrassShadowGridPoint)+
                p.ShadowReferences.capacity()*sizeof(GrassShadowGridReference)+
                p.ShadowSurfaces.capacity()*sizeof(GrassShadowSurface)+
                (p.Midrange.Members.capacity()+p.Midrange.GroupForRoot.capacity()+p.Midrange.GroupOrder.capacity())*sizeof(uint32_t)+
                p.Midrange.Groups.capacity()*sizeof(GrassMidrangeCluster)+
                p.Midrange.Nodes.capacity()*sizeof(GrassMidrangeClusters::Node);
        };
        size_t resident=0;
        for (const auto& [key,entry] : Entries) resident+=bytes(entry);
        while (Entries.size() > Capacity || resident > ByteCapacity) {
            auto oldest = Entries.end();
            for (auto entry = Entries.begin(); entry != Entries.end(); ++entry) {
                if (entry->first == protectedKey || entry->second.State == EntryState::Pending) {
                    continue;
                }
                if (oldest == Entries.end() || entry->second.LastUsedFrame < oldest->second.LastUsedFrame) {
                    oldest = entry;
                }
            }
            if (oldest == Entries.end()) {
                break;
            }
            resident-=bytes(oldest->second);
            Entries.erase(oldest);
        }
        Statistics.ResidentBytes=resident;
    }

    // Queues the write of a ready placement still in use after the settle
    // delay; true when a worker must be woken.
    bool QueueSettledStore(Entry& entry) {
        if (!entry.PendingDiskKey || !entry.Placement ||
            std::chrono::steady_clock::now() - entry.ReadyTime < DiskSettleDelay)
            return false;
        Job job;
        job.Directory = std::move(entry.PendingDiskDirectory);
        job.Store = entry.Placement;
        job.StoreKey = *entry.PendingDiskKey;
        entry.PendingDiskKey.reset();
        Jobs.push_back(std::move(job));
        return true;
    }

    void RefreshCounts() {
        Statistics.Entries = Entries.size();
        Statistics.Pending = static_cast<size_t>(std::count_if(Entries.begin(), Entries.end(), [](const auto& entry) {
            return entry.second.State == EntryState::Pending;
        }));
    }

    std::filesystem::path Directory;
    size_t Capacity = 1U;
    size_t ByteCapacity = 0U;
    mutable std::mutex Mutex;
    std::condition_variable WorkReady;
    std::condition_variable Idle;
    std::deque<Job> Jobs;
    std::vector<std::thread> Workers;
    std::unordered_map<BuildKey, Entry, BuildKeyHash> Entries;
    GrassAsyncPlacementStats Statistics;
    uint64_t Generation = 1U;
    size_t RunningJobs = 0U;
    bool Stopping = false;
    std::chrono::steady_clock::duration DiskSettleDelay = std::chrono::seconds(2);
};

GrassAsyncPlacementBuilder::GrassAsyncPlacementBuilder(size_t capacity, size_t workerCount, size_t byteCapacity)
    : mImpl(std::make_unique<Impl>(capacity, workerCount, byteCapacity)) {
}

GrassAsyncPlacementBuilder::~GrassAsyncPlacementBuilder() = default;

GrassAsyncPlacementResult GrassAsyncPlacementBuilder::ResolveOrQueue(GrassAsyncPlacementRequest request) {
    if (!Valid(request)) {
        return {};
    }
    const BuildKey key{
        request.PlacementKey,
        request.WorldIdentity,
        TransformVersion(request, request.ShadowGrid),
    };

    std::shared_future<GrassPlacementCompletion> completion;
    bool storeQueued = false;
    const auto ready = [&](Impl::Entry& entry) {
        entry.LastUsedFrame = request.FrameId;
        ++mImpl->Statistics.Hits;
        storeQueued = mImpl->QueueSettledStore(entry);
        return GrassAsyncPlacementResult{
            GrassAsyncPlacementState::Ready,
            entry.Placement,
            false,
            entry.BuildMilliseconds,
        };
    };
    {
        std::scoped_lock lock(mImpl->Mutex);
        // A placement built with the shadow grid carries every field of one
        // without it: a scene without directional shadows reuses it.
        if (!request.ShadowGrid && !mImpl->Entries.contains(key)) {
            const BuildKey shadowed{key.Placement, key.WorldIdentity, TransformVersion(request, true)};
            if (const auto found = mImpl->Entries.find(shadowed);
                found != mImpl->Entries.end() && found->second.State == Impl::EntryState::Ready) {
                auto result = ready(found->second);
                if (storeQueued)
                    mImpl->WorkReady.notify_one();
                return result;
            }
        }
        if (const auto found = mImpl->Entries.find(key); found != mImpl->Entries.end()) {
            found->second.LastUsedFrame = request.FrameId;
            switch (found->second.State) {
                case Impl::EntryState::Ready: {
                    auto result = ready(found->second);
                    if (storeQueued)
                        mImpl->WorkReady.notify_one();
                    return result;
                }
                case Impl::EntryState::Pending:
                    ++mImpl->Statistics.PendingHits;
                    return {
                        GrassAsyncPlacementState::Pending,
                        nullptr, false, 0.0, found->second.Completion,
                    };
                case Impl::EntryState::Failed:
                    return {
                        GrassAsyncPlacementState::Failed,
                    };
            }
        }

        ++mImpl->Statistics.Misses;
        static const bool diagnose = std::getenv("OOT3D_GRASS_DIAGNOSTICS") != nullptr;
        if (diagnose)
            std::fprintf(stderr,
                "[grass-miss] world=%llx geometry=%llx placement_version=%llx source_version=%llx transform=%llx "
                "budget=%u color=%llx frame=%llu\n",
                static_cast<unsigned long long>(request.WorldIdentity),
                static_cast<unsigned long long>(request.PlacementKey.GeometryId),
                static_cast<unsigned long long>(request.PlacementKey.ContentVersion),
                static_cast<unsigned long long>(request.SourceContentVersion),
                static_cast<unsigned long long>(key.Transform), request.Budget,
                static_cast<unsigned long long>(request.ColorSource.ContentVersion()),
                static_cast<unsigned long long>(request.FrameId));
        auto promise = std::make_shared<std::promise<GrassPlacementCompletion>>();
        completion = promise->get_future().share();
        mImpl->Entries.emplace(key, Impl::Entry{
                                        Impl::EntryState::Pending,
                                        mImpl->Generation,
                                        request.FrameId,
                                        nullptr, 0.0, completion,
                                    });
        mImpl->Jobs.push_back({
            key,
            mImpl->Generation,
            std::move(request),
            mImpl->Directory,
            std::move(promise),
        });
        mImpl->RefreshCounts();
    }
    mImpl->WorkReady.notify_one();
    return {
        GrassAsyncPlacementState::Pending,
        nullptr,
        true,
        0.0, completion,
    };
}

GrassAsyncPlacementStats GrassAsyncPlacementBuilder::Stats() const {
    std::scoped_lock lock(mImpl->Mutex);
    auto stats = mImpl->Statistics;
    stats.Entries = mImpl->Entries.size();
    stats.Pending =
        static_cast<size_t>(std::count_if(mImpl->Entries.begin(), mImpl->Entries.end(), [](const auto& entry) {
            return entry.second.State == Impl::EntryState::Pending;
        }));
    return stats;
}

void GrassAsyncPlacementBuilder::SetDiskSettleDelay(std::chrono::steady_clock::duration delay) {
    std::scoped_lock lock(mImpl->Mutex);
    mImpl->DiskSettleDelay = delay;
}

void GrassAsyncPlacementBuilder::SetCacheDirectory(std::filesystem::path directory) {
    std::scoped_lock lock(mImpl->Mutex);
    mImpl->Directory = std::move(directory);
}

void GrassAsyncPlacementBuilder::Clear() {
    std::scoped_lock lock(mImpl->Mutex);
    ++mImpl->Generation;
    if (mImpl->Generation == 0U) {
        ++mImpl->Generation;
    }
    mImpl->Jobs.clear();
    mImpl->Entries.clear();
    mImpl->Statistics = {};
    mImpl->RefreshCounts();
}

void GrassAsyncPlacementBuilder::WaitForIdle() {
    std::unique_lock lock(mImpl->Mutex);
    mImpl->Idle.wait(lock, [this] { return mImpl->Jobs.empty() && mImpl->RunningJobs == 0U; });
}

} // namespace Fast::Oot3d
