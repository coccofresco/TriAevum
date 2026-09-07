#include "oot3d_typed_gameplay_bridge.h"

#include "oot3d_gameplay_actor.h"
#include "oot3d_gameplay_animation.h"
#include "oot3d_gameplay_camera.h"
#include "oot3d_gameplay_camera_animation.h"
#include "oot3d_gameplay_cutscene.h"
#include "oot3d_gameplay_math.h"
#include "oot3d_gameplay_player.h"
#include "oot3d_gameplay_quake.h"
#include "oot3d_gameplay_skel_anime.h"
#include "oot3d_native_a32_memory.h"
#include "oot3d_native_a32_vfp_ops.h"
#include "oot3d_typed_actor_lifecycle.h"
#include "oot3d_typed_actor_update_records.h"
#include "oot3d_typed_audio_request_callback.h"
#include "oot3d_typed_dyna_interaction_reset.h"
#include "oot3d_typed_game_state.h"
#include "oot3d_typed_pause_ui_alpha.h"
#include "oot3d_typed_player_lock_on.h"
#include "oot3d_typed_record_initializer.h"
#include "recomp/a32_vfp_scalar.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>

namespace Oot3dNativeGame {
namespace {

using oot3d::gameplay::Actor;
using oot3d::gameplay::ActorCutsceneCameraOwnerWire;
using oot3d::gameplay::ActorKinematics;
using oot3d::gameplay::ActorLifecycleState;
using oot3d::gameplay::ActorMovementConstants;
using oot3d::gameplay::AngleSample;
using oot3d::gameplay::AngleTableEntry;
using oot3d::gameplay::AnimationChangeConstants;
using oot3d::gameplay::AnimationChangePlan;
using oot3d::gameplay::AnimationChangePoseEffect;
using oot3d::gameplay::AnimationChangeRequest;
using oot3d::gameplay::AnimationChangeState;
using oot3d::gameplay::AnimationFrameCrossingConstants;
using oot3d::gameplay::AnimationOnFrameConstants;
using oot3d::gameplay::CameraAnimationChannelSamples;
using oot3d::gameplay::CameraAnimationCmadContainerHeaderWire;
using oot3d::gameplay::CameraAnimationCmadRecordWire;
using oot3d::gameplay::CameraAnimationDefaultsWire;
using oot3d::gameplay::CameraAnimationResourcePairWire;
using oot3d::gameplay::CameraAnimationSample;
using oot3d::gameplay::CameraAnimationStateWire;
using oot3d::gameplay::CameraCurveHeaderWire;
using oot3d::gameplay::CameraCurveSampleStatus;
using oot3d::gameplay::CameraCurveType;
using oot3d::gameplay::CameraCurveView;
using oot3d::gameplay::CameraDemo1Wire;
using oot3d::gameplay::CameraPlayStateWire;
using oot3d::gameplay::CameraQuakeCallback;
using oot3d::gameplay::CameraQuakeRequestWire;
using oot3d::gameplay::CameraSpecial5TimerAction;
using oot3d::gameplay::CutsceneActorCueWire;
using oot3d::gameplay::CutsceneCommandStream;
using oot3d::gameplay::CutsceneContextWire;
using oot3d::gameplay::CutsceneFrameOwnerPlayStateWire;
using oot3d::gameplay::CutscenePlayStateWire;
using oot3d::gameplay::GuestPtr;
using oot3d::gameplay::LinkAnimationChangePlan;
using oot3d::gameplay::LinkAnimationChangePoseEffect;
using oot3d::gameplay::PlayerCutsceneActionState;
using oot3d::gameplay::PlayerCutsceneModeConstants;
using oot3d::gameplay::PlayerCutsceneModeState;
using oot3d::gameplay::PlayerCommonCountdownState;
using oot3d::gameplay::PlayerEquipmentState;
using oot3d::gameplay::PlayerHeightConstants;
using oot3d::gameplay::PlayerHeightState;
using oot3d::gameplay::PlayerIdleAnimationState;
using oot3d::gameplay::PlayerLockOnState;
using oot3d::gameplay::PlayerMovementContextWire;
using oot3d::gameplay::PlayerPlayStateWire;
using oot3d::gameplay::PlayerSwimVerticalConstants;
using oot3d::gameplay::PlayerSwimVerticalState;
using oot3d::gameplay::PlayerWireState;
using oot3d::gameplay::SkelAnime;
using oot3d::gameplay::SkelAnimePlaybackState;
using oot3d::gameplay::SkelAnimePoseEffect;
using oot3d::gameplay::SkelAnimeUpdateConstants;
using oot3d::gameplay::SkelAnimeUpdatePlan;

constexpr std::uint32_t kSinInterpolationScaleLiteral = 0x002CFCD8U;
constexpr std::uint32_t kSinTablePointerLiteral = 0x002CFCDCU;
constexpr std::uint32_t kCosInterpolationScaleLiteral = 0x00338F98U;
constexpr std::uint32_t kCosTablePointerLiteral = 0x00338F9CU;
constexpr std::uint32_t kActorPositionRotationScaleLiteral = 0x0033BE5CU;
constexpr std::uint32_t kActorVelocityGravityScaleLiteral = 0x0035FB90U;
constexpr std::uint32_t kActorUpdatePosScaleLiteral = 0x0036B9DCU;
constexpr std::uint32_t kSmoothStepToFScaleLiteral = 0x0036E280U;
constexpr std::uint32_t kSmoothStepToFEpsilonLiteral = 0x0036E284U;
constexpr std::uint32_t kSkelAnimeFrameCrossingZeroLiteral = 0x0036E66CU;
constexpr std::uint32_t kApproachZeroFScaleLiteral = 0x0036FCA0U;
constexpr std::uint32_t kApproachZeroFEpsilonLiteral = 0x0036FCA4U;
constexpr std::uint32_t kSmoothStepUpdateScaleLiteral = 0x00370168U;
constexpr std::uint32_t kSmoothStepRoundingBiasLiteral = 0x0037016CU;
constexpr std::uint32_t kScaledStepToSScaleLiteral = 0x0037040CU;
constexpr std::uint32_t kStepToFZeroLiteral = 0x00370628U;
constexpr std::uint32_t kStepToFScaleLiteral = 0x00370630U;
constexpr std::uint32_t kStepToSScaleLiteral = 0x00372B48U;
constexpr std::uint32_t kStepToSRoundingBiasLiteral = 0x00372B4CU;
constexpr std::uint32_t kApproachFScaleLiteral = 0x00373590U;
constexpr std::uint32_t kApproachFEpsilonLiteral = 0x00373594U;
constexpr std::uint32_t kAnimationOnFrameImplZeroLiteral = 0x00373788U;
constexpr std::uint32_t kSmoothStepToSScaleLiteral = 0x00375B68U;
constexpr std::uint32_t kSmoothStepToSRoundingBiasLiteral = 0x00375B6CU;
constexpr std::uint32_t kActorMoveGravityScaleLiteral = 0x00376934U;
constexpr std::uint32_t kActorMovePositionScaleLiteral = 0x00376938U;
constexpr std::uint32_t kDirectMorphZeroLiteral = 0x002BB338U;
constexpr std::uint32_t kDirectMorphScaleLiteral = 0x002BB348U;
constexpr std::uint32_t kLegacyMorphZeroLiteral = 0x002BB430U;
constexpr std::uint32_t kLegacyMorphScaleLiteral = 0x002BB438U;
constexpr std::uint32_t kAnimationChangeZeroLiteral = 0x00353188U;
constexpr std::uint32_t kAnimationChangeOneLiteral = 0x0035318CU;
constexpr std::uint32_t kStepToAngleSScaleLiteral = 0x00352AA0U;
constexpr std::uint32_t kStepToAngleSRoundingBiasLiteral = 0x00352AA4U;
constexpr std::uint32_t kLinkAnimationPlayOnceWithSpeedZeroLiteral =
    0x00340518U;
constexpr std::uint32_t kLinkAnimationPlayLoopSetSpeedZeroLiteral = 0x00358E6CU;
constexpr std::uint32_t kLinkAnimationPlayOnceZeroLiteral = 0x00359B04U;
constexpr std::uint32_t kLinkAnimationPlayOnceOneLiteral = 0x00359B08U;
constexpr std::uint32_t kLinkAnimationChangeZeroLiteral = 0x003603B8U;
constexpr std::uint32_t kLinkAnimationChangeOneLiteral = 0x003603BCU;
constexpr std::uint32_t kLinkAnimationPlayLoopZeroLiteral = 0x00360554U;
constexpr std::uint32_t kLinkAnimationPlayLoopOneLiteral = 0x00360558U;
constexpr std::uint32_t kLinkAnimationOnFrameHalfLiteral = 0x0036B2C4U;
constexpr std::uint32_t kLinkAnimationOnFrameZeroLiteral = 0x0036B2C8U;
constexpr std::uint32_t kLinkAnimationOnFrameQuantizeScaleLiteral = 0x0036B2CCU;
constexpr std::uint32_t kLinkAnimationOnFrameQuantizeInverseLiteral =
    0x0036B2D0U;
constexpr std::uint32_t kSkelAnimeLegacyScaleLiteral = 0x0036B800U;
constexpr std::uint32_t kSkelAnimeOneLiteral = 0x0036B804U;
constexpr std::uint32_t kSkelAnimeDirectScaleLiteral = 0x0036B808U;
constexpr std::uint32_t kSkelAnimeZeroLiteral = 0x0036B80CU;
constexpr std::uint32_t kSkelAnimeTaperAngleScaleLiteral = 0x0048539CU;
constexpr std::uint32_t kSkelAnimeTaperDirectScaleLiteral = 0x004853A4U;
constexpr std::uint32_t kSkelAnimeTaperZeroLiteral = 0x004853A8U;
constexpr std::uint32_t kSkelAnimeTaperOneLiteral = 0x004853ACU;
constexpr std::uint32_t kPlayerLockOnZeroLiteral = 0x003495DCU;
constexpr std::uint32_t kPlayerSwimTerminalLiteral = 0x0034B25CU;
constexpr std::uint32_t kPlayerSwimZeroLiteral = 0x0034B260U;
constexpr std::uint32_t kPlayerSwimSurfaceOffsetLiteral = 0x0034B264U;
constexpr std::uint32_t kPlayerSwimDampingLiteral = 0x0034B268U;
constexpr std::uint32_t kPlayerSwimSurfaceAccelerationLiteral = 0x0034B26CU;
constexpr std::uint32_t kPlayerSwimSinkThresholdLiteral = 0x0034B270U;
constexpr std::uint32_t kPlayerSwimAlternateSinkThresholdLiteral =
    0x0034B274U;
constexpr std::uint32_t kPlayerSwimSinkAccelerationLiteral = 0x0034B278U;
constexpr std::uint32_t kPlayerSwimRisingTerminalLiteral = 0x0034B27CU;
constexpr std::uint32_t kPlayerSwimRisingAccelerationLiteral = 0x0034B280U;
constexpr std::uint32_t kPlayerSwimDeepWaterThresholdLiteral = 0x0034B284U;
constexpr std::uint32_t kPlayerIdleRuntimeFlagsOffsetLiteral = 0x0034D67CU;
constexpr std::uint32_t kPlayerIdleAnimationTableLiteral = 0x0034D680U;
constexpr std::uint32_t kPlayerIdleTransitionStateLiteral = 0x0034D684U;
constexpr std::uint32_t kEnKanbanTerminalVelocityLiteral = 0x0022CB00U;
constexpr std::uint32_t kPlayerHeightDefaultFlagOffsetLiteral = 0x00367F20U;
constexpr std::uint32_t kPlayerHeightRaisedFlagOffsetLiteral = 0x00367F24U;
constexpr std::uint32_t kPlayerHeightStatePointerLiteral = 0x00367F28U;
constexpr std::uint32_t kPlayerHeightAlternateBaseLiteral = 0x00367F2CU;
constexpr std::uint32_t kPlayerHeightDefaultBaseLiteral = 0x00367F30U;
constexpr std::uint32_t kPlayerCutscenePlayerOffsetLiteral = 0x0036A830U;
constexpr std::uint32_t kPlayerCutsceneMaskLiteral = 0x0036A834U;
constexpr std::uint32_t kPlayerCutsceneGlobalStateLiteral = 0x0036A838U;
constexpr std::uint32_t kPlayerMeleeWeaponTipStepLiteral = 0x00313CC0U;
constexpr std::uint32_t kPlayerMeleeWeaponTipOneLiteral = 0x00313CC4U;
constexpr std::array kPlayerInvincibilityHoldActionLiterals{
    0x00251314U,
    0x00251318U,
    0x0025131CU,
    0x00251320U,
};
constexpr std::uint32_t kPicaCommandListCursorAddress = 0x0054CC4CU;
constexpr std::uint32_t kMainCutsceneCameraLocalFrameAddress = 0x0051B310U;
constexpr std::uint32_t kMainCutsceneCameraStateOffset = 0x232CU;
constexpr std::uint32_t kActorCutsceneCameraPrimingStateAddress = 0x0059B964U;
constexpr std::uint32_t kActorCutsceneCameraStateAddress = 0x0059BB20U;
constexpr std::uint32_t kCameraGlobalStateAddress = 0x00516E9CU;
constexpr std::uint32_t kCameraFloorMissCounterOffset = 0x9CU;
constexpr std::uint32_t kCameraInterfaceDelayOffset = 0x28U;
constexpr std::uint32_t kCameraInterfaceCommand = 0x3200U;
constexpr std::uint32_t kCameraWaterDistortionTimerOffset = 0x198U;
constexpr std::uint32_t kCameraWaterFlag4BaseLiteral = 0x002D8FA0U;
constexpr std::uint32_t kCameraWaterFlag4ScaleLiteral = 0x002D8FA4U;
constexpr std::uint32_t kCameraWaterFlag8Field26Literal = 0x002D9370U;
constexpr std::uint32_t kCameraWaterFlag8Field0Literal = 0x002D9374U;
constexpr std::uint32_t kCameraWaterFlag8Field23Literal = 0x002D9378U;
constexpr std::uint32_t kCameraWaterFlag8Field18Literal = 0x002D937CU;
constexpr std::uint32_t kCameraWaterFlag8Field19Literal = 0x002D9380U;
constexpr std::uint32_t kCameraWaterFlag8Field24Literal = 0x002D9384U;
constexpr std::uint32_t kCameraWaterFlag8ScaleLiteral = 0x002D9388U;
constexpr std::uint32_t kCameraQuakeRequestArrayAddress = 0x005A543CU;
constexpr std::uint32_t kCameraQuakeRequestCount = 4U;
constexpr std::uint32_t kCameraQuakeComposeHelperEntry = 0x00369D44U;
constexpr std::uint32_t kCameraQuakeActiveBranch = 0x00478904U;
constexpr std::uint32_t kCameraQuakeRemoveBranch = 0x004788E4U;
constexpr std::uint32_t kCameraQuakeRandomStateAddress = 0x0050C0C4U;

enum class CameraModeCountdownLoad : std::uint8_t {
  Unsigned16,
  Signed16,
};

struct CameraModeCountdownSite {
  std::uint32_t Entry = 0U;
  std::uint32_t Continuation = 0U;
  std::uint8_t CameraRegister = 0U;
  std::uint8_t ModeBaseRegister = 0U;
  std::uint16_t ModeBaseOffset = 0U;
  std::uint16_t CountdownOffset = 0U;
  CameraModeCountdownLoad Load = CameraModeCountdownLoad::Unsigned16;
  bool RequiresNonzeroCondition = false;
};

// These boundaries are the homologous frame countdowns reached by the
// callbacks referenced by native camera setting 1. Registers and offsets are
// ABI facts recovered from each callback body, not camera-name dispatch.
constexpr std::array<CameraModeCountdownSite, 12>
    kCameraModeCountdownSites{{
        {kOot3dCameraJump1FrameCountdownBlock,
         kOot3dCameraJump1FrameCountdownContinue, 4U, 5U, 0x20U, 0x3AU},
        {kOot3dCameraJump2FrameCountdownBlock,
         kOot3dCameraJump2FrameCountdownContinue, 4U, 5U, 0x24U, 0x30U},
        {kOot3dCameraBattle1FrameCountdownBlock,
         kOot3dCameraBattle1FrameCountdownContinue, 4U, 5U, 0x30U, 0x4AU},
        {kOot3dCameraBattle4FrameCountdownBlock,
         kOot3dCameraBattle4FrameCountdownContinue, 4U, 6U, 0x1CU, 0x1CU},
        {kOot3dCameraKeepOn1FrameCountdownBlock,
         kOot3dCameraKeepOn1FrameCountdownContinue, 4U, 5U, 0x34U, 0x4AU},
        {kOot3dCameraKeepOn3FrameCountdownBlock,
         kOot3dCameraKeepOn3FrameCountdownContinue, 6U, 4U, 0x30U, 0x4CU},
        {kOot3dCameraNormal1FrameCountdownBlock,
         kOot3dCameraNormal1FrameCountdownContinue, 4U, 5U, 0x24U, 0x4CU,
         CameraModeCountdownLoad::Signed16, true},
        {kOot3dCameraNormal1SpeedCountdownBlock,
         kOot3dCameraNormal1SpeedCountdownContinue, 4U, 5U, 0x24U, 0x4EU},
        {kOot3dCameraNormal1RateCountdownBlock,
         kOot3dCameraNormal1RateCountdownContinue, 4U, 5U, 0x24U, 0x3EU},
        {kOot3dCameraUnique1FrameCountdownBlock,
         kOot3dCameraUnique1FrameCountdownContinue, 4U, 5U, 0x1CU, 0x24U},
        {kOot3dCameraSubj3FrameCountdownBlock,
         kOot3dCameraSubj3FrameCountdownContinue, 4U, 5U, 0x24U, 0x2CU},
        {kOot3dCameraParallel1FrameCountdownBlock,
         kOot3dCameraParallel1FrameCountdownContinue, 4U, 5U, 0x28U, 0x40U},
    }};

constexpr std::uint32_t kSkelAnimeLegacySampleTail = 0x0036B5ACU;
constexpr std::uint32_t kSkelAnimeLegacyTerminalTail = 0x0036B5ECU;
constexpr std::uint32_t kSkelAnimeLegacyOnceSampleTail = 0x0036B650U;
constexpr std::uint32_t kSkelAnimeLegacyBlendTail = 0x0036B6B0U;
constexpr std::uint32_t kSkelAnimeDirectTerminalTail = 0x0036B7CCU;
constexpr std::uint32_t kSkelAnimeDirectSampleTail = 0x0036B86CU;
constexpr std::uint32_t kSkelAnimeDirectLinearBlendTail = 0x0036B8DCU;
constexpr std::uint32_t kSkelAnimeDirectTaperedBlendTail = 0x00485350U;
constexpr std::uint32_t kDirectMorphNoBlendTail = 0x002BB2E8U;
constexpr std::uint32_t kDirectMorphBlendTail = 0x002BB2F4U;
constexpr std::uint32_t kLegacyMorphNoBlendTail = 0x002BB404U;
constexpr std::uint32_t kLegacyMorphBlendTail = 0x002BB40CU;
constexpr std::uint32_t kSkelAnimeGetFrameDataEntry = 0x003204A4U;
constexpr std::uint32_t kSkelAnimeCopyFrameTableEntry = 0x00358338U;
constexpr std::uint32_t kAnimationChangeEpilogue = 0x0035317CU;
constexpr std::uint32_t kLinkAnimationQueueMorphTail = 0x003602E4U;
constexpr std::uint32_t kLinkAnimationQueueJointTail = 0x00360330U;
constexpr std::uint32_t kLinkAnimationCommonTail = 0x00360378U;
constexpr std::uint32_t kAngleTableEntrySize = 0x10U;
constexpr std::uint32_t kAnimationTransformSize = 0x34U;

constexpr std::array kTypedGameplayEntryPoints{
    kOot3dCameraQuakeSineCallbackEntry,
    kOot3dCameraQuakeSineFadeCallbackEntry,
    kOot3dCameraQuakeRandomCallbackEntry,
    kOot3dCameraQuakePerpetualSineRandomCallbackEntry,
    kOot3dCameraQuakeRandomFadeCallbackEntry,
    kOot3dCameraQuakeSineRandomCallbackEntry,
    kOot3dEnKoBlinkAdvanceBlock,
    kOot3dEnKoBlinkRngReturnBlock,
    kOot3dEnZl4ActorCameraAdvanceBlock,
    kOot3dCameraDemo1Entry,
    kOot3dCameraJump1FrameCountdownBlock,
    kOot3dCameraJump2FrameCountdownBlock,
    kOot3dEnKanbanPhaseAdvanceBlock,
    kOot3dEnKanbanState0CountdownBlock,
    kOot3dEnKanbanActorFlagCountdownBlock,
    kOot3dEnKanbanInteractionCooldownBlock,
    kOot3dEnKanbanDrawGateRampBlock,
    kOot3dEnKanbanOscillatorXBlock,
    kOot3dEnKanbanOscillatorYBlock,
    kOot3dEnKanbanPieceLifetimeBlock,
    kOot3dEnKanbanRippleEventGateBlock,
    kOot3dCameraBattle1FrameCountdownBlock,
    kOot3dCameraBattle4FrameCountdownBlock,
    kOot3dCameraKeepOn1FrameCountdownBlock,
    kOot3dCameraKeepOn3FrameCountdownBlock,
    kOot3dCameraNormal1FrameCountdownBlock,
    kOot3dCameraNormal1SpeedCountdownBlock,
    kOot3dCameraNormal1RateCountdownBlock,
    kOot3dCameraUnique1FrameCountdownBlock,
    kOot3dPlayerRespawnDamageAdvanceBlock,
    kOot3dPlayerCommonCountdownBlock,
    kOot3dPlayerInvincibilityTimerBlock,
    kOot3dPlayerDamageRunTimerBlock,
    kOot3dPlayerAttentionPersistenceAdvanceBlock,
    kOot3dPlayerFishingStateRecoveryBlock,
    kOot3dPlayerMeleeActionTimerBlock,
    kOot3dPlayerUnderwaterTimerResetBlock,
    kOot3dPlayerUnderwaterTimerIncrementBlock,
    kOot3dPlayerRandomTurnTimerDecrementBlock,
    kOot3dPlayerRandomTurnTimerRefreshBlock,
    kOot3dCameraSpecial5TimerBlock,
    kOot3dCameraSubj3FrameCountdownBlock,
    kOot3dCameraParallel1FrameCountdownBlock,
    kOot3dSkelAnimeDirectMorphBoundary,
    kOot3dSkelAnimeLegacyMorphBoundary,
    kOot3dSinIdx8Entry,
    kOot3dCameraWaterDistortionTimerAdvanceBlock,
    kOot3dActorDestroyEntry,
    kOot3dActorDestroyCallbackReturn,
    kOot3dActorDestroyModelContextReturn,
    kOot3dActorDestroyOwnedSlotReturn,
    kOot3dActorDestroyLastOwnedSlotReturn,
    kOot3dCameraFloorMissCounterAdvanceBlock,
    kOot3dCameraInterfaceDelayAdvanceBlock,
    kOot3dCameraWaterDistortionFlag4SampleBlock,
    kOot3dCameraWaterDistortionFlag8SampleBlock,
    kOot3dCameraWaterDistortionCustomSampleBlock,
    kOot3dRecordInitializerEntry,
    kOot3dRecordInitializerMemzeroReturn,
    kOot3dPlayerMeleeWeaponTipComboAdvanceBlock,
    kOot3dSkelAnimeSetUpdateEntry,
    kOot3dCutsceneNormalFrameAdvanceBlock,
    kOot3dPlayerGetExplosiveHeldEntry,
    kOot3dPlayerSetCsActionEntry,
    kOot3dPlayerReleaseLockOnEntry,
    kOot3dCosIdx8Entry,
    kOot3dActorUpdatePosWithVelocityFromRotationEntry,
    kOot3dCameraAnimationApplyFrameEntry,
    kOot3dLinkAnimationPlayOnceWithSpeedEntry,
    kOot3dZarGetCsabByIndexEntry,
    kOot3dPlayerUpdateHostileLockOnEntry,
    kOot3dPlayerUpdateSwimVerticalVelocityEntry,
    kOot3dPlayerHoldsHookshotWithoutHeldActorEntry,
    kOot3dPlayerGetIdleAnimEntry,
    kOot3dPlayerIsItemInHandEntry,
    kOot3dMathStepToAngleSEntry,
    kOot3dAnimationChangeEntry,
    kOot3dPlayerHoldsHookshotEntry,
    kOot3dLinkAnimationPlayLoopSetSpeedEntry,
    kOot3dLinkAnimationPlayOnceEntry,
    kOot3dPlayerSetRuntimeFlag200Entry,
    kOot3dPlayerHoldsTwoHandedWeaponEntry,
    kOot3dActorUpdateVelocityXZGravityEntry,
    kOot3dLinkAnimationChangeEntry,
    kOot3dLinkAnimationPlayLoopEntry,
    kOot3dEnvironmentPathInterpolateActorPosAndRotationEntry,
    kOot3dActorUpdateVelocityXYZEntry,
    kOot3dPlayerGetHeightEntry,
    kOot3dPlayerInCsModeEntry,
    kOot3dLinkAnimationOnFrameEntry,
    kOot3dSkelAnimeUpdateEntry,
    kOot3dActorUpdatePosEntry,
    kOot3dActorHasNoParentEntry,
    kOot3dMathSmoothStepToFEntry,
    kOot3dSkelAnimeIsFrameCrossedEntry,
    kOot3dPlayerSetCsActionWithHaltedActorsEntry,
    kOot3dMathApproachZeroFEntry,
    kOot3dMathSmoothStepToSUpdateRateEntry,
    kOot3dMathScaledStepToSEntry,
    kOot3dMathStepToFEntry,
    kOot3dActorHasParentEntry,
    kOot3dMathStepToSEntry,
    kOot3dMathApproachFEntry,
    kOot3dAnimationOnFrameImplEntry,
    kOot3dActorKillEntry,
    kOot3dActorSetScaleEntry,
    kOot3dMathSmoothStepToSEntry,
    kOot3dActorMoveForwardEntry,
    kOot3dAnimationGetLengthEntry,
    kOot3dGameStateUpdateOwnerEntry,
    kOot3dGameStateUpdateMainReturn,
    kOot3dActorUpdateAllContextFreezeBlock,
    kOot3dActorUpdateAllInitCallbackEntry,
    kOot3dActorUpdateAllInitCallbackReturn,
    kOot3dActorUpdateAllInstanceFreezeBlock,
    kOot3dActorUpdateAllEffectTimersBlock,
    kOot3dActorUpdateAllUpdateCallbackEntry,
    kOot3dAudioRequestFlag100CallbackEntry,
    kOot3dAudioRequestFlag100ReferenceAcquireReturn,
    kOot3dAudioRequestFlag100StatusQueryReturn,
    kOot3dAudioRequestFlag100ReferenceCleanupReturn,
    kOot3dMeshCommandPacketSubmitEntry,
    kOot3dCameraQuakeCallbackReturnBoundary,
    kOot3dPauseUiUpdateDualAlphaEntry,
    kOot3dPauseUiAlphaPauseStateReturn,
    kOot3dPauseUiAlphaFadeOutStepReturn,
    kOot3dPauseUiAlphaFadeInStepReturn,
    kOot3dDynaResetActorInteractionIfRegisteredEntry,
    kOot3dActorUpdateRecordInitializeDefaultsEntry,
    kOot3dActorUpdateRecordClearHalfwordsEntry,
    kOot3dPlayerDamageFlickerCounterBlock,
};

constexpr std::array kTypedGameplayObservableExitPoints{
    kOot3dCameraQuakeSineCallbackEntry,
    kOot3dCameraQuakeSineFadeCallbackEntry,
    kOot3dCameraQuakeRandomCallbackEntry,
    kOot3dCameraQuakePerpetualSineRandomCallbackEntry,
    kOot3dCameraQuakeRandomFadeCallbackEntry,
    kOot3dCameraQuakeSineRandomCallbackEntry,
    kOot3dEnKoBlinkAdvanceBlock,
    kOot3dEnKoBlinkRngReturnBlock,
    kOot3dEnZl4ActorCameraAdvanceBlock,
    kOot3dCameraJump1FrameCountdownBlock,
    kOot3dCameraJump2FrameCountdownBlock,
    kOot3dEnKanbanPhaseAdvanceBlock,
    kOot3dEnKanbanState0CountdownBlock,
    kOot3dEnKanbanActorFlagCountdownBlock,
    kOot3dEnKanbanInteractionCooldownBlock,
    kOot3dEnKanbanDrawGateRampBlock,
    kOot3dEnKanbanOscillatorXBlock,
    kOot3dEnKanbanOscillatorYBlock,
    kOot3dEnKanbanPieceLifetimeBlock,
    kOot3dEnKanbanRippleEventGateBlock,
    kOot3dCameraBattle1FrameCountdownBlock,
    kOot3dCameraBattle4FrameCountdownBlock,
    kOot3dCameraKeepOn1FrameCountdownBlock,
    kOot3dCameraKeepOn3FrameCountdownBlock,
    kOot3dCameraNormal1FrameCountdownBlock,
    kOot3dCameraNormal1SpeedCountdownBlock,
    kOot3dCameraNormal1RateCountdownBlock,
    kOot3dCameraUnique1FrameCountdownBlock,
    kOot3dPlayerRespawnDamageAdvanceBlock,
    kOot3dPlayerCommonCountdownBlock,
    kOot3dPlayerInvincibilityTimerBlock,
    kOot3dPlayerDamageRunTimerBlock,
    kOot3dPlayerAttentionPersistenceAdvanceBlock,
    kOot3dPlayerFishingStateRecoveryBlock,
    kOot3dPlayerMeleeActionTimerBlock,
    kOot3dPlayerUnderwaterTimerResetBlock,
    kOot3dPlayerUnderwaterTimerIncrementBlock,
    kOot3dPlayerRandomTurnTimerDecrementBlock,
    kOot3dPlayerRandomTurnTimerRefreshBlock,
    kOot3dCameraSpecial5TimerBlock,
    kOot3dCameraSubj3FrameCountdownBlock,
    kOot3dCameraParallel1FrameCountdownBlock,
    kOot3dCameraWaterDistortionTimerAdvanceBlock,
    kOot3dActorDestroyEntry,
    kOot3dActorDestroyCallbackReturn,
    kOot3dActorDestroyModelContextReturn,
    kOot3dActorDestroyOwnedSlotReturn,
    kOot3dActorDestroyLastOwnedSlotReturn,
    kOot3dCameraFloorMissCounterAdvanceBlock,
    kOot3dCameraInterfaceDelayAdvanceBlock,
    kOot3dCameraWaterDistortionFlag4SampleBlock,
    kOot3dCameraWaterDistortionFlag8SampleBlock,
    kOot3dCameraWaterDistortionCustomSampleBlock,
    kOot3dRecordInitializerEntry,
    kOot3dRecordInitializerMemzeroReturn,
    kOot3dPlayerMeleeWeaponTipComboAdvanceBlock,
    kOot3dCutsceneNormalFrameAdvanceBlock,
    kOot3dGameStateUpdateOwnerEntry,
    kOot3dGameStateUpdateMainReturn,
    kOot3dActorUpdateAllContextFreezeBlock,
    kOot3dActorUpdateAllInitCallbackEntry,
    kOot3dActorUpdateAllInitCallbackReturn,
    kOot3dActorUpdateAllInstanceFreezeBlock,
    kOot3dActorUpdateAllEffectTimersBlock,
    kOot3dActorUpdateAllUpdateCallbackEntry,
    kOot3dAudioRequestFlag100CallbackEntry,
    kOot3dAudioRequestFlag100ReferenceAcquireReturn,
    kOot3dAudioRequestFlag100StatusQueryReturn,
    kOot3dAudioRequestFlag100ReferenceCleanupReturn,
    kOot3dCameraQuakeCallbackReturnBoundary,
    kOot3dPauseUiUpdateDualAlphaEntry,
    kOot3dPauseUiAlphaPauseStateReturn,
    kOot3dPauseUiAlphaFadeOutStepReturn,
    kOot3dPauseUiAlphaFadeInStepReturn,
    kOot3dDynaResetActorInteractionIfRegisteredEntry,
    kOot3dPlayerDamageFlickerCounterBlock,
};

Oot3dTypedGameplayStats gStats;

struct CameraAnimationBinding {
  std::uint32_t PlayAddress = 0U;
  std::uint32_t ActiveCutsceneDataAddress = 0U;
  std::uint32_t OutputStateAddress = 0U;
  std::uint32_t DefaultsAddress = 0U;
  std::uint32_t CmadContainerAddress = 0U;
  std::int32_t NativeFrame = 0;
  bool Valid = false;
};

CameraAnimationBinding gMainCutsceneCameraBinding;

struct ActorCameraAnimationBinding {
  std::uint32_t OwnerAddress = 0U;
  std::uint32_t OutputStateAddress = 0U;
  std::uint32_t DefaultsAddress = 0U;
  std::uint32_t CmadContainerAddress = 0U;
  std::int32_t NativeFrame = 0;
  std::int32_t AnimationIndex = -1;
  bool Valid = false;
};

ActorCameraAnimationBinding gActorCutsceneCameraBinding;

struct CameraQuakeSignalCache {
  std::int16_t RequestId = 0;
  std::int16_t InitialCountdown = 0;
  std::int16_t CountdownAfter = 0;
  std::uint32_t CameraAddress = 0U;
  CameraQuakeCallback Callback = CameraQuakeCallback::None;
  oot3d::gameplay::CameraQuakeFactors Factors;
  bool Valid = false;
};

struct CameraQuakePendingReturn {
  std::uint32_t RequestAddress = 0U;
  std::int32_t ReturnValue = 0;
  bool Active = false;
};

std::array<CameraQuakeSignalCache, kCameraQuakeRequestCount>
    gCameraQuakeSignalCache;
CameraQuakePendingReturn gCameraQuakePendingReturn;

template <typename T>
bool ReadWireObject(const NativeA32Memory &memory, std::uint32_t address,
                    T *value) {
  static_assert(std::is_trivially_copyable_v<T>);
  if (value == nullptr) {
    return false;
  }
  return memory.ReadBytes(
      address, std::span<std::uint8_t>(reinterpret_cast<std::uint8_t *>(value),
                                       sizeof(T)));
}

template <typename T>
bool WriteWireObject(NativeA32Memory &memory, std::uint32_t address,
                     const T &value) {
  static_assert(std::is_trivially_copyable_v<T>);
  return memory.WriteBytes(
      address, std::span<const std::uint8_t>(
                   reinterpret_cast<const std::uint8_t *>(&value), sizeof(T)));
}

bool ReadFloat(const NativeA32Memory &memory, std::uint32_t address,
               float *value) {
  std::uint32_t bits = 0;
  if (value == nullptr || !memory.ReadFast(address, &bits)) {
    return false;
  }
  *value = std::bit_cast<float>(bits);
  return std::isfinite(*value);
}

bool CheckedAddress(std::uint32_t base, std::uint64_t offset,
                    std::uint32_t *address) noexcept {
  const std::uint64_t result = static_cast<std::uint64_t>(base) + offset;
  if (address == nullptr ||
      result > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }
  *address = static_cast<std::uint32_t>(result);
  return true;
}

bool ReadCsabByIndex(const NativeA32Memory &memory, std::uint32_t zarAddress,
                     std::uint32_t animationIndex, std::uint32_t *csabAddress) {
  if (zarAddress == 0U || csabAddress == nullptr) {
    return false;
  }

  std::uint32_t selectedGroup = 0U;
  if (!memory.ReadFast(zarAddress + 0x20U, &selectedGroup)) {
    return false;
  }
  if (selectedGroup == std::numeric_limits<std::uint32_t>::max()) {
    *csabAddress = 0U;
    return true;
  }

  std::uint32_t groupTable = 0U;
  std::uint32_t groupAddress = 0U;
  std::uint32_t resourceCount = 0U;
  if (!memory.ReadFast(zarAddress + 0x0CU, &groupTable) || groupTable == 0U ||
      !CheckedAddress(groupTable,
                      static_cast<std::uint64_t>(selectedGroup) * 0x10U,
                      &groupAddress) ||
      !memory.ReadFast(groupAddress, &resourceCount)) {
    return false;
  }
  if (resourceCount <= animationIndex) {
    *csabAddress = 0U;
    return true;
  }

  std::uint32_t csabTable = 0U;
  std::uint32_t entryAddress = 0U;
  return memory.ReadFast(zarAddress + 0x50U, &csabTable) && csabTable != 0U &&
         CheckedAddress(csabTable,
                        static_cast<std::uint64_t>(animationIndex) * 4U,
                        &entryAddress) &&
         memory.ReadFast(entryAddress, csabAddress);
}

bool ReadCsabFrameCount(const NativeA32Memory &memory,
                        std::uint32_t csabAddress, std::int32_t *frameCount) {
  if (frameCount == nullptr) {
    return false;
  }
  if (csabAddress == 0U) {
    *frameCount = -1;
    return true;
  }

  std::uint32_t csabData = 0U;
  std::uint32_t frameTableOffset = 0U;
  std::uint32_t frameCountAddress = 0U;
  std::uint32_t frameCountWord = 0U;
  if (!memory.ReadFast(csabAddress, &csabData) || csabData == 0U ||
      !memory.ReadFast(csabData + 0x14U, &frameTableOffset) ||
      !CheckedAddress(csabData,
                      static_cast<std::uint64_t>(frameTableOffset) + 0x10U,
                      &frameCountAddress) ||
      !memory.ReadFast(frameCountAddress, &frameCountWord)) {
    return false;
  }
  *frameCount = static_cast<std::int32_t>(
      std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(frameCountWord)));
  return true;
}

bool ReadAnimationFrameCount(const NativeA32Memory &memory,
                             std::uint32_t skelAnimeAddress,
                             std::uint32_t animationIndex,
                             std::int32_t *frameCount) {
  std::uint32_t zarAddress = 0U;
  std::uint32_t csabAddress = 0U;
  return skelAnimeAddress != 0U &&
         memory.ReadFast(skelAnimeAddress + static_cast<std::uint32_t>(offsetof(
                                                SkelAnime, ZarArchive)),
                         &zarAddress) &&
         ReadCsabByIndex(memory, zarAddress, animationIndex, &csabAddress) &&
         ReadCsabFrameCount(memory, csabAddress, frameCount);
}

bool ReadLinkAnimationResource(const NativeA32Memory &memory,
                               const SkelAnime &skelAnime,
                               std::uint32_t animationIndex,
                               std::uint32_t *csabAddress,
                               std::int32_t *frameCount) {
  if (csabAddress == nullptr || frameCount == nullptr ||
      skelAnime.ZarArchive.Address == 0U ||
      !ReadCsabByIndex(memory, skelAnime.ZarArchive.Address, animationIndex,
                       csabAddress)) {
    return false;
  }
  if (*csabAddress == 0U &&
      !ReadCsabByIndex(memory, skelAnime.ZarArchive.Address, 0U, csabAddress)) {
    return false;
  }
  return *csabAddress != 0U &&
         ReadCsabFrameCount(memory, *csabAddress, frameCount);
}

std::int16_t LowS16(std::uint32_t value) noexcept {
  return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value));
}

std::uint32_t SubtractConditionFlags(std::uint32_t left,
                                     std::uint32_t right) noexcept {
  const std::uint32_t value = left - right;
  std::uint32_t flags = 0U;
  if ((value & oot3d::recomp::a32::kFlagN) != 0U) {
    flags |= oot3d::recomp::a32::kFlagN;
  }
  if (value == 0U) {
    flags |= oot3d::recomp::a32::kFlagZ;
  }
  if (left >= right) {
    flags |= oot3d::recomp::a32::kFlagC;
  }
  if ((((left ^ right) & (left ^ value)) &
       oot3d::recomp::a32::kFlagN) != 0U) {
    flags |= oot3d::recomp::a32::kFlagV;
  }
  return flags;
}

bool CompleteCall(std::uint32_t entry, oot3d::recomp::a32::GuestState &state,
                  oot3d::recomp::a32::ExecutionResult *result,
                  std::uint32_t *blocksConsumed) {
  state.r[15] = state.r[14];
  *result = {
      oot3d::recomp::a32::ExitKind::Branch,
      state.r[15],
      oot3d::recomp::a32::FallbackReason::None,
      entry,
  };
  if (blocksConsumed != nullptr) {
    *blocksConsumed = 1U;
  }
  ++gStats.Calls;
  return true;
}

bool FailRead() {
  ++gStats.RetainedAotFallbacks;
  ++gStats.ReadFailures;
  return false;
}

bool FailWrite() {
  ++gStats.RetainedAotFallbacks;
  ++gStats.WriteFailures;
  return false;
}

bool ExecuteMeshCommandPacketSubmit(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed) {
  const std::uint32_t packetAddress = state.r[0];
  std::uint32_t byteCount = 0U;
  std::uint32_t activePacketIndex = 0U;
  std::uint32_t commandListCursor = 0U;
  if (!memory.ReadFast(packetAddress + 0x10U, &byteCount) ||
      !memory.ReadFast(packetAddress + 0x14U, &activePacketIndex) ||
      !memory.ReadFast(kPicaCommandListCursorAddress, &commandListCursor)) {
    return FailRead();
  }
  if (!memory.IsWritable(kPicaCommandListCursorAddress,
                         sizeof(commandListCursor))) {
    return FailWrite();
  }

  const std::int32_t signedByteCount = std::bit_cast<std::int32_t>(byteCount);
  std::uint32_t packetSlotAddress = 0U;
  if (!CheckedAddress(packetAddress,
                      static_cast<std::uint64_t>(activePacketIndex) * 4U +
                          0x08U,
                      &packetSlotAddress)) {
    return FailRead();
  }
  std::uint32_t commandSource = 0U;
  if (!memory.ReadFast(packetSlotAddress, &commandSource)) {
    return FailRead();
  }

  if (signedByteCount > 0) {
    if ((byteCount & 0x07U) != 0U) {
      ++gStats.RetainedAotFallbacks;
      return false;
    }
    const std::uint8_t *source =
        memory.GetReadPointer(commandSource, byteCount);
    if (source == nullptr) {
      return FailRead();
    }
    if (!memory.IsWritable(commandListCursor, byteCount)) {
      return FailWrite();
    }
    if (!memory.WriteBytes(
            commandListCursor,
            std::span<const std::uint8_t>(source, byteCount))) {
      return FailWrite();
    }
  }

  const std::uint32_t nextCursor = commandListCursor + byteCount;
  if (!memory.WriteFast(kPicaCommandListCursorAddress, nextCursor)) {
    return FailWrite();
  }

  // Preserve the observable caller-clobbered results of
  // PicaCommandStats_AddBytes, the native tail called by this routine.
  state.r[0] = nextCursor;
  state.r[1] = kPicaCommandListCursorAddress;
  state.r[2] = commandListCursor;
  ++gStats.RendererCalls;
  return CompleteCall(kOot3dMeshCommandPacketSubmitEntry, state, result,
                      blocksConsumed);
}

bool ReadAngleTableEntry(const NativeA32Memory &memory,
                         std::uint32_t tablePointerLiteral, std::uint16_t angle,
                         AngleTableEntry *entry) {
  std::uint32_t tableAddress = 0;
  if (!memory.ReadFast(tablePointerLiteral, &tableAddress)) {
    return false;
  }
  const std::uint32_t index = angle >> 8U;
  const std::uint64_t entryAddress =
      static_cast<std::uint64_t>(tableAddress) +
      static_cast<std::uint64_t>(index) * kAngleTableEntrySize;
  return entryAddress <= std::numeric_limits<std::uint32_t>::max() &&
         ReadWireObject(memory, static_cast<std::uint32_t>(entryAddress),
                        entry);
}

bool ReadAngleComponent(const NativeA32Memory &memory, std::uint16_t angle,
                        bool cosine, float *value) {
  AngleTableEntry entry;
  float interpolationScale = 0.0f;
  const std::uint32_t tablePointerLiteral =
      cosine ? kCosTablePointerLiteral : kSinTablePointerLiteral;
  const std::uint32_t scaleLiteral =
      cosine ? kCosInterpolationScaleLiteral : kSinInterpolationScaleLiteral;
  if (!ReadAngleTableEntry(memory, tablePointerLiteral, angle, &entry) ||
      !ReadFloat(memory, scaleLiteral, &interpolationScale)) {
    return false;
  }
  *value = cosine
               ? oot3d::gameplay::InterpolateAngleCos(angle, interpolationScale,
                                                      entry)
               : oot3d::gameplay::InterpolateAngleSin(angle, interpolationScale,
                                                      entry);
  return std::isfinite(*value);
}

bool ReadAngleSample(const NativeA32Memory &memory, std::uint16_t angle,
                     AngleSample *sample) {
  return sample != nullptr &&
         ReadAngleComponent(memory, angle, false, &sample->Sin) &&
         ReadAngleComponent(memory, angle, true, &sample->Cos);
}

CameraQuakeCallback CameraQuakeCallbackForEntry(
    std::uint32_t entry) noexcept {
  switch (entry) {
  case kOot3dCameraQuakeSineRandomCallbackEntry:
    return CameraQuakeCallback::SineRandom;
  case kOot3dCameraQuakeRandomCallbackEntry:
    return CameraQuakeCallback::Random;
  case kOot3dCameraQuakeSineFadeCallbackEntry:
    return CameraQuakeCallback::SineFade;
  case kOot3dCameraQuakeRandomFadeCallbackEntry:
    return CameraQuakeCallback::RandomFade;
  case kOot3dCameraQuakeSineCallbackEntry:
    return CameraQuakeCallback::Sine;
  case kOot3dCameraQuakePerpetualSineRandomCallbackEntry:
    return CameraQuakeCallback::PerpetualSineRandom;
  default:
    return CameraQuakeCallback::None;
  }
}

bool ResolveCameraQuakeSlot(std::uint32_t requestAddress,
                            std::size_t *slot) noexcept {
  if (slot == nullptr || requestAddress < kCameraQuakeRequestArrayAddress) {
    return false;
  }
  const std::uint32_t offset =
      requestAddress - kCameraQuakeRequestArrayAddress;
  if ((offset % sizeof(CameraQuakeRequestWire)) != 0U) {
    return false;
  }
  const std::size_t candidate = offset / sizeof(CameraQuakeRequestWire);
  if (candidate >= kCameraQuakeRequestCount) {
    return false;
  }
  *slot = candidate;
  return true;
}

bool CompleteCameraQuakeCallbackReturn(
    oot3d::recomp::a32::GuestState &state,
    oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed) {
  if (!gCameraQuakePendingReturn.Active) {
    return false;
  }

  state.r[0] =
      std::bit_cast<std::uint32_t>(gCameraQuakePendingReturn.ReturnValue);
  constexpr std::uint32_t kConditionFlagsMask =
      oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagZ |
      oot3d::recomp::a32::kFlagC | oot3d::recomp::a32::kFlagV;
  state.cpsr = (state.cpsr & ~kConditionFlagsMask) |
               SubtractConditionFlags(state.r[0], 0U);
  const std::uint32_t next =
      state.r[0] != 0U ? kCameraQuakeActiveBranch : kCameraQuakeRemoveBranch;
  state.r[15] = next;
  *result = {
      oot3d::recomp::a32::ExitKind::Branch,
      next,
      oot3d::recomp::a32::FallbackReason::None,
      kOot3dCameraQuakeCallbackReturnBoundary,
  };
  if (blocksConsumed != nullptr) {
    *blocksConsumed = 1U;
  }
  gCameraQuakePendingReturn = {};
  ++gStats.CameraQuakeReturnDispatches;
  return true;
}

enum class CameraQuakeFactorStatus {
  Ready,
  CacheMiss,
  Failure,
};

CameraQuakeFactorStatus ResolveCameraQuakeFactorsForTick(
    const oot3d::gameplay::CameraQuakeSignalPlan &plan,
    const CameraQuakeRequestWire &request, std::size_t slot,
    const oot3d::gameplay::TimeContext &time, NativeA32Memory &memory,
    oot3d::gameplay::CameraQuakeFactors *factors) {
  if (factors == nullptr || !plan.Evaluate) {
    return CameraQuakeFactorStatus::Failure;
  }

  auto &cache = gCameraQuakeSignalCache[slot];
  if (!time.CrossedLogicalFrame) {
    if (!cache.Valid || cache.RequestId != request.RequestId ||
        cache.InitialCountdown != request.InitialCountdown ||
        cache.CountdownAfter != request.Countdown ||
        cache.CameraAddress != request.CameraAddress ||
        cache.Callback != plan.Callback) {
      ++gStats.CameraQuakeSignalCacheMisses;
      return CameraQuakeFactorStatus::CacheMiss;
    }
    *factors = cache.Factors;
    ++gStats.CameraQuakeSignalReuses;
    return CameraQuakeFactorStatus::Ready;
  }

  float sine = 0.0F;
  if (plan.NeedsSine &&
      !ReadAngleComponent(memory,
                          std::bit_cast<std::uint16_t>(plan.SineAngle), false,
                          &sine)) {
    return CameraQuakeFactorStatus::Failure;
  }

  std::array<float, 2> randomSamples{};
  std::uint32_t seed = 0U;
  std::uint32_t scratchBits = 0U;
  if (plan.RandomSampleCount != 0U) {
    if (plan.RandomSampleCount > randomSamples.size() ||
        !memory.ReadFast(kCameraQuakeRandomStateAddress, &seed) ||
        !memory.IsWritable(kCameraQuakeRandomStateAddress,
                           2U * sizeof(std::uint32_t))) {
      return CameraQuakeFactorStatus::Failure;
    }
    for (std::size_t index = 0U; index < plan.RandomSampleCount; ++index) {
      const auto step = oot3d::gameplay::AdvanceCameraQuakeRandom(seed);
      seed = step.Seed;
      scratchBits = step.ScratchBits;
      randomSamples[index] = step.Value;
    }
  }

  *factors = oot3d::gameplay::ResolveCameraQuakeFactors(
      plan, sine, randomSamples);
  if (!factors->Supported) {
    return CameraQuakeFactorStatus::Failure;
  }
  if (plan.RandomSampleCount != 0U &&
      (!memory.WriteFast(kCameraQuakeRandomStateAddress, seed) ||
       !memory.WriteFast(kCameraQuakeRandomStateAddress +
                             sizeof(std::uint32_t),
                         scratchBits))) {
    return CameraQuakeFactorStatus::Failure;
  }
  cache.RequestId = request.RequestId;
  cache.InitialCountdown = request.InitialCountdown;
  cache.CountdownAfter = plan.CountdownAfter;
  cache.CameraAddress = request.CameraAddress;
  cache.Callback = plan.Callback;
  cache.Factors = *factors;
  cache.Valid = true;
  gStats.CameraQuakeRandomSamples += plan.RandomSampleCount;
  return CameraQuakeFactorStatus::Ready;
}

bool ExecuteCameraQuakeCallback(
    std::uint32_t entry, oot3d::recomp::a32::GuestState &state,
    NativeA32Memory &memory, oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  const CameraQuakeCallback callback = CameraQuakeCallbackForEntry(entry);
  std::size_t slot = 0U;
  CameraQuakeRequestWire request;
  if (context.Time == nullptr || callback == CameraQuakeCallback::None ||
      !ResolveCameraQuakeSlot(state.r[0], &slot) || state.r[1] == 0U ||
      !memory.IsWritable(state.r[1], 0x20U) ||
      !ReadWireObject(memory, state.r[0], &request) ||
      request.CallbackIndex != static_cast<std::uint8_t>(callback)) {
    ++gStats.CameraQuakeFailures;
    return FailRead();
  }

  const auto plan =
      oot3d::gameplay::ResolveCameraQuakeSignal(request, *context.Time);
  if (!plan.Supported) {
    ++gStats.CameraQuakeFailures;
    return FailRead();
  }
  const std::uint32_t countdownAddress =
      state.r[0] +
      static_cast<std::uint32_t>(
          offsetof(CameraQuakeRequestWire, Countdown));
  if (plan.CountdownMutated &&
      !memory.IsWritable(countdownAddress, sizeof(std::int16_t))) {
    ++gStats.CameraQuakeFailures;
    return FailWrite();
  }

  oot3d::gameplay::CameraQuakeFactors factors;
  const CameraQuakeFactorStatus factorStatus =
      plan.Evaluate
          ? ResolveCameraQuakeFactorsForTick(
                plan, request, slot, *context.Time, memory, &factors)
          : CameraQuakeFactorStatus::Ready;
  if (factorStatus == CameraQuakeFactorStatus::Failure) {
    ++gStats.CameraQuakeFailures;
    return FailRead();
  }

  if (plan.CountdownMutated &&
      !WriteWireObject(memory, countdownAddress, plan.CountdownAfter)) {
    ++gStats.CameraQuakeFailures;
    return FailWrite();
  }

  gCameraQuakePendingReturn = {
      .RequestAddress = state.r[0],
      .ReturnValue = plan.ReturnValue,
      .Active = true,
  };
  ++gStats.Calls;
  ++gStats.CameraCalls;
  ++gStats.CameraQuakeCallbackCalls;
  if (plan.CountdownMutated) {
    ++gStats.CameraQuakeLogicalAdvances;
  } else if (plan.Evaluate && !context.Time->CrossedLogicalFrame) {
    ++gStats.CameraQuakeIntermediateHolds;
  }

  if (!plan.Evaluate || factorStatus == CameraQuakeFactorStatus::CacheMiss) {
    return CompleteCameraQuakeCallbackReturn(state, result, blocksConsumed);
  }

  state.vfp[0] = std::bit_cast<std::uint32_t>(factors.Primary);
  state.vfp[1] = std::bit_cast<std::uint32_t>(factors.Secondary);
  state.r[14] = kOot3dCameraQuakeCallbackReturnBoundary;
  state.r[15] = kCameraQuakeComposeHelperEntry;
  *result = {
      oot3d::recomp::a32::ExitKind::Branch,
      kCameraQuakeComposeHelperEntry,
      oot3d::recomp::a32::FallbackReason::None,
      entry,
  };
  if (blocksConsumed != nullptr) {
    *blocksConsumed = 1U;
  }
  ++gStats.CameraQuakeHelperDispatches;
  return true;
}

bool ReadActor(const NativeA32Memory &memory, std::uint32_t address,
               Actor *actor) {
  return address != 0U && ReadWireObject(memory, address, actor);
}

template <typename T>
bool ReadField(const NativeA32Memory &memory, std::uint32_t base,
               std::size_t offset, T *value) {
  std::uint32_t address = 0U;
  return base != 0U && CheckedAddress(base, offset, &address) &&
         ReadWireObject(memory, address, value);
}

template <typename T>
bool WriteField(NativeA32Memory &memory, std::uint32_t base,
                std::size_t offset, const T &value) {
  std::uint32_t address = 0U;
  return base != 0U && CheckedAddress(base, offset, &address) &&
         memory.IsWritable(address, sizeof(T)) &&
         WriteWireObject(memory, address, value);
}

template <typename T>
bool CanWriteField(const NativeA32Memory &memory, std::uint32_t base,
                   std::size_t offset) {
  std::uint32_t address = 0U;
  return base != 0U && CheckedAddress(base, offset, &address) &&
         memory.IsWritable(address, sizeof(T));
}

bool ReadSignedByteField(const NativeA32Memory &memory, std::uint32_t base,
                         std::size_t offset, std::int8_t *value) {
  std::uint8_t bits = 0U;
  if (value == nullptr || !ReadField(memory, base, offset, &bits)) {
    return false;
  }
  *value = std::bit_cast<std::int8_t>(bits);
  return true;
}

bool ReadPlayerFromPlay(const NativeA32Memory &memory,
                        std::uint32_t playAddress,
                        std::uint32_t *playerAddress) {
  GuestPtr<PlayerWireState> player;
  if (playerAddress == nullptr ||
      !ReadField(memory, playAddress, offsetof(PlayerPlayStateWire, Player),
                 &player) ||
      player.Address == 0U) {
    return false;
  }
  *playerAddress = player.Address;
  return true;
}

SkelAnimePlaybackState
DecodeSkelAnimePlayback(const SkelAnime &skelAnime) noexcept {
  return {
      skelAnime.MorphWeight,     skelAnime.MorphRate,  skelAnime.CurrentFrame,
      skelAnime.PlaySpeed,       skelAnime.StartFrame, skelAnime.EndFrame,
      skelAnime.AnimationLength, skelAnime.MorphTaper, skelAnime.AnimationMode,
      skelAnime.UpdateMode,
  };
}

AnimationChangeState
DecodeAnimationChangeState(const SkelAnime &skelAnime) noexcept {
  return {
      skelAnime.AnimationIndex, skelAnime.MorphWeight,     skelAnime.MorphRate,
      skelAnime.CurrentFrame,   skelAnime.PlaySpeed,       skelAnime.StartFrame,
      skelAnime.EndFrame,       skelAnime.AnimationLength, skelAnime.MorphTaper,
      skelAnime.AnimationMode,  skelAnime.UpdateMode,
  };
}

void EncodeAnimationChangeState(SkelAnime &skelAnime,
                                const AnimationChangeState &state) noexcept {
  skelAnime.AnimationIndex = state.AnimationIndex;
  skelAnime.MorphWeight = state.MorphWeight;
  skelAnime.MorphRate = state.MorphRate;
  skelAnime.CurrentFrame = state.CurrentFrame;
  skelAnime.PlaySpeed = state.PlaySpeed;
  skelAnime.StartFrame = state.StartFrame;
  skelAnime.EndFrame = state.EndFrame;
  skelAnime.AnimationLength = state.AnimationLength;
  skelAnime.MorphTaper = state.MorphTaper;
  skelAnime.AnimationMode = state.AnimationMode;
  skelAnime.UpdateMode = state.UpdateMode;
}

bool ReadSkelAnimeConstants(const NativeA32Memory &memory,
                            SkelAnimeUpdateConstants *constants) {
  return constants != nullptr &&
         ReadFloat(memory, kSkelAnimeLegacyScaleLiteral,
                   &constants->LegacyUpdateScale) &&
         ReadFloat(memory, kSkelAnimeDirectScaleLiteral,
                   &constants->DirectUpdateScale) &&
         ReadFloat(memory, kSkelAnimeTaperAngleScaleLiteral,
                   &constants->TaperAngleScale) &&
         ReadFloat(memory, kSkelAnimeTaperDirectScaleLiteral,
                   &constants->TaperUpdateScale) &&
         ReadFloat(memory, kSkelAnimeTaperZeroLiteral, &constants->TaperZero) &&
         ReadFloat(memory, kSkelAnimeTaperOneLiteral, &constants->TaperOne) &&
         ReadFloat(memory, kSkelAnimeZeroLiteral, &constants->Zero) &&
         ReadFloat(memory, kSkelAnimeOneLiteral, &constants->One);
}

bool CanCommitSkelAnimePlayback(const NativeA32Memory &memory,
                                std::uint32_t address,
                                const SkelAnimeUpdatePlan &plan) {
  const auto writable = [&](std::size_t offset, std::size_t size) {
    return memory.IsWritable(address + static_cast<std::uint32_t>(offset),
                             size);
  };
  return (!plan.WritesMorphWeight ||
          writable(offsetof(SkelAnime, MorphWeight), sizeof(float))) &&
         (!plan.WritesCurrentFrame ||
          writable(offsetof(SkelAnime, CurrentFrame), sizeof(float))) &&
         (!plan.WritesUpdateMode ||
          writable(offsetof(SkelAnime, UpdateMode), sizeof(std::uint8_t)));
}

bool CommitSkelAnimePlayback(NativeA32Memory &memory, std::uint32_t address,
                             const SkelAnimePlaybackState &playback,
                             const SkelAnimeUpdatePlan &plan) {
  if (plan.WritesMorphWeight &&
      !WriteWireObject(memory,
                       address + static_cast<std::uint32_t>(
                                     offsetof(SkelAnime, MorphWeight)),
                       playback.MorphWeight)) {
    return false;
  }
  if (plan.WritesCurrentFrame &&
      !WriteWireObject(memory,
                       address + static_cast<std::uint32_t>(
                                     offsetof(SkelAnime, CurrentFrame)),
                       playback.CurrentFrame)) {
    return false;
  }
  return !plan.WritesUpdateMode ||
         WriteWireObject(memory,
                         address + static_cast<std::uint32_t>(
                                       offsetof(SkelAnime, UpdateMode)),
                         playback.UpdateMode);
}

bool PrepareSkelAnimeTailFrame(NativeA32Memory &memory,
                               oot3d::recomp::a32::GuestState &state,
                               std::uint32_t skelAnimeAddress,
                               std::uint32_t playAddress) {
  constexpr std::uint32_t frameSize = 40U;
  const std::uint32_t originalSp = state.r[13];
  if (originalSp < frameSize ||
      !memory.IsWritable(originalSp - frameSize, frameSize)) {
    return false;
  }

  const std::uint32_t frameSp = originalSp - frameSize;
  const bool wroteFrame = memory.WriteFast(originalSp - 16U, state.r[4]) &&
                          memory.WriteFast(originalSp - 12U, state.r[5]) &&
                          memory.WriteFast(originalSp - 8U, state.r[6]) &&
                          memory.WriteFast(originalSp - 4U, state.r[14]) &&
                          memory.WriteFast(originalSp - 32U, state.vfp[16]) &&
                          memory.WriteFast(originalSp - 28U, state.vfp[17]) &&
                          memory.WriteFast(originalSp - 24U, state.vfp[18]) &&
                          memory.WriteFast(originalSp - 20U, state.vfp[19]);
  if (!wroteFrame) {
    return false;
  }

  state.r[0] = playAddress;
  // Direct update modes reach their shared sampling tails after the original
  // body has materialized SkelAnime::animation at r4 + 0x30.  The incoming r1
  // is a separate caller argument and is not a valid substitute.
  state.r[1] = skelAnimeAddress + 0x30U;
  state.r[4] = skelAnimeAddress;
  state.r[5] = 0U;
  state.r[6] = 1U;
  state.r[13] = frameSp;
  return true;
}

bool PrepareTaperedMorphTailFrame(NativeA32Memory &memory,
                                  oot3d::recomp::a32::GuestState &state,
                                  std::uint32_t skelAnimeAddress) {
  constexpr std::uint32_t frameSize = 32U;
  const std::uint32_t originalSp = state.r[13];
  if (originalSp < frameSize ||
      !memory.IsWritable(originalSp - frameSize, frameSize)) {
    return false;
  }

  const std::uint32_t frameSp = originalSp - frameSize;
  const bool wroteFrame = memory.WriteFast(originalSp - 12U, state.r[4]) &&
                          memory.WriteFast(originalSp - 8U, state.r[5]) &&
                          memory.WriteFast(originalSp - 4U, state.r[14]) &&
                          memory.WriteFast(originalSp - 28U, state.vfp[16]) &&
                          memory.WriteFast(originalSp - 24U, state.vfp[17]) &&
                          memory.WriteFast(originalSp - 20U, state.vfp[18]) &&
                          memory.WriteFast(originalSp - 16U, state.vfp[19]);
  if (!wroteFrame) {
    return false;
  }

  state.r[4] = skelAnimeAddress;
  state.r[13] = frameSp;
  return true;
}

bool BranchToTypedAnimationTail(std::uint32_t entry, std::uint32_t tail,
                                oot3d::recomp::a32::GuestState &state,
                                oot3d::recomp::a32::ExecutionResult *result,
                                std::uint32_t *blocksConsumed) {
  state.r[15] = tail;
  *result = {
      oot3d::recomp::a32::ExitKind::Branch,
      tail,
      oot3d::recomp::a32::FallbackReason::None,
      entry,
  };
  if (blocksConsumed != nullptr) {
    *blocksConsumed = 1U;
  }
  ++gStats.Calls;
  ++gStats.AnimationCalls;
  return true;
}

bool PrepareAnimationChangeEffectFrame(NativeA32Memory &memory,
                                       oot3d::recomp::a32::GuestState &state) {
  constexpr std::uint32_t frameSize = 48U;
  const std::uint32_t originalSp = state.r[13];
  if (originalSp < frameSize ||
      !memory.IsWritable(originalSp - frameSize, frameSize)) {
    return false;
  }

  const std::uint32_t frameSp = originalSp - frameSize;
  for (std::size_t index = 0U; index < 6U; ++index) {
    if (!memory.WriteFast(frameSp + static_cast<std::uint32_t>(index) * 4U,
                          state.vfp[16U + index])) {
      return false;
    }
  }
  for (std::size_t index = 0U; index < 5U; ++index) {
    if (!memory.WriteFast(frameSp + 24U +
                              static_cast<std::uint32_t>(index) * 4U,
                          state.r[4U + index])) {
      return false;
    }
  }
  if (!memory.WriteFast(frameSp + 44U, state.r[14])) {
    return false;
  }

  state.r[13] = frameSp;
  state.r[14] = kAnimationChangeEpilogue;
  return true;
}

bool PrepareLinkAnimationEffectFrame(NativeA32Memory &memory,
                                     oot3d::recomp::a32::GuestState &state) {
  constexpr std::uint32_t frameSize = 72U;
  const std::uint32_t originalSp = state.r[13];
  if (originalSp < frameSize ||
      !memory.IsWritable(originalSp - frameSize, frameSize)) {
    return false;
  }

  const std::uint32_t frameSp = originalSp - frameSize;
  for (std::size_t index = 0U; index < 6U; ++index) {
    if (!memory.WriteFast(frameSp + 16U +
                              static_cast<std::uint32_t>(index) * 4U,
                          state.vfp[16U + index])) {
      return false;
    }
  }
  for (std::size_t index = 0U; index < 7U; ++index) {
    if (!memory.WriteFast(frameSp + 40U +
                              static_cast<std::uint32_t>(index) * 4U,
                          state.r[4U + index])) {
      return false;
    }
  }
  if (!memory.WriteFast(frameSp + 68U, state.r[14])) {
    return false;
  }

  state.r[13] = frameSp;
  return true;
}

bool CanAccessAnimationPose(const NativeA32Memory &memory,
                            const SkelAnime &skelAnime,
                            AnimationChangePoseEffect effect) {
  const std::size_t poseBytes =
      static_cast<std::size_t>(skelAnime.LimbCount) * kAnimationTransformSize;
  if (poseBytes == 0U) {
    return true;
  }
  if (effect == AnimationChangePoseEffect::SampleJointPose) {
    return memory.IsWritable(skelAnime.JointMatrices.Address, poseBytes);
  }
  if (effect == AnimationChangePoseEffect::SampleMorphPose) {
    return memory.IsWritable(skelAnime.MorphMatrices.Address, poseBytes);
  }
  return memory.IsMapped(skelAnime.JointMatrices.Address, poseBytes) &&
         memory.IsWritable(skelAnime.MorphMatrices.Address, poseBytes);
}

bool CanAccessLinkAnimationPose(const NativeA32Memory &memory,
                                const SkelAnime &skelAnime,
                                LinkAnimationChangePoseEffect effect) {
  switch (effect) {
  case LinkAnimationChangePoseEffect::QueueJointPose:
    return CanAccessAnimationPose(memory, skelAnime,
                                  AnimationChangePoseEffect::SampleJointPose);
  case LinkAnimationChangePoseEffect::QueueMorphPose:
    return CanAccessAnimationPose(memory, skelAnime,
                                  AnimationChangePoseEffect::SampleMorphPose);
  case LinkAnimationChangePoseEffect::CopyJointPoseToMorph:
    return CanAccessAnimationPose(
        memory, skelAnime, AnimationChangePoseEffect::CopyJointPoseToMorph);
  }
  return false;
}

bool CommitAnimationChangeState(NativeA32Memory &memory,
                                std::uint32_t skelAnimeAddress,
                                const SkelAnime &skelAnime) {
  constexpr std::size_t playbackOffset = offsetof(SkelAnime, AnimationIndex);
  constexpr std::size_t playbackSize =
      offsetof(SkelAnime, InitFlags) - playbackOffset;
  constexpr std::size_t modeOffset = offsetof(SkelAnime, AnimationMode);
  constexpr std::size_t modeSize = 2U;
  const std::uint32_t playbackAddress =
      skelAnimeAddress + static_cast<std::uint32_t>(playbackOffset);
  const std::uint32_t modeAddress =
      skelAnimeAddress + static_cast<std::uint32_t>(modeOffset);
  if (!memory.IsWritable(playbackAddress, playbackSize) ||
      !memory.IsWritable(modeAddress, modeSize)) {
    return false;
  }
  const auto *bytes = reinterpret_cast<const std::uint8_t *>(&skelAnime);
  return memory.WriteBytes(playbackAddress,
                           std::span<const std::uint8_t>(bytes + playbackOffset,
                                                         playbackSize)) &&
         memory.WriteBytes(modeAddress, std::span<const std::uint8_t>(
                                            bytes + modeOffset, modeSize));
}

bool ExecuteSkelAnimeSetUpdate(oot3d::recomp::a32::GuestState &state,
                               NativeA32Memory &memory,
                               oot3d::recomp::a32::ExecutionResult *result,
                               std::uint32_t *blocksConsumed) {
  std::uint8_t animationMode = 0U;
  const std::uint32_t updateModeAddress =
      state.r[0] + static_cast<std::uint32_t>(offsetof(SkelAnime, UpdateMode));
  if (state.r[0] == 0U ||
      !memory.ReadFast(state.r[0] + static_cast<std::uint32_t>(
                                        offsetof(SkelAnime, AnimationMode)),
                       &animationMode)) {
    return FailRead();
  }
  const std::uint8_t updateMode =
      oot3d::gameplay::ResolveSkelAnimeUpdateMode(animationMode);
  if (!memory.WriteFast(updateModeAddress, updateMode)) {
    return FailWrite();
  }
  ++gStats.AnimationCalls;
  return CompleteCall(kOot3dSkelAnimeSetUpdateEntry, state, result,
                      blocksConsumed);
}

bool ExecuteZarGetCsabByIndex(oot3d::recomp::a32::GuestState &state,
                              NativeA32Memory &memory,
                              oot3d::recomp::a32::ExecutionResult *result,
                              std::uint32_t *blocksConsumed) {
  std::uint32_t csabAddress = 0U;
  if (!ReadCsabByIndex(memory, state.r[0], state.r[1], &csabAddress)) {
    return FailRead();
  }
  state.r[0] = csabAddress;
  ++gStats.AnimationCalls;
  return CompleteCall(kOot3dZarGetCsabByIndexEntry, state, result,
                      blocksConsumed);
}

bool ExecuteAnimationGetLength(oot3d::recomp::a32::GuestState &state,
                               NativeA32Memory &memory,
                               oot3d::recomp::a32::ExecutionResult *result,
                               std::uint32_t *blocksConsumed) {
  std::int32_t frameCount = 0;
  if (!ReadAnimationFrameCount(memory, state.r[0], state.r[1], &frameCount)) {
    return FailRead();
  }
  state.r[0] = std::bit_cast<std::uint32_t>(frameCount);
  ++gStats.AnimationCalls;
  return CompleteCall(kOot3dAnimationGetLengthEntry, state, result,
                      blocksConsumed);
}

bool ExecuteAnimationChange(oot3d::recomp::a32::GuestState &state,
                            NativeA32Memory &memory,
                            oot3d::recomp::a32::ExecutionResult *result,
                            std::uint32_t *blocksConsumed) {
  const auto originalState = state;
  const std::uint32_t skelAnimeAddress = state.r[0];
  SkelAnime skelAnime;
  std::int32_t animationFrameCount = 0;
  AnimationChangeConstants constants;
  if (skelAnimeAddress == 0U ||
      !ReadWireObject(memory, skelAnimeAddress, &skelAnime) ||
      !ReadAnimationFrameCount(memory, skelAnimeAddress, state.r[1],
                               &animationFrameCount) ||
      !ReadFloat(memory, kAnimationChangeZeroLiteral, &constants.Zero) ||
      !ReadFloat(memory, kAnimationChangeOneLiteral, &constants.One)) {
    return FailRead();
  }

  AnimationChangeState changeState = DecodeAnimationChangeState(skelAnime);
  const AnimationChangeRequest request{
      .AnimationIndex = std::bit_cast<std::int32_t>(state.r[1]),
      .PlaySpeed = std::bit_cast<float>(state.vfp[0]),
      .StartFrame = std::bit_cast<float>(state.vfp[1]),
      .EndFrame = std::bit_cast<float>(state.vfp[2]),
      .MorphFrames = std::bit_cast<float>(state.vfp[3]),
      .AnimationFrameCount = animationFrameCount,
      .MorphTaper =
          std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(state.r[3])),
      .AnimationMode = static_cast<std::uint8_t>(state.r[2]),
  };
  const AnimationChangePlan plan =
      oot3d::gameplay::ApplyAnimationChange(changeState, request, constants);
  if (!plan.Supported) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }
  EncodeAnimationChangeState(skelAnime, changeState);
  if (!CanAccessAnimationPose(memory, skelAnime, plan.PoseEffect) ||
      !PrepareAnimationChangeEffectFrame(memory, state)) {
    state = originalState;
    return FailWrite();
  }
  if (!CommitAnimationChangeState(memory, skelAnimeAddress, skelAnime)) {
    state = originalState;
    return FailWrite();
  }

  std::uint32_t effectEntry = kSkelAnimeGetFrameDataEntry;
  state.r[0] = skelAnimeAddress;
  state.r[4] = skelAnimeAddress +
               static_cast<std::uint32_t>(offsetof(SkelAnime, AnimationIndex));
  state.vfp[20] = std::bit_cast<std::uint32_t>(request.PlaySpeed);
  if (plan.PoseEffect == AnimationChangePoseEffect::CopyJointPoseToMorph) {
    effectEntry = kSkelAnimeCopyFrameTableEntry;
    state.r[1] = skelAnime.MorphMatrices.Address;
    state.r[2] = skelAnime.JointMatrices.Address;
  } else {
    state.r[1] = std::bit_cast<std::uint32_t>(request.AnimationIndex);
    state.r[2] = skelAnime.LimbCount;
    state.r[3] = plan.PoseEffect == AnimationChangePoseEffect::SampleJointPose
                     ? skelAnime.JointMatrices.Address
                     : skelAnime.MorphMatrices.Address;
    state.vfp[0] = std::bit_cast<std::uint32_t>(plan.SampleFrame);
  }
  return BranchToTypedAnimationTail(kOot3dAnimationChangeEntry, effectEntry,
                                    state, result, blocksConsumed);
}

bool ExecuteLinkAnimationPlay(std::uint32_t pc,
                              oot3d::recomp::a32::GuestState &state,
                              NativeA32Memory &memory,
                              oot3d::recomp::a32::ExecutionResult *result,
                              std::uint32_t *blocksConsumed) {
  std::uint32_t zeroLiteral = 0U;
  std::uint32_t oneLiteral = 0U;
  std::uint8_t animationMode = 0U;
  bool fixedPlaySpeed = false;
  switch (pc) {
  case kOot3dLinkAnimationPlayOnceWithSpeedEntry:
    zeroLiteral = kLinkAnimationPlayOnceWithSpeedZeroLiteral;
    break;
  case kOot3dLinkAnimationPlayLoopSetSpeedEntry:
    zeroLiteral = kLinkAnimationPlayLoopSetSpeedZeroLiteral;
    animationMode = 2U;
    break;
  case kOot3dLinkAnimationPlayOnceEntry:
    zeroLiteral = kLinkAnimationPlayOnceZeroLiteral;
    oneLiteral = kLinkAnimationPlayOnceOneLiteral;
    fixedPlaySpeed = true;
    break;
  case kOot3dLinkAnimationPlayLoopEntry:
    zeroLiteral = kLinkAnimationPlayLoopZeroLiteral;
    oneLiteral = kLinkAnimationPlayLoopOneLiteral;
    animationMode = 2U;
    fixedPlaySpeed = true;
    break;
  default:
    return false;
  }

  std::int32_t animationFrameCount = 0;
  float zero = 0.0f;
  float playSpeed = std::bit_cast<float>(state.vfp[0]);
  if (state.r[0] == 0U ||
      !ReadAnimationFrameCount(memory, state.r[0], state.r[2],
                               &animationFrameCount) ||
      !ReadFloat(memory, zeroLiteral, &zero) || zero != 0.0f) {
    return FailRead();
  }
  if (fixedPlaySpeed && !ReadFloat(memory, oneLiteral, &playSpeed)) {
    return FailRead();
  }
  if (!std::isfinite(playSpeed)) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  state.r[3] = animationMode;
  state.vfp[0] = std::bit_cast<std::uint32_t>(playSpeed);
  state.vfp[1] = std::bit_cast<std::uint32_t>(zero);
  state.vfp[2] =
      std::bit_cast<std::uint32_t>(static_cast<float>(animationFrameCount));
  state.vfp[3] = std::bit_cast<std::uint32_t>(zero);
  return BranchToTypedAnimationTail(pc, kOot3dLinkAnimationChangeEntry, state,
                                    result, blocksConsumed);
}

bool ExecuteLinkAnimationOnFrame(oot3d::recomp::a32::GuestState &state,
                                 NativeA32Memory &memory,
                                 oot3d::recomp::a32::ExecutionResult *result,
                                 const Oot3dTypedGameplayContext &context,
                                 std::uint32_t *blocksConsumed) {
  SkelAnime skelAnime;
  AnimationOnFrameConstants constants;
  if (state.r[0] == 0U || !ReadWireObject(memory, state.r[0], &skelAnime) ||
      !ReadFloat(memory, kLinkAnimationOnFrameHalfLiteral, &constants.Half) ||
      !ReadFloat(memory, kLinkAnimationOnFrameZeroLiteral, &constants.Zero) ||
      !ReadFloat(memory, kLinkAnimationOnFrameQuantizeScaleLiteral,
                 &constants.QuantizeScale) ||
      !ReadFloat(memory, kLinkAnimationOnFrameQuantizeInverseLiteral,
                 &constants.QuantizeInverse)) {
    return FailRead();
  }

  bool onFrame = false;
  try {
    onFrame = oot3d::gameplay::IsAnimationOnFrame(
        skelAnime.CurrentFrame, skelAnime.PlaySpeed, skelAnime.AnimationLength,
        std::bit_cast<float>(state.vfp[0]), context.NativeUpdateRate,
        constants);
  } catch (...) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }
  state.r[0] = onFrame ? 1U : 0U;
  ++gStats.AnimationCalls;
  return CompleteCall(kOot3dLinkAnimationOnFrameEntry, state, result,
                      blocksConsumed);
}

bool ExecuteAnimationFrameCrossing(std::uint32_t entry,
                                   oot3d::recomp::a32::GuestState &state,
                                   NativeA32Memory &memory,
                                   oot3d::recomp::a32::ExecutionResult *result,
                                   std::uint32_t *blocksConsumed) {
  SkelAnime skelAnime;
  AnimationFrameCrossingConstants constants;
  const std::uint32_t zeroLiteral = entry == kOot3dSkelAnimeIsFrameCrossedEntry
                                        ? kSkelAnimeFrameCrossingZeroLiteral
                                        : kAnimationOnFrameImplZeroLiteral;
  if (state.r[0] == 0U || !ReadWireObject(memory, state.r[0], &skelAnime) ||
      !ReadFloat(memory, zeroLiteral, &constants.Zero)) {
    return FailRead();
  }

  bool crossed = false;
  try {
    crossed = oot3d::gameplay::IsAnimationFrameCrossed(
        skelAnime.CurrentFrame, skelAnime.PlaySpeed, skelAnime.AnimationLength,
        std::bit_cast<float>(state.vfp[0]), std::bit_cast<float>(state.vfp[1]),
        constants);
  } catch (...) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }
  state.r[0] = crossed ? 1U : 0U;
  ++gStats.AnimationCalls;
  return CompleteCall(entry, state, result, blocksConsumed);
}

bool ExecuteLinkAnimationChange(oot3d::recomp::a32::GuestState &state,
                                NativeA32Memory &memory,
                                oot3d::recomp::a32::ExecutionResult *result,
                                std::uint32_t *blocksConsumed) {
  const auto originalState = state;
  const std::uint32_t skelAnimeAddress = state.r[0];
  const std::uint32_t playAddress = state.r[1];
  SkelAnime skelAnime;
  std::uint32_t csabAddress = 0U;
  std::uint32_t animationContextAddress = 0U;
  std::int32_t animationFrameCount = 0;
  AnimationChangeConstants constants;
  if (skelAnimeAddress == 0U || playAddress == 0U ||
      !ReadWireObject(memory, skelAnimeAddress, &skelAnime) ||
      !ReadLinkAnimationResource(memory, skelAnime, state.r[2], &csabAddress,
                                 &animationFrameCount) ||
      !CheckedAddress(playAddress, 0x3410U, &animationContextAddress) ||
      !ReadFloat(memory, kLinkAnimationChangeZeroLiteral, &constants.Zero) ||
      !ReadFloat(memory, kLinkAnimationChangeOneLiteral, &constants.One)) {
    return FailRead();
  }

  AnimationChangeState changeState = DecodeAnimationChangeState(skelAnime);
  const std::uint32_t previousAnimationIndex =
      std::bit_cast<std::uint32_t>(changeState.AnimationIndex);
  const std::uint32_t animationEntryS0 =
      changeState.AnimationIndex == std::bit_cast<std::int32_t>(state.r[2])
          ? std::bit_cast<std::uint32_t>(changeState.CurrentFrame)
          : state.vfp[0];
  const AnimationChangeRequest request{
      .AnimationIndex = std::bit_cast<std::int32_t>(state.r[2]),
      .PlaySpeed = std::bit_cast<float>(state.vfp[0]),
      .StartFrame = std::bit_cast<float>(state.vfp[1]),
      .EndFrame = std::bit_cast<float>(state.vfp[2]),
      .MorphFrames = std::bit_cast<float>(state.vfp[3]),
      .AnimationFrameCount = animationFrameCount,
      .AnimationMode = static_cast<std::uint8_t>(state.r[3]),
  };
  const LinkAnimationChangePlan plan =
      oot3d::gameplay::ApplyLinkAnimationChange(changeState, request,
                                                constants);
  if (!plan.Supported) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }
  EncodeAnimationChangeState(skelAnime, changeState);
  if (!CanAccessLinkAnimationPose(memory, skelAnime, plan.PoseEffect) ||
      !PrepareLinkAnimationEffectFrame(memory, state)) {
    state = originalState;
    return FailWrite();
  }
  if (!CommitAnimationChangeState(memory, skelAnimeAddress, skelAnime)) {
    state = originalState;
    return FailWrite();
  }

  state.r[4] = skelAnimeAddress;
  state.r[5] = playAddress;
  state.r[6] = request.AnimationMode;
  state.r[1] = 1U;
  state.r[2] = 2U;
  state.r[3] = previousAnimationIndex;
  state.r[8] = csabAddress;
  state.r[9] = std::bit_cast<std::uint32_t>(request.AnimationIndex);
  state.vfp[16] = std::bit_cast<std::uint32_t>(request.MorphFrames);
  state.vfp[17] = std::bit_cast<std::uint32_t>(request.StartFrame);
  state.vfp[18] = std::bit_cast<std::uint32_t>(request.PlaySpeed);
  state.vfp[19] = std::bit_cast<std::uint32_t>(request.EndFrame);
  state.vfp[20] = std::bit_cast<std::uint32_t>(constants.Zero);
  state.vfp[0] = animationEntryS0;

  std::uint32_t effectEntry = kLinkAnimationQueueJointTail;
  if (plan.PoseEffect == LinkAnimationChangePoseEffect::QueueMorphPose) {
    effectEntry = kLinkAnimationQueueMorphTail;
    state.r[0] = animationContextAddress;
  } else if (plan.PoseEffect ==
             LinkAnimationChangePoseEffect::CopyJointPoseToMorph) {
    effectEntry = kSkelAnimeCopyFrameTableEntry;
    state.r[0] = skelAnimeAddress;
    state.r[1] = skelAnime.MorphMatrices.Address;
    state.r[2] = skelAnime.JointMatrices.Address;
    state.r[14] = kLinkAnimationCommonTail;
  } else {
    state.r[0] = animationContextAddress;
  }
  return BranchToTypedAnimationTail(kOot3dLinkAnimationChangeEntry, effectEntry,
                                    state, result, blocksConsumed);
}

bool ExecuteSkelAnimeMorphBoundary(std::uint32_t pc,
                                   oot3d::recomp::a32::GuestState &state,
                                   NativeA32Memory &memory,
                                   oot3d::recomp::a32::ExecutionResult *result,
                                   const Oot3dTypedGameplayContext &context,
                                   std::uint32_t *blocksConsumed) {
  const bool direct = pc == kOot3dSkelAnimeDirectMorphBoundary;
  const std::uint32_t skelAnimeAddress = state.r[4];
  if (skelAnimeAddress == 0U ||
      (direct && state.r[5] != skelAnimeAddress + 0x30U)) {
    return FailRead();
  }

  float morphWeight = 0.0f;
  float morphRate = 0.0f;
  float updateScale = 0.0f;
  float zero = 0.0f;
  const std::uint32_t morphWeightAddress =
      skelAnimeAddress +
      static_cast<std::uint32_t>(offsetof(SkelAnime, MorphWeight));
  const std::uint32_t morphRateAddress =
      skelAnimeAddress +
      static_cast<std::uint32_t>(offsetof(SkelAnime, MorphRate));
  if (!ReadFloat(memory, morphWeightAddress, &morphWeight) ||
      !ReadFloat(memory, morphRateAddress, &morphRate) ||
      !ReadFloat(memory,
                 direct ? kDirectMorphScaleLiteral : kLegacyMorphScaleLiteral,
                 &updateScale) ||
      !ReadFloat(memory,
                 direct ? kDirectMorphZeroLiteral : kLegacyMorphZeroLiteral,
                 &zero)) {
    return FailRead();
  }

  const auto step = oot3d::gameplay::AdvanceSkelAnimeMorphWeight(
      morphWeight, morphRate, context.NativeUpdateRate, updateScale, zero);
  if (!step.Supported) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }
  if (step.WritesMorphWeight) {
    if (!memory.IsWritable(morphWeightAddress, sizeof(morphWeight)) ||
        !WriteWireObject(memory, morphWeightAddress, morphWeight)) {
      return FailWrite();
    }
  }

  std::uint32_t tail =
      direct ? kDirectMorphNoBlendTail : kLegacyMorphNoBlendTail;
  if (step.BlendPose) {
    tail = direct ? kDirectMorphBlendTail : kLegacyMorphBlendTail;
    if (direct) {
      state.vfp[0] = std::bit_cast<std::uint32_t>(morphWeight);
    }
  }
  return BranchToTypedAnimationTail(pc, tail, state, result, blocksConsumed);
}

bool ExecuteSkelAnimeUpdate(oot3d::recomp::a32::GuestState &state,
                            NativeA32Memory &memory,
                            oot3d::recomp::a32::ExecutionResult *result,
                            const Oot3dTypedGameplayContext &context,
                            std::uint32_t *blocksConsumed) {
  const auto originalState = state;
  const std::uint32_t skelAnimeAddress = state.r[0];
  const std::uint32_t playAddress = state.r[1];
  SkelAnime skelAnime;
  SkelAnimeUpdateConstants constants;
  if (skelAnimeAddress == 0U ||
      !ReadWireObject(memory, skelAnimeAddress, &skelAnime) ||
      !ReadSkelAnimeConstants(memory, &constants)) {
    return FailRead();
  }

  auto playback = DecodeSkelAnimePlayback(skelAnime);
  const SkelAnimeUpdatePlan plan = oot3d::gameplay::AdvanceSkelAnimePlayback(
      playback, context.NativeUpdateRate, constants);
  if (!plan.Supported) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  if (plan.PoseEffect == SkelAnimePoseEffect::None) {
    state.r[0] = playAddress;
    ++gStats.AnimationCalls;
    return CompleteCall(kOot3dSkelAnimeUpdateEntry, state, result,
                        blocksConsumed);
  }
  if (!CanCommitSkelAnimePlayback(memory, skelAnimeAddress, plan)) {
    return FailWrite();
  }

  std::uint32_t tail = 0U;
  bool prepared = false;
  switch (plan.PoseEffect) {
  case SkelAnimePoseEffect::LegacySample:
    tail = plan.Complete
               ? kSkelAnimeLegacyTerminalTail
               : (skelAnime.UpdateMode == 2U ? kSkelAnimeLegacyOnceSampleTail
                                             : kSkelAnimeLegacySampleTail);
    prepared =
        PrepareSkelAnimeTailFrame(memory, state, skelAnimeAddress, playAddress);
    break;
  case SkelAnimePoseEffect::LegacyBlend:
    tail = kSkelAnimeLegacyBlendTail;
    prepared =
        PrepareSkelAnimeTailFrame(memory, state, skelAnimeAddress, playAddress);
    state.vfp[17] = std::bit_cast<std::uint32_t>(plan.PreviousMorphWeight);
    state.vfp[18] = std::bit_cast<std::uint32_t>(constants.One);
    break;
  case SkelAnimePoseEffect::DirectSample:
    tail = kSkelAnimeDirectSampleTail;
    prepared =
        PrepareSkelAnimeTailFrame(memory, state, skelAnimeAddress, playAddress);
    break;
  case SkelAnimePoseEffect::DirectTerminalSample:
    tail = kSkelAnimeDirectTerminalTail;
    prepared =
        PrepareSkelAnimeTailFrame(memory, state, skelAnimeAddress, playAddress);
    break;
  case SkelAnimePoseEffect::DirectLinearBlend:
    tail = kSkelAnimeDirectLinearBlendTail;
    prepared =
        PrepareSkelAnimeTailFrame(memory, state, skelAnimeAddress, playAddress);
    state.vfp[2] = std::bit_cast<std::uint32_t>(plan.PreviousMorphWeight);
    state.vfp[18] = std::bit_cast<std::uint32_t>(constants.One);
    break;
  case SkelAnimePoseEffect::DirectTaperedBlend: {
    float previousCurve = 0.0f;
    float currentCurve = 0.0f;
    const auto previousAngle =
        static_cast<std::uint16_t>(oot3d::gameplay::ResolveTaperAngle(
            plan.PreviousMorphWeight, constants.TaperAngleScale));
    const auto currentAngle =
        static_cast<std::uint16_t>(oot3d::gameplay::ResolveTaperAngle(
            playback.MorphWeight, constants.TaperAngleScale));
    if (skelAnime.MorphTaper < 0) {
      float previousCos = 0.0f;
      float currentCos = 0.0f;
      if (!ReadAngleComponent(memory, previousAngle, true, &previousCos) ||
          !ReadAngleComponent(memory, currentAngle, true, &currentCos)) {
        state = originalState;
        return FailRead();
      }
      previousCurve = constants.TaperOne - previousCos;
      currentCurve = constants.TaperOne - currentCos;
    } else if (!ReadAngleComponent(memory, previousAngle, false,
                                   &previousCurve) ||
               !ReadAngleComponent(memory, currentAngle, false,
                                   &currentCurve)) {
      state = originalState;
      return FailRead();
    }
    const float ratio = oot3d::gameplay::ResolveTaperedMorphRatio(
        previousCurve, currentCurve, constants.TaperZero);
    tail = kSkelAnimeDirectTaperedBlendTail;
    prepared = PrepareTaperedMorphTailFrame(memory, state, skelAnimeAddress);
    state.vfp[17] = std::bit_cast<std::uint32_t>(ratio);
    state.vfp[18] = std::bit_cast<std::uint32_t>(constants.TaperOne);
    break;
  }
  case SkelAnimePoseEffect::None:
    break;
  }

  if (!prepared) {
    state = originalState;
    return FailWrite();
  }
  if (!CommitSkelAnimePlayback(memory, skelAnimeAddress, playback, plan)) {
    state = originalState;
    return FailWrite();
  }
  return BranchToTypedAnimationTail(kOot3dSkelAnimeUpdateEntry, tail, state,
                                    result, blocksConsumed);
}

bool CompletePlayerCall(std::uint32_t entry,
                        oot3d::recomp::a32::GuestState &state,
                        oot3d::recomp::a32::ExecutionResult *result,
                        std::uint32_t *blocksConsumed) {
  ++gStats.PlayerCalls;
  return CompleteCall(entry, state, result, blocksConsumed);
}

bool ExecutePlayerRespawnDamageAdvanceBlock(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  constexpr std::uint32_t kConditionFlagsMask =
      oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagZ |
      oot3d::recomp::a32::kFlagC | oot3d::recomp::a32::kFlagV;

  const std::uint32_t playerAddress = state.r[4];
  std::uint32_t expectedR6 = 0U;
  if (context.Time == nullptr || playerAddress == 0U ||
      !CheckedAddress(playerAddress, 0x2000U, &expectedR6) ||
      state.r[6] != expectedR6) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  std::int8_t respawnDamageState = 0;
  if (!ReadSignedByteField(
          memory, playerAddress,
          offsetof(PlayerWireState, RespawnDamageState),
          &respawnDamageState)) {
    return FailRead();
  }

  const std::int8_t previousState = respawnDamageState;
  const std::uint32_t previousSignedState =
      static_cast<std::uint32_t>(static_cast<std::int32_t>(previousState));
  if (previousState >= 0 || state.r[0] != previousSignedState) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  const bool dispatchAudio =
      oot3d::gameplay::PlayerAdvanceRespawnDamageState(
          respawnDamageState, *context.Time);
  if (respawnDamageState != previousState &&
      !WriteField(
          memory, playerAddress,
          offsetof(PlayerWireState, RespawnDamageState),
          std::bit_cast<std::uint8_t>(respawnDamageState))) {
    return FailWrite();
  }

  const std::uint32_t resolvedSignedState =
      static_cast<std::uint32_t>(
          static_cast<std::int32_t>(respawnDamageState));
  const std::uint32_t nextPc =
      dispatchAudio ? kOot3dPlayerRespawnDamageAudioBranch
                    : kOot3dPlayerRespawnDamageContinue;
  state.cpsr =
      (state.cpsr & ~kConditionFlagsMask) |
      SubtractConditionFlags(resolvedSignedState, 0U);
  state.r[0] = resolvedSignedState;
  state.r[15] = nextPc;
  *result = {
      oot3d::recomp::a32::ExitKind::Branch,
      nextPc,
      oot3d::recomp::a32::FallbackReason::None,
      kOot3dPlayerRespawnDamageAdvanceBlock,
  };
  if (blocksConsumed != nullptr) {
    *blocksConsumed = 1U;
  }

  ++gStats.Calls;
  ++gStats.PlayerCalls;
  ++gStats.RespawnDamageBlockCalls;
  if (respawnDamageState != previousState) {
    ++gStats.RespawnDamageLogicalAdvances;
  } else {
    ++gStats.RespawnDamageIntermediateHolds;
  }
  if (dispatchAudio) {
    ++gStats.RespawnDamageAudioDispatches;
  }
  return true;
}

bool ExecutePlayerRandomTurnTimerDecrementBlock(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  constexpr std::uint32_t kConditionFlagsMask =
      oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagZ |
      oot3d::recomp::a32::kFlagC | oot3d::recomp::a32::kFlagV;

  if (context.Time == nullptr || state.r[4] == 0U) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }
  std::uint16_t encodedTimer = 0U;
  if (!ReadField(memory, state.r[4], sizeof(std::int16_t),
                 &encodedTimer)) {
    return FailRead();
  }

  std::int16_t timer = std::bit_cast<std::int16_t>(encodedTimer);
  const std::uint32_t signedTimer =
      static_cast<std::uint32_t>(static_cast<std::int32_t>(timer));
  if (timer == 0 || state.r[3] != signedTimer) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  const std::int16_t previousTimer = timer;
  const bool refresh =
      oot3d::gameplay::PlayerAdvanceRandomTurnTimer(timer, *context.Time);
  if (timer != previousTimer &&
      !WriteField(memory, state.r[4], sizeof(std::int16_t),
                  std::bit_cast<std::uint16_t>(timer))) {
    return FailWrite();
  }

  const std::uint32_t resolvedSignedTimer =
      static_cast<std::uint32_t>(static_cast<std::int32_t>(timer));
  const std::uint32_t nextPc =
      refresh ? kOot3dPlayerRandomTurnTimerRefreshBlock
              : kOot3dPlayerRandomTurnTimerContinue;
  state.cpsr =
      (state.cpsr & ~kConditionFlagsMask) |
      SubtractConditionFlags(resolvedSignedTimer, 0U);
  state.r[3] = resolvedSignedTimer;
  state.r[15] = nextPc;
  *result = {
      oot3d::recomp::a32::ExitKind::Branch,
      nextPc,
      oot3d::recomp::a32::FallbackReason::None,
      kOot3dPlayerRandomTurnTimerDecrementBlock,
  };
  if (blocksConsumed != nullptr) {
    *blocksConsumed = 1U;
  }

  ++gStats.Calls;
  ++gStats.PlayerCalls;
  ++gStats.RandomTurnTimerDecrementBlockCalls;
  if (timer != previousTimer) {
    ++gStats.RandomTurnTimerLogicalAdvances;
  } else {
    ++gStats.RandomTurnTimerIntermediateHolds;
  }
  return true;
}

bool ExecutePlayerRandomTurnTimerRefreshBlock(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  if (context.Time == nullptr || state.r[4] == 0U) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }
  std::uint16_t encodedTimer = 0U;
  if (!ReadField(memory, state.r[4], sizeof(std::int16_t),
                 &encodedTimer)) {
    return FailRead();
  }
  if (encodedTimer != 0U || state.r[3] != 0U) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  std::int16_t timer = 0;
  const bool dispatchRng =
      oot3d::gameplay::PlayerAdvanceRandomTurnTimer(timer, *context.Time);
  const std::uint32_t nextPc =
      dispatchRng ? kOot3dPlayerRandomTurnTimerRngEntry
                  : kOot3dPlayerRandomTurnTimerEpilogue;
  if (dispatchRng) {
    state.r[14] = kOot3dPlayerRandomTurnTimerRngReturn;
  }
  state.r[15] = nextPc;
  *result = {
      oot3d::recomp::a32::ExitKind::Branch,
      nextPc,
      oot3d::recomp::a32::FallbackReason::None,
      kOot3dPlayerRandomTurnTimerRefreshBlock,
  };
  if (blocksConsumed != nullptr) {
    *blocksConsumed = 1U;
  }

  ++gStats.Calls;
  ++gStats.PlayerCalls;
  ++gStats.RandomTurnTimerRefreshBlockCalls;
  if (dispatchRng) {
    ++gStats.RandomTurnTimerRngDispatches;
  } else {
    ++gStats.RandomTurnTimerRngIntermediateSuppressions;
  }
  return true;
}

bool ExecutePlayerAttentionPersistenceAdvanceBlock(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  constexpr std::uint32_t kConditionFlagsMask =
      oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagZ |
      oot3d::recomp::a32::kFlagC | oot3d::recomp::a32::kFlagV;

  std::uint32_t expectedR5 = 0U;
  if (context.Time == nullptr || state.r[4] == 0U ||
      !CheckedAddress(state.r[4], 0x1000U, &expectedR5) ||
      state.r[5] != expectedR5) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  std::uint8_t counter = 0U;
  if (!ReadField(memory, state.r[4],
                 offsetof(PlayerWireState, AttentionPersistenceCounter),
                 &counter)) {
    return FailRead();
  }
  const std::uint8_t previousCounter = counter;
  const std::uint8_t comparisonValue =
      context.Time->CrossedLogicalFrame
          ? static_cast<std::uint8_t>(counter + 1U)
          : counter;
  oot3d::gameplay::PlayerAdvanceAttentionPersistenceCounter(
      counter, *context.Time);
  if (counter != previousCounter &&
      !WriteField(
          memory, state.r[4],
          offsetof(PlayerWireState, AttentionPersistenceCounter),
          counter)) {
    return FailWrite();
  }

  state.cpsr =
      (state.cpsr & ~kConditionFlagsMask) |
      SubtractConditionFlags(static_cast<std::uint32_t>(comparisonValue),
                             0xFEU);
  state.r[0] = counter;
  state.r[15] = kOot3dPlayerAttentionPersistenceContinue;
  *result = {
      oot3d::recomp::a32::ExitKind::Branch,
      kOot3dPlayerAttentionPersistenceContinue,
      oot3d::recomp::a32::FallbackReason::None,
      kOot3dPlayerAttentionPersistenceAdvanceBlock,
  };
  if (blocksConsumed != nullptr) {
    *blocksConsumed = 1U;
  }

  ++gStats.Calls;
  ++gStats.PlayerCalls;
  ++gStats.AttentionPersistenceBlockCalls;
  if (!context.Time->CrossedLogicalFrame) {
    ++gStats.AttentionPersistenceIntermediateHolds;
  } else if (counter != previousCounter) {
    ++gStats.AttentionPersistenceLogicalAdvances;
  } else {
    ++gStats.AttentionPersistenceSaturationHolds;
  }
  return true;
}

bool CheckedSignedAddress(std::uint32_t base, std::int32_t offset,
                          std::uint32_t *address) noexcept {
  const std::int64_t result =
      static_cast<std::int64_t>(base) + static_cast<std::int64_t>(offset);
  if (address == nullptr || result < 0 ||
      result > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }
  *address = static_cast<std::uint32_t>(result);
  return true;
}

bool ReadCameraCurveView(const NativeA32Memory &memory,
                         std::uint32_t curveAddress,
                         CameraCurveView *curve) {
  CameraCurveHeaderWire header;
  if (curve == nullptr || curveAddress == 0U ||
      !ReadWireObject(memory, curveAddress, &header)) {
    return false;
  }

  curve->Type = static_cast<CameraCurveType>(header.Type);
  curve->PointCount = header.PointCount;
  curve->LoopEndFrame = header.LoopEndFrame;
  curve->PointBytes = {};

  std::size_t pointSize = 0U;
  switch (curve->Type) {
  case CameraCurveType::Linear:
  case CameraCurveType::Step:
    pointSize = sizeof(oot3d::gameplay::CameraLinearKeyframeWire);
    break;
  case CameraCurveType::Hermite:
    pointSize = sizeof(oot3d::gameplay::CameraHermiteKeyframeWire);
    break;
  default:
    return true;
  }
  if (header.PointCount <= 0) {
    return true;
  }

  const std::uint64_t byteCount64 =
      static_cast<std::uint64_t>(header.PointCount) * pointSize;
  if (byteCount64 > std::numeric_limits<std::size_t>::max()) {
    return false;
  }
  std::uint32_t pointsAddress = 0U;
  if (!CheckedAddress(curveAddress, sizeof(CameraCurveHeaderWire),
                      &pointsAddress)) {
    return false;
  }
  const std::size_t byteCount = static_cast<std::size_t>(byteCount64);
  const std::uint8_t *bytes =
      memory.GetReadPointer(pointsAddress, byteCount);
  if (bytes == nullptr) {
    return false;
  }
  curve->PointBytes = std::span<const std::byte>(
      reinterpret_cast<const std::byte *>(bytes), byteCount);
  return true;
}

bool SampleCameraCurveFromGuest(const NativeA32Memory &memory,
                                std::uint32_t curveAddress, float frame,
                                float *value) {
  CameraCurveView curve;
  if (!ReadCameraCurveView(memory, curveAddress, &curve)) {
    return false;
  }
  const CameraCurveSampleStatus status =
      oot3d::gameplay::SampleCameraCurve(curve, frame, false, value);
  // NativeCurve_SampleFloat returns zero for unrecognized curve types.
  return status == CameraCurveSampleStatus::Ok ||
         status == CameraCurveSampleStatus::UnsupportedType;
}

bool BuildCameraAnimationSample(const NativeA32Memory &memory,
                                std::uint32_t defaultsAddress,
                                std::uint32_t cmadContainerAddress,
                                float frame, CameraAnimationSample *sample,
                                std::uint64_t *curveSampleCount) {
  CameraAnimationDefaultsWire defaults;
  CameraAnimationCmadContainerHeaderWire container;
  if (sample == nullptr || curveSampleCount == nullptr ||
      !ReadWireObject(memory, defaultsAddress, &defaults) ||
      !ReadWireObject(memory, cmadContainerAddress, &container)) {
    return false;
  }

  oot3d::gameplay::InitializeCameraAnimationSample(defaults, sample);
  std::uint64_t sampledCurves = 0U;
  if (container.RecordCount <= 0) {
    *curveSampleCount = sampledCurves;
    return true;
  }

  for (std::int32_t recordIndex = 0; recordIndex < container.RecordCount;
       ++recordIndex) {
    std::uint32_t tableEntryAddress = 0U;
    if (!CheckedAddress(
            cmadContainerAddress,
            0x08ULL + static_cast<std::uint64_t>(recordIndex) *
                          sizeof(std::uint32_t),
            &tableEntryAddress)) {
      return false;
    }
    std::uint32_t relativeRecordAddress = 0U;
    std::uint32_t recordAddress = 0U;
    CameraAnimationCmadRecordWire record;
    if (!memory.ReadFast(tableEntryAddress, &relativeRecordAddress) ||
        !CheckedAddress(cmadContainerAddress, relativeRecordAddress,
                        &recordAddress) ||
        !ReadWireObject(memory, recordAddress, &record)) {
      return false;
    }

    CameraAnimationChannelSamples channels{};
    const std::size_t channelCount =
        oot3d::gameplay::CameraAnimationRecordChannelCount(record.Type);
    for (std::size_t channel = 0U; channel < channelCount; ++channel) {
      const std::int16_t relativeCurveAddress = record.CurveOffsets[channel];
      if (relativeCurveAddress == 0) {
        continue;
      }
      std::uint32_t curveAddress = 0U;
      if (!CheckedSignedAddress(recordAddress, relativeCurveAddress,
                                &curveAddress) ||
          !SampleCameraCurveFromGuest(memory, curveAddress, frame,
                                      &channels[channel].Value)) {
        return false;
      }
      channels[channel].Present = true;
      ++sampledCurves;
    }
    oot3d::gameplay::ApplyCameraAnimationRecord(record.Type, channels, sample);
  }

  *curveSampleCount = sampledCurves;
  return true;
}

bool CanWriteCameraAnimationSample(const NativeA32Memory &memory,
                                   std::uint32_t outputAddress) {
  return CanWriteField<float>(
             memory, outputAddress,
             offsetof(CameraAnimationStateWire, Field80)) &&
         CanWriteField<float>(
             memory, outputAddress,
             offsetof(CameraAnimationStateWire, Field84)) &&
         CanWriteField<float>(
             memory, outputAddress,
             offsetof(CameraAnimationStateWire, Field88)) &&
         CanWriteField<float>(
             memory, outputAddress,
             offsetof(CameraAnimationStateWire, Field8C)) &&
         CanWriteField<float>(
             memory, outputAddress,
             offsetof(CameraAnimationStateWire, Field90)) &&
         CanWriteField<float>(
             memory, outputAddress,
             offsetof(CameraAnimationStateWire, Field94)) &&
         CanWriteField<float>(
             memory, outputAddress,
             offsetof(CameraAnimationStateWire, FieldD0)) &&
         CanWriteField<float>(
             memory, outputAddress,
             offsetof(CameraAnimationStateWire, Field144)) &&
         CanWriteField<std::uint16_t>(
             memory, outputAddress,
             offsetof(CameraAnimationStateWire, Field1A2));
}

bool WriteCameraAnimationSample(NativeA32Memory &memory,
                                std::uint32_t outputAddress,
                                const CameraAnimationSample &sample,
                                std::uint16_t field1A2) {
  return WriteField(memory, outputAddress,
                    offsetof(CameraAnimationStateWire, Field80),
                    sample.Field80) &&
         WriteField(memory, outputAddress,
                    offsetof(CameraAnimationStateWire, Field84),
                    sample.Field84) &&
         WriteField(memory, outputAddress,
                    offsetof(CameraAnimationStateWire, Field88),
                    sample.Field88) &&
         WriteField(memory, outputAddress,
                    offsetof(CameraAnimationStateWire, Field8C),
                    sample.Field8C) &&
         WriteField(memory, outputAddress,
                    offsetof(CameraAnimationStateWire, Field90),
                    sample.Field90) &&
         WriteField(memory, outputAddress,
                    offsetof(CameraAnimationStateWire, Field94),
                    sample.Field94) &&
         WriteField(memory, outputAddress,
                    offsetof(CameraAnimationStateWire, FieldD0),
                    sample.FieldD0) &&
         WriteField(memory, outputAddress,
                    offsetof(CameraAnimationStateWire, Field144),
                    sample.Field144) &&
         WriteField(memory, outputAddress,
                    offsetof(CameraAnimationStateWire, Field1A2),
                    field1A2);
}

bool ValidateMainCutsceneCameraBinding(const NativeA32Memory &memory,
                                       const CameraAnimationBinding &binding) {
  GuestPtr<CutsceneCommandStream> activeCutscene;
  std::uint8_t backendClockActive = 0U;
  std::uint32_t localFrame = 0U;
  return binding.Valid && binding.PlayAddress != 0U &&
         binding.ActiveCutsceneDataAddress != 0U &&
         binding.OutputStateAddress != 0U && binding.DefaultsAddress != 0U &&
         binding.CmadContainerAddress != 0U &&
         ReadField(memory, binding.PlayAddress,
                   offsetof(CutscenePlayStateWire, ActiveCutsceneData),
                   &activeCutscene) &&
         activeCutscene.Address == binding.ActiveCutsceneDataAddress &&
         ReadField(memory, binding.PlayAddress,
                   offsetof(CutsceneFrameOwnerPlayStateWire,
                            BackendClockActive),
                   &backendClockActive) &&
         backendClockActive == 0U &&
         memory.ReadFast(kMainCutsceneCameraLocalFrameAddress, &localFrame) &&
         localFrame == std::bit_cast<std::uint32_t>(binding.NativeFrame) &&
         memory.IsMapped(binding.DefaultsAddress,
                         sizeof(CameraAnimationDefaultsWire)) &&
         memory.IsMapped(binding.CmadContainerAddress,
                         sizeof(CameraAnimationCmadContainerHeaderWire));
}

bool ObserveMainCutsceneCameraAnimationApplyFrame(
    const oot3d::recomp::a32::GuestState &state,
    const NativeA32Memory &memory) {
  const std::uint32_t outputAddress = state.r[2];
  CameraAnimationResourcePairWire resources;
  GuestPtr<CutsceneCommandStream> activeCutscene;
  std::uint8_t backendClockActive = 0U;
  std::uint32_t localFrame = 0U;
  if (outputAddress < kMainCutsceneCameraStateOffset ||
      !ReadWireObject(memory, state.r[0], &resources)) {
    ++gStats.CutsceneCameraRejectedObservations;
    return false;
  }

  const std::uint32_t playAddress =
      outputAddress - kMainCutsceneCameraStateOffset;
  if (resources.Defaults.Address == 0U ||
      resources.CmadContainer.Address == 0U ||
      !ReadField(memory, playAddress,
                 offsetof(CutscenePlayStateWire, ActiveCutsceneData),
                 &activeCutscene) ||
      activeCutscene.Address == 0U ||
      !ReadField(memory, playAddress,
                 offsetof(CutsceneFrameOwnerPlayStateWire,
                          BackendClockActive),
                 &backendClockActive) ||
      backendClockActive != 0U ||
      !memory.ReadFast(kMainCutsceneCameraLocalFrameAddress, &localFrame) ||
      localFrame != state.r[1] ||
      !memory.IsMapped(resources.Defaults.Address,
                       sizeof(CameraAnimationDefaultsWire)) ||
      !memory.IsMapped(resources.CmadContainer.Address,
                       sizeof(CameraAnimationCmadContainerHeaderWire)) ||
      !memory.IsWritable(outputAddress, sizeof(CameraAnimationStateWire))) {
    ++gStats.CutsceneCameraRejectedObservations;
    return false;
  }

  gMainCutsceneCameraBinding = {
      .PlayAddress = playAddress,
      .ActiveCutsceneDataAddress = activeCutscene.Address,
      .OutputStateAddress = outputAddress,
      .DefaultsAddress = resources.Defaults.Address,
      .CmadContainerAddress = resources.CmadContainer.Address,
      .NativeFrame = std::bit_cast<std::int32_t>(state.r[1]),
      .Valid = true,
  };
  ++gStats.CutsceneCameraBindingObservations;
  return false;
}

bool ObserveActorCutsceneCameraAnimationApplyFrame(
    const oot3d::recomp::a32::GuestState &state,
    const NativeA32Memory &memory) {
  constexpr std::uint32_t kResourcesOffset =
      offsetof(ActorCutsceneCameraOwnerWire, Resources);
  if (state.r[0] < kResourcesOffset) {
    gActorCutsceneCameraBinding = {};
    ++gStats.ActorCutsceneCameraRejectedObservations;
    return false;
  }

  const std::uint32_t ownerAddress = state.r[0] - kResourcesOffset;
  CameraAnimationResourcePairWire resources;
  std::int32_t frameCursor = 0;
  std::int32_t animationIndex = -1;
  if (!ReadWireObject(memory, state.r[0], &resources) ||
      !ReadField(memory, ownerAddress,
                 offsetof(ActorCutsceneCameraOwnerWire, NativeFrameCursor),
                 &frameCursor) ||
      !ReadField(memory, ownerAddress,
                 offsetof(ActorCutsceneCameraOwnerWire, AnimationIndex),
                 &animationIndex) ||
      frameCursor != std::bit_cast<std::int32_t>(state.r[1]) ||
      animationIndex < 0 || resources.Defaults.Address == 0U ||
      resources.CmadContainer.Address == 0U ||
      !memory.IsMapped(resources.Defaults.Address,
                       sizeof(CameraAnimationDefaultsWire)) ||
      !memory.IsMapped(resources.CmadContainer.Address,
                       sizeof(CameraAnimationCmadContainerHeaderWire)) ||
      !memory.IsWritable(kActorCutsceneCameraStateAddress,
                         sizeof(CameraAnimationStateWire))) {
    gActorCutsceneCameraBinding = {};
    ++gStats.ActorCutsceneCameraRejectedObservations;
    return false;
  }

  gActorCutsceneCameraBinding = {
      .OwnerAddress = ownerAddress,
      .OutputStateAddress = kActorCutsceneCameraStateAddress,
      .DefaultsAddress = resources.Defaults.Address,
      .CmadContainerAddress = resources.CmadContainer.Address,
      .NativeFrame = frameCursor,
      .AnimationIndex = animationIndex,
      .Valid = true,
  };
  ++gStats.ActorCutsceneCameraBindingObservations;
  return false;
}

bool ObserveCameraAnimationApplyFrame(
    const oot3d::recomp::a32::GuestState &state,
    const NativeA32Memory &memory) {
  if (state.r[2] == kActorCutsceneCameraPrimingStateAddress) {
    ++gStats.ActorCutsceneCameraPrimingObservations;
    return false;
  }
  if (state.r[2] == kActorCutsceneCameraStateAddress) {
    return ObserveActorCutsceneCameraAnimationApplyFrame(state, memory);
  }
  return ObserveMainCutsceneCameraAnimationApplyFrame(state, memory);
}

bool SampleMainCutsceneCameraAnimation(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    const Oot3dTypedGameplayContext &context) {
  if (context.Time == nullptr || context.Time->CrossedLogicalFrame ||
      !gMainCutsceneCameraBinding.Valid) {
    return false;
  }
  const float continuousFrame =
      oot3d::gameplay::ResolveCameraAnimationContinuousFrame(
          gMainCutsceneCameraBinding.NativeFrame, *context.Time);
  if (continuousFrame ==
      static_cast<float>(gMainCutsceneCameraBinding.NativeFrame)) {
    return false;
  }

  GuestPtr<CameraAnimationStateWire> attachedState;
  std::uint16_t animationFlags = 0U;
  if (!ReadField(memory, state.r[0],
                 offsetof(CameraDemo1Wire, AttachedAnimationState),
                 &attachedState) ||
      !ReadField(memory, state.r[0],
                 offsetof(CameraDemo1Wire, AnimationFlags),
                 &animationFlags) ||
      attachedState.Address !=
          gMainCutsceneCameraBinding.OutputStateAddress ||
      (animationFlags & 0x0004U) == 0U) {
    return false;
  }
  if (!ValidateMainCutsceneCameraBinding(
          memory, gMainCutsceneCameraBinding)) {
    gMainCutsceneCameraBinding.Valid = false;
    ++gStats.CutsceneCameraSampleFailures;
    return false;
  }

  CameraAnimationSample sample;
  std::uint64_t sampledCurves = 0U;
  if (!BuildCameraAnimationSample(
          memory, gMainCutsceneCameraBinding.DefaultsAddress,
          gMainCutsceneCameraBinding.CmadContainerAddress, continuousFrame,
          &sample, &sampledCurves) ||
      !CanWriteCameraAnimationSample(
          memory, gMainCutsceneCameraBinding.OutputStateAddress)) {
    ++gStats.CutsceneCameraSampleFailures;
    return false;
  }

  const auto converted = oot3d::recomp::a32::VfpBinary32ToSigned(
      std::bit_cast<std::uint32_t>(sample.Field1A2Scaled), state.fpscr);
  const std::uint16_t field1A2 =
      static_cast<std::uint16_t>(converted.value);
  if (!WriteCameraAnimationSample(
          memory, gMainCutsceneCameraBinding.OutputStateAddress, sample,
          field1A2)) {
    ++gStats.CutsceneCameraSampleFailures;
    return false;
  }

  state.fpscr |= converted.exception_flags;
  ++gStats.CutsceneCalls;
  ++gStats.CutsceneCameraFractionalSamples;
  gStats.CutsceneCameraCurveSamples += sampledCurves;
  return false;
}

bool ExecuteActorCutsceneCameraAdvanceBlock(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  if (context.Time == nullptr || context.Time->CrossedLogicalFrame ||
      !gActorCutsceneCameraBinding.Valid) {
    return false;
  }

  const float continuousFrame =
      oot3d::gameplay::ResolveCameraAnimationContinuousFrame(
          gActorCutsceneCameraBinding.NativeFrame, *context.Time);
  const double nextNativeFrame =
      static_cast<double>(gActorCutsceneCameraBinding.NativeFrame) + 1.0;
  if (!(continuousFrame >
            static_cast<float>(gActorCutsceneCameraBinding.NativeFrame) &&
        static_cast<double>(continuousFrame) < nextNativeFrame)) {
    return false;
  }

  std::uint32_t expectedCameraContext = 0U;
  std::uint32_t expectedCutsceneFlags = 0U;
  if (state.r[4] != gActorCutsceneCameraBinding.OwnerAddress ||
      !CheckedAddress(state.r[6], 0xA00U, &expectedCameraContext) ||
      !CheckedAddress(state.r[6], 0x20ACU, &expectedCutsceneFlags) ||
      state.r[5] != expectedCameraContext ||
      state.r[7] != expectedCutsceneFlags) {
    gActorCutsceneCameraBinding.Valid = false;
    ++gStats.ActorCutsceneCameraSampleFailures;
    return false;
  }

  std::int32_t frameCursor = 0;
  std::int32_t animationIndex = -1;
  std::int32_t segmentEndFrame = 0;
  CameraAnimationResourcePairWire resources;
  std::int16_t activeCameraIndex = -1;
  if (!ReadField(
          memory, gActorCutsceneCameraBinding.OwnerAddress,
          offsetof(ActorCutsceneCameraOwnerWire, NativeFrameCursor),
          &frameCursor) ||
      !ReadField(memory, gActorCutsceneCameraBinding.OwnerAddress,
                 offsetof(ActorCutsceneCameraOwnerWire, AnimationIndex),
                 &animationIndex) ||
      !ReadField(memory, gActorCutsceneCameraBinding.OwnerAddress,
                 offsetof(ActorCutsceneCameraOwnerWire, Resources),
                 &resources) ||
      !ReadField(memory, gActorCutsceneCameraBinding.DefaultsAddress,
                 offsetof(CameraAnimationDefaultsWire, SegmentEndFrame),
                 &segmentEndFrame) ||
      !ReadField(memory, state.r[6],
                 offsetof(CameraPlayStateWire, ActiveCameraIndex),
                 &activeCameraIndex)) {
    gActorCutsceneCameraBinding.Valid = false;
    ++gStats.ActorCutsceneCameraSampleFailures;
    return false;
  }

  constexpr std::int16_t kCameraCount =
      static_cast<std::int16_t>(CameraPlayStateWire{}.Cameras.size());
  const std::int64_t expectedFrameCursor =
      static_cast<std::int64_t>(gActorCutsceneCameraBinding.NativeFrame) + 1;
  if (activeCameraIndex < 0 || activeCameraIndex >= kCameraCount ||
      expectedFrameCursor != frameCursor || frameCursor > segmentEndFrame ||
      animationIndex != gActorCutsceneCameraBinding.AnimationIndex ||
      resources.Defaults.Address !=
          gActorCutsceneCameraBinding.DefaultsAddress ||
      resources.CmadContainer.Address !=
          gActorCutsceneCameraBinding.CmadContainerAddress) {
    gActorCutsceneCameraBinding.Valid = false;
    ++gStats.ActorCutsceneCameraSampleFailures;
    return false;
  }

  GuestPtr<CameraDemo1Wire> activeCamera;
  const std::size_t cameraOffset =
      offsetof(CameraPlayStateWire, Cameras) +
      static_cast<std::size_t>(activeCameraIndex) *
          sizeof(GuestPtr<CameraDemo1Wire>);
  GuestPtr<CameraAnimationStateWire> attachedState;
  std::uint16_t animationFlags = 0U;
  if (!ReadField(memory, state.r[6], cameraOffset, &activeCamera) ||
      activeCamera.Address == 0U || activeCamera.Address != state.r[8] ||
      !ReadField(memory, activeCamera.Address,
                 offsetof(CameraDemo1Wire, AttachedAnimationState),
                 &attachedState) ||
      !ReadField(memory, activeCamera.Address,
                 offsetof(CameraDemo1Wire, AnimationFlags),
                 &animationFlags) ||
      attachedState.Address !=
          gActorCutsceneCameraBinding.OutputStateAddress ||
      (animationFlags & 0x0004U) == 0U ||
      !CanWriteCameraAnimationSample(
          memory, gActorCutsceneCameraBinding.OutputStateAddress)) {
    gActorCutsceneCameraBinding.Valid = false;
    ++gStats.ActorCutsceneCameraSampleFailures;
    return false;
  }

  CameraAnimationSample sample;
  std::uint64_t sampledCurves = 0U;
  if (!BuildCameraAnimationSample(
          memory, gActorCutsceneCameraBinding.DefaultsAddress,
          gActorCutsceneCameraBinding.CmadContainerAddress, continuousFrame,
          &sample, &sampledCurves)) {
    ++gStats.ActorCutsceneCameraSampleFailures;
    return false;
  }

  const auto converted = oot3d::recomp::a32::VfpBinary32ToSigned(
      std::bit_cast<std::uint32_t>(sample.Field1A2Scaled), state.fpscr);
  if (!WriteCameraAnimationSample(
          memory, gActorCutsceneCameraBinding.OutputStateAddress, sample,
          static_cast<std::uint16_t>(converted.value))) {
    ++gStats.ActorCutsceneCameraSampleFailures;
    return false;
  }

  state.fpscr |= converted.exception_flags;
  state.r[15] = kOot3dEnZl4ActorCameraAdvanceContinue;
  *result = {
      oot3d::recomp::a32::ExitKind::Branch,
      state.r[15],
      oot3d::recomp::a32::FallbackReason::None,
      kOot3dEnZl4ActorCameraAdvanceBlock,
  };
  if (blocksConsumed != nullptr) {
    *blocksConsumed = 1U;
  }
  ++gStats.Calls;
  ++gStats.CutsceneCalls;
  ++gStats.ActorCutsceneCameraIntermediateHolds;
  ++gStats.ActorCutsceneCameraFractionalSamples;
  gStats.ActorCutsceneCameraCurveSamples += sampledCurves;
  return true;
}

bool ValidateCameraUpdateState(
    const oot3d::recomp::a32::GuestState &state) noexcept {
  std::uint32_t expectedCameraState = 0U;
  return state.r[4] != 0U &&
         CheckedAddress(state.r[4], 0x100U, &expectedCameraState) &&
         state.r[5] == expectedCameraState;
}

const CameraModeCountdownSite *
FindCameraModeCountdownSite(std::uint32_t pc) noexcept {
  for (const auto &site : kCameraModeCountdownSites) {
    if (site.Entry == pc) {
      return &site;
    }
  }
  return nullptr;
}

bool ValidateCameraModeCountdownState(
    const oot3d::recomp::a32::GuestState &state,
    const CameraModeCountdownSite &site) noexcept {
  std::uint32_t expectedModeState = 0U;
  const std::uint32_t camera = state.r[site.CameraRegister];
  return camera != 0U &&
         CheckedAddress(camera, site.ModeBaseOffset, &expectedModeState) &&
         state.r[site.ModeBaseRegister] == expectedModeState;
}

bool ValidateCameraUpdateCounterState(
    const oot3d::recomp::a32::GuestState &state) noexcept {
  return ValidateCameraUpdateState(state) &&
         state.r[7] == kCameraGlobalStateAddress;
}

bool ValidateCameraCheckWaterTimerState(
    const oot3d::recomp::a32::GuestState &state) noexcept {
  std::uint32_t expectedCameraState = 0U;
  std::uint32_t expectedQuakeId = 0U;
  return state.r[5] != 0U &&
         CheckedAddress(state.r[5], 0x100U, &expectedCameraState) &&
         CheckedAddress(state.r[5], 0x168U, &expectedQuakeId) &&
         state.r[4] == expectedCameraState && state.r[6] == expectedQuakeId;
}

bool CompleteCameraTemporalBlock(
    std::uint32_t entry, std::uint32_t continuation,
    oot3d::recomp::a32::GuestState &state,
    oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed) {
  state.r[15] = continuation;
  *result = {
      oot3d::recomp::a32::ExitKind::Branch,
      continuation,
      oot3d::recomp::a32::FallbackReason::None,
      entry,
  };
  if (blocksConsumed != nullptr) {
    *blocksConsumed = 1U;
  }
  ++gStats.Calls;
  ++gStats.CameraCalls;
  return true;
}

bool ExecuteCameraModeFrameCountdownBlock(
    std::uint32_t pc, oot3d::recomp::a32::GuestState &state,
    NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  const CameraModeCountdownSite *site = FindCameraModeCountdownSite(pc);
  if (site == nullptr || context.Time == nullptr ||
      !ValidateCameraModeCountdownState(state, *site)) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  std::uint32_t countdownAddress = 0U;
  std::uint16_t countdownBits = 0U;
  if (!CheckedAddress(state.r[site->CameraRegister], site->CountdownOffset,
                      &countdownAddress) ||
      !memory.ReadFast(countdownAddress, &countdownBits)) {
    return FailRead();
  }
  const std::int16_t countdown =
      std::bit_cast<std::int16_t>(countdownBits);
  const std::uint32_t loadedCountdown =
      site->Load == CameraModeCountdownLoad::Signed16
          ? static_cast<std::uint32_t>(static_cast<std::int32_t>(countdown))
          : static_cast<std::uint32_t>(countdownBits);
  if (state.r[0] != loadedCountdown ||
      (site->RequiresNonzeroCondition &&
       (countdown == 0 ||
        (state.cpsr & oot3d::recomp::a32::kFlagZ) != 0U))) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  const auto advance =
      oot3d::gameplay::AdvanceCameraModeFrameCountdown(
          countdown, *context.Time);
  if (advance.Mutated &&
      !memory.WriteFast(
          countdownAddress,
          std::bit_cast<std::uint16_t>(advance.Value))) {
    return FailWrite();
  }

  // Native SUB does not set flags and leaves the untruncated 32-bit result in
  // r0; the callback continuation consumes or overwrites that exact value.
  if (advance.Mutated) {
    state.r[0] -= 1U;
  }
  ++gStats.CameraModeFrameCountdownBlockCalls;
  if (advance.Mutated) {
    ++gStats.CameraModeFrameCountdownLogicalAdvances;
  } else {
    ++gStats.CameraModeFrameCountdownIntermediateHolds;
  }
  return CompleteCameraTemporalBlock(site->Entry, site->Continuation, state,
                                     result, blocksConsumed);
}

bool ExecuteCameraSpecial5TimerBlock(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  constexpr std::uint32_t kConditionFlagsMask =
      oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagZ |
      oot3d::recomp::a32::kFlagC | oot3d::recomp::a32::kFlagV;
  constexpr std::uint32_t kTimerOffset = 0x1CU;

  std::uint32_t timerAddress = 0U;
  if (context.Time == nullptr || state.r[4] == 0U ||
      !CheckedAddress(state.r[4], kTimerOffset, &timerAddress) ||
      state.r[7] != timerAddress || state.r[2] != 0U) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  std::uint16_t timerBits = 0U;
  if (!memory.ReadFast(timerAddress, &timerBits)) {
    return FailRead();
  }
  const std::int16_t timer = std::bit_cast<std::int16_t>(timerBits);
  const std::uint32_t loadedTimer =
      static_cast<std::uint32_t>(static_cast<std::int32_t>(timer));
  const std::uint32_t expectedFlags =
      SubtractConditionFlags(loadedTimer, 0U);
  if (state.r[0] != loadedTimer ||
      (state.cpsr & kConditionFlagsMask) != expectedFlags) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  const auto advance =
      oot3d::gameplay::AdvanceCameraSpecial5Timer(timer, *context.Time);
  std::uint32_t continuation = kOot3dCameraSpecial5CommonContinue;
  switch (advance.Action) {
  case CameraSpecial5TimerAction::Hold:
    ++gStats.CameraSpecial5TimerIntermediateHolds;
    break;
  case CameraSpecial5TimerAction::Decrement:
    if (!memory.WriteFast(timerAddress,
                          std::bit_cast<std::uint16_t>(advance.Value))) {
      return FailWrite();
    }
    state.r[0] -= 1U;
    ++gStats.CameraSpecial5TimerLogicalDecrements;
    break;
  case CameraSpecial5TimerAction::ZeroTransition:
    continuation = kOot3dCameraSpecial5ZeroTransition;
    ++gStats.CameraSpecial5TimerZeroTransitions;
    break;
  case CameraSpecial5TimerAction::Terminal:
    ++gStats.CameraSpecial5TimerTerminalContinues;
    break;
  }

  ++gStats.CameraSpecial5TimerBlockCalls;
  return CompleteCameraTemporalBlock(kOot3dCameraSpecial5TimerBlock,
                                     continuation, state, result,
                                     blocksConsumed);
}

bool CompleteActorTemporalBlock(
    std::uint32_t entry, std::uint32_t continuation,
    oot3d::recomp::a32::GuestState &state,
    oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed) {
  state.r[15] = continuation;
  *result = {
      oot3d::recomp::a32::ExitKind::Branch,
      continuation,
      oot3d::recomp::a32::FallbackReason::None,
      entry,
  };
  if (blocksConsumed != nullptr) {
    *blocksConsumed = 1U;
  }
  ++gStats.Calls;
  ++gStats.ActorCalls;
  return true;
}

bool ExecuteEnKanbanPhaseAdvanceBlock(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  constexpr std::uint32_t kPhaseOffset = 0x1A8U;

  std::uint32_t phaseAddress = 0U;
  std::uint8_t phase = 0U;
  if (context.Time == nullptr || state.r[4] == 0U ||
      !CheckedAddress(state.r[4], kPhaseOffset, &phaseAddress)) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }
  if (!memory.ReadFast(phaseAddress, &phase)) {
    return FailRead();
  }
  // The preceding native LDRB must have loaded this exact field into r0.
  if (state.r[0] != static_cast<std::uint32_t>(phase)) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  const auto advance =
      oot3d::gameplay::AdvanceActorAuthoredPhase(phase, *context.Time);
  if (advance.Mutated) {
    if (!memory.WriteFast(phaseAddress, advance.Value)) {
      return FailWrite();
    }
    // Native ADD is 32-bit; only the subsequent STRB wraps the stored phase.
    state.r[0] += 1U;
    ++gStats.EnKanbanPhaseLogicalAdvances;
  } else {
    ++gStats.EnKanbanPhaseIntermediateHolds;
  }

  ++gStats.EnKanbanPhaseBlockCalls;
  return CompleteActorTemporalBlock(
      kOot3dEnKanbanPhaseAdvanceBlock,
      kOot3dEnKanbanPhaseAdvanceContinue, state, result, blocksConsumed);
}

bool ResolveEnKanbanStateField(
    const oot3d::recomp::a32::GuestState &state, std::uint32_t fieldOffset,
    std::uint32_t *fieldAddress) noexcept {
  constexpr std::uint32_t kStateBaseOffset = 0x100U;
  std::uint32_t expectedStateBase = 0U;
  return state.r[4] != 0U &&
         CheckedAddress(state.r[4], kStateBaseOffset, &expectedStateBase) &&
         state.r[5] == expectedStateBase &&
         CheckedAddress(state.r[4], fieldOffset, fieldAddress);
}

bool ExecuteEnKanbanState0CountdownBlock(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  constexpr std::uint32_t kTimerOffset = 0x1B2U;
  constexpr std::uint32_t kConditionFlagsMask =
      oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagZ |
      oot3d::recomp::a32::kFlagC | oot3d::recomp::a32::kFlagV;

  std::uint32_t timerAddress = 0U;
  if (context.Time == nullptr ||
      !ResolveEnKanbanStateField(state, kTimerOffset, &timerAddress)) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }
  std::uint16_t timerBits = 0U;
  if (!memory.ReadFast(timerAddress, &timerBits)) {
    return FailRead();
  }
  std::int16_t timer = std::bit_cast<std::int16_t>(timerBits);
  const std::uint32_t loadedTimer =
      static_cast<std::uint32_t>(static_cast<std::int32_t>(timer));
  if (state.r[0] != loadedTimer ||
      (state.cpsr & kConditionFlagsMask) !=
          SubtractConditionFlags(loadedTimer, 0U)) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  const auto advance =
      oot3d::gameplay::TickDownWrappingIfNonzero(timer, *context.Time);
  if (advance.Mutated &&
      !memory.WriteFast(timerAddress, std::bit_cast<std::uint16_t>(timer))) {
    return FailWrite();
  }
  if (advance.Mutated) {
    // Native SUBNE is 32-bit and STRHNE truncates only the stored field.
    state.r[0] -= 1U;
    ++gStats.EnKanbanState0CountdownLogicalAdvances;
  } else if (advance.Previous != 0 && !context.Time->CrossedLogicalFrame) {
    ++gStats.EnKanbanState0CountdownIntermediateHolds;
  }
  ++gStats.EnKanbanState0CountdownBlockCalls;

  return CompleteActorTemporalBlock(
      kOot3dEnKanbanState0CountdownBlock,
      kOot3dEnKanbanState0CountdownContinue, state, result, blocksConsumed);
}

bool ExecuteEnKanbanActorFlagCountdownBlock(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  constexpr std::uint32_t kTimerOffset = 0x1F2U;
  constexpr std::uint32_t kActorFlagsOffset = 0x04U;
  constexpr std::uint32_t kConditionFlagsMask =
      oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagZ |
      oot3d::recomp::a32::kFlagC | oot3d::recomp::a32::kFlagV;

  std::uint32_t timerAddress = 0U;
  std::uint32_t actorFlagsAddress = 0U;
  if (context.Time == nullptr ||
      !ResolveEnKanbanStateField(state, kTimerOffset, &timerAddress) ||
      !CheckedAddress(state.r[4], kActorFlagsOffset, &actorFlagsAddress)) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }
  std::uint16_t timerBits = 0U;
  if (!memory.ReadFast(timerAddress, &timerBits)) {
    return FailRead();
  }
  std::int16_t timer = std::bit_cast<std::int16_t>(timerBits);
  const std::uint32_t loadedTimer =
      static_cast<std::uint32_t>(static_cast<std::int32_t>(timer));
  if (state.r[0] != loadedTimer ||
      (state.cpsr & kConditionFlagsMask) !=
          SubtractConditionFlags(loadedTimer, 0U)) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  const auto advance =
      oot3d::gameplay::TickDownWrappingIfNonzero(timer, *context.Time);
  const bool clearActorFlag = advance.Mutated && timer == 1;
  std::uint32_t actorFlags = 0U;
  if (clearActorFlag && !memory.ReadFast(actorFlagsAddress, &actorFlags)) {
    return FailRead();
  }
  if (advance.Mutated &&
      !memory.IsWritable(timerAddress, sizeof(std::uint16_t))) {
    return FailWrite();
  }
  if (clearActorFlag &&
      !memory.IsWritable(actorFlagsAddress, sizeof(std::uint32_t))) {
    return FailWrite();
  }
  if (advance.Mutated &&
      !memory.WriteFast(timerAddress, std::bit_cast<std::uint16_t>(timer))) {
    return FailWrite();
  }
  if (clearActorFlag &&
      !memory.WriteFast(actorFlagsAddress, actorFlags & ~std::uint32_t{1})) {
    return FailWrite();
  }

  if (advance.Mutated) {
    state.r[0] = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(timer));
    state.cpsr =
        (state.cpsr & ~kConditionFlagsMask) |
        SubtractConditionFlags(state.r[0], 1U);
    ++gStats.EnKanbanActorFlagCountdownLogicalAdvances;
  } else if (advance.Previous != 0 && !context.Time->CrossedLogicalFrame) {
    ++gStats.EnKanbanActorFlagCountdownIntermediateHolds;
  }
  if (clearActorFlag) {
    ++gStats.EnKanbanActorFlagClears;
  }
  ++gStats.EnKanbanActorFlagCountdownBlockCalls;

  return CompleteActorTemporalBlock(
      kOot3dEnKanbanActorFlagCountdownBlock,
      kOot3dEnKanbanActorFlagCountdownContinue, state, result,
      blocksConsumed);
}

bool ExecuteEnKanbanInteractionCooldownBlock(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  constexpr std::uint32_t kTimerOffset = 0x1F5U;
  constexpr std::uint32_t kConditionFlagsMask =
      oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagZ |
      oot3d::recomp::a32::kFlagC | oot3d::recomp::a32::kFlagV;

  std::uint32_t timerAddress = 0U;
  if (context.Time == nullptr ||
      !ResolveEnKanbanStateField(state, kTimerOffset, &timerAddress)) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }
  std::uint8_t timer = 0U;
  if (!memory.ReadFast(timerAddress, &timer)) {
    return FailRead();
  }
  if (state.r[0] != static_cast<std::uint32_t>(timer) ||
      (state.cpsr & kConditionFlagsMask) !=
          SubtractConditionFlags(static_cast<std::uint32_t>(timer), 0U)) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  const bool wasBlocked = timer != 0U;
  const auto advance =
      oot3d::gameplay::TickDownIfNonzero(timer, *context.Time);
  if (advance.Mutated && !memory.WriteFast(timerAddress, timer)) {
    return FailWrite();
  }
  if (advance.Mutated) {
    state.r[0] = timer;
    ++gStats.EnKanbanInteractionCooldownLogicalAdvances;
  } else if (wasBlocked && !context.Time->CrossedLogicalFrame) {
    ++gStats.EnKanbanInteractionCooldownIntermediateHolds;
  }

  const std::uint32_t continuation =
      wasBlocked ? kOot3dEnKanbanInteractionCooldownBlocked
                 : kOot3dEnKanbanInteractionCooldownReady;
  if (wasBlocked) {
    ++gStats.EnKanbanInteractionCooldownBlockedPasses;
  } else {
    ++gStats.EnKanbanInteractionCooldownReadyPasses;
  }
  ++gStats.EnKanbanInteractionCooldownBlockCalls;

  return CompleteActorTemporalBlock(
      kOot3dEnKanbanInteractionCooldownBlock, continuation, state, result,
      blocksConsumed);
}

bool ExecuteEnKanbanDrawGateRampBlock(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  constexpr std::uint32_t kTimerOffset = 0x1EEU;
  constexpr std::uint32_t kRampOffset = 0x1F0U;
  constexpr std::int16_t kSplitThreshold = 8;
  constexpr std::uint16_t kIncreaseStep = 0xFFU;
  constexpr std::uint16_t kDecreaseStep = 0x41U;
  constexpr std::int16_t kMinimum = 0;
  constexpr std::int16_t kMaximum = 0xFF;
  constexpr std::uint32_t kConditionFlagsMask =
      oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagZ |
      oot3d::recomp::a32::kFlagC | oot3d::recomp::a32::kFlagV;

  std::uint32_t timerAddress = 0U;
  std::uint32_t rampAddress = 0U;
  if (context.Time == nullptr || state.r[7] != 0U ||
      !ResolveEnKanbanStateField(state, kTimerOffset, &timerAddress) ||
      !ResolveEnKanbanStateField(state, kRampOffset, &rampAddress)) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  std::uint16_t timerBits = 0U;
  if (!memory.ReadFast(timerAddress, &timerBits)) {
    return FailRead();
  }
  const std::int16_t timer = std::bit_cast<std::int16_t>(timerBits);
  const std::uint32_t loadedTimer =
      static_cast<std::uint32_t>(static_cast<std::int32_t>(timer));
  if (state.r[0] != loadedTimer ||
      (state.cpsr & kConditionFlagsMask) !=
          SubtractConditionFlags(loadedTimer, 0U)) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  std::uint16_t ramp = 0U;
  const bool evaluates =
      timer != 0 && context.Time->CrossedLogicalFrame;
  if (evaluates && !memory.ReadFast(rampAddress, &ramp)) {
    return FailRead();
  }
  const auto advance = oot3d::gameplay::AdvanceActorAuthoredRamp(
      timer, ramp, kSplitThreshold, kIncreaseStep, kDecreaseStep, kMinimum,
      kMaximum, *context.Time);
  if (!advance.Valid) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  if (advance.Mutated &&
      (!memory.IsWritable(rampAddress, sizeof(std::uint16_t)) ||
       !memory.IsWritable(timerAddress, sizeof(std::uint16_t)))) {
    return FailWrite();
  }
  if (advance.Mutated &&
      !memory.WriteFast(rampAddress, advance.StoredValue)) {
    return FailWrite();
  }
  if (advance.Mutated &&
      !memory.WriteFast(timerAddress,
                        std::bit_cast<std::uint16_t>(advance.Timer))) {
    return FailWrite();
  }

  if (advance.Mutated) {
    // Native SUB retains its 32-bit result while STRH stores the timer wire.
    state.r[0] -= 1U;
    state.r[1] = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(advance.RegisterValue));
    const std::uint32_t comparisonRight =
        advance.Action ==
                oot3d::gameplay::ActorAuthoredRampAction::Increase
            ? static_cast<std::uint32_t>(
                  static_cast<std::int32_t>(kMaximum))
            : static_cast<std::uint32_t>(
                  static_cast<std::int32_t>(kMinimum));
    state.cpsr =
        (state.cpsr & ~kConditionFlagsMask) |
        SubtractConditionFlags(
            static_cast<std::uint32_t>(
                static_cast<std::int32_t>(advance.ComparisonValue)),
            comparisonRight);
    ++gStats.EnKanbanDrawGateRampLogicalAdvances;
    if (advance.Action ==
        oot3d::gameplay::ActorAuthoredRampAction::Increase) {
      ++gStats.EnKanbanDrawGateRampIncreaseSteps;
    } else {
      ++gStats.EnKanbanDrawGateRampDecreaseSteps;
    }
    if (advance.ValueClamped) {
      ++gStats.EnKanbanDrawGateRampClamps;
    }
  } else if (timer != 0 && !context.Time->CrossedLogicalFrame) {
    ++gStats.EnKanbanDrawGateRampIntermediateHolds;
  } else if (timer == 0) {
    ++gStats.EnKanbanDrawGateRampInactivePasses;
  }
  ++gStats.EnKanbanDrawGateRampBlockCalls;

  return CompleteActorTemporalBlock(
      kOot3dEnKanbanDrawGateRampBlock,
      kOot3dEnKanbanDrawGateRampContinue, state, result, blocksConsumed);
}

bool ExecuteEnKanbanOscillatorAxisBlock(
    std::uint32_t entry, oot3d::recomp::a32::GuestState &state,
    NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  struct Site {
    std::uint32_t Entry;
    std::uint32_t Continuation;
    std::uint32_t DisplacementOffset;
    std::uint32_t VelocityOffset;
    std::uint32_t DirectionOffset;
  };
  constexpr std::array kSites{
      Site{kOot3dEnKanbanOscillatorXBlock,
           kOot3dEnKanbanOscillatorXContinue, 0x1C0U, 0x1C6U, 0x1CCU},
      Site{kOot3dEnKanbanOscillatorYBlock,
           kOot3dEnKanbanOscillatorYContinue, 0x1C4U, 0x1CAU, 0x1CDU},
  };
  constexpr std::uint32_t kActorStateOffset = 0x1ACU;
  constexpr std::uint32_t kBackgroundFlagsOffset = 0x90U;
  constexpr std::int16_t kAcceleration = -0xC00;
  constexpr std::uint32_t kConditionFlagsMask =
      oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagZ |
      oot3d::recomp::a32::kFlagC | oot3d::recomp::a32::kFlagV;

  const auto site = std::find_if(
      kSites.begin(), kSites.end(),
      [entry](const Site &candidate) { return candidate.Entry == entry; });
  std::uint32_t displacementAddress = 0U;
  std::uint32_t velocityAddress = 0U;
  std::uint32_t directionAddress = 0U;
  std::uint32_t actorStateAddress = 0U;
  std::uint32_t backgroundFlagsAddress = 0U;
  if (site == kSites.end() ||
      !ResolveEnKanbanStateField(
          state, site->DisplacementOffset, &displacementAddress) ||
      !ResolveEnKanbanStateField(state, site->VelocityOffset,
                                 &velocityAddress) ||
      !ResolveEnKanbanStateField(state, site->DirectionOffset,
                                 &directionAddress) ||
      !ResolveEnKanbanStateField(state, kActorStateOffset,
                                 &actorStateAddress) ||
      !CheckedAddress(state.r[4], kBackgroundFlagsOffset,
                      &backgroundFlagsAddress) ||
      state.r[7] != 0U) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  std::uint16_t displacementBits = 0U;
  std::uint16_t velocityBits = 0U;
  std::uint8_t direction = 0U;
  std::uint8_t actorState = 0U;
  std::uint16_t backgroundFlags = 0U;
  std::uint32_t terminalVelocityBits = 0U;
  float updateScale = 0.0F;
  if (!memory.ReadFast(displacementAddress, &displacementBits) ||
      !memory.ReadFast(velocityAddress, &velocityBits) ||
      !memory.ReadFast(directionAddress, &direction) ||
      !memory.ReadFast(actorStateAddress, &actorState) ||
      !memory.ReadFast(backgroundFlagsAddress, &backgroundFlags) ||
      !memory.ReadFast(kEnKanbanTerminalVelocityLiteral,
                       &terminalVelocityBits) ||
      !ReadFloat(memory, kActorPositionRotationScaleLiteral, &updateScale)) {
    return FailRead();
  }

  const std::uint32_t grounded =
      static_cast<std::uint32_t>(backgroundFlags & 1U);
  if ((actorState != 1U && actorState != 2U) ||
      state.r[0] != static_cast<std::uint32_t>(displacementBits) ||
      state.r[1] != static_cast<std::uint32_t>(velocityBits) ||
      state.r[2] != terminalVelocityBits || state.r[6] != grounded ||
      (state.cpsr & kConditionFlagsMask) !=
          SubtractConditionFlags(static_cast<std::uint32_t>(direction), 0U)) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  const auto advance = oot3d::gameplay::AdvanceActorAngularOscillator(
      {
          std::bit_cast<std::int16_t>(displacementBits),
          std::bit_cast<std::int16_t>(velocityBits),
          direction,
      },
      grounded != 0U, kAcceleration,
      std::bit_cast<std::int16_t>(
          static_cast<std::uint16_t>(terminalVelocityBits)),
      context.NativeUpdateRate, updateScale);
  if (!advance.Valid) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  if (!memory.IsWritable(displacementAddress, sizeof(std::uint16_t)) ||
      !memory.IsWritable(velocityAddress, sizeof(std::uint16_t)) ||
      !memory.WriteFast(
          displacementAddress,
          std::bit_cast<std::uint16_t>(advance.State.Displacement)) ||
      !memory.WriteFast(velocityAddress,
                        std::bit_cast<std::uint16_t>(
                            advance.State.Velocity))) {
    return FailWrite();
  }

  state.r[0] = static_cast<std::uint32_t>(
      static_cast<std::int32_t>(advance.RegisterValue));
  const std::uint32_t conditionFlags =
      advance.GroundReset
          ? SubtractConditionFlags(state.r[6], 0U)
          : SubtractConditionFlags(state.r[0], terminalVelocityBits);
  state.cpsr =
      (state.cpsr & ~kConditionFlagsMask) | conditionFlags;

  ++gStats.EnKanbanOscillatorAxisBlockCalls;
  if (entry == kOot3dEnKanbanOscillatorXBlock) {
    ++gStats.EnKanbanOscillatorXBlockCalls;
  } else {
    ++gStats.EnKanbanOscillatorYBlockCalls;
  }
  if (context.NativeUpdateRate * updateScale != 1.0F) {
    ++gStats.EnKanbanOscillatorRateAdjustedSteps;
  }
  if (advance.GroundReset) {
    ++gStats.EnKanbanOscillatorGroundResets;
  }
  if (advance.VelocityClamped) {
    ++gStats.EnKanbanOscillatorVelocityClamps;
  }

  return CompleteActorTemporalBlock(
      site->Entry, site->Continuation, state, result, blocksConsumed);
}

bool ExecuteEnKanbanPieceLifetimeBlock(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  constexpr std::uint32_t kTimerOffset = 0x1AAU;
  constexpr std::uint32_t kStateOffset = 0x1ACU;
  constexpr std::uint8_t kSourceState = 2U;
  constexpr std::uint8_t kTransitionState = 3U;
  constexpr std::uint32_t kConditionFlagsMask =
      oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagZ |
      oot3d::recomp::a32::kFlagC | oot3d::recomp::a32::kFlagV;

  std::uint32_t timerAddress = 0U;
  std::uint32_t actorStateAddress = 0U;
  if (context.Time == nullptr ||
      !ResolveEnKanbanStateField(state, kTimerOffset, &timerAddress) ||
      !ResolveEnKanbanStateField(state, kStateOffset, &actorStateAddress)) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  std::uint16_t timerBits = 0U;
  std::uint8_t actorState = 0U;
  if (!memory.ReadFast(timerAddress, &timerBits) ||
      !memory.ReadFast(actorStateAddress, &actorState)) {
    return FailRead();
  }
  const std::int16_t timer = std::bit_cast<std::int16_t>(timerBits);
  const std::uint32_t loadedTimer =
      static_cast<std::uint32_t>(static_cast<std::int32_t>(timer));
  if (actorState != kSourceState || state.r[0] != loadedTimer ||
      (state.cpsr & kConditionFlagsMask) !=
          SubtractConditionFlags(loadedTimer, 0U)) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  const auto advance =
      oot3d::gameplay::AdvanceActorAuthoredCountdownToTransition(
          timer, *context.Time);
  const bool transitions =
      advance.Action ==
      oot3d::gameplay::ActorAuthoredCountdownAction::Transition;
  if ((advance.TimerMutated &&
       !memory.IsWritable(timerAddress, sizeof(std::uint16_t))) ||
      (transitions &&
       !memory.IsWritable(actorStateAddress, sizeof(std::uint8_t)))) {
    return FailWrite();
  }
  if (advance.TimerMutated &&
      !memory.WriteFast(timerAddress,
                        std::bit_cast<std::uint16_t>(advance.Timer))) {
    return FailWrite();
  }
  if (transitions &&
      !memory.WriteFast(actorStateAddress, kTransitionState)) {
    return FailWrite();
  }

  if (advance.TimerMutated) {
    const std::uint32_t decremented =
        static_cast<std::uint32_t>(
            static_cast<std::int32_t>(advance.Timer));
    state.r[0] = decremented;
    state.cpsr =
        (state.cpsr & ~kConditionFlagsMask) |
        SubtractConditionFlags(decremented, 0U);
    ++gStats.EnKanbanPieceLifetimeLogicalDecrements;
  }
  if (transitions) {
    // Native MOV does not alter the flags produced by the preceding CMP.
    state.r[0] = kTransitionState;
    ++gStats.EnKanbanPieceLifetimeStateTransitions;
    if (advance.TimerMutated) {
      ++gStats.EnKanbanPieceLifetimeZeroCrossingTransitions;
    } else {
      ++gStats.EnKanbanPieceLifetimeExistingZeroTransitions;
    }
  } else if (!context.Time->CrossedLogicalFrame) {
    ++gStats.EnKanbanPieceLifetimeIntermediateHolds;
  }
  ++gStats.EnKanbanPieceLifetimeBlockCalls;

  return CompleteActorTemporalBlock(
      kOot3dEnKanbanPieceLifetimeBlock,
      kOot3dEnKanbanPieceLifetimeContinue, state, result, blocksConsumed);
}

bool ExecuteEnKanbanRippleEventGateBlock(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  constexpr std::uint32_t kPhaseOffset = 0x1A8U;
  constexpr std::uint32_t kNzFlagsMask =
      oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagZ;

  std::uint32_t phaseAddress = 0U;
  std::uint8_t phase = 0U;
  if (context.Time == nullptr || state.r[4] == 0U ||
      !CheckedAddress(state.r[4], kPhaseOffset, &phaseAddress)) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }
  if (!memory.ReadFast(phaseAddress, &phase)) {
    return FailRead();
  }

  state.r[1] = phase;
  const std::uint32_t testValue = state.r[0] & state.r[1];
  state.cpsr =
      (state.cpsr & ~kNzFlagsMask) |
      (testValue == 0U ? oot3d::recomp::a32::kFlagZ : 0U) |
      (testValue & oot3d::recomp::a32::kFlagN);

  const bool nativeCondition = testValue == 0U;
  const bool dispatch = oot3d::gameplay::ShouldDispatchActorAuthoredEvent(
      nativeCondition, *context.Time);
  if (dispatch) {
    ++gStats.EnKanbanRippleDispatches;
  } else if (nativeCondition && !context.Time->CrossedLogicalFrame) {
    ++gStats.EnKanbanRippleIntermediateSuppressions;
  }
  ++gStats.EnKanbanRippleGateCalls;

  return CompleteActorTemporalBlock(
      kOot3dEnKanbanRippleEventGateBlock,
      dispatch ? kOot3dEnKanbanRippleEventPath
               : kOot3dEnKanbanRippleEventSkip,
      state, result, blocksConsumed);
}

bool ValidateEnKoBlinkState(const oot3d::recomp::a32::GuestState &state,
                            std::uint32_t *timerAddress,
                            std::uint32_t *sequenceAddress) noexcept {
  constexpr std::uint32_t kActorStateBaseOffset = 0x200U;
  constexpr std::uint32_t kBlinkTimerOffset = 0x2B8U;
  constexpr std::uint32_t kBlinkSequenceOffset = 0x2BAU;

  std::uint32_t expectedStateBase = 0U;
  return state.r[4] != 0U &&
         CheckedAddress(state.r[4], kActorStateBaseOffset,
                        &expectedStateBase) &&
         state.r[11] == expectedStateBase &&
         CheckedAddress(state.r[4], kBlinkTimerOffset, timerAddress) &&
         CheckedAddress(state.r[4], kBlinkSequenceOffset, sequenceAddress);
}

bool ExecuteEnKoBlinkAdvanceBlock(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  constexpr std::uint16_t kSequenceLength = 4U;
  constexpr std::uint32_t kTimerBase = 30U;
  constexpr std::uint32_t kTimerRange = 30U;
  constexpr std::uint32_t kConditionFlagsMask =
      oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagZ |
      oot3d::recomp::a32::kFlagC | oot3d::recomp::a32::kFlagV;

  std::uint32_t timerAddress = 0U;
  std::uint32_t sequenceAddress = 0U;
  if (context.Time == nullptr ||
      !ValidateEnKoBlinkState(state, &timerAddress, &sequenceAddress)) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  std::uint16_t timerBits = 0U;
  std::uint16_t sequenceIndex = 0U;
  if (!memory.ReadFast(timerAddress, &timerBits) ||
      !memory.ReadFast(sequenceAddress, &sequenceIndex)) {
    return FailRead();
  }
  const std::int16_t timer = std::bit_cast<std::int16_t>(timerBits);
  const auto advance = oot3d::gameplay::AdvanceActorBlinkState(
      timer, sequenceIndex, kSequenceLength, *context.Time);
  if (!advance.Valid) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  if ((advance.TimerMutated &&
       !memory.IsWritable(timerAddress, sizeof(std::uint16_t))) ||
      (advance.SequenceMutated &&
       !memory.IsWritable(sequenceAddress, sizeof(std::uint16_t)))) {
    return FailWrite();
  }
  if (advance.TimerMutated &&
      !memory.WriteFast(timerAddress,
                        std::bit_cast<std::uint16_t>(advance.Timer))) {
    return FailWrite();
  }
  if (advance.SequenceMutated &&
      !memory.WriteFast(sequenceAddress, advance.SequenceIndex)) {
    return FailWrite();
  }

  std::uint32_t continuation = kOot3dEnKoBlinkContinue;
  if (advance.Action ==
      oot3d::gameplay::ActorBlinkAction::DispatchRandom) {
    state.cpsr =
        (state.cpsr & ~kConditionFlagsMask) |
        SubtractConditionFlags(kSequenceLength, kSequenceLength);
    state.r[0] = kTimerBase;
    state.r[1] = kTimerRange;
    state.r[14] = kOot3dEnKoBlinkRngReturnBlock;
    continuation = kOot3dEnKoBlinkRngEntry;
  } else if (advance.Action ==
             oot3d::gameplay::ActorBlinkAction::Continue) {
    const std::int32_t comparison =
        advance.SequenceMutated
            ? static_cast<std::int32_t>(
                  std::bit_cast<std::int16_t>(advance.SequenceIndex))
            : static_cast<std::int32_t>(advance.Timer);
    state.r[0] = static_cast<std::uint32_t>(comparison);
    state.cpsr =
        (state.cpsr & ~kConditionFlagsMask) |
        SubtractConditionFlags(state.r[0],
                               advance.SequenceMutated
                                   ? static_cast<std::uint32_t>(kSequenceLength)
                                   : 0U);
  }

  ++gStats.EnKoBlinkBlockCalls;
  if (advance.Action == oot3d::gameplay::ActorBlinkAction::Hold) {
    ++gStats.EnKoBlinkIntermediateHolds;
  } else {
    ++gStats.EnKoBlinkLogicalAdvances;
  }
  gStats.EnKoBlinkTimerAdvances +=
      static_cast<std::uint64_t>(advance.TimerMutated);
  gStats.EnKoBlinkSequenceAdvances +=
      static_cast<std::uint64_t>(advance.SequenceMutated);
  if (advance.Action ==
      oot3d::gameplay::ActorBlinkAction::DispatchRandom) {
    ++gStats.EnKoBlinkRngDispatches;
  }
  return CompleteActorTemporalBlock(
      kOot3dEnKoBlinkAdvanceBlock, continuation, state, result,
      blocksConsumed);
}

bool ExecuteEnKoBlinkRngReturnBlock(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed) {
  std::uint32_t timerAddress = 0U;
  std::uint32_t sequenceAddress = 0U;
  if (!ValidateEnKoBlinkState(state, &timerAddress, &sequenceAddress)) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }
  std::uint16_t sequenceIndex = 0U;
  if (!memory.ReadFast(sequenceAddress, &sequenceIndex)) {
    return FailRead();
  }
  if (sequenceIndex != 0U) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }
  const std::uint16_t timer = static_cast<std::uint16_t>(state.r[0]);
  if (!memory.WriteFast(timerAddress, timer)) {
    return FailWrite();
  }

  ++gStats.EnKoBlinkRngReturns;
  return CompleteActorTemporalBlock(
      kOot3dEnKoBlinkRngReturnBlock, kOot3dEnKoBlinkContinue, state, result,
      blocksConsumed);
}

bool ExecuteActorUpdateAllContextFreezeBlock(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  constexpr std::uint32_t kTimerOffset = 0x02U;
  constexpr std::uint32_t kConditionFlagsMask =
      oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagZ |
      oot3d::recomp::a32::kFlagC | oot3d::recomp::a32::kFlagV;

  std::uint32_t timerAddress = 0U;
  std::uint8_t timer = 0U;
  const std::uint32_t actorContext = state.r[0];
  if (context.Time == nullptr || actorContext == 0U ||
      !CheckedAddress(actorContext, kTimerOffset, &timerAddress)) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }
  if (!memory.ReadFast(timerAddress, &timer)) {
    return FailRead();
  }

  const auto advance =
      oot3d::gameplay::AdvanceActorContextFreezeTimer(timer, *context.Time);
  if (advance.Mutated &&
      !memory.WriteFast(timerAddress, advance.Value)) {
    return FailWrite();
  }

  // The native block reloads the context into r1 only on the nonzero path.
  state.r[0] = advance.Value;
  if (timer != 0U) {
    state.r[1] = actorContext;
  }
  state.cpsr =
      (state.cpsr & ~kConditionFlagsMask) |
      SubtractConditionFlags(static_cast<std::uint32_t>(timer), 0U);

  ++gStats.ActorUpdateAllContextFreezeBlockCalls;
  if (advance.Mutated) {
    ++gStats.ActorUpdateAllContextFreezeLogicalAdvances;
  } else if (timer != 0U && !context.Time->CrossedLogicalFrame) {
    ++gStats.ActorUpdateAllContextFreezeIntermediateHolds;
  }
  return CompleteActorTemporalBlock(
      kOot3dActorUpdateAllContextFreezeBlock,
      kOot3dActorUpdateAllContextFreezeContinue, state, result,
      blocksConsumed);
}

bool ExecuteActorUpdateAllInstanceFreezeBlock(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  constexpr std::uint32_t kTimerBaseOffset = 0x100U;
  constexpr std::uint32_t kTimerOffset = 0x118U;
  constexpr std::uint32_t kConditionFlagsMask =
      oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagZ |
      oot3d::recomp::a32::kFlagC | oot3d::recomp::a32::kFlagV;

  std::uint32_t expectedTimerBase = 0U;
  std::uint32_t timerAddress = 0U;
  if (context.Time == nullptr || state.r[4] == 0U ||
      !CheckedAddress(state.r[4], kTimerBaseOffset, &expectedTimerBase) ||
      state.r[0] != expectedTimerBase ||
      !CheckedAddress(state.r[4], kTimerOffset, &timerAddress)) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  std::uint16_t timer = 0U;
  if (!memory.ReadFast(timerAddress, &timer)) {
    return FailRead();
  }
  const auto advance =
      oot3d::gameplay::AdvanceActorInstanceFreezeTimer(timer, *context.Time);
  if (advance.Mutated &&
      !memory.WriteFast(timerAddress, advance.Value)) {
    return FailWrite();
  }

  state.r[1] = advance.Value;
  if (timer == 0U) {
    state.cpsr =
        (state.cpsr & ~kConditionFlagsMask) |
        SubtractConditionFlags(0U, 0U);
  } else {
    // LSL/LSRS zero-extends the decremented halfword, sets N/Z/C, and
    // preserves V. On an intermediate hold the direct skip consumes no flags,
    // so publishing the held value's equivalent flags is deterministic.
    const std::uint32_t flags =
        (state.cpsr & oot3d::recomp::a32::kFlagV) |
        (advance.Value == 0U ? oot3d::recomp::a32::kFlagZ : 0U);
    state.cpsr = (state.cpsr & ~kConditionFlagsMask) | flags;
  }

  ++gStats.ActorUpdateAllInstanceFreezeBlockCalls;
  if (advance.Mutated) {
    ++gStats.ActorUpdateAllInstanceFreezeLogicalAdvances;
  } else if (timer != 0U && !context.Time->CrossedLogicalFrame) {
    ++gStats.ActorUpdateAllInstanceFreezeIntermediateHolds;
  }
  if (advance.PassesTimerGate) {
    ++gStats.ActorUpdateAllInstanceFreezeGatePasses;
  } else {
    ++gStats.ActorUpdateAllInstanceFreezeGateSkips;
  }
  const std::uint32_t continuation =
      advance.PassesTimerGate
          ? kOot3dActorUpdateAllInstanceFreezePass
          : kOot3dActorUpdateAllInstanceFreezeSkip;
  return CompleteActorTemporalBlock(
      kOot3dActorUpdateAllInstanceFreezeBlock, continuation, state, result,
      blocksConsumed);
}

bool ExecuteActorUpdateAllEffectTimersBlock(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  constexpr std::uint32_t kTimerBaseOffset = 0x100U;
  constexpr std::uint32_t kColorFilterTimerOffset = 0x11AU;
  constexpr std::uint32_t kSfxTimerOffset = 0x19CU;
  constexpr std::uint32_t kConditionFlagsMask =
      oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagZ |
      oot3d::recomp::a32::kFlagC | oot3d::recomp::a32::kFlagV;

  std::uint32_t expectedTimerBase = 0U;
  std::uint32_t colorTimerAddress = 0U;
  std::uint32_t sfxTimerAddress = 0U;
  if (context.Time == nullptr || state.r[4] == 0U ||
      !CheckedAddress(state.r[4], kTimerBaseOffset, &expectedTimerBase) ||
      state.r[0] != expectedTimerBase ||
      !CheckedAddress(state.r[4], kColorFilterTimerOffset,
                      &colorTimerAddress) ||
      !CheckedAddress(state.r[4], kSfxTimerOffset, &sfxTimerAddress)) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  std::uint16_t colorTimer = 0U;
  std::uint16_t sfxTimerBits = 0U;
  if (!memory.ReadFast(colorTimerAddress, &colorTimer) ||
      !memory.ReadFast(sfxTimerAddress, &sfxTimerBits)) {
    return FailRead();
  }
  const std::int16_t sfxTimer =
      std::bit_cast<std::int16_t>(sfxTimerBits);
  const auto advance = oot3d::gameplay::AdvanceActorEffectTimers(
      colorTimer, sfxTimer, *context.Time);
  if ((advance.ColorFilterMutated &&
       !memory.IsWritable(colorTimerAddress, sizeof(std::uint16_t))) ||
      (advance.SfxMutated &&
       !memory.IsWritable(sfxTimerAddress, sizeof(std::uint16_t)))) {
    return FailWrite();
  }
  if (advance.ColorFilterMutated &&
      !memory.WriteFast(colorTimerAddress, advance.ColorFilterTimer)) {
    return FailWrite();
  }
  if (advance.SfxMutated &&
      !memory.WriteFast(
          sfxTimerAddress,
          std::bit_cast<std::uint16_t>(advance.SfxTimer))) {
    return FailWrite();
  }

  // CMP precedes conditional SUBGT, which does not update flags.
  state.r[1] = static_cast<std::uint32_t>(
      static_cast<std::int32_t>(advance.SfxTimer));
  state.cpsr =
      (state.cpsr & ~kConditionFlagsMask) |
      SubtractConditionFlags(
          static_cast<std::uint32_t>(static_cast<std::int32_t>(sfxTimer)),
          0U);

  ++gStats.ActorUpdateAllEffectTimerBlockCalls;
  gStats.ActorUpdateAllEffectTimerLogicalFieldAdvances +=
      static_cast<std::uint64_t>(advance.ColorFilterMutated) +
      static_cast<std::uint64_t>(advance.SfxMutated);
  if (!context.Time->CrossedLogicalFrame) {
    gStats.ActorUpdateAllEffectTimerIntermediateFieldHolds +=
        static_cast<std::uint64_t>(colorTimer != 0U) +
        static_cast<std::uint64_t>(sfxTimer > 0);
  }
  return CompleteActorTemporalBlock(
      kOot3dActorUpdateAllEffectTimersBlock,
      kOot3dActorUpdateAllEffectTimersContinue, state, result,
      blocksConsumed);
}

bool ExecuteCameraWaterDistortionTimerAdvanceBlock(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  if (context.Time == nullptr ||
      !ValidateCameraCheckWaterTimerState(state)) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  const std::uint32_t timerAddress =
      state.r[5] + kCameraWaterDistortionTimerOffset;
  std::uint16_t timerBits = 0U;
  if (!memory.ReadFast(timerAddress, &timerBits)) {
    return FailRead();
  }
  const std::int16_t timer = std::bit_cast<std::int16_t>(timerBits);
  // Nonpositive values take a separate scene-dependent branch in the
  // original function and remain owned by A32.
  if (timer <= 0) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  const auto advance =
      oot3d::gameplay::AdvanceCameraWaterDistortionTimer(timer, *context.Time);
  if (advance.Mutated &&
      !memory.WriteFast(timerAddress,
                        std::bit_cast<std::uint16_t>(advance.Value))) {
    return FailWrite();
  }

  state.r[0] = std::bit_cast<std::uint32_t>(
      static_cast<std::int32_t>(advance.Value));
  ++gStats.CameraWaterDistortionTimerBlockCalls;
  if (advance.Mutated) {
    ++gStats.CameraWaterDistortionTimerLogicalAdvances;
  } else {
    ++gStats.CameraWaterDistortionTimerIntermediateHolds;
  }
  return CompleteCameraTemporalBlock(
      kOot3dCameraWaterDistortionTimerAdvanceBlock,
      kOot3dCameraWaterDistortionTimerContinue, state, result,
      blocksConsumed);
}

bool ExecuteCameraWaterDistortionSampleBlock(
    std::uint32_t entry, oot3d::recomp::a32::GuestState &state,
    NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  if (context.Time == nullptr) {
    ++gStats.RetainedAotFallbacks;
    ++gStats.CameraWaterDistortionSampleFailures;
    return false;
  }
  if (context.Time->CrossedLogicalFrame) {
    return false;
  }
  if (!ValidateCameraUpdateState(state)) {
    ++gStats.RetainedAotFallbacks;
    ++gStats.CameraWaterDistortionSampleFailures;
    return false;
  }

  std::uint16_t timerBits = 0U;
  if (!memory.ReadFast(state.r[4] + kCameraWaterDistortionTimerOffset,
                       &timerBits)) {
    ++gStats.CameraWaterDistortionSampleFailures;
    return FailRead();
  }
  const std::int16_t timer = std::bit_cast<std::int16_t>(timerBits);
  if (timer <= 0) {
    ++gStats.RetainedAotFallbacks;
    ++gStats.CameraWaterDistortionSampleFailures;
    return false;
  }

  const float sample =
      oot3d::gameplay::SampleCameraWaterDistortionTimer(timer, *context.Time);
  if (!std::isfinite(sample) || sample == static_cast<float>(timer)) {
    return false;
  }
  const std::uint32_t sampleBits = std::bit_cast<std::uint32_t>(sample);
  const std::uint32_t signedTimer = std::bit_cast<std::uint32_t>(
      static_cast<std::int32_t>(timer));
  std::uint32_t exceptionFlags = 0U;

  switch (entry) {
  case kOot3dCameraWaterDistortionFlag4SampleBlock: {
    if ((state.r[1] & 0x4U) == 0U) {
      ++gStats.RetainedAotFallbacks;
      ++gStats.CameraWaterDistortionSampleFailures;
      return false;
    }
    std::uint32_t baseBits = 0U;
    std::uint32_t scaleBits = 0U;
    if (!memory.ReadFast(kCameraWaterFlag4BaseLiteral, &baseBits) ||
        !memory.ReadFast(kCameraWaterFlag4ScaleLiteral, &scaleBits)) {
      ++gStats.CameraWaterDistortionSampleFailures;
      return FailRead();
    }
    const auto scaled = oot3d::recomp::a32::VfpBinary32Multiply(
        sampleBits, scaleBits, state.fpscr);
    const std::uint32_t incomingS16 = state.vfp[16];
    const std::uint32_t incomingS19 = state.vfp[19];
    state.r[0] = signedTimer;
    state.vfp[0] = baseBits;
    state.vfp[1] = scaleBits;
    state.vfp[2] = sampleBits;
    state.vfp[16] = scaled.value;
    state.vfp[18] = incomingS19;
    state.vfp[19] = incomingS16;
    state.vfp[20] = incomingS16;
    state.vfp[21] = incomingS16;
    state.vfp[22] = incomingS16;
    state.vfp[26] = incomingS16;
    exceptionFlags = scaled.exception_flags;
    ++gStats.CameraWaterDistortionFlag4Samples;
    break;
  }
  case kOot3dCameraWaterDistortionFlag8SampleBlock: {
    if ((state.r[1] & 0x4U) != 0U || (state.r[1] & 0x8U) == 0U) {
      ++gStats.RetainedAotFallbacks;
      ++gStats.CameraWaterDistortionSampleFailures;
      return false;
    }
    std::array<std::uint32_t, 7> literals{};
    constexpr std::array addresses{
        kCameraWaterFlag8Field26Literal, kCameraWaterFlag8Field0Literal,
        kCameraWaterFlag8Field23Literal, kCameraWaterFlag8Field18Literal,
        kCameraWaterFlag8Field19Literal, kCameraWaterFlag8Field24Literal,
        kCameraWaterFlag8ScaleLiteral,
    };
    for (std::size_t index = 0U; index < addresses.size(); ++index) {
      if (!memory.ReadFast(addresses[index], &literals[index])) {
        ++gStats.CameraWaterDistortionSampleFailures;
        return FailRead();
      }
    }
    const auto scaled = oot3d::recomp::a32::VfpBinary32Multiply(
        sampleBits, literals[6], state.fpscr);
    const std::uint32_t incomingS16 = state.vfp[16];
    state.r[0] = signedTimer;
    state.vfp[0] = literals[1];
    state.vfp[1] = literals[6];
    state.vfp[2] = sampleBits;
    state.vfp[16] = scaled.value;
    state.vfp[18] = literals[3];
    state.vfp[19] = literals[4];
    state.vfp[20] = incomingS16;
    state.vfp[21] = incomingS16;
    state.vfp[22] = incomingS16;
    state.vfp[23] = literals[2];
    state.vfp[24] = literals[5];
    state.vfp[26] = literals[0];
    exceptionFlags = scaled.exception_flags;
    ++gStats.CameraWaterDistortionFlag8Samples;
    break;
  }
  case kOot3dCameraWaterDistortionCustomSampleBlock: {
    const auto convertedField =
        oot3d::recomp::a32::VfpBinary32FromSigned(state.vfp[2],
                                                  state.fpscr);
    const auto scaledField = oot3d::recomp::a32::VfpBinary32Multiply(
        convertedField.value, state.vfp[1], state.fpscr);
    const auto convertedDivisor =
        oot3d::recomp::a32::VfpBinary32FromSigned(state.r[0], state.fpscr);
    const auto amplitude = oot3d::recomp::a32::VfpBinary32Divide(
        sampleBits, convertedDivisor.value, state.fpscr);
    state.r[1] = signedTimer;
    state.vfp[1] = sampleBits;
    state.vfp[2] = convertedDivisor.value;
    state.vfp[16] = amplitude.value;
    state.vfp[24] = scaledField.value;
    exceptionFlags = convertedField.exception_flags |
                     scaledField.exception_flags |
                     convertedDivisor.exception_flags |
                     amplitude.exception_flags;
    ++gStats.CameraWaterDistortionCustomSamples;
    break;
  }
  default:
    return false;
  }

  state.fpscr |= exceptionFlags;
  ++gStats.CameraWaterDistortionFractionalSamples;
  return CompleteCameraTemporalBlock(
      entry, kOot3dCameraWaterDistortionSampleContinue, state, result,
      blocksConsumed);
}

bool ExecuteCameraFloorMissCounterAdvanceBlock(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  if (context.Time == nullptr || !ValidateCameraUpdateCounterState(state)) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  const std::uint32_t counterAddress =
      kCameraGlobalStateAddress + kCameraFloorMissCounterOffset;
  std::uint32_t counter = 0U;
  if (!memory.ReadFast(counterAddress, &counter)) {
    return FailRead();
  }

  const auto advance =
      oot3d::gameplay::AdvanceCameraFloorMissCounter(counter, *context.Time);
  if (advance.Mutated &&
      !memory.WriteFast(counterAddress, advance.Value)) {
    return FailWrite();
  }

  state.r[0] = advance.Value;
  ++gStats.CameraFloorMissCounterBlockCalls;
  if (advance.Mutated) {
    ++gStats.CameraFloorMissCounterLogicalAdvances;
  } else {
    ++gStats.CameraFloorMissCounterIntermediateHolds;
  }
  return CompleteCameraTemporalBlock(
      kOot3dCameraFloorMissCounterAdvanceBlock,
      kOot3dCameraFloorMissCounterContinue, state, result, blocksConsumed);
}

bool ExecuteCameraInterfaceDelayAdvanceBlock(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  if (context.Time == nullptr || !ValidateCameraUpdateCounterState(state) ||
      state.r[8] != kCameraInterfaceCommand) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  const std::uint32_t delayAddress =
      kCameraGlobalStateAddress + kCameraInterfaceDelayOffset;
  std::uint32_t delay = 0U;
  if (!memory.ReadFast(delayAddress, &delay)) {
    return FailRead();
  }
  if (delay == 0U || state.r[1] != delay) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  const auto advance =
      oot3d::gameplay::AdvanceCameraInterfaceDelay(delay, *context.Time);
  if (advance.Mutated && !memory.WriteFast(delayAddress, advance.Value)) {
    return FailWrite();
  }

  state.r[0] = advance.Value;
  ++gStats.CameraInterfaceDelayBlockCalls;
  if (advance.Mutated) {
    ++gStats.CameraInterfaceDelayLogicalAdvances;
  } else {
    ++gStats.CameraInterfaceDelayIntermediateHolds;
  }
  return CompleteCameraTemporalBlock(
      kOot3dCameraInterfaceDelayAdvanceBlock,
      kOot3dCameraInterfaceDelayContinue, state, result, blocksConsumed);
}

bool ExecuteCutsceneNormalFrameAdvanceBlock(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  const std::uint32_t cutsceneContextAddress = state.r[4];
  const std::uint32_t playAddress = state.r[7];
  std::uint32_t expectedPlayBase = 0U;
  if (context.Time == nullptr || cutsceneContextAddress == 0U ||
      playAddress == 0U ||
      !CheckedAddress(playAddress, 0x2000U, &expectedPlayBase) ||
      state.r[5] != expectedPlayBase) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  std::uint16_t frame = 0U;
  if (!ReadField(memory, cutsceneContextAddress,
                 offsetof(CutsceneContextWire, Frame), &frame)) {
    return FailRead();
  }
  const auto advance =
      oot3d::gameplay::AdvanceCutsceneNormalFrame(frame, *context.Time);

  if (advance.DispatchDiscreteCommands) {
    GuestPtr<CutsceneCommandStream> commandStream;
    std::array<std::uint32_t, 6> savedRegisters{};
    if (!ReadField(memory, playAddress,
                   offsetof(CutscenePlayStateWire, ActiveCutsceneData),
                   &commandStream)) {
      return FailRead();
    }
    for (std::size_t index = 0U; index < savedRegisters.size(); ++index) {
      std::uint32_t slot = 0U;
      if (!CheckedAddress(state.r[13], index * sizeof(std::uint32_t), &slot) ||
          !memory.ReadFast(slot, &savedRegisters[index])) {
        return FailRead();
      }
    }
    if (!CanWriteField<std::uint16_t>(
            memory, cutsceneContextAddress,
            offsetof(CutsceneContextWire, Frame)) ||
        !WriteField(memory, cutsceneContextAddress,
                    offsetof(CutsceneContextWire, Frame), frame)) {
      return FailWrite();
    }

    state.r[0] = playAddress;
    state.r[1] = cutsceneContextAddress;
    state.r[2] = commandStream.Address;
    for (std::size_t index = 0U; index < 5U; ++index) {
      state.r[4U + index] = savedRegisters[index];
    }
    state.r[14] = savedRegisters[5];
    state.r[13] +=
        static_cast<std::uint32_t>(savedRegisters.size() * sizeof(std::uint32_t));
    state.r[15] = kOot3dCutsceneProcessCommandsEntry;
  } else {
    state.r[15] = kOot3dCutsceneFrameAdvanceEpilogue;
  }

  *result = {
      oot3d::recomp::a32::ExitKind::Branch,
      state.r[15],
      oot3d::recomp::a32::FallbackReason::None,
      kOot3dCutsceneNormalFrameAdvanceBlock,
  };
  if (blocksConsumed != nullptr) {
    *blocksConsumed = 1U;
  }

  ++gStats.Calls;
  ++gStats.CutsceneCalls;
  ++gStats.CutsceneNormalFrameBlockCalls;
  if (advance.DispatchDiscreteCommands) {
    ++gStats.CutsceneLogicalFrameAdvances;
    ++gStats.CutsceneCommandDispatches;
  } else {
    ++gStats.CutsceneIntermediateHolds;
  }
  return true;
}

bool ExecuteEnvironmentPathInterpolationEntry(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  if (context.Time == nullptr) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  const std::uint32_t actorAddress = state.r[0];
  const std::uint32_t playAddress = state.r[1];
  const std::uint32_t cueIndex = state.r[2];
  std::uint16_t legacyFrame = 0U;
  std::uint8_t backendClockActive = 0U;
  if (!ReadField(memory, playAddress,
                 offsetof(CutscenePlayStateWire, Frame), &legacyFrame) ||
      !ReadField(memory, playAddress,
                 offsetof(CutsceneFrameOwnerPlayStateWire,
                          BackendClockActive),
                 &backendClockActive)) {
    return FailRead();
  }

  if (backendClockActive != 0U) {
    std::uint8_t sceneStateMode = 0U;
    std::uint8_t backendClockReady = 0U;
    std::int32_t backendClockTicks = -1;
    if (!ReadField(memory, playAddress,
                   offsetof(CutsceneFrameOwnerPlayStateWire, SceneStateMode),
                   &sceneStateMode) ||
        !ReadField(memory, playAddress,
                   offsetof(CutsceneFrameOwnerPlayStateWire,
                            BackendClockReady),
                   &backendClockReady) ||
        !ReadField(memory, playAddress,
                   offsetof(CutsceneFrameOwnerPlayStateWire,
                            BackendClockTicks),
                   &backendClockTicks)) {
      return FailRead();
    }
    if (sceneStateMode != 2U || backendClockReady == 0U ||
        backendClockTicks >= 0) {
      return false;
    }
  }

  const double continuousFrame =
      oot3d::gameplay::ResolveCutsceneContinuousFrame(
          legacyFrame, *context.Time);
  if (continuousFrame == static_cast<double>(legacyFrame)) {
    // Native30 and logical crossings retain the complete original A32 body.
    return false;
  }

  std::uint32_t cueSlotAddress = 0U;
  GuestPtr<CutsceneActorCueWire> cuePointer;
  CutsceneActorCueWire cue;
  if (actorAddress == 0U || playAddress == 0U ||
      !CheckedAddress(
          playAddress,
          static_cast<std::uint64_t>(
              offsetof(CutscenePlayStateWire, ActorCueSlots)) +
              static_cast<std::uint64_t>(cueIndex) *
                  sizeof(GuestPtr<CutsceneActorCueWire>),
          &cueSlotAddress) ||
      !ReadWireObject(memory, cueSlotAddress, &cuePointer) ||
      cuePointer.Address == 0U ||
      !ReadWireObject(memory, cuePointer.Address, &cue)) {
    return FailRead();
  }

  constexpr std::uint32_t frameSize = 64U;
  const std::uint32_t originalSp = state.r[13];
  if (originalSp < frameSize ||
      !memory.IsWritable(originalSp - frameSize, frameSize)) {
    return FailWrite();
  }

  const std::uint32_t pushedCore = originalSp - 12U;
  const std::uint32_t pushedVfp = originalSp - 36U;
  if (!memory.WriteFast(pushedCore, state.r[4]) ||
      !memory.WriteFast(pushedCore + 4U, state.r[5]) ||
      !memory.WriteFast(pushedCore + 8U, state.r[14])) {
    return FailWrite();
  }
  for (std::size_t lane = 0U; lane < 6U; ++lane) {
    if (!memory.WriteFast(pushedVfp + static_cast<std::uint32_t>(lane * 4U),
                          state.vfp[16U + lane])) {
      return FailWrite();
    }
  }

  const std::array<std::int32_t, 6> coordinates{
      cue.StartX, cue.StartY, cue.StartZ, cue.EndX, cue.EndY, cue.EndZ};
  for (std::size_t lane = 0U; lane < coordinates.size(); ++lane) {
    const auto converted = oot3d::recomp::a32::VfpBinary32FromSigned(
        std::bit_cast<std::uint32_t>(coordinates[lane]), state.fpscr);
    state.vfp[16U + lane] = converted.value;
    state.fpscr |= converted.exception_flags;
  }

  const float weight = oot3d::gameplay::InterpolateCutsceneCueWeight(
      cue.StartFrame, cue.EndFrame, continuousFrame);
  const std::int32_t duration =
      static_cast<std::int32_t>(cue.EndFrame) -
      static_cast<std::int32_t>(cue.StartFrame);
  state.r[0] = std::bit_cast<std::uint32_t>(weight);
  state.r[1] = std::bit_cast<std::uint32_t>(duration);
  state.r[2] = legacyFrame;
  state.r[4] = actorAddress;
  state.r[5] = state.r[3];
  state.r[13] = originalSp - frameSize;
  state.r[14] = kOot3dEnvironmentPathInterpolationContinue;
  state.r[15] = kOot3dEnvironmentPathInterpolationContinue;
  state.vfp[0] = std::bit_cast<std::uint32_t>(weight);
  state.vfp[1] = std::bit_cast<std::uint32_t>(
      static_cast<float>(cue.EndFrame) -
      static_cast<float>(continuousFrame));
  state.vfp[2] = std::bit_cast<std::uint32_t>(1.0F);
  constexpr std::uint32_t nzcvMask = 0xF0000000U;
  std::uint32_t durationCompareFlags = oot3d::recomp::a32::kFlagC;
  if (duration < 0) {
    durationCompareFlags = oot3d::recomp::a32::kFlagN;
  } else if (duration == 0) {
    durationCompareFlags =
        oot3d::recomp::a32::kFlagZ | oot3d::recomp::a32::kFlagC;
  }
  state.fpscr = (state.fpscr & ~nzcvMask) | durationCompareFlags;

  *result = {
      oot3d::recomp::a32::ExitKind::Branch,
      kOot3dEnvironmentPathInterpolationContinue,
      oot3d::recomp::a32::FallbackReason::None,
      kOot3dEnvironmentPathInterpolateActorPosAndRotationEntry,
  };
  if (blocksConsumed != nullptr) {
    *blocksConsumed = 1U;
  }

  ++gStats.Calls;
  ++gStats.ActorCalls;
  ++gStats.CutsceneCalls;
  ++gStats.CutsceneActorCueInterpolationCalls;
  ++gStats.CutsceneActorCueFractionalSamples;
  return true;
}

bool ExecutePlayerCommonCountdownBlock(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  constexpr std::uint32_t kConditionFlagsMask =
      oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagZ |
      oot3d::recomp::a32::kFlagC | oot3d::recomp::a32::kFlagV;
  constexpr std::uint32_t kStateFlagActorCategoryA = 0x02000000U;
  constexpr std::uint32_t kStateFlagActorCategoryB = 0x20000000U;

  const std::uint32_t playerAddress = state.r[4];
  std::uint32_t expectedR5 = 0U;
  std::uint32_t expectedR6 = 0U;
  std::uint32_t expectedTimerBase = 0U;
  std::uint32_t actorContextAddress = 0U;
  std::uint32_t timerBaseSlot = 0U;
  std::uint32_t actorContextSlot = 0U;
  std::uint32_t timerBaseAddress = 0U;
  if (context.Time == nullptr || playerAddress == 0U ||
      !CheckedAddress(playerAddress, 0x1000U, &expectedR5) ||
      !CheckedAddress(playerAddress, 0x2000U, &expectedR6) ||
      !CheckedAddress(playerAddress, 0x2400U, &expectedTimerBase) ||
      !CheckedAddress(state.r[10], 0x208CU, &actorContextAddress) ||
      !CheckedAddress(state.r[13], 0x40U, &timerBaseSlot) ||
      !CheckedAddress(state.r[13], 0x3CU, &actorContextSlot) ||
      !memory.ReadFast(timerBaseSlot, &timerBaseAddress) ||
      state.r[6] != expectedR6 || timerBaseAddress != expectedTimerBase) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  PlayerCommonCountdownState countdowns;
  std::uint32_t stateFlags = 0U;
  if (!ReadField(memory, playerAddress,
                 offsetof(PlayerWireState, ItemActionCooldownTimer),
                 &countdowns.ItemActionCooldownTimer) ||
      !ReadField(memory, playerAddress,
                 offsetof(PlayerWireState, TextboxButtonCooldownTimer),
                 &countdowns.TextboxButtonCooldownTimer) ||
      !ReadField(memory, playerAddress,
                 offsetof(PlayerWireState, FairyReviveGraceTimer),
                 &countdowns.FairyReviveGraceTimer) ||
      !ReadField(memory, playerAddress,
                 offsetof(PlayerWireState, CollisionSfxCooldownTimer),
                 &countdowns.CollisionSfxCooldownTimer) ||
      !ReadField(memory, playerAddress,
                 offsetof(PlayerWireState, StateFlags), &stateFlags)) {
    return FailRead();
  }
  const PlayerCommonCountdownState previous = countdowns;

  oot3d::gameplay::PlayerUpdateCommonCountdowns(countdowns, *context.Time);
  if ((countdowns.ItemActionCooldownTimer !=
           previous.ItemActionCooldownTimer &&
       !WriteField(memory, playerAddress,
                   offsetof(PlayerWireState, ItemActionCooldownTimer),
                   countdowns.ItemActionCooldownTimer)) ||
      (countdowns.TextboxButtonCooldownTimer !=
           previous.TextboxButtonCooldownTimer &&
       !WriteField(memory, playerAddress,
                   offsetof(PlayerWireState, TextboxButtonCooldownTimer),
                   countdowns.TextboxButtonCooldownTimer)) ||
      (countdowns.FairyReviveGraceTimer !=
           previous.FairyReviveGraceTimer &&
       !WriteField(memory, playerAddress,
                   offsetof(PlayerWireState, FairyReviveGraceTimer),
                   countdowns.FairyReviveGraceTimer)) ||
      (countdowns.CollisionSfxCooldownTimer !=
           previous.CollisionSfxCooldownTimer &&
       !WriteField(memory, playerAddress,
                   offsetof(PlayerWireState, CollisionSfxCooldownTimer),
                   countdowns.CollisionSfxCooldownTimer)) ||
      !memory.WriteFast(actorContextSlot, actorContextAddress)) {
    return FailWrite();
  }

  const bool actorBranch =
      (stateFlags & kStateFlagActorCategoryA) != 0U &&
      (stateFlags & kStateFlagActorCategoryB) != 0U;
  const std::uint32_t nextPc =
      actorBranch ? kOot3dPlayerCommonCountdownActorBranch
                  : kOot3dPlayerInvincibilityTimerBlock;
  state.cpsr =
      (state.cpsr & ~kConditionFlagsMask) |
      (actorBranch ? 0U : oot3d::recomp::a32::kFlagZ);
  state.r[0] = stateFlags;
  state.r[1] = actorContextAddress;
  state.r[5] = expectedR5;
  state.r[15] = nextPc;
  *result = {
      oot3d::recomp::a32::ExitKind::Branch,
      nextPc,
      oot3d::recomp::a32::FallbackReason::None,
      kOot3dPlayerCommonCountdownBlock,
  };
  if (blocksConsumed != nullptr) {
    *blocksConsumed = 1U;
  }

  const std::array previousLogical{
      previous.ItemActionCooldownTimer,
      previous.TextboxButtonCooldownTimer,
      previous.CollisionSfxCooldownTimer,
  };
  const std::array currentLogical{
      countdowns.ItemActionCooldownTimer,
      countdowns.TextboxButtonCooldownTimer,
      countdowns.CollisionSfxCooldownTimer,
  };
  ++gStats.Calls;
  ++gStats.PlayerCalls;
  ++gStats.CommonCountdownBlockCalls;
  for (std::size_t index = 0U; index < previousLogical.size(); ++index) {
    if (currentLogical[index] != previousLogical[index]) {
      ++gStats.CommonCountdownLogicalFieldAdvances;
    } else if (previousLogical[index] != 0U &&
               !context.Time->CrossedLogicalFrame) {
      ++gStats.CommonCountdownLogicalFieldIntermediateHolds;
    }
  }
  if (countdowns.FairyReviveGraceTimer !=
      previous.FairyReviveGraceTimer) {
    ++gStats.FairyReviveGraceTimerAdvances;
  }
  return true;
}

bool ExecutePlayerInvincibilityTimerBlock(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  constexpr std::uint32_t kConditionFlagsMask =
      oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagZ |
      oot3d::recomp::a32::kFlagC | oot3d::recomp::a32::kFlagV;

  const std::uint32_t playerAddress = state.r[4];
  std::uint32_t expectedR5 = 0U;
  std::uint32_t expectedR6 = 0U;
  std::uint32_t expectedTimerBase = 0U;
  std::uint32_t timerBaseAddress = 0U;
  std::uint32_t timerBaseSlot = 0U;
  if (context.Time == nullptr || playerAddress == 0U ||
      !CheckedAddress(playerAddress, 0x1000U, &expectedR5) ||
      !CheckedAddress(playerAddress, 0x2000U, &expectedR6) ||
      !CheckedAddress(playerAddress, 0x2400U, &expectedTimerBase) ||
      !CheckedAddress(state.r[13], 0x40U, &timerBaseSlot) ||
      !memory.ReadFast(timerBaseSlot, &timerBaseAddress) ||
      state.r[5] != expectedR5 || state.r[6] != expectedR6 ||
      timerBaseAddress != expectedTimerBase) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  std::int8_t timer = 0;
  if (!ReadSignedByteField(memory, playerAddress,
                           offsetof(PlayerWireState, InvincibilityTimer),
                           &timer)) {
    return FailRead();
  }
  const std::int8_t previousTimer = timer;
  const std::uint32_t signedTimer = static_cast<std::uint32_t>(
      static_cast<std::int32_t>(previousTimer));
  std::uint32_t conditionFlags =
      SubtractConditionFlags(signedTimer, 0U);
  std::uint32_t nextR0 = timerBaseAddress;
  std::uint32_t nextR2 = state.r[2];
  bool holdPositiveTimer = false;

  if (previousTimer < 0) {
    nextR0 = signedTimer + 1U;
  } else if (previousTimer > 0) {
    std::uint32_t actionFunction = 0U;
    if (!ReadField(memory, playerAddress,
                   offsetof(PlayerWireState, ActionFunction),
                   &actionFunction)) {
      return FailRead();
    }
    nextR0 = actionFunction;
    for (const std::uint32_t literal :
         kPlayerInvincibilityHoldActionLiterals) {
      if (!memory.ReadFast(literal, &nextR2)) {
        return FailRead();
      }
      conditionFlags = SubtractConditionFlags(actionFunction, nextR2);
      if (actionFunction == nextR2) {
        holdPositiveTimer = true;
        break;
      }
    }
    if (!holdPositiveTimer) {
      nextR0 = signedTimer - 1U;
    }
  }

  oot3d::gameplay::PlayerUpdateInvincibilityTimer(
      timer, holdPositiveTimer, *context.Time);
  if (timer != previousTimer &&
      !WriteField(memory, playerAddress,
                  offsetof(PlayerWireState, InvincibilityTimer), timer)) {
    return FailWrite();
  }

  state.cpsr =
      (state.cpsr & ~kConditionFlagsMask) | conditionFlags;
  state.r[0] = nextR0;
  state.r[1] = signedTimer;
  state.r[2] = nextR2;
  state.r[15] = kOot3dPlayerDamageRunTimerBlock;
  *result = {
      oot3d::recomp::a32::ExitKind::Branch,
      kOot3dPlayerDamageRunTimerBlock,
      oot3d::recomp::a32::FallbackReason::None,
      kOot3dPlayerInvincibilityTimerBlock,
  };
  if (blocksConsumed != nullptr) {
    *blocksConsumed = 1U;
  }

  ++gStats.Calls;
  ++gStats.PlayerCalls;
  ++gStats.InvincibilityTimerBlockCalls;
  if (timer != previousTimer) {
    ++gStats.InvincibilityTimerLogicalAdvances;
  } else if (previousTimer != 0 && !context.Time->CrossedLogicalFrame) {
    ++gStats.InvincibilityTimerIntermediateHolds;
  }
  if (previousTimer > 0 && holdPositiveTimer) {
    ++gStats.InvincibilityTimerPositiveActionHolds;
  }
  return true;
}

bool ExecutePlayerDamageRunTimerBlock(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  constexpr std::uint32_t kConditionFlagsMask = 0xF0000000U;
  constexpr std::uint32_t kZeroFlag = 1U << 30U;
  constexpr std::uint32_t kCarryFlag = 1U << 29U;

  const std::uint32_t playerAddress = state.r[4];
  std::uint32_t expectedR6 = 0U;
  if (context.Time == nullptr || playerAddress == 0U ||
      !CheckedAddress(playerAddress, 0x2000U, &expectedR6) ||
      state.r[6] != expectedR6) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  std::uint8_t timer = 0U;
  if (!ReadField(memory, playerAddress,
                 offsetof(PlayerWireState, DamageRunTimer), &timer)) {
    return FailRead();
  }
  const std::uint8_t previousTimer = timer;
  oot3d::gameplay::PlayerUpdateDamageRunTimer(timer, *context.Time);
  if (timer != previousTimer &&
      !WriteField(memory, playerAddress,
                  offsetof(PlayerWireState, DamageRunTimer), timer)) {
    return FailWrite();
  }

  const std::uint32_t conditionFlags =
      kCarryFlag | (previousTimer == 0U ? kZeroFlag : 0U);
  state.cpsr =
      (state.cpsr & ~kConditionFlagsMask) | conditionFlags;
  state.r[0] = state.r[10];
  state.r[1] = state.r[4];
  state.r[14] = kOot3dPlayerDamageRunTimerReturn;
  state.r[15] = kOot3dPlayerUpdateContextActionAndSequenceStateEntry;
  *result = {
      oot3d::recomp::a32::ExitKind::Branch,
      kOot3dPlayerUpdateContextActionAndSequenceStateEntry,
      oot3d::recomp::a32::FallbackReason::None,
      kOot3dPlayerDamageRunTimerBlock,
  };
  if (blocksConsumed != nullptr) {
    *blocksConsumed = 1U;
  }

  ++gStats.Calls;
  ++gStats.PlayerCalls;
  ++gStats.DamageRunTimerBlockCalls;
  if (previousTimer != 0U) {
    if (context.Time->CrossedLogicalFrame) {
      ++gStats.DamageRunTimerLogicalAdvances;
    } else {
      ++gStats.DamageRunTimerIntermediateHolds;
    }
  }
  return true;
}

bool ExecutePlayerDamageFlickerCounterBlock(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  constexpr std::uint32_t kConditionFlagsMask =
      oot3d::recomp::a32::kFlagN | oot3d::recomp::a32::kFlagZ |
      oot3d::recomp::a32::kFlagC | oot3d::recomp::a32::kFlagV;

  const std::uint32_t playerAddress = state.r[4];
  std::uint32_t expectedR5 = 0U;
  if (context.Time == nullptr || playerAddress == 0U ||
      !CheckedAddress(playerAddress, 0x2000U, &expectedR5) ||
      state.r[5] != expectedR5) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  std::uint8_t counter = 0U;
  if (!ReadField(memory, playerAddress,
                 offsetof(PlayerWireState, DamageFlickerAnimationCounter),
                 &counter)) {
    return FailRead();
  }
  const std::uint8_t previousCounter = counter;
  const std::uint32_t authoredAdvance = state.r[11];
  const std::int32_t signedAdvance =
      std::bit_cast<std::int32_t>(authoredAdvance);
  std::uint32_t clampedAdvance = authoredAdvance;
  std::uint32_t conditionFlags = 0U;
  if (signedAdvance < 8) {
    conditionFlags = SubtractConditionFlags(authoredAdvance, 8U);
    clampedAdvance = 8U;
  } else {
    conditionFlags = SubtractConditionFlags(authoredAdvance, 40U);
    if (signedAdvance > 40) {
      clampedAdvance = 40U;
    }
  }

  oot3d::gameplay::PlayerAdvanceDamageFlickerAnimationCounter(
      counter, static_cast<std::uint8_t>(clampedAdvance), *context.Time);
  if (counter != previousCounter &&
      !WriteField(memory, playerAddress,
                  offsetof(PlayerWireState, DamageFlickerAnimationCounter),
                  counter)) {
    return FailWrite();
  }

  state.cpsr =
      (state.cpsr & ~kConditionFlagsMask) | conditionFlags;
  state.r[0] = static_cast<std::uint32_t>(previousCounter) + clampedAdvance;
  state.r[11] = clampedAdvance;
  state.r[15] = kOot3dPlayerDamageFlickerCounterContinue;
  *result = {
      oot3d::recomp::a32::ExitKind::Branch,
      kOot3dPlayerDamageFlickerCounterContinue,
      oot3d::recomp::a32::FallbackReason::None,
      kOot3dPlayerDamageFlickerCounterBlock,
  };
  if (blocksConsumed != nullptr) {
    *blocksConsumed = 1U;
  }

  ++gStats.Calls;
  ++gStats.PlayerCalls;
  ++gStats.DamageFlickerCounterBlockCalls;
  if (context.Time->CrossedLogicalFrame) {
    ++gStats.DamageFlickerCounterLogicalAdvances;
  } else {
    ++gStats.DamageFlickerCounterIntermediateHolds;
  }
  return true;
}

bool ExecutePlayerFishingStateRecoveryBlock(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  constexpr std::uint32_t kConditionFlagsMask = 0xF0000000U;
  constexpr std::uint32_t kNegativeFlag = 1U << 31U;
  constexpr std::uint32_t kZeroFlag = 1U << 30U;
  constexpr std::uint32_t kCarryFlag = 1U << 29U;

  const std::uint32_t playerAddress = state.r[4];
  std::uint32_t expectedR6 = 0U;
  std::uint32_t expectedR7 = 0U;
  std::uint32_t itemStateAddress = 0U;
  if (context.Time == nullptr ||
      state.r[0] != static_cast<std::uint32_t>(
                          oot3d::gameplay::kPlayerHeldItemActionFishingPole) ||
      !CheckedAddress(playerAddress, 0x2000U, &expectedR6) ||
      !CheckedAddress(playerAddress, 0x2200U, &expectedR7) ||
      !CheckedAddress(
          playerAddress,
          offsetof(PlayerWireState, ItemActionStateOrBurnTimer),
          &itemStateAddress) ||
      state.r[6] != expectedR6 || state.r[7] != expectedR7) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  std::uint16_t encodedState = 0U;
  if (!memory.ReadFast(itemStateAddress, &encodedState)) {
    return FailRead();
  }
  const std::int16_t previousState =
      std::bit_cast<std::int16_t>(encodedState);
  std::int16_t resolvedState = previousState;
  oot3d::gameplay::PlayerAdvanceFishingItemStateTowardReady(
      resolvedState,
      oot3d::gameplay::kPlayerHeldItemActionFishingPole,
      *context.Time);

  if (resolvedState != previousState &&
      !memory.WriteFast(itemStateAddress,
                        std::bit_cast<std::uint16_t>(resolvedState))) {
    return FailWrite();
  }

  std::uint32_t conditionFlags = kCarryFlag;
  if (previousState < 0) {
    conditionFlags |= kNegativeFlag;
    if (context.Time->CrossedLogicalFrame) {
      ++gStats.FishingStateLogicalAdvances;
    } else {
      ++gStats.FishingStateIntermediateHolds;
    }
  } else if (previousState == 0) {
    conditionFlags |= kZeroFlag;
  }
  state.cpsr =
      (state.cpsr & ~kConditionFlagsMask) | conditionFlags;
  state.r[0] = std::bit_cast<std::uint32_t>(
      static_cast<std::int32_t>(resolvedState));
  state.r[15] = kOot3dPlayerFishingStateRecoveryContinue;
  *result = {
      oot3d::recomp::a32::ExitKind::Branch,
      kOot3dPlayerFishingStateRecoveryContinue,
      oot3d::recomp::a32::FallbackReason::None,
      kOot3dPlayerFishingStateRecoveryBlock,
  };
  if (blocksConsumed != nullptr) {
    *blocksConsumed = 1U;
  }
  ++gStats.Calls;
  ++gStats.PlayerCalls;
  ++gStats.FishingStateBlockCalls;
  return true;
}

bool ExecutePlayerMeleeActionTimerBlock(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  constexpr std::uint32_t kConditionFlagsMask = 0xF0000000U;
  constexpr std::uint32_t kNegativeFlag = 1U << 31U;
  constexpr std::uint32_t kZeroFlag = 1U << 30U;
  constexpr std::uint32_t kCarryFlag = 1U << 29U;

  const std::uint32_t playerAddress = state.r[4];
  std::uint32_t expectedR6 = 0U;
  std::uint32_t expectedR7 = 0U;
  if (context.Time == nullptr || playerAddress == 0U ||
      !CheckedAddress(playerAddress, 0x2000U, &expectedR6) ||
      !CheckedAddress(playerAddress, 0x2200U, &expectedR7) ||
      state.r[6] != expectedR6 || state.r[7] != expectedR7) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  oot3d::gameplay::PlayerMeleeActionTimingState timing;
  if (!ReadSignedByteField(
          memory, playerAddress,
          offsetof(PlayerWireState, MeleeWeaponActionTimer), &timing.Timer) ||
      !ReadField(memory, playerAddress,
                 offsetof(PlayerWireState, MeleeWeaponComboState),
                 &timing.ComboState)) {
    return FailRead();
  }
  const auto previous = timing;
  oot3d::gameplay::PlayerUpdateMeleeActionTiming(timing, *context.Time);

  if (timing.Timer != previous.Timer &&
      !WriteField(memory, playerAddress,
                  offsetof(PlayerWireState, MeleeWeaponActionTimer),
                  std::bit_cast<std::uint8_t>(timing.Timer))) {
    return FailWrite();
  }
  if (timing.ComboState != previous.ComboState &&
      !WriteField(memory, playerAddress,
                  offsetof(PlayerWireState, MeleeWeaponComboState),
                  timing.ComboState)) {
    return FailWrite();
  }

  std::uint32_t conditionFlags = kCarryFlag;
  if (previous.Timer < 0) {
    conditionFlags |= kNegativeFlag;
  } else if (previous.Timer == 0) {
    conditionFlags |= kZeroFlag;
  }
  state.cpsr =
      (state.cpsr & ~kConditionFlagsMask) | conditionFlags;
  state.r[0] = std::bit_cast<std::uint32_t>(
      static_cast<std::int32_t>(timing.Timer));
  state.r[15] = kOot3dPlayerMeleeActionTimerContinue;
  *result = {
      oot3d::recomp::a32::ExitKind::Branch,
      kOot3dPlayerMeleeActionTimerContinue,
      oot3d::recomp::a32::FallbackReason::None,
      kOot3dPlayerMeleeActionTimerBlock,
  };
  if (blocksConsumed != nullptr) {
    *blocksConsumed = 1U;
  }

  ++gStats.Calls;
  ++gStats.PlayerCalls;
  ++gStats.MeleeActionTimerBlockCalls;
  if (previous.Timer == 0) {
    if (previous.ComboState != timing.ComboState) {
      ++gStats.MeleeActionComboClears;
    }
  } else if (context.Time->CrossedLogicalFrame) {
    ++gStats.MeleeActionTimerLogicalAdvances;
  } else {
    ++gStats.MeleeActionTimerIntermediateHolds;
  }
  return true;
}

bool ExecutePlayerMeleeWeaponTipComboAdvanceBlock(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  constexpr std::uint32_t kFpscrVectorModeMask = 0x00370000U;
  constexpr std::uint32_t kFpscrExceptionEnableMask = 0x00009F00U;
  if (context.Time == nullptr || state.r[0] < 0x2000U ||
      state.r[1] < 3U ||
      state.r[1] > std::numeric_limits<std::uint8_t>::max() ||
      (state.fpscr &
       (kFpscrVectorModeMask | kFpscrExceptionEnableMask)) != 0U) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  const std::uint32_t playerAddress = state.r[0] - 0x2000U;
  std::uint8_t comboState = 0U;
  if (!ReadField(memory, playerAddress,
                 offsetof(PlayerWireState, MeleeWeaponComboState),
                 &comboState)) {
    return FailRead();
  }
  if (comboState != state.r[1]) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  const std::uint8_t previousState = comboState;
  oot3d::gameplay::PlayerAdvanceMeleeWeaponTipComboState(comboState,
                                                         *context.Time);

  std::uint32_t stepBits = 0U;
  std::uint32_t oneBits = 0U;
  if (!memory.ReadFast(kPlayerMeleeWeaponTipStepLiteral, &stepBits) ||
      !memory.ReadFast(kPlayerMeleeWeaponTipOneLiteral, &oneBits)) {
    return FailRead();
  }
  if (!memory.IsWritable(state.r[2], sizeof(std::uint32_t))) {
    return FailWrite();
  }

  const std::uint32_t resolvedR0 = 9U - comboState;
  const auto converted = oot3d::recomp::a32::VfpBinary32FromSigned(
      resolvedR0, state.fpscr);
  const auto scale =
      oot3d::recomp::a32::VfpBinary32MultiplyAccumulate(
          oneBits, converted.value, stepBits, state.fpscr);
  const auto scaledTip = oot3d::recomp::a32::VfpBinary32Multiply(
      state.vfp[0], scale.value, state.fpscr);

  if (comboState != previousState &&
      !WriteField(memory, playerAddress,
                  offsetof(PlayerWireState, MeleeWeaponComboState),
                  comboState)) {
    return FailWrite();
  }
  if (!memory.WriteFast(state.r[2], scaledTip.value)) {
    return FailWrite();
  }

  state.r[0] = resolvedR0;
  state.r[1] = comboState;
  state.vfp[0] = scaledTip.value;
  state.vfp[1] = scale.value;
  state.vfp[2] = converted.value;
  state.vfp[3] = stepBits;
  state.fpscr |= converted.exception_flags | scale.exception_flags |
                 scaledTip.exception_flags;
  state.r[15] = kOot3dPlayerMeleeWeaponTipComboAdvanceContinue;
  *result = {
      oot3d::recomp::a32::ExitKind::Branch,
      kOot3dPlayerMeleeWeaponTipComboAdvanceContinue,
      oot3d::recomp::a32::FallbackReason::None,
      kOot3dPlayerMeleeWeaponTipComboAdvanceBlock,
  };
  if (blocksConsumed != nullptr) {
    *blocksConsumed = 1U;
  }

  ++gStats.Calls;
  ++gStats.PlayerCalls;
  ++gStats.MeleeWeaponTipComboBlockCalls;
  if (context.Time->CrossedLogicalFrame) {
    ++gStats.MeleeWeaponTipComboLogicalAdvances;
  } else {
    ++gStats.MeleeWeaponTipComboIntermediateHolds;
  }
  return true;
}

bool ResolvePlayerUnderwaterTimerAddress(
    const oot3d::recomp::a32::GuestState &state,
    std::uint32_t *timerAddress) {
  std::uint32_t expectedR7 = 0U;
  return state.r[4] != 0U &&
         CheckedAddress(state.r[4], 0x2200U, &expectedR7) &&
         expectedR7 == state.r[7] &&
         CheckedAddress(state.r[4], offsetof(PlayerWireState, UnderwaterTimer),
                        timerAddress);
}

bool CompletePlayerUnderwaterTimerBlock(
    std::uint32_t entry, oot3d::recomp::a32::GuestState &state,
    oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed) {
  state.r[15] = kOot3dPlayerUnderwaterTimerContinue;
  *result = {
      oot3d::recomp::a32::ExitKind::Branch,
      kOot3dPlayerUnderwaterTimerContinue,
      oot3d::recomp::a32::FallbackReason::None,
      entry,
  };
  if (blocksConsumed != nullptr) {
    *blocksConsumed = 1U;
  }
  ++gStats.Calls;
  ++gStats.PlayerCalls;
  return true;
}

bool ExecutePlayerUnderwaterTimerResetBlock(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  std::uint32_t timerAddress = 0U;
  std::uint16_t timer = 0U;
  if (context.Time == nullptr ||
      !ResolvePlayerUnderwaterTimerAddress(state, &timerAddress)) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }
  if (!memory.ReadFast(timerAddress, &timer)) {
    return FailRead();
  }

  oot3d::gameplay::PlayerUpdateUnderwaterTimer(timer, false, *context.Time);
  if (!memory.WriteFast(timerAddress, timer)) {
    return FailWrite();
  }
  state.r[0] = 0U;
  ++gStats.UnderwaterTimerResetBlockCalls;
  return CompletePlayerUnderwaterTimerBlock(
      kOot3dPlayerUnderwaterTimerResetBlock, state, result, blocksConsumed);
}

bool ExecutePlayerUnderwaterTimerIncrementBlock(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context,
    std::uint32_t *blocksConsumed) {
  std::uint32_t timerAddress = 0U;
  std::uint16_t timer = 0U;
  if (context.Time == nullptr ||
      !ResolvePlayerUnderwaterTimerAddress(state, &timerAddress)) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }
  if (!memory.ReadFast(timerAddress, &timer)) {
    return FailRead();
  }
  if (state.r[0] != timer ||
      state.r[1] !=
          oot3d::gameplay::kPlayerUnderwaterTimerSaturationLogicalFrames ||
      timer >=
          oot3d::gameplay::kPlayerUnderwaterTimerSaturationLogicalFrames) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }

  oot3d::gameplay::PlayerUpdateUnderwaterTimer(timer, true, *context.Time);
  if (!memory.WriteFast(timerAddress, timer)) {
    return FailWrite();
  }
  state.r[0] = timer;
  ++gStats.UnderwaterTimerIncrementBlockCalls;
  if (context.Time->CrossedLogicalFrame) {
    ++gStats.UnderwaterTimerLogicalAdvances;
  } else {
    ++gStats.UnderwaterTimerIntermediateHolds;
  }
  return CompletePlayerUnderwaterTimerBlock(
      kOot3dPlayerUnderwaterTimerIncrementBlock, state, result,
      blocksConsumed);
}

bool ExecutePlayerEquipmentQuery(
    std::uint32_t entry, oot3d::recomp::a32::GuestState &state,
    NativeA32Memory &memory, oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed) {
  PlayerEquipmentState player;
  switch (entry) {
  case kOot3dPlayerGetExplosiveHeldEntry:
  case kOot3dPlayerHoldsHookshotEntry:
  case kOot3dPlayerHoldsTwoHandedWeaponEntry:
  case kOot3dPlayerHoldsHookshotWithoutHeldActorEntry:
    if (!ReadSignedByteField(memory, state.r[0],
                             offsetof(PlayerWireState, HeldItemAction),
                             &player.HeldItemAction)) {
      return FailRead();
    }
    break;
  case kOot3dPlayerIsItemInHandEntry:
    if (!ReadField(memory, state.r[0], offsetof(PlayerWireState, StateFlags),
                   &player.StateFlags)) {
      return FailRead();
    }
    break;
  default:
    return false;
  }

  if (entry == kOot3dPlayerHoldsHookshotWithoutHeldActorEntry) {
    GuestPtr<Actor> heldActor;
    if (!ReadField(memory, state.r[0], offsetof(PlayerWireState, HeldActor),
                   &heldActor)) {
      return FailRead();
    }
    player.HasHeldActor = heldActor.Address != 0U;
  }

  if (entry == kOot3dPlayerGetExplosiveHeldEntry) {
    state.r[0] = std::bit_cast<std::uint32_t>(
        oot3d::gameplay::PlayerGetExplosiveHeld(player));
  } else if (entry == kOot3dPlayerHoldsHookshotEntry) {
    state.r[0] =
        static_cast<std::uint32_t>(oot3d::gameplay::PlayerHoldsHookshot(player));
  } else if (entry == kOot3dPlayerHoldsTwoHandedWeaponEntry) {
    state.r[0] = static_cast<std::uint32_t>(
        oot3d::gameplay::PlayerHoldsTwoHandedWeapon(player));
  } else if (entry == kOot3dPlayerHoldsHookshotWithoutHeldActorEntry) {
    state.r[0] = static_cast<std::uint32_t>(
        oot3d::gameplay::PlayerHasFreeHookshotHand(player));
  } else {
    state.r[0] = oot3d::gameplay::PlayerItemInHandMask(player);
  }
  return CompletePlayerCall(entry, state, result, blocksConsumed);
}

bool ExecutePlayerSetRuntimeFlag200(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed) {
  const std::uint32_t playerAddress = state.r[0];
  std::uint32_t runtimeFlags = 0U;
  if (!ReadField(memory, playerAddress,
                 offsetof(PlayerWireState, RuntimeFlags), &runtimeFlags)) {
    return FailRead();
  }
  if (!CanWriteField<std::uint32_t>(memory, playerAddress,
                                    offsetof(PlayerWireState, RuntimeFlags))) {
    return FailWrite();
  }
  oot3d::gameplay::PlayerSetRuntimeFlag200(runtimeFlags, state.r[1] == 1U);
  if (!WriteField(memory, playerAddress,
                  offsetof(PlayerWireState, RuntimeFlags), runtimeFlags)) {
    return FailWrite();
  }
  state.r[0] = playerAddress + 0x2000U;
  return CompletePlayerCall(kOot3dPlayerSetRuntimeFlag200Entry, state, result,
                            blocksConsumed);
}

bool ExecutePlayerSetCutsceneAction(
    std::uint32_t entry, oot3d::recomp::a32::GuestState &state,
    NativeA32Memory &memory, oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed) {
  std::uint32_t playerAddress = 0U;
  if (!ReadPlayerFromPlay(memory, state.r[0], &playerAddress)) {
    return FailRead();
  }
  if (!CanWriteField<std::uint8_t>(
          memory, playerAddress,
          offsetof(PlayerWireState, CutsceneActionMode)) ||
      !CanWriteField<GuestPtr<oot3d::gameplay::GuestFunction>>(
          memory, playerAddress, offsetof(PlayerWireState, CutsceneAction)) ||
      !CanWriteField<std::uint16_t>(memory, playerAddress,
                                    offsetof(PlayerWireState, HaltActors))) {
    return FailWrite();
  }

  PlayerCutsceneActionState actionState;
  oot3d::gameplay::PlayerSetCutsceneAction(
      actionState, state.r[1], static_cast<std::uint8_t>(state.r[2]),
      entry == kOot3dPlayerSetCsActionWithHaltedActorsEntry);
  const GuestPtr<oot3d::gameplay::GuestFunction> action{actionState.Action};
  if (!WriteField(memory, playerAddress,
                  offsetof(PlayerWireState, CutsceneActionMode),
                  actionState.Mode) ||
      !WriteField(memory, playerAddress,
                  offsetof(PlayerWireState, CutsceneAction), action) ||
      !WriteField(memory, playerAddress, offsetof(PlayerWireState, HaltActors),
                  actionState.HaltActors)) {
    return FailWrite();
  }
  state.r[0] = 1U;
  return CompletePlayerCall(entry, state, result, blocksConsumed);
}

bool ExecutePlayerUpdateHostileLockOn(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed) {
  const std::uint32_t playerAddress = state.r[0];
  GuestPtr<Actor> focusActor;
  PlayerLockOnState lockOn;
  float zero = 0.0f;
  constexpr std::size_t shapeYawOffset =
      offsetof(PlayerWireState, BaseActor) + offsetof(Actor, Shape) +
      offsetof(oot3d::gameplay::ActorShape, Rotation) +
      offsetof(oot3d::gameplay::Vec3s, Y);
  if (!ReadField(memory, playerAddress, offsetof(PlayerWireState, FocusActor),
                 &focusActor) ||
      !ReadField(memory, playerAddress, offsetof(PlayerWireState, StateFlags),
                 &lockOn.StateFlags) ||
      !ReadField(memory, playerAddress, offsetof(PlayerWireState, Speed),
                 &lockOn.Speed) ||
      !ReadField(memory, playerAddress, shapeYawOffset, &lockOn.ShapeYaw) ||
      !ReadField(memory, playerAddress, offsetof(PlayerWireState, LockOnYaw),
                 &lockOn.LockOnYaw) ||
      !ReadFloat(memory, kPlayerLockOnZeroLiteral, &zero)) {
    return FailRead();
  }
  lockOn.HasFocusActor = focusActor.Address != 0U;
  if (lockOn.HasFocusActor &&
      !ReadField(memory, focusActor.Address, offsetof(Actor, Flags),
                 &lockOn.FocusActorFlags)) {
    return FailRead();
  }
  if (!CanWriteField<std::uint32_t>(
          memory, playerAddress, offsetof(PlayerWireState, StateFlags)) ||
      !CanWriteField<std::int16_t>(
          memory, playerAddress, offsetof(PlayerWireState, LockOnYaw))) {
    return FailWrite();
  }

  const std::uint32_t previousFlags = lockOn.StateFlags;
  const std::int16_t previousYaw = lockOn.LockOnYaw;
  const bool active =
      oot3d::gameplay::PlayerUpdateHostileLockOn(lockOn, zero);
  if ((lockOn.StateFlags != previousFlags &&
       !WriteField(memory, playerAddress,
                   offsetof(PlayerWireState, StateFlags),
                   lockOn.StateFlags)) ||
      (lockOn.LockOnYaw != previousYaw &&
       !WriteField(memory, playerAddress,
                   offsetof(PlayerWireState, LockOnYaw),
                   lockOn.LockOnYaw))) {
    return FailWrite();
  }
  state.r[0] = static_cast<std::uint32_t>(active);
  return CompletePlayerCall(kOot3dPlayerUpdateHostileLockOnEntry, state, result,
                            blocksConsumed);
}

bool ExecutePlayerUpdateSwimVerticalVelocity(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed) {
  const std::uint32_t playerAddress = state.r[0];
  PlayerSwimVerticalState swim;
  PlayerSwimVerticalConstants constants;
  GuestPtr<PlayerMovementContextWire> movementContext;
  constexpr std::size_t velocityYOffset =
      offsetof(PlayerWireState, BaseActor) + offsetof(Actor, Velocity) +
      offsetof(oot3d::gameplay::Vec3f, Y);
  constexpr std::size_t gravityOffset =
      offsetof(PlayerWireState, BaseActor) + offsetof(Actor, Gravity);
  constexpr std::size_t depthInWaterOffset =
      offsetof(PlayerWireState, BaseActor) + offsetof(Actor, DepthInWater);
  constexpr std::size_t animationIndexOffset =
      offsetof(PlayerWireState, MainAnimation) +
      offsetof(SkelAnime, AnimationIndex);

  if (!ReadField(memory, playerAddress, velocityYOffset, &swim.VelocityY) ||
      !ReadField(memory, playerAddress, gravityOffset, &swim.Gravity) ||
      !ReadField(memory, playerAddress, depthInWaterOffset,
                 &swim.DepthInWater) ||
      !ReadField(memory, playerAddress,
                 offsetof(PlayerWireState, MovementContext),
                 &movementContext) ||
      movementContext.Address == 0U ||
      !ReadField(memory, movementContext.Address,
                 offsetof(PlayerMovementContextWire, SurfaceReference),
                 &swim.SurfaceReference) ||
      !ReadField(memory, playerAddress, animationIndexOffset,
                 &swim.AnimationIndex) ||
      !ReadField(memory, playerAddress, offsetof(PlayerWireState, StateFlags),
                 &swim.StateFlags) ||
      !ReadField(memory, playerAddress,
                 offsetof(PlayerWireState, SecondaryStateFlags),
                 &swim.SecondaryStateFlags) ||
      !ReadField(memory, playerAddress,
                 offsetof(PlayerWireState, MovementState),
                 &swim.MovementState) ||
      !ReadFloat(memory, kPlayerSwimTerminalLiteral,
                 &constants.DefaultTerminalVelocity) ||
      !ReadFloat(memory, kPlayerSwimZeroLiteral, &constants.Zero) ||
      !ReadFloat(memory, kPlayerSwimSurfaceOffsetLiteral,
                 &constants.DescendingSurfaceOffset) ||
      !ReadFloat(memory, kPlayerSwimDampingLiteral,
                 &constants.PositiveVelocityDamping) ||
      !ReadFloat(memory, kPlayerSwimSurfaceAccelerationLiteral,
                 &constants.SurfaceAcceleration) ||
      !ReadFloat(memory, kPlayerSwimSinkThresholdLiteral,
                 &constants.DefaultSinkThreshold) ||
      !ReadFloat(memory, kPlayerSwimAlternateSinkThresholdLiteral,
                 &constants.AlternateSinkThreshold) ||
      !ReadFloat(memory, kPlayerSwimSinkAccelerationLiteral,
                 &constants.SinkAcceleration) ||
      !ReadFloat(memory, kPlayerSwimRisingTerminalLiteral,
                 &constants.RisingTerminalVelocity) ||
      !ReadFloat(memory, kPlayerSwimRisingAccelerationLiteral,
                 &constants.RisingAcceleration) ||
      !ReadFloat(memory, kPlayerSwimDeepWaterThresholdLiteral,
                 &constants.DeepWaterThreshold)) {
    return FailRead();
  }
  if (!CanWriteField<float>(memory, playerAddress, velocityYOffset) ||
      !CanWriteField<float>(memory, playerAddress, gravityOffset) ||
      !CanWriteField<std::uint32_t>(
          memory, playerAddress,
          offsetof(PlayerWireState, SecondaryStateFlags))) {
    return FailWrite();
  }

  oot3d::gameplay::PlayerUpdateSwimVerticalVelocity(swim, constants);
  if (!WriteField(memory, playerAddress, velocityYOffset, swim.VelocityY) ||
      !WriteField(memory, playerAddress, gravityOffset, swim.Gravity) ||
      !WriteField(memory, playerAddress,
                  offsetof(PlayerWireState, SecondaryStateFlags),
                  swim.SecondaryStateFlags)) {
    return FailWrite();
  }
  return CompletePlayerCall(kOot3dPlayerUpdateSwimVerticalVelocityEntry, state,
                            result, blocksConsumed);
}

bool ExecutePlayerGetIdleAnimation(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed) {
  PlayerIdleAnimationState idle;
  std::uint32_t runtimeFlagsOffset = 0U;
  std::uint32_t animationTable = 0U;
  std::uint32_t transitionStateAddress = 0U;
  std::uint8_t animationType = 0U;
  std::uint32_t normalBits = 0U;
  std::uint32_t alternateBits = 0U;
  if (!memory.ReadFast(kPlayerIdleRuntimeFlagsOffsetLiteral,
                       &runtimeFlagsOffset) ||
      runtimeFlagsOffset != offsetof(PlayerWireState, RuntimeFlags) ||
      !memory.ReadFast(kPlayerIdleAnimationTableLiteral, &animationTable) ||
      !memory.ReadFast(kPlayerIdleTransitionStateLiteral,
                       &transitionStateAddress) ||
      !ReadField(memory, state.r[0], runtimeFlagsOffset, &idle.RuntimeFlags) ||
      !ReadField(memory, state.r[0],
                 offsetof(PlayerWireState, BackgroundState),
                 &idle.BackgroundState) ||
      !ReadField(memory, state.r[0],
                 offsetof(PlayerWireState, IdleAnimationType),
                 &animationType) ||
      !ReadSignedByteField(memory, transitionStateAddress, 0U,
                           &idle.GlobalTransitionState)) {
    return FailRead();
  }

  std::uint32_t normalAddress = 0U;
  std::uint32_t alternateAddress = 0U;
  const std::uint64_t indexOffset =
      static_cast<std::uint64_t>(animationType) * sizeof(std::uint32_t);
  if (!CheckedAddress(animationTable, indexOffset, &normalAddress) ||
      !CheckedAddress(animationTable, indexOffset + 0x4F8U,
                      &alternateAddress) ||
      !memory.ReadFast(normalAddress, &normalBits) ||
      !memory.ReadFast(alternateAddress, &alternateBits)) {
    return FailRead();
  }
  state.r[0] = std::bit_cast<std::uint32_t>(
      oot3d::gameplay::PlayerSelectIdleAnimation(
          idle, std::bit_cast<std::int32_t>(normalBits),
          std::bit_cast<std::int32_t>(alternateBits)));
  return CompletePlayerCall(kOot3dPlayerGetIdleAnimEntry, state, result,
                            blocksConsumed);
}

bool ExecutePlayerGetHeight(oot3d::recomp::a32::GuestState &state,
                            NativeA32Memory &memory,
                            oot3d::recomp::a32::ExecutionResult *result,
                            std::uint32_t *blocksConsumed) {
  PlayerHeightState height;
  PlayerHeightConstants constants;
  std::uint32_t heightStateAddress = 0U;
  std::uint32_t heightStateWord = 0U;
  if (!ReadField(memory, state.r[0], offsetof(PlayerWireState, StateFlags),
                 &height.StateFlags) ||
      !memory.ReadFast(kPlayerHeightStatePointerLiteral, &heightStateAddress) ||
      !ReadField(memory, heightStateAddress, sizeof(std::uint32_t),
                 &heightStateWord) ||
      !ReadFloat(memory, kPlayerHeightDefaultFlagOffsetLiteral,
                 &constants.DefaultFlagOffset) ||
      !ReadFloat(memory, kPlayerHeightRaisedFlagOffsetLiteral,
                 &constants.RaisedFlagOffset) ||
      !ReadFloat(memory, kPlayerHeightAlternateBaseLiteral,
                 &constants.AlternateBase) ||
      !ReadFloat(memory, kPlayerHeightDefaultBaseLiteral,
                 &constants.DefaultBase)) {
    return FailRead();
  }
  height.AlternateBaseHeight = heightStateWord != 0U;
  state.vfp[0] = std::bit_cast<std::uint32_t>(
      oot3d::gameplay::PlayerGetHeight(height, constants));
  state.r[0] = heightStateWord;
  return CompletePlayerCall(kOot3dPlayerGetHeightEntry, state, result,
                            blocksConsumed);
}

bool ExecutePlayerInCutsceneMode(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    std::uint32_t *blocksConsumed) {
  const std::uint32_t playAddress = state.r[0];
  std::uint32_t playerOffset = 0U;
  std::uint32_t playerAddress = 0U;
  std::uint32_t globalStateAddress = 0U;
  PlayerCutsceneModeState cutscene;
  PlayerCutsceneModeConstants constants;
  if (!memory.ReadFast(kPlayerCutscenePlayerOffsetLiteral, &playerOffset) ||
      playerOffset != offsetof(PlayerPlayStateWire, Player) ||
      !memory.ReadFast(kPlayerCutsceneMaskLiteral,
                       &constants.BlockingStateMask) ||
      !memory.ReadFast(kPlayerCutsceneGlobalStateLiteral,
                       &globalStateAddress) ||
      !ReadPlayerFromPlay(memory, playAddress, &playerAddress) ||
      !ReadField(memory, playerAddress,
                 offsetof(PlayerWireState, CutsceneActionMode),
                 &cutscene.CutsceneAction) ||
      !ReadField(memory, playerAddress, offsetof(PlayerWireState, StateFlags),
                 &cutscene.StateFlags) ||
      !ReadField(memory, playerAddress,
                 offsetof(PlayerWireState, StateFlagsByte),
                 &cutscene.StateFlagsByte) ||
      !ReadSignedByteField(memory, playerAddress,
                           offsetof(PlayerWireState, ItemAction),
                           &cutscene.ItemAction) ||
      !ReadField(memory, playerAddress, offsetof(PlayerWireState, State1749),
                 &cutscene.State1749) ||
      !ReadField(memory, playAddress,
                 offsetof(PlayerPlayStateWire, State5C2D),
                 &cutscene.PlayState5C2D) ||
      !ReadField(memory, globalStateAddress, 0x80U,
                 &cutscene.GlobalState80)) {
    return FailRead();
  }
  state.r[0] = static_cast<std::uint32_t>(
      oot3d::gameplay::PlayerInCutsceneMode(cutscene, constants));
  return CompletePlayerCall(kOot3dPlayerInCsModeEntry, state, result,
                            blocksConsumed);
}

ActorKinematics DecodeActorKinematics(const Actor &actor) noexcept {
  return {
      actor.WorldPosition,
      actor.WorldRotation,
      actor.Velocity,
      actor.SpeedXZ,
      actor.Gravity,
      actor.MinimumVelocityY,
      actor.CollisionCheck.Displacement,
  };
}

bool CommitActorPosition(NativeA32Memory &memory, std::uint32_t address,
                         const ActorKinematics &actor) {
  const std::uint32_t positionAddress =
      address + static_cast<std::uint32_t>(offsetof(Actor, WorldPosition));
  if (!memory.IsWritable(positionAddress, sizeof(actor.Position))) {
    return false;
  }
  return WriteWireObject(memory, positionAddress, actor.Position);
}

bool CommitActorVelocity(NativeA32Memory &memory, std::uint32_t address,
                         const ActorKinematics &actor) {
  const std::uint32_t velocityAddress =
      address + static_cast<std::uint32_t>(offsetof(Actor, Velocity));
  return memory.IsWritable(velocityAddress, sizeof(actor.Velocity)) &&
         WriteWireObject(memory, velocityAddress, actor.Velocity);
}

bool CommitActorMotion(NativeA32Memory &memory, std::uint32_t address,
                       const ActorKinematics &actor) {
  return CommitActorVelocity(memory, address, actor) &&
         CommitActorPosition(memory, address, actor);
}

bool CommitActorKilled(NativeA32Memory &memory, std::uint32_t address,
                       const ActorLifecycleState &actor) {
  const std::uint32_t drawAddress =
      address + static_cast<std::uint32_t>(offsetof(Actor, Draw));
  const std::uint32_t updateAddress =
      address + static_cast<std::uint32_t>(offsetof(Actor, Update));
  const std::uint32_t flagsAddress =
      address + static_cast<std::uint32_t>(offsetof(Actor, Flags));
  if (actor.UpdateEnabled || actor.DrawEnabled ||
      !memory.IsWritable(drawAddress, sizeof(GuestPtr<void>)) ||
      !memory.IsWritable(updateAddress, sizeof(GuestPtr<void>)) ||
      !memory.IsWritable(flagsAddress, sizeof(actor.Flags))) {
    return false;
  }
  const GuestPtr<oot3d::gameplay::GuestFunction> nullCallback{};
  return WriteWireObject(memory, drawAddress, nullCallback) &&
         WriteWireObject(memory, updateAddress, nullCallback) &&
         WriteWireObject(memory, flagsAddress, actor.Flags);
}

bool CommitActorScale(NativeA32Memory &memory, std::uint32_t address,
                      const oot3d::gameplay::Vec3f &scale) {
  const std::uint32_t scaleAddress =
      address + static_cast<std::uint32_t>(offsetof(Actor, Scale));
  if (!memory.IsWritable(scaleAddress, sizeof(scale))) {
    return false;
  }
  return WriteWireObject(memory,
                         scaleAddress + offsetof(oot3d::gameplay::Vec3f, Z),
                         scale.Z) &&
         WriteWireObject(memory,
                         scaleAddress + offsetof(oot3d::gameplay::Vec3f, Y),
                         scale.Y) &&
         WriteWireObject(memory,
                         scaleAddress + offsetof(oot3d::gameplay::Vec3f, X),
                         scale.X);
}

bool ExecuteAngle(std::uint32_t entry, oot3d::recomp::a32::GuestState &state,
                  NativeA32Memory &memory,
                  oot3d::recomp::a32::ExecutionResult *result,
                  std::uint32_t *blocksConsumed, bool cosine) {
  float value = 0.0f;
  if (!ReadAngleComponent(memory, static_cast<std::uint16_t>(state.r[0]),
                          cosine, &value)) {
    return FailRead();
  }
  state.vfp[0] = std::bit_cast<std::uint32_t>(value);
  ++gStats.AngleCalls;
  return CompleteCall(entry, state, result, blocksConsumed);
}

bool ExecuteActorUpdatePosition(oot3d::recomp::a32::GuestState &state,
                                NativeA32Memory &memory,
                                oot3d::recomp::a32::ExecutionResult *result,
                                const Oot3dTypedGameplayContext &context,
                                std::uint32_t *blocksConsumed) {
  Actor actor;
  float positionUpdateScale = 0.0f;
  if (!ReadActor(memory, state.r[0], &actor) ||
      !ReadFloat(memory, kActorUpdatePosScaleLiteral, &positionUpdateScale)) {
    return FailRead();
  }
  ActorKinematics kinematics = DecodeActorKinematics(actor);
  oot3d::gameplay::ActorUpdatePosition(kinematics, context.NativeUpdateRate,
                                       positionUpdateScale);
  if (!CommitActorPosition(memory, state.r[0], kinematics)) {
    return FailWrite();
  }
  ++gStats.ActorCalls;
  return CompleteCall(kOot3dActorUpdatePosEntry, state, result, blocksConsumed);
}

bool ExecuteActorUpdateVelocityXZGravity(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context, std::uint32_t *blocksConsumed) {
  Actor actor;
  AngleSample direction;
  float gravityUpdateScale = 0.0f;
  if (!ReadActor(memory, state.r[0], &actor) ||
      !ReadAngleSample(memory,
                       static_cast<std::uint16_t>(actor.WorldRotation.Y),
                       &direction) ||
      !ReadFloat(memory, kActorVelocityGravityScaleLiteral,
                 &gravityUpdateScale)) {
    return FailRead();
  }
  ActorKinematics kinematics = DecodeActorKinematics(actor);
  oot3d::gameplay::ActorUpdateVelocityXZGravity(
      kinematics, context.NativeUpdateRate, gravityUpdateScale, direction);
  if (!CommitActorVelocity(memory, state.r[0], kinematics)) {
    return FailWrite();
  }
  ++gStats.ActorCalls;
  return CompleteCall(kOot3dActorUpdateVelocityXZGravityEntry, state, result,
                      blocksConsumed);
}

bool ReadActorRotationSamples(const NativeA32Memory &memory, const Actor &actor,
                              AngleSample *elevation, AngleSample *azimuth) {
  return ReadAngleSample(memory,
                         static_cast<std::uint16_t>(actor.WorldRotation.X),
                         elevation) &&
         ReadAngleSample(memory,
                         static_cast<std::uint16_t>(actor.WorldRotation.Y),
                         azimuth);
}

bool ExecuteActorUpdateVelocityXYZ(oot3d::recomp::a32::GuestState &state,
                                   NativeA32Memory &memory,
                                   oot3d::recomp::a32::ExecutionResult *result,
                                   std::uint32_t *blocksConsumed) {
  Actor actor;
  AngleSample elevation;
  AngleSample azimuth;
  if (!ReadActor(memory, state.r[0], &actor) ||
      !ReadActorRotationSamples(memory, actor, &elevation, &azimuth)) {
    return FailRead();
  }
  ActorKinematics kinematics = DecodeActorKinematics(actor);
  oot3d::gameplay::ActorUpdateVelocityXYZ(kinematics, elevation, azimuth);
  if (!CommitActorVelocity(memory, state.r[0], kinematics)) {
    return FailWrite();
  }
  ++gStats.ActorCalls;
  return CompleteCall(kOot3dActorUpdateVelocityXYZEntry, state, result,
                      blocksConsumed);
}

bool ExecuteActorUpdatePositionWithVelocityFromRotation(
    oot3d::recomp::a32::GuestState &state, NativeA32Memory &memory,
    oot3d::recomp::a32::ExecutionResult *result,
    const Oot3dTypedGameplayContext &context, std::uint32_t *blocksConsumed) {
  Actor actor;
  AngleSample elevation;
  AngleSample azimuth;
  float positionUpdateScale = 0.0f;
  if (!ReadActor(memory, state.r[0], &actor) ||
      !ReadActorRotationSamples(memory, actor, &elevation, &azimuth) ||
      !ReadFloat(memory, kActorPositionRotationScaleLiteral,
                 &positionUpdateScale)) {
    return FailRead();
  }
  ActorKinematics kinematics = DecodeActorKinematics(actor);
  oot3d::gameplay::ActorUpdatePositionWithVelocityFromRotation(
      kinematics, context.NativeUpdateRate, positionUpdateScale, elevation,
      azimuth);
  if (!CommitActorMotion(memory, state.r[0], kinematics)) {
    return FailWrite();
  }
  ++gStats.ActorCalls;
  return CompleteCall(kOot3dActorUpdatePosWithVelocityFromRotationEntry, state,
                      result, blocksConsumed);
}

bool ExecuteActorMoveForward(oot3d::recomp::a32::GuestState &state,
                             NativeA32Memory &memory,
                             oot3d::recomp::a32::ExecutionResult *result,
                             const Oot3dTypedGameplayContext &context,
                             std::uint32_t *blocksConsumed) {
  Actor actor;
  AngleSample direction;
  ActorMovementConstants constants;
  if (!ReadActor(memory, state.r[0], &actor) ||
      !ReadAngleSample(memory,
                       static_cast<std::uint16_t>(actor.WorldRotation.Y),
                       &direction) ||
      !ReadFloat(memory, kActorMoveGravityScaleLiteral,
                 &constants.GravityUpdateScale) ||
      !ReadFloat(memory, kActorMovePositionScaleLiteral,
                 &constants.PositionUpdateScale)) {
    return FailRead();
  }
  ActorKinematics kinematics = DecodeActorKinematics(actor);
  oot3d::gameplay::ActorMoveForward(kinematics, context.NativeUpdateRate,
                                    constants, direction);
  if (!CommitActorMotion(memory, state.r[0], kinematics)) {
    return FailWrite();
  }
  ++gStats.ActorCalls;
  return CompleteCall(kOot3dActorMoveForwardEntry, state, result,
                      blocksConsumed);
}

bool ExecuteActorParentQuery(std::uint32_t entry,
                             oot3d::recomp::a32::GuestState &state,
                             NativeA32Memory &memory,
                             oot3d::recomp::a32::ExecutionResult *result,
                             std::uint32_t *blocksConsumed,
                             bool queryHasParent) {
  GuestPtr<Actor> parent;
  if (state.r[0] == 0U ||
      !ReadWireObject(memory,
                      state.r[0] +
                          static_cast<std::uint32_t>(offsetof(Actor, Parent)),
                      &parent)) {
    return FailRead();
  }
  const ActorLifecycleState lifecycle{
      .HasParent = parent.Address != 0U,
  };
  state.r[0] = queryHasParent
                   ? static_cast<std::uint32_t>(
                         oot3d::gameplay::ActorHasParent(lifecycle))
                   : static_cast<std::uint32_t>(
                         oot3d::gameplay::ActorHasNoParent(lifecycle));
  ++gStats.ActorCalls;
  return CompleteCall(entry, state, result, blocksConsumed);
}

bool ExecuteActorKill(oot3d::recomp::a32::GuestState &state,
                      NativeA32Memory &memory,
                      oot3d::recomp::a32::ExecutionResult *result,
                      std::uint32_t *blocksConsumed) {
  std::uint32_t flags = 0;
  if (state.r[0] == 0U ||
      !memory.ReadFast(state.r[0] +
                           static_cast<std::uint32_t>(offsetof(Actor, Flags)),
                       &flags)) {
    return FailRead();
  }
  ActorLifecycleState lifecycle{
      .Flags = flags,
      .UpdateEnabled = true,
      .DrawEnabled = true,
  };
  oot3d::gameplay::ActorKill(lifecycle);
  if (!CommitActorKilled(memory, state.r[0], lifecycle)) {
    return FailWrite();
  }
  ++gStats.ActorCalls;
  return CompleteCall(kOot3dActorKillEntry, state, result, blocksConsumed);
}

bool ExecuteActorSetScale(oot3d::recomp::a32::GuestState &state,
                          NativeA32Memory &memory,
                          oot3d::recomp::a32::ExecutionResult *result,
                          std::uint32_t *blocksConsumed) {
  if (state.r[0] == 0U) {
    return FailRead();
  }
  oot3d::gameplay::Vec3f scale;
  oot3d::gameplay::ActorSetUniformScale(scale,
                                        std::bit_cast<float>(state.vfp[0]));
  if (!CommitActorScale(memory, state.r[0], scale)) {
    return FailWrite();
  }
  ++gStats.ActorCalls;
  return CompleteCall(kOot3dActorSetScaleEntry, state, result, blocksConsumed);
}

bool ExecuteStepToF(oot3d::recomp::a32::GuestState &state,
                    NativeA32Memory &memory,
                    oot3d::recomp::a32::ExecutionResult *result,
                    const Oot3dTypedGameplayContext &context,
                    std::uint32_t *blocksConsumed) {
  const std::uint32_t valueAddress = state.r[0];
  float current = 0.0f;
  float zero = 0.0f;
  float updateScale = 0.0f;
  if (valueAddress == 0U || !ReadFloat(memory, valueAddress, &current) ||
      !ReadFloat(memory, kStepToFZeroLiteral, &zero) ||
      !ReadFloat(memory, kStepToFScaleLiteral, &updateScale)) {
    return FailRead();
  }

  bool reached = false;
  try {
    reached =
        oot3d::gameplay::StepToF(current, std::bit_cast<float>(state.vfp[0]),
                                 std::bit_cast<float>(state.vfp[1]),
                                 context.NativeUpdateRate, updateScale, zero);
  } catch (...) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }
  if (!memory.WriteFast(valueAddress, std::bit_cast<std::uint32_t>(current))) {
    return FailWrite();
  }
  state.r[0] = reached ? 1U : 0U;
  ++gStats.MathCalls;
  return CompleteCall(kOot3dMathStepToFEntry, state, result, blocksConsumed);
}

bool ExecuteApproachF(oot3d::recomp::a32::GuestState &state,
                      NativeA32Memory &memory,
                      oot3d::recomp::a32::ExecutionResult *result,
                      const Oot3dTypedGameplayContext &context,
                      std::uint32_t *blocksConsumed) {
  const std::uint32_t valueAddress = state.r[0];
  float current = 0.0f;
  float updateScale = 0.0f;
  float epsilon = 0.0f;
  if (valueAddress == 0U || !ReadFloat(memory, valueAddress, &current) ||
      !ReadFloat(memory, kApproachFScaleLiteral, &updateScale) ||
      !ReadFloat(memory, kApproachFEpsilonLiteral, &epsilon)) {
    return FailRead();
  }
  try {
    oot3d::gameplay::ApproachF(current, std::bit_cast<float>(state.vfp[0]),
                               std::bit_cast<float>(state.vfp[1]),
                               std::bit_cast<float>(state.vfp[2]),
                               context.NativeUpdateRate, updateScale, epsilon);
  } catch (...) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }
  if (!memory.WriteFast(valueAddress, std::bit_cast<std::uint32_t>(current))) {
    return FailWrite();
  }
  ++gStats.MathCalls;
  return CompleteCall(kOot3dMathApproachFEntry, state, result, blocksConsumed);
}

bool ExecuteApproachZeroF(oot3d::recomp::a32::GuestState &state,
                          NativeA32Memory &memory,
                          oot3d::recomp::a32::ExecutionResult *result,
                          const Oot3dTypedGameplayContext &context,
                          std::uint32_t *blocksConsumed) {
  const std::uint32_t valueAddress = state.r[0];
  float current = 0.0f;
  float updateScale = 0.0f;
  float epsilon = 0.0f;
  if (valueAddress == 0U || !ReadFloat(memory, valueAddress, &current) ||
      !ReadFloat(memory, kApproachZeroFScaleLiteral, &updateScale) ||
      !ReadFloat(memory, kApproachZeroFEpsilonLiteral, &epsilon)) {
    return FailRead();
  }
  try {
    oot3d::gameplay::ApproachZeroF(current, std::bit_cast<float>(state.vfp[0]),
                                   std::bit_cast<float>(state.vfp[1]),
                                   context.NativeUpdateRate, updateScale,
                                   epsilon);
  } catch (...) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }
  if (!memory.WriteFast(valueAddress, std::bit_cast<std::uint32_t>(current))) {
    return FailWrite();
  }
  ++gStats.MathCalls;
  return CompleteCall(kOot3dMathApproachZeroFEntry, state, result,
                      blocksConsumed);
}

bool ExecuteScaledStepToS(oot3d::recomp::a32::GuestState &state,
                          NativeA32Memory &memory,
                          oot3d::recomp::a32::ExecutionResult *result,
                          const Oot3dTypedGameplayContext &context,
                          std::uint32_t *blocksConsumed) {
  const std::uint32_t valueAddress = state.r[0];
  std::uint16_t currentBits = 0U;
  float updateScale = 0.0f;
  if (valueAddress == 0U || !memory.ReadFast(valueAddress, &currentBits) ||
      !ReadFloat(memory, kScaledStepToSScaleLiteral, &updateScale)) {
    return FailRead();
  }
  std::int16_t current = std::bit_cast<std::int16_t>(currentBits);
  bool reached = false;
  try {
    reached = oot3d::gameplay::ScaledStepToS(
        current, LowS16(state.r[1]), LowS16(state.r[2]),
        context.NativeUpdateRate, updateScale);
  } catch (...) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }
  if (!memory.WriteFast(valueAddress, std::bit_cast<std::uint16_t>(current))) {
    return FailWrite();
  }
  state.r[0] = reached ? 1U : 0U;
  ++gStats.MathCalls;
  return CompleteCall(kOot3dMathScaledStepToSEntry, state, result,
                      blocksConsumed);
}

bool ExecuteStepToS(oot3d::recomp::a32::GuestState &state,
                    NativeA32Memory &memory,
                    oot3d::recomp::a32::ExecutionResult *result,
                    const Oot3dTypedGameplayContext &context,
                    std::uint32_t *blocksConsumed) {
  const std::uint32_t valueAddress = state.r[0];
  std::uint16_t currentBits = 0U;
  float updateScale = 0.0f;
  float roundingBias = 0.0f;
  if (valueAddress == 0U || !memory.ReadFast(valueAddress, &currentBits) ||
      !ReadFloat(memory, kStepToSScaleLiteral, &updateScale) ||
      !ReadFloat(memory, kStepToSRoundingBiasLiteral, &roundingBias)) {
    return FailRead();
  }
  std::int16_t current = std::bit_cast<std::int16_t>(currentBits);
  bool reached = false;
  try {
    reached = oot3d::gameplay::StepToS(
        current, LowS16(state.r[1]), LowS16(state.r[2]),
        context.NativeUpdateRate, updateScale, roundingBias);
  } catch (...) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }
  if (!memory.WriteFast(valueAddress, std::bit_cast<std::uint16_t>(current))) {
    return FailWrite();
  }
  state.r[0] = reached ? 1U : 0U;
  ++gStats.MathCalls;
  return CompleteCall(kOot3dMathStepToSEntry, state, result, blocksConsumed);
}

bool ExecuteStepToAngleS(oot3d::recomp::a32::GuestState &state,
                         NativeA32Memory &memory,
                         oot3d::recomp::a32::ExecutionResult *result,
                         const Oot3dTypedGameplayContext &context,
                         std::uint32_t *blocksConsumed) {
  const std::uint32_t valueAddress = state.r[0];
  std::uint16_t currentBits = 0U;
  float updateScale = 0.0f;
  float roundingBias = 0.0f;
  if (valueAddress == 0U || !memory.ReadFast(valueAddress, &currentBits) ||
      !ReadFloat(memory, kStepToAngleSScaleLiteral, &updateScale) ||
      !ReadFloat(memory, kStepToAngleSRoundingBiasLiteral, &roundingBias)) {
    return FailRead();
  }
  std::int16_t current = std::bit_cast<std::int16_t>(currentBits);
  bool reached = false;
  try {
    reached = oot3d::gameplay::StepToAngleS(
        current, LowS16(state.r[1]), LowS16(state.r[2]),
        context.NativeUpdateRate, updateScale, roundingBias);
  } catch (...) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }
  if (!memory.WriteFast(valueAddress, std::bit_cast<std::uint16_t>(current))) {
    return FailWrite();
  }
  state.r[0] = reached ? 1U : 0U;
  ++gStats.MathCalls;
  return CompleteCall(kOot3dMathStepToAngleSEntry, state, result,
                      blocksConsumed);
}

bool ExecuteSmoothStepToF(oot3d::recomp::a32::GuestState &state,
                          NativeA32Memory &memory,
                          oot3d::recomp::a32::ExecutionResult *result,
                          const Oot3dTypedGameplayContext &context,
                          std::uint32_t *blocksConsumed) {
  const std::uint32_t valueAddress = state.r[0];
  float current = 0.0f;
  float updateScale = 0.0f;
  float epsilon = 0.0f;
  if (valueAddress == 0U || !ReadFloat(memory, valueAddress, &current) ||
      !ReadFloat(memory, kSmoothStepToFScaleLiteral, &updateScale) ||
      !ReadFloat(memory, kSmoothStepToFEpsilonLiteral, &epsilon)) {
    return FailRead();
  }

  float remaining = 0.0f;
  try {
    remaining = oot3d::gameplay::SmoothStepToF(
        current, std::bit_cast<float>(state.vfp[0]),
        std::bit_cast<float>(state.vfp[1]), std::bit_cast<float>(state.vfp[2]),
        std::bit_cast<float>(state.vfp[3]), context.NativeUpdateRate,
        updateScale, epsilon);
  } catch (...) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }
  if (!memory.WriteFast(valueAddress, std::bit_cast<std::uint32_t>(current))) {
    return FailWrite();
  }
  state.vfp[0] = std::bit_cast<std::uint32_t>(remaining);
  ++gStats.MathCalls;
  return CompleteCall(kOot3dMathSmoothStepToFEntry, state, result,
                      blocksConsumed);
}

bool ExecuteSmoothStepToS(oot3d::recomp::a32::GuestState &state,
                          NativeA32Memory &memory,
                          oot3d::recomp::a32::ExecutionResult *result,
                          const Oot3dTypedGameplayContext &context,
                          std::uint32_t *blocksConsumed) {
  const std::uint32_t valueAddress = state.r[0];
  std::uint16_t currentBits = 0U;
  std::uint32_t minimumStepBits = 0U;
  float updateScale = 0.0f;
  float roundingBias = 0.0f;
  if (valueAddress == 0U || !memory.ReadFast(valueAddress, &currentBits) ||
      !memory.ReadFast(state.r[13], &minimumStepBits) ||
      !ReadFloat(memory, kSmoothStepToSScaleLiteral, &updateScale) ||
      !ReadFloat(memory, kSmoothStepToSRoundingBiasLiteral, &roundingBias)) {
    return FailRead();
  }

  std::int16_t current = std::bit_cast<std::int16_t>(currentBits);
  std::int16_t difference = 0;
  try {
    difference = oot3d::gameplay::SmoothStepToS(
        current, LowS16(state.r[1]), std::bit_cast<std::int32_t>(state.r[2]),
        std::bit_cast<std::int32_t>(state.r[3]),
        std::bit_cast<std::int32_t>(minimumStepBits), context.NativeUpdateRate,
        updateScale, roundingBias);
  } catch (...) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }
  if (!memory.WriteFast(valueAddress, std::bit_cast<std::uint16_t>(current))) {
    return FailWrite();
  }
  state.r[0] =
      std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(difference));
  ++gStats.MathCalls;
  return CompleteCall(kOot3dMathSmoothStepToSEntry, state, result,
                      blocksConsumed);
}

bool ExecuteSmoothStep(oot3d::recomp::a32::GuestState &state,
                       NativeA32Memory &memory,
                       oot3d::recomp::a32::ExecutionResult *result,
                       const Oot3dTypedGameplayContext &context,
                       std::uint32_t *blocksConsumed) {
  std::uint16_t currentBits = 0;
  float updateScale = 0.0f;
  float roundingBias = 0.0f;
  if (state.r[0] == 0U || !memory.ReadFast(state.r[0], &currentBits) ||
      !ReadFloat(memory, kSmoothStepUpdateScaleLiteral, &updateScale) ||
      !ReadFloat(memory, kSmoothStepRoundingBiasLiteral, &roundingBias)) {
    return FailRead();
  }

  std::int16_t next = 0;
  try {
    next = oot3d::gameplay::SmoothStepToSUpdateRate(
        std::bit_cast<std::int16_t>(currentBits), LowS16(state.r[1]),
        static_cast<std::int32_t>(state.r[2]),
        static_cast<std::int32_t>(state.r[3]), context.NativeUpdateRate,
        updateScale, roundingBias);
  } catch (...) {
    ++gStats.RetainedAotFallbacks;
    return false;
  }
  if (!memory.WriteFast(state.r[0], std::bit_cast<std::uint16_t>(next))) {
    return FailWrite();
  }
  ++gStats.MathCalls;
  return CompleteCall(kOot3dMathSmoothStepToSUpdateRateEntry, state, result,
                      blocksConsumed);
}

} // namespace

std::span<const std::uint32_t> Oot3dTypedGameplayEntryPoints() noexcept {
  return kTypedGameplayEntryPoints;
}

std::span<const std::uint32_t>
Oot3dTypedGameplayObservableExitPoints() noexcept {
  return kTypedGameplayObservableExitPoints;
}

void ResetOot3dTypedGameplayTransientState() noexcept {
  gMainCutsceneCameraBinding = {};
  gActorCutsceneCameraBinding = {};
  gCameraQuakeSignalCache = {};
  gCameraQuakePendingReturn = {};
}

void ResetOot3dTypedGameplayStats() noexcept {
  gStats = {};
  ResetOot3dTypedGameplayTransientState();
}

Oot3dTypedGameplayStats GetOot3dTypedGameplayStats() noexcept { return gStats; }

bool ExecuteOot3dTypedGameplay(std::uint32_t pc,
                               oot3d::recomp::a32::GuestState &state,
                               NativeA32Memory &memory,
                               oot3d::recomp::a32::ExecutionResult *result,
                               const Oot3dTypedGameplayContext &context,
                               std::uint32_t *blocksConsumed) {
  if (result == nullptr || !std::isfinite(context.NativeUpdateRate) ||
      context.NativeUpdateRate <= 0.0f) {
    return false;
  }
  switch (pc) {
  case kOot3dGameStateUpdateOwnerEntry:
  case kOot3dGameStateUpdateMainReturn: {
    const auto gameStateResult = ExecuteOot3dTypedGameState(
        pc, state, memory, result, blocksConsumed);
    switch (gameStateResult) {
    case Oot3dTypedGameStateResult::MainDispatched:
      ++gStats.Calls;
      ++gStats.GameStateUpdateEntryCalls;
      ++gStats.GameStateMainDispatches;
      return true;
    case Oot3dTypedGameStateResult::UpdateReturned:
      ++gStats.Calls;
      ++gStats.GameStateUpdateReturns;
      return true;
    case Oot3dTypedGameStateResult::ReadFailure:
      return FailRead();
    case Oot3dTypedGameStateResult::WriteFailure:
      return FailWrite();
    case Oot3dTypedGameStateResult::NotHandled:
      return false;
    }
    return false;
  }
  case kOot3dActorDestroyEntry:
  case kOot3dActorDestroyCallbackReturn:
  case kOot3dActorDestroyModelContextReturn:
  case kOot3dActorDestroyOwnedSlotReturn:
  case kOot3dActorDestroyLastOwnedSlotReturn:
  case kOot3dActorUpdateAllInitCallbackEntry:
  case kOot3dActorUpdateAllInitCallbackReturn:
  case kOot3dActorUpdateAllUpdateCallbackEntry: {
    const auto lifecycleResult = ExecuteOot3dTypedActorLifecycle(
        pc, state, memory, result, blocksConsumed);
    switch (lifecycleResult) {
    case Oot3dTypedActorLifecycleResult::InitCallbackDispatched:
      ++gStats.Calls;
      ++gStats.ActorCalls;
      ++gStats.ActorInitCallbackDispatches;
      return true;
    case Oot3dTypedActorLifecycleResult::InitCallbackReturned:
      ++gStats.Calls;
      ++gStats.ActorCalls;
      ++gStats.ActorInitCallbackReturns;
      return true;
    case Oot3dTypedActorLifecycleResult::UpdateCallbackDispatched:
      ++gStats.Calls;
      ++gStats.ActorCalls;
      ++gStats.ActorUpdateCallbackDispatches;
      return true;
    case Oot3dTypedActorLifecycleResult::DestroyCallbackDispatched:
      ++gStats.Calls;
      ++gStats.ActorCalls;
      ++gStats.ActorDestroyCallbackDispatches;
      return true;
    case Oot3dTypedActorLifecycleResult::ResourceCallbackDispatched:
      ++gStats.Calls;
      ++gStats.ActorCalls;
      ++gStats.ActorResourceCleanupDispatches;
      return true;
    case Oot3dTypedActorLifecycleResult::DestroyReturned:
      ++gStats.Calls;
      ++gStats.ActorCalls;
      ++gStats.ActorDestroyReturns;
      return true;
    case Oot3dTypedActorLifecycleResult::InvalidState:
      ++gStats.RetainedAotFallbacks;
      return false;
    case Oot3dTypedActorLifecycleResult::ReadFailure:
      return FailRead();
    case Oot3dTypedActorLifecycleResult::WriteFailure:
      return FailWrite();
    case Oot3dTypedActorLifecycleResult::NotHandled:
      return false;
    }
    return false;
  }
  case kOot3dAudioRequestFlag100CallbackEntry:
  case kOot3dAudioRequestFlag100ReferenceAcquireReturn:
  case kOot3dAudioRequestFlag100StatusQueryReturn:
  case kOot3dAudioRequestFlag100ReferenceCleanupReturn: {
    const auto callbackResult = ExecuteOot3dTypedAudioRequestCallback(
        pc, state, memory, result, blocksConsumed);
    switch (callbackResult) {
    case Oot3dTypedAudioRequestCallbackResult::ReferenceAcquireDispatched:
      ++gStats.Calls;
      ++gStats.AudioRequestReferenceAcquireDispatches;
      return true;
    case Oot3dTypedAudioRequestCallbackResult::StatusQueryDispatched:
      ++gStats.Calls;
      ++gStats.AudioRequestStatusQueryDispatches;
      return true;
    case Oot3dTypedAudioRequestCallbackResult::ReferenceCleanupDispatched:
      ++gStats.Calls;
      ++gStats.AudioRequestReferenceCleanupDispatches;
      return true;
    case Oot3dTypedAudioRequestCallbackResult::CallbackReturned:
      ++gStats.Calls;
      ++gStats.AudioRequestCallbackReturns;
      return true;
    case Oot3dTypedAudioRequestCallbackResult::InvalidState:
      ++gStats.RetainedAotFallbacks;
      return false;
    case Oot3dTypedAudioRequestCallbackResult::ReadFailure:
      return FailRead();
    case Oot3dTypedAudioRequestCallbackResult::WriteFailure:
      return FailWrite();
    case Oot3dTypedAudioRequestCallbackResult::NotHandled:
      return false;
    }
    return false;
  }
  case kOot3dPauseUiUpdateDualAlphaEntry:
  case kOot3dPauseUiAlphaPauseStateReturn:
  case kOot3dPauseUiAlphaFadeOutStepReturn:
  case kOot3dPauseUiAlphaFadeInStepReturn: {
    const auto pauseUiResult = ExecuteOot3dTypedPauseUiAlpha(
        pc, state, memory, result, blocksConsumed);
    switch (pauseUiResult) {
    case Oot3dTypedPauseUiAlphaResult::PauseStateQueryDispatched:
      ++gStats.Calls;
      ++gStats.ActorCalls;
      ++gStats.PauseUiAlphaPauseStateDispatches;
      return true;
    case Oot3dTypedPauseUiAlphaResult::FirstAlphaStepDispatched:
      ++gStats.Calls;
      ++gStats.ActorCalls;
      ++gStats.PauseUiAlphaFirstStepDispatches;
      return true;
    case Oot3dTypedPauseUiAlphaResult::TailAlphaStepDispatched:
      ++gStats.Calls;
      ++gStats.ActorCalls;
      ++gStats.PauseUiAlphaTailStepDispatches;
      return true;
    case Oot3dTypedPauseUiAlphaResult::UpdateReturned:
      ++gStats.Calls;
      ++gStats.ActorCalls;
      ++gStats.PauseUiAlphaReturns;
      return true;
    case Oot3dTypedPauseUiAlphaResult::InvalidState:
      ++gStats.RetainedAotFallbacks;
      return false;
    case Oot3dTypedPauseUiAlphaResult::ReadFailure:
      return FailRead();
    case Oot3dTypedPauseUiAlphaResult::WriteFailure:
      return FailWrite();
    case Oot3dTypedPauseUiAlphaResult::NotHandled:
      return false;
    }
    return false;
  }
  case kOot3dDynaResetActorInteractionIfRegisteredEntry: {
    const auto resetResult = ExecuteOot3dTypedDynaInteractionReset(
        pc, state, memory, result, blocksConsumed);
    switch (resetResult) {
    case Oot3dTypedDynaInteractionResetResult::InteractionReset:
      ++gStats.Calls;
      ++gStats.ActorCalls;
      ++gStats.DynaInteractionResetMatches;
      return true;
    case Oot3dTypedDynaInteractionResetResult::ActorNotRegistered:
      ++gStats.Calls;
      ++gStats.ActorCalls;
      ++gStats.DynaInteractionResetMisses;
      return true;
    case Oot3dTypedDynaInteractionResetResult::ReadFailure:
      return FailRead();
    case Oot3dTypedDynaInteractionResetResult::WriteFailure:
      return FailWrite();
    case Oot3dTypedDynaInteractionResetResult::NotHandled:
      return false;
    }
    return false;
  }
  case kOot3dPlayerReleaseLockOnEntry: {
    const auto lockOnResult = ExecuteOot3dTypedPlayerLockOn(
        pc, state, memory, result, blocksConsumed);
    switch (lockOnResult) {
    case Oot3dTypedPlayerLockOnResult::Released:
      ++gStats.Calls;
      ++gStats.PlayerCalls;
      ++gStats.PlayerReleaseLockOnCalls;
      return true;
    case Oot3dTypedPlayerLockOnResult::ReadFailure:
      return FailRead();
    case Oot3dTypedPlayerLockOnResult::WriteFailure:
      return FailWrite();
    case Oot3dTypedPlayerLockOnResult::NotHandled:
      return false;
    }
    return false;
  }
  case kOot3dActorUpdateRecordInitializeDefaultsEntry:
  case kOot3dActorUpdateRecordClearHalfwordsEntry: {
    const auto recordResult = ExecuteOot3dTypedActorUpdateRecord(
        pc, state, memory, result, blocksConsumed);
    switch (recordResult) {
    case Oot3dTypedActorUpdateRecordResult::InitializedDefaults:
      ++gStats.Calls;
      ++gStats.ActorCalls;
      ++gStats.ActorUpdateRecordInitializeCalls;
      return true;
    case Oot3dTypedActorUpdateRecordResult::ClearedHalfwords:
      ++gStats.Calls;
      ++gStats.ActorCalls;
      ++gStats.ActorUpdateRecordClearCalls;
      return true;
    case Oot3dTypedActorUpdateRecordResult::ReadFailure:
      return FailRead();
    case Oot3dTypedActorUpdateRecordResult::WriteFailure:
      return FailWrite();
    case Oot3dTypedActorUpdateRecordResult::NotHandled:
      return false;
    }
    return false;
  }
  case kOot3dRecordInitializerEntry:
  case kOot3dRecordInitializerMemzeroReturn: {
    const auto initializerResult = ExecuteOot3dTypedRecordInitializer(
        pc, state, memory, result, blocksConsumed);
    switch (initializerResult) {
    case Oot3dTypedRecordInitializerResult::MemzeroDispatched:
      ++gStats.Calls;
      ++gStats.RecordInitializerMemzeroDispatches;
      return true;
    case Oot3dTypedRecordInitializerResult::InitReturned:
      ++gStats.Calls;
      ++gStats.RecordInitializerReturns;
      return true;
    case Oot3dTypedRecordInitializerResult::InvalidState:
      ++gStats.RetainedAotFallbacks;
      return false;
    case Oot3dTypedRecordInitializerResult::ReadFailure:
      return FailRead();
    case Oot3dTypedRecordInitializerResult::WriteFailure:
      return FailWrite();
    case Oot3dTypedRecordInitializerResult::NotHandled:
      return false;
    }
    return false;
  }
  case kOot3dCameraQuakeSineCallbackEntry:
  case kOot3dCameraQuakeSineFadeCallbackEntry:
  case kOot3dCameraQuakeRandomCallbackEntry:
  case kOot3dCameraQuakePerpetualSineRandomCallbackEntry:
  case kOot3dCameraQuakeRandomFadeCallbackEntry:
  case kOot3dCameraQuakeSineRandomCallbackEntry:
    return ExecuteCameraQuakeCallback(pc, state, memory, result, context,
                                      blocksConsumed);
  case kOot3dCameraQuakeCallbackReturnBoundary:
    return CompleteCameraQuakeCallbackReturn(state, result, blocksConsumed);
  case kOot3dEnKoBlinkAdvanceBlock:
    return ExecuteEnKoBlinkAdvanceBlock(
        state, memory, result, context, blocksConsumed);
  case kOot3dEnKoBlinkRngReturnBlock:
    return ExecuteEnKoBlinkRngReturnBlock(
        state, memory, result, blocksConsumed);
  case kOot3dEnKanbanPhaseAdvanceBlock:
    return ExecuteEnKanbanPhaseAdvanceBlock(
        state, memory, result, context, blocksConsumed);
  case kOot3dEnKanbanState0CountdownBlock:
    return ExecuteEnKanbanState0CountdownBlock(
        state, memory, result, context, blocksConsumed);
  case kOot3dEnKanbanActorFlagCountdownBlock:
    return ExecuteEnKanbanActorFlagCountdownBlock(
        state, memory, result, context, blocksConsumed);
  case kOot3dEnKanbanInteractionCooldownBlock:
    return ExecuteEnKanbanInteractionCooldownBlock(
        state, memory, result, context, blocksConsumed);
  case kOot3dEnKanbanDrawGateRampBlock:
    return ExecuteEnKanbanDrawGateRampBlock(
        state, memory, result, context, blocksConsumed);
  case kOot3dEnKanbanOscillatorXBlock:
  case kOot3dEnKanbanOscillatorYBlock:
    return ExecuteEnKanbanOscillatorAxisBlock(
        pc, state, memory, result, context, blocksConsumed);
  case kOot3dEnKanbanPieceLifetimeBlock:
    return ExecuteEnKanbanPieceLifetimeBlock(
        state, memory, result, context, blocksConsumed);
  case kOot3dEnKanbanRippleEventGateBlock:
    return ExecuteEnKanbanRippleEventGateBlock(
        state, memory, result, context, blocksConsumed);
  case kOot3dCameraJump1FrameCountdownBlock:
  case kOot3dCameraJump2FrameCountdownBlock:
  case kOot3dCameraBattle1FrameCountdownBlock:
  case kOot3dCameraBattle4FrameCountdownBlock:
  case kOot3dCameraKeepOn1FrameCountdownBlock:
  case kOot3dCameraKeepOn3FrameCountdownBlock:
  case kOot3dCameraNormal1FrameCountdownBlock:
  case kOot3dCameraNormal1SpeedCountdownBlock:
  case kOot3dCameraNormal1RateCountdownBlock:
  case kOot3dCameraUnique1FrameCountdownBlock:
  case kOot3dCameraSubj3FrameCountdownBlock:
  case kOot3dCameraParallel1FrameCountdownBlock:
    return ExecuteCameraModeFrameCountdownBlock(
        pc, state, memory, result, context, blocksConsumed);
  case kOot3dCameraSpecial5TimerBlock:
    return ExecuteCameraSpecial5TimerBlock(
        state, memory, result, context, blocksConsumed);
  case kOot3dActorUpdateAllContextFreezeBlock:
    return ExecuteActorUpdateAllContextFreezeBlock(
        state, memory, result, context, blocksConsumed);
  case kOot3dActorUpdateAllInstanceFreezeBlock:
    return ExecuteActorUpdateAllInstanceFreezeBlock(
        state, memory, result, context, blocksConsumed);
  case kOot3dActorUpdateAllEffectTimersBlock:
    return ExecuteActorUpdateAllEffectTimersBlock(
        state, memory, result, context, blocksConsumed);
  case kOot3dEnZl4ActorCameraAdvanceBlock:
    return ExecuteActorCutsceneCameraAdvanceBlock(
        state, memory, result, context, blocksConsumed);
  case kOot3dCameraDemo1Entry:
    return SampleMainCutsceneCameraAnimation(state, memory, context);
  case kOot3dPlayerRespawnDamageAdvanceBlock:
    return ExecutePlayerRespawnDamageAdvanceBlock(
        state, memory, result, context, blocksConsumed);
  case kOot3dPlayerRandomTurnTimerDecrementBlock:
    return ExecutePlayerRandomTurnTimerDecrementBlock(
        state, memory, result, context, blocksConsumed);
  case kOot3dPlayerRandomTurnTimerRefreshBlock:
    return ExecutePlayerRandomTurnTimerRefreshBlock(
        state, memory, result, context, blocksConsumed);
  case kOot3dPlayerAttentionPersistenceAdvanceBlock:
    return ExecutePlayerAttentionPersistenceAdvanceBlock(
        state, memory, result, context, blocksConsumed);
  case kOot3dCutsceneNormalFrameAdvanceBlock:
    return ExecuteCutsceneNormalFrameAdvanceBlock(
        state, memory, result, context, blocksConsumed);
  case kOot3dEnvironmentPathInterpolateActorPosAndRotationEntry:
    return ExecuteEnvironmentPathInterpolationEntry(
        state, memory, result, context, blocksConsumed);
  case kOot3dCameraAnimationApplyFrameEntry:
    return ObserveCameraAnimationApplyFrame(state, memory);
  case kOot3dPlayerCommonCountdownBlock:
    return ExecutePlayerCommonCountdownBlock(
        state, memory, result, context, blocksConsumed);
  case kOot3dPlayerInvincibilityTimerBlock:
    return ExecutePlayerInvincibilityTimerBlock(
        state, memory, result, context, blocksConsumed);
  case kOot3dPlayerDamageRunTimerBlock:
    return ExecutePlayerDamageRunTimerBlock(
        state, memory, result, context, blocksConsumed);
  case kOot3dPlayerDamageFlickerCounterBlock:
    return ExecutePlayerDamageFlickerCounterBlock(
        state, memory, result, context, blocksConsumed);
  case kOot3dPlayerFishingStateRecoveryBlock:
    return ExecutePlayerFishingStateRecoveryBlock(
        state, memory, result, context, blocksConsumed);
  case kOot3dPlayerMeleeActionTimerBlock:
    return ExecutePlayerMeleeActionTimerBlock(
        state, memory, result, context, blocksConsumed);
  case kOot3dPlayerMeleeWeaponTipComboAdvanceBlock:
    return ExecutePlayerMeleeWeaponTipComboAdvanceBlock(
        state, memory, result, context, blocksConsumed);
  case kOot3dPlayerUnderwaterTimerResetBlock:
    return ExecutePlayerUnderwaterTimerResetBlock(
        state, memory, result, context, blocksConsumed);
  case kOot3dPlayerUnderwaterTimerIncrementBlock:
    return ExecutePlayerUnderwaterTimerIncrementBlock(
        state, memory, result, context, blocksConsumed);
  case kOot3dSkelAnimeDirectMorphBoundary:
  case kOot3dSkelAnimeLegacyMorphBoundary:
    return ExecuteSkelAnimeMorphBoundary(pc, state, memory, result, context,
                                         blocksConsumed);
  case kOot3dSinIdx8Entry:
    return ExecuteAngle(pc, state, memory, result, blocksConsumed, false);
  case kOot3dCameraWaterDistortionTimerAdvanceBlock:
    return ExecuteCameraWaterDistortionTimerAdvanceBlock(
        state, memory, result, context, blocksConsumed);
  case kOot3dCameraFloorMissCounterAdvanceBlock:
    return ExecuteCameraFloorMissCounterAdvanceBlock(
        state, memory, result, context, blocksConsumed);
  case kOot3dCameraInterfaceDelayAdvanceBlock:
    return ExecuteCameraInterfaceDelayAdvanceBlock(
        state, memory, result, context, blocksConsumed);
  case kOot3dCameraWaterDistortionFlag4SampleBlock:
  case kOot3dCameraWaterDistortionFlag8SampleBlock:
  case kOot3dCameraWaterDistortionCustomSampleBlock:
    return ExecuteCameraWaterDistortionSampleBlock(
        pc, state, memory, result, context, blocksConsumed);
  case kOot3dSkelAnimeSetUpdateEntry:
    return ExecuteSkelAnimeSetUpdate(state, memory, result, blocksConsumed);
  case kOot3dPlayerGetExplosiveHeldEntry:
  case kOot3dPlayerHoldsHookshotWithoutHeldActorEntry:
  case kOot3dPlayerIsItemInHandEntry:
  case kOot3dPlayerHoldsHookshotEntry:
  case kOot3dPlayerHoldsTwoHandedWeaponEntry:
    return ExecutePlayerEquipmentQuery(pc, state, memory, result,
                                       blocksConsumed);
  case kOot3dPlayerSetCsActionEntry:
  case kOot3dPlayerSetCsActionWithHaltedActorsEntry:
    return ExecutePlayerSetCutsceneAction(pc, state, memory, result,
                                          blocksConsumed);
  case kOot3dCosIdx8Entry:
    return ExecuteAngle(pc, state, memory, result, blocksConsumed, true);
  case kOot3dActorUpdatePosWithVelocityFromRotationEntry:
    return ExecuteActorUpdatePositionWithVelocityFromRotation(
        state, memory, result, context, blocksConsumed);
  case kOot3dLinkAnimationPlayOnceWithSpeedEntry:
  case kOot3dLinkAnimationPlayLoopSetSpeedEntry:
  case kOot3dLinkAnimationPlayOnceEntry:
  case kOot3dLinkAnimationPlayLoopEntry:
    return ExecuteLinkAnimationPlay(pc, state, memory, result, blocksConsumed);
  case kOot3dPlayerUpdateHostileLockOnEntry:
    return ExecutePlayerUpdateHostileLockOn(state, memory, result,
                                            blocksConsumed);
  case kOot3dPlayerUpdateSwimVerticalVelocityEntry:
    return ExecutePlayerUpdateSwimVerticalVelocity(state, memory, result,
                                                   blocksConsumed);
  case kOot3dPlayerGetIdleAnimEntry:
    return ExecutePlayerGetIdleAnimation(state, memory, result,
                                         blocksConsumed);
  case kOot3dPlayerSetRuntimeFlag200Entry:
    return ExecutePlayerSetRuntimeFlag200(state, memory, result,
                                          blocksConsumed);
  case kOot3dActorUpdateVelocityXYZEntry:
    return ExecuteActorUpdateVelocityXYZ(state, memory, result, blocksConsumed);
  case kOot3dPlayerGetHeightEntry:
    return ExecutePlayerGetHeight(state, memory, result, blocksConsumed);
  case kOot3dPlayerInCsModeEntry:
    return ExecutePlayerInCutsceneMode(state, memory, result, blocksConsumed);
  case kOot3dZarGetCsabByIndexEntry:
    return ExecuteZarGetCsabByIndex(state, memory, result, blocksConsumed);
  case kOot3dMathStepToAngleSEntry:
    return ExecuteStepToAngleS(state, memory, result, context, blocksConsumed);
  case kOot3dAnimationChangeEntry:
    return ExecuteAnimationChange(state, memory, result, blocksConsumed);
  case kOot3dActorUpdateVelocityXZGravityEntry:
    return ExecuteActorUpdateVelocityXZGravity(state, memory, result, context,
                                               blocksConsumed);
  case kOot3dLinkAnimationChangeEntry:
    return ExecuteLinkAnimationChange(state, memory, result, blocksConsumed);
  case kOot3dLinkAnimationOnFrameEntry:
    return ExecuteLinkAnimationOnFrame(state, memory, result, context,
                                       blocksConsumed);
  case kOot3dSkelAnimeUpdateEntry:
    return ExecuteSkelAnimeUpdate(state, memory, result, context,
                                  blocksConsumed);
  case kOot3dActorUpdatePosEntry:
    return ExecuteActorUpdatePosition(state, memory, result, context,
                                      blocksConsumed);
  case kOot3dActorHasNoParentEntry:
    return ExecuteActorParentQuery(pc, state, memory, result, blocksConsumed,
                                   false);
  case kOot3dMathSmoothStepToFEntry:
    return ExecuteSmoothStepToF(state, memory, result, context, blocksConsumed);
  case kOot3dSkelAnimeIsFrameCrossedEntry:
    return ExecuteAnimationFrameCrossing(pc, state, memory, result,
                                         blocksConsumed);
  case kOot3dMathApproachZeroFEntry:
    return ExecuteApproachZeroF(state, memory, result, context, blocksConsumed);
  case kOot3dMathSmoothStepToSUpdateRateEntry:
    return ExecuteSmoothStep(state, memory, result, context, blocksConsumed);
  case kOot3dMathScaledStepToSEntry:
    return ExecuteScaledStepToS(state, memory, result, context, blocksConsumed);
  case kOot3dMathStepToFEntry:
    return ExecuteStepToF(state, memory, result, context, blocksConsumed);
  case kOot3dActorHasParentEntry:
    return ExecuteActorParentQuery(pc, state, memory, result, blocksConsumed,
                                   true);
  case kOot3dMathStepToSEntry:
    return ExecuteStepToS(state, memory, result, context, blocksConsumed);
  case kOot3dMathApproachFEntry:
    return ExecuteApproachF(state, memory, result, context, blocksConsumed);
  case kOot3dAnimationOnFrameImplEntry:
    return ExecuteAnimationFrameCrossing(pc, state, memory, result,
                                         blocksConsumed);
  case kOot3dActorKillEntry:
    return ExecuteActorKill(state, memory, result, blocksConsumed);
  case kOot3dActorSetScaleEntry:
    return ExecuteActorSetScale(state, memory, result, blocksConsumed);
  case kOot3dMathSmoothStepToSEntry:
    return ExecuteSmoothStepToS(state, memory, result, context, blocksConsumed);
  case kOot3dActorMoveForwardEntry:
    return ExecuteActorMoveForward(state, memory, result, context,
                                   blocksConsumed);
  case kOot3dAnimationGetLengthEntry:
    return ExecuteAnimationGetLength(state, memory, result, blocksConsumed);
  case kOot3dMeshCommandPacketSubmitEntry:
    return ExecuteMeshCommandPacketSubmit(state, memory, result,
                                          blocksConsumed);
  default:
    return false;
  }
}

} // namespace Oot3dNativeGame
