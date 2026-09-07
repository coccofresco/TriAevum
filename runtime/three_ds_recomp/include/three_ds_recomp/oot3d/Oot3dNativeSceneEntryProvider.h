#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>

#include "three_ds_recomp/oot3d/Oot3dAssetCatalog.h"
#include "three_ds_recomp/oot3d/Oot3dNativeSourceProvider.h"
#include "three_ds_recomp/oot3d/Oot3dSemanticRouteCatalog.h"

namespace ThreeDsRecomp::Oot3d {

struct NativeSceneEntry {
    int32_t SetupIndex = -1;
    int32_t GlobalEntranceIndex = -1;
    int32_t LocalEntranceIndex = -1;
    int32_t SpawnIndex = -1;
    int32_t Room = -1;
    int32_t ActorId = -1;
    int16_t PositionX = 0;
    int16_t PositionY = 0;
    int16_t PositionZ = 0;
    int16_t RotationX = 0;
    int16_t RotationY = 0;
    int16_t RotationZ = 0;
    uint16_t Params = 0;
    uint8_t CameraDataIndex = 0xFF;
    uint8_t PlayerStartMode = 0;
    bool UsesExplicitCameraData = false;
};

struct SceneEntrySelection {
    const SemanticRouteRecord* Route = nullptr;
    const AssetCatalogRecord* NativeScene = nullptr;
    std::shared_ptr<const NativeSource> Source;
    NativeSceneEntry Entry;
    std::string Status;

    bool Ready() const;
};

NativeSceneEntry ParseNativeZsiSceneEntry(std::span<const uint8_t> bytes, int32_t setupIndex,
                                          int32_t localEntranceIndex,
                                          std::string_view sourceName = {});

class NativeSceneEntryProvider {
  public:
    NativeSceneEntryProvider(const AssetCatalog& catalog, NativeSourceProvider& sources);

    SceneEntrySelection ResolveSemanticEntry(const SemanticRouteCatalog& routes,
                                             int32_t scaffoldEntranceIndex,
                                             std::string_view variantKey,
                                             int32_t nativeSetupIndex = -1);
    SceneEntrySelection ResolveSemanticEntry(const SemanticRouteCatalog& routes,
                                             int32_t scaffoldEntranceIndex,
                                             std::span<const SemanticGameplayFact> facts,
                                             int32_t nativeSetupIndex = -1);

  private:
    SceneEntrySelection ResolveRoute(const SemanticRouteRecord* route, int32_t nativeSetupIndex);

    const AssetCatalog& mCatalog;
    NativeSourceProvider& mSources;
};

} // namespace ThreeDsRecomp::Oot3d
