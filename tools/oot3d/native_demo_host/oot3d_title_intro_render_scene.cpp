#include "oot3d_title_intro_render_scene.h"

#include <cmath>
#include <limits>

#include "oot3d_title_intro_logo_runtime.h"
#include "three_ds_recomp/oot3d/Oot3dNativeDemoScene.h"
#include "three_ds_recomp/oot3d/Oot3dNativeRenderer.h"

bool NativePicaLightingDebugRenderMode(const Args& args) {
    return args.RenderMode == "native_pica_lighting_debug";
}

ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene BuildRenderSceneForMode(
    const Args& args,
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
    const ThreeDsRecomp::Oot3d::Oot3dNativeRuntimeEnvironmentInput* runtimeEnvironment,
    float materialAnimationFrame) {
    if (NativePicaLightingDebugRenderMode(args)) {
        return ThreeDsRecomp::Oot3d::BuildOot3dNativeDemoPicaLightingDebugRenderScene(scene);
    }
    const float effectiveMaterialAnimationFrame =
        std::isfinite(materialAnimationFrame)
            ? materialAnimationFrame
            : static_cast<float>(args.MaterialAnimationFrame);
    return ThreeDsRecomp::Oot3d::BuildOot3dNativeDemoRenderScene(
        scene, scene.LinkStandingPose, scene.LinkSkinTransforms,
        effectiveMaterialAnimationFrame, runtimeEnvironment);
}

ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene BuildRenderSceneForMode(
    const Args& args,
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
    const ThreeDsRecomp::Oot3d::Oot3dNativeRuntimeEnvironmentInput* runtimeEnvironment) {
    return BuildRenderSceneForMode(
        args, scene, runtimeEnvironment, std::numeric_limits<float>::quiet_NaN());
}

ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene BuildRenderSceneForMode(
    const Args& args,
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene) {
    return BuildRenderSceneForMode(
        args, scene, nullptr, std::numeric_limits<float>::quiet_NaN());
}

ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene BuildRenderSceneForTitleIntroPlayback(
    const Args& args,
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
    const ThreeDsRecomp::Oot3d::Oot3dNativeRuntimeEnvironmentInput* runtimeEnvironment,
    float materialAnimationFrame) {
    if (NativePicaLightingDebugRenderMode(args)) {
        return ThreeDsRecomp::Oot3d::BuildOot3dNativeDemoPicaLightingDebugRenderScene(scene);
    }
    const float effectiveMaterialAnimationFrame =
        std::isfinite(materialAnimationFrame)
            ? materialAnimationFrame
            : static_cast<float>(args.MaterialAnimationFrame);
    return ThreeDsRecomp::Oot3d::BuildOot3dNativeDemoRenderScene(
        scene, scene.LinkStandingPose, scene.LinkSkinTransforms,
        effectiveMaterialAnimationFrame, runtimeEnvironment, false,
        ThreeDsRecomp::Oot3d::Oot3dNativeActorVisualSelection::NativeBehaviorResolved);
}

void ReapplyRenderModeAfterLinkPoseUpdate(
    const Args& args,
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene,
    const ThreeDsRecomp::Oot3d::Oot3dNativeRuntimeEnvironmentInput* runtimeEnvironment) {
    if (runtimeEnvironment != nullptr) {
        ThreeDsRecomp::Oot3d::ApplyOot3dNativePicaLighting(
            scene, renderScene, runtimeEnvironment);
    } else {
        ThreeDsRecomp::Oot3d::ApplyOot3dNativePicaLightingToLink(renderScene, false);
    }
    if (NativePicaLightingDebugRenderMode(args)) {
        ThreeDsRecomp::Oot3d::ApplyOot3dNativePicaLightingDebug(scene, renderScene);
    }
}

void RebuildRenderSceneBounds(ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene,
                              bool includeLink) {
    renderScene.Bounds = {};
    ThreeDsRecomp::Oot3d::NativeDemoExpandBoundsByBounds(
        renderScene.Bounds,
        ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelWorldBounds(renderScene.Room));
    for (const auto& roomModel : renderScene.AdditionalRoomModels) {
        ThreeDsRecomp::Oot3d::NativeDemoExpandBoundsByBounds(
            renderScene.Bounds,
            ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelWorldBounds(roomModel));
    }
    for (const auto& actorVisual : renderScene.ActorVisuals) {
        if (IsTitleIntroLogoRenderModel(actorVisual)) {
            continue;
        }
        ThreeDsRecomp::Oot3d::NativeDemoExpandBoundsByBounds(
            renderScene.Bounds,
            ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelWorldBounds(actorVisual));
    }
    if (includeLink) {
        ThreeDsRecomp::Oot3d::NativeDemoExpandBoundsByBounds(renderScene.Bounds, renderScene.Link.Bounds);
    }
}

void HideGameplayLinkForTitleIntro(ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene,
                                   bool rebuildBounds) {
    renderScene.Link = {};
    renderScene.LinkActorShadow = {};
    if (rebuildBounds) {
        RebuildRenderSceneBounds(renderScene, false);
    }
}
