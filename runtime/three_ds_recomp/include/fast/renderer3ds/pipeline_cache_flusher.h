#pragma once

// Periodic persistence of a Vulkan pipeline cache while pipelines are still
// being created. Storing only at clean shutdown loses every compilation of a
// session that crashes, is killed, or runs out of its time budget (Forge's
// preparation on a slow driver); the flusher writes the cache from its own
// below-normal thread whenever enough new pipelines have accumulated, through
// the same atomic store the shutdown path uses. Adapted from the WINDROID
// fork's periodic cache flush (RECOMP-ZELDA-3DS-ANDROID, 857081f).

#include "fast/renderer3ds/vulkan_pipeline_cache_store.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace Fast::Renderer3ds {

struct PipelineCacheFlushPolicy {
    // A flush needs at least this many pipelines created since the last one...
    uint64_t MinimumNewPipelines = 1;
    // ...and at least this long since the last flush, so a cold session that
    // creates hundreds of pipelines per minute rewrites the file at a bounded
    // rate instead of once per pipeline.
    double MinimumIntervalSeconds = 20.0;
};

// Pure decision shared by the renderer and its tests.
bool PipelineCacheFlushDue(uint64_t createdSinceFlush, double secondsSinceFlush,
                           const PipelineCacheFlushPolicy& policy);

class PipelineCacheFlusher final {
  public:
    // Returns the cache bytes to persist; empty when there is nothing to store.
    // Called on the flusher thread while pipelines may be created elsewhere:
    // VkPipelineCache access is internally synchronized unless created
    // externally synchronized, which NRI does not do.
    using Snapshot = std::function<std::vector<uint8_t>()>;

    struct Statistics {
        uint64_t Flushes = 0;
        uint64_t FlushedBytes = 0;
        uint64_t Failures = 0;
        uint64_t Skipped = 0;  // snapshot empty or over the limit
    };

    PipelineCacheFlusher() = default;
    ~PipelineCacheFlusher();
    PipelineCacheFlusher(const PipelineCacheFlusher&) = delete;
    PipelineCacheFlusher& operator=(const PipelineCacheFlusher&) = delete;

    void Start(std::filesystem::path path, VulkanPipelineCacheHeader identity, uint64_t maximumBytes,
               Snapshot snapshot, PipelineCacheFlushPolicy policy = {});
    // Cheap, any thread: the total number of pipelines created so far.
    void Notice(uint64_t createdTotal);
    // Joins the thread. No final flush: the owner stores the cache itself at
    // shutdown, after the last creation.
    void Stop();
    bool Running() const { return mThread.joinable(); }
    Statistics Stats() const;

  private:
    void Worker();
    bool FlushOnce(uint64_t created);

    std::filesystem::path mPath;
    VulkanPipelineCacheHeader mIdentity{};
    uint64_t mMaximumBytes = 0;
    Snapshot mSnapshot;
    PipelineCacheFlushPolicy mPolicy;
    std::thread mThread;
    mutable std::mutex mMutex;
    std::condition_variable mWake;
    bool mStopping = false;
    std::atomic<uint64_t> mCreatedTotal{0};
    uint64_t mFlushedTotal = 0;
    std::chrono::steady_clock::time_point mLastFlush{};
    Statistics mStatistics;
};

}  // namespace Fast::Renderer3ds
