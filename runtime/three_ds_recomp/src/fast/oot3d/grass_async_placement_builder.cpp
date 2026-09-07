#include "fast/oot3d/grass_async_placement_builder.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
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
    HashValue(hash, request.MaterialWrapS);
    HashValue(hash, request.MaterialWrapT);
    return hash == 0U ? 1U : hash;
}

namespace {
uint64_t TransformVersion(const GrassAsyncPlacementRequest& request) noexcept {
    uint64_t hash = GrassPlacementSourceVersion(request);
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
           request.Mask != nullptr && !request.Mask->Samples.empty() && request.Budget != 0U;
}

std::shared_ptr<const GrassWorldPlacement> Build(const GrassAsyncPlacementRequest& request) {
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

    auto anchors =
        GrassSurfaceExtractor::Extract(surface, request.Rule, request.Generation, *request.Mask, request.Budget);
    auto local = BuildGrassPlacementSet(std::move(anchors), request.ClusterSize);

    GrassWorldPlacementRequest world;
    world.Identity = request.WorldIdentity;
    world.ContentVersion = request.PlacementKey.ContentVersion ^ TransformVersion(request);
    if (world.ContentVersion == 0U) world.ContentVersion = 1U;
    world.FrameId = request.FrameId;
    world.Anchors = local.Anchors;
    world.Clusters = local.Clusters;
    world.ModelToWorld = request.ModelToWorld;
    world.TransformBakedIntoVertices = request.TransformBakedIntoVertices;
    world.NormalOffset = request.NormalOffset;
    world.HeightScale = request.HeightScale;
    return std::make_shared<const GrassWorldPlacement>(BuildGrassWorldPlacement(world));
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
    };

    struct Job {
        BuildKey Key;
        uint64_t Generation = 0U;
        GrassAsyncPlacementRequest Request;
        std::shared_ptr<std::promise<GrassPlacementCompletion>> Completion;
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

            std::shared_ptr<const GrassWorldPlacement> placement;
            bool failed = false;
            const auto begin = std::chrono::steady_clock::now();
            try {
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
                    found->second.Placement = std::move(placement);
                    found->second.BuildMilliseconds = elapsed;
                    ++Statistics.Builds;
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
                p.VisibilityClusterOrder.capacity()*sizeof(uint32_t);
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

    void RefreshCounts() {
        Statistics.Entries = Entries.size();
        Statistics.Pending = static_cast<size_t>(std::count_if(Entries.begin(), Entries.end(), [](const auto& entry) {
            return entry.second.State == EntryState::Pending;
        }));
    }

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
        TransformVersion(request),
    };

    std::shared_future<GrassPlacementCompletion> completion;
    {
        std::scoped_lock lock(mImpl->Mutex);
        if (const auto found = mImpl->Entries.find(key); found != mImpl->Entries.end()) {
            found->second.LastUsedFrame = request.FrameId;
            switch (found->second.State) {
                case Impl::EntryState::Ready:
                    ++mImpl->Statistics.Hits;
                    return {
                        GrassAsyncPlacementState::Ready,
                        found->second.Placement,
                        false,
                        found->second.BuildMilliseconds,
                    };
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
