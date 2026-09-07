#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "three_ds_recomp/oot3d/Oot3dAssetCatalog.h"
#include "three_ds_recomp/oot3d/Oot3dNativeQdbProvider.h"
#include "three_ds_recomp/oot3d/Oot3dSemanticRouteCatalog.h"

namespace ThreeDsRecomp::Oot3d {

struct CutsceneSelection {
    const SemanticRouteRecord* Route = nullptr;
    const AssetCatalogRecord* NativeScene = nullptr;
    std::vector<std::shared_ptr<const NativeQdbAsset>> NativeTimelines;
    std::string Status;

    bool Ready() const;
};

struct CutsceneCompletion {
    std::string RouteId;
    std::string Event;
};

class CutsceneProvider {
  public:
    CutsceneProvider(const AssetCatalog& catalog, NativeSourceProvider::FileLoader loader);

    CutsceneSelection ResolveSemanticCutscene(const SemanticRouteCatalog& routes,
                                              std::string_view gameState, int32_t scaffoldSceneId,
                                              int32_t sceneSetupIndex, int32_t cutsceneIndex);
    CutsceneCompletion Complete(const CutsceneSelection& selection) const;
    void Clear();

  private:
    const AssetCatalog& mCatalog;
    NativeQdbProvider mQdb;
};

} // namespace ThreeDsRecomp::Oot3d
