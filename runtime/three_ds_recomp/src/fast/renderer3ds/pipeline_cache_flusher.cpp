#include "fast/renderer3ds/pipeline_cache_flusher.h"

#include "fast/renderer/preparation_host.h"

#include <cstdio>
#include <exception>
#include <string>
#include <utility>

namespace Fast::Renderer3ds {

bool PipelineCacheFlushDue(uint64_t createdSinceFlush, double secondsSinceFlush,
                           const PipelineCacheFlushPolicy& policy) {
    if (createdSinceFlush == 0 || createdSinceFlush < policy.MinimumNewPipelines) return false;
    return secondsSinceFlush >= policy.MinimumIntervalSeconds;
}

PipelineCacheFlusher::~PipelineCacheFlusher() { Stop(); }

void PipelineCacheFlusher::Start(std::filesystem::path path, VulkanPipelineCacheHeader identity,
                                 uint64_t maximumBytes, Snapshot snapshot, PipelineCacheFlushPolicy policy) {
    Stop();
    if (path.empty() || !snapshot || maximumBytes == 0) return;
    mPath = std::move(path);
    mIdentity = identity;
    mMaximumBytes = maximumBytes;
    mSnapshot = std::move(snapshot);
    mPolicy = policy;
    {
        std::lock_guard lock(mMutex);
        mStopping = false;
        mFlushedTotal = mCreatedTotal.load();
        mLastFlush = std::chrono::steady_clock::now();
    }
    mThread = std::thread([this] { Worker(); });
}

void PipelineCacheFlusher::Notice(uint64_t createdTotal) {
    const uint64_t previous = mCreatedTotal.exchange(createdTotal);
    if (createdTotal != previous) mWake.notify_one();
}

void PipelineCacheFlusher::Stop() {
    if (!mThread.joinable()) return;
    {
        std::lock_guard lock(mMutex);
        mStopping = true;
    }
    mWake.notify_all();
    mThread.join();
}

PipelineCacheFlusher::Statistics PipelineCacheFlusher::Stats() const {
    std::lock_guard lock(mMutex);
    return mStatistics;
}

void PipelineCacheFlusher::Worker() {
    Renderer::LowerPreparationThreadPriority();
    for (;;) {
        uint64_t created = 0;
        {
            std::unique_lock lock(mMutex);
            // The interval may elapse without a new creation; poll each second.
            mWake.wait_for(lock, std::chrono::seconds(1), [this] { return mStopping; });
            if (mStopping) return;
            created = mCreatedTotal.load();
            const double elapsed =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - mLastFlush).count();
            if (!PipelineCacheFlushDue(created - mFlushedTotal, elapsed, mPolicy)) continue;
        }
        const bool stored = FlushOnce(created);
        std::lock_guard lock(mMutex);
        mLastFlush = std::chrono::steady_clock::now();
        // A failed or skipped flush is not retried for the same pipelines; the
        // next creations try again after the interval.
        mFlushedTotal = created;
        (void)stored;
    }
}

bool PipelineCacheFlusher::FlushOnce(uint64_t created) {
    std::vector<uint8_t> data;
    try {
        data = mSnapshot();
    } catch (const std::exception& exception) {
        std::lock_guard lock(mMutex);
        ++mStatistics.Failures;
        std::fprintf(stderr, "TRIAEVUM_NRI_PIPELINE_CACHE_FLUSH result=failed created=%llu error=%s\n",
                     static_cast<unsigned long long>(created), exception.what());
        return false;
    }
    if (data.empty() || data.size() > mMaximumBytes) {
        std::lock_guard lock(mMutex);
        ++mStatistics.Skipped;
        return false;
    }
    std::string error;
    const bool stored = StorePipelineCacheData(mPath, mIdentity, data, &error, mMaximumBytes);
    std::lock_guard lock(mMutex);
    if (stored) {
        ++mStatistics.Flushes;
        mStatistics.FlushedBytes += data.size();
        std::fprintf(stderr, "TRIAEVUM_NRI_PIPELINE_CACHE_FLUSH result=stored bytes=%zu created=%llu\n",
                     data.size(), static_cast<unsigned long long>(created));
    } else {
        ++mStatistics.Failures;
        std::fprintf(stderr, "TRIAEVUM_NRI_PIPELINE_CACHE_FLUSH result=failed bytes=%zu created=%llu error=%s\n",
                     data.size(), static_cast<unsigned long long>(created), error.c_str());
    }
    return stored;
}

}  // namespace Fast::Renderer3ds
