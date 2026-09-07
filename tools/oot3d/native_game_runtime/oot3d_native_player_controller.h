#pragma once

#include "oot3d_demo_host_player_controller.h"
#include "oot3d_native_player_clip_runtime.h"
#include "oot3d_native_root_motion.h"

struct LinkNativeSurfaceClimbQueryResult;

namespace Oot3dNativeGame {

class PlayerController final : public Oot3dDemoHostPlayerController {
  public:
    PlayerController(LinkNativeCollisionActionConfig collisionActionConfig,
                     LinkNativePlayerActionConfig playerActionConfig);

    LinkMotionState UpdateMovement(
        const Oot3dDemoHostInputState& input,
        const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
        const LinkNativeLocomotionConfig& locomotionConfig,
        const Camera& camera,
        LinkInstance& link,
        const LinkInstance& resetLink,
        double deltaSeconds) override;

    void UpdateAnimation(
        const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
        const LinkNativeLocomotionConfig& locomotionConfig,
        ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene,
        LinkAnimation& animation,
        const LinkMotionState& motion,
        LinkInstance& link,
        double deltaSeconds) override;

  private:
    enum class SurfaceClimbPhase {
        Start,
        Hold,
        UpLeft,
        UpRight,
        DismountBottomLeft,
        DismountBottomRight,
        DismountTopBackLeft,
        DismountTopBackRight,
    };

    enum class SurfaceClimbEntryClip {
        FreeBack,
        FreeFront,
        LadderFront,
    };

    void BeginSurfaceClimb(
        LinkInstance& link, const LinkNativeSurfaceClimbQueryResult& surface,
        SurfaceClimbEntryClip entryClip);
    void ResetSurfaceClimbPlayback();

    PlayerClipRuntime mClipRuntime;
    LinkNativeCollisionActionConfig mCollisionActionConfig;
    LinkNativePlayerActionConfig mPlayerActionConfig;
    RootMotionState mLedgeRootMotion;
    RootMotionState mSurfaceClimbRootMotion;
    bool mAirborneLandingAnticipation = false;
    bool mLandingAnimationComplete = false;
    bool mLedgeHoldIntroComplete = false;
    bool mLedgeClimbAnimationComplete = false;
    bool mLedgeRootMotionInitialized = false;
    SurfaceClimbPhase mSurfaceClimbPhase = SurfaceClimbPhase::Start;
    SurfaceClimbEntryClip mSurfaceClimbEntryClip = SurfaceClimbEntryClip::FreeBack;
    int mSurfaceClimbFootPhase = 0;
    double mSurfaceClimbVerticalIntent = 0.0;
    double mSurfaceClimbPlaybackDirection = 1.0;
    double mSurfaceClimbDismountFloorY = 0.0;
    int mSurfaceClimbDismountFloorPolygonIndex = -1;
    int mSurfaceClimbDismountFloorSurfaceType = -1;
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 mSurfaceClimbDismountTarget;
    bool mSurfaceClimbDismountTargetValid = false;
    bool mSurfaceClimbAnimationComplete = false;
    bool mSurfaceClimbRootMotionInitialized = false;
    bool mSceneEntranceInitialized = false;
};

} // namespace Oot3dNativeGame
