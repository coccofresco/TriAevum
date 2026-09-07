#include "oot3d_title_intro_render_binding_diagnostics.h"

std::string TitleIntroOpeningFrameRenderBindingStatus(const TitleIntroPlayback& playback) {
    std::string renderBindingStatus = "diagnostic_runtime_sample_only_pending_backend_actor_camera_logo_binding";
    if (playback.OpeningFrameRuntimeCameraUsedForRender &&
        playback.OpeningFrameRuntimeLinkActorTransformUsedForRender &&
        playback.OpeningFrameRuntimeEponaActorTransformUsedForRender &&
        playback.OpeningFrameRuntimeLogoDrawUsedForRender &&
        playback.OpeningFrameRuntimeLogoDrawSubmittedVisualCount == 0) {
        renderBindingStatus =
            "camera_link_epona_and_logo_draw_bound_native_alpha_zero_no_logo_visual_submitted";
    } else if (playback.OpeningFrameRuntimeCameraUsedForRender &&
        playback.OpeningFrameRuntimeLinkActorTransformUsedForRender &&
        playback.OpeningFrameRuntimeEponaActorTransformUsedForRender &&
        playback.OpeningFrameRuntimeLogoDrawUsedForRender) {
        renderBindingStatus = "camera_link_epona_and_logo_draw_bound_to_render_scene";
    } else if (playback.OpeningFrameRuntimeCameraUsedForRender &&
        playback.OpeningFrameRuntimeLinkActorTransformUsedForRender &&
        playback.OpeningFrameRuntimeEponaActorTransformUsedForRender &&
        !playback.OpeningFrameRuntimeLogoDrawUsedForRender) {
        renderBindingStatus =
            "camera_link_and_epona_actor_bound_to_render_scene_logo_draw_pending_render_binding";
    } else if (playback.OpeningFrameRuntimeCameraUsedForRender &&
        playback.OpeningFrameRuntimeLinkActorSuppressedByPairedMountDraw &&
        playback.OpeningFrameRuntimeEponaActorTransformUsedForRender &&
        playback.OpeningFrameRuntimeLogoDrawUsedForRender) {
        renderBindingStatus =
            "camera_and_epona_bound_link_visual_owned_by_native_paired_mount_draw_logo_bound";
    } else if (playback.OpeningFrameRuntimeCameraUsedForRender &&
        playback.OpeningFrameRuntimeLinkActorSuppressedByPairedMountDraw &&
        playback.OpeningFrameRuntimeEponaActorTransformUsedForRender &&
        !playback.OpeningFrameRuntimeLogoDrawUsedForRender) {
        renderBindingStatus =
            "camera_and_epona_bound_link_visual_owned_by_native_paired_mount_draw_logo_pending";
    } else if (playback.OpeningFrameRuntimeCameraUsedForRender &&
        playback.OpeningFrameRuntimeLinkActorTransformUsedForRender &&
        !playback.OpeningFrameRuntimeEponaActorTransformUsedForRender &&
        !playback.OpeningFrameRuntimeLogoDrawUsedForRender) {
        renderBindingStatus =
            "camera_and_link_actor_bound_to_render_scene_epona_mount_and_logo_draw_pending_render_binding";
    } else if (playback.OpeningFrameRuntimeCameraUsedForRender) {
        renderBindingStatus = "camera_bound_to_render_scene_actor_motion_and_logo_draw_pending_render_binding";
    }
    return renderBindingStatus;
}
