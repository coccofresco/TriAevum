#include "fast/oot3d/texture_catalog_runtime.h"

#include <algorithm>

namespace Fast::Oot3d {

TextureCatalogRuntime& TextureCatalogRuntime::Instance() {
    static TextureCatalogRuntime runtime;
    return runtime;
}

void TextureCatalogRuntime::Observe(uint64_t contentHash,
                                    uint32_t physicalAddress,
                                    uint16_t width, uint16_t height,
                                    uint8_t nativeFormat) {
    std::scoped_lock lock(mMutex);
    const auto found = std::find_if(
        mEntries.begin(), mEntries.end(), [&](const auto& entry) {
            return entry.ContentHash == contentHash &&
                   entry.Width == width && entry.Height == height &&
                   entry.NativeFormat == nativeFormat;
        });
    if (found != mEntries.end()) {
        found->PhysicalAddress = physicalAddress;
        ++found->Observations;
        return;
    }
    mEntries.push_back({contentHash, physicalAddress, width, height,
                        nativeFormat, 1});
}

std::vector<TextureCatalogEntry> TextureCatalogRuntime::Snapshot() const {
    std::scoped_lock lock(mMutex);
    auto result = mEntries;
    std::sort(result.begin(), result.end(), [](const auto& left,
                                               const auto& right) {
        return left.Observations != right.Observations
                   ? left.Observations > right.Observations
                   : left.ContentHash < right.ContentHash;
    });
    return result;
}

void TextureCatalogRuntime::Clear() {
    std::scoped_lock lock(mMutex);
    mEntries.clear();
}

} // namespace Fast::Oot3d
