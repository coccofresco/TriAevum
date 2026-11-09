#pragma once

// Free list for the images of the GPU texture-copy sampler path (raw-copy
// captures and the sampled images derived from them).
//
// The path used to create one image per executed copy and one per (texture key,
// producer version) and destroy both when the producer retired, which made the
// driver allocate and free GPU memory and virtual address ranges every frame.
// Retired images now come here instead and the next creation with an identical
// key takes one back. The pool never decides when an image is idle: the caller
// releases an image only at the point where it used to destroy it (after the
// frame fence that covered its last use), so every pooled image is free of
// in-flight GPU work by construction.
//
// Header-only and API-neutral so the policy (key equality, LRU reuse, bounds,
// trimming) is unit-tested without a device.

#include <cstddef>
#include <cstdint>
#include <list>
#include <utility>
#include <vector>

namespace Fast {

struct GpuCopyImagePoolKey {
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t Format = 0;       // VkFormat
    uint32_t Usage = 0;        // VkImageUsageFlags the image was created with
    uint32_t MipLevels = 1;
    uint32_t Layers = 1;
    uint32_t Allocator = 0;    // who owns the allocation (NRI-owned vs raw Vulkan)

    friend bool operator==(const GpuCopyImagePoolKey&, const GpuCopyImagePoolKey&) = default;
};

struct GpuCopyImagePoolLimits {
    size_t MaxEntries = 128;
    uint64_t MaxBytes = 256ULL * 1024ULL * 1024ULL;
    // An entry not reused for this many frames is released by Trim.
    uint64_t MaxIdleFrames = 240;
};

struct GpuCopyImagePoolStats {
    uint64_t Created = 0;     // acquisitions that missed and had to allocate
    uint64_t Reused = 0;      // acquisitions served from the pool
    uint64_t Released = 0;    // images accepted into the pool
    uint64_t Trimmed = 0;     // pooled images destroyed by bounds, age or a full release
    uint64_t Rejected = 0;    // releases destroyed immediately (pool disabled or image too large)
    uint64_t PeakBytes = 0;
};

template <typename Payload>
class GpuCopyImagePool {
public:
    struct Evicted {
        GpuCopyImagePoolKey Key;
        Payload Value;
        uint64_t Bytes = 0;
    };

    explicit GpuCopyImagePool(GpuCopyImagePoolLimits limits = {}) : mLimits(limits) {}

    void SetLimits(const GpuCopyImagePoolLimits& limits) { mLimits = limits; }
    const GpuCopyImagePoolLimits& Limits() const { return mLimits; }
    void SetEnabled(bool enabled) { mEnabled = enabled; }
    bool Enabled() const { return mEnabled; }

    // Takes the most recently released image with exactly this key. A miss is
    // counted as a creation, which the caller then performs.
    bool Acquire(const GpuCopyImagePoolKey& key, Payload& out) {
        if (mEnabled) {
            for (auto it = mEntries.rbegin(); it != mEntries.rend(); ++it) {
                if (it->Key == key) {
                    out = std::move(it->Value);
                    mBytes -= it->Bytes;
                    mEntries.erase(std::next(it).base());
                    ++mStats.Reused;
                    return true;
                }
            }
        }
        ++mStats.Created;
        return false;
    }

    // Counts an allocation that bypassed Acquire (a caller that may not take a
    // pooled image, for example because its writes do not cover every texel).
    void CountCreation() { ++mStats.Created; }

    // Offers a retired image. Returns the images the caller must destroy now:
    // the offered one when it is not accepted, and whatever the bounds evict
    // (oldest first).
    std::vector<Evicted> Release(const GpuCopyImagePoolKey& key, Payload value,
                                 uint64_t bytes, uint64_t frame) {
        std::vector<Evicted> evicted;
        if (!mEnabled || mLimits.MaxEntries == 0U || bytes > mLimits.MaxBytes) {
            ++mStats.Rejected;
            evicted.push_back({key, std::move(value), bytes});
            return evicted;
        }
        mEntries.push_back({key, std::move(value), bytes, frame});
        mBytes += bytes;
        ++mStats.Released;
        EvictToBounds(evicted);
        if (mBytes > mStats.PeakBytes) mStats.PeakBytes = mBytes;
        return evicted;
    }

    // Releases entries idle for longer than MaxIdleFrames and enforces bounds.
    std::vector<Evicted> Trim(uint64_t frame) {
        std::vector<Evicted> evicted;
        for (auto it = mEntries.begin(); it != mEntries.end();) {
            if (frame > it->ReleasedFrame &&
                frame - it->ReleasedFrame > mLimits.MaxIdleFrames) {
                evicted.push_back({it->Key, std::move(it->Value), it->Bytes});
                mBytes -= it->Bytes;
                it = mEntries.erase(it);
                ++mStats.Trimmed;
            } else {
                ++it;
            }
        }
        EvictToBounds(evicted);
        return evicted;
    }

    // Everything goes: resize, scale change, device loss, shutdown.
    std::vector<Evicted> ReleaseAll() {
        std::vector<Evicted> evicted;
        evicted.reserve(mEntries.size());
        for (auto& entry : mEntries) {
            evicted.push_back({entry.Key, std::move(entry.Value), entry.Bytes});
            ++mStats.Trimmed;
        }
        mEntries.clear();
        mBytes = 0;
        return evicted;
    }

    size_t Entries() const { return mEntries.size(); }
    uint64_t Bytes() const { return mBytes; }
    const GpuCopyImagePoolStats& Stats() const { return mStats; }

private:
    struct Entry {
        GpuCopyImagePoolKey Key;
        Payload Value;
        uint64_t Bytes = 0;
        uint64_t ReleasedFrame = 0;
    };

    void EvictToBounds(std::vector<Evicted>& evicted) {
        while (!mEntries.empty() &&
               (mEntries.size() > mLimits.MaxEntries || mBytes > mLimits.MaxBytes)) {
            auto& oldest = mEntries.front();
            evicted.push_back({oldest.Key, std::move(oldest.Value), oldest.Bytes});
            mBytes -= oldest.Bytes;
            mEntries.pop_front();
            ++mStats.Trimmed;
        }
    }

    GpuCopyImagePoolLimits mLimits;
    std::list<Entry> mEntries;  // oldest release first
    uint64_t mBytes = 0;
    bool mEnabled = true;
    GpuCopyImagePoolStats mStats;
};

// A reused image keeps its previous texels, a fresh one starts undefined. The
// derived-image path may take a pooled image only when its writes provably
// cover every texel; this checks the union of the regions exactly (overlaps do
// not count twice, unlike a texel sum).
struct GpuCopyCoverRect {
    uint32_t X = 0;
    uint32_t Y = 0;
    uint32_t Width = 0;
    uint32_t Height = 0;
};

inline bool GpuCopyRectsCoverExactly(uint32_t width, uint32_t height,
                                     const std::vector<GpuCopyCoverRect>& rects) {
    if (width == 0U || height == 0U) return false;
    const uint64_t texels = uint64_t{width} * height;
    if (texels > (16ULL << 20)) return false;  // not worth proving; allocate fresh
    std::vector<bool> covered(static_cast<size_t>(texels), false);
    uint64_t count = 0;
    for (const auto& rect : rects) {
        if (rect.X >= width || rect.Y >= height) continue;
        const uint32_t x1 = rect.Width > width - rect.X ? width : rect.X + rect.Width;
        const uint32_t y1 = rect.Height > height - rect.Y ? height : rect.Y + rect.Height;
        for (uint32_t y = rect.Y; y < y1; ++y) {
            for (uint32_t x = rect.X; x < x1; ++x) {
                const size_t index = size_t{y} * width + x;
                if (!covered[index]) {
                    covered[index] = true;
                    ++count;
                }
            }
        }
    }
    return count == texels;
}

// Sampler state key for the permanent sampler cache: every field the renderer
// writes into VkSamplerCreateInfo, compared exactly (floats bitwise).
struct GpuCopySamplerKey {
    uint32_t MagFilter = 0;
    uint32_t MinFilter = 0;
    uint32_t MipmapMode = 0;
    uint32_t AddressU = 0;
    uint32_t AddressV = 0;
    uint32_t AddressW = 0;
    uint32_t MipLodBiasBits = 0;
    uint32_t MinLodBits = 0;
    uint32_t MaxLodBits = 0;
    uint32_t AnisotropyEnable = 0;
    uint32_t MaxAnisotropyBits = 0;

    friend bool operator==(const GpuCopySamplerKey&, const GpuCopySamplerKey&) = default;
};

struct GpuCopySamplerKeyHash {
    size_t operator()(const GpuCopySamplerKey& key) const noexcept {
        uint64_t hash = 1469598103934665603ULL;
        for (uint32_t value : {key.MagFilter, key.MinFilter, key.MipmapMode, key.AddressU,
                               key.AddressV, key.AddressW, key.MipLodBiasBits, key.MinLodBits,
                               key.MaxLodBits, key.AnisotropyEnable, key.MaxAnisotropyBits}) {
            hash = (hash ^ value) * 1099511628211ULL;
        }
        return static_cast<size_t>(hash);
    }
};

} // namespace Fast
