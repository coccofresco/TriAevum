#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace oot3d::recomp::a32 {
class MemoryBus;
struct GuestState;
} // namespace oot3d::recomp::a32

namespace Oot3dNativeGame {

// Native entry and field offsets recovered from code.bin. These belong to the
// A32 compatibility layer; source-recompiled gameplay must expose typed state
// instead of depending on them.
inline constexpr uint32_t kOot3dPlayerUpdateEntry = 0x001E1B54U;

struct NativeA32PlayerTimingSnapshot {
    uint32_t PlayerAddress = 0;
    uint32_t PlayStateAddress = 0;
    uint32_t TimeStateAddress = 0;
    float WorldX = 0.0F;
    float WorldY = 0.0F;
    float WorldZ = 0.0F;
    float VelocityX = 0.0F;
    float VelocityY = 0.0F;
    float VelocityZ = 0.0F;
    float ActorSpeed = 0.0F;
    float PlayerSpeed = 0.0F;
    float FloorHeight = 0.0F;
    float AnimationFrame = 0.0F;
    float AnimationPlaySpeed = 0.0F;
    float AnimationStartFrame = 0.0F;
    float AnimationEndFrame = 0.0F;
    uint32_t AnimationResource = 0;
    uint32_t ActionFunction = 0;
    uint32_t StateFlags1 = 0;
    uint32_t StateFlags2 = 0;
    uint16_t BackgroundCheckFlags = 0;
    int16_t ShapeYaw = 0;
    int16_t RandomTurnState = 0;
    int16_t RandomTurnTimer = 0;
    uint8_t AttentionPersistenceCounter = 0;
    uint8_t ActorCategory = 0;
    uint8_t AnimationMode = 0;
    uint16_t UnderwaterTimer = 0;
    int8_t MeleeWeaponActionTimer = 0;
    uint8_t MeleeWeaponComboState = 0;
    int16_t ItemActionStateOrBurnTimer = 0;
    uint8_t LedgeClimbType = 0;
    uint8_t LedgeClimbDelayTimer = 0;
    uint8_t TextboxButtonCooldownTimer = 0;
    uint8_t DamageFlickerAnimationCounter = 0;
    uint8_t DamageRunTimer = 0;
    uint8_t ItemActionCooldownTimer = 0;
    uint8_t CollisionSfxCooldownTimer = 0;
    int8_t InvincibilityTimer = 0;
    uint8_t FloorTypeTimer = 0;
    uint8_t PreviousFloorType = 0;
    int8_t RespawnDamageState = 0;
    uint8_t FairyReviveGraceTimer = 0;
    int16_t TimeStateUpdateRate = 0;
};

struct NativeA32PlayerTimingProbeStats {
    uint64_t UpdateEntriesObserved = 0;
    uint64_t SnapshotsCaptured = 0;
    uint64_t ReadFailures = 0;
    uint32_t LastPlayerAddress = 0;
    uint32_t LastPlayStateAddress = 0;
};

class NativeA32PlayerTimingProbe {
  public:
    void ObserveUpdateEntry(const oot3d::recomp::a32::GuestState& state);
    std::optional<NativeA32PlayerTimingSnapshot>
    Capture(oot3d::recomp::a32::MemoryBus& memory);

    const NativeA32PlayerTimingProbeStats& Stats() const;
    const std::string& LastError() const;

  private:
    NativeA32PlayerTimingProbeStats mStats;
    std::string mLastError;
};

} // namespace Oot3dNativeGame
