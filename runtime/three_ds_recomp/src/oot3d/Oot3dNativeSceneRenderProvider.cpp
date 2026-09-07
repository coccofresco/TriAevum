#include "three_ds_recomp/oot3d/Oot3dNativeSceneRenderProvider.h"

#include <exception>
#include <map>

namespace ThreeDsRecomp::Oot3d {

bool NativeSceneRenderSource::Ready() const {
    return Status == "ready" && RenderScene != nullptr;
}

bool NativeKankyoRenderSource::Ready() const {
    return Status == "ready" && TemporalState.Available && Archive != nullptr &&
           Archive->Ready() && !RenderModels.empty();
}

NativeSceneRenderProvider::NativeSceneRenderProvider(
    NativeRoomRenderProvider& rooms, NativeSceneEnvironmentProvider& environments)
    : mRooms(rooms), mEnvironments(environments) {
}

std::shared_ptr<const NativeSceneRenderSource> NativeSceneRenderProvider::Resolve(
    int32_t sceneId, int32_t setupIndex, int32_t roomIndex, int32_t floorLightSettingIndex) {
    const std::string key = std::to_string(sceneId) + ":" + std::to_string(setupIndex) + ":" +
                            std::to_string(roomIndex) + ":" + std::to_string(floorLightSettingIndex);
    {
        std::scoped_lock lock(mMutex);
        const auto cached = mCache.find(key);
        if (cached != mCache.end()) {
            return cached->second;
        }
    }

    auto result = std::make_shared<NativeSceneRenderSource>();
    result->FloorLightSettingIndex = floorLightSettingIndex;
    result->Room = mRooms.Resolve(sceneId, setupIndex, roomIndex);
    result->Environment = mEnvironments.Resolve(sceneId, setupIndex);
    if (!result->Room->Ready()) {
        result->Status = "room_render_source_unavailable";
        result->Error = result->Room->Status + ": " + result->Room->Error;
    } else if (!result->Environment->Ready()) {
        result->Status = "scene_environment_unavailable";
        result->Error = result->Environment->Status + ": " + result->Environment->Error;
    } else if (floorLightSettingIndex < 0) {
        result->Status = "floor_light_setting_unresolved";
    } else {
        try {
            Oot3dNativeDemoScene nativeScene;
            nativeScene.NativePicaLighting = result->Environment->DecodedLighting;
            nativeScene.NativePicaLightingSemantics = result->Environment->LightingSemantics;
            nativeScene.NativePicaLightingSemanticsAvailable =
                result->Environment->LightingSemanticsAvailable;
            nativeScene.NativePicaLightingSemanticsFormat =
                result->Environment->LightingSemantics.value("format", "");
            nativeScene.NativePicaLightingSemanticsSourceKind =
                result->Environment->LightingSemantics.value("source_kind", "");
            nativeScene.PlayerStart.FloorLightSettingIndex = floorLightSettingIndex;

            auto renderScene = std::make_shared<Oot3dNativeDemoRenderScene>();
            renderScene->Room = result->Room->RenderModels.front();
            renderScene->AdditionalRoomModels.assign(result->Room->RenderModels.begin() + 1,
                                                     result->Room->RenderModels.end());
            NativeDemoExpandBoundsByBounds(
                renderScene->Bounds, Oot3dNativeRenderModelWorldBounds(renderScene->Room));
            for (const auto& roomModel : renderScene->AdditionalRoomModels) {
                NativeDemoExpandBoundsByBounds(
                    renderScene->Bounds, Oot3dNativeRenderModelWorldBounds(roomModel));
            }
            ApplyOot3dNativePicaLighting(
                nativeScene, *renderScene, nullptr,
                &result->Environment->LightingSemanticPlan);
            MarkOot3dNativeRenderModelVertexDataCacheable(renderScene->Room);
            for (auto& roomModel : renderScene->AdditionalRoomModels) {
                MarkOot3dNativeRenderModelVertexDataCacheable(roomModel);
            }
            if (!renderScene->PicaLighting.Available) {
                result->Status = "native_pica_lighting_unresolved";
            } else {
                renderScene->PicaFog =
                    BuildOot3dNativePicaFogState(nativeScene, renderScene->PicaLighting);
                result->RenderScene = std::move(renderScene);
                result->Status = "ready";
            }
        } catch (const std::exception& error) {
            result->Status = "scene_render_composition_failed";
            result->Error = error.what();
        }
    }

    std::scoped_lock lock(mMutex);
    return mCache.emplace(key, result).first->second;
}

void NativeSceneRenderProvider::Clear() {
    std::scoped_lock lock(mMutex);
    mCache.clear();
}

Oot3dNativePicaLightingRenderState NativeSceneRenderProvider::ResolveRuntimeLighting(
    int32_t sceneId, int32_t setupIndex, int32_t floorLightSettingIndex,
    const Oot3dNativeRuntimeEnvironmentInput& runtimeEnvironment) {
    const auto environment = mEnvironments.Resolve(sceneId, setupIndex);
    if (!environment->Ready() || floorLightSettingIndex < 0) {
        return {};
    }
    Oot3dNativeDemoScene nativeScene;
    nativeScene.NativePicaLighting = environment->DecodedLighting;
    nativeScene.NativePicaLightingSemanticsAvailable = environment->LightingSemanticsAvailable;
    nativeScene.NativePicaLightingSemanticsSourceKind = environment->LightingSemanticPlan.SourceKind;
    nativeScene.PlayerStart.FloorLightSettingIndex = floorLightSettingIndex;
    return BuildOot3dNativePicaLightingRenderState(
        nativeScene, environment->LightingSemanticPlan, &runtimeEnvironment);
}

Oot3dNativePicaFogState NativeSceneRenderProvider::ResolveRuntimeFog(
    int32_t sceneId, int32_t setupIndex, int32_t floorLightSettingIndex,
    const Oot3dNativeRuntimeEnvironmentInput& runtimeEnvironment,
    const Oot3dNativePicaLightingRenderState& runtimeLighting) {
    const auto environment = mEnvironments.Resolve(sceneId, setupIndex);
    if (!environment->Ready() || floorLightSettingIndex < 0 || !runtimeLighting.Available) {
        return {};
    }
    Oot3dNativeDemoScene nativeScene;
    nativeScene.NativePicaLighting = environment->DecodedLighting;
    return BuildOot3dNativePicaFogState(
        nativeScene, runtimeLighting, &environment->FogRuntimeDefaults);
}

NativeKankyoRenderSource NativeSceneRenderProvider::ResolveRuntimeKankyo(
    int32_t sceneId, int32_t setupIndex,
    const Oot3dNativePicaLightingRenderState& runtimeLighting,
    const Oot3dNativeRuntimeEnvironmentInput& runtimeEnvironment) {
    NativeKankyoRenderSource result;
    const auto environment = mEnvironments.Resolve(sceneId, setupIndex);
    if (!environment->Ready() || !runtimeLighting.Available ||
        !runtimeEnvironment.TimeResolved) {
        result.Status = "kankyo_runtime_environment_unresolved";
        return result;
    }
    int32_t mode = runtimeLighting.ResolvedRuntimeLightSetting.CurrentMode;
    if (mode < 0) {
        mode = environment->DecodedLighting.NativeRuntimeTransitionGlobalFallbackMode;
    }
    result.TemporalState = mEnvironments.ResolveKankyoTemporalState(
        environment->SkyboxId, mode, runtimeEnvironment.SkyboxTime);
    if (!result.TemporalState.Available) {
        result.Status = "kankyo_temporal_profile_unresolved";
        return result;
    }
    result.Archive = mEnvironments.ResolveKankyoArchive(result.TemporalState.ArchiveName);
    if (!result.Archive->Ready()) {
        result.Status = "kankyo_archive_unavailable";
        result.Error = result.Archive->Status + ": " + result.Archive->Error;
        return result;
    }
    result.MaterialAnimationFrame = static_cast<float>(runtimeEnvironment.TimeStartFrame);
    std::map<uint32_t, int32_t> selected;
    const auto addProfile = [&](int32_t profile) {
        if (profile < 0 || profile >= result.TemporalState.ProfileCount) {
            return;
        }
        for (int32_t layer = 0; layer < result.TemporalState.LayerCount; ++layer) {
            const int32_t index = profile + layer * result.TemporalState.ProfileCount;
            if (index >= 0 && index < result.TemporalState.CoreCmbCount) {
                selected[static_cast<uint32_t>(index)] = profile;
            }
        }
    };
    addProfile(result.TemporalState.CurrentProfileIndex);
    addProfile(result.TemporalState.NextProfileIndex);
    if (result.TemporalState.CurrentProfileIndex >= 0 &&
        result.TemporalState.NextProfileIndex >= 0 &&
        (result.TemporalState.CurrentProfileIndex >> 2) ==
            (result.TemporalState.NextProfileIndex >> 2)) {
        addProfile((result.TemporalState.CurrentProfileIndex >> 2) << 2);
    }
    for (const auto& model : result.Archive->Models) {
        const auto selectedIt = selected.find(model.TypeLocalIndex);
        if (selectedIt == selected.end()) {
            continue;
        }
        result.RenderModels.push_back(&model.RenderModel);
        result.SourceModels.push_back(&model.Model);
        result.MaterialAnimations.push_back(&model.MaterialAnimations);
        result.ProfileIndices.push_back(selectedIt->second);
        result.LayerIndices.push_back(
            result.TemporalState.ProfileCount > 0
                ? static_cast<int32_t>(model.TypeLocalIndex) /
                      result.TemporalState.ProfileCount
                : -1);
    }
    result.Status = result.RenderModels.empty() ? "kankyo_profile_contains_no_models" : "ready";
    return result;
}

} // namespace ThreeDsRecomp::Oot3d
