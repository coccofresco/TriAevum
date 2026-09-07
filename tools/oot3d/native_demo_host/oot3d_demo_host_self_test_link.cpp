#include "oot3d_demo_host_self_test_link.h"

#include <algorithm>
#include <limits>

#include <nlohmann/json.hpp>

#include "three_ds_recomp/oot3d/Oot3dNativeRenderer.h"

#include "oot3d_demo_host_diagnostics.h"
#include "oot3d_demo_host_io.h"
#include "oot3d_demo_host_self_test_camera.h"
#include "oot3d_demo_host_summary.h"
#include "oot3d_link_animation.h"
#include "oot3d_link_collision.h"
#include "oot3d_link_instance.h"
#include "oot3d_link_movement.h"
#include "oot3d_link_render.h"
#include "oot3d_link_surface_state.h"
#include "oot3d_native_collision_math.h"
#include "oot3d_title_intro_render_scene.h"

void RunLinkSelfTest(const Args& args, const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                     ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene,
                     const LinkNativeLocomotionConfig& locomotionConfig, const NativeCameraConfig& cameraConfig,
                     LinkInstance& link) {    LinkAnimation animation = InitialLinkAnimation(scene, locomotionConfig);
    constexpr double selfTestDt = 0.25;
    const LinkMotionState idleStartMotion =
        ApplyLinkMovementIntentWithCollision(scene, locomotionConfig, link, { 1.0, 0.0, 0.0 }, link.MoveSpeed,
                                             selfTestDt);
    const LinkMotionState runningTurnMotion =
        ApplyLinkMovementIntentWithCollision(scene, locomotionConfig, link, { 0.0, 0.0, -1.0 }, link.MoveSpeed,
                                             selfTestDt);
    LinkInstance sharpTurnLink = InitialLinkInstance(scene, locomotionConfig);
    ApplyLinkMovementIntentWithCollision(scene, locomotionConfig, sharpTurnLink, { 1.0, 0.0, 0.0 },
                                         sharpTurnLink.MoveSpeed,
                                         selfTestDt);
    const LinkMotionState sharpTurnMotion =
        ApplyLinkMovementIntentWithCollision(scene, locomotionConfig, sharpTurnLink, { -1.0, 0.0, 0.0 },
                                             sharpTurnLink.MoveSpeed,
                                             1.0 / locomotionConfig.PlayerTickRate);

    LinkInstance wallTestLink = InitialLinkInstance(scene, locomotionConfig);
    SetLinkActorPosition(wallTestLink, { scene.Spawn.X, scene.Spawn.Y, 135.0 });
    ApplyLinkFloorGrounding(scene, wallTestLink);
    const auto wallTestBeforeActor = LinkActorPosition(wallTestLink);
    const LinkMotionState wallTestMotion =
        ApplyLinkMovementIntentWithCollision(scene, locomotionConfig, wallTestLink, { 0.0, 0.0, 1.0 },
                                             wallTestLink.MoveSpeed, selfTestDt);

    LinkInstance wallStabilityLink = InitialLinkInstance(scene, locomotionConfig);
    SetLinkActorPosition(wallStabilityLink, { scene.Spawn.X, scene.Spawn.Y, 135.0 });
    ApplyLinkFloorGrounding(scene, wallStabilityLink);
    constexpr int wallStabilityFrameCount = 90;
    const double wallStabilityDt = 1.0 / locomotionConfig.PlayerTickRate;
    bool wallStabilityContact = false;
    bool wallStabilitySpeedLimitSeen = false;
    double wallStabilityNormalX = 0.0;
    double wallStabilityNormalZ = 0.0;
    double wallStabilityProjectionMin = std::numeric_limits<double>::infinity();
    double wallStabilityProjectionMax = -std::numeric_limits<double>::infinity();
    int wallStabilityContactFrameCount = 0;
    int wallStabilitySampleCount = 0;
    LinkMotionState wallStabilityMotion;
    for (int frame = 0; frame < wallStabilityFrameCount; ++frame) {
        Vec3 wallStabilityDirection = { 0.0, 0.0, 1.0 };
        if (wallStabilityContact) {
            wallStabilityDirection = Normalize({ -wallStabilityNormalX, 0.0, -wallStabilityNormalZ });
        }
        wallStabilityMotion =
            ApplyLinkMovementIntentWithCollision(scene, locomotionConfig, wallStabilityLink, wallStabilityDirection,
                                                 wallStabilityLink.MoveSpeed, wallStabilityDt);
        if (!wallStabilityLink.HorizontalCollision) {
            continue;
        }

        const auto actor = LinkActorPosition(wallStabilityLink);
        if (!wallStabilityContact) {
            wallStabilityContact = true;
            wallStabilityNormalX = wallStabilityLink.WallPushNormalX;
            wallStabilityNormalZ = wallStabilityLink.WallPushNormalZ;
        }
        wallStabilityContactFrameCount++;

        if (wallStabilityContactFrameCount > 10) {
            const double projection = actor.X * wallStabilityNormalX + actor.Z * wallStabilityNormalZ;
            wallStabilityProjectionMin = std::min(wallStabilityProjectionMin, projection);
            wallStabilityProjectionMax = std::max(wallStabilityProjectionMax, projection);
            wallStabilitySampleCount++;
        }
        wallStabilitySpeedLimitSeen =
            wallStabilitySpeedLimitSeen || wallStabilityLink.NativeWallSpeedLimitActive;
    }

    LinkInstance floorSmallDropLink = InitialLinkInstance(scene, locomotionConfig);
    const auto floorDropPreviousActor = LinkActorPosition(floorSmallDropLink);
    auto floorSmallDropActor = floorDropPreviousActor;
    floorSmallDropActor.Y += kNativeGroundSnapDropLimit - 1.0;
    SetLinkActorPosition(floorSmallDropLink, floorSmallDropActor);
    ApplyLinkFloorGrounding(scene, floorSmallDropLink, floorDropPreviousActor);

    LinkInstance floorLargeDropLink = InitialLinkInstance(scene, locomotionConfig);
    auto floorLargeDropActor = floorDropPreviousActor;
    floorLargeDropActor.Y += kNativeGroundSnapDropLimit + 1.0;
    SetLinkActorPosition(floorLargeDropLink, floorLargeDropActor);
    ApplyLinkFloorGrounding(scene, floorLargeDropLink, floorDropPreviousActor);

    AdvanceLinkAnimation(scene, locomotionConfig, animation, runningTurnMotion, selfTestDt);
    ApplyLinkAnimationFrame(scene, locomotionConfig, renderScene, animation, link);
    ReapplyRenderModeAfterLinkPoseUpdate(args, scene, renderScene);
    UpdateLinkActorShadowState(scene, link, renderScene);
    auto wallLimitedRenderScene = ThreeDsRecomp::Oot3d::BuildOot3dNativeDemoRenderScene(scene);
    LinkAnimation wallLimitedAnimation = InitialLinkAnimation(scene, locomotionConfig);
    AdvanceLinkAnimation(scene, locomotionConfig, wallLimitedAnimation, wallStabilityMotion, wallStabilityDt);
    ApplyLinkAnimationFrame(scene, locomotionConfig, wallLimitedRenderScene, wallLimitedAnimation, wallStabilityLink);
    auto blendZoneRenderScene = ThreeDsRecomp::Oot3d::BuildOot3dNativeDemoRenderScene(scene);
    LinkInstance blendZoneLink = InitialLinkInstance(scene, locomotionConfig);
    blendZoneLink.NativeLinearVelocityUnitsPerTick = locomotionConfig.WalkRunThresholdUnitsPerTick + 0.5;
    LinkMotionState blendZoneMotion = runningTurnMotion;
    blendZoneMotion.Moving = true;
    blendZoneMotion.NativeLinearVelocityUnitsPerTick = blendZoneLink.NativeLinearVelocityUnitsPerTick;
    blendZoneMotion.NativeWallSpeedLimitUnitsPerTick = locomotionConfig.RunSpeedUnitsPerTick;
    blendZoneMotion.NativeWallSpeedLimitActive = false;
    blendZoneMotion.NativeWallLimitedWalk = false;
    blendZoneMotion.NativeWalkRunThresholdUnitsPerTick = locomotionConfig.WalkRunThresholdUnitsPerTick;
    blendZoneMotion.LocomotionAnimationClass = "run";
    blendZoneMotion.Speed = blendZoneMotion.NativeLinearVelocityUnitsPerTick * locomotionConfig.PlayerTickRate;
    blendZoneMotion.SpeedRatio = blendZoneLink.MoveSpeed > 0.000001 ? blendZoneMotion.Speed / blendZoneLink.MoveSpeed : 1.0;
    LinkAnimation blendZoneAnimation = InitialLinkAnimation(scene, locomotionConfig);
    blendZoneAnimation.Moving = true;
    blendZoneAnimation.LocomotionAnimationClass = "walk";
    blendZoneAnimation.PreviousLocomotionAnimationClass = "walk";
    blendZoneAnimation.NativeStartBlendWeight = 1.0;
    AdvanceLinkAnimation(scene, locomotionConfig, blendZoneAnimation, blendZoneMotion, wallStabilityDt);
    ApplyLinkAnimationFrame(scene, locomotionConfig, blendZoneRenderScene, blendZoneAnimation, blendZoneLink);
    auto startMorphRenderScene = ThreeDsRecomp::Oot3d::BuildOot3dNativeDemoRenderScene(scene);
    LinkInstance startMorphLink = InitialLinkInstance(scene, locomotionConfig);
    LinkAnimation startMorphAnimation = InitialLinkAnimation(scene, locomotionConfig);
    const double startMorphDt = 0.5 / locomotionConfig.PlayerTickRate;
    const LinkMotionState startMorphMotion =
        ApplyLinkMovementIntentWithCollision(scene, locomotionConfig, startMorphLink, { 1.0, 0.0, 0.0 },
                                             startMorphLink.MoveSpeed, startMorphDt);
    AdvanceLinkAnimation(scene, locomotionConfig, startMorphAnimation, startMorphMotion, startMorphDt);
    ApplyLinkAnimationFrame(scene, locomotionConfig, startMorphRenderScene, startMorphAnimation, startMorphLink);
    auto walkEndRenderScene = ThreeDsRecomp::Oot3d::BuildOot3dNativeDemoRenderScene(scene);
    LinkInstance walkEndLink = InitialLinkInstance(scene, locomotionConfig);
    LinkAnimation walkEndAnimation = InitialLinkAnimation(scene, locomotionConfig);
    const double walkEndDt = 1.0 / locomotionConfig.PlayerTickRate;
    LinkMotionState walkEndMotion;
    for (int frame = 0; frame < 20; ++frame) {
        walkEndMotion =
            ApplyLinkMovementIntentWithCollision(scene, locomotionConfig, walkEndLink, { 1.0, 0.0, 0.0 },
                                                 walkEndLink.MoveSpeed, walkEndDt);
        AdvanceLinkAnimation(scene, locomotionConfig, walkEndAnimation, walkEndMotion, walkEndDt);
        ApplyLinkAnimationFrame(scene, locomotionConfig, walkEndRenderScene, walkEndAnimation, walkEndLink);
    }
    bool walkEndTriggerSeen = false;
    bool walkEndIdleMorphSeen = false;
    LinkAnimation walkEndTriggerAnimation;
    LinkAnimation walkEndIdleMorphAnimation;
    LinkMotionState walkEndTriggerMotion;
    LinkMotionState walkEndIdleMorphMotion;
    for (int frame = 0; frame < 160; ++frame) {
        walkEndMotion =
            ApplyLinkMovementIntentWithCollision(scene, locomotionConfig, walkEndLink, {}, walkEndLink.MoveSpeed,
                                                 walkEndDt);
        AdvanceLinkAnimation(scene, locomotionConfig, walkEndAnimation, walkEndMotion, walkEndDt);
        ApplyLinkAnimationFrame(scene, locomotionConfig, walkEndRenderScene, walkEndAnimation, walkEndLink);

        if (!walkEndTriggerSeen && walkEndAnimation.NativeWalkEndActive) {
            walkEndTriggerSeen = true;
            walkEndTriggerAnimation = walkEndAnimation;
            walkEndTriggerMotion = walkEndMotion;
        }
        if (!walkEndIdleMorphSeen && walkEndAnimation.NativeWalkEndToIdleMorphActive) {
            walkEndIdleMorphSeen = true;
            walkEndIdleMorphAnimation = walkEndAnimation;
            walkEndIdleMorphMotion = walkEndMotion;
        }
        if (walkEndTriggerSeen && walkEndIdleMorphSeen && !walkEndAnimation.NativeWalkEndToIdleMorphActive &&
            walkEndAnimation.LocomotionAnimationClass == "idle") {
            break;
        }
    }

    const auto cameraDiagnostics = BuildCameraSelfTestDiagnostics(scene, locomotionConfig, cameraConfig);

    auto summary = BaseSummary(args, scene, renderScene, locomotionConfig, cameraConfig);
    summary["self_test"] = true;
    summary["link_instance"] = LinkInstanceToJson(scene, link, locomotionConfig);
    summary["link_motion"] = LinkMotionToJson(runningTurnMotion);
    summary["link_idle_start_motion"] = LinkMotionToJson(idleStartMotion);
    summary["link_running_turn_motion"] = LinkMotionToJson(runningTurnMotion);
    summary["link_sharp_turn_motion"] = LinkMotionToJson(sharpTurnMotion);
    summary["link_wall_collision_test"] = {
        { "before_actor_position",
          { { "x", wallTestBeforeActor.X }, { "y", wallTestBeforeActor.Y }, { "z", wallTestBeforeActor.Z } } },
        { "after_link_instance", LinkInstanceToJson(scene, wallTestLink, locomotionConfig) },
        { "motion", LinkMotionToJson(wallTestMotion) },
    };
    summary["link_wall_stability_test"] = {
        { "frame_count", wallStabilityFrameCount },
        { "contact_frame_count", wallStabilityContactFrameCount },
        { "sample_count", wallStabilitySampleCount },
        { "contact_seen", wallStabilityContact },
        { "wall_speed_limit_seen", wallStabilitySpeedLimitSeen },
        { "contact_projection_span",
          wallStabilitySampleCount > 0 ? wallStabilityProjectionMax - wallStabilityProjectionMin : 0.0 },
        { "final_link_instance", LinkInstanceToJson(scene, wallStabilityLink, locomotionConfig) },
        { "final_motion", LinkMotionToJson(wallStabilityMotion) },
    };
    summary["link_floor_drop_continuity_test"] = {
        { "threshold_units", kNativeGroundSnapDropLimit },
        { "basis", kNativeFloorProbeBasis },
        { "small_drop_link_instance", LinkInstanceToJson(scene, floorSmallDropLink, locomotionConfig) },
        { "large_drop_link_instance", LinkInstanceToJson(scene, floorLargeDropLink, locomotionConfig) },
    };
    summary["link_animation"] = LinkAnimationToJson(scene, animation, locomotionConfig);
    summary["link_wall_limited_animation"] = LinkAnimationToJson(scene, wallLimitedAnimation, locomotionConfig);
    summary["link_blend_zone_motion"] = LinkMotionToJson(blendZoneMotion);
    summary["link_blend_zone_animation"] = LinkAnimationToJson(scene, blendZoneAnimation, locomotionConfig);
    summary["link_start_morph_motion"] = LinkMotionToJson(startMorphMotion);
    summary["link_start_morph_animation"] = LinkAnimationToJson(scene, startMorphAnimation, locomotionConfig);
    summary["link_walk_end_test"] = {
        { "trigger_seen", walkEndTriggerSeen },
        { "idle_morph_seen", walkEndIdleMorphSeen },
        { "trigger_motion", LinkMotionToJson(walkEndTriggerMotion) },
        { "trigger_animation", LinkAnimationToJson(scene, walkEndTriggerAnimation, locomotionConfig) },
        { "idle_morph_motion", LinkMotionToJson(walkEndIdleMorphMotion) },
        { "idle_morph_animation", LinkAnimationToJson(scene, walkEndIdleMorphAnimation, locomotionConfig) },
        { "final_motion", LinkMotionToJson(walkEndMotion) },
        { "final_animation", LinkAnimationToJson(scene, walkEndAnimation, locomotionConfig) },
    };
    AddCameraSelfTestSummary(summary, scene, locomotionConfig, cameraConfig, cameraDiagnostics);
    summary["world_to_clip_matrix_ready"] = true;
    WriteJsonFile(args.OutputPath, summary);
}
