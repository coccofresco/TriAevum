#include "three_ds_recomp/oot3d/Oot3dNativeRenderCommand.h"

#include <algorithm>
#include <deque>
#include <mutex>
#include <optional>
#include <unordered_map>

#include "three_ds_recomp/oot3d/Oot3dNativeFast3dRenderer.h"

namespace ThreeDsRecomp::Oot3d {
namespace {

constexpr size_t kMaximumPendingScenes = 256;
std::mutex sMutex;
NativeRenderCommandToken sNextToken = 1;
std::deque<NativeRenderCommandToken> sOrder;
struct QueuedNativeScene {
    std::shared_ptr<const Oot3dNativeDemoRenderScene> Scene;
    std::optional<Oot3dNativePicaLightingRenderState> RuntimeLighting;
    std::optional<Oot3dNativePicaFogState> RuntimeFog;
    std::shared_ptr<const void> EnvironmentOwner;
    std::vector<Oot3dNativeEnvironmentOverlay> EnvironmentModels;
};
std::unordered_map<NativeRenderCommandToken, QueuedNativeScene> sScenes;
Fast::GfxRenderingAPI* sBackendApi = nullptr;
std::unique_ptr<Oot3dNativeFast3dRenderBackend> sBackend;

void RemoveFromOrder(NativeRenderCommandToken token) {
    const auto it = std::find(sOrder.begin(), sOrder.end(), token);
    if (it != sOrder.end()) {
        sOrder.erase(it);
    }
}

} // namespace

NativeRenderCommandToken QueueNativeRenderScene(std::shared_ptr<const Oot3dNativeDemoRenderScene> scene) {
    if (scene == nullptr) {
        return 0;
    }
    std::scoped_lock lock(sMutex);
    while (sOrder.size() >= kMaximumPendingScenes) {
        sScenes.erase(sOrder.front());
        sOrder.pop_front();
    }
    const NativeRenderCommandToken token = sNextToken++;
    sScenes.emplace(token, QueuedNativeScene{ std::move(scene), std::nullopt, std::nullopt, {}, {} });
    sOrder.push_back(token);
    return token;
}

NativeRenderCommandToken QueueNativeRenderScene(
    std::shared_ptr<const Oot3dNativeDemoRenderScene> scene,
    const Oot3dNativePicaLightingRenderState& runtimeLighting) {
    if (scene == nullptr || !runtimeLighting.Available) {
        return 0;
    }
    std::scoped_lock lock(sMutex);
    while (sOrder.size() >= kMaximumPendingScenes) {
        sScenes.erase(sOrder.front());
        sOrder.pop_front();
    }
    const NativeRenderCommandToken token = sNextToken++;
    sScenes.emplace(token, QueuedNativeScene{ std::move(scene), runtimeLighting, std::nullopt, {}, {} });
    sOrder.push_back(token);
    return token;
}

NativeRenderCommandToken QueueNativeRenderScene(
    std::shared_ptr<const Oot3dNativeDemoRenderScene> scene,
    const Oot3dNativePicaLightingRenderState& runtimeLighting,
    const Oot3dNativePicaFogState& runtimeFog) {
    if (scene == nullptr || !runtimeLighting.Available || !runtimeFog.Available) {
        return 0;
    }
    std::scoped_lock lock(sMutex);
    while (sOrder.size() >= kMaximumPendingScenes) {
        sScenes.erase(sOrder.front());
        sOrder.pop_front();
    }
    const NativeRenderCommandToken token = sNextToken++;
    sScenes.emplace(token, QueuedNativeScene{ std::move(scene), runtimeLighting, runtimeFog, {}, {} });
    sOrder.push_back(token);
    return token;
}

NativeRenderCommandToken QueueNativeRenderScene(
    std::shared_ptr<const Oot3dNativeDemoRenderScene> scene,
    const Oot3dNativePicaLightingRenderState& runtimeLighting,
    const Oot3dNativePicaFogState& runtimeFog,
    std::shared_ptr<const void> environmentOwner,
    std::vector<Oot3dNativeEnvironmentOverlay> environmentModels) {
    if (scene == nullptr || !runtimeLighting.Available || !runtimeFog.Available ||
        environmentOwner == nullptr || environmentModels.empty()) {
        return 0;
    }
    std::scoped_lock lock(sMutex);
    while (sOrder.size() >= kMaximumPendingScenes) {
        sScenes.erase(sOrder.front());
        sOrder.pop_front();
    }
    const NativeRenderCommandToken token = sNextToken++;
    sScenes.emplace(token, QueuedNativeScene{ std::move(scene), runtimeLighting, runtimeFog,
                                              std::move(environmentOwner),
                                              std::move(environmentModels) });
    sOrder.push_back(token);
    return token;
}

Oot3dNativeRendererSubmitResult ExecuteNativeRenderScene(NativeRenderCommandToken token,
                                                          Fast::GfxRenderingAPI& renderingApi,
                                                          const Matrix4f& worldToClip,
                                                          const Matrix4f& viewToClip) {
    QueuedNativeScene queued;
    {
        std::scoped_lock lock(sMutex);
        const auto it = sScenes.find(token);
        if (it == sScenes.end()) {
            Oot3dNativeRendererSubmitResult result;
            result.Issues.push_back({ "oot3d_native_render_command_missing", "", 0,
                                      "native render command token was not queued or was evicted" });
            return result;
        }
        queued = std::move(it->second);
        sScenes.erase(it);
        RemoveFromOrder(token);
    }
    if (sBackend == nullptr || sBackendApi != &renderingApi) {
        sBackend.reset();
        sBackendApi = &renderingApi;
        sBackend = std::make_unique<Oot3dNativeFast3dRenderBackend>(renderingApi);
    }
    auto config = sBackend->Config();
    config.WorldToClip = worldToClip;
    config.ViewToClip = viewToClip;
    config.ViewToClipAvailable = true;
    // CMB output-merger state is authoritative. Enabling host alpha globally makes
    // opaque materials with meaningful texture alpha blend incorrectly.
    config.EnableAlphaBlend = false;
    config.ClearDepthBeforeSmallQueue = false;
    sBackend->SetConfig(config);
    sBackend->SetRuntimePicaLighting(queued.RuntimeLighting);
    sBackend->SetRuntimePicaFog(queued.RuntimeFog);
    const auto result = SubmitOot3dNativeDemoRenderScene(
        *queued.Scene, *sBackend, queued.EnvironmentModels);
    sBackend->ReleaseCurrentShaderBinding();
    return result;
}

bool CancelNativeRenderScene(NativeRenderCommandToken token) {
    std::scoped_lock lock(sMutex);
    const bool removed = sScenes.erase(token) != 0;
    RemoveFromOrder(token);
    return removed;
}

void ClearNativeRenderScenes() {
    std::scoped_lock lock(sMutex);
    sScenes.clear();
    sOrder.clear();
}

size_t PendingNativeRenderSceneCount() {
    std::scoped_lock lock(sMutex);
    return sScenes.size();
}

} // namespace ThreeDsRecomp::Oot3d
