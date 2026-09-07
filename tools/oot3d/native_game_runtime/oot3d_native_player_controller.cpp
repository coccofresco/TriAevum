#include "oot3d_native_player_controller.h"

#include "oot3d_demo_host_link_controls.h"
#include "oot3d_link_animation.h"
#include "oot3d_link_collision.h"
#include "oot3d_link_instance.h"
#include "oot3d_link_movement.h"
#include "oot3d_link_render.h"
#include "oot3d_native_player_action_runtime.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Oot3dNativeGame {
namespace {

size_t RequirePlayerClipIndex(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                              const char* clipId) {
    for (size_t index = 0; index < scene.LinkCsabClips.size(); ++index) {
        if (scene.LinkCsabClips[index].Id == clipId) {
            return index;
        }
    }
    throw std::runtime_error(std::string("native player action clip is missing: ") + clipId);
}

void SelectPlayerActionAnimation(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                                 LinkAnimation& animation, size_t clipIndex,
                                 const char* actionName,
                                 double framesPerSecond) {
    const bool changed = animation.ClipIndex != clipIndex;
    const auto& clip = scene.LinkCsabClips.at(clipIndex);
    animation.ClipIndex = clipIndex;
    animation.ClipId = clip.Id;
    animation.CsabName = clip.CsabName;
    animation.FrameCount = clip.Metadata.FrameCount;
    animation.FramesPerSecond = framesPerSecond;
    animation.Moving = false;
    animation.NativeLocomotionControllerActive = false;
    animation.NativeLocomotionState = actionName;
    animation.PreviousLocomotionAnimationClass = animation.LocomotionAnimationClass;
    animation.LocomotionAnimationClass = actionName;
    animation.NativeCycleFramesPerSecond = 0.0;
    animation.NativeWalkRunBlendWeight = 0.0;
    animation.NativeWalkRunBlendActive = false;
    animation.NativeStartMorphActive = false;
    animation.NativeWalkEndActive = false;
    animation.NativeWalkEndEntryMorphActive = false;
    animation.NativeWalkEndToIdleMorphActive = false;
    animation.PoseBlendActive = false;
    if (changed) {
        animation.Frame = 0.0;
    }
}

int16_t NativeYawS16(double yaw) {
    const long long value = std::llround(yaw / kOot3dS16AngleToRadians);
    return static_cast<int16_t>(static_cast<uint16_t>(value));
}

AnimTransform ToAnimTransform(const ThreeDsRecomp::Oot3d::Matrix4f& matrix) {
    AnimTransform out;
    for (size_t row = 0; row < 3; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            out.Rows[row][column] = matrix.M[row][column];
        }
    }
    return out;
}

void CopyAnimTransform(const AnimTransform& source, ThreeDsRecomp::Oot3d::Matrix4f& destination) {
    for (size_t row = 0; row < 3; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            destination.M[row][column] = source.Rows[row][column];
        }
    }
}

Vec3f TranslationOf(const ThreeDsRecomp::Oot3d::Matrix4f& matrix) {
    return { matrix.M[0][3], matrix.M[1][3], matrix.M[2][3] };
}

void ConsumePlayerRootMotion(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, LinkAnimation& animation,
    LinkInstance& link, RootMotionState& rootMotion, bool& initialized) {
    if (!animation.CurrentPose.Valid ||
        animation.CurrentPose.LocalTransforms.size() != scene.LinkModel.Skeleton.Bones.size() ||
        !scene.LinkPoseSamplingPolicyAvailable ||
        scene.LinkPoseSamplingPolicy.SpecialBone < 0 ||
        scene.LinkPoseSamplingPolicy.SpecialBone >
            static_cast<int32_t>(std::numeric_limits<uint8_t>::max()) ||
        static_cast<size_t>(scene.LinkPoseSamplingPolicy.SpecialBone) >=
            animation.CurrentPose.LocalTransforms.size()) {
        throw std::runtime_error("native player root-motion pose contract is unavailable");
    }

    const size_t specialBone = static_cast<size_t>(scene.LinkPoseSamplingPolicy.SpecialBone);
    if (!initialized) {
        rootMotion = {};
        rootMotion.MovementFlags = kRootMotionUpdateY;
        rootMotion.SpecialLimb = static_cast<uint8_t>(specialBone);
        rootMotion.PreviousRotation = NativeYawS16(link.Yaw);
        rootMotion.PreviousTranslation =
            TranslationOf(animation.CurrentPose.LocalTransforms[specialBone]);
        rootMotion.BaseTranslation = {
            scene.LinkRootBaseTranslation[0], scene.LinkRootBaseTranslation[1],
            scene.LinkRootBaseTranslation[2]
        };
        initialized = true;
    }

    std::vector<AnimTransform> joints;
    joints.reserve(animation.CurrentPose.LocalTransforms.size());
    for (const auto& matrix : animation.CurrentPose.LocalTransforms) {
        joints.push_back(ToAnimTransform(matrix));
    }
    const auto actorPosition = LinkActorPosition(link);
    ActorTransform actor;
    actor.Position = {
        static_cast<float>(actorPosition.X), static_cast<float>(actorPosition.Y),
        static_cast<float>(actorPosition.Z)
    };
    actor.Scale = {
        static_cast<float>(link.Scale), static_cast<float>(link.Scale),
        static_cast<float>(link.Scale)
    };
    ApplyRootMotionToActor(rootMotion, joints, NativeYawS16(link.Yaw), 1.0f, actor);
    SetLinkActorPosition(link, { actor.Position.X, actor.Position.Y, actor.Position.Z });
    CopyAnimTransform(joints[specialBone],
                      animation.CurrentPose.LocalTransforms[specialBone]);
    if (!ThreeDsRecomp::Oot3d::RecomposeCsabPoseWorldTransforms(
            scene.LinkModel.Skeleton, animation.CurrentPose)) {
        throw std::runtime_error("native player pose recomposition failed after root motion");
    }
}

void ConstrainSurfaceClimbActor(LinkInstance& link) {
    auto actor = LinkActorPosition(link);
    actor.X = link.NativeSurfaceClimbAnchorActorPosition.X;
    actor.Z = link.NativeSurfaceClimbAnchorActorPosition.Z;
    actor.Y = std::clamp(
        actor.Y, link.NativeSurfaceClimbMinimumY,
        link.NativeSurfaceClimbMaximumY);
    SetLinkActorPosition(link, actor);
}

} // namespace

PlayerController::PlayerController(
    LinkNativeCollisionActionConfig collisionActionConfig,
    LinkNativePlayerActionConfig playerActionConfig)
    : mCollisionActionConfig(std::move(collisionActionConfig)),
      mPlayerActionConfig(std::move(playerActionConfig)) {
}

void PlayerController::ResetSurfaceClimbPlayback() {
    mSurfaceClimbPhase = SurfaceClimbPhase::Start;
    mSurfaceClimbEntryClip = SurfaceClimbEntryClip::FreeBack;
    mSurfaceClimbFootPhase =
        std::abs(mCollisionActionConfig.GroundClimbInitialPhase) & 1;
    mSurfaceClimbVerticalIntent = 0.0;
    mSurfaceClimbPlaybackDirection = 1.0;
    mSurfaceClimbDismountFloorY = 0.0;
    mSurfaceClimbDismountFloorPolygonIndex = -1;
    mSurfaceClimbDismountFloorSurfaceType = -1;
    mSurfaceClimbDismountTarget = {};
    mSurfaceClimbDismountTargetValid = false;
    mSurfaceClimbAnimationComplete = false;
    mSurfaceClimbRootMotionInitialized = false;
}

void PlayerController::BeginSurfaceClimb(
    LinkInstance& link, const LinkNativeSurfaceClimbQueryResult& surface,
    SurfaceClimbEntryClip entryClip) {
    BeginNativePlayerSurfaceClimb(link, surface);
    ResetSurfaceClimbPlayback();
    mSurfaceClimbEntryClip = entryClip;
    mClipRuntime.Reset();
}

LinkMotionState PlayerController::UpdateMovement(
    const Oot3dDemoHostInputState& inputState,
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
    const LinkNativeLocomotionConfig& locomotionConfig,
    const Camera& camera,
    LinkInstance& link,
    const LinkInstance& resetLink,
    double deltaSeconds) {
    link.ColliderRadius = mCollisionActionConfig.WallCheckRadius;
    link.NativeWallCheckHeight = mCollisionActionConfig.StandingWallCheckHeight;
    link.NativeWallSpeedScalePerS16 = mCollisionActionConfig.WallYawSpeedScale;
    link.NativeMinimumWallSpeedLimitUnitsPerTick =
        mCollisionActionConfig.MinimumWallSpeed;

    if (inputState.Reset) {
        link = resetLink;
        link.ColliderRadius = mCollisionActionConfig.WallCheckRadius;
        link.NativeWallCheckHeight = mCollisionActionConfig.StandingWallCheckHeight;
        link.NativeWallSpeedScalePerS16 = mCollisionActionConfig.WallYawSpeedScale;
        link.NativeMinimumWallSpeedLimitUnitsPerTick =
            mCollisionActionConfig.MinimumWallSpeed;
        mClipRuntime.Reset();
        mAirborneLandingAnticipation = false;
        mLandingAnimationComplete = false;
        mLedgeHoldIntroComplete = false;
        mLedgeClimbAnimationComplete = false;
        mLedgeRootMotionInitialized = false;
        mSceneEntranceInitialized = false;
        ResetSurfaceClimbPlayback();
        LinkMotionState resetMotion;
        PopulateNativePlayerActionMotion(link, resetMotion);
        return resetMotion;
    }

    if (!mSceneEntranceInitialized) {
        mSceneEntranceInitialized = true;
        if (scene.PlayerStart.Valid && BeginNativePlayerSceneEntrance(
                link, scene.PlayerStart.Params, mPlayerActionConfig)) {
            mClipRuntime.Reset();
        }
    }

    if (link.NativePlayerAction == LinkNativePlayerAction::SceneEntrance) {
        UpdateNativePlayerSceneEntranceDistance(link);
        const double targetSpeed = AdvanceNativePlayerSceneEntrance(
            link, mPlayerActionConfig, locomotionConfig.PlayerTickRate,
            deltaSeconds);
        const auto actor = LinkActorPosition(link);
        const Vec3 targetDirection = Normalize({
            link.NativeSceneEntranceTargetActorPosition.X - actor.X, 0.0,
            link.NativeSceneEntranceTargetActorPosition.Z - actor.Z,
        });
        LinkMotionState entranceMotion = ApplyLinkMovementIntentWithCollision(
            scene, locomotionConfig, link, targetDirection,
            targetSpeed > 0.0 ? link.MoveSpeed : 0.0, deltaSeconds);
        UpdateNativePlayerSceneEntranceDistance(link);
        if (NativePlayerSceneEntranceShouldComplete(link)) {
            CompleteNativePlayerSceneEntrance(link, locomotionConfig);
        }
        PopulateNativePlayerActionMotion(link, entranceMotion);
        return entranceMotion;
    }

    if (mLandingAnimationComplete &&
        link.NativePlayerAction == LinkNativePlayerAction::Landing) {
        CompleteNativePlayerLanding(link);
        mLandingAnimationComplete = false;
    }

    if (mLedgeClimbAnimationComplete &&
        link.NativePlayerAction == LinkNativePlayerAction::LedgeClimb) {
        CompleteNativePlayerLedgeClimb(link);
        mLedgeClimbAnimationComplete = false;
        mLedgeHoldIntroComplete = false;
        mLedgeRootMotionInitialized = false;
        mClipRuntime.Reset();
    }

    if (link.NativePlayerAction == LinkNativePlayerAction::LedgeHold) {
        const Vec3 input = LinkInputDirection(inputState, camera);
        const Vec3 wallward = Normalize({
            -link.NativeLedgePushNormalX, 0.0, -link.NativeLedgePushNormalZ
        });
        if (mLedgeHoldIntroComplete && Dot(input, wallward) >
                mCollisionActionConfig.ClimbInputMinimumWallwardDotExclusive) {
            BeginNativePlayerLedgeClimb(link);
            mLedgeClimbAnimationComplete = false;
            mLedgeRootMotionInitialized = false;
            mClipRuntime.Reset();
        }
        LinkMotionState motion;
        PopulateNativePlayerActionMotion(link, motion);
        return motion;
    }
    if (link.NativePlayerAction == LinkNativePlayerAction::LedgeClimb) {
        LinkMotionState motion;
        PopulateNativePlayerActionMotion(link, motion);
        return motion;
    }
    if (link.NativePlayerAction == LinkNativePlayerAction::SurfaceClimb) {
        const Vec3 input = LinkInputDirection(inputState, camera);
        const Vec3 wallward = Normalize({
            -link.NativeSurfaceClimbPushNormalX, 0.0,
            -link.NativeSurfaceClimbPushNormalZ
        });
        mSurfaceClimbVerticalIntent = Dot(input, wallward);
        const bool hasVerticalIntent =
            std::abs(mSurfaceClimbVerticalIntent) >
            std::numeric_limits<double>::epsilon();
        if (hasVerticalIntent) {
            mSurfaceClimbPlaybackDirection =
                std::copysign(1.0, mSurfaceClimbVerticalIntent);
        }

        if (mSurfaceClimbAnimationComplete) {
            const auto completedPhase = mSurfaceClimbPhase;
            mSurfaceClimbAnimationComplete = false;
            mSurfaceClimbRootMotionInitialized = false;
            mClipRuntime.Reset();

            if (completedPhase == SurfaceClimbPhase::Start) {
                mSurfaceClimbPhase = SurfaceClimbPhase::Hold;
            } else if (completedPhase == SurfaceClimbPhase::UpLeft ||
                       completedPhase == SurfaceClimbPhase::UpRight) {
                const int completedFootPhase =
                    completedPhase == SurfaceClimbPhase::UpLeft ? 0 : 1;
                const double actorY = LinkActorPosition(link).Y;
                bool boundaryTransition = false;

                if (mSurfaceClimbVerticalIntent > 0.0) {
                    if (link.NativeClimbMode == LinkNativeClimbMode::RegularLadder) {
                        const auto topFloor = FindLinkNativeClimbTopFloor(
                            scene, link, mCollisionActionConfig);
                        link.NativeSurfaceClimbTopFloorPolygonIndex =
                            topFloor.FloorPolygonIndex;
                        link.NativeSurfaceClimbTopFloorY = topFloor.FloorY;
                        if (topFloor.Available && topFloor.FloorY > actorY) {
                            mSurfaceClimbDismountFloorY = topFloor.FloorY;
                            mSurfaceClimbDismountFloorPolygonIndex =
                                topFloor.FloorPolygonIndex;
                            mSurfaceClimbDismountFloorSurfaceType =
                                topFloor.FloorSurfaceType;
                            mSurfaceClimbDismountTarget = topFloor.ProbePosition;
                            mSurfaceClimbDismountTarget.Y = topFloor.FloorY;
                            mSurfaceClimbDismountTargetValid = true;
                            mSurfaceClimbPhase = completedFootPhase == 0
                                ? SurfaceClimbPhase::DismountTopBackRight
                                : SurfaceClimbPhase::DismountTopBackLeft;
                            boundaryTransition = true;
                        }
                    } else {
                        const auto ledge = FindLinkNativeLedge(
                            scene, link, mCollisionActionConfig);
                        link.NativeLedgeQueryStatus =
                            LinkNativeLedgeQueryStatusName(ledge.Status);
                        if (ledge.Available &&
                            ledge.YDistance <=
                                mCollisionActionConfig.SurfaceClimbTopReachHeight) {
                            BeginNativePlayerLedgeHold(link, ledge);
                            mLedgeHoldIntroComplete = false;
                            mLedgeClimbAnimationComplete = false;
                            mLedgeRootMotionInitialized = false;
                            ResetSurfaceClimbPlayback();
                            mClipRuntime.Reset();
                            LinkMotionState surfaceMotion;
                            PopulateNativePlayerActionMotion(link, surfaceMotion);
                            return surfaceMotion;
                        }
                    }
                }

                if (!boundaryTransition && mSurfaceClimbVerticalIntent < 0.0 &&
                    actorY <= link.NativeSurfaceClimbMinimumY +
                        mCollisionActionConfig.SurfaceClimbBottomDismountFloorDelta) {
                    if (link.NativeClimbMode == LinkNativeClimbMode::RegularLadder) {
                        mSurfaceClimbDismountFloorY =
                            link.NativeSurfaceClimbMinimumY;
                        mSurfaceClimbPhase = completedFootPhase == 0
                            ? SurfaceClimbPhase::DismountBottomLeft
                            : SurfaceClimbPhase::DismountBottomRight;
                        boundaryTransition = true;
                    } else {
                        const double bottomY = link.NativeSurfaceClimbMinimumY;
                        CompleteNativePlayerSurfaceClimb(link, true, bottomY);
                        ApplyLinkFloorGrounding(scene, link);
                        ResetSurfaceClimbPlayback();
                        mClipRuntime.Reset();
                        LinkMotionState surfaceMotion;
                        PopulateNativePlayerActionMotion(link, surfaceMotion);
                        return surfaceMotion;
                    }
                }

                if (!boundaryTransition) {
                    mSurfaceClimbFootPhase = completedFootPhase ^ 1;
                    mSurfaceClimbPhase = hasVerticalIntent
                        ? (mSurfaceClimbFootPhase == 0
                               ? SurfaceClimbPhase::UpLeft
                               : SurfaceClimbPhase::UpRight)
                        : SurfaceClimbPhase::Hold;
                }
            } else {
                if (mSurfaceClimbDismountTargetValid) {
                    SetLinkActorPosition(link, mSurfaceClimbDismountTarget);
                    CompleteNativePlayerSurfaceClimb(
                        link, true, mSurfaceClimbDismountFloorY);
                    link.FloorPolygonIndex =
                        mSurfaceClimbDismountFloorPolygonIndex;
                    link.FloorSurfaceType =
                        mSurfaceClimbDismountFloorSurfaceType;
                } else {
                    CompleteNativePlayerSurfaceClimb(
                        link, true, mSurfaceClimbDismountFloorY);
                    ApplyLinkFloorGrounding(scene, link);
                }
                ResetSurfaceClimbPlayback();
                mClipRuntime.Reset();
                LinkMotionState surfaceMotion;
                PopulateNativePlayerActionMotion(link, surfaceMotion);
                return surfaceMotion;
            }
        }

        if (mSurfaceClimbPhase == SurfaceClimbPhase::Hold && hasVerticalIntent) {
            mSurfaceClimbPhase = mSurfaceClimbFootPhase == 0
                ? SurfaceClimbPhase::UpLeft
                : SurfaceClimbPhase::UpRight;
            mSurfaceClimbRootMotionInitialized = false;
            mClipRuntime.Reset();
        }
        LinkMotionState surfaceMotion;
        PopulateNativePlayerActionMotion(link, surfaceMotion);
        return surfaceMotion;
    }

    const bool wasGrounded = link.Grounded;
    LinkMotionState motion;
    if (link.NativePlayerAction == LinkNativePlayerAction::Landing) {
        motion = ApplyLinkMovementIntentWithCollision(
            scene, locomotionConfig, link, {}, 0.0, deltaSeconds);
    } else {
        motion = UpdateLinkInstance(
            inputState, scene, locomotionConfig, camera, link, resetLink,
            deltaSeconds);
    }

    const double actorY = LinkActorPosition(link).Y;
    if (link.NativePlayerAction == LinkNativePlayerAction::Locomotion && !link.Grounded) {
        if (NativePlayerCanAutoJump(link, locomotionConfig, wasGrounded)) {
            BeginNativePlayerAutoJump(link, locomotionConfig, actorY);
            mAirborneLandingAnticipation = false;
            mClipRuntime.Reset();
        } else {
            BeginNativePlayerAirborne(link, actorY);
            mAirborneLandingAnticipation = false;
            mClipRuntime.Reset();
        }
    } else if (link.NativePlayerAction == LinkNativePlayerAction::Landing && !link.Grounded) {
        BeginNativePlayerAirborne(link, actorY);
        mAirborneLandingAnticipation = false;
        mClipRuntime.Reset();
    }

    if (NativePlayerIsAirborne(link)) {
        if (link.Grounded) {
            BeginNativePlayerLanding(link, locomotionConfig, actorY);
        } else {
            const auto ledge = FindLinkNativeLedge(scene, link, mCollisionActionConfig);
            link.NativeLedgeQueryStatus = LinkNativeLedgeQueryStatusName(ledge.Status);
            if (NativePlayerCanGrabLedge(link, mCollisionActionConfig, ledge)) {
                BeginNativePlayerLedgeHold(link, ledge);
                mClipRuntime.Reset();
                mLedgeHoldIntroComplete = false;
                mLedgeClimbAnimationComplete = false;
                mLedgeRootMotionInitialized = false;
                PopulateNativePlayerActionMotion(link, motion);
                return motion;
            }
            const auto verticalStep = AdvanceNativePlayerAirborne(
                link, locomotionConfig, deltaSeconds);
            if (verticalStep.DeltaY != 0.0) {
                ApplyLinkCollisionConstrainedTranslation(
                    scene, locomotionConfig, link, { 0.0, verticalStep.DeltaY, 0.0 });
            }
            const auto surface = FindLinkNativeSurfaceClimb(
                scene, link, mCollisionActionConfig);
            link.NativeSurfaceClimbQueryStatus =
                LinkNativeSurfaceClimbQueryStatusName(surface.Status);
            if (surface.Available &&
                link.NativeVerticalVelocityUnitsPerTick <= 0.0) {
                BeginSurfaceClimb(
                    link, surface, SurfaceClimbEntryClip::FreeBack);
                PopulateNativePlayerActionMotion(link, motion);
                return motion;
            }
            const double updatedActorY = LinkActorPosition(link).Y;
            UpdateNativePlayerFallDistance(link, updatedActorY);
            if (link.Grounded) {
                BeginNativePlayerLanding(link, locomotionConfig, updatedActorY);
            } else if (!mAirborneLandingAnticipation &&
                       NativePlayerShouldUseLandingAnticipation(link)) {
                mAirborneLandingAnticipation = true;
                mClipRuntime.Reset();
            }
        }
    }

    if (link.NativePlayerAction == LinkNativePlayerAction::Locomotion &&
        link.HorizontalCollision) {
        auto surface = FindLinkNativeSurfaceClimb(
            scene, link, mCollisionActionConfig);
        SurfaceClimbEntryClip entryClip = SurfaceClimbEntryClip::FreeFront;
        if (!surface.Available) {
            surface = FindLinkNativeRegularLadder(
                scene, link, mCollisionActionConfig);
            entryClip = SurfaceClimbEntryClip::LadderFront;
        }
        link.NativeSurfaceClimbQueryStatus =
            LinkNativeSurfaceClimbQueryStatusName(surface.Status);
        const Vec3 input = LinkInputDirection(inputState, camera);
        const Vec3 wallward = Normalize({
            -surface.WallPushNormal.X, 0.0, -surface.WallPushNormal.Z
        });
        if (surface.Available &&
            Dot(input, wallward) >
                mCollisionActionConfig.ClimbInputMinimumWallwardDotExclusive) {
            BeginSurfaceClimb(link, surface, entryClip);
            mSurfaceClimbVerticalIntent = Dot(input, wallward);
            PopulateNativePlayerActionMotion(link, motion);
            return motion;
        }
    }

    PopulateNativePlayerActionMotion(link, motion);
    return motion;
}

void PlayerController::UpdateAnimation(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
    const LinkNativeLocomotionConfig& locomotionConfig,
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene,
    LinkAnimation& animation,
    const LinkMotionState& motion,
    LinkInstance& link,
    double deltaSeconds) {
    if (link.NativePlayerAction != LinkNativePlayerAction::Locomotion &&
        link.NativePlayerAction != LinkNativePlayerAction::SceneEntrance) {
        const bool autoJump = link.NativePlayerAction == LinkNativePlayerAction::AutoJump;
        const bool airborne = NativePlayerIsAirborne(link);
        const bool landingAnticipation =
            airborne && mAirborneLandingAnticipation;
        const bool landing = link.NativePlayerAction == LinkNativePlayerAction::Landing;
        const bool ledgeHold = link.NativePlayerAction == LinkNativePlayerAction::LedgeHold;
        const bool ledgeClimb = link.NativePlayerAction == LinkNativePlayerAction::LedgeClimb;
        const bool surfaceClimb =
            link.NativePlayerAction == LinkNativePlayerAction::SurfaceClimb;
        if (surfaceClimb) {
            const bool hold = mSurfaceClimbPhase == SurfaceClimbPhase::Hold;
            const bool start = mSurfaceClimbPhase == SurfaceClimbPhase::Start;
            const bool dismount =
                mSurfaceClimbPhase == SurfaceClimbPhase::DismountBottomLeft ||
                mSurfaceClimbPhase == SurfaceClimbPhase::DismountBottomRight ||
                mSurfaceClimbPhase == SurfaceClimbPhase::DismountTopBackLeft ||
                mSurfaceClimbPhase == SurfaceClimbPhase::DismountTopBackRight;
            const char* entryClipId = "surface_climb_start_back";
            if (mSurfaceClimbEntryClip == SurfaceClimbEntryClip::FreeFront) {
                entryClipId = "surface_climb_start_front";
            } else if (mSurfaceClimbEntryClip ==
                       SurfaceClimbEntryClip::LadderFront) {
                entryClipId = "ladder_climb_start_front";
            }

            const char* clipId = entryClipId;
            switch (mSurfaceClimbPhase) {
                case SurfaceClimbPhase::Start:
                case SurfaceClimbPhase::Hold:
                    break;
                case SurfaceClimbPhase::UpLeft:
                    clipId = link.NativeClimbMode == LinkNativeClimbMode::RegularLadder
                        ? "ladder_climb_up_left" : "surface_climb_up_left";
                    break;
                case SurfaceClimbPhase::UpRight:
                    clipId = link.NativeClimbMode == LinkNativeClimbMode::RegularLadder
                        ? "ladder_climb_up_right" : "surface_climb_up_right";
                    break;
                case SurfaceClimbPhase::DismountBottomLeft:
                    clipId = "ladder_climb_end_front_left";
                    break;
                case SurfaceClimbPhase::DismountBottomRight:
                    clipId = "ladder_climb_end_front_right";
                    break;
                case SurfaceClimbPhase::DismountTopBackLeft:
                    clipId = "ladder_climb_end_back_left";
                    break;
                case SurfaceClimbPhase::DismountTopBackRight:
                    clipId = "ladder_climb_end_back_right";
                    break;
            }
            const size_t clipIndex = RequirePlayerClipIndex(scene, clipId);
            const double playSpeed = start ? 1.0 : (hold ? 0.0 :
                (dismount ? mCollisionActionConfig.SurfaceClimbDismountPlaySpeed :
                    mSurfaceClimbPlaybackDirection *
                        (std::abs(mSurfaceClimbVerticalIntent) >
                                 std::numeric_limits<double>::epsilon()
                             ? mCollisionActionConfig.SurfaceClimbMaximumPlaySpeed
                             : mCollisionActionConfig.SurfaceClimbMinimumPlaySpeed)));
            const double framesPerSecond =
                kNativeSkelAnimeFramesPerSecondAtUnitPlaySpeed * playSpeed;
            SelectPlayerActionAnimation(
                scene, animation, clipIndex, NativePlayerActionName(link.NativePlayerAction),
                framesPerSecond);

            PlayerClipCommand command;
            command.ClipIndex = clipIndex;
            command.Mode = SkelAnimeMode::Once;
            command.FramesPerSecond = static_cast<float>(framesPerSecond);
            if (playSpeed < 0.0) {
                command.StartFrame = static_cast<float>(animation.FrameCount);
                command.EndFrame = 0.0f;
                command.InitialFrame = command.StartFrame;
            } else {
                command.EndFrame = static_cast<float>(animation.FrameCount);
                command.InitialFrame = hold ? command.EndFrame : 0.0f;
            }
            const auto sample = mClipRuntime.Advance(command, deltaSeconds);
            animation.Frame = sample.CurrentFrame;
            SampleLinkAnimationPose(scene, locomotionConfig, animation);
            if (!hold) {
                ConsumePlayerRootMotion(
                    scene, animation, link, mSurfaceClimbRootMotion,
                    mSurfaceClimbRootMotionInitialized);
                if (!dismount) {
                    ConstrainSurfaceClimbActor(link);
                }
            }
            ApplyLinkCurrentAnimationPose(scene, renderScene, animation, link);
            mSurfaceClimbAnimationComplete = !hold && sample.Complete;
            return;
        }
        const char* clipId = "airborne_wait";
        if (landingAnticipation) {
            clipId = "landing";
        } else if (autoJump) {
            clipId = link.NativeAutoJumpKind == LinkNativeAutoJumpKind::Run
                ? "run_auto_jump" : "auto_jump";
        } else if (landing) {
            if (link.NativeLandingKind == LinkNativeLandingKind::RunAutoJump) {
                clipId = "run_auto_jump_end";
            } else {
                clipId = link.NativeLandingKind == LinkNativeLandingKind::Short
                    ? "short_landing" : "landing";
            }
        } else if (ledgeHold) {
            clipId = mLedgeHoldIntroComplete ? "ledge_wait" : "ledge_hold";
        } else if (ledgeClimb) {
            clipId = "ledge_climb_up";
        }
        const size_t clipIndex = RequirePlayerClipIndex(scene, clipId);
        const bool clipChanged = !mClipRuntime.Active() ||
                                 mClipRuntime.ClipIndex() != clipIndex;
        SelectPlayerActionAnimation(
            scene, animation, clipIndex, NativePlayerActionName(link.NativePlayerAction),
            locomotionConfig.CharacterFramesPerSecond);

        PlayerClipCommand command;
        command.ClipIndex = clipIndex;
        command.Mode = (landingAnticipation || autoJump || landing || ledgeClimb ||
                        (ledgeHold && !mLedgeHoldIntroComplete))
            ? SkelAnimeMode::Once : SkelAnimeMode::Loop;
        command.FramesPerSecond = landingAnticipation
            ? 0.0f : static_cast<float>(animation.FramesPerSecond);
        command.EndFrame = landingAnticipation
            ? 0.0f : static_cast<float>(animation.FrameCount);
        command.InitialFrame = static_cast<float>(clipChanged ? 0.0 : animation.Frame);
        const auto sample = mClipRuntime.Advance(command, deltaSeconds);
        animation.Frame = sample.CurrentFrame;
        SampleLinkAnimationPose(scene, locomotionConfig, animation);
        if (ledgeClimb) {
            ConsumePlayerRootMotion(scene, animation, link, mLedgeRootMotion,
                                    mLedgeRootMotionInitialized);
        }
        ApplyLinkCurrentAnimationPose(scene, renderScene, animation, link);
        mLandingAnimationComplete = landing && sample.Complete;
        if (ledgeHold && !mLedgeHoldIntroComplete && sample.Complete) {
            mLedgeHoldIntroComplete = true;
        }
        mLedgeClimbAnimationComplete = ledgeClimb && sample.Complete;
        return;
    }

    mLandingAnimationComplete = false;
    mLedgeClimbAnimationComplete = false;
    mLedgeRootMotionInitialized = false;
    mSurfaceClimbAnimationComplete = false;
    mSurfaceClimbRootMotionInitialized = false;
    const double previousFrame = animation.Frame;
    const size_t previousClipIndex = animation.ClipIndex;
    AdvanceLinkAnimation(scene, locomotionConfig, animation, motion, deltaSeconds);
    const bool clipChanged = !mClipRuntime.Active() || previousClipIndex != animation.ClipIndex;
    PlayerClipCommand command;
    command.ClipIndex = animation.ClipIndex;
    command.Mode = animation.NativeWalkEndActive ? SkelAnimeMode::Once : SkelAnimeMode::Loop;
    command.FramesPerSecond = static_cast<float>(
        animation.NativeWalkEndToIdleMorphActive ? 0.0 : animation.FramesPerSecond);
    command.EndFrame = static_cast<float>(animation.FrameCount);
    command.InitialFrame = static_cast<float>(clipChanged ? animation.Frame : previousFrame);
    const auto sample = mClipRuntime.Advance(command, deltaSeconds);
    animation.Frame = sample.CurrentFrame;
    ApplyLinkAnimationFrame(scene, locomotionConfig, renderScene, animation, link);
}

} // namespace Oot3dNativeGame
