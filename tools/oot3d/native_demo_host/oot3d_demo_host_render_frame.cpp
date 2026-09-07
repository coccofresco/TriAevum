#include "oot3d_demo_host_render_frame.h"

#include <chrono>
#include <cstdint>

#include "oot3d_demo_host_view_projection.h"
#include "three_ds_recomp/oot3d/Oot3dNativeRenderer.h"

WindowDemoRenderFrameResult RenderWindowDemoFrame(
    Fast::Fast3dWindow& window, Fast::GfxRenderingAPI& api,
    ThreeDsRecomp::Oot3d::Oot3dNativeFast3dRenderConfig& renderConfig,
    ThreeDsRecomp::Oot3d::Oot3dNativeFast3dRenderBackend& backend,
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene, const Camera& camera, double aspect, uint32_t width,
    uint32_t height, bool collectDiagnostics) {
    WindowDemoRenderFrameResult result;
    auto phaseStart = std::chrono::steady_clock::now();
    window.StartFrame();
    api.UpdateFramebufferParameters(0, width, height, 1, false, true, true, true);
    api.StartFrame();
    result.FrameStartSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - phaseStart).count();
    phaseStart = std::chrono::steady_clock::now();
    api.StartDrawToFramebuffer(0, 1.0f);
    if (renderScene.EnvironmentBackground.Available && renderScene.EnvironmentBackground.UsedForRender) {
        const auto& clear = renderScene.EnvironmentBackground.ClearColor;
        api.SetClearColor(static_cast<float>(clear.R) / 255.0f, static_cast<float>(clear.G) / 255.0f,
                          static_cast<float>(clear.B) / 255.0f, static_cast<float>(clear.A) / 255.0f);
    } else {
        api.SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    }
    api.ClearFramebuffer(true, true);
    api.SetViewport(0, 0, static_cast<int>(width), static_cast<int>(height));
    api.SetScissor(0, 0, static_cast<int>(width), static_cast<int>(height));

    const auto viewProjection =
        BuildAndMaterializeCurrentFrameNativeViewProjection(renderScene, camera, aspect);
    renderConfig.ViewToClip = viewProjection.ViewToClip;
    renderConfig.ViewToClipAvailable = true;
    renderConfig.WorldToClip = viewProjection.WorldToClip;
    backend.SetConfig(renderConfig);
    result.SetupSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - phaseStart).count();

    phaseStart = std::chrono::steady_clock::now();
    const auto submission = ThreeDsRecomp::Oot3d::SubmitOot3dNativeDemoRenderScene(renderScene, backend);
    result.SceneSubmitSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - phaseStart).count();
    if (!collectDiagnostics) {
        return result;
    }
    result.Submission = ThreeDsRecomp::Oot3d::Oot3dNativeRendererSubmitResultToJson(submission);
    result.AdapterStats = ThreeDsRecomp::Oot3d::Oot3dNativeFast3dRenderStatsToJson(backend.Stats());
    return result;
}
