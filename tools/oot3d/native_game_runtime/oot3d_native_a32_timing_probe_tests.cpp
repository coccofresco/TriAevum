#include "oot3d_native_a32_timing_probe.h"

#include "a32_runtime.h"
#include "oot3d_native_frame_rate.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace {

class TimingProbeMemoryBus final : public oot3d::recomp::a32::MemoryBus {
  public:
    bool Read32(uint32_t address, uint32_t* value) override {
        if (value == nullptr) {
            return false;
        }
        if (address == Oot3dNativeGame::kOot3dTimeStatePointerAddress) {
            *value = TimeState;
            return true;
        }
        if (address + sizeof(uint32_t) > Bytes.size()) {
            return false;
        }
        *value = static_cast<uint32_t>(Bytes[address]) |
                 (static_cast<uint32_t>(Bytes[address + 1U]) << 8U) |
                 (static_cast<uint32_t>(Bytes[address + 2U]) << 16U) |
                 (static_cast<uint32_t>(Bytes[address + 3U]) << 24U);
        return true;
    }

    bool Write32(uint32_t address, uint32_t value) override {
        if (address == Oot3dNativeGame::kOot3dTimeStatePointerAddress) {
            TimeState = value;
            return true;
        }
        if (address + sizeof(uint32_t) > Bytes.size()) {
            return false;
        }
        for (uint32_t index = 0; index < sizeof(uint32_t); ++index) {
            Bytes[address + index] =
                static_cast<uint8_t>(value >> (index * 8U));
        }
        return true;
    }

    void WriteFloat(uint32_t address, float value) {
        if (!Write32(address, std::bit_cast<uint32_t>(value))) {
            throw std::runtime_error("timing probe test write failed");
        }
    }

    std::array<uint8_t, 0x4000U> Bytes{};
    uint32_t TimeState = 0;
};

void ExpectNear(float actual, float expected, const char* role) {
    if (std::abs(actual - expected) > 1.0e-6F) {
        throw std::runtime_error(role);
    }
}

} // namespace

void RunNativeA32TimingProbeTests() {
    Oot3dNativeGame::NativeA32PlayerTimingProbe probe;
    TimingProbeMemoryBus memory;
    if (probe.Capture(memory).has_value()) {
        throw std::runtime_error("timing probe captured without Player_Update");
    }

    constexpr uint32_t player = 0x100U;
    constexpr uint32_t playState = 0x3500U;
    constexpr uint32_t timeState = 0x3000U;
    constexpr uint32_t skelAnime = player + 0x254U;
    oot3d::recomp::a32::GuestState state{};
    state.r[0] = player;
    state.r[1] = playState;
    probe.ObserveUpdateEntry(state);

    memory.Bytes[player + 0x02U] = 2U;
    memory.WriteFloat(player + 0x28U, 10.25F);
    memory.WriteFloat(player + 0x2CU, -4.5F);
    memory.WriteFloat(player + 0x30U, 80.0F);
    memory.WriteFloat(player + 0x60U, 1.0F);
    memory.WriteFloat(player + 0x64U, -2.0F);
    memory.WriteFloat(player + 0x68U, 3.0F);
    memory.WriteFloat(player + 0x6CU, 4.0F);
    memory.WriteFloat(player + 0x84U, -5.0F);
    memory.WriteFloat(player + 0x221CU, 6.0F);
    memory.WriteFloat(skelAnime + 0x3CU, 7.5F);
    memory.WriteFloat(skelAnime + 0x40U, 0.75F);
    memory.WriteFloat(skelAnime + 0x44U, 2.0F);
    memory.WriteFloat(skelAnime + 0x48U, 18.0F);
    memory.Write32(skelAnime + 0x30U, 0x12345678U);
    memory.Write32(player + 0x1708U, 0x00432100U);
    memory.Write32(player + 0x1710U, 0xAABBCCDDU);
    memory.Write32(player + 0x1714U, 0x11223344U);
    memory.Bytes[player + 0x1220U] = 2U;
    memory.Bytes[player + 0x1221U] = 0U;
    memory.Bytes[player + 0x1222U] = 37U;
    memory.Bytes[player + 0x1223U] = 0U;
    memory.Bytes[player + 0x174FU] = 3U;
    memory.Bytes[player + 0x2224U] = 0xADU;
    memory.Bytes[player + 0x2225U] = 0x01U;
    memory.Bytes[player + 0x2228U] = 0xFDU;
    memory.Bytes[player + 0x2229U] = 5U;
    memory.Bytes[player + 0x2248U] = 0xFDU;
    memory.Bytes[player + 0x2249U] = 0xFFU;
    memory.Bytes[player + 0x2278U] = 3U;
    memory.Bytes[player + 0x2279U] = 19U;
    memory.Bytes[player + 0x227AU] = 15U;
    memory.Bytes[player + 0x227BU] = 91U;
    memory.Bytes[player + 0x227CU] = 23U;
    memory.Bytes[player + 0x247EU] = 4U;
    memory.Bytes[player + 0x2482U] = 3U;
    memory.Bytes[player + 0x2488U] = 0xFDU;
    memory.Bytes[player + 0x2489U] = 27U;
    memory.Bytes[player + 0x248BU] = 11U;
    memory.Bytes[player + 0x249EU] = 0xFEU;
    memory.Bytes[player + 0x249FU] = 29U;
    memory.Bytes[player + 0x90U] = 0x21U;
    memory.Bytes[player + 0x91U] = 0x02U;
    memory.Bytes[player + 0xBEU] = 0x00U;
    memory.Bytes[player + 0xBFU] = 0xC0U;
    memory.Bytes[skelAnime + 0x71U] = 4U;
    memory.Bytes[timeState + Oot3dNativeGame::kOot3dTimeStateUpdateRateOffset] =
        2U;
    memory.Write32(Oot3dNativeGame::kOot3dTimeStatePointerAddress, timeState);

    const auto snapshot = probe.Capture(memory);
    if (!snapshot.has_value() || snapshot->PlayerAddress != player ||
        snapshot->PlayStateAddress != playState ||
        snapshot->ActorCategory != 2U ||
        snapshot->AnimationResource != 0x12345678U ||
        snapshot->ActionFunction != 0x00432100U ||
        snapshot->StateFlags1 != 0xAABBCCDDU ||
        snapshot->StateFlags2 != 0x11223344U ||
        snapshot->RandomTurnState != 2 ||
        snapshot->RandomTurnTimer != 37 ||
        snapshot->AttentionPersistenceCounter != 3U ||
        snapshot->UnderwaterTimer != 429U ||
        snapshot->MeleeWeaponActionTimer != -3 ||
        snapshot->MeleeWeaponComboState != 5U ||
        snapshot->ItemActionStateOrBurnTimer != -3 ||
        snapshot->LedgeClimbType != 3U ||
        snapshot->LedgeClimbDelayTimer != 19U ||
        snapshot->TextboxButtonCooldownTimer != 15U ||
        snapshot->DamageFlickerAnimationCounter != 91U ||
        snapshot->DamageRunTimer != 23U ||
        snapshot->ItemActionCooldownTimer != 4U ||
        snapshot->CollisionSfxCooldownTimer != 3U ||
        snapshot->InvincibilityTimer != -3 ||
        snapshot->FloorTypeTimer != 27U ||
        snapshot->PreviousFloorType != 11U ||
        snapshot->RespawnDamageState != -2 ||
        snapshot->FairyReviveGraceTimer != 29U ||
        snapshot->BackgroundCheckFlags != 0x0221U ||
        snapshot->ShapeYaw != static_cast<int16_t>(0xC000U) ||
        snapshot->AnimationMode != 4U) {
        throw std::runtime_error("native player timing snapshot mismatch");
    }
    if (snapshot->TimeStateAddress != timeState ||
        snapshot->TimeStateUpdateRate != 2) {
        throw std::runtime_error("native animation timing source mismatch");
    }
    ExpectNear(snapshot->WorldX, 10.25F, "timing probe world x");
    ExpectNear(snapshot->WorldY, -4.5F, "timing probe world y");
    ExpectNear(snapshot->WorldZ, 80.0F, "timing probe world z");
    ExpectNear(snapshot->VelocityY, -2.0F, "timing probe velocity y");
    ExpectNear(snapshot->ActorSpeed, 4.0F, "timing probe actor speed");
    ExpectNear(snapshot->PlayerSpeed, 6.0F, "timing probe player speed");
    ExpectNear(snapshot->FloorHeight, -5.0F, "timing probe floor height");
    ExpectNear(snapshot->AnimationFrame, 7.5F, "timing probe animation frame");
    ExpectNear(snapshot->AnimationPlaySpeed, 0.75F,
               "timing probe animation speed");

    if (probe.Stats().UpdateEntriesObserved != 1U ||
        probe.Stats().SnapshotsCaptured != 1U ||
        probe.Stats().ReadFailures != 0U) {
        throw std::runtime_error("native player timing probe stats mismatch");
    }
}
