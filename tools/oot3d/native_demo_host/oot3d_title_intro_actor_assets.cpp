#include "oot3d_title_intro_actor_assets.h"

#include <algorithm>
#include <exception>
#include <iomanip>
#include <sstream>

#include "three_ds_recomp/oot3d/Oot3dNativeDemoScene.h"
#include "three_ds_recomp/oot3d/Oot3dNativeRenderScene.h"

namespace {

void BuildTitleIntroPlayerResourceVisibility(TitleIntroNativeActor& actor) {
    if (actor.OpeningActorBindingRow == nullptr ||
        actor.OpeningActorBindingRow->actorKind != OOT3D_TITLE_INTRO_OPENING_ACTOR_KIND_LINK_ADULT) {
        return;
    }

    actor.ResourceVisibilityRow = &gOot3dTitleIntroPlayerResourceVisibilityRow;
    size_t resourceCount = 0;
    for (const auto& mesh : actor.Model.Meshes) {
        resourceCount = std::max(resourceCount, static_cast<size_t>(mesh.VisibilityId) + 1u);
    }
    actor.NativeResourceVisibility.assign(resourceCount, 0u);
    actor.NativeVisibleResourceCount = Oot3d_TitleIntroPlayerBuildResourceVisibility(
        actor.ResourceVisibilityRow,
        actor.NativeResourceVisibility.data(),
        static_cast<uint32_t>(actor.NativeResourceVisibility.size()));
    actor.ResourceVisibilityStatus =
        actor.NativeVisibleResourceCount == actor.ResourceVisibilityRow->activeResourceIdCount
            ? "resolved_from_native_player_resource_visibility_tables"
            : "native_player_resource_visibility_id_out_of_cmb_range";
}

} // namespace

void AddTitleIntroNativeActorMotionClip(TitleIntroNativeActor& actor,
                                        const char* role,
                                        const char* csabName,
                                        uint16_t animationIndex,
                                        float playSpeedScale,
                                        const char* source) {
    if (csabName == nullptr || csabName[0] == '\0') {
        return;
    }
    for (const auto& existing : actor.MotionClips) {
        if (existing.CsabName == csabName) {
            return;
        }
    }

    TitleIntroNativeActorCsabClip clip;
    clip.Role = role != nullptr ? role : "motion";
    clip.CsabName = csabName;
    clip.AnimationIndex = animationIndex;
    clip.PlaySpeedScale = playSpeedScale;
    try {
        clip.Bytes = ThreeDsRecomp::Oot3d::ExtractZarFileBytes(actor.ArchivePath, clip.CsabName);
        clip.Metadata = ThreeDsRecomp::Oot3d::ParseCsabMetadataBytes(clip.Bytes);
        clip.Loaded = !clip.Bytes.empty() && clip.Metadata.FrameCount > 0;
        clip.Source = source != nullptr ? source : "";
    } catch (const std::exception& ex) {
        clip.Source = std::string("native_motion_csab_load_failed: ") + ex.what();
    }
    actor.MotionClips.push_back(std::move(clip));
}

void LoadTitleIntroNativeActorMotionClips(TitleIntroNativeActor& actor,
                                          const Oot3dTitleIntroActorMotionAnimationRow* motionRow) {
    const uint8_t motionRowDecoded =
        Oot3d_TitleIntroOpeningActorRuntimeIsMotionAnimationRowDecoded(motionRow);
    if (motionRowDecoded == 0u) {
        actor.MotionClipStatus =
            Oot3d_TitleIntroOpeningActorRuntimeMotionClipLoadStatus(0u, 0u, 0u);
        return;
    }

    const char* speedStateSource =
        Oot3d_TitleIntroOpeningActorRuntimeSpeedStateMotionClipSource();
    AddTitleIntroNativeActorMotionClip(actor, "idle", motionRow->idleCsabName,
                                       motionRow->idleAnimationIndex, motionRow->idlePlaySpeedScale,
                                       speedStateSource);
    AddTitleIntroNativeActorMotionClip(actor, "walk", motionRow->walkCsabName,
                                       motionRow->walkAnimationIndex, motionRow->walkPlaySpeedScale,
                                       speedStateSource);
    AddTitleIntroNativeActorMotionClip(actor, "trot", motionRow->trotCsabName,
                                       motionRow->trotAnimationIndex, motionRow->trotPlaySpeedScale,
                                       speedStateSource);
    AddTitleIntroNativeActorMotionClip(actor, "fast", motionRow->fastCsabName,
                                       motionRow->fastAnimationIndex, motionRow->fastPlaySpeedScale,
                                       speedStateSource);

    const auto* gallopRoute = Oot3d_TitleIntroSourceFindHorseStateRouteRow("mounted_gallop_state");
    const bool canLoadGallopCarrot =
        Oot3d_TitleIntroOpeningActorRuntimeCanLoadMountedGallopCarrot(
            actor.OpeningActorBindingRow,
            gallopRoute) != 0u;
    if (canLoadGallopCarrot) {
        AddTitleIntroNativeActorMotionClip(
            actor,
            "gallop_carrot",
            gallopRoute->gallopCarrotCsabName,
            gallopRoute->gallopCarrotAnimationIndex,
            gallopRoute->gallopPlaySpeedMin,
            Oot3d_TitleIntroOpeningActorRuntimeMountedGallopMotionClipSource());
    }

    const size_t expectedClipCount = canLoadGallopCarrot ? 5u : 4u;
    const bool allLoaded = actor.MotionClips.size() == expectedClipCount &&
                           std::all_of(actor.MotionClips.begin(), actor.MotionClips.end(),
                                       [](const TitleIntroNativeActorCsabClip& clip) {
                                           return clip.Loaded;
                                       });
    actor.MotionClipStatus =
        Oot3d_TitleIntroOpeningActorRuntimeMotionClipLoadStatus(
            motionRowDecoded,
            allLoaded ? 1u : 0u,
            canLoadGallopCarrot ? 1u : 0u);
}

TitleIntroNativeActor LoadTitleIntroNativeActor(const std::filesystem::path& romfsRoot,
                                                const Oot3dTitleIntroActorAnimationRow* row,
                                                const Oot3dTitleIntroActorMotionAnimationRow* motionRow,
                                                const Oot3dTitleIntroOpeningActorBindingRow* openingBindingRow,
                                                const std::string& openingBindingStatus,
                                                const Oot3dSceneCutsceneActorResource* resource) {
    TitleIntroNativeActor actor;
    actor.OpeningActorBindingRow = openingBindingRow;
    if (!openingBindingStatus.empty()) {
        actor.OpeningActorBindingStatus = openingBindingStatus;
    }
    actor.AnimationRow = row;
    actor.MotionAnimationRow = motionRow;
    const bool useCutsceneResource = resource != nullptr && resource->valid != 0u;
    actor.Role = useCutsceneResource
                     ? (resource->actorRole != nullptr ? resource->actorRole : "")
                     : (row != nullptr && row->actorRole != nullptr ? row->actorRole : "");
    actor.ArchivePath = romfsRoot /
        (useCutsceneResource
             ? (resource->archivePath != nullptr ? resource->archivePath : "")
             : (row != nullptr && row->archivePath != nullptr ? row->archivePath : ""));
    actor.CmbName = useCutsceneResource
                        ? (resource->cmbName != nullptr ? resource->cmbName : "")
                        : (row != nullptr && row->cmbName != nullptr ? row->cmbName : "");
    actor.CsabName = useCutsceneResource
                         ? (resource->baseCsabName != nullptr ? resource->baseCsabName : "")
                         : (row != nullptr && row->titleVisualCsabName != nullptr
                                ? row->titleVisualCsabName
                                : "");

    if (!useCutsceneResource) {
        const char* animationRowStatus =
            Oot3d_TitleIntroOpeningActorRuntimeAnimationRowStatus(row);
        if (animationRowStatus != nullptr) {
            actor.Status = animationRowStatus;
            return actor;
        }
    }

    if (!std::filesystem::is_regular_file(actor.ArchivePath)) {
        actor.Status = Oot3d_TitleIntroOpeningActorRuntimeArchiveMissingStatus();
        return actor;
    }

    const auto cmbBytes = ThreeDsRecomp::Oot3d::ExtractZarFileBytes(actor.ArchivePath, actor.CmbName);
    actor.Model =
        ThreeDsRecomp::Oot3d::ParseCmbModelBytes(cmbBytes, actor.ArchivePath.string() + "!" + actor.CmbName);
    BuildTitleIntroPlayerResourceVisibility(actor);
    ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelBuildOptions renderOptions;
    renderOptions.BakeTransformIntoVertices = false;
    if (actor.ResourceVisibilityRow != nullptr) {
        renderOptions.ResourceVisibility = &actor.NativeResourceVisibility;
    }
    actor.BaseRenderModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(actor.Model, renderOptions);
    actor.BaseRenderModelBuilt = !actor.BaseRenderModel.Batches.empty();
    actor.CsabBytes = ThreeDsRecomp::Oot3d::ExtractZarFileBytes(actor.ArchivePath, actor.CsabName);
    actor.Csab = ThreeDsRecomp::Oot3d::ParseCsabMetadataBytes(actor.CsabBytes);
    if (useCutsceneResource) {
        for (uint16_t i = 0; i < resource->animationResourceCount; ++i) {
            const auto& animation = resource->animationResources[i];
            if (animation.valid == 0u || animation.csabName == nullptr ||
                actor.CsabName == animation.csabName) {
                continue;
            }
            AddTitleIntroNativeActorMotionClip(
                actor, animation.role, animation.csabName, animation.animationIndex,
                animation.playSpeedScale, animation.source);
        }
        actor.MotionClipStatus = "loaded_from_scene_cutscene_program_snapshot";
    } else {
        LoadTitleIntroNativeActorMotionClips(actor, motionRow);
    }
    actor.BindWorldTransforms = ThreeDsRecomp::Oot3d::BuildCmbSkeletonWorldTransforms(actor.Model.Skeleton);
    actor.Loaded = true;
    actor.Status = Oot3d_TitleIntroOpeningActorRuntimeLoadedFromNativeZarStatus();
    return actor;
}

TitleIntroNativeActor LoadTitleIntroNativeActorFromCutsceneResource(
    const std::filesystem::path& romfsRoot,
    const Oot3dSceneCutsceneActorResource& resource) {
    const auto* binding = Oot3d_TitleIntroOpeningActorRuntimeGetBinding(resource.actorBindingIndex);
    const auto* animationRow =
        Oot3d_TitleIntroOpeningActorRuntimeGetAnimationSourceRow(resource.actorBindingIndex);
    const auto* motionRow =
        Oot3d_TitleIntroOpeningActorRuntimeGetMotionAnimationSourceRow(resource.actorBindingIndex);
    auto actor = LoadTitleIntroNativeActor(
        romfsRoot, animationRow, motionRow, binding,
        "resolved_from_scene_cutscene_program_snapshot", &resource);
    actor.Scale = resource.actorScale;
    actor.NativeActorScale = resource.actorScale;
    actor.NativeActorScaleBase = resource.actorScale;
    actor.ScaleSource = "scene_cutscene_program_actor_resource";
    actor.ScaleStatus = resource.valid != 0u ? "resolved" : "missing_resource";
    return actor;
}

TitleIntroNativeActor LoadTitleIntroNativeActorFromOpeningBinding(
    const std::filesystem::path& romfsRoot,
    const Oot3dTitleIntroOpeningActorBindingRow* binding,
    const std::string& bindingStatus) {
    if (binding == nullptr) {
        TitleIntroNativeActor actor;
        actor.OpeningActorBindingStatus = bindingStatus;
        actor.Status = Oot3d_TitleIntroOpeningActorRuntimeBindingMissingStatus();
        return actor;
    }

    auto actor = LoadTitleIntroNativeActor(
        romfsRoot,
        Oot3d_TitleIntroOpeningActorRuntimeGetAnimationSourceRow(binding->actorBindingIndex),
        Oot3d_TitleIntroOpeningActorRuntimeGetMotionAnimationSourceRow(binding->actorBindingIndex),
        binding,
        bindingStatus);
    return actor;
}

void ApplyTitleIntroNativeActorScale(TitleIntroNativeActor& actor,
                                     const Oot3dTitleIntroActorScaleRow* row,
                                     double fallbackCmbToSceneScale) {
    actor.ScaleRow = row;
    actor.NativeActorScale = row != nullptr ? static_cast<double>(row->actorScale) : 0.0;
    actor.NativeActorScaleBase = actor.NativeActorScale;
    const uint8_t scaleDecoded =
        Oot3d_TitleIntroOpeningActorRuntimeIsScaleRowDecoded(row);
    actor.ScaleSource = Oot3d_TitleIntroOpeningActorRuntimeScaleSource(scaleDecoded);
    actor.ScaleStatus = Oot3d_TitleIntroOpeningActorRuntimeScaleStatus(scaleDecoded);
    if (scaleDecoded == 0u) {
        actor.Scale = fallbackCmbToSceneScale > 0.0 ? fallbackCmbToSceneScale : 1.0;
        return;
    }
    actor.Scale = actor.NativeActorScale;
}

const TitleIntroNativeActorCsabClip* FindTitleIntroNativeActorMotionClip(
    const TitleIntroNativeActor& actor,
    const char* role) {
    if (role == nullptr) {
        return nullptr;
    }
    for (const auto& clip : actor.MotionClips) {
        if (clip.Loaded && clip.Role == role) {
            return &clip;
        }
    }
    return nullptr;
}

const TitleIntroNativeActorCsabClip* FindTitleIntroNativeActorMotionClipByCsabName(
    const TitleIntroNativeActor& actor,
    const char* csabName) {
    if (csabName == nullptr) {
        return nullptr;
    }
    for (const auto& clip : actor.MotionClips) {
        if (clip.Loaded && clip.CsabName == csabName) {
            return &clip;
        }
    }
    return nullptr;
}

namespace {

std::string TitleIntroHexU32(uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << value;
    return out.str();
}

void AppendTitleIntroActorMotionCueSource(
    std::ostringstream& source,
    const Oot3dTitleIntroOpeningActorMotionCueRow* cue) {
    if (cue == nullptr) {
        return;
    }

    source << ";actor_motion_action_id=" << (cue->actionIdHex != nullptr ? cue->actionIdHex : "")
           << ";player_action_consumer_status="
           << (cue->playerActionConsumerStatus != nullptr ? cue->playerActionConsumerStatus : "");
    if (cue->directStateCasePresent != 0 &&
        cue->directStateId != OOT3D_TITLE_INTRO_OPENING_ACTOR_RUNTIME_NO_INDEX) {
        source << ";direct_state_id=" << (cue->directStateIdHex != nullptr ? cue->directStateIdHex : "")
               << ";direct_state_switch=" << TitleIntroHexU32(cue->directStateSwitchFunction)
               << ";direct_state_wrapper=" << TitleIntroHexU32(cue->directStateSwitchWrapperFunction)
               << ";direct_state_setter=" << TitleIntroHexU32(cue->directStateSetterFunction)
               << ";direct_state_handler=" << TitleIntroHexU32(cue->directStateHandlerFunction)
               << ";direct_state_outgoing="
               << (cue->directStateOutgoingTargets != nullptr ? cue->directStateOutgoingTargets : "")
               << ";direct_record_layout_index=" << cue->directRecordLayoutIndex;
        return;
    }

    source << ";direct_state_case=none;motion_consumer_only_no_direct_state_case";
}

void BindTitleIntroNativeActorMorph(
    const TitleIntroNativeActor& actor,
    bool active,
    const char* sourceCsabName,
    float sourceFrame,
    float weight,
    float rate,
    float frames,
    const char* source,
    TitleIntroActorClipSelection& selection) {
    selection.NativeMorphActive = active;
    selection.MorphPoseFrame = sourceFrame;
    selection.MorphWeight = weight;
    selection.MorphRate = rate;
    selection.MorphFrames = frames;
    selection.MorphCsabName = sourceCsabName != nullptr ? sourceCsabName : "";
    selection.MorphSource = source != nullptr ? source : "";
    if (!active || sourceCsabName == nullptr) {
        return;
    }
    if (actor.CsabName == sourceCsabName) {
        selection.MorphCsabBytes = &actor.CsabBytes;
        selection.MorphCsab = &actor.Csab;
        return;
    }
    const auto* clip = FindTitleIntroNativeActorMotionClipByCsabName(actor, sourceCsabName);
    if (clip != nullptr) {
        selection.MorphCsabBytes = &clip->Bytes;
        selection.MorphCsab = &clip->Metadata;
    }
}

} // namespace

TitleIntroActorClipSelection SelectTitleIntroActorClip(
    const TitleIntroNativeActor& actor,
    const Oot3dTitleIntroOpeningActorMotionSample* actorMotion,
    bool usePairedMountMotion,
    const Oot3dSceneCutsceneActorSnapshot* genericActorSnapshot) {
    TitleIntroActorClipSelection selection;
    selection.CsabBytes = &actor.CsabBytes;
    selection.Csab = &actor.Csab;
    selection.CsabName = actor.CsabName;

    if (genericActorSnapshot != nullptr && genericActorSnapshot->animationValid != 0u) {
        selection.AnimationIndex = genericActorSnapshot->animationIndex;
        const char* csabName = genericActorSnapshot->animationCsabName;
        if (csabName != nullptr && actor.CsabName != csabName) {
            const auto* clip = FindTitleIntroNativeActorMotionClipByCsabName(actor, csabName);
            if (clip == nullptr) {
                selection.Source = Oot3d_TitleIntroOpeningActorRuntimeMotionClipMissingFallbackSource();
                return selection;
            }
            selection.CsabBytes = &clip->Bytes;
            selection.Csab = &clip->Metadata;
            selection.CsabName = clip->CsabName;
        }
        selection.MotionRole = genericActorSnapshot->animationRole != nullptr
                                   ? genericActorSnapshot->animationRole
                                   : "";
        selection.PlaySpeedScale = genericActorSnapshot->animationPlaySpeedScale;
        selection.NativeSpeed = genericActorSnapshot->nativeSpeed;
        selection.PoseFrameValid = genericActorSnapshot->animationFrameValid != 0u;
        selection.PoseFrame = genericActorSnapshot->animationFrame;
        selection.Source = genericActorSnapshot->animationSource != nullptr
                               ? genericActorSnapshot->animationSource
                               : "scene_cutscene_frame_runtime_animation";
        BindTitleIntroNativeActorMorph(
            actor,
            genericActorSnapshot->animationMorphActive != 0u,
            genericActorSnapshot->animationMorphSourceCsabName,
            genericActorSnapshot->animationMorphSourceFrame,
            genericActorSnapshot->animationMorphWeight,
            genericActorSnapshot->animationMorphRate,
            genericActorSnapshot->animationMorphFrames,
            genericActorSnapshot->animationMorphSource,
            selection);
        return selection;
    }

    Oot3dTitleIntroOpeningActorClipRuntimeSelection runtimeSelection{};
    if (Oot3d_TitleIntroOpeningActorRuntimeSelectClip(
            actor.OpeningActorBindingRow,
            actor.MotionAnimationRow,
            actorMotion,
            usePairedMountMotion ? 1u : 0u,
            actor.CsabName.c_str(),
            &runtimeSelection) == 0u) {
        return selection;
    }
    selection.AnimationIndex = runtimeSelection.animationIndex;

    switch (runtimeSelection.kind) {
        case OOT3D_TITLE_INTRO_OPENING_ACTOR_CLIP_SELECT_LINK_PLAYER_ACTION_TITLE_VISUAL: {
            const auto* motionRow = runtimeSelection.motionRow;
            std::ostringstream source;
            source << (runtimeSelection.sourceKind != nullptr ? runtimeSelection.sourceKind : "")
                   << ";motion_ref_index=" << motionRow->motionRefIndex
                   << ";native_command_source_index=" << motionRow->nativeCommandSourceIndex
                   << ";local_command_index=" << motionRow->localCommandIndex
                   << ";entry_index=" << motionRow->entryIndex
                   << ";action_id=" << (motionRow->actionIdHex != nullptr ? motionRow->actionIdHex : "")
                   << ";player_action_source_index=" << actorMotion->playerActionSourceIndex;
            AppendTitleIntroActorMotionCueSource(source, actorMotion->cue);
            selection.Source = source.str();
            selection.MotionRole = runtimeSelection.motionRole != nullptr ? runtimeSelection.motionRole : "";
            selection.NativeSpeed = runtimeSelection.nativeSpeed;
            return selection;
        }
        case OOT3D_TITLE_INTRO_OPENING_ACTOR_CLIP_SELECT_PAIRED_MOUNT_TITLE_VISUAL: {
            std::ostringstream source;
            source << (runtimeSelection.sourceKind != nullptr ? runtimeSelection.sourceKind : "")
                   << ";paired_mount_transform_source_kind="
                   << actorMotion->pairedMountVisualTransform.sourceKind;
            if (actorMotion->pairedMountCueRow != nullptr) {
                source << ";actor_cue_index=" << actorMotion->pairedMountCueRow->actorCueIndex
                       << ";cue_command_id=" << actorMotion->pairedMountCueRow->cueCommandId
                       << ";qdb_index=" << actorMotion->pairedMountCueRow->qdbIndex
                       << ";cue_start_frame=" << actorMotion->pairedMountCueRow->startFrame
                       << ";cue_end_frame=" << actorMotion->pairedMountCueRow->endFrame;
            }
            if (actorMotion->pairedMountVisualTransform.motionRow != nullptr) {
                source << ";paired_mount_motion_ref_index="
                       << actorMotion->pairedMountVisualTransform.motionRow->motionRefIndex
                       << ";paired_mount_action_id="
                       << (actorMotion->pairedMountVisualTransform.motionRow->actionIdHex != nullptr
                               ? actorMotion->pairedMountVisualTransform.motionRow->actionIdHex
                               : "");
            }
            source << ";paired_mount_visual_active="
                   << (actorMotion->pairedMountVisualTransform.active != 0 ? "true" : "false")
                   << ";title_visual_csab_name=" << actor.OpeningActorBindingRow->titleVisualCsabName
                   << ";native_speed=" << runtimeSelection.nativeSpeed
                   << ";opening_binding_basis="
                   << (actor.OpeningActorBindingRow->nativeBasis != nullptr
                           ? actor.OpeningActorBindingRow->nativeBasis
                           : "");
            AppendTitleIntroActorMotionCueSource(source, actorMotion->cue);
            selection.Source = source.str();
            selection.MotionRole = runtimeSelection.motionRole != nullptr ? runtimeSelection.motionRole : "";
            selection.PlaySpeedScale = runtimeSelection.playSpeedScale;
            selection.NativeSpeed = runtimeSelection.nativeSpeed;
            return selection;
        }
        case OOT3D_TITLE_INTRO_OPENING_ACTOR_CLIP_SELECT_HORSE_CUTSCENE_ACTION_ROUTE: {
            const auto* routeClip =
                FindTitleIntroNativeActorMotionClipByCsabName(actor, runtimeSelection.csabName);
            if (routeClip == nullptr) {
                selection.Source = Oot3d_TitleIntroOpeningActorRuntimeMotionClipMissingFallbackSource();
                return selection;
            }
            selection.CsabBytes = &routeClip->Bytes;
            selection.Csab = &routeClip->Metadata;
            selection.CsabName = routeClip->CsabName;
            selection.MotionRole = runtimeSelection.motionRole != nullptr
                                       ? runtimeSelection.motionRole
                                       : "horse_cutscene_action";
            selection.PlaySpeedScale = runtimeSelection.playSpeedScale;
            selection.NativeSpeed = runtimeSelection.nativeSpeed;
            selection.PoseFrameValid = runtimeSelection.animationFrameValid != 0u;
            selection.PoseFrame = runtimeSelection.animationFrame;
            selection.Source = runtimeSelection.sourceKind != nullptr
                                   ? runtimeSelection.sourceKind
                                   : "decoded_title_horse_cutscene_action_route";
            BindTitleIntroNativeActorMorph(
                actor,
                runtimeSelection.animationMorphActive != 0u,
                runtimeSelection.animationMorphSourceCsabName,
                runtimeSelection.animationMorphSourceFrame,
                runtimeSelection.animationMorphWeight,
                runtimeSelection.animationMorphRate,
                runtimeSelection.animationMorphFrames,
                runtimeSelection.animationMorphSource,
                selection);
            return selection;
        }
        case OOT3D_TITLE_INTRO_OPENING_ACTOR_CLIP_SELECT_MOUNTED_GALLOP_ROUTE: {
            const auto* gallopRoute = runtimeSelection.gallopRoute;
            const char* clipRole = runtimeSelection.clipRole;
            const auto* gallopClip = FindTitleIntroNativeActorMotionClip(actor, clipRole);
            if (clipRole != nullptr && gallopClip != nullptr) {
                selection.CsabBytes = &gallopClip->Bytes;
                selection.Csab = &gallopClip->Metadata;
                selection.CsabName = gallopClip->CsabName;
                selection.MotionRole = runtimeSelection.motionRole != nullptr ? runtimeSelection.motionRole : "";
                selection.PlaySpeedScale = runtimeSelection.playSpeedScale;
                selection.NativeSpeed = runtimeSelection.nativeSpeed;

                std::ostringstream source;
                source << gallopClip->Source
                       << ";title_state_route=mounted_gallop_state"
                       << ";native_speed=" << runtimeSelection.nativeSpeed
                       << ";native_play_speed=" << runtimeSelection.playSpeedScale
                       << ";play_speed_scale_literal=" << gallopRoute->gallopPlaySpeedScale
                       << ";play_speed_switch=" << gallopRoute->gallopPlaySpeedSwitch
                       << ";play_speed_min=" << gallopRoute->gallopPlaySpeedMin
                       << ";play_speed_max=" << gallopRoute->gallopPlaySpeedMax
                       << ";gallop_e74="
                       << Oot3d_TitleIntroOpeningActorRuntimeMountedGallopAnimationIndex(gallopRoute, clipRole);
                AppendTitleIntroActorMotionCueSource(source, actorMotion->cue);
                source << ";pending_title_qdb_consumer_split";
                selection.Source = source.str();
                return selection;
            }
            return selection;
        }
        case OOT3D_TITLE_INTRO_OPENING_ACTOR_CLIP_SELECT_SPEED_STATE_ROUTE:
            break;
        case OOT3D_TITLE_INTRO_OPENING_ACTOR_CLIP_SELECT_BASE_TITLE_VISUAL:
        default:
            return selection;
    }

    if (actor.MotionClips.empty()) {
        return selection;
    }
    const char* role = runtimeSelection.clipRole;
    const auto* clip = FindTitleIntroNativeActorMotionClip(actor, role);
    if (clip == nullptr) {
        selection.Source = Oot3d_TitleIntroOpeningActorRuntimeMotionClipMissingFallbackSource();
        return selection;
    }

    selection.CsabBytes = &clip->Bytes;
    selection.Csab = &clip->Metadata;
    selection.CsabName = clip->CsabName;
    selection.MotionRole = clip->Role;
    selection.PlaySpeedScale = clip->PlaySpeedScale;
    selection.NativeSpeed = runtimeSelection.nativeSpeed;
    std::ostringstream source;
    source << clip->Source;
    AppendTitleIntroActorMotionCueSource(source, actorMotion->cue);
    selection.Source = source.str();
    return selection;
}

double TitleIntroMountedVisualYOffset(const TitleIntroNativeActor& actor,
                                      const Oot3dTitleIntroOpeningActorMotionSample* actorMotion,
                                      bool usePairedMountMotion) {
    (void)actor;
    (void)actorMotion;
    (void)usePairedMountMotion;
    return 0.0;
}

ThreeDsRecomp::Oot3d::Oot3dDemoVec3 TitleIntroActorDrawPosition(
    const TitleIntroNativeActor& actor,
    const TitleIntroCueSample& cue,
    const Oot3dTitleIntroOpeningActorMotionSample* actorMotion,
    bool usePairedMountMotion) {
    auto position = cue.Position;
    position.Y += TitleIntroMountedVisualYOffset(actor, actorMotion, usePairedMountMotion);
    return position;
}

TitleIntroActorRenderScale ResolveTitleIntroActorRenderScale(
    const TitleIntroNativeActor& actor,
    const Oot3dTitleIntroOpeningActorMotionSample* actorMotion,
    bool usePairedMountMotion) {
    (void)actorMotion;
    (void)usePairedMountMotion;
    TitleIntroActorRenderScale scale;
    scale.Scale = actor.Scale;
    scale.Source = Oot3d_TitleIntroOpeningActorRuntimeActorInitScaleSource();
    return scale;
}

bool TitleIntroNativePairedMountDrawOwnsRider(
    const TitleIntroNativeActor& mountActor,
    const Oot3dTitleIntroOpeningActorMotionSample* actorMotion,
    const TitleIntroActorVisualAppendResult& mountVisual) {
    return Oot3d_TitleIntroOpeningActorRuntimePairedMountDrawOwnsRider(
               mountActor.OpeningActorBindingRow,
               actorMotion,
               mountVisual.Submitted ? 1u : 0u) != 0u;
}

int FindTitleIntroBoneIndexForCsabNode(const ThreeDsRecomp::Oot3d::CsabMetadata& csab,
                                       uint16_t nodeIndex) {
    for (size_t boneIndex = 0; boneIndex < csab.BoneToNodeIndices.size(); ++boneIndex) {
        if (csab.BoneToNodeIndices[boneIndex] == nodeIndex) {
            return static_cast<int>(boneIndex);
        }
    }
    return -1;
}
