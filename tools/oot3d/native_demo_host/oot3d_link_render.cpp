#include "oot3d_link_render.h"

#include <utility>

#include "oot3d_demo_host_diagnostics.h"
#include "oot3d_demo_math.h"
#include "oot3d_title_intro_logo_runtime.h"
#include "three_ds_recomp/oot3d/Oot3dNativeRenderer.h"

void ApplyLinkInstance(ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene, const LinkInstance& link) {
    renderScene.Link.ModelToWorld =
        ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderScaleYawTranslateTransform(link.Scale, link.Yaw, link.Position);
    renderScene.Link.Bounds = ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelWorldBounds(renderScene.Link);
    renderScene.Bounds = {};
    ThreeDsRecomp::Oot3d::NativeDemoExpandBoundsByBounds(renderScene.Bounds,
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
            renderScene.Bounds, ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelWorldBounds(actorVisual));
    }
    ThreeDsRecomp::Oot3d::NativeDemoExpandBoundsByBounds(renderScene.Bounds, renderScene.Link.Bounds);
}

void SampleLinkAnimationPose(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                             const LinkNativeLocomotionConfig& config,
                             LinkAnimation& animation) {
    ThreeDsRecomp::Oot3d::CsabPose targetPose;
    if (animation.NativeWalkRunBlendActive) {
        const auto walkPose = ThreeDsRecomp::Oot3d::SampleOot3dNativeDemoLinkPoseFrame(
            scene, config.Clips.WalkClipIndex, static_cast<float>(animation.WalkFrame));
        const auto runPose = ThreeDsRecomp::Oot3d::SampleOot3dNativeDemoLinkPoseFrame(
            scene, config.Clips.RunClipIndex, static_cast<float>(animation.RunFrame));
        targetPose = BlendCsabPoses(walkPose, runPose, animation.NativeWalkRunBlendWeight);
        animation.PoseBlendActive = true;
        animation.PoseBlendFromClipId = scene.LinkCsabClips[config.Clips.WalkClipIndex].Id;
        animation.PoseBlendToClipId = scene.LinkCsabClips[config.Clips.RunClipIndex].Id;
        animation.PoseBlendWeight = animation.NativeWalkRunBlendWeight;
    } else {
        animation.Frame = WrapAnimationFrame(animation.Frame, animation.FrameCount);
        targetPose = ThreeDsRecomp::Oot3d::SampleOot3dNativeDemoLinkPoseFrame(scene, animation.ClipIndex,
                                                                     static_cast<float>(animation.Frame));
    }

    if (animation.NativeStartMorphActive) {
        animation.CurrentPose = BlendCsabPoses(animation.StartMorphPose, targetPose, animation.NativeStartBlendWeight);
        animation.PoseBlendActive = true;
        animation.PoseBlendFromClipId = animation.PreviousLocomotionAnimationClass;
        animation.PoseBlendToClipId = animation.LocomotionAnimationClass;
        animation.PoseBlendWeight = animation.NativeStartBlendWeight;
    } else if (animation.NativeWalkEndEntryMorphActive) {
        animation.CurrentPose =
            BlendCsabPoses(animation.WalkEndEntryPose, targetPose, animation.NativeWalkEndEntryBlendWeight);
        animation.PoseBlendActive = true;
        animation.PoseBlendFromClipId = animation.PreviousLocomotionAnimationClass;
        animation.PoseBlendToClipId = animation.ClipId;
        animation.PoseBlendWeight = animation.NativeWalkEndEntryBlendWeight;
    } else if (animation.NativeWalkEndToIdleMorphActive) {
        animation.CurrentPose =
            BlendCsabPoses(animation.WalkEndToIdlePose, targetPose, animation.NativeWalkEndToIdleBlendWeight);
        animation.PoseBlendActive = true;
        animation.PoseBlendFromClipId = animation.NativeWalkEndSide == "left" ? "walk_end_left" : "walk_end_right";
        animation.PoseBlendToClipId = "idle";
        animation.PoseBlendWeight = animation.NativeWalkEndToIdleBlendWeight;
    } else {
        animation.CurrentPose = std::move(targetPose);
    }
}

void ApplyLinkCurrentAnimationPose(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene,
    LinkAnimation& animation, const LinkInstance& link) {
    const auto skinTransforms =
        ThreeDsRecomp::Oot3d::BuildOot3dNativeDemoSkinTransforms(scene.LinkBindWorldTransforms, animation.CurrentPose);
    if (renderScene.Link.Batches.empty()) {
        renderScene.Link = ThreeDsRecomp::Oot3d::BuildOot3dNativeDemoLinkRenderModel(
            scene, animation.CurrentPose, skinTransforms);
    } else {
        ThreeDsRecomp::Oot3d::ApplyOot3dNativeRenderModelPose(
            renderScene.Link, scene.LinkModel, &animation.CurrentPose, &skinTransforms);
    }
    const auto materialSelection = ThreeDsRecomp::Oot3d::SampleOot3dNativeDemoLinkMaterialFrameSelection(
        scene, animation.ClipIndex, static_cast<float>(animation.Frame));
    ThreeDsRecomp::Oot3d::ApplyOot3dNativeRenderModelMaterialAnimationFrames(
        renderScene.Link, scene.LinkModel, scene.LinkMaterialAnimations,
        MaterialAnimationFramesByRole(materialSelection), 0.0f);
    ApplyLinkInstance(renderScene, link);
}

void ApplyLinkAnimationFrame(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                             const LinkNativeLocomotionConfig& config,
                             ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene,
                             LinkAnimation& animation, const LinkInstance& link) {
    SampleLinkAnimationPose(scene, config, animation);
    ApplyLinkCurrentAnimationPose(scene, renderScene, animation, link);
}
