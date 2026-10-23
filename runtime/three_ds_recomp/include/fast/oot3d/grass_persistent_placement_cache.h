#pragma once
#include "fast/oot3d/grass_async_placement_builder.h"
#include <filesystem>

namespace Fast::Oot3d {
// Bump whenever extraction, world packing, clustering or overlay semantics change.
inline constexpr uint32_t kGrassPlacementDiskVersion = 1;
struct GrassPlacementDiskKey {
    uint64_t Content = 0, Settings = 0;
    bool operator==(const GrassPlacementDiskKey&) const = default;
};
[[nodiscard]] GrassPlacementDiskKey GrassPlacementPersistentKey(const GrassAsyncPlacementRequest& request);
// Phases of the calling thread's last Load, for entry diagnostics.
struct GrassPlacementDiskLoadTiming {
    double MutexWaitMilliseconds = 0, ReadMilliseconds = 0, ValidateMilliseconds = 0;
    uint64_t Bytes = 0;
};
[[nodiscard]] GrassPlacementDiskLoadTiming& GrassLastPlacementDiskLoadTiming() noexcept;
// Whole final placement, including baked RGB, visibility hierarchy and shadow
// support. No texture, mask, vertex pointer or process-local identity is stored.
class GrassPersistentPlacementCache {
  public:
    explicit GrassPersistentPlacementCache(std::filesystem::path directory = {},
                                           uint64_t byteLimit = 1024ULL * 1024 * 1024, size_t entryLimit = 128)
        : mDirectory(std::move(directory)), mByteLimit(byteLimit), mEntryLimit(entryLimit) {
    }
    [[nodiscard]] std::shared_ptr<GrassWorldPlacement> Load(GrassPlacementDiskKey key, bool* rejected = nullptr) const;
    [[nodiscard]] bool Store(GrassPlacementDiskKey key, const GrassWorldPlacement& placement) const;
    [[nodiscard]] std::filesystem::path Path(GrassPlacementDiskKey key) const;

  private:
    std::filesystem::path mDirectory;
    uint64_t mByteLimit;
    size_t mEntryLimit;
};
} // namespace Fast::Oot3d
