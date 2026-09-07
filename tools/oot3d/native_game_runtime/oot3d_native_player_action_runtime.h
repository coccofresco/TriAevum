#pragma once

#include "oot3d_link_runtime_types.h"
#include "oot3d_link_collision.h"

namespace Oot3dNativeGame {

struct NativeVerticalMotionStep {
    int TickCount = 0;
    double DeltaY = 0.0;
    double VelocityYUnitsPerTick = 0.0;
};

bool BeginNativePlayerSceneEntrance(
    LinkInstance& link, int playerParams,
    const LinkNativePlayerActionConfig& config);
double AdvanceNativePlayerSceneEntrance(
    LinkInstance& link, const LinkNativePlayerActionConfig& config,
    double playerTickRate, double deltaSeconds);
void UpdateNativePlayerSceneEntranceDistance(LinkInstance& link);
bool NativePlayerSceneEntranceShouldComplete(const LinkInstance& link);
void CompleteNativePlayerSceneEntrance(
    LinkInstance& link, const LinkNativeLocomotionConfig& config);

bool NativePlayerCanAutoJump(const LinkInstance& link,
                             const LinkNativeLocomotionConfig& config,
                             bool wasGrounded);
void BeginNativePlayerAutoJump(LinkInstance& link,
                               const LinkNativeLocomotionConfig& config,
                               double actorY);
bool NativePlayerIsAirborne(const LinkInstance& link);
bool NativePlayerShouldUseLandingAnticipation(const LinkInstance& link);
void BeginNativePlayerAirborne(LinkInstance& link, double actorY,
                               double initialVelocityYUnitsPerTick = 0.0);
NativeVerticalMotionStep AdvanceNativePlayerAirborne(
    LinkInstance& link, const LinkNativeLocomotionConfig& config, double deltaSeconds);
void UpdateNativePlayerFallDistance(LinkInstance& link, double actorY);
void BeginNativePlayerLanding(LinkInstance& link, const LinkNativeLocomotionConfig& config,
                              double actorY);
void CompleteNativePlayerLanding(LinkInstance& link);
bool NativePlayerCanGrabLedge(const LinkInstance& link,
                              const LinkNativeCollisionActionConfig& config,
                              const LinkNativeLedgeQueryResult& ledge);
void BeginNativePlayerLedgeHold(LinkInstance& link,
                               const LinkNativeLedgeQueryResult& ledge);
void BeginNativePlayerLedgeClimb(LinkInstance& link);
void CompleteNativePlayerLedgeClimb(LinkInstance& link);
void BeginNativePlayerSurfaceClimb(
    LinkInstance& link, const LinkNativeSurfaceClimbQueryResult& surface);
void CompleteNativePlayerSurfaceClimb(LinkInstance& link, bool grounded,
                                      double actorY);
void PopulateNativePlayerActionMotion(const LinkInstance& link, LinkMotionState& motion);

} // namespace Oot3dNativeGame
