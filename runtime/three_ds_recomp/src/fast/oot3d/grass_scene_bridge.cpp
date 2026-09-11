#include "fast/oot3d/grass_scene_bridge.h"
#include "fast/oot3d/grass_texture_source_cache.h"

#include <algorithm>

namespace Fast::Oot3d {

GrassSceneBridge& GrassSceneBridge::Instance() {
    static GrassSceneBridge bridge;
    return bridge;
}

void GrassSceneBridge::Publish(GrassSceneMesh mesh) {
    if (mesh.GeometryId == 0 || mesh.InstanceId == 0 ||
        mesh.Vertices == nullptr || mesh.Indices == nullptr ||
        mesh.Vertices->empty() || mesh.Indices->empty()) return;
    mesh.ObservedTextureHash =
        GrassTextureSourceCache::Instance().ResolveObservedHash(
            mesh.TextureHash, mesh.TextureWidth,
            mesh.TextureHeight);
    std::scoped_lock lock(mMutex);
    ResolveStaticAnchor(mesh);
    if (const auto found = mMeshes.find(mesh.InstanceId);
        found != mMeshes.end() && mesh.PreserveWorldAnchor &&
        found->second.PreserveWorldAnchor &&
        found->second.GeometryId == mesh.GeometryId &&
        found->second.AnchorVersion == mesh.AnchorVersion &&
        found->second.RenderTargetNamespace == mesh.RenderTargetNamespace &&
        found->second.TextureHash == mesh.TextureHash &&
        found->second.ObservedTextureHash ==
            mesh.ObservedTextureHash &&
        found->second.TextureWidth == mesh.TextureWidth &&
        found->second.TextureHeight == mesh.TextureHeight &&
        found->second.MapperSlot == mesh.MapperSlot) {
        if (found->second.ContentVersion != mesh.ContentVersion) {
            // UV/material content can change without moving a static room
            // surface. Refresh that payload while retaining the established
            // world anchor.
            mesh.ModelToWorld = found->second.ModelToWorld;
            mMeshes.insert_or_assign(
                mesh.InstanceId, std::move(mesh));
            return;
        }
        // Vertex/index payload and the initial model-to-world anchor are
        // immutable for renderer-owned static grass. The exact PICA clip
        // transform is camera-dependent, so refresh it alongside liveness.
        found->second.FrameId = mesh.FrameId;
        found->second.SubmissionId = mesh.SubmissionId;
        found->second.RenderTargetNamespace =
            mesh.RenderTargetNamespace;
        found->second.FramebufferColorPhysicalAddress =
            mesh.FramebufferColorPhysicalAddress;
        found->second.PicaModelToClip = mesh.PicaModelToClip;
        found->second.PicaModelToClipAvailable =
            mesh.PicaModelToClipAvailable;
        found->second.MaterialWrapS = mesh.MaterialWrapS;
        found->second.MaterialWrapT = mesh.MaterialWrapT;
        found->second.Shading = mesh.Shading;
        return;
    }
    if (const auto found = mMeshes.find(mesh.InstanceId);
        found != mMeshes.end()) {
        UnindexMesh(found->second);
    }
    const uint64_t instanceId = mesh.InstanceId;
    mMeshes.insert_or_assign(instanceId, std::move(mesh));
    IndexMesh(mMeshes.at(instanceId));
}

void GrassSceneBridge::RemoveInstance(uint64_t instanceId) {
    std::scoped_lock lock(mMutex);
    mStaticAnchors.erase(instanceId);
    const auto found = mMeshes.find(instanceId);
    if (found == mMeshes.end()) {
        return;
    }
    UnindexMesh(found->second);
    mMeshes.erase(found);
}

void GrassSceneBridge::RemoveRoom(uint64_t roomId) {
    std::scoped_lock lock(mMutex);
    std::erase_if(mStaticAnchors, [roomId](const auto& entry) {
        return entry.second.RenderTargetNamespace == roomId;
    });
    for (auto entry = mMeshes.begin(); entry != mMeshes.end();) {
        if (entry->second.RenderTargetNamespace != roomId) {
            ++entry;
            continue;
        }
        UnindexMesh(entry->second);
        entry = mMeshes.erase(entry);
    }
}

void GrassSceneBridge::PruneBeforeFrame(uint64_t frameId) {
    std::scoped_lock lock(mMutex);
    for (auto entry = mMeshes.begin(); entry != mMeshes.end();) {
        if (entry->second.FrameId == 0 ||
            entry->second.FrameId >= frameId) {
            ++entry;
            continue;
        }
        UnindexMesh(entry->second);
        entry = mMeshes.erase(entry);
    }
}

std::vector<GrassSceneMesh> GrassSceneBridge::Matching(
    const GrassTextureSelector& selector,
    const GrassSceneScope* scope) const {
    return Match(selector, scope).ScopedMeshes;
}

GrassSceneMatch GrassSceneBridge::Match(
    const GrassTextureSelector& selector,
    const GrassSceneScope* scope) const {
    std::scoped_lock lock(mMutex);
    GrassSceneMatch result;
    const auto indexed =
        mInstancesByTexture.find(selector.Rgba8Hash);
    if (indexed == mInstancesByTexture.end()) {
        return result;
    }
    result.ScopedMeshes.reserve(indexed->second.size());
    for (const uint64_t instanceId : indexed->second) {
        const auto found = mMeshes.find(instanceId);
        if (found == mMeshes.end()) {
            continue;
        }
        const auto& mesh = found->second;
        if ((selector.MapperSlotMask &
             (1U << mesh.MapperSlot)) == 0U ||
            (selector.Width != 0U &&
             selector.Width != mesh.TextureWidth) ||
            (selector.Height != 0U &&
             selector.Height != mesh.TextureHeight)) {
            continue;
        }
        ++result.MatchingMeshes;
        if (scope != nullptr &&
            (mesh.FrameId != scope->FrameId ||
             mesh.RenderTargetNamespace !=
                 scope->RenderTargetNamespace ||
             mesh.FramebufferColorPhysicalAddress !=
                 scope->FramebufferColorPhysicalAddress)) {
            continue;
        }
        result.ScopedMeshes.push_back(mesh);
    }
    // Hash-table insertion/pruning order is not a placement or upload identity.
    std::sort(result.ScopedMeshes.begin(), result.ScopedMeshes.end(),
              [](const auto& a, const auto& b) { return a.InstanceId < b.InstanceId; });
    return result;
}

size_t GrassSceneBridge::Size() const {
    std::scoped_lock lock(mMutex);
    return mMeshes.size();
}

std::vector<GrassTextureSelector> GrassSceneBridge::AvailableTextures() const {
    std::scoped_lock lock(mMutex);
    std::vector<GrassTextureSelector> result;
    for (const auto& [instanceId, mesh] : mMeshes) {
        GrassTextureSelector selector;
        selector.Rgba8Hash = mesh.ObservedTextureHash;
        selector.Width = mesh.TextureWidth;
        selector.Height = mesh.TextureHeight;
        selector.MapperSlotMask = static_cast<uint8_t>(1U << mesh.MapperSlot);
        if (std::none_of(result.begin(), result.end(), [&](const auto& value) {
                return value.Rgba8Hash == selector.Rgba8Hash &&
                       value.Width == selector.Width &&
                       value.Height == selector.Height;
            })) {
            result.push_back(selector);
        }
    }
    return result;
}

void GrassSceneBridge::Clear() {
    std::scoped_lock lock(mMutex);
    mMeshes.clear();
    mInstancesByTexture.clear();
    mStaticAnchors.clear();
    mAnchorUse = 0;
}

void GrassSceneBridge::ResolveStaticAnchor(GrassSceneMesh& mesh) {
    if (!mesh.PreserveWorldAnchor) {
        mStaticAnchors.erase(mesh.InstanceId);
        return;
    }
    const auto found = mStaticAnchors.find(mesh.InstanceId);
    if (found != mStaticAnchors.end()) {
        auto& anchor = found->second;
        if (anchor.GeometryId == mesh.GeometryId &&
            anchor.AnchorVersion == mesh.AnchorVersion &&
            anchor.TextureHash == mesh.TextureHash &&
            anchor.RenderTargetNamespace == mesh.RenderTargetNamespace &&
            anchor.TextureWidth == mesh.TextureWidth &&
            anchor.TextureHeight == mesh.TextureHeight &&
            anchor.MapperSlot == mesh.MapperSlot) {
            mesh.ModelToWorld = anchor.ModelToWorld;
            anchor.LastUse = ++mAnchorUse;
            return;
        }
        mStaticAnchors.erase(found);
    }
    // Bound inactive scene metadata independently of presentation rate.
    constexpr size_t capacity = 4096;
    if (mStaticAnchors.size() >= capacity) {
        const auto oldest = std::min_element(
            mStaticAnchors.begin(), mStaticAnchors.end(),
            [](const auto& a, const auto& b) {
                return a.second.LastUse < b.second.LastUse;
            });
        mStaticAnchors.erase(oldest);
    }
    mStaticAnchors.emplace(mesh.InstanceId, StaticAnchor{
        mesh.GeometryId, mesh.AnchorVersion, mesh.TextureHash, mesh.RenderTargetNamespace,
        ++mAnchorUse, mesh.TextureWidth, mesh.TextureHeight,
        mesh.MapperSlot, mesh.ModelToWorld});
}

void GrassSceneBridge::IndexMesh(const GrassSceneMesh& mesh) {
    mInstancesByTexture[mesh.ObservedTextureHash].insert(
        mesh.InstanceId);
}

void GrassSceneBridge::UnindexMesh(const GrassSceneMesh& mesh) {
    const auto indexed =
        mInstancesByTexture.find(mesh.ObservedTextureHash);
    if (indexed == mInstancesByTexture.end()) {
        return;
    }
    indexed->second.erase(mesh.InstanceId);
    if (indexed->second.empty()) {
        mInstancesByTexture.erase(indexed);
    }
}

} // namespace Fast::Oot3d
