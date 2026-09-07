#include "fast/oot3d/grass_placement_cache.h"

namespace Fast::Oot3d {

size_t GrassPlacementKeyHash::operator()(
    const GrassPlacementKey& key) const noexcept {
    size_t hash = static_cast<size_t>(key.GeometryId);
    hash ^= static_cast<size_t>(key.ContentVersion) + 0x9e3779b9U +
            (hash << 6U) + (hash >> 2U);
    hash ^= static_cast<size_t>(key.TextureHash) + 0x9e3779b9U +
            (hash << 6U) + (hash >> 2U);
    hash ^= static_cast<size_t>(key.RuleId) + 0x9e3779b9U +
            (hash << 6U) + (hash >> 2U);
    return hash;
}

GrassPlacementCache::Placement GrassPlacementCache::Replace(
    GrassPlacementKey key, GrassPlacementSet placement) {
    auto shared = std::make_shared<const GrassPlacementSet>(
        std::move(placement));
    std::scoped_lock lock(mMutex);
    std::erase_if(mEntries, [&key](const auto& entry) {
        return entry.first.GeometryId == key.GeometryId &&
               entry.first.TextureHash == key.TextureHash &&
               entry.first.RuleId == key.RuleId &&
               entry.first != key;
    });
    mEntries.insert_or_assign(key, shared);
    return shared;
}

GrassPlacementCache::Placement GrassPlacementCache::Find(
    const GrassPlacementKey& key) const {
    std::scoped_lock lock(mMutex);
    const auto found = mEntries.find(key);
    return found == mEntries.end() ? nullptr : found->second;
}

void GrassPlacementCache::InvalidateGeometry(uint64_t geometryId) {
    std::scoped_lock lock(mMutex);
    std::erase_if(mEntries, [geometryId](const auto& entry) {
        return entry.first.GeometryId == geometryId;
    });
}

void GrassPlacementCache::Clear() {
    std::scoped_lock lock(mMutex);
    mEntries.clear();
}

} // namespace Fast::Oot3d
