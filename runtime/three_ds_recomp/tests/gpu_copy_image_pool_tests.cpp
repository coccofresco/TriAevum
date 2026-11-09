#include "fast/backends/gpu_copy_image_pool.h"
#include <iostream>
#include <stdexcept>

using namespace Fast;

static void Require(bool value, const char* message) {
    if (!value) {
        std::cerr << message << std::endl;
        throw std::runtime_error(message);
    }
}

static GpuCopyImagePoolKey Key(uint32_t width, uint32_t height, uint32_t usage = 0x24U,
                               uint32_t allocator = 1U) {
    GpuCopyImagePoolKey key;
    key.Width = width;
    key.Height = height;
    key.Format = 37U;  // VK_FORMAT_R8G8B8A8_UNORM
    key.Usage = usage;
    key.MipLevels = 1U;
    key.Layers = 1U;
    key.Allocator = allocator;
    return key;
}

static void KeyEquality() {
    Require(Key(256, 200) == Key(256, 200), "identical keys compare equal");
    Require(!(Key(256, 200) == Key(200, 256)), "extent is part of the key");
    Require(!(Key(64, 64, 0x24U) == Key(64, 64, 0x25U)), "usage is part of the key");
    Require(!(Key(64, 64, 0x24U, 1U) == Key(64, 64, 0x24U, 2U)), "allocator is part of the key");
    auto mips = Key(64, 64);
    mips.MipLevels = 2U;
    Require(!(mips == Key(64, 64)), "mip count is part of the key");
    auto layers = Key(64, 64);
    layers.Layers = 6U;
    Require(!(layers == Key(64, 64)), "layer count is part of the key");
    auto format = Key(64, 64);
    format.Format = 44U;
    Require(!(format == Key(64, 64)), "format is part of the key");

    GpuCopySamplerKey a;
    GpuCopySamplerKey b;
    Require(a == b && GpuCopySamplerKeyHash{}(a) == GpuCopySamplerKeyHash{}(b),
            "equal sampler keys hash equal");
    b.MaxAnisotropyBits = 1U;
    Require(!(a == b), "anisotropy is part of the sampler key");
}

static void ReuseOnlyExactKey() {
    GpuCopyImagePool<int> pool;
    int out = 0;
    Require(!pool.Acquire(Key(32, 64), out), "empty pool misses");
    Require(pool.Stats().Created == 1U, "a miss counts as a creation");
    Require(pool.Release(Key(32, 64), 7, 32 * 64 * 4, 10).empty(), "release is accepted");
    Require(pool.Entries() == 1U && pool.Bytes() == 32U * 64U * 4U, "pool holds the image");
    Require(!pool.Acquire(Key(64, 32), out), "a different extent is not reused");
    Require(pool.Acquire(Key(32, 64), out) && out == 7, "the same key is reused");
    Require(pool.Stats().Reused == 1U && pool.Stats().Created == 2U, "reuse and miss counted");
    Require(pool.Entries() == 0U && pool.Bytes() == 0U, "an acquired image leaves the pool");
    Require(!pool.Acquire(Key(32, 64), out), "one image is handed out once");
}

static void MostRecentFirst() {
    GpuCopyImagePool<int> pool;
    pool.Release(Key(8, 8), 1, 256, 1);
    pool.Release(Key(8, 8), 2, 256, 2);
    int out = 0;
    Require(pool.Acquire(Key(8, 8), out) && out == 2, "the most recently released image is reused");
    Require(pool.Acquire(Key(8, 8), out) && out == 1, "then the older one");
}

static void EntryAndByteBounds() {
    GpuCopyImagePoolLimits limits;
    limits.MaxEntries = 2;
    limits.MaxBytes = 1000;
    GpuCopyImagePool<int> pool(limits);
    Require(pool.Release(Key(1, 1), 1, 100, 1).empty(), "first fits");
    Require(pool.Release(Key(1, 1), 2, 100, 1).empty(), "second fits");
    auto evicted = pool.Release(Key(1, 1), 3, 100, 1);
    Require(evicted.size() == 1U && evicted[0].Value == 1, "entry bound evicts the oldest");
    Require(pool.Entries() == 2U && pool.Stats().Trimmed == 1U, "entry bound holds");
    evicted = pool.Release(Key(2, 2), 4, 900, 1);
    Require(evicted.size() == 1U && evicted[0].Value == 2,
            "byte bound evicts oldest first until it fits");
    Require(pool.Bytes() == 1000U && pool.Entries() == 2U, "byte bound holds at the limit");
    evicted = pool.Release(Key(1, 1), 6, 950, 1);
    Require(evicted.size() == 2U && evicted[0].Value == 3 && evicted[1].Value == 4,
            "a byte overflow can evict several entries");
    Require(pool.Bytes() == 950U && pool.Entries() == 1U, "only the newest remains");
    evicted = pool.Release(Key(4, 4), 5, 2000, 1);
    Require(evicted.size() == 1U && evicted[0].Value == 5 && pool.Stats().Rejected == 1U,
            "an image larger than the pool is returned for destruction");
    Require(pool.Stats().PeakBytes <= limits.MaxBytes, "peak never exceeds the bound");
}

static void IdleTrimAndReleaseAll() {
    GpuCopyImagePoolLimits limits;
    limits.MaxIdleFrames = 10;
    GpuCopyImagePool<int> pool(limits);
    pool.Release(Key(1, 1), 1, 4, 100);
    pool.Release(Key(2, 2), 2, 16, 105);
    Require(pool.Trim(110).empty(), "nothing is idle yet");
    auto evicted = pool.Trim(111);
    Require(evicted.size() == 1U && evicted[0].Value == 1, "idle entries are trimmed");
    Require(pool.Entries() == 1U && pool.Bytes() == 16U, "trim updates the bytes");
    evicted = pool.ReleaseAll();
    Require(evicted.size() == 1U && evicted[0].Value == 2, "release all returns everything");
    Require(pool.Entries() == 0U && pool.Bytes() == 0U, "pool is empty after release all");
    Require(pool.Stats().Trimmed == 2U, "trim and release all are counted");
}

static void Disabled() {
    GpuCopyImagePool<int> pool;
    pool.SetEnabled(false);
    auto evicted = pool.Release(Key(1, 1), 9, 4, 1);
    Require(evicted.size() == 1U && evicted[0].Value == 9, "a disabled pool destroys at once");
    int out = 0;
    Require(!pool.Acquire(Key(1, 1), out), "a disabled pool never reuses");
}

static void Coverage() {
    Require(GpuCopyRectsCoverExactly(4, 4, {{0, 0, 4, 4}}), "one full rect covers");
    Require(GpuCopyRectsCoverExactly(4, 4, {{0, 0, 4, 2}, {0, 2, 4, 2}}), "two halves cover");
    Require(!GpuCopyRectsCoverExactly(4, 4, {{0, 0, 4, 2}, {0, 1, 4, 2}}),
            "overlapping rects with the full texel count still leave a gap");
    Require(!GpuCopyRectsCoverExactly(4, 4, {{0, 0, 4, 3}}), "a partial rect does not cover");
    Require(GpuCopyRectsCoverExactly(4, 4, {{0, 0, 9, 9}}), "rects are clamped to the extent");
    Require(!GpuCopyRectsCoverExactly(0, 4, {{0, 0, 4, 4}}), "an empty extent never covers");
}

int main() {
    try {
        KeyEquality();
        ReuseOnlyExactKey();
        MostRecentFirst();
        EntryAndByteBounds();
        IdleTrimAndReleaseAll();
        Disabled();
        Coverage();
    } catch (const std::exception&) {
        return 1;
    }
    std::cout << "gpu_copy_image_pool_tests passed" << std::endl;
    return 0;
}
