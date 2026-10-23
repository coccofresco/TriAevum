#pragma once

#include "recomp/a32_runtime.h"

#include <cstdint>
#include <span>

namespace oot3d::gameplay {
struct TimeContext;
}

namespace Oot3dNativeGame {

class NativeA32Memory;

inline constexpr std::uint32_t kOot3dEnKoBlinkAdvanceBlock = 0x001B6028U;
inline constexpr std::uint32_t kOot3dEnKoBlinkRngReturnBlock = 0x001B6074U;
inline constexpr std::uint32_t kOot3dEnKoBlinkContinue = 0x001B6130U;
inline constexpr std::uint32_t kOot3dEnKoBlinkRngEntry = 0x003702C8U;
inline constexpr std::uint32_t kOot3dEnZl4ActorCameraAdvanceBlock =
    0x001E0474U;
inline constexpr std::uint32_t kOot3dEnZl4ActorCameraAdvanceContinue =
    0x001E04E0U;
inline constexpr std::uint32_t kOot3dCameraQuakeSineCallbackEntry =
    0x00111CB0U;
inline constexpr std::uint32_t kOot3dCameraQuakeSineFadeCallbackEntry =
    0x001344F0U;
inline constexpr std::uint32_t kOot3dCameraQuakeRandomCallbackEntry =
    0x00144EE8U;
inline constexpr std::uint32_t
    kOot3dCameraQuakePerpetualSineRandomCallbackEntry = 0x00150CFCU;
inline constexpr std::uint32_t kOot3dCameraQuakeRandomFadeCallbackEntry =
    0x0015F25CU;
inline constexpr std::uint32_t kOot3dCameraQuakeSineRandomCallbackEntry =
    0x0015F2CCU;
inline constexpr std::uint32_t kOot3dCameraDemo1Entry = 0x00200118U;
inline constexpr std::uint32_t kOot3dCameraJump1FrameCountdownBlock =
    0x002029C4U;
inline constexpr std::uint32_t kOot3dCameraJump1FrameCountdownContinue =
    0x002029CCU;
inline constexpr std::uint32_t kOot3dCameraJump2FrameCountdownBlock =
    0x002033E4U;
inline constexpr std::uint32_t kOot3dCameraJump2FrameCountdownContinue =
    0x002033ECU;
inline constexpr std::uint32_t kOot3dEnKanbanPhaseAdvanceBlock =
    0x0022C2C4U;
inline constexpr std::uint32_t kOot3dEnKanbanPhaseAdvanceContinue =
    0x0022C2CCU;
inline constexpr std::uint32_t kOot3dEnKanbanState0CountdownBlock =
    0x0022C31CU;
inline constexpr std::uint32_t kOot3dEnKanbanState0CountdownContinue =
    0x0022C324U;
inline constexpr std::uint32_t kOot3dEnKanbanActorFlagCountdownBlock =
    0x0022C32CU;
inline constexpr std::uint32_t kOot3dEnKanbanActorFlagCountdownContinue =
    0x0022C350U;
inline constexpr std::uint32_t kOot3dEnKanbanInteractionCooldownBlock =
    0x0022C374U;
inline constexpr std::uint32_t kOot3dEnKanbanInteractionCooldownReady =
    0x0022C380U;
inline constexpr std::uint32_t kOot3dEnKanbanInteractionCooldownBlocked =
    0x0022C400U;
inline constexpr std::uint32_t kOot3dEnKanbanDrawGateRampBlock =
    0x0022C93CU;
inline constexpr std::uint32_t kOot3dEnKanbanDrawGateRampContinue =
    0x0022C984U;
inline constexpr std::uint32_t kOot3dEnKanbanOscillatorXBlock =
    0x0022CA9CU;
inline constexpr std::uint32_t kOot3dEnKanbanOscillatorXContinue =
    0x0022CB38U;
inline constexpr std::uint32_t kOot3dEnKanbanOscillatorYBlock =
    0x0022CB48U;
inline constexpr std::uint32_t kOot3dEnKanbanOscillatorYContinue =
    0x0022CBB4U;
inline constexpr std::uint32_t kOot3dEnKanbanPieceLifetimeBlock =
    0x0022CF00U;
inline constexpr std::uint32_t kOot3dEnKanbanPieceLifetimeContinue =
    0x0022CF20U;
inline constexpr std::uint32_t kOot3dEnKanbanRippleEventGateBlock =
    0x0022D1E0U;
inline constexpr std::uint32_t kOot3dEnKanbanRippleEventPath =
    0x0022D1ECU;
inline constexpr std::uint32_t kOot3dEnKanbanRippleEventSkip =
    0x0022D364U;
inline constexpr std::uint32_t kOot3dCameraBattle1FrameCountdownBlock =
    0x00234E38U;
inline constexpr std::uint32_t kOot3dCameraBattle1FrameCountdownContinue =
    0x00234E40U;
inline constexpr std::uint32_t kOot3dCameraBattle4FrameCountdownBlock =
    0x00235508U;
inline constexpr std::uint32_t kOot3dCameraBattle4FrameCountdownContinue =
    0x00235510U;
inline constexpr std::uint32_t kOot3dCameraKeepOn1FrameCountdownBlock =
    0x00236CB8U;
inline constexpr std::uint32_t kOot3dCameraKeepOn1FrameCountdownContinue =
    0x00236CC0U;
inline constexpr std::uint32_t kOot3dCameraKeepOn3FrameCountdownBlock =
    0x002378FCU;
inline constexpr std::uint32_t kOot3dCameraKeepOn3FrameCountdownContinue =
    0x00237904U;
inline constexpr std::uint32_t kOot3dCameraNormal1FrameCountdownBlock =
    0x0023A268U;
inline constexpr std::uint32_t kOot3dCameraNormal1FrameCountdownContinue =
    0x0023A270U;
inline constexpr std::uint32_t kOot3dCameraNormal1SpeedCountdownBlock =
    0x0023A340U;
inline constexpr std::uint32_t kOot3dCameraNormal1SpeedCountdownContinue =
    0x0023A2BCU;
inline constexpr std::uint32_t kOot3dCameraNormal1RateCountdownBlock =
    0x0023A45CU;
inline constexpr std::uint32_t kOot3dCameraNormal1RateCountdownContinue =
    0x0023A464U;
inline constexpr std::uint32_t kOot3dCameraUnique1FrameCountdownBlock =
    0x0023D3FCU;
inline constexpr std::uint32_t kOot3dCameraUnique1FrameCountdownContinue =
    0x0023D404U;
inline constexpr std::uint32_t kOot3dCameraSpecial5TimerBlock = 0x0025B6C8U;
inline constexpr std::uint32_t kOot3dCameraSpecial5ZeroTransition =
    0x0025B6DCU;
inline constexpr std::uint32_t kOot3dCameraSpecial5CommonContinue =
    0x0025B80CU;
inline constexpr std::uint32_t kOot3dCameraSubj3FrameCountdownBlock =
    0x0025CB84U;
inline constexpr std::uint32_t kOot3dCameraSubj3FrameCountdownContinue =
    0x0025CB8CU;
inline constexpr std::uint32_t kOot3dCameraParallel1FrameCountdownBlock =
    0x00275F28U;
inline constexpr std::uint32_t kOot3dCameraParallel1FrameCountdownContinue =
    0x00275F30U;
inline constexpr std::uint32_t kOot3dSkelAnimeDirectMorphBoundary = 0x002BB29CU;
inline constexpr std::uint32_t kOot3dSkelAnimeLegacyMorphBoundary = 0x002BB3B4U;
inline constexpr std::uint32_t kOot3dSinIdx8Entry = 0x002CFCA0U;
inline constexpr std::uint32_t kOot3dCameraWaterDistortionTimerAdvanceBlock =
    0x002D0A80U;
inline constexpr std::uint32_t kOot3dCameraWaterDistortionTimerContinue =
    0x002D0A94U;
inline constexpr std::uint32_t kOot3dCameraFloorMissCounterAdvanceBlock =
    0x002D86F0U;
inline constexpr std::uint32_t kOot3dCameraFloorMissCounterContinue =
    0x002D86FCU;
inline constexpr std::uint32_t kOot3dCameraInterfaceDelayAdvanceBlock =
    0x002D89D0U;
inline constexpr std::uint32_t kOot3dCameraInterfaceDelayContinue =
    0x002D89D8U;
inline constexpr std::uint32_t kOot3dCameraWaterDistortionFlag4SampleBlock =
    0x002D8E70U;
inline constexpr std::uint32_t kOot3dCameraWaterDistortionFlag8SampleBlock =
    0x002D8FB4U;
inline constexpr std::uint32_t kOot3dCameraWaterDistortionCustomSampleBlock =
    0x002D9120U;
inline constexpr std::uint32_t kOot3dCameraWaterDistortionSampleContinue =
    0x002D9140U;
inline constexpr std::uint32_t kOot3dPlayerGetExplosiveHeldEntry =
    0x003279DCU;
inline constexpr std::uint32_t kOot3dPlayerRespawnDamageAdvanceBlock =
    0x00250B08U;
inline constexpr std::uint32_t kOot3dPlayerRespawnDamageAudioBranch =
    0x00250B1CU;
inline constexpr std::uint32_t kOot3dPlayerRespawnDamageContinue =
    0x00250B44U;
inline constexpr std::uint32_t kOot3dPlayerInvincibilityTimerBlock =
    0x00250BE4U;
inline constexpr std::uint32_t kOot3dPlayerCommonCountdownBlock =
    0x00250B50U;
inline constexpr std::uint32_t kOot3dPlayerCommonCountdownActorBranch =
    0x00250BB4U;
inline constexpr std::uint32_t kOot3dPlayerDamageRunTimerBlock =
    0x00250C30U;
inline constexpr std::uint32_t
    kOot3dPlayerDamageRunTimerReturn = 0x00250C4CU;
inline constexpr std::uint32_t
    kOot3dPlayerAttentionPersistenceAdvanceBlock = 0x0025101CU;
inline constexpr std::uint32_t
    kOot3dPlayerAttentionPersistenceContinue = 0x00251044U;
inline constexpr std::uint32_t
    kOot3dPlayerUpdateContextActionAndSequenceStateEntry = 0x003C45F4U;
inline constexpr std::uint32_t kOot3dPlayerFishingStateRecoveryBlock =
    0x00251374U;
inline constexpr std::uint32_t kOot3dPlayerFishingStateRecoveryContinue =
    0x00251384U;
inline constexpr std::uint32_t kOot3dPlayerUnderwaterTimerResetBlock =
    0x00252038U;
inline constexpr std::uint32_t kOot3dPlayerUnderwaterTimerIncrementBlock =
    0x0025205CU;
inline constexpr std::uint32_t kOot3dPlayerUnderwaterTimerContinue =
    0x00252064U;
inline constexpr std::uint32_t kOot3dPlayerRandomTurnTimerDecrementBlock =
    0x0025344CU;
inline constexpr std::uint32_t kOot3dPlayerRandomTurnTimerRefreshBlock =
    0x00253460U;
inline constexpr std::uint32_t kOot3dPlayerRandomTurnTimerRngReturn =
    0x00253468U;
inline constexpr std::uint32_t kOot3dPlayerRandomTurnTimerContinue =
    0x0025346CU;
inline constexpr std::uint32_t kOot3dPlayerRandomTurnTimerEpilogue =
    0x002534A0U;
inline constexpr std::uint32_t kOot3dPlayerRandomTurnTimerRngEntry =
    0x003702C8U;
inline constexpr std::uint32_t kOot3dPlayerMeleeActionTimerBlock =
    0x002518B4U;
inline constexpr std::uint32_t kOot3dPlayerMeleeActionTimerContinue =
    0x002518D4U;
inline constexpr std::uint32_t kOot3dPlayerMeleeWeaponTipComboAdvanceBlock =
    0x00313C48U;
inline constexpr std::uint32_t kOot3dPlayerMeleeWeaponTipComboAdvanceContinue =
    0x00313C74U;
inline constexpr std::uint32_t kOot3dSkelAnimeSetUpdateEntry = 0x00320D28U;
inline constexpr std::uint32_t kOot3dCutsceneNormalFrameAdvanceBlock =
    0x00322054U;
inline constexpr std::uint32_t kOot3dCutsceneFrameAdvanceEpilogue =
    0x00321FB8U;
inline constexpr std::uint32_t kOot3dCutsceneProcessCommandsEntry =
    0x002C5BA0U;
inline constexpr std::uint32_t
    kOot3dEnvironmentPathInterpolateActorPosAndRotationEntry = 0x00361F00U;
inline constexpr std::uint32_t
    kOot3dEnvironmentPathInterpolationContinue = 0x00361F7CU;
inline constexpr std::uint32_t kOot3dPlayerSetCsActionEntry = 0x00330D5CU;
inline constexpr std::uint32_t kOot3dCosIdx8Entry = 0x00338F60U;
inline constexpr std::uint32_t
    kOot3dActorUpdatePosWithVelocityFromRotationEntry = 0x0033BD9CU;
inline constexpr std::uint32_t kOot3dCameraAnimationApplyFrameEntry =
    0x0033CB90U;
inline constexpr std::uint32_t kOot3dLinkAnimationPlayOnceWithSpeedEntry =
    0x003404A8U;
inline constexpr std::uint32_t kOot3dPlayerUpdateHostileLockOnEntry =
    0x00349574U;
inline constexpr std::uint32_t kOot3dPlayerUpdateSwimVerticalVelocityEntry =
    0x0034B17CU;
inline constexpr std::uint32_t
    kOot3dPlayerHoldsHookshotWithoutHeldActorEntry = 0x0034D4B0U;
inline constexpr std::uint32_t kOot3dPlayerGetIdleAnimEntry = 0x0034D628U;
inline constexpr std::uint32_t kOot3dPlayerIsItemInHandEntry = 0x0034DD2CU;
inline constexpr std::uint32_t kOot3dZarGetCsabByIndexEntry = 0x0034807CU;
inline constexpr std::uint32_t kOot3dMathStepToAngleSEntry = 0x003529D4U;
inline constexpr std::uint32_t kOot3dAnimationChangeEntry = 0x0035302CU;
inline constexpr std::uint32_t kOot3dLinkAnimationPlayLoopSetSpeedEntry =
    0x00358DFCU;
inline constexpr std::uint32_t kOot3dLinkAnimationPlayOnceEntry = 0x00359AA0U;
inline constexpr std::uint32_t kOot3dPlayerHoldsHookshotEntry = 0x00355A60U;
inline constexpr std::uint32_t kOot3dPlayerSetRuntimeFlag200Entry =
    0x0035AF04U;
inline constexpr std::uint32_t kOot3dPlayerHoldsTwoHandedWeaponEntry =
    0x0035D260U;
inline constexpr std::uint32_t kOot3dActorUpdateVelocityXZGravityEntry =
    0x0035FB14U;
inline constexpr std::uint32_t kOot3dLinkAnimationChangeEntry = 0x00360190U;
inline constexpr std::uint32_t kOot3dLinkAnimationPlayLoopEntry = 0x003604F0U;
inline constexpr std::uint32_t kOot3dActorUpdateVelocityXYZEntry = 0x00365860U;
inline constexpr std::uint32_t kOot3dPlayerGetHeightEntry = 0x00367EF0U;
inline constexpr std::uint32_t kOot3dPlayerInCsModeEntry = 0x0036A7A0U;
inline constexpr std::uint32_t kOot3dLinkAnimationOnFrameEntry = 0x0036B1E0U;
inline constexpr std::uint32_t kOot3dSkelAnimeUpdateEntry = 0x0036B4ECU;
inline constexpr std::uint32_t kOot3dActorUpdatePosEntry = 0x0036B96CU;
inline constexpr std::uint32_t kOot3dActorHasNoParentEntry = 0x0036C940U;
inline constexpr std::uint32_t kOot3dMathSmoothStepToFEntry = 0x0036E168U;
inline constexpr std::uint32_t kOot3dSkelAnimeIsFrameCrossedEntry = 0x0036E5E0U;
inline constexpr std::uint32_t kOot3dPlayerSetCsActionWithHaltedActorsEntry =
    0x0036E980U;
inline constexpr std::uint32_t kOot3dMathApproachZeroFEntry = 0x0036FC20U;
inline constexpr std::uint32_t kOot3dMathSmoothStepToSUpdateRateEntry =
    0x00370084U;
inline constexpr std::uint32_t kOot3dMathScaledStepToSEntry = 0x00370378U;
inline constexpr std::uint32_t kOot3dMathStepToFEntry = 0x003705A0U;
inline constexpr std::uint32_t kOot3dActorHasParentEntry = 0x00371E40U;
inline constexpr std::uint32_t kOot3dMathStepToSEntry = 0x00372AA8U;
inline constexpr std::uint32_t kOot3dMathApproachFEntry = 0x00373500U;
inline constexpr std::uint32_t kOot3dAnimationOnFrameImplEntry = 0x003736FCU;
inline constexpr std::uint32_t kOot3dActorKillEntry = 0x00374428U;
inline constexpr std::uint32_t kOot3dActorSetScaleEntry = 0x0037572CU;
inline constexpr std::uint32_t kOot3dMathSmoothStepToSEntry = 0x00375A18U;
inline constexpr std::uint32_t kOot3dActorMoveForwardEntry = 0x00376864U;
inline constexpr std::uint32_t kOot3dAnimationGetLengthEntry = 0x003FE340U;
inline constexpr std::uint32_t kOot3dActorUpdateAllContextFreezeBlock =
    0x00461460U;
inline constexpr std::uint32_t kOot3dActorUpdateAllContextFreezeContinue =
    0x00461474U;
inline constexpr std::uint32_t kOot3dActorUpdateAllInstanceFreezeBlock =
    0x00461730U;
inline constexpr std::uint32_t kOot3dActorUpdateAllInstanceFreezePass =
    0x00461750U;
inline constexpr std::uint32_t kOot3dActorUpdateAllInstanceFreezeSkip =
    0x004617C4U;
inline constexpr std::uint32_t kOot3dActorUpdateAllEffectTimersBlock =
    0x00461784U;
inline constexpr std::uint32_t kOot3dActorUpdateAllEffectTimersContinue =
    0x004617A4U;
inline constexpr std::uint32_t kOot3dMeshCommandPacketSubmitEntry =
    0x00466E2CU;
inline constexpr std::uint32_t kOot3dCameraQuakeCallbackReturnBoundary =
    0x004788DCU;
inline constexpr std::uint32_t kOot3dPlayerDamageFlickerCounterBlock =
    0x004BF6D8U;
inline constexpr std::uint32_t kOot3dPlayerDamageFlickerCounterContinue =
    0x004BF6F8U;

struct Oot3dTypedGameplayContext {
  float NativeUpdateRate = 2.0f;
  // Updated at the authoritative GameState owner entry. Leaf ports retain
  // NativeUpdateRate for ABI compatibility while promoted owner graphs can
  // consume the complete temporal contract.
  const oot3d::gameplay::TimeContext *Time = nullptr;
};

struct Oot3dTypedGameplayStats {
  std::uint64_t Calls = 0;
  std::uint64_t AngleCalls = 0;
  std::uint64_t ActorCalls = 0;
  std::uint64_t EnKoBlinkBlockCalls = 0;
  std::uint64_t EnKoBlinkLogicalAdvances = 0;
  std::uint64_t EnKoBlinkIntermediateHolds = 0;
  std::uint64_t EnKoBlinkTimerAdvances = 0;
  std::uint64_t EnKoBlinkSequenceAdvances = 0;
  std::uint64_t EnKoBlinkRngDispatches = 0;
  std::uint64_t EnKoBlinkRngReturns = 0;
  std::uint64_t EnKanbanPhaseBlockCalls = 0;
  std::uint64_t EnKanbanPhaseLogicalAdvances = 0;
  std::uint64_t EnKanbanPhaseIntermediateHolds = 0;
  std::uint64_t EnKanbanState0CountdownBlockCalls = 0;
  std::uint64_t EnKanbanState0CountdownLogicalAdvances = 0;
  std::uint64_t EnKanbanState0CountdownIntermediateHolds = 0;
  std::uint64_t EnKanbanActorFlagCountdownBlockCalls = 0;
  std::uint64_t EnKanbanActorFlagCountdownLogicalAdvances = 0;
  std::uint64_t EnKanbanActorFlagCountdownIntermediateHolds = 0;
  std::uint64_t EnKanbanActorFlagClears = 0;
  std::uint64_t EnKanbanInteractionCooldownBlockCalls = 0;
  std::uint64_t EnKanbanInteractionCooldownLogicalAdvances = 0;
  std::uint64_t EnKanbanInteractionCooldownIntermediateHolds = 0;
  std::uint64_t EnKanbanInteractionCooldownReadyPasses = 0;
  std::uint64_t EnKanbanInteractionCooldownBlockedPasses = 0;
  std::uint64_t EnKanbanDrawGateRampBlockCalls = 0;
  std::uint64_t EnKanbanDrawGateRampLogicalAdvances = 0;
  std::uint64_t EnKanbanDrawGateRampIntermediateHolds = 0;
  std::uint64_t EnKanbanDrawGateRampInactivePasses = 0;
  std::uint64_t EnKanbanDrawGateRampIncreaseSteps = 0;
  std::uint64_t EnKanbanDrawGateRampDecreaseSteps = 0;
  std::uint64_t EnKanbanDrawGateRampClamps = 0;
  std::uint64_t EnKanbanOscillatorAxisBlockCalls = 0;
  std::uint64_t EnKanbanOscillatorXBlockCalls = 0;
  std::uint64_t EnKanbanOscillatorYBlockCalls = 0;
  std::uint64_t EnKanbanOscillatorRateAdjustedSteps = 0;
  std::uint64_t EnKanbanOscillatorGroundResets = 0;
  std::uint64_t EnKanbanOscillatorVelocityClamps = 0;
  std::uint64_t EnKanbanPieceLifetimeBlockCalls = 0;
  std::uint64_t EnKanbanPieceLifetimeLogicalDecrements = 0;
  std::uint64_t EnKanbanPieceLifetimeIntermediateHolds = 0;
  std::uint64_t EnKanbanPieceLifetimeStateTransitions = 0;
  std::uint64_t EnKanbanPieceLifetimeZeroCrossingTransitions = 0;
  std::uint64_t EnKanbanPieceLifetimeExistingZeroTransitions = 0;
  std::uint64_t EnKanbanRippleGateCalls = 0;
  std::uint64_t EnKanbanRippleDispatches = 0;
  std::uint64_t EnKanbanRippleIntermediateSuppressions = 0;
  std::uint64_t ActorUpdateAllContextFreezeBlockCalls = 0;
  std::uint64_t ActorUpdateAllContextFreezeLogicalAdvances = 0;
  std::uint64_t ActorUpdateAllContextFreezeIntermediateHolds = 0;
  std::uint64_t ActorUpdateAllInstanceFreezeBlockCalls = 0;
  std::uint64_t ActorUpdateAllInstanceFreezeLogicalAdvances = 0;
  std::uint64_t ActorUpdateAllInstanceFreezeIntermediateHolds = 0;
  std::uint64_t ActorUpdateAllInstanceFreezeGatePasses = 0;
  std::uint64_t ActorUpdateAllInstanceFreezeGateSkips = 0;
  std::uint64_t ActorUpdateAllEffectTimerBlockCalls = 0;
  std::uint64_t ActorUpdateAllEffectTimerLogicalFieldAdvances = 0;
  std::uint64_t ActorUpdateAllEffectTimerIntermediateFieldHolds = 0;
  std::uint64_t ActorInitCallbackDispatches = 0;
  std::uint64_t ActorInitCallbackReturns = 0;
  std::uint64_t ActorUpdateCallbackDispatches = 0;
  std::uint64_t ActorDestroyCallbackDispatches = 0;
  std::uint64_t ActorResourceCleanupDispatches = 0;
  std::uint64_t ActorDestroyReturns = 0;
  std::uint64_t AudioRequestReferenceAcquireDispatches = 0;
  std::uint64_t AudioRequestStatusQueryDispatches = 0;
  std::uint64_t AudioRequestReferenceCleanupDispatches = 0;
  std::uint64_t AudioRequestCallbackReturns = 0;
  std::uint64_t PauseUiAlphaPauseStateDispatches = 0;
  std::uint64_t PauseUiAlphaFirstStepDispatches = 0;
  std::uint64_t PauseUiAlphaTailStepDispatches = 0;
  std::uint64_t PauseUiAlphaReturns = 0;
  std::uint64_t DynaInteractionResetMatches = 0;
  std::uint64_t DynaInteractionResetMisses = 0;
  std::uint64_t PlayerReleaseLockOnCalls = 0;
  std::uint64_t ActorUpdateRecordInitializeCalls = 0;
  std::uint64_t ActorUpdateRecordClearCalls = 0;
  std::uint64_t RecordInitializerMemzeroDispatches = 0;
  std::uint64_t RecordInitializerReturns = 0;
  std::uint64_t PlayerCalls = 0;
  std::uint64_t MathCalls = 0;
  std::uint64_t AnimationCalls = 0;
  std::uint64_t RendererCalls = 0;
  std::uint64_t CutsceneCalls = 0;
  std::uint64_t CameraCalls = 0;
  std::uint64_t CameraModeFrameCountdownBlockCalls = 0;
  std::uint64_t CameraModeFrameCountdownLogicalAdvances = 0;
  std::uint64_t CameraModeFrameCountdownIntermediateHolds = 0;
  std::uint64_t CameraSpecial5TimerBlockCalls = 0;
  std::uint64_t CameraSpecial5TimerLogicalDecrements = 0;
  std::uint64_t CameraSpecial5TimerIntermediateHolds = 0;
  std::uint64_t CameraSpecial5TimerZeroTransitions = 0;
  std::uint64_t CameraSpecial5TimerTerminalContinues = 0;
  std::uint64_t CameraFloorMissCounterBlockCalls = 0;
  std::uint64_t CameraFloorMissCounterLogicalAdvances = 0;
  std::uint64_t CameraFloorMissCounterIntermediateHolds = 0;
  std::uint64_t CameraInterfaceDelayBlockCalls = 0;
  std::uint64_t CameraInterfaceDelayLogicalAdvances = 0;
  std::uint64_t CameraInterfaceDelayIntermediateHolds = 0;
  std::uint64_t CameraWaterDistortionTimerBlockCalls = 0;
  std::uint64_t CameraWaterDistortionTimerLogicalAdvances = 0;
  std::uint64_t CameraWaterDistortionTimerIntermediateHolds = 0;
  std::uint64_t CameraWaterDistortionFractionalSamples = 0;
  std::uint64_t CameraWaterDistortionFlag4Samples = 0;
  std::uint64_t CameraWaterDistortionFlag8Samples = 0;
  std::uint64_t CameraWaterDistortionCustomSamples = 0;
  std::uint64_t CameraWaterDistortionSampleFailures = 0;
  std::uint64_t CameraQuakeCallbackCalls = 0;
  std::uint64_t CameraQuakeLogicalAdvances = 0;
  std::uint64_t CameraQuakeIntermediateHolds = 0;
  std::uint64_t CameraQuakeRandomSamples = 0;
  std::uint64_t CameraQuakeSignalReuses = 0;
  std::uint64_t CameraQuakeSignalCacheMisses = 0;
  std::uint64_t CameraQuakeHelperDispatches = 0;
  std::uint64_t CameraQuakeReturnDispatches = 0;
  std::uint64_t CameraQuakeFailures = 0;
  std::uint64_t CutsceneNormalFrameBlockCalls = 0;
  std::uint64_t CutsceneLogicalFrameAdvances = 0;
  std::uint64_t CutsceneIntermediateHolds = 0;
  std::uint64_t CutsceneCommandDispatches = 0;
  std::uint64_t CutsceneActorCueInterpolationCalls = 0;
  std::uint64_t CutsceneActorCueFractionalSamples = 0;
  std::uint64_t CutsceneCameraBindingObservations = 0;
  std::uint64_t CutsceneCameraRejectedObservations = 0;
  std::uint64_t CutsceneCameraFractionalSamples = 0;
  std::uint64_t CutsceneCameraCurveSamples = 0;
  std::uint64_t CutsceneCameraSampleFailures = 0;
  std::uint64_t ActorCutsceneCameraBindingObservations = 0;
  std::uint64_t ActorCutsceneCameraPrimingObservations = 0;
  std::uint64_t ActorCutsceneCameraRejectedObservations = 0;
  std::uint64_t ActorCutsceneCameraIntermediateHolds = 0;
  std::uint64_t ActorCutsceneCameraFractionalSamples = 0;
  std::uint64_t ActorCutsceneCameraCurveSamples = 0;
  std::uint64_t ActorCutsceneCameraSampleFailures = 0;
  std::uint64_t DamageRunTimerBlockCalls = 0;
  std::uint64_t DamageRunTimerLogicalAdvances = 0;
  std::uint64_t DamageRunTimerIntermediateHolds = 0;
  std::uint64_t InvincibilityTimerBlockCalls = 0;
  std::uint64_t InvincibilityTimerLogicalAdvances = 0;
  std::uint64_t InvincibilityTimerIntermediateHolds = 0;
  std::uint64_t InvincibilityTimerPositiveActionHolds = 0;
  std::uint64_t DamageFlickerCounterBlockCalls = 0;
  std::uint64_t DamageFlickerCounterLogicalAdvances = 0;
  std::uint64_t DamageFlickerCounterIntermediateHolds = 0;
  std::uint64_t RespawnDamageBlockCalls = 0;
  std::uint64_t RespawnDamageLogicalAdvances = 0;
  std::uint64_t RespawnDamageIntermediateHolds = 0;
  std::uint64_t RespawnDamageAudioDispatches = 0;
  std::uint64_t RandomTurnTimerDecrementBlockCalls = 0;
  std::uint64_t RandomTurnTimerRefreshBlockCalls = 0;
  std::uint64_t RandomTurnTimerLogicalAdvances = 0;
  std::uint64_t RandomTurnTimerIntermediateHolds = 0;
  std::uint64_t RandomTurnTimerRngDispatches = 0;
  std::uint64_t RandomTurnTimerRngIntermediateSuppressions = 0;
  std::uint64_t AttentionPersistenceBlockCalls = 0;
  std::uint64_t AttentionPersistenceLogicalAdvances = 0;
  std::uint64_t AttentionPersistenceIntermediateHolds = 0;
  std::uint64_t AttentionPersistenceSaturationHolds = 0;
  std::uint64_t CommonCountdownBlockCalls = 0;
  std::uint64_t CommonCountdownLogicalFieldAdvances = 0;
  std::uint64_t CommonCountdownLogicalFieldIntermediateHolds = 0;
  std::uint64_t FairyReviveGraceTimerAdvances = 0;
  std::uint64_t FishingStateBlockCalls = 0;
  std::uint64_t FishingStateLogicalAdvances = 0;
  std::uint64_t FishingStateIntermediateHolds = 0;
  std::uint64_t UnderwaterTimerResetBlockCalls = 0;
  std::uint64_t UnderwaterTimerIncrementBlockCalls = 0;
  std::uint64_t UnderwaterTimerLogicalAdvances = 0;
  std::uint64_t UnderwaterTimerIntermediateHolds = 0;
  std::uint64_t MeleeActionTimerBlockCalls = 0;
  std::uint64_t MeleeActionTimerLogicalAdvances = 0;
  std::uint64_t MeleeActionTimerIntermediateHolds = 0;
  std::uint64_t MeleeActionComboClears = 0;
  std::uint64_t MeleeWeaponTipComboBlockCalls = 0;
  std::uint64_t MeleeWeaponTipComboLogicalAdvances = 0;
  std::uint64_t MeleeWeaponTipComboIntermediateHolds = 0;
  std::uint64_t GameStateUpdateEntryCalls = 0;
  std::uint64_t GameStateMainDispatches = 0;
  std::uint64_t GameStateUpdateReturns = 0;
  std::uint64_t RetainedAotFallbacks = 0;
  std::uint64_t ReadFailures = 0;
  std::uint64_t WriteFailures = 0;
};

std::span<const std::uint32_t> Oot3dTypedGameplayEntryPoints() noexcept;
std::span<const std::uint32_t>
Oot3dTypedGameplayObservableExitPoints() noexcept;
void ResetOot3dTypedGameplayTransientState() noexcept;
void ResetOot3dTypedGameplayStats() noexcept;
Oot3dTypedGameplayStats GetOot3dTypedGameplayStats() noexcept;

bool ExecuteOot3dTypedGameplay(std::uint32_t pc,
                               oot3d::recomp::a32::GuestState &state,
                               NativeA32Memory &memory,
                               oot3d::recomp::a32::ExecutionResult *result,
                               const Oot3dTypedGameplayContext &context,
                               std::uint32_t *blocksConsumed = nullptr);

} // namespace Oot3dNativeGame
