#include "oot3d_native_a32_timing_probe.h"

#include "a32_runtime.h"
#include "oot3d_native_frame_rate.h"

#include <bit>

namespace Oot3dNativeGame {
namespace {

constexpr uint32_t kActorCategoryOffset = 0x02U;
constexpr uint32_t kActorWorldPositionOffset = 0x28U;
constexpr uint32_t kActorVelocityOffset = 0x60U;
constexpr uint32_t kActorSpeedOffset = 0x6CU;
constexpr uint32_t kActorFloorHeightOffset = 0x84U;
constexpr uint32_t kActorBackgroundCheckFlagsOffset = 0x90U;
constexpr uint32_t kActorShapeYawOffset = 0xBEU;
constexpr uint32_t kPlayerSkelAnimeOffset = 0x254U;
constexpr uint32_t kPlayerActionFunctionOffset = 0x1708U;
constexpr uint32_t kPlayerStateFlags1Offset = 0x1710U;
constexpr uint32_t kPlayerStateFlags2Offset = 0x1714U;
constexpr uint32_t kPlayerRandomTurnStateOffset = 0x1220U;
constexpr uint32_t kPlayerRandomTurnTimerOffset = 0x1222U;
constexpr uint32_t kPlayerAttentionPersistenceCounterOffset = 0x174FU;
constexpr uint32_t kPlayerSpeedOffset = 0x221CU;
constexpr uint32_t kPlayerUnderwaterTimerOffset = 0x2224U;
constexpr uint32_t kPlayerMeleeWeaponActionTimerOffset = 0x2228U;
constexpr uint32_t kPlayerMeleeWeaponComboStateOffset = 0x2229U;
constexpr uint32_t kPlayerItemActionStateOrBurnTimerOffset = 0x2248U;
constexpr uint32_t kPlayerLedgeTemporalHalfwordOffset = 0x2278U;
constexpr uint32_t kPlayerFeedbackTemporalHalfwordOffset = 0x227AU;
constexpr uint32_t kPlayerDamageRunTimerOffset = 0x227CU;
constexpr uint32_t kPlayerItemActionCooldownTimerOffset = 0x247EU;
constexpr uint32_t kPlayerCollisionSfxCooldownTimerOffset = 0x2482U;
constexpr uint32_t kPlayerFloorTemporalWordOffset = 0x2488U;
constexpr uint32_t kPlayerRespawnDamageStateOffset = 0x249EU;
constexpr uint32_t kPlayerFairyReviveGraceTimerOffset = 0x249FU;
constexpr uint32_t kSkelAnimeResourceOffset = 0x30U;
constexpr uint32_t kSkelAnimeCurrentFrameOffset = 0x3CU;
constexpr uint32_t kSkelAnimePlaySpeedOffset = 0x40U;
constexpr uint32_t kSkelAnimeStartFrameOffset = 0x44U;
constexpr uint32_t kSkelAnimeEndFrameOffset = 0x48U;
constexpr uint32_t kSkelAnimeModeOffset = 0x71U;
constexpr uint8_t kPlayerActorCategory = 2U;

bool ReadFloat(oot3d::recomp::a32::MemoryBus& memory, uint32_t address,
               float& value) {
    uint32_t encoded = 0;
    if (!memory.Read32(address, &encoded)) {
        return false;
    }
    value = std::bit_cast<float>(encoded);
    return true;
}

} // namespace

void NativeA32PlayerTimingProbe::ObserveUpdateEntry(
    const oot3d::recomp::a32::GuestState& state) {
    ++mStats.UpdateEntriesObserved;
    mStats.LastPlayerAddress = state.r[0];
    mStats.LastPlayStateAddress = state.r[1];
}

std::optional<NativeA32PlayerTimingSnapshot>
NativeA32PlayerTimingProbe::Capture(oot3d::recomp::a32::MemoryBus& memory) {
    if (mStats.LastPlayerAddress == 0U || mStats.LastPlayStateAddress == 0U) {
        mLastError = "Player_Update has not supplied a player context";
        return std::nullopt;
    }

    NativeA32PlayerTimingSnapshot snapshot;
    snapshot.PlayerAddress = mStats.LastPlayerAddress;
    snapshot.PlayStateAddress = mStats.LastPlayStateAddress;
    const uint32_t player = snapshot.PlayerAddress;
    const uint32_t skelAnime = player + kPlayerSkelAnimeOffset;

    uint8_t category = 0;
    uint8_t animationMode = 0;
    uint16_t backgroundCheckFlags = 0;
    uint16_t shapeYaw = 0;
    uint16_t timeStateUpdateRate = 0;
    uint16_t randomTurnState = 0;
    uint16_t randomTurnTimer = 0;
    uint8_t attentionPersistenceCounter = 0;
    uint16_t underwaterTimer = 0;
    uint8_t meleeWeaponActionTimer = 0;
    uint8_t meleeWeaponComboState = 0;
    uint16_t itemActionStateOrBurnTimer = 0;
    uint16_t ledgeTemporalHalfword = 0;
    uint16_t feedbackTemporalHalfword = 0;
    uint8_t damageRunTimer = 0;
    uint8_t itemActionCooldownTimer = 0;
    uint8_t collisionSfxCooldownTimer = 0;
    uint32_t floorTemporalWord = 0;
    uint8_t respawnDamageState = 0;
    uint8_t fairyReviveGraceTimer = 0;
    const bool read =
        memory.Read8(player + kActorCategoryOffset, &category) &&
        ReadFloat(memory, player + kActorWorldPositionOffset,
                  snapshot.WorldX) &&
        ReadFloat(memory, player + kActorWorldPositionOffset + 4U,
                  snapshot.WorldY) &&
        ReadFloat(memory, player + kActorWorldPositionOffset + 8U,
                  snapshot.WorldZ) &&
        ReadFloat(memory, player + kActorVelocityOffset, snapshot.VelocityX) &&
        ReadFloat(memory, player + kActorVelocityOffset + 4U,
                  snapshot.VelocityY) &&
        ReadFloat(memory, player + kActorVelocityOffset + 8U,
                  snapshot.VelocityZ) &&
        ReadFloat(memory, player + kActorSpeedOffset, snapshot.ActorSpeed) &&
        ReadFloat(memory, player + kPlayerSpeedOffset, snapshot.PlayerSpeed) &&
        ReadFloat(memory, player + kActorFloorHeightOffset,
                  snapshot.FloorHeight) &&
        ReadFloat(memory, skelAnime + kSkelAnimeCurrentFrameOffset,
                  snapshot.AnimationFrame) &&
        ReadFloat(memory, skelAnime + kSkelAnimePlaySpeedOffset,
                  snapshot.AnimationPlaySpeed) &&
        ReadFloat(memory, skelAnime + kSkelAnimeStartFrameOffset,
                  snapshot.AnimationStartFrame) &&
        ReadFloat(memory, skelAnime + kSkelAnimeEndFrameOffset,
                  snapshot.AnimationEndFrame) &&
        memory.Read32(skelAnime + kSkelAnimeResourceOffset,
                      &snapshot.AnimationResource) &&
        memory.Read32(player + kPlayerActionFunctionOffset,
                      &snapshot.ActionFunction) &&
        memory.Read32(player + kPlayerStateFlags1Offset,
                      &snapshot.StateFlags1) &&
        memory.Read32(player + kPlayerStateFlags2Offset,
                      &snapshot.StateFlags2) &&
        memory.Read16(player + kPlayerRandomTurnStateOffset,
                      &randomTurnState) &&
        memory.Read16(player + kPlayerRandomTurnTimerOffset,
                      &randomTurnTimer) &&
        memory.Read8(player + kPlayerAttentionPersistenceCounterOffset,
                     &attentionPersistenceCounter) &&
        memory.Read16(player + kPlayerUnderwaterTimerOffset,
                      &underwaterTimer) &&
        memory.Read8(player + kPlayerMeleeWeaponActionTimerOffset,
                     &meleeWeaponActionTimer) &&
        memory.Read8(player + kPlayerMeleeWeaponComboStateOffset,
                     &meleeWeaponComboState) &&
        memory.Read16(player + kPlayerItemActionStateOrBurnTimerOffset,
                      &itemActionStateOrBurnTimer) &&
        memory.Read16(player + kPlayerLedgeTemporalHalfwordOffset,
                      &ledgeTemporalHalfword) &&
        memory.Read16(player + kPlayerFeedbackTemporalHalfwordOffset,
                      &feedbackTemporalHalfword) &&
        memory.Read8(player + kPlayerDamageRunTimerOffset,
                     &damageRunTimer) &&
        memory.Read8(player + kPlayerItemActionCooldownTimerOffset,
                     &itemActionCooldownTimer) &&
        memory.Read8(player + kPlayerCollisionSfxCooldownTimerOffset,
                     &collisionSfxCooldownTimer) &&
        memory.Read32(player + kPlayerFloorTemporalWordOffset,
                      &floorTemporalWord) &&
        memory.Read8(player + kPlayerRespawnDamageStateOffset,
                     &respawnDamageState) &&
        memory.Read8(player + kPlayerFairyReviveGraceTimerOffset,
                     &fairyReviveGraceTimer) &&
        memory.Read16(player + kActorBackgroundCheckFlagsOffset,
                      &backgroundCheckFlags) &&
        memory.Read16(player + kActorShapeYawOffset, &shapeYaw) &&
        memory.Read8(skelAnime + kSkelAnimeModeOffset, &animationMode) &&
        memory.Read32(kOot3dTimeStatePointerAddress,
                      &snapshot.TimeStateAddress) &&
        snapshot.TimeStateAddress != 0U &&
        memory.Read16(snapshot.TimeStateAddress +
                          kOot3dTimeStateUpdateRateOffset,
                      &timeStateUpdateRate);
    if (!read) {
        ++mStats.ReadFailures;
        mLastError = "could not read the native Player timing state";
        return std::nullopt;
    }
    if (category != kPlayerActorCategory) {
        ++mStats.ReadFailures;
        mLastError =
            "Player_Update argument does not identify the player category";
        return std::nullopt;
    }

    snapshot.ActorCategory = category;
    snapshot.AnimationMode = animationMode;
    snapshot.RandomTurnState = std::bit_cast<int16_t>(randomTurnState);
    snapshot.RandomTurnTimer = std::bit_cast<int16_t>(randomTurnTimer);
    snapshot.AttentionPersistenceCounter = attentionPersistenceCounter;
    snapshot.UnderwaterTimer = underwaterTimer;
    snapshot.MeleeWeaponActionTimer =
        std::bit_cast<int8_t>(meleeWeaponActionTimer);
    snapshot.MeleeWeaponComboState = meleeWeaponComboState;
    snapshot.ItemActionStateOrBurnTimer =
        std::bit_cast<int16_t>(itemActionStateOrBurnTimer);
    snapshot.LedgeClimbType =
        static_cast<uint8_t>(ledgeTemporalHalfword);
    snapshot.LedgeClimbDelayTimer =
        static_cast<uint8_t>(ledgeTemporalHalfword >> 8U);
    snapshot.TextboxButtonCooldownTimer =
        static_cast<uint8_t>(feedbackTemporalHalfword);
    snapshot.DamageFlickerAnimationCounter =
        static_cast<uint8_t>(feedbackTemporalHalfword >> 8U);
    snapshot.DamageRunTimer = damageRunTimer;
    snapshot.ItemActionCooldownTimer = itemActionCooldownTimer;
    snapshot.CollisionSfxCooldownTimer = collisionSfxCooldownTimer;
    snapshot.InvincibilityTimer =
        std::bit_cast<int8_t>(static_cast<uint8_t>(floorTemporalWord));
    snapshot.FloorTypeTimer =
        static_cast<uint8_t>(floorTemporalWord >> 8U);
    snapshot.PreviousFloorType =
        static_cast<uint8_t>(floorTemporalWord >> 24U);
    snapshot.RespawnDamageState =
        std::bit_cast<int8_t>(respawnDamageState);
    snapshot.FairyReviveGraceTimer = fairyReviveGraceTimer;
    snapshot.BackgroundCheckFlags = backgroundCheckFlags;
    snapshot.ShapeYaw = static_cast<int16_t>(shapeYaw);
    snapshot.TimeStateUpdateRate = static_cast<int16_t>(timeStateUpdateRate);
    ++mStats.SnapshotsCaptured;
    mLastError.clear();
    return snapshot;
}

const NativeA32PlayerTimingProbeStats&
NativeA32PlayerTimingProbe::Stats() const {
    return mStats;
}

const std::string& NativeA32PlayerTimingProbe::LastError() const {
    return mLastError;
}

} // namespace Oot3dNativeGame
