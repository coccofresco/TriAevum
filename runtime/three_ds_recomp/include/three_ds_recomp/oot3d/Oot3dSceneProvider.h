#pragma once

#include "three_ds_recomp/oot3d/Oot3dAssetCatalog.h"
#include "three_ds_recomp/oot3d/Oot3dSemanticRouteCatalog.h"

namespace ThreeDsRecomp::Oot3d {

struct SceneSelection {
    const SemanticRouteRecord* Route = nullptr;
    const AssetCatalogRecord* Scene = nullptr;
    std::vector<const AssetCatalogRecord*> Rooms;
    std::vector<const AssetCatalogRecord*> Collisions;
    std::vector<std::string> MissingEngineCapabilities;
    bool SetupAvailable = false;
    bool Packaged = false;
    bool RenderableCandidate = false;
    bool TraversalCandidate = false;
    int32_t NativeSceneId = -1;
    std::string RouteStatus;
};

class SceneProvider {
  public:
    explicit SceneProvider(const AssetCatalog& catalog);
    SceneSelection Resolve(int32_t sceneId, int32_t setupIndex, int32_t roomIndex) const;
    SceneSelection ResolveSemanticScene(const SemanticRouteCatalog& routes, int32_t scaffoldSceneId,
                                        int32_t nativeSetupIndex, int32_t roomIndex) const;

  private:
    const AssetCatalog& mCatalog;
};

} // namespace ThreeDsRecomp::Oot3d
