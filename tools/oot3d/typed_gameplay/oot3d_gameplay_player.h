#pragma once

#include "oot3d_gameplay_time.h"
#include "oot3d_gameplay_types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace oot3d::gameplay {

struct PlayerMovementContextWire {
  std::array<std::byte, 0x28> Reserved00{};
  float SurfaceReference = 0.0f;
};

// Sparse but exact wire layout for the fields touched by the converted Player
// leaves. Unknown ranges stay opaque and are never copied into domain state.
struct PlayerWireState {
  Actor BaseActor;
  std::array<std::byte, 0x3> Reserved1A4{};
  std::uint8_t MovementState = 0;
  std::uint8_t Reserved1A8 = 0;
  std::int8_t HeldItemAction = 0;
  std::array<std::byte, 0x2> Reserved1AA{};
  std::int8_t ItemAction = 0;
  std::array<std::byte, 0x6> Reserved1AD{};
  std::uint8_t IdleAnimationType = 0;
  std::array<std::byte, 0xA0> Reserved1B4{};
  SkelAnime MainAnimation;
  std::array<std::byte, 0xF48> Reserved2D8{};
  std::int16_t RandomTurnState = 0;
  std::int16_t RandomTurnTimer = 0;
  GuestPtr<Actor> HeldActor;
  std::array<std::byte, 0x94> Reserved1228{};
  std::uint8_t CutsceneActionMode = 0;
  std::array<std::byte, 0x3> Reserved12BD{};
  GuestPtr<GuestFunction> CutsceneAction;
  std::array<std::byte, 0x1E> Reserved12C4{};
  std::uint16_t HaltActors = 0;
  std::array<std::byte, 0x414> Reserved12E4{};
  GuestPtr<Actor> FocusActor;
  std::array<std::byte, 0xC> Reserved16FC{};
  GuestPtr<GuestFunction> ActionFunction;
  GuestPtr<PlayerMovementContextWire> MovementContext;
  std::uint32_t StateFlags = 0;
  std::uint32_t SecondaryStateFlags = 0;
  std::array<std::byte, 0x12> Reserved1718{};
  std::uint8_t StateFlagsByte = 0;
  std::array<std::byte, 0x1E> Reserved172B{};
  std::uint8_t State1749 = 0;
  std::array<std::byte, 0x4> Reserved174A{};
  std::uint8_t BackgroundState = 0;
  std::uint8_t AttentionPersistenceCounter = 0;
  std::array<std::byte, 0xACC> Reserved1750{};
  float Speed = 0.0f;
  std::int16_t LockOnYaw = 0;
  std::array<std::byte, 0x2> Reserved2222{};
  std::uint16_t UnderwaterTimer = 0;
  std::array<std::byte, 0x2> Reserved2226{};
  std::int8_t MeleeWeaponActionTimer = 0;
  std::uint8_t MeleeWeaponComboState = 0;
  std::array<std::byte, 0x1E> Reserved222A{};
  std::int16_t ItemActionStateOrBurnTimer = 0;
  std::array<std::byte, 0x2E> Reserved224A{};
  std::uint8_t LedgeClimbType = 0;
  std::uint8_t LedgeClimbDelayTimer = 0;
  std::uint8_t TextboxButtonCooldownTimer = 0;
  std::uint8_t DamageFlickerAnimationCounter = 0;
  std::uint8_t DamageRunTimer = 0;
  std::array<std::byte, 0x201> Reserved227D{};
  std::uint8_t ItemActionCooldownTimer = 0;
  std::array<std::byte, 0x3> Reserved247F{};
  std::uint8_t CollisionSfxCooldownTimer = 0;
  std::array<std::byte, 0x5> Reserved2483{};
  std::int8_t InvincibilityTimer = 0;
  std::uint8_t FloorTypeTimer = 0;
  std::uint8_t Reserved248A = 0;
  std::uint8_t PreviousFloorType = 0;
  std::array<std::byte, 0x12> Reserved248C{};
  std::int8_t RespawnDamageState = 0;
  std::uint8_t FairyReviveGraceTimer = 0;
  std::array<std::byte, 0x518> Reserved24A0{};
  std::uint32_t RuntimeFlags = 0;
};

struct PlayerPlayStateWire {
  std::array<std::byte, 0x20AC> Reserved0000{};
  GuestPtr<PlayerWireState> Player;
  std::array<std::byte, 0x3B7D> Reserved20B0{};
  std::uint8_t State5C2D = 0;
};

static_assert(sizeof(PlayerMovementContextWire) == 0x2C);
static_assert(offsetof(PlayerWireState, MovementState) == 0x1A7);
static_assert(offsetof(PlayerWireState, HeldItemAction) == 0x1A9);
static_assert(offsetof(PlayerWireState, ItemAction) == 0x1AC);
static_assert(offsetof(PlayerWireState, IdleAnimationType) == 0x1B3);
static_assert(offsetof(PlayerWireState, MainAnimation) == 0x254);
static_assert(offsetof(PlayerWireState, RandomTurnState) == 0x1220);
static_assert(offsetof(PlayerWireState, RandomTurnTimer) == 0x1222);
static_assert(offsetof(PlayerWireState, HeldActor) == 0x1224);
static_assert(offsetof(PlayerWireState, CutsceneActionMode) == 0x12BC);
static_assert(offsetof(PlayerWireState, CutsceneAction) == 0x12C0);
static_assert(offsetof(PlayerWireState, HaltActors) == 0x12E2);
static_assert(offsetof(PlayerWireState, FocusActor) == 0x16F8);
static_assert(offsetof(PlayerWireState, ActionFunction) == 0x1708);
static_assert(offsetof(PlayerWireState, MovementContext) == 0x170C);
static_assert(offsetof(PlayerWireState, StateFlags) == 0x1710);
static_assert(offsetof(PlayerWireState, SecondaryStateFlags) == 0x1714);
static_assert(offsetof(PlayerWireState, StateFlagsByte) == 0x172A);
static_assert(offsetof(PlayerWireState, State1749) == 0x1749);
static_assert(offsetof(PlayerWireState, BackgroundState) == 0x174E);
static_assert(offsetof(PlayerWireState, AttentionPersistenceCounter) ==
              0x174F);
static_assert(offsetof(PlayerWireState, Speed) == 0x221C);
static_assert(offsetof(PlayerWireState, LockOnYaw) == 0x2220);
static_assert(offsetof(PlayerWireState, UnderwaterTimer) == 0x2224);
static_assert(offsetof(PlayerWireState, MeleeWeaponActionTimer) == 0x2228);
static_assert(offsetof(PlayerWireState, MeleeWeaponComboState) == 0x2229);
static_assert(offsetof(PlayerWireState, ItemActionStateOrBurnTimer) == 0x2248);
static_assert(offsetof(PlayerWireState, LedgeClimbType) == 0x2278);
static_assert(offsetof(PlayerWireState, LedgeClimbDelayTimer) == 0x2279);
static_assert(offsetof(PlayerWireState, TextboxButtonCooldownTimer) == 0x227A);
static_assert(offsetof(PlayerWireState, DamageFlickerAnimationCounter) ==
              0x227B);
static_assert(offsetof(PlayerWireState, DamageRunTimer) == 0x227C);
static_assert(offsetof(PlayerWireState, ItemActionCooldownTimer) == 0x247E);
static_assert(offsetof(PlayerWireState, CollisionSfxCooldownTimer) == 0x2482);
static_assert(offsetof(PlayerWireState, InvincibilityTimer) == 0x2488);
static_assert(offsetof(PlayerWireState, FloorTypeTimer) == 0x2489);
static_assert(offsetof(PlayerWireState, PreviousFloorType) == 0x248B);
static_assert(offsetof(PlayerWireState, RespawnDamageState) == 0x249E);
static_assert(offsetof(PlayerWireState, FairyReviveGraceTimer) == 0x249F);
static_assert(offsetof(PlayerWireState, RuntimeFlags) == 0x29B8);
static_assert(sizeof(PlayerWireState) == 0x29BC);
static_assert(offsetof(PlayerPlayStateWire, Player) == 0x20AC);
static_assert(offsetof(PlayerPlayStateWire, State5C2D) == 0x5C2D);
static_assert(sizeof(PlayerPlayStateWire) == 0x5C30);
static_assert(std::is_standard_layout_v<PlayerWireState>);
static_assert(std::is_trivially_copyable_v<PlayerWireState>);

struct PlayerEquipmentState {
  std::int8_t HeldItemAction = 0;
  std::uint32_t StateFlags = 0;
  bool HasHeldActor = false;
};

std::int32_t PlayerGetExplosiveHeld(
    const PlayerEquipmentState &player) noexcept;
bool PlayerHoldsHookshot(const PlayerEquipmentState &player) noexcept;
bool PlayerHoldsTwoHandedWeapon(
    const PlayerEquipmentState &player) noexcept;
bool PlayerHasFreeHookshotHand(const PlayerEquipmentState &player) noexcept;
std::uint32_t
PlayerItemInHandMask(const PlayerEquipmentState &player) noexcept;

struct PlayerFloorTypeTimerState {
  std::uint8_t Timer = 0;
  std::uint8_t PreviousFloorType = 0;
};

inline constexpr std::int8_t kPlayerHeldItemActionFishingPole = 2;
inline constexpr std::int8_t kPlayerHeldItemActionDekuStick = 6;
inline constexpr std::uint16_t
    kPlayerUnderwaterTimerSaturationLogicalFrames = 450U;

// The depth transition is authoritative on every simulation tick. Time spent
// beyond the native surface reference remains in the original 30 Hz domain.
void PlayerUpdateUnderwaterTimer(
    std::uint16_t &underwaterTimer, bool submergedBeyondSurfaceReference,
    const TimeContext &time) noexcept;

struct PlayerMeleeActionTimingState {
  std::int8_t Timer = 0;
  std::uint8_t ComboState = 0;
};

// A zero timer clears the combo state immediately. A non-zero signed timer
// approaches zero in the original 30 Hz logical-frame domain.
void PlayerUpdateMeleeActionTiming(
    PlayerMeleeActionTimingState &state,
    const TimeContext &time) noexcept;

// The melee tip/collider builder advances the combo state once per native
// invocation after the third attack. Preserve that cadence in logical frames
// while allowing the native geometry path to run on every simulation tick.
void PlayerAdvanceMeleeWeaponTipComboState(
    std::uint8_t &comboState,
    const TimeContext &time) noexcept;

// Player_ApplyDamageResponse starts this countdown at the OOT3D-authored
// duration. The damage-run window expires in the original logical-frame
// domain, while its native consumers remain active on every simulation tick.
void PlayerUpdateDamageRunTimer(
    std::uint8_t &damageRunTimer,
    const TimeContext &time) noexcept;

// Positive values are intangibility and negative values are invulnerability.
// Both approach zero in logical frames. Selected native Player actions suspend
// only the positive countdown; the caller derives that condition from OOT3D's
// action-function table.
void PlayerUpdateInvincibilityTimer(
    std::int8_t &invincibilityTimer, bool holdPositiveTimer,
    const TimeContext &time) noexcept;

// Player_Draw advances this byte before deriving the native invincibility-fog
// phase. Keep the wrapping byte and authored step, but advance its phase only
// once per original logical frame.
void PlayerAdvanceDamageFlickerAnimationCounter(
    std::uint8_t &counter, std::uint8_t nativeAdvance,
    const TimeContext &time) noexcept;

// Player_Init arms this signed state at -2 for the native respawn-damage
// paths. Negative values approach zero in logical frames; reaching zero
// dispatches the original one-shot audio path, which then changes the wire
// state to 1 for the native damage/hazard consumer.
bool PlayerAdvanceRespawnDamageState(
    std::int8_t &respawnDamageState,
    const TimeContext &time) noexcept;

// Player_UpdateRandomTurnTimer owns a signed authored-frame countdown. A
// zero timer requests the native RNG refresh only on a logical frame.
bool PlayerAdvanceRandomTurnTimer(
    std::int16_t &randomTurnTimer,
    const TimeContext &time) noexcept;

// This unsigned state measures the authored Player updates elapsed while
// attention retargeting remains active. It wraps 0xFF to zero and saturates
// at 0xFE exactly like the native byte sequence.
void PlayerAdvanceAttentionPersistenceCounter(
    std::uint8_t &counter,
    const TimeContext &time) noexcept;

struct PlayerCommonCountdownState {
  std::uint8_t ItemActionCooldownTimer = 0;
  std::uint8_t TextboxButtonCooldownTimer = 0;
  std::uint8_t FairyReviveGraceTimer = 0;
  std::uint8_t CollisionSfxCooldownTimer = 0;
};

// Three authored byte countdowns retain their original 30 Hz duration. The
// Fairy revive writer already emits round(60 / NativeUpdateRate) ticks, so
// that field must continue to advance on every real simulation tick.
void PlayerUpdateCommonCountdowns(
    PlayerCommonCountdownState &state,
    const TimeContext &time) noexcept;

// Player+0x2248 is a sum-type wire field. It is a rate-scaled burn timer while
// holding a Deku Stick, but a negative fishing state advances in logical
// frames. This helper owns only the latter interpretation.
void PlayerAdvanceFishingItemStateTowardReady(
    std::int16_t &itemActionStateOrBurnTimer,
    std::int8_t heldItemAction,
    const TimeContext &time) noexcept;

// Floor identity changes are simulation events and take effect immediately.
// Stability duration remains in the original 30 Hz logical-frame domain.
void PlayerUpdateFloorTypeTimer(
    PlayerFloorTypeTimerState &state, std::uint8_t currentFloorType,
    const TimeContext &time) noexcept;

inline constexpr double kPlayerLedgeContactSaturationNativeTimeUnits = 300.0;

enum class PlayerLedgeGate : std::uint8_t {
  DynamicWallClimbPrompt,
  HighLedgeClimb,
  LowLedgeJump,
};

struct PlayerLedgeContactTimerState {
  double ElapsedNativeTimeUnits = 0.0;
  std::uint8_t Type = 0;
};

// The native wire byte counts simulation updates, while the intended contact
// age is wall-clock time. Native time units preserve that age at both 30 Hz
// (two units per update) and 60 Hz (one unit per update).
void PlayerUpdateLedgeContactTimer(
    PlayerLedgeContactTimerState &state, std::uint8_t currentType,
    bool resetTimer, bool advanceTimer, const TimeContext &time) noexcept;

std::uint8_t PlayerProjectLedgeContactTimerToWire(
    const PlayerLedgeContactTimerState &state,
    const TimeContext &time) noexcept;

double PlayerLedgeGateNativeTimeUnits(PlayerLedgeGate gate) noexcept;
bool PlayerLedgeGateReached(double elapsedNativeTimeUnits,
                            PlayerLedgeGate gate) noexcept;

void PlayerSetRuntimeFlag200(std::uint32_t &runtimeFlags,
                             bool enabled) noexcept;

struct PlayerCutsceneActionState {
  std::uint8_t Mode = 0;
  GuestAddress Action = 0;
  std::uint16_t HaltActors = 0;
};

void PlayerSetCutsceneAction(PlayerCutsceneActionState &state,
                             GuestAddress action, std::uint8_t mode,
                             bool haltActors) noexcept;

struct PlayerCutsceneModeState {
  std::uint32_t StateFlags = 0;
  std::uint8_t CutsceneAction = 0;
  std::uint8_t StateFlagsByte = 0;
  std::int8_t ItemAction = 0;
  std::uint8_t State1749 = 0;
  std::uint8_t PlayState5C2D = 0;
  std::uint16_t GlobalState80 = 0;
};

struct PlayerCutsceneModeConstants {
  std::uint32_t BlockingStateMask = 0;
};

bool PlayerInCutsceneMode(
    const PlayerCutsceneModeState &state,
    const PlayerCutsceneModeConstants &constants) noexcept;

struct PlayerHeightState {
  std::uint32_t StateFlags = 0;
  bool AlternateBaseHeight = false;
};

struct PlayerHeightConstants {
  float DefaultFlagOffset = 0.0f;
  float RaisedFlagOffset = 0.0f;
  float AlternateBase = 0.0f;
  float DefaultBase = 0.0f;
};

float PlayerGetHeight(const PlayerHeightState &state,
                      const PlayerHeightConstants &constants) noexcept;

struct PlayerIdleAnimationState {
  std::uint32_t RuntimeFlags = 0;
  std::uint8_t BackgroundState = 0;
  std::int8_t GlobalTransitionState = 0;
};

std::int32_t PlayerSelectIdleAnimation(
    const PlayerIdleAnimationState &state, std::int32_t normalAnimation,
    std::int32_t alternateAnimation) noexcept;

struct PlayerLockOnState {
  std::uint32_t StateFlags = 0;
  bool HasFocusActor = false;
  std::uint32_t FocusActorFlags = 0;
  float Speed = 0.0f;
  std::int16_t ShapeYaw = 0;
  std::int16_t LockOnYaw = 0;
};

bool PlayerUpdateHostileLockOn(PlayerLockOnState &state,
                               float zero) noexcept;

struct PlayerSwimVerticalState {
  float VelocityY = 0.0f;
  float Gravity = 0.0f;
  float DepthInWater = 0.0f;
  float SurfaceReference = 0.0f;
  std::int32_t AnimationIndex = 0;
  std::uint32_t StateFlags = 0;
  std::uint32_t SecondaryStateFlags = 0;
  std::uint8_t MovementState = 0;
};

struct PlayerSwimVerticalConstants {
  float DefaultTerminalVelocity = 0.0f;
  float Zero = 0.0f;
  float DescendingSurfaceOffset = 0.0f;
  float PositiveVelocityDamping = 0.0f;
  float SurfaceAcceleration = 0.0f;
  float DefaultSinkThreshold = 0.0f;
  float AlternateSinkThreshold = 0.0f;
  float SinkAcceleration = 0.0f;
  float RisingTerminalVelocity = 0.0f;
  float RisingAcceleration = 0.0f;
  float DeepWaterThreshold = 0.0f;
};

void PlayerUpdateSwimVerticalVelocity(
    PlayerSwimVerticalState &state,
    const PlayerSwimVerticalConstants &constants) noexcept;

} // namespace oot3d::gameplay
