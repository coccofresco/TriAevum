#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Fast::Renderer3ds {

// Device-local live objects, not serialized driver/shader cache data. Active
// aliases pin their pipeline; only unreferenced objects count toward the limit.
template<class Pipeline> class PicaDevicePipelinePool {
  public:
    PicaDevicePipelinePool() : PicaDevicePipelinePool(128) {}
    explicit PicaDevicePipelinePool(size_t idleLimit) : mIdleLimit(idleLimit) {}

    std::shared_ptr<Pipeline> Find(const std::vector<uint8_t>& key) {
        const auto found = mEntries.find(key);
        if (found == mEntries.end()) return {};
        found->second.LastUse = ++mClock;
        return found->second.Object;
    }

    void Retain(std::vector<uint8_t> key, std::shared_ptr<Pipeline> object) {
        if (!object) throw std::invalid_argument("cannot retain a null device pipeline");
        if (!mEntries.emplace(std::move(key), Entry{std::move(object), ++mClock}).second)
            throw std::logic_error("device pipeline key already retained");
        Prune();
    }

    // Call after releasing logical aliases, at the existing GPU-safe retirement
    // boundary. Never evict an object still referenced by a logical pipeline.
    void Prune() {
        for (;;) {
            size_t idle = 0;
            auto oldest = mEntries.end();
            for (auto it = mEntries.begin(); it != mEntries.end(); ++it) {
                if (it->second.Object.use_count() != 1) continue;
                ++idle;
                if (oldest == mEntries.end() || it->second.LastUse < oldest->second.LastUse)
                    oldest = it;
            }
            if (idle <= mIdleLimit) return;
            mEntries.erase(oldest);
        }
    }

    void Clear() { mEntries.clear(); mClock = 0; }
    size_t Size() const { return mEntries.size(); }

  private:
    struct Entry { std::shared_ptr<Pipeline> Object; uint64_t LastUse; };
    size_t mIdleLimit;
    uint64_t mClock = 0;
    std::map<std::vector<uint8_t>, Entry> mEntries;
};

} // namespace Fast::Renderer3ds
