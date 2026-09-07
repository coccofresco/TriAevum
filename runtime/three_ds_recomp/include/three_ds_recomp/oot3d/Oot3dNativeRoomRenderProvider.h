#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "three_ds_recomp/oot3d/Oot3dAssetCatalog.h"
#include "three_ds_recomp/oot3d/Oot3dNativeRenderScene.h"
#include "three_ds_recomp/oot3d/Oot3dNativeSourceProvider.h"

namespace ThreeDsRecomp::Oot3d {

struct NativeRoomRenderSource {
    const AssetCatalogRecord* CatalogRecord = nullptr;
    std::vector<ZsiEmbeddedCmb> EmbeddedCmbs;
    std::vector<Oot3dNativeRenderModel> RenderModels;
    std::string Status;
    std::string Error;

    bool Ready() const;
};

class NativeRoomRenderProvider {
  public:
    NativeRoomRenderProvider(const AssetCatalog& catalog, NativeSourceProvider& sources);
    std::shared_ptr<const NativeRoomRenderSource> Resolve(int32_t sceneId, int32_t setupIndex, int32_t roomIndex);
    void Clear();

  private:
    const AssetCatalog& mCatalog;
    NativeSourceProvider& mSources;
    std::mutex mMutex;
    std::unordered_map<std::string, std::shared_ptr<const NativeRoomRenderSource>> mCache;
};

} // namespace ThreeDsRecomp::Oot3d
