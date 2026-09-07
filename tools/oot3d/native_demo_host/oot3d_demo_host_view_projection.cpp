#include "oot3d_demo_host_view_projection.h"

#include <algorithm>

#include "oot3d_demo_math.h"

double AspectFromDimensions(uint32_t width, uint32_t height) {
    return static_cast<double>(std::max<uint32_t>(1, width)) /
           static_cast<double>(std::max<uint32_t>(1, height));
}

NativeViewProjectionMatrices BuildAndMaterializeCurrentFrameNativeViewProjection(
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene, const Camera& camera,
    double aspect) {
    const double extent = std::max(ThreeDsRecomp::Oot3d::NativeDemoBoundsMaxExtent(renderScene.Bounds), 80.0);
    const bool nativeProjectionRangeAvailable = renderScene.PicaFog.ProjectionRangeAvailable;
    const double zNear = nativeProjectionRangeAvailable
                             ? static_cast<double>(renderScene.PicaFog.ProjectionNear)
                             : std::max(1.0, extent / 800.0);
    const double zFar = nativeProjectionRangeAvailable
                            ? static_cast<double>(renderScene.PicaFog.ProjectionFar)
                            : std::max(2000.0, extent * 10.0);
    NativeViewProjectionMatrices matrices;
    matrices.ViewToClip = BuildViewToClipMatrix(camera, aspect, zNear, zFar);
    matrices.WorldToClip = BuildWorldToClipMatrix(camera, aspect, zNear, zFar);
    ThreeDsRecomp::Oot3d::MaterializeOot3dNativeKankyoLensRuntimeViewProjection(
        renderScene, matrices.WorldToClip,
        ThreeDsRecomp::Oot3d::Oot3dDemoVec3{
            static_cast<float>(camera.Position.X),
            static_cast<float>(camera.Position.Y),
            static_cast<float>(camera.Position.Z),
        },
        "oot3d_demo_current_camera_world_to_clip_before_backend_clip_adjustment",
        "current-frame active camera eye/at/up/fov was converted to the demo world-to-clip "
        "matrix before backend OpenGL clip-space adjustment and exposed as the native "
        "play+0x5bb4 materialization candidate");
    ThreeDsRecomp::Oot3d::MaterializeOot3dNativeKankyoSkyRuntime(
        renderScene,
        {
            static_cast<float>(camera.Position.X),
            static_cast<float>(camera.Position.Y),
            static_cast<float>(camera.Position.Z),
        },
        {
            static_cast<float>(camera.Target.X),
            static_cast<float>(camera.Target.Y),
            static_cast<float>(camera.Target.Z),
        },
        {
            static_cast<float>(camera.Up.X),
            static_cast<float>(camera.Up.Y),
            static_cast<float>(camera.Up.Z),
        });
    ThreeDsRecomp::Oot3d::MaterializeOot3dNativeKankyoMoonRuntime(
        renderScene,
        {
            static_cast<float>(camera.Position.X),
            static_cast<float>(camera.Position.Y),
            static_cast<float>(camera.Position.Z),
        },
        {
            static_cast<float>(camera.Target.X),
            static_cast<float>(camera.Target.Y),
            static_cast<float>(camera.Target.Z),
        },
        {
            static_cast<float>(camera.Up.X),
            static_cast<float>(camera.Up.Y),
            static_cast<float>(camera.Up.Z),
        });
    return matrices;
}
