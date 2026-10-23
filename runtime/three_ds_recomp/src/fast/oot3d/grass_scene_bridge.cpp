#include "fast/oot3d/grass_scene_bridge.h"
#include "fast/oot3d/grass_texture_source_cache.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace Fast::Oot3d {

GrassSceneBridge& GrassSceneBridge::Instance() {
    static GrassSceneBridge bridge;
    return bridge;
}

void GrassSceneBridge::Publish(GrassSceneMesh mesh) {
    if (mesh.GeometryId == 0 || mesh.InstanceId == 0 ||
        mesh.Vertices == nullptr || mesh.Indices == nullptr ||
        mesh.Vertices->empty() || mesh.Indices->empty()) return;
    if (!mesh.ObservedTextureResolved)
        mesh.ObservedTextureHash = GrassTextureSourceCache::Instance().ResolveObservedHash(
            mesh.TextureHash, mesh.TextureWidth, mesh.TextureHeight);
    std::scoped_lock lock(mMutex);
    mLatestFrame = std::max(mLatestFrame, mesh.FrameId);
    if (mesh.Overlay) {
        if (mOverlayHistory.size() >= 4096 && !mOverlayHistory.contains(mesh.InstanceId)) {
            const auto oldest =
                std::min_element(mOverlayHistory.begin(), mOverlayHistory.end(),
                                 [](const auto& a, const auto& b) { return a.second.LastFrame < b.second.LastFrame; });
            mOverlayHistory.erase(oldest);
        }
        auto [entry, inserted] = mOverlayHistory.try_emplace(
            mesh.InstanceId, OverlayHistory{ mesh.CoordinateVersion, mesh.AnchorVersion, mesh.RenderTargetNamespace,
                                             mesh.FrameId, mesh.ModelToWorld, false });
        auto& history = entry->second;
        history.LastFrame = mesh.FrameId;
        bool moved = false;
        if (!mesh.TransformBakedIntoVertices)
            for (size_t i = 0; i < 16; ++i)
                moved |= std::abs(history.ModelToWorld[i] - mesh.ModelToWorld[i]) > (i % 4 == 3 ? 0.05F : 1e-4F);
        if (!inserted && (moved || history.CoordinateVersion != mesh.CoordinateVersion ||
                          history.AnchorVersion != mesh.AnchorVersion)) {
            if (!history.Animated && std::getenv("TRIAEVUM_GRASS_MASK_DIAGNOSTICS"))
                std::fprintf(stderr, "TRIAEVUM_GRASS_MASK excluded=animated-geometry-or-uv geometry=%016llx\n",
                             static_cast<unsigned long long>(mesh.GeometryId));
            history.Animated = true;
        }
        if (history.Animated) {
            if (const auto found = mMeshes.find(mesh.InstanceId); found != mMeshes.end()) {
                UnindexMesh(found->second);
                mMeshes.erase(found);
            }
            return;
        }
    }
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
        found->second.DrawOrder = mesh.DrawOrder;
        found->second.SequenceId = mesh.SequenceId;
        found->second.Overlay = mesh.Overlay;
        found->second.ConfiguredRuleHash = mesh.ConfiguredRuleHash;
        found->second.ConfiguredRuleId = mesh.ConfiguredRuleId;
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
    mOverlayHistory.erase(instanceId);
    mMaskMeasurements.erase(instanceId);
    mOverlaySets.erase(instanceId);
    mPendingOverlayPlacements.erase(instanceId);
    mOverlaySetScopes.erase(instanceId);
    const auto found = mMeshes.find(instanceId);
    if (found == mMeshes.end()) {
        return;
    }
    UnindexMesh(found->second);
    mMeshes.erase(found);
}

void GrassSceneBridge::RemoveRoom(uint64_t roomId) {
    std::scoped_lock lock(mMutex);
    std::erase_if(mOverlayHistory, [roomId](const auto& entry) { return entry.second.Namespace == roomId; });
    mOverlaySets.clear();
    mPendingOverlayPlacements.clear();
    mMaskMeasurements.clear();
    ++mMeasurementEpoch;
    mMaskStatisticsFrame = 0;
    mOverlaySetScopes.clear();
    mHighlightedMask.reset();
    std::erase_if(mStaticAnchors, [roomId](const auto& entry) { return entry.second.RenderTargetNamespace == roomId; });
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
    mLatestFrame = std::max(mLatestFrame, frameId);
    for (auto it = mOverlaySetScopes.begin(); it != mOverlaySetScopes.end();) {
        if (it->second.FrameId < frameId) {
            mOverlaySets.erase(it->first);
            mPendingOverlayPlacements.erase(it->first);
            mMaskMeasurements.erase(it->first);
            it = mOverlaySetScopes.erase(it);
        } else
            ++it;
    }
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
        if (mesh.Overlay)
            continue;
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
    mOverlayHistory.clear();
    mOverlaySets.clear();
    mPendingOverlayPlacements.clear();
    mMaskMeasurements.clear();
    ++mMeasurementEpoch;
    mMaskStatisticsFrame = 0;
    mOverlaySetScopes.clear();
    mHighlightedMask.reset();
    mOverlayCount = 0;
    mLatestFrame = 0;
}

void GrassSceneBridge::ResolveStaticAnchor(GrassSceneMesh& mesh) {
    // A world instance keeps one anchor in every render target it is drawn
    // into, so a redrawn scene does not re-derive a slightly different
    // matrix (and new placements) from another target's projection.
    const bool worldInstance = mesh.WorldInstanceId != 0U;
    const uint64_t key = worldInstance ? mesh.WorldInstanceId : mesh.InstanceId;
    if (!mesh.PreserveWorldAnchor) {
        mStaticAnchors.erase(key);
        return;
    }
    const auto found = mStaticAnchors.find(key);
    if (found != mStaticAnchors.end()) {
        auto& anchor = found->second;
        if (anchor.GeometryId == mesh.GeometryId &&
            anchor.AnchorVersion == mesh.AnchorVersion &&
            anchor.TextureHash == mesh.TextureHash &&
            (worldInstance || anchor.RenderTargetNamespace == mesh.RenderTargetNamespace) &&
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
    mStaticAnchors.emplace(key, StaticAnchor{
        mesh.GeometryId, mesh.AnchorVersion, mesh.TextureHash, mesh.RenderTargetNamespace,
        ++mAnchorUse, mesh.TextureWidth, mesh.TextureHeight,
        mesh.MapperSlot, mesh.ModelToWorld});
}

void GrassSceneBridge::IndexMesh(const GrassSceneMesh& mesh) {
    if (mesh.Overlay) {
        ++mOverlayCount;
        return;
    }
    mInstancesByTexture[mesh.ObservedTextureHash].insert(
        mesh.InstanceId);
}

void GrassSceneBridge::UnindexMesh(const GrassSceneMesh& mesh) {
    if (mesh.Overlay) {
        --mOverlayCount;
        return;
    }
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

std::shared_ptr<const GrassOverlaySet> GrassSceneBridge::OverlaysFor(const GrassSceneMesh& source) {
    std::scoped_lock lock(mMutex);
    if (!mOverlayCount || !source.Vertices || !source.Indices) {
        mOverlaySets.erase(source.InstanceId);
        mPendingOverlayPlacements.erase(source.InstanceId);
        mMaskMeasurements.erase(source.InstanceId);
        mOverlaySetScopes.erase(source.InstanceId);
        return {};
    }
    const auto start = std::chrono::steady_clock::now();
    std::vector<const GrassSceneMesh*> overlays;
    for (const auto& [id, mesh] : mMeshes)
        if (mesh.Overlay && mesh.FrameId == source.FrameId && mesh.SequenceId == source.SequenceId &&
            mesh.DrawOrder > source.DrawOrder && mesh.RenderTargetNamespace == source.RenderTargetNamespace &&
            mesh.FramebufferColorPhysicalAddress == source.FramebufferColorPhysicalAddress)
            overlays.push_back(&mesh);
    if (overlays.empty()) {
        mOverlaySets.erase(source.InstanceId);
        mPendingOverlayPlacements.erase(source.InstanceId);
        mMaskMeasurements.erase(source.InstanceId);
        mOverlaySetScopes.erase(source.InstanceId);
        return {};
    }
    mOverlaySetScopes[source.InstanceId] = { source.FrameId, source.RenderTargetNamespace,
                                             source.FramebufferColorPhysicalAddress };
    std::sort(overlays.begin(), overlays.end(), [](auto a, auto b) { return a->InstanceId < b->InstanceId; });
    const auto mix = [](uint64_t& hash, uint64_t x) { hash ^= x + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2); };
    uint64_t sourceVersion = 14695981039346656037ULL;
    mix(sourceVersion, kGrassOverlayVersion);
    mix(sourceVersion, source.ContentVersion);
    mix(sourceVersion, source.TransformBakedIntoVertices);
    for (float x : source.ModelToWorld)
        mix(sourceVersion, std::bit_cast<uint32_t>(x));
    uint64_t version = sourceVersion, placementVersion = sourceVersion;
    for (auto mesh : overlays) {
        uint64_t overlay = mesh->WorldInstanceId != 0U ? mesh->WorldInstanceId : mesh->InstanceId;
        mix(overlay, mesh->ContentVersion);
        mix(overlay, mesh->Overlay->Version);
        mix(overlay, mesh->TransformBakedIntoVertices);
        for (float x : mesh->ModelToWorld)
            mix(overlay, std::bit_cast<uint32_t>(x));
        mix(version, overlay);
        if (GrassOverlayCanCover(*mesh->Overlay))
            mix(placementVersion, overlay);
    }
    const auto previous = mOverlaySets.find(source.InstanceId);
    if (previous != mOverlaySets.end() && previous->second->Version == version) {
        QueueMaskMeasurement(source, overlays, previous->second);
        return previous->second->Triangles.empty() ? nullptr : previous->second;
    }
    const bool sameSource = previous != mOverlaySets.end() && previous->second->SourceVersion == sourceVersion;
    const bool overlaysChanged = sameSource && previous->second->PlacementVersion != placementVersion;
    if (overlaysChanged) {
        // The same source under changed covering overlays. A change right
        // after another waits until it settles; an animated mask never does
        // and keeps its last settled placement.
        const auto changed = previous->second->PlacementChangedFrame;
        const auto pending = mPendingOverlayPlacements.find(source.InstanceId);
        if (pending != mPendingOverlayPlacements.end() ||
            (changed != 0 && source.FrameId < changed + kGrassOverlaySettleFrames)) {
            auto& waiting = mPendingOverlayPlacements[source.InstanceId];
            if (waiting.Version != placementVersion)
                waiting = { placementVersion, source.FrameId };
            if (source.FrameId < waiting.Since + kGrassOverlaySettleFrames)
                return previous->second->Triangles.empty() ? nullptr : previous->second;
        }
    }
    mPendingOverlayPlacements.erase(source.InstanceId);
    auto result = std::make_shared<GrassOverlaySet>();
    result->Version = version ? version : 1;
    result->PlacementVersion = placementVersion ? placementVersion : 1;
    result->SourceVersion = sourceVersion;
    result->PlacementChangedFrame = overlaysChanged ? std::max<uint64_t>(source.FrameId, 1)
                                    : sameSource    ? previous->second->PlacementChangedFrame
                                                    : 0;
    const auto world = [](const GrassSceneMesh& mesh, const std::array<float, 3>& p) {
        if (mesh.TransformBakedIntoVertices)
            return p;
        const auto& m = mesh.ModelToWorld;
        const float w = m[12] * p[0] + m[13] * p[1] + m[14] * p[2] + m[15];
        const float inv = std::abs(w) > 1e-6F ? 1 / w : 1;
        return std::array<float, 3>{ (m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3]) * inv,
                                     (m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7]) * inv,
                                     (m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11]) * inv };
    };
    std::array<float, 3> low{ INFINITY, INFINITY, INFINITY }, high{ -INFINITY, -INFINITY, -INFINITY };
    for (const auto& v : *source.Vertices) {
        const auto p = world(source, v.Position);
        for (size_t a = 0; a < 3; ++a) {
            low[a] = std::min(low[a], p[a]);
            high[a] = std::max(high[a], p[a]);
        }
    }
    const bool diagnostics = std::getenv("TRIAEVUM_GRASS_MASK_DIAGNOSTICS") != nullptr;
    for (auto mesh : overlays) {
        const auto first = result->Triangles.size();
        for (size_t i = 0; i + 2 < mesh->Indices->size(); i += 3) {
            GrassOverlayTriangle t;
            t.Draw = mesh->InstanceId;
            t.Alpha = mesh->Overlay;
            bool valid = true;
            for (size_t j = 0; j < 3; ++j) {
                const auto index = (*mesh->Indices)[i + j];
                if (index >= mesh->Vertices->size() || (mesh->Alphas && index >= mesh->Alphas->size())) {
                    valid = false;
                    break;
                }
                const auto& v = (*mesh->Vertices)[index];
                t.Vertices[j] = { world(*mesh, v.Position), v.Uv, mesh->Alphas ? mesh->Alphas->at(index) : 1.0F };
            }
            if (!valid || !PrepareGrassOverlayTriangle(t))
                continue;
            for (size_t a = 0; a < 3; ++a)
                valid &= low[a] <= t.High[a] + kGrassOverlayPlaneTolerance &&
                         high[a] >= t.Low[a] - kGrassOverlayPlaneTolerance;
            if (valid)
                result->Triangles.push_back(std::move(t));
        }
        if (diagnostics && result->Triangles.size() != first)
            std::fprintf(
                stderr,
                "TRIAEVUM_GRASS_MASK pair source_order=%llu source_submission=%llu source_geometry=%016llx "
                "source_native=%016llx source_texture_hash=%016llx configured_rule_hash=%016llx configured_rule_id=%u "
                "mask_order=%llu mask_submission=%llu mask_geometry=%016llx mask_native=%016llx mask_rule_hash=%016llx "
                "candidate_triangles=%zu\n",
                static_cast<unsigned long long>(source.DrawOrder), static_cast<unsigned long long>(source.SubmissionId),
                static_cast<unsigned long long>(source.GeometryId),
                static_cast<unsigned long long>(source.NativeTextureIdentity),
                static_cast<unsigned long long>(source.ObservedTextureHash),
                static_cast<unsigned long long>(source.ConfiguredRuleHash), source.ConfiguredRuleId,
                static_cast<unsigned long long>(mesh->DrawOrder), static_cast<unsigned long long>(mesh->SubmissionId),
                static_cast<unsigned long long>(mesh->GeometryId),
                static_cast<unsigned long long>(mesh->NativeTextureIdentity),
                static_cast<unsigned long long>(mesh->ObservedTextureHash), result->Triangles.size() - first);
    }
    if (diagnostics)
        std::fprintf(stderr,
                     "TRIAEVUM_GRASS_MASK prepare source=%016llx overlays=%zu triangles=%zu ms=%.3f version=%016llx\n",
                     static_cast<unsigned long long>(source.GeometryId), overlays.size(), result->Triangles.size(),
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(),
                     static_cast<unsigned long long>(result->Version));
    if (mOverlaySets.size() > 256) {
        mOverlaySets.clear();
        mPendingOverlayPlacements.clear();
        mMaskMeasurements.clear();
        ++mMeasurementEpoch; // immutable worker references remain valid
        mOverlaySetScopes.clear();
        mOverlaySetScopes[source.InstanceId] = { source.FrameId, source.RenderTargetNamespace,
                                                 source.FramebufferColorPhysicalAddress };
    }
    mOverlaySets[source.InstanceId] = result;
    QueueMaskMeasurement(source, overlays, result);
    return result->Triangles.empty() ? nullptr : result;
}

void GrassSceneBridge::CollectMaskMeasurement() const {
    if (!mMaskMeasurement.valid() || mMaskMeasurement.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    auto measurement = mMaskMeasurement.get();
    const auto found = mOverlaySets.find(measurement.Instance);
    if (measurement.Epoch == mMeasurementEpoch && found != mOverlaySets.end() &&
        found->second->Version == measurement.Version)
        mMaskMeasurements.insert_or_assign(measurement.Instance, std::move(measurement));
}

void GrassSceneBridge::QueueMaskMeasurement(const GrassSceneMesh& source,
                                            const std::vector<const GrassSceneMesh*>& overlays,
                                            std::shared_ptr<const GrassOverlaySet> set) {
    CollectMaskMeasurement();
    if ((mMaskStatisticsFrame == 0 || mMaskStatisticsFrame + 2 < mLatestFrame) &&
        !std::getenv("TRIAEVUM_GRASS_MASK_DIAGNOSTICS"))
        return;
    if (mMaskMeasurement.valid())
        return; // One bounded editor statistics job.
    if (const auto found = mMaskMeasurements.find(source.InstanceId);
        found != mMaskMeasurements.end() && found->second.Version == set->Version)
        return;
    mMaskMeasurements.erase(source.InstanceId);
    std::vector<GrassSceneMesh> captured;
    captured.reserve(overlays.size());
    for (auto mesh : overlays) {
        captured.push_back(*mesh);
        captured.back().Overlay = std::make_shared<const GrassOverlayAlpha>(*mesh->Overlay);
    }
    // Histogram sampling is editor work, not a synchronous placement rebuild.
    // Only immutable CPU snapshots cross this boundary; no renderer/UI state.
    mMaskMeasurement =
        std::async(std::launch::async, [source, captured = std::move(captured), set = std::move(set),
                                        epoch = mMeasurementEpoch]() mutable {
            set = ResolveGrassOverlayTextures(std::move(set));
            for (auto& mesh : captured) {
                auto alpha = std::make_shared<GrassOverlayAlpha>(*mesh.Overlay);
                if (alpha->LoadTextures) {
                    alpha->LoadTextures(*alpha);
                    alpha->LoadTextures = {};
                }
                mesh.Overlay = std::move(alpha);
            }
            MaskMeasurement measurement;
            measurement.Instance = source.InstanceId;
            measurement.Version = set->Version;
            measurement.Epoch = epoch;
            const auto world = [](const GrassSceneMesh& mesh, const std::array<float, 3>& p) {
                if (mesh.TransformBakedIntoVertices)
                    return p;
                const auto& m = mesh.ModelToWorld;
                const float w = m[12] * p[0] + m[13] * p[1] + m[14] * p[2] + m[15];
                const float inv = std::abs(w) > 1e-6F ? 1 / w : 1;
                return std::array<float, 3>{ (m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3]) * inv,
                                             (m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7]) * inv,
                                             (m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11]) * inv };
            };
            std::unordered_map<uint64_t, size_t> footprintByDraw;
            for (const auto& ownedMesh : captured) {
                const auto* mesh = &ownedMesh;
                GrassOverlayFootprint footprint;
                footprint.Texture = { "", mesh->ObservedTextureHash, mesh->TextureWidth, mesh->TextureHeight };
                footprint.Source = { "", source.ObservedTextureHash, source.TextureWidth, source.TextureHeight };
                footprint.Draw = mesh->InstanceId;
                footprint.NativeIdentity = mesh->NativeTextureIdentity;
                footprint.Mode = mesh->Overlay->Rule.Mode;
                footprint.NativeSupported = mesh->Overlay->NativeSupported;
                footprintByDraw[footprint.Draw] = measurement.Footprints.size();
                measurement.Footprints.push_back(std::move(footprint));
            }
            for (size_t i = 0; i + 2 < source.Indices->size(); i += 3) {
                std::array<std::array<float, 3>, 3> triangle;
                bool valid = true;
                for (size_t j = 0; j < 3; ++j) {
                    const auto index = (*source.Indices)[i + j];
                    if (index >= source.Vertices->size()) {
                        valid = false;
                        break;
                    }
                    triangle[j] = world(source, (*source.Vertices)[index].Position);
                }
                if (!valid)
                    continue;
                for (auto overlay : GrassOverlaysForTriangle(triangle, *set)) {
                    auto triangleSnapshot = *overlay;
                    const auto index = footprintByDraw.at(overlay->Draw);
                    triangleSnapshot.Alpha = captured[index].Overlay;
                    MeasureGrassOverlayFootprint(triangle, triangleSnapshot, measurement.Footprints[index]);
                }
            }
            std::erase_if(measurement.Footprints, [](const auto& footprint) { return footprint.Area <= 0; });
            if (std::getenv("TRIAEVUM_GRASS_MASK_DIAGNOSTICS"))
                for (const auto& footprint : measurement.Footprints) {
                    std::fprintf(stderr,
                                 "TRIAEVUM_GRASS_MASK coverage native=%016llx rgba8=%016llx size=%ux%u source=%016llx "
                                 "area_m2=%.6f effective_mean=%.6f supported=%d texture_hist=",
                                 static_cast<unsigned long long>(footprint.NativeIdentity),
                                 static_cast<unsigned long long>(footprint.Texture.Rgba8Hash), footprint.Texture.Width,
                                 footprint.Texture.Height, static_cast<unsigned long long>(footprint.Source.Rgba8Hash),
                                 footprint.Area / 10000, footprint.CoverageArea / footprint.Area,
                                 footprint.NativeSupported ? 1 : 0);
                    for (double area : footprint.TextureAlphaArea)
                        std::fprintf(stderr, "%.6f,", area / footprint.Area);
                    std::fprintf(stderr, " native_hist=");
                    for (double area : footprint.NativeAlphaArea)
                        std::fprintf(stderr, "%.6f,", area / footprint.Area);
                    std::fprintf(stderr, "\n");
                }
            return measurement;
        });
}

std::vector<GrassOverlayFootprint> GrassSceneBridge::ObservedMasks() const {
    std::scoped_lock lock(mMutex);
    mMaskStatisticsFrame = mLatestFrame;
    std::vector<GrassOverlayFootprint> result;
    CollectMaskMeasurement();
    for (const auto& [instance, measurement] : mMaskMeasurements) {
        const auto set = mOverlaySets.find(instance);
        if (set == mOverlaySets.end() || set->second->Version != measurement.Version)
            continue;
        const auto scope = mOverlaySetScopes.find(instance);
        if (scope == mOverlaySetScopes.end() || scope->second.FrameId + 1 < mLatestFrame)
            continue;
        for (const auto& footprint : measurement.Footprints) {
            GrassOverlayFootprint copy{ footprint.Texture,
                                        footprint.Source,
                                        footprint.Draw,
                                        footprint.NativeIdentity,
                                        footprint.Mode,
                                        footprint.NativeSupported,
                                        footprint.Area,
                                        footprint.CoverageArea,
                                        footprint.TextureAlphaArea,
                                        footprint.NativeAlphaArea,
                                        {} };
            result.push_back(std::move(copy));
        }
    }
    return result;
}

void GrassSceneBridge::HighlightMask(std::optional<GrassTextureSelector> texture) {
    std::scoped_lock lock(mMutex);
    mHighlightedMask = texture;
    mHighlightUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
}

WorldDebugLineSet GrassSceneBridge::MaskHighlight(const GrassSceneScope& scope) const {
    std::scoped_lock lock(mMutex);
    WorldDebugLineSet lines;
    if (!mHighlightedMask || std::chrono::steady_clock::now() > mHighlightUntil)
        return lines;
    CollectMaskMeasurement();
    for (const auto& [instance, measurement] : mMaskMeasurements) {
        const auto set = mOverlaySets.find(instance);
        if (set == mOverlaySets.end() || set->second->Version != measurement.Version)
            continue;
        const auto found = mOverlaySetScopes.find(instance);
        if (found == mOverlaySetScopes.end())
            continue;
        const auto& current = found->second;
        if (current.FrameId != scope.FrameId || current.RenderTargetNamespace != scope.RenderTargetNamespace ||
            current.FramebufferColorPhysicalAddress != scope.FramebufferColorPhysicalAddress)
            continue;
        for (const auto& footprint : measurement.Footprints) {
            if (footprint.Texture.Rgba8Hash != mHighlightedMask->Rgba8Hash ||
                footprint.Texture.Width != mHighlightedMask->Width ||
                footprint.Texture.Height != mHighlightedMask->Height)
                continue;
            for (const auto& triangle : footprint.Triangles)
                for (size_t edge = 0; edge < 3; ++edge) {
                    if (lines.Lines.size() >= kMaximumWorldDebugLines)
                        return lines;
                    lines.Lines.push_back({ triangle[edge], triangle[(edge + 1) % 3], { 1.0F, .65F, .05F, 1.0F } });
                }
        }
    }
    return lines;
}

} // namespace Fast::Oot3d
