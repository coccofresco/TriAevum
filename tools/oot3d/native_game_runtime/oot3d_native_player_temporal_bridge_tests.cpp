#include "oot3d_native_player_temporal_bridge.h"

#include "a32_runtime.h"
#include "oot3d_gameplay_player.h"
#include "oot3d_native_a32_memory.h"

#include <stdexcept>
#include <string>

namespace {

constexpr uint32_t kPlayerAddress = 0x10000000U;
constexpr uint32_t kLedgeClimbTypeOffset = 0x2278U;
constexpr uint32_t kLedgeClimbDelayTimerOffset = 0x2279U;
constexpr uint32_t kFloorTypeTimerOffset = 0x2489U;
constexpr uint32_t kPreviousFloorTypeOffset = 0x248BU;

void Expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

oot3d::gameplay::TimeContext Time(uint64_t tick, bool crossedLogicalFrame,
                                  float nativeUpdateRate = 1.0F) {
    oot3d::gameplay::TimeContext time;
    time.SimulationTick = tick;
    time.CrossedLogicalFrame = crossedLogicalFrame;
    time.NativeUpdateRate = nativeUpdateRate;
    return time;
}

void WriteFloorState(Oot3dNativeGame::NativeA32Memory& memory, uint8_t timer,
                     uint8_t floorType) {
    Expect(memory.Write8(kPlayerAddress + kFloorTypeTimerOffset, timer),
           "write floor timer");
    Expect(memory.Write8(kPlayerAddress + kPreviousFloorTypeOffset, floorType),
           "write previous floor type");
}

void WriteLedgeState(Oot3dNativeGame::NativeA32Memory& memory, uint8_t type,
                     uint8_t timer) {
    Expect(memory.Write8(kPlayerAddress + kLedgeClimbTypeOffset, type),
           "write ledge type");
    Expect(memory.Write8(kPlayerAddress + kLedgeClimbDelayTimerOffset, timer),
           "write ledge timer");
}

uint8_t Read8(Oot3dNativeGame::NativeA32Memory& memory, uint32_t address) {
    uint8_t value = 0;
    Expect(memory.Read8(address, &value), "read temporal player byte");
    return value;
}

void TestSourceFloorTimerDomain() {
    oot3d::gameplay::PlayerFloorTypeTimerState state{7U, 3U};
    oot3d::gameplay::PlayerUpdateFloorTypeTimer(state, 3U, Time(1U, false));
    Expect(state.Timer == 7U && state.PreviousFloorType == 3U,
           "intermediate substep advanced source floor timer");

    oot3d::gameplay::PlayerUpdateFloorTypeTimer(state, 3U, Time(2U, true));
    Expect(state.Timer == 8U && state.PreviousFloorType == 3U,
           "logical frame did not advance source floor timer");

    oot3d::gameplay::PlayerUpdateFloorTypeTimer(state, 4U, Time(3U, false));
    Expect(state.Timer == 0U && state.PreviousFloorType == 4U,
           "floor identity change was not immediate");

    state = {0xFFU, 4U};
    oot3d::gameplay::PlayerUpdateFloorTypeTimer(state, 4U, Time(4U, true));
    Expect(state.Timer == 0U,
           "source floor timer did not preserve native byte wraparound");
}

void TestSourceLedgeTimerDomain() {
    using oot3d::gameplay::PlayerLedgeContactTimerState;
    using oot3d::gameplay::PlayerLedgeGate;

    PlayerLedgeContactTimerState native30{0.0, 2U};
    oot3d::gameplay::PlayerUpdateLedgeContactTimer(
        native30, 2U, false, true, Time(1U, false, 2.0F));
    Expect(native30.ElapsedNativeTimeUnits == 2.0 &&
               oot3d::gameplay::PlayerProjectLedgeContactTimerToWire(
                   native30, Time(1U, false, 2.0F)) == 1U,
           "30 Hz ledge timer did not preserve native time units");

    native30.ElapsedNativeTimeUnits = 298.0;
    oot3d::gameplay::PlayerUpdateLedgeContactTimer(
        native30, 2U, false, true, Time(2U, true, 2.0F));
    oot3d::gameplay::PlayerUpdateLedgeContactTimer(
        native30, 2U, false, true, Time(3U, false, 2.0F));
    Expect(native30.ElapsedNativeTimeUnits == 300.0 &&
               oot3d::gameplay::PlayerProjectLedgeContactTimerToWire(
                   native30, Time(3U, false, 2.0F)) == 150U,
           "30 Hz ledge timer did not saturate at five seconds");

    PlayerLedgeContactTimerState enhanced60{255.0, 2U};
    oot3d::gameplay::PlayerUpdateLedgeContactTimer(
        enhanced60, 2U, false, true, Time(4U, true));
    Expect(enhanced60.ElapsedNativeTimeUnits == 256.0 &&
               oot3d::gameplay::PlayerProjectLedgeContactTimerToWire(
                   enhanced60, Time(4U, true)) == 255U,
           "60 Hz ledge timer wire projection wrapped");
    enhanced60.ElapsedNativeTimeUnits = 299.0;
    oot3d::gameplay::PlayerUpdateLedgeContactTimer(
        enhanced60, 2U, false, true, Time(5U, false));
    oot3d::gameplay::PlayerUpdateLedgeContactTimer(
        enhanced60, 2U, false, true, Time(6U, true));
    Expect(enhanced60.ElapsedNativeTimeUnits == 300.0 &&
               oot3d::gameplay::PlayerProjectLedgeContactTimerToWire(
                   enhanced60, Time(6U, true)) == 255U,
           "60 Hz source ledge timer did not saturate independently of ABI");

    oot3d::gameplay::PlayerUpdateLedgeContactTimer(
        enhanced60, 3U, false, true, Time(7U, false));
    Expect(enhanced60.Type == 3U &&
               enhanced60.ElapsedNativeTimeUnits == 0.0,
           "ledge type transition did not reset immediately");
    enhanced60.ElapsedNativeTimeUnits = 12.0;
    oot3d::gameplay::PlayerUpdateLedgeContactTimer(
        enhanced60, 3U, true, true, Time(8U, true));
    Expect(enhanced60.ElapsedNativeTimeUnits == 0.0,
           "explicit ledge reset advanced on the same tick");

    Expect(!oot3d::gameplay::PlayerLedgeGateReached(
               25.0, PlayerLedgeGate::DynamicWallClimbPrompt) &&
               oot3d::gameplay::PlayerLedgeGateReached(
                   26.0, PlayerLedgeGate::DynamicWallClimbPrompt),
           "dynamic-wall ledge gate duration mismatch");
    Expect(!oot3d::gameplay::PlayerLedgeGateReached(
               17.0, PlayerLedgeGate::HighLedgeClimb) &&
               oot3d::gameplay::PlayerLedgeGateReached(
                   18.0, PlayerLedgeGate::HighLedgeClimb),
           "high-ledge gate duration mismatch");
    Expect(!oot3d::gameplay::PlayerLedgeGateReached(
               7.0, PlayerLedgeGate::LowLedgeJump) &&
               oot3d::gameplay::PlayerLedgeGateReached(
                   8.0, PlayerLedgeGate::LowLedgeJump),
           "low-ledge gate duration mismatch");
}

void TestA32CollisionBoundaryBridge() {
    Oot3dNativeGame::NativeA32Memory memory;
    Oot3dNativeGame::NativeA32MemoryRegionConfig region;
    region.Name = "player";
    region.BaseAddress = kPlayerAddress;
    region.Size = 0x3000U;
    region.Writable = true;
    std::string error;
    Expect(memory.MapRegion(region, &error), "map temporal player state");

    oot3d::recomp::a32::GuestState guest{};
    guest.r[1] = kPlayerAddress;
    Oot3dNativeGame::NativeA32PlayerTemporalBridge bridge;

    WriteFloorState(memory, 7U, 3U);
    bridge.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dPlayerSceneCollisionEntry, guest, memory,
        Time(1U, false));
    WriteFloorState(memory, 8U, 3U);
    bridge.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dPlayerSceneCollisionReturnA, guest, memory,
        Time(1U, false));
    Expect(Read8(memory, kPlayerAddress + kFloorTypeTimerOffset) == 7U,
           "bridge retained native increment on intermediate substep");

    bridge.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dPlayerSceneCollisionEntry, guest, memory,
        Time(2U, true));
    WriteFloorState(memory, 8U, 3U);
    bridge.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dPlayerSceneCollisionReturnB, guest, memory,
        Time(2U, true));
    Expect(Read8(memory, kPlayerAddress + kFloorTypeTimerOffset) == 8U,
           "bridge suppressed increment on logical frame");

    bridge.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dPlayerSceneCollisionEntry, guest, memory,
        Time(3U, false));
    WriteFloorState(memory, 0U, 4U);
    bridge.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dPlayerSceneCollisionReturnB, guest, memory,
        Time(3U, false));
    Expect(Read8(memory, kPlayerAddress + kFloorTypeTimerOffset) == 0U &&
               Read8(memory, kPlayerAddress + kPreviousFloorTypeOffset) == 4U,
           "bridge delayed immediate floor identity change");

    bridge.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dPlayerSceneCollisionEntry, guest, memory,
        Time(4U, false));
    WriteFloorState(memory, 5U, 4U);
    bridge.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dPlayerSceneCollisionReturnB, guest, memory,
        Time(4U, false));
    Expect(Read8(memory, kPlayerAddress + kFloorTypeTimerOffset) == 5U,
           "bridge rewrote an unrecognized native mutation");

    const auto& stats = bridge.Stats();
    Expect(stats.CollisionEntries == 4U && stats.CollisionReturns == 4U,
           "bridge collision boundary counts");
    Expect(stats.LogicalFrameReturns == 1U &&
               stats.IntermediateReturns == 3U,
           "bridge temporal-domain counts");
    Expect(stats.FloorTimerReconciliations == 1U,
           "bridge reconciliation count");
    Expect(stats.ImmediateFloorTypeChanges == 1U,
           "bridge floor transition count");
    Expect(stats.NativeMutationMismatches == 1U,
           "bridge mismatch count");
}

void TestA32LedgeBoundaryBridge() {
    Oot3dNativeGame::NativeA32Memory memory;
    Oot3dNativeGame::NativeA32MemoryRegionConfig region;
    region.Name = "player";
    region.BaseAddress = kPlayerAddress;
    region.Size = 0x3000U;
    region.Writable = true;
    std::string error;
    Expect(memory.MapRegion(region, &error), "map ledge player state");

    oot3d::recomp::a32::GuestState guest{};
    guest.r[1] = kPlayerAddress;
    Oot3dNativeGame::NativeA32PlayerTemporalBridge bridge;

    WriteFloorState(memory, 7U, 3U);
    WriteLedgeState(memory, 2U, 0xFFU);
    bridge.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dPlayerSceneCollisionEntry, guest, memory,
        Time(1U, false));
    bridge.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dPlayerLedgeIncrementEntry, guest, memory,
        Time(1U, false));
    WriteFloorState(memory, 8U, 3U);
    WriteLedgeState(memory, 2U, 0U);
    bridge.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dPlayerSceneCollisionReturnB, guest, memory,
        Time(1U, false));
    Expect(Read8(memory, kPlayerAddress + kLedgeClimbDelayTimerOffset) ==
               0xFFU,
           "ledge bridge retained native byte wraparound");

    WriteFloorState(memory, 8U, 3U);
    WriteLedgeState(memory, 2U, 9U);
    bridge.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dPlayerSceneCollisionEntry, guest, memory,
        Time(2U, true));
    WriteFloorState(memory, 9U, 3U);
    WriteLedgeState(memory, 2U, 0U);
    bridge.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dPlayerSceneCollisionReturnA, guest, memory,
        Time(2U, true));
    Expect(Read8(memory, kPlayerAddress + kLedgeClimbDelayTimerOffset) == 0U,
           "ledge bridge blocked an explicit native reset");

    WriteFloorState(memory, 9U, 3U);
    WriteLedgeState(memory, 2U, 5U);
    bridge.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dPlayerSceneCollisionEntry, guest, memory,
        Time(3U, false));
    WriteFloorState(memory, 10U, 3U);
    WriteLedgeState(memory, 3U, 0U);
    bridge.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dPlayerSceneCollisionReturnA, guest, memory,
        Time(3U, false));
    Expect(Read8(memory, kPlayerAddress + kLedgeClimbTypeOffset) == 3U &&
               Read8(memory, kPlayerAddress +
                                 kLedgeClimbDelayTimerOffset) == 0U,
           "ledge bridge delayed an immediate type transition");

    WriteFloorState(memory, 10U, 3U);
    WriteLedgeState(memory, 3U, 9U);
    bridge.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dPlayerSceneCollisionEntry, guest, memory,
        Time(4U, false));
    bridge.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dPlayerLedgeIncrementEntry, guest, memory,
        Time(4U, false));
    WriteFloorState(memory, 11U, 3U);
    WriteLedgeState(memory, 3U, 1U);
    bridge.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dPlayerSceneCollisionReturnB, guest, memory,
        Time(4U, false));
    Expect(Read8(memory, kPlayerAddress + kLedgeClimbDelayTimerOffset) == 1U,
           "ledge bridge rejected native reset-then-increment");

    const auto& stats = bridge.Stats();
    Expect(stats.LedgeIncrementPaths == 2U &&
               stats.LedgeWireOverflowReconciliations == 1U,
           "ledge overflow reconciliation counts");
    Expect(stats.LedgeTimerResets == 2U &&
               stats.ImmediateLedgeTypeChanges == 1U,
           "ledge reset/type transition counts");
    Expect(stats.LedgeMutationMismatches == 0U &&
               stats.UnmatchedLedgeIncrementPaths == 0U,
           "ledge bridge reported false mismatches");
}

void TestA32LedgeConsumerGates() {
    Oot3dNativeGame::NativeA32Memory memory;
    oot3d::recomp::a32::GuestState guest{};
    Oot3dNativeGame::NativeA32PlayerTemporalBridge bridge;
    const auto time = Time(1U, false);

    guest.r[1] = 25U;
    bridge.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dPlayerDynamicWallGateCompare, guest, memory,
        time);
    Expect(guest.r[1] == 12U,
           "dynamic-wall gate opened before native duration");
    guest.r[1] = 26U;
    bridge.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dPlayerDynamicWallGateCompare, guest, memory,
        time);
    Expect(guest.r[1] == 13U,
           "dynamic-wall gate did not open at native duration");

    guest.r[2] = 17U;
    bridge.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dPlayerHighLedgeGateCompare, guest, memory,
        time);
    Expect(guest.r[1] == guest.r[2],
           "high-ledge gate opened before native duration");
    guest.r[2] = 18U;
    bridge.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dPlayerHighLedgeGateCompare, guest, memory,
        time);
    Expect(guest.r[1] < guest.r[2],
           "high-ledge gate did not open at native duration");

    guest.r[1] = 7U;
    bridge.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dPlayerLowLedgeGateCompare, guest, memory,
        time);
    Expect(guest.r[0] >= guest.r[1],
           "low-ledge gate opened before native duration");
    guest.r[1] = 8U;
    bridge.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dPlayerLowLedgeGateCompare, guest, memory,
        time);
    Expect(guest.r[0] < guest.r[1],
           "low-ledge gate did not open at native duration");

    const auto& stats = bridge.Stats();
    Expect(stats.DynamicWallGateEvaluations == 2U &&
               stats.HighLedgeGateEvaluations == 2U &&
               stats.LowLedgeGateEvaluations == 2U &&
               stats.LedgeGatesReached == 3U,
           "ledge gate telemetry mismatch");

    Oot3dNativeGame::NativeA32PlayerTemporalBridge native30Bridge;
    const auto native30Time = Time(2U, true, 2.0F);
    guest.r[1] = 12U;
    native30Bridge.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dPlayerDynamicWallGateCompare, guest, memory,
        native30Time);
    Expect(guest.r[1] == 12U,
           "native 30 Hz dynamic-wall gate opened at timer 12");
    guest.r[1] = 13U;
    native30Bridge.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dPlayerDynamicWallGateCompare, guest, memory,
        native30Time);
    Expect(guest.r[1] == 13U,
           "native 30 Hz dynamic-wall gate did not open at timer 13");

    guest.r[2] = 8U;
    native30Bridge.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dPlayerHighLedgeGateCompare, guest, memory,
        native30Time);
    Expect(guest.r[1] == guest.r[2],
           "native 30 Hz high-ledge gate opened at timer 8");
    guest.r[2] = 9U;
    native30Bridge.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dPlayerHighLedgeGateCompare, guest, memory,
        native30Time);
    Expect(guest.r[1] < guest.r[2],
           "native 30 Hz high-ledge gate did not open at timer 9");

    guest.r[1] = 3U;
    native30Bridge.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dPlayerLowLedgeGateCompare, guest, memory,
        native30Time);
    Expect(guest.r[0] >= guest.r[1],
           "native 30 Hz low-ledge gate opened at timer 3");
    guest.r[1] = 4U;
    native30Bridge.ObserveBlockEntry(
        Oot3dNativeGame::kOot3dPlayerLowLedgeGateCompare, guest, memory,
        native30Time);
    Expect(guest.r[0] < guest.r[1],
           "native 30 Hz low-ledge gate did not open at timer 4");
}

} // namespace

void RunNativePlayerTemporalBridgeTests() {
    TestSourceFloorTimerDomain();
    TestSourceLedgeTimerDomain();
    TestA32CollisionBoundaryBridge();
    TestA32LedgeBoundaryBridge();
    TestA32LedgeConsumerGates();
}
