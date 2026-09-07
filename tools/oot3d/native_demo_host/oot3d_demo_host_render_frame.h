#pragma once

#include <cstdint>

#include <nlohmann/json.hpp>

#include "fast/Fast3dWindow.h"
#include "fast/backends/gfx_rendering_api.h"
#include "oot3d_native_camera_runtime.h"
#include "three_ds_recomp/oot3d/Oot3dNativeFast3dRenderer.h"
#include "three_ds_recomp/oot3d/Oot3dNativeRenderScene.h"

struct WindowDemoRenderFrameResult {
    nlohmann::json Submission;
    nlohmann::json AdapterStats;
    double FrameStartSeconds = 0.0;
    double SetupSeconds = 0.0;
    double SceneSubmitSeconds = 0.0;
};

WindowDemoRenderFrameResult RenderWindowDemoFrame(
    Fast::Fast3dWindow& window, Fast::GfxRenderingAPI& api,
    ThreeDsRecomp::Oot3d::Oot3dNativeFast3dRenderConfig& renderConfig,
    ThreeDsRecomp::Oot3d::Oot3dNativeFast3dRenderBackend& backend,
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene, const Camera& camera, double aspect, uint32_t width,
    uint32_t height, bool collectDiagnostics = true);
