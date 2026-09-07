#include "oot3d_title_intro_actor_render.h"

#include <cmath>
#include <sstream>
#include <utility>

#include "oot3d_demo_math.h"
#include "oot3d_title_intro_actor_assets.h"
#include "oot3d_title_intro_actor_diagnostics.h"

namespace {

size_t StripSubmittedTitleIntroActorTexturePayloads(TitleIntroNativeActor& actor) {
    if (!actor.BaseRenderModelBuilt || actor.BaseRenderModelTexturePayloadsStripped) {
        return 0;
    }
    const size_t strippedByteCount =
        ThreeDsRecomp::Oot3d::StripOot3dNativeRenderModelTexturePayloads(actor.BaseRenderModel);
    actor.BaseRenderModelTexturePayloadsStripped = true;
    return strippedByteCount;
}

} // namespace

TitleIntroActorVisualAppendResult AppendTitleIntroActorVisual(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
    TitleIntroNativeActor& actor, const TitleIntroCueSample& cue,
    double frame,
    const Oot3dTitleIntroOpeningActorMotionSample* actorMotion,
    bool usePairedMountMotion,
    const TitleIntroMountedLinkContext* mountedLinkContext,
    const ThreeDsRecomp::Oot3d::Oot3dNativeHorseDustEmitterProfile* horseDustEmitterProfile,
    bool buildDiagnostics,
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene) {
    TitleIntroActorVisualAppendResult result;
    if (!actor.Loaded || !cue.Valid) {
        return result;
    }

    const auto clip = SelectTitleIntroActorClip(
        actor, actorMotion, usePairedMountMotion, cue.GenericActorSnapshot);
    result.AnimationIndex = clip.AnimationIndex;
    const bool isEpona =
        Oot3d_TitleIntroOpeningActorRuntimeIsEponaBinding(actor.OpeningActorBindingRow) != 0u;
    float poseFrame = 0.0f;
    if (clip.Csab != nullptr && clip.Csab->FrameCount > 0) {
        const double poseFrameInput =
            clip.PoseFrameValid
                ? static_cast<double>(clip.PoseFrame)
                : (isEpona ? 0.0 : frame * static_cast<double>(clip.PlaySpeedScale));
        poseFrame = static_cast<float>(
            WrapAnimationFrame(poseFrameInput, clip.Csab->FrameCount));
        result.PoseFrameValid = clip.PoseFrameValid || !isEpona;
        result.PoseFrame = poseFrame;
    }
    auto pose = ThreeDsRecomp::Oot3d::SampleCsabPoseFrameBytes(*clip.CsabBytes, actor.Model, poseFrame);
    result.NativeMorphActive = clip.NativeMorphActive;
    result.NativeMorphWeight = clip.MorphWeight;
    result.NativeMorphSourceFrame = clip.MorphPoseFrame;
    result.NativeMorphSourceCsabName = clip.MorphCsabName;
    if (clip.NativeMorphActive && clip.MorphCsabBytes != nullptr && clip.MorphCsab != nullptr &&
        clip.MorphCsab->FrameCount > 0) {
        const float morphPoseFrame = static_cast<float>(WrapAnimationFrame(
            static_cast<double>(clip.MorphPoseFrame), clip.MorphCsab->FrameCount));
        const auto morphPose = ThreeDsRecomp::Oot3d::SampleCsabPoseFrameBytes(
            *clip.MorphCsabBytes, actor.Model, morphPoseFrame);
        auto morphedPose = ThreeDsRecomp::Oot3d::MorphCsabPoseNative(
            actor.Model.Skeleton, pose, morphPose, clip.MorphWeight);
        if (morphedPose.Valid) {
            pose = std::move(morphedPose);
            result.NativeMorphApplied = true;
            result.NativeMorphSourceFrame = morphPoseFrame;
        }
    }
    auto skinTransforms = ThreeDsRecomp::Oot3d::BuildOot3dNativeDemoSkinTransforms(actor.BindWorldTransforms, pose);
    ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel renderModel;
    if (actor.BaseRenderModelBuilt) {
        renderModel = actor.BaseRenderModel;
        ThreeDsRecomp::Oot3d::ApplyOot3dNativeRenderModelPose(
            renderModel, actor.Model, pose.Valid ? &pose : nullptr,
            !skinTransforms.empty() ? &skinTransforms : nullptr);
    } else {
        ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelBuildOptions options;
        options.BakeTransformIntoVertices = false;
        options.Pose = pose.Valid ? &pose : nullptr;
        options.SkinTransforms = !skinTransforms.empty() ? &skinTransforms : nullptr;
        renderModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(actor.Model, options);
    }
    renderModel.Name = "title_intro:" + actor.Role + ":" + renderModel.Name;
    std::ostringstream source;
    source << actor.ArchivePath.string() << "!" << actor.CmbName << "!" << clip.CsabName
           << ";title_actor_motion_clip=" << clip.MotionRole
           << ";title_actor_animation_index=" << clip.AnimationIndex
           << ";title_actor_pose_frame=" << poseFrame
           << ";title_actor_pose_frame_source="
           << (clip.PoseFrameValid
                   ? "scene_cutscene_frame_runtime_native_actor_animation_state"
                   : (isEpona
                          ? "missing_native_epona_animation_state_no_global_clock_fallback"
                          : "legacy_global_cutscene_frame_pending_actor_state_migration"))
           << ";native_speed=" << clip.NativeSpeed
           << ";play_speed_scale=" << clip.PlaySpeedScale
           << ";clip_source=" << clip.Source;
    if (clip.NativeMorphActive) {
        source << ";native_skelanime_morph_active=true"
               << ";native_skelanime_morph_applied=" << (result.NativeMorphApplied ? "true" : "false")
               << ";native_skelanime_morph_source_csab=" << clip.MorphCsabName
               << ";native_skelanime_morph_source_frame=" << result.NativeMorphSourceFrame
               << ";native_skelanime_morph_weight=" << clip.MorphWeight
               << ";native_skelanime_morph_rate=" << clip.MorphRate
               << ";native_skelanime_morph_frames=" << clip.MorphFrames
               << ";native_skelanime_morph_source=" << clip.MorphSource;
    }
    const double mountedVisualYOffset =
        TitleIntroMountedVisualYOffset(actor, actorMotion, usePairedMountMotion);
    if (mountedVisualYOffset != 0.0) {
        source << ";mounted_visual_y_offset=" << mountedVisualYOffset
               << ";mounted_visual_y_offset_source=oot3d_actor_focus_y_offset_literal"
               << ";mounted_visual_y_offset_basis=shared_player_action_paired_mount_visual";
    }
    const bool mountedRootInputAvailable =
        cue.HasMountedAttachment &&
        mountedLinkContext != nullptr &&
        mountedLinkContext->RideActorRootInputValid &&
        mountedLinkContext->RideActorPoseOffsetNodeIndex ==
            static_cast<int>(cue.MountedParentPoseOffsetNodeIndex);
    auto drawPosition = TitleIntroActorDrawPosition(actor, cue, actorMotion, usePairedMountMotion);
    auto drawRotation = cue.Rotation;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 childRootMotion{};
    int childRootMotionBoneIndex = -1;
    if (mountedRootInputAvailable && clip.Csab != nullptr && pose.Valid) {
        childRootMotionBoneIndex = FindTitleIntroBoneIndexForCsabNode(
            *clip.Csab, cue.MountedChildRootMotionNodeIndex);
        if (childRootMotionBoneIndex >= 0 &&
            pose.WorldTransforms.size() > static_cast<size_t>(childRootMotionBoneIndex)) {
            childRootMotion = TransformDemoPoint(
                pose.WorldTransforms[static_cast<size_t>(childRootMotionBoneIndex)], {});
            drawPosition.X = mountedLinkContext->RideActorPosition.X +
                             mountedLinkContext->RideActorPoseOffset.X -
                             childRootMotion.X * cue.MountedChildRootMotionScale;
            drawPosition.Y = mountedLinkContext->RideActorPosition.Y +
                             mountedLinkContext->RideActorPoseOffset.Y -
                             childRootMotion.Y * cue.MountedChildRootMotionScale;
            drawPosition.Z = mountedLinkContext->RideActorPosition.Z +
                             mountedLinkContext->RideActorPoseOffset.Z -
                             childRootMotion.Z * cue.MountedChildRootMotionScale;
            if (cue.MountedCopyParentYaw) {
                drawRotation.Y = mountedLinkContext->RideActorRotation.Y;
            }
        }
    }
    const bool mountedRootApplied = mountedRootInputAvailable && childRootMotionBoneIndex >= 0;
    if (mountedRootApplied) {
        source << ";mounted_link_draw_position_source=oot3d_mounted_player_action_root_formula"
               << ";mounted_link_attachment_source=" << cue.MountedAttachmentSource
               << ";mounted_link_attachment_parent_actor_binding_index="
               << cue.MountedParentActorBindingIndex
               << ";mounted_link_parent_pose_offset_node_index="
               << cue.MountedParentPoseOffsetNodeIndex
               << ";mounted_link_child_root_motion_node_index="
               << cue.MountedChildRootMotionNodeIndex
               << ";mounted_link_child_root_motion_bone_index=" << childRootMotionBoneIndex
               << ";mounted_link_child_root_motion_scale=" << cue.MountedChildRootMotionScale
               << ";mounted_link_child_root_motion_x=" << childRootMotion.X
               << ";mounted_link_child_root_motion_y=" << childRootMotion.Y
               << ";mounted_link_child_root_motion_z=" << childRootMotion.Z
               << ";mounted_link_player_action_function=0x" << std::hex
               << cue.MountedPlayerActionFunction
               << ";mounted_link_skeleton_node_offset_function=0x"
               << cue.MountedSkeletonNodeOffsetFunction << std::dec
               << ";mounted_link_copy_parent_yaw=" << (cue.MountedCopyParentYaw ? 1 : 0)
               << ";mounted_link_root_status=applied";
    }
    if (mountedRootApplied) {
        source << ";mounted_link_ride_actor_position_x="
               << mountedLinkContext->RideActorPosition.X
               << ";mounted_link_ride_actor_position_y="
               << mountedLinkContext->RideActorPosition.Y
               << ";mounted_link_ride_actor_position_z="
               << mountedLinkContext->RideActorPosition.Z
               << ";mounted_link_ride_actor_pose_offset_x="
               << mountedLinkContext->RideActorPoseOffset.X
               << ";mounted_link_ride_actor_pose_offset_y="
               << mountedLinkContext->RideActorPoseOffset.Y
               << ";mounted_link_ride_actor_pose_offset_z="
               << mountedLinkContext->RideActorPoseOffset.Z
               << ";mounted_link_root_x=" << drawPosition.X
               << ";mounted_link_root_y=" << drawPosition.Y
               << ";mounted_link_root_z=" << drawPosition.Z;
    }
    const TitleIntroActorRenderScale renderScale =
        ResolveTitleIntroActorRenderScale(actor, actorMotion, usePairedMountMotion);
    source << ";title_actor_render_scale=" << renderScale.Scale
           << ";title_actor_render_scale_source=" << renderScale.Source
           << ";title_actor_render_scale_runtime_override="
           << (renderScale.RuntimeOverrideApplied ? 1 : 0);
    renderModel.ModelToWorld =
        ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderActorEntryTransform(renderScale.Scale, drawRotation, drawPosition);
    if (isEpona && horseDustEmitterProfile != nullptr && horseDustEmitterProfile->Decoded &&
        clip.Csab != nullptr && pose.Valid) {
        bool allHoofPositionsValid = true;
        for (size_t contactIndex = 0; contactIndex < horseDustEmitterProfile->Contacts.size(); ++contactIndex) {
            const auto& contact = horseDustEmitterProfile->Contacts[contactIndex];
            const int boneIndex = FindTitleIntroBoneIndexForCsabNode(*clip.Csab, contact.SkeletonNodeIndex);
            if (boneIndex < 0 || pose.WorldTransforms.size() <= static_cast<size_t>(boneIndex)) {
                allHoofPositionsValid = false;
                continue;
            }
            const ThreeDsRecomp::Oot3d::Oot3dDemoVec3 hoofOffset = {
                horseDustEmitterProfile->HoofLocalOffset.X,
                horseDustEmitterProfile->HoofLocalOffset.Y,
                horseDustEmitterProfile->HoofLocalOffset.Z,
            };
            const auto hoofModel = TransformDemoPoint(
                pose.WorldTransforms[static_cast<size_t>(boneIndex)], hoofOffset);
            result.HorseDustHoofWorldPositions[contactIndex] =
                TransformDemoPoint(renderModel.ModelToWorld, hoofModel);
            result.HorseDustHoofWorldPositionValid[contactIndex] = true;
        }
        result.HorseDustEmitterInputValid = allHoofPositionsValid;
        source << ";horse_dust_emitter_input="
               << (allHoofPositionsValid ? "decoded_hoof_pose_nodes" : "missing_hoof_pose_node");
    }
    if (mountedRootApplied) {
        const auto mountedChildRootWorldPoint =
            TransformDemoPoint(renderModel.ModelToWorld, childRootMotion);
        const ThreeDsRecomp::Oot3d::Oot3dDemoVec3 mountedParentAnchorWorldPoint = {
            mountedLinkContext->RideActorPosition.X + mountedLinkContext->RideActorPoseOffset.X,
            mountedLinkContext->RideActorPosition.Y + mountedLinkContext->RideActorPoseOffset.Y,
            mountedLinkContext->RideActorPosition.Z + mountedLinkContext->RideActorPoseOffset.Z,
        };
        const double mountedAttachmentErrorX =
            mountedChildRootWorldPoint.X - mountedParentAnchorWorldPoint.X;
        const double mountedAttachmentErrorY =
            mountedChildRootWorldPoint.Y - mountedParentAnchorWorldPoint.Y;
        const double mountedAttachmentErrorZ =
            mountedChildRootWorldPoint.Z - mountedParentAnchorWorldPoint.Z;
        const double mountedAttachmentErrorLength =
            std::sqrt(mountedAttachmentErrorX * mountedAttachmentErrorX +
                      mountedAttachmentErrorY * mountedAttachmentErrorY +
                      mountedAttachmentErrorZ * mountedAttachmentErrorZ);
        result.MountedAttachmentErrorValid = true;
        result.MountedAttachmentErrorLength = mountedAttachmentErrorLength;
        source << ";mounted_link_shape_rotation_source="
               << Oot3d_TitleIntroOpeningActorRuntimeMountedPlayerShapeRotationSource()
               << ";mounted_link_shape_rot_x=" << drawRotation.X
               << ";mounted_link_shape_rot_y=" << drawRotation.Y
               << ";mounted_link_shape_rot_z=" << drawRotation.Z
               << ";mounted_link_child_root_world_x=" << mountedChildRootWorldPoint.X
               << ";mounted_link_child_root_world_y=" << mountedChildRootWorldPoint.Y
               << ";mounted_link_child_root_world_z=" << mountedChildRootWorldPoint.Z
               << ";mounted_link_parent_anchor_world_x=" << mountedParentAnchorWorldPoint.X
               << ";mounted_link_parent_anchor_world_y=" << mountedParentAnchorWorldPoint.Y
               << ";mounted_link_parent_anchor_world_z=" << mountedParentAnchorWorldPoint.Z
               << ";mounted_link_attachment_error_x=" << mountedAttachmentErrorX
               << ";mounted_link_attachment_error_y=" << mountedAttachmentErrorY
               << ";mounted_link_attachment_error_z=" << mountedAttachmentErrorZ
               << ";mounted_link_attachment_error_length=" << mountedAttachmentErrorLength;
    }
    if (buildDiagnostics && pose.Valid) {
        renderModel.Diagnostics =
            TitleIntroActorTransformDiagnostics(actor, renderModel, pose, skinTransforms,
                                                poseFrame, drawPosition);
        renderModel.Diagnostics["title_actor_render_scale"] = {
            { "actor_init_scale", actor.Scale },
            { "render_scale", renderScale.Scale },
            { "runtime_override_applied", renderScale.RuntimeOverrideApplied },
            { "source", renderScale.Source },
        };
    }
    const auto* mountProducer =
        actorMotion != nullptr && actorMotion->linkMountAttachment.active != 0u
            ? &actorMotion->linkMountAttachment
            : nullptr;
    const int rideActorPoseOffsetBoneIndex =
        clip.Csab != nullptr && mountProducer != nullptr
            ? FindTitleIntroBoneIndexForCsabNode(
                  *clip.Csab, mountProducer->parentPoseOffsetNodeIndex)
            : -1;
    if (isEpona &&
        usePairedMountMotion &&
        pose.Valid &&
        rideActorPoseOffsetBoneIndex >= 0 &&
        pose.WorldTransforms.size() > static_cast<size_t>(rideActorPoseOffsetBoneIndex)) {
        result.RideActorPoseOffsetModel = TransformDemoPoint(
            pose.WorldTransforms[static_cast<size_t>(rideActorPoseOffsetBoneIndex)], {});
        const auto poseOffsetWorldPoint =
            TransformDemoPoint(renderModel.ModelToWorld, result.RideActorPoseOffsetModel);
        result.RideActorPosition = drawPosition;
        result.RideActorPoseOffset = {
            poseOffsetWorldPoint.X - drawPosition.X,
            poseOffsetWorldPoint.Y - drawPosition.Y,
            poseOffsetWorldPoint.Z - drawPosition.Z,
        };
        result.RideActorRotation = drawRotation;
        result.RideActorRootInputValid = true;
        result.RideActorPoseOffsetNodeIndex =
            static_cast<int>(mountProducer->parentPoseOffsetNodeIndex);
        result.RideActorPoseOffsetBoneIndex = rideActorPoseOffsetBoneIndex;
        source << ";epona_mounted_player_offset_source=oot3d_skeleton_node_offset_00408828"
               << ";epona_mounted_player_offset_node_index="
               << mountProducer->parentPoseOffsetNodeIndex
               << ";epona_mounted_player_offset_bone_index="
               << rideActorPoseOffsetBoneIndex
               << ";epona_mounted_player_offset_model_x=" << result.RideActorPoseOffsetModel.X
               << ";epona_mounted_player_offset_model_y=" << result.RideActorPoseOffsetModel.Y
               << ";epona_mounted_player_offset_model_z=" << result.RideActorPoseOffsetModel.Z
               << ";epona_mounted_player_offset_x=" << result.RideActorPoseOffset.X
               << ";epona_mounted_player_offset_y=" << result.RideActorPoseOffset.Y
               << ";epona_mounted_player_offset_z=" << result.RideActorPoseOffset.Z;
    }
    renderModel.Source = source.str();
    renderModel.Bounds = ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelWorldBounds(renderModel);
    ThreeDsRecomp::Oot3d::NativeDemoExpandBoundsByBounds(renderScene.Bounds, renderModel.Bounds);
    result.DrawPosition = drawPosition;
    result.Submitted = true;
    renderScene.ActorVisuals.push_back(std::move(renderModel));
    return result;
}

size_t StripSubmittedTitleIntroActorTexturePayloads(TitleIntroPlayback& playback) {
    if (!playback.Enabled || !playback.Initialized) {
        return 0;
    }

    size_t strippedByteCount = 0;
    if (playback.OpeningFrameRuntimeEponaActorTransformUsedForRender) {
        strippedByteCount += StripSubmittedTitleIntroActorTexturePayloads(playback.EponaActor);
    }
    if (playback.OpeningFrameRuntimeLinkActorTransformUsedForRender) {
        strippedByteCount += StripSubmittedTitleIntroActorTexturePayloads(playback.LinkActor);
    }
    return strippedByteCount;
}
