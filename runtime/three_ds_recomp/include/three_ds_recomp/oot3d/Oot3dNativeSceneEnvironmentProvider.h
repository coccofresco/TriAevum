#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "three_ds_recomp/oot3d/Oot3dAssetCatalog.h"
#include "three_ds_recomp/oot3d/Oot3dNativeDemoScene.h"
#include "three_ds_recomp/oot3d/Oot3dNativeRenderScene.h"
#include "three_ds_recomp/oot3d/Oot3dNativeSourceProvider.h"

namespace ThreeDsRecomp::Oot3d {

struct NativePicaLightSettingRecord {
    uint32_t Index = 0;
    std::array<uint8_t, 0x1C> Raw = {};
};

struct NativeKankyoCmbSource {
    std::string Name;
    uint32_t TypeLocalIndex = 0;
    CmbModel Model;
    Oot3dNativeRenderModel RenderModel;
    std::vector<CmabMaterialAnimation> MaterialAnimations;
};

struct NativeKankyoCmabSource {
    std::string Name;
    CmabMaterialAnimation Animation;
};

struct NativeKankyoCtxbSource {
    std::string Name;
    CtxbTexture Texture;
};

struct NativeKankyoArchiveSource {
    std::string ArchiveName;
    std::string ResourcePath;
    std::shared_ptr<const NativeSource> Source;
    ZarArchive Archive;
    std::vector<NativeKankyoCmbSource> Models;
    std::vector<NativeKankyoCmabSource> MaterialAnimations;
    std::vector<NativeKankyoCtxbSource> Textures;
    std::string Status;
    std::string Error;

    bool Ready() const;
};

struct NativeKankyoTemporalState {
    bool Available = false;
    int32_t SkyboxId = -1;
    int32_t Mode = -1;
    uint16_t ActiveAngle = 0;
    int32_t ScheduleEntryIndex = -1;
    int32_t CurrentProfileIndex = -1;
    int32_t NextProfileIndex = -1;
    uint8_t BlendAlpha = 0;
    std::string RomPath;
    std::string ArchiveName;
    int32_t ProfileCount = 0;
    int32_t LayerCount = 0;
    int32_t CoreCmbCount = 0;
    float DrawScale = 1.0f;
    std::string SourceKind;
};

struct NativeSceneEnvironmentSetup {
    const AssetCatalogRecord* Scene = nullptr;
    int32_t SetupIndex = -1;
    std::string SetupRole;
    int32_t SkyboxId = -1;
    int32_t Weather = -1;
    int32_t Indoors = -1;
    int32_t CameraOrWorldMapArea = -1;
    uint32_t MiscRawArgument = 0;
    std::vector<NativePicaLightSettingRecord> LightSettings;
    Oot3dNativeDemoPicaLightingState DecodedLighting;
    nlohmann::json LightingSemantics;
    Oot3dNativePicaLightingSemanticPlan LightingSemanticPlan;
    Oot3dNativeFogRuntimeDefaults FogRuntimeDefaults;
    bool LightingSemanticsAvailable = false;
    std::string LightingSemanticsResource;
    std::string Status;
    std::string Error;

    bool Ready() const;
};

class NativeSceneEnvironmentProvider {
  public:
    NativeSceneEnvironmentProvider(const AssetCatalog& catalog, NativeSourceProvider& sources);
    std::shared_ptr<const NativeSceneEnvironmentSetup> Resolve(int32_t sceneId, int32_t setupIndex);
    std::shared_ptr<const NativeKankyoArchiveSource> ResolveKankyoArchive(
        std::string_view archiveName);
    NativeKankyoTemporalState ResolveKankyoTemporalState(
        int32_t skyboxId, int32_t mode, uint16_t activeAngle) const;
    void Clear();

  private:
    const AssetCatalog& mCatalog;
    NativeSourceProvider& mSources;
    std::mutex mMutex;
    std::unordered_map<std::string, std::shared_ptr<const NativeSceneEnvironmentSetup>> mCache;
    std::unordered_map<std::string, std::shared_ptr<const NativeKankyoArchiveSource>> mKankyoCache;
    nlohmann::json mKankyoManifest;
    std::string mKankyoManifestError;
    std::vector<uint8_t> mKankyoSkyboxRecords;
    std::vector<uint8_t> mKankyoScheduleTable;
    float mKankyoDrawScale = 1.0f;
    bool mKankyoRuntimeTablesAvailable = false;
    std::string mKankyoRuntimeTablesError;
    nlohmann::json mLightingSemantics;
    Oot3dNativePicaLightingSemanticPlan mLightingSemanticPlan;
    std::string mLightingSemanticsResource;
    std::string mLightingSemanticsError;
    Oot3dNativeDemoPicaLightingState mRuntimeLightingContract;
    Oot3dNativeFogRuntimeDefaults mFogRuntimeDefaults;
    std::string mRuntimeTransitionTableError;
    std::string mFogRuntimeDefaultsError;
};

} // namespace ThreeDsRecomp::Oot3d
