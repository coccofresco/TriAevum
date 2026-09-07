#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "three_ds_recomp/oot3d/Oot3dNativeRoomRenderProvider.h"
#include "three_ds_recomp/oot3d/Oot3dNativeSceneEnvironmentProvider.h"

namespace ThreeDsRecomp::Oot3d {

struct NativeSceneRenderSource {
    std::shared_ptr<const NativeRoomRenderSource> Room;
    std::shared_ptr<const NativeSceneEnvironmentSetup> Environment;
    std::shared_ptr<const Oot3dNativeDemoRenderScene> RenderScene;
    int32_t FloorLightSettingIndex = -1;
    std::string Status;
    std::string Error;

    bool Ready() const;
};

struct NativeKankyoRenderSource {
    NativeKankyoTemporalState TemporalState;
    std::shared_ptr<const NativeKankyoArchiveSource> Archive;
    std::vector<const Oot3dNativeRenderModel*> RenderModels;
    std::vector<const CmbModel*> SourceModels;
    std::vector<const std::vector<CmabMaterialAnimation>*> MaterialAnimations;
    float MaterialAnimationFrame = 0.0f;
    std::vector<int32_t> ProfileIndices;
    std::vector<int32_t> LayerIndices;
    std::string Status;
    std::string Error;

    bool Ready() const;
};

class NativeSceneRenderProvider {
  public:
    NativeSceneRenderProvider(NativeRoomRenderProvider& rooms,
                              NativeSceneEnvironmentProvider& environments);
    std::shared_ptr<const NativeSceneRenderSource> Resolve(int32_t sceneId, int32_t setupIndex,
                                                            int32_t roomIndex,
                                                            int32_t floorLightSettingIndex);
    Oot3dNativePicaLightingRenderState ResolveRuntimeLighting(
        int32_t sceneId, int32_t setupIndex, int32_t floorLightSettingIndex,
        const Oot3dNativeRuntimeEnvironmentInput& runtimeEnvironment);
    Oot3dNativePicaFogState ResolveRuntimeFog(
        int32_t sceneId, int32_t setupIndex, int32_t floorLightSettingIndex,
        const Oot3dNativeRuntimeEnvironmentInput& runtimeEnvironment,
        const Oot3dNativePicaLightingRenderState& runtimeLighting);
    NativeKankyoRenderSource ResolveRuntimeKankyo(
        int32_t sceneId, int32_t setupIndex,
        const Oot3dNativePicaLightingRenderState& runtimeLighting,
        const Oot3dNativeRuntimeEnvironmentInput& runtimeEnvironment);
    void Clear();

  private:
    NativeRoomRenderProvider& mRooms;
    NativeSceneEnvironmentProvider& mEnvironments;
    std::mutex mMutex;
    std::unordered_map<std::string, std::shared_ptr<const NativeSceneRenderSource>> mCache;
};

} // namespace ThreeDsRecomp::Oot3d
