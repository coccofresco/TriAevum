#include "oot3d_demo_host_self_test.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include <nlohmann/json.hpp>

#include "three_ds_recomp/oot3d/Oot3dNativeDemoScene.h"
#include "three_ds_recomp/oot3d/Oot3dNativeRenderScene.h"
#include "three_ds_recomp/oot3d/Oot3dNativeRenderer.h"

#include "oot3d_demo_host_diagnostics.h"
#include "oot3d_demo_host_io.h"
#include "oot3d_demo_host_self_test_link.h"
#include "oot3d_demo_host_self_test_playback.h"
#include "oot3d_demo_host_summary.h"
#include "oot3d_demo_host_view_projection.h"
#include "oot3d_demo_math.h"
#include "oot3d_link_animation.h"
#include "oot3d_link_collision.h"
#include "oot3d_link_instance.h"
#include "oot3d_link_locomotion_config.h"
#include "oot3d_link_movement.h"
#include "oot3d_link_render.h"
#include "oot3d_link_runtime_types.h"
#include "oot3d_link_surface_state.h"
#include "oot3d_native_bg_camera.h"
#include "oot3d_native_camera_collision.h"
#include "oot3d_native_camera_config.h"
#include "oot3d_native_camera_controller.h"
#include "oot3d_native_camera_normal.h"
#include "oot3d_native_camera_runtime.h"
#include "oot3d_native_camera_scene.h"
#include "oot3d_native_collision_math.h"
#include "oot3d_title_intro_playback_init.h"
#include "oot3d_title_intro_render_scene.h"
void RunSelfTest(const Args& args) {
    auto scene = ThreeDsRecomp::Oot3d::LoadOot3dNativeDemoSceneFromManifest(
        args.ManifestPath, ThreeDsRecomp::Oot3d::kDefaultLinkStandingCsabName, args.EntranceIndex,
        ActiveSceneSetupOverrideForArgs(args), ActiveSceneSetupSourceForArgs(args),
        args.NativePlayerClips ? &*args.NativePlayerClips : nullptr);
    auto renderScene = BuildRenderSceneForMode(args, scene);
    const auto locomotionConfig = BuildLinkNativeLocomotionConfig(scene);
    const auto cameraConfig = BuildNativeCameraConfig(scene);
    LinkInstance link = InitialLinkInstance(scene, locomotionConfig);
    if (TryRunPlaybackSelfTest(args, scene, renderScene, locomotionConfig, cameraConfig, link)) {
        return;
    }
    RunLinkSelfTest(args, scene, renderScene, locomotionConfig, cameraConfig, link);
}
