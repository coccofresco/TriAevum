#include "three_ds_recomp/oot3d/Oot3dNativeRoomRenderProvider.h"

#include <algorithm>
#include <exception>

namespace ThreeDsRecomp::Oot3d {

bool NativeRoomRenderSource::Ready() const {
    return Status == "ready" && CatalogRecord != nullptr && !RenderModels.empty();
}

NativeRoomRenderProvider::NativeRoomRenderProvider(const AssetCatalog& catalog, NativeSourceProvider& sources)
    : mCatalog(catalog), mSources(sources) {
}

std::shared_ptr<const NativeRoomRenderSource> NativeRoomRenderProvider::Resolve(int32_t sceneId, int32_t setupIndex,
                                                                                int32_t roomIndex) {
    const std::string key = std::to_string(sceneId) + ":" + std::to_string(setupIndex) + ":" +
                            std::to_string(roomIndex);
    {
        std::scoped_lock lock(mMutex);
        const auto cached = mCache.find(key);
        if (cached != mCache.end()) {
            return cached->second;
        }
    }

    auto result = std::make_shared<NativeRoomRenderSource>();
    const auto& candidates = mCatalog.FindRooms(sceneId, roomIndex);
    std::vector<const AssetCatalogRecord*> setupCandidates;
    std::copy_if(candidates.begin(), candidates.end(), std::back_inserter(setupCandidates),
                 [setupIndex](const AssetCatalogRecord* record) {
                     return std::find(record->SetupIndices.begin(), record->SetupIndices.end(), setupIndex) !=
                            record->SetupIndices.end();
                 });
    if (setupCandidates.empty()) {
        result->Status = "room_not_cataloged_for_setup";
    } else if (setupCandidates.size() != 1) {
        result->Status = "ambiguous_room_source";
    } else if (setupCandidates.front()->SupportTier < 2 || setupCandidates.front()->CanonicalResources.size() != 1) {
        result->Status = "room_source_not_packaged";
    } else {
        result->CatalogRecord = setupCandidates.front();
        try {
            result->EmbeddedCmbs = mSources.LoadZsiEmbeddedCmbs(result->CatalogRecord->CanonicalResources.front());
            result->RenderModels.reserve(result->EmbeddedCmbs.size());
            for (const auto& embedded : result->EmbeddedCmbs) {
                result->RenderModels.push_back(BuildOot3dNativeRenderModel(embedded.Model));
            }
            result->Status = result->RenderModels.empty() ? "room_contains_no_cmb" : "ready";
        } catch (const std::exception& error) {
            result->Status = "room_parse_failed";
            result->Error = error.what();
        }
    }
    std::scoped_lock lock(mMutex);
    return mCache.emplace(key, result).first->second;
}

void NativeRoomRenderProvider::Clear() {
    std::scoped_lock lock(mMutex);
    mCache.clear();
}

} // namespace ThreeDsRecomp::Oot3d
