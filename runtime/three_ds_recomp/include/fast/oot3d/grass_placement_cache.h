#pragma once

#include "fast/oot3d/grass_surface_extractor.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace Fast::Oot3d {

struct GrassPlacementKey {
    uint64_t GeometryId = 0;
    uint64_t ContentVersion = 0;
    uint64_t TextureHash = 0;
    uint32_t RuleId = 0;
    auto operator<=>(const GrassPlacementKey&) const = default;
};

struct GrassPlacementKeyHash {
    size_t operator()(const GrassPlacementKey& key) const noexcept;
};

class GrassPlacementCache final {
  public:
    using Placement = std::shared_ptr<const GrassPlacementSet>;

    [[nodiscard]] Placement Replace(
        GrassPlacementKey key, GrassPlacementSet placement);
    [[nodiscard]] Placement Find(const GrassPlacementKey& key) const;
    void InvalidateGeometry(uint64_t geometryId);
    void Clear();

  private:
    mutable std::mutex mMutex;
    std::unordered_map<GrassPlacementKey, Placement,
                       GrassPlacementKeyHash> mEntries;
};

} // namespace Fast::Oot3d
