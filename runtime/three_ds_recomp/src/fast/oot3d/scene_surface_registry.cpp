#include "fast/oot3d/scene_surface_registry.h"

namespace Fast::Oot3d {

size_t SceneSurfaceKeyHash::operator()(const SceneSurfaceKey& key) const noexcept {
    size_t hash = std::hash<uint64_t>{}(key.TargetNamespace);
    hash ^= std::hash<uint32_t>{}(key.PhysicalAddress) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    hash ^= std::hash<uint8_t>{}(static_cast<uint8_t>(key.Kind)) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    return hash;
}

SceneSurfaceRegistry::SceneSurfaceRegistry(RetireCallback retire) : mRetire(std::move(retire)) {}

const SceneSurface& SceneSurfaceRegistry::Publish(SceneSurface surface) {
    if (auto existing = mSurfaces.find(surface.Key); existing != mSurfaces.end()) {
        if (existing->second.NativeImage == surface.NativeImage &&
            existing->second.Width == surface.Width && existing->second.Height == surface.Height &&
            existing->second.Format == surface.Format &&
            existing->second.Sampleable == surface.Sampleable &&
            existing->second.ColorEncoding == surface.ColorEncoding) {
            return existing->second;
        }
        NotifyRetired(existing->second);
    }
    surface.Generation = mNextGeneration++;
    return mSurfaces.insert_or_assign(surface.Key, surface).first->second;
}

std::optional<SceneSurface> SceneSurfaceRegistry::Find(const SceneSurfaceKey& key) const {
    const auto it = mSurfaces.find(key);
    return it == mSurfaces.end() ? std::nullopt : std::optional<SceneSurface>(it->second);
}

bool SceneSurfaceRegistry::Retire(const SceneSurfaceKey& key) {
    const auto it = mSurfaces.find(key);
    if (it == mSurfaces.end()) return false;
    NotifyRetired(it->second);
    mSurfaces.erase(it);
    return true;
}

void SceneSurfaceRegistry::RetireNamespace(uint64_t targetNamespace) {
    for (auto it = mSurfaces.begin(); it != mSurfaces.end();) {
        if (it->first.TargetNamespace == targetNamespace) {
            NotifyRetired(it->second);
            it = mSurfaces.erase(it);
        } else ++it;
    }
}

void SceneSurfaceRegistry::Clear() {
    for (const auto& [_, surface] : mSurfaces) NotifyRetired(surface);
    mSurfaces.clear();
}

void SceneSurfaceRegistry::NotifyRetired(const SceneSurface& surface) {
    if (mRetire) mRetire(surface);
}

} // namespace Fast::Oot3d
