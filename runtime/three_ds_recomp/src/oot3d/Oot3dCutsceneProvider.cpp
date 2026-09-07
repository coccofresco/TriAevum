#include "three_ds_recomp/oot3d/Oot3dCutsceneProvider.h"

#include <stdexcept>
#include <utility>

namespace ThreeDsRecomp::Oot3d {

bool CutsceneSelection::Ready() const {
    return Route != nullptr && NativeScene != nullptr && !NativeTimelines.empty() && Status == "resolved";
}

CutsceneProvider::CutsceneProvider(const AssetCatalog& catalog, NativeSourceProvider::FileLoader loader)
    : mCatalog(catalog), mQdb(std::move(loader)) {
}

CutsceneSelection CutsceneProvider::ResolveSemanticCutscene(const SemanticRouteCatalog& routes,
                                                             std::string_view gameState,
                                                             int32_t scaffoldSceneId,
                                                             int32_t sceneSetupIndex,
                                                             int32_t cutsceneIndex) {
    CutsceneSelection selection;
    selection.Route = routes.FindCutsceneRequest(
        gameState, scaffoldSceneId, sceneSetupIndex, cutsceneIndex);
    if (selection.Route == nullptr) {
        selection.Status = "route_missing";
        return selection;
    }
    selection.NativeScene = mCatalog.Find(selection.Route->Native.AssetId);
    if (selection.NativeScene == nullptr || selection.NativeScene->Family != "scene_profile") {
        selection.Status = "native_scene_missing";
        return selection;
    }
    selection.NativeTimelines.reserve(selection.Route->Native.SequenceAssetIds.size());
    for (const auto& assetId : selection.Route->Native.SequenceAssetIds) {
        const auto* record = mCatalog.Find(assetId);
        if (record == nullptr || record->Family != "cutscene_timeline") {
            selection.NativeTimelines.clear();
            selection.Status = "native_timeline_missing";
            return selection;
        }
        auto timeline = mQdb.Load(*record);
        if (timeline == nullptr) {
            selection.NativeTimelines.clear();
            selection.Status = "native_timeline_source_missing";
            return selection;
        }
        selection.NativeTimelines.push_back(std::move(timeline));
    }
    selection.Status = "resolved";
    return selection;
}

CutsceneCompletion CutsceneProvider::Complete(const CutsceneSelection& selection) const {
    if (!selection.Ready()) {
        throw std::invalid_argument("Cannot complete an unresolved OOT3D semantic cutscene");
    }
    return { selection.Route->RouteId, selection.Route->CompletionEvent };
}

void CutsceneProvider::Clear() {
    mQdb.Clear();
}

} // namespace ThreeDsRecomp::Oot3d
