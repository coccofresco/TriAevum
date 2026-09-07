#include "three_ds_recomp/oot3d/Oot3dSceneProvider.h"

#include <algorithm>
#include <set>

namespace ThreeDsRecomp::Oot3d {

SceneProvider::SceneProvider(const AssetCatalog& catalog) : mCatalog(catalog) {
}

SceneSelection SceneProvider::Resolve(int32_t sceneId, int32_t setupIndex, int32_t roomIndex) const {
    SceneSelection selection;
    selection.NativeSceneId = sceneId;
    selection.Scene = mCatalog.FindScene(sceneId);
    if (selection.Scene == nullptr) {
        return selection;
    }
    selection.SetupAvailable = std::find(selection.Scene->SetupIndices.begin(), selection.Scene->SetupIndices.end(),
                                         setupIndex) != selection.Scene->SetupIndices.end();
    selection.Rooms = mCatalog.FindRooms(sceneId, roomIndex);
    selection.Collisions = mCatalog.FindSceneFamily(sceneId, "scene_collision");

    std::set<std::string> missing;
    bool packaged = selection.Scene->SupportTier >= 2 && !selection.Rooms.empty();
    const auto collect = [&](const AssetCatalogRecord& record) {
        const auto gaps = mCatalog.MissingEngineCapabilities(record);
        missing.insert(gaps.begin(), gaps.end());
        packaged = packaged && record.SupportTier >= 2;
    };
    collect(*selection.Scene);
    for (const auto* room : selection.Rooms) {
        collect(*room);
    }
    for (const auto* collision : selection.Collisions) {
        collect(*collision);
    }
    selection.MissingEngineCapabilities.assign(missing.begin(), missing.end());
    selection.Packaged = packaged;
    selection.RenderableCandidate = selection.SetupAvailable && selection.Packaged &&
                                    selection.MissingEngineCapabilities.empty();
    selection.TraversalCandidate = selection.RenderableCandidate && !selection.Collisions.empty() &&
                                   std::all_of(selection.Collisions.begin(), selection.Collisions.end(),
                                               [](const AssetCatalogRecord* collision) {
                                                   return collision->SupportTier >= 2;
                                               });
    return selection;
}

SceneSelection SceneProvider::ResolveSemanticScene(const SemanticRouteCatalog& routes,
                                                   int32_t scaffoldSceneId,
                                                   int32_t nativeSetupIndex,
                                                   int32_t roomIndex) const {
    const auto* route = routes.FindSceneRequest(scaffoldSceneId);
    if (route == nullptr) {
        SceneSelection selection;
        selection.RouteStatus = "semantic_scene_route_not_found";
        return selection;
    }
    auto selection = Resolve(route->Native.SceneId, nativeSetupIndex, roomIndex);
    selection.Route = route;
    selection.RouteStatus = "resolved";
    return selection;
}

} // namespace ThreeDsRecomp::Oot3d
