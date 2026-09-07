#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "three_ds_recomp/oot3d/Oot3dNativeRenderer.h"

namespace Fast {
class GfxRenderingAPI;
}

namespace ThreeDsRecomp::Oot3d {

using NativeRenderCommandToken = uintptr_t;

NativeRenderCommandToken QueueNativeRenderScene(std::shared_ptr<const Oot3dNativeDemoRenderScene> scene);
NativeRenderCommandToken QueueNativeRenderScene(
    std::shared_ptr<const Oot3dNativeDemoRenderScene> scene,
    const Oot3dNativePicaLightingRenderState& runtimeLighting);
NativeRenderCommandToken QueueNativeRenderScene(
    std::shared_ptr<const Oot3dNativeDemoRenderScene> scene,
    const Oot3dNativePicaLightingRenderState& runtimeLighting,
    const Oot3dNativePicaFogState& runtimeFog);
NativeRenderCommandToken QueueNativeRenderScene(
    std::shared_ptr<const Oot3dNativeDemoRenderScene> scene,
    const Oot3dNativePicaLightingRenderState& runtimeLighting,
    const Oot3dNativePicaFogState& runtimeFog,
    std::shared_ptr<const void> environmentOwner,
    std::vector<Oot3dNativeEnvironmentOverlay> environmentModels);
Oot3dNativeRendererSubmitResult ExecuteNativeRenderScene(NativeRenderCommandToken token,
                                                          Fast::GfxRenderingAPI& renderingApi,
                                                          const Matrix4f& worldToClip,
                                                          const Matrix4f& viewToClip);
bool CancelNativeRenderScene(NativeRenderCommandToken token);
void ClearNativeRenderScenes();
size_t PendingNativeRenderSceneCount();

} // namespace ThreeDsRecomp::Oot3d
