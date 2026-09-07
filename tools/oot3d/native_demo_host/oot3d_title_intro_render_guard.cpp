#include "oot3d_title_intro_render_guard.h"

namespace {
constexpr double kMountedLinkAttachmentErrorTolerance = 0.001;
}

nlohmann::json BuildTitleIntroRenderGuard(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene,
    const TitleIntroPlayback& titleIntro) {
    const bool roomSubmitted = !renderScene.Room.Batches.empty();
    const bool titleActorsSubmitted =
        titleIntro.AddedActorVisualCount >= 2 && renderScene.ActorVisuals.size() >= titleIntro.AddedActorVisualCount;
    const bool linkBound =
        titleIntro.LinkCueResolvedFromOpeningFrameRuntime &&
        titleIntro.OpeningFrameRuntimeLinkActorTransformUsedForRender;
    const bool eponaBound =
        titleIntro.EponaCueResolvedFromOpeningFrameRuntime &&
        titleIntro.OpeningFrameRuntimeEponaActorTransformUsedForRender;
    const bool mountedLinkAttachmentAligned =
        titleIntro.OpeningFrameRuntimeMountedLinkAttachmentErrorValid &&
        titleIntro.OpeningFrameRuntimeMountedLinkAttachmentErrorLength <=
            kMountedLinkAttachmentErrorTolerance;
    const bool kankyoSubmitted =
        renderScene.EnvironmentBackground.NativeKankyoDrawRouteDecoded &&
        renderScene.EnvironmentBackground.UsedForRender &&
        !renderScene.EnvironmentModels.empty();
    const bool nativeFogResolved =
        renderScene.PicaFog.Available &&
        renderScene.PicaFog.RuntimeFogDistanceContractResolved &&
        renderScene.PicaFog.RuntimeFogDistancesUsedForRender &&
        renderScene.PicaFog.ProjectionRangeAvailable &&
        renderScene.PicaFog.SourceFar > renderScene.PicaFog.SourceNear;
    const bool logoRequirementMet =
        titleIntro.OpeningFrameRuntimeLogoDrawVisibleComponentCount == 0 ||
        titleIntro.OpeningFrameRuntimeLogoDrawSubmittedVisualCount > 0;
    const bool passed =
        roomSubmitted &&
        titleActorsSubmitted &&
        linkBound &&
        eponaBound &&
        mountedLinkAttachmentAligned &&
        !titleIntro.QdbActorCueFallbackUsedForRender &&
        kankyoSubmitted &&
        nativeFogResolved &&
        logoRequirementMet;

    return {
        { "passed", passed },
        { "room_batch_count", renderScene.Room.Batches.size() },
        { "actor_visual_count", renderScene.ActorVisuals.size() },
        { "added_title_actor_visual_count", titleIntro.AddedActorVisualCount },
        { "link_bound_from_opening_runtime", linkBound },
        { "epona_bound_from_opening_runtime", eponaBound },
        { "mounted_link_attachment_aligned", mountedLinkAttachmentAligned },
        { "mounted_link_attachment_error_valid",
          titleIntro.OpeningFrameRuntimeMountedLinkAttachmentErrorValid },
        { "mounted_link_attachment_error_length",
          titleIntro.OpeningFrameRuntimeMountedLinkAttachmentErrorLength },
        { "mounted_link_attachment_error_tolerance", kMountedLinkAttachmentErrorTolerance },
        { "qdb_actor_cue_fallback_used", titleIntro.QdbActorCueFallbackUsedForRender },
        { "environment_model_count", renderScene.EnvironmentModels.size() },
        { "kankyo_draw_route_decoded", renderScene.EnvironmentBackground.NativeKankyoDrawRouteDecoded },
        { "kankyo_used_for_render", renderScene.EnvironmentBackground.UsedForRender },
        { "native_fog_resolved", nativeFogResolved },
        { "native_fog_near", renderScene.PicaFog.SourceNear },
        { "native_fog_far", renderScene.PicaFog.SourceFar },
        { "native_projection_near", renderScene.PicaFog.ProjectionNear },
        { "native_projection_far", renderScene.PicaFog.ProjectionFar },
        { "logo_visible_component_count", titleIntro.OpeningFrameRuntimeLogoDrawVisibleComponentCount },
        { "logo_submitted_visual_count", titleIntro.OpeningFrameRuntimeLogoDrawSubmittedVisualCount },
        { "logo_requirement_met", logoRequirementMet },
        { "source",
          "self-test guard against title-intro render regressions that remove native OOT3D room, "
          "Link/Epona, logo, kankyo visuals, or runtime fog, or detach the mounted Player root "
          "from the EnHorse node-14 anchor" },
    };
}
