#include "oot3d_native_player_temporal_bridge.h"

#include "a32_runtime.h"
#include "oot3d_gameplay_player.h"

#include <array>

namespace Oot3dNativeGame {
namespace {

constexpr uint32_t kFloorTypeTimerOffset = 0x2489U;
constexpr uint32_t kPreviousFloorTypeOffset = 0x248BU;
constexpr uint32_t kLedgeClimbTypeOffset = 0x2278U;
constexpr uint32_t kLedgeClimbDelayTimerOffset = 0x2279U;

constexpr std::array kHookPcs{
    kOot3dPlayerDynamicWallGateCompare,
    kOot3dPlayerHighLedgeGateCompare,
    kOot3dPlayerLowLedgeGateCompare,
    kOot3dPlayerSceneCollisionReturnA,
    kOot3dPlayerSceneCollisionReturnB,
    kOot3dPlayerSceneCollisionEntry,
    kOot3dPlayerLedgeIncrementEntry,
};

} // namespace

std::span<const uint32_t> NativeA32PlayerTemporalHookPcs() noexcept {
    return kHookPcs;
}

void NativeA32PlayerTemporalBridge::ObserveBlockEntry(
    uint32_t pc, oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory,
    const oot3d::gameplay::TimeContext& time) {
    if (pc == kOot3dPlayerSceneCollisionEntry) {
        BeginCollision(state, memory, time);
    } else if (pc == kOot3dPlayerLedgeIncrementEntry) {
        ObserveLedgeIncrementPath();
    } else if (pc == kOot3dPlayerSceneCollisionReturnA ||
               pc == kOot3dPlayerSceneCollisionReturnB) {
        FinishCollision(memory, time);
    } else if (pc == kOot3dPlayerDynamicWallGateCompare ||
               pc == kOot3dPlayerHighLedgeGateCompare ||
               pc == kOot3dPlayerLowLedgeGateCompare) {
        ApplyLedgeGate(pc, state, time);
    }
}

void NativeA32PlayerTemporalBridge::Reset() noexcept {
    mStats = {};
    mPendingCollision.reset();
}

const NativeA32PlayerTemporalBridgeStats&
NativeA32PlayerTemporalBridge::Stats() const noexcept {
    return mStats;
}

void NativeA32PlayerTemporalBridge::BeginCollision(
    const oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory,
    const oot3d::gameplay::TimeContext& time) {
    ++mStats.CollisionEntries;
    if (mPendingCollision.has_value()) {
        ++mStats.ReplacedPendingEntries;
        mPendingCollision.reset();
    }

    const uint32_t playerAddress = state.r[1];
    PendingCollision pending;
    pending.PlayerAddress = playerAddress;
    pending.SimulationTick = time.SimulationTick;
    if (playerAddress == 0U ||
        !memory.Read8(playerAddress + kFloorTypeTimerOffset,
                      &pending.FloorTypeTimer) ||
        !memory.Read8(playerAddress + kPreviousFloorTypeOffset,
                      &pending.PreviousFloorType) ||
        !memory.Read8(playerAddress + kLedgeClimbTypeOffset,
                      &pending.LedgeClimbType) ||
        !memory.Read8(playerAddress + kLedgeClimbDelayTimerOffset,
                      &pending.LedgeClimbDelayTimer)) {
        ++mStats.ReadFailures;
        return;
    }
    mPendingCollision = pending;
}

void NativeA32PlayerTemporalBridge::FinishCollision(
    oot3d::recomp::a32::MemoryBus& memory,
    const oot3d::gameplay::TimeContext& time) {
    ++mStats.CollisionReturns;
    if (!mPendingCollision.has_value()) {
        ++mStats.UnmatchedReturns;
        return;
    }

    const PendingCollision pending = *mPendingCollision;
    mPendingCollision.reset();
    if (pending.SimulationTick != time.SimulationTick) {
        ++mStats.StaleEntries;
        return;
    }

    uint8_t nativeFloorTimer = 0;
    uint8_t nativeFloorType = 0;
    uint8_t nativeLedgeType = 0;
    uint8_t nativeLedgeTimer = 0;
    if (!memory.Read8(pending.PlayerAddress + kFloorTypeTimerOffset,
                      &nativeFloorTimer) ||
        !memory.Read8(pending.PlayerAddress + kPreviousFloorTypeOffset,
                      &nativeFloorType) ||
        !memory.Read8(pending.PlayerAddress + kLedgeClimbTypeOffset,
                      &nativeLedgeType) ||
        !memory.Read8(pending.PlayerAddress + kLedgeClimbDelayTimerOffset,
                      &nativeLedgeTimer)) {
        ++mStats.ReadFailures;
        return;
    }

    if (time.CrossedLogicalFrame) {
        ++mStats.LogicalFrameReturns;
    } else {
        ++mStats.IntermediateReturns;
    }

    const bool floorTypeChanged =
        nativeFloorType != pending.PreviousFloorType;
    if (floorTypeChanged) {
        ++mStats.ImmediateFloorTypeChanges;
    }
    const uint8_t expectedNativeTimer =
        floorTypeChanged
            ? 0U
            : static_cast<uint8_t>(pending.FloorTypeTimer + 1U);
    if (nativeFloorTimer != expectedNativeTimer) {
        ++mStats.NativeMutationMismatches;
    } else {
        oot3d::gameplay::PlayerFloorTypeTimerState resolved{
            pending.FloorTypeTimer,
            pending.PreviousFloorType,
        };
        oot3d::gameplay::PlayerUpdateFloorTypeTimer(
            resolved, nativeFloorType, time);
        if (resolved.Timer != nativeFloorTimer) {
            if (!memory.Write8(pending.PlayerAddress + kFloorTypeTimerOffset,
                               resolved.Timer)) {
                ++mStats.WriteFailures;
            } else {
                ++mStats.FloorTimerReconciliations;
            }
        }
    }

    const bool ledgeTypeChanged =
        nativeLedgeType != pending.LedgeClimbType;
    if (ledgeTypeChanged) {
        ++mStats.ImmediateLedgeTypeChanges;
    }
    if (pending.LedgeIncrementObserved) {
        const uint8_t expectedLedgeTimer =
            static_cast<uint8_t>(pending.LedgeClimbDelayTimer + 1U);
        const bool resetThenIncremented =
            !ledgeTypeChanged && pending.LedgeClimbDelayTimer != 0U &&
            nativeLedgeTimer == 1U;
        if (ledgeTypeChanged ||
            (nativeLedgeTimer != expectedLedgeTimer &&
             !resetThenIncremented)) {
            ++mStats.LedgeMutationMismatches;
            return;
        }
        if (resetThenIncremented) {
            ++mStats.LedgeTimerResets;
            return;
        }

        oot3d::gameplay::PlayerLedgeContactTimerState resolved{
            static_cast<double>(pending.LedgeClimbDelayTimer) *
                static_cast<double>(time.NativeUpdateRate),
            pending.LedgeClimbType,
        };
        oot3d::gameplay::PlayerUpdateLedgeContactTimer(
            resolved, nativeLedgeType, false, true, time);
        const uint8_t projectedTimer =
            oot3d::gameplay::PlayerProjectLedgeContactTimerToWire(
                resolved, time);
        if (projectedTimer != nativeLedgeTimer) {
            if (!memory.Write8(
                    pending.PlayerAddress + kLedgeClimbDelayTimerOffset,
                    projectedTimer)) {
                ++mStats.WriteFailures;
            } else {
                ++mStats.LedgeWireOverflowReconciliations;
            }
        }
        return;
    }

    const bool recognizedLedgeMutation =
        ledgeTypeChanged ? nativeLedgeTimer == 0U
                         : nativeLedgeTimer == pending.LedgeClimbDelayTimer ||
                               nativeLedgeTimer == 0U;
    if (!recognizedLedgeMutation) {
        ++mStats.LedgeMutationMismatches;
    } else if (!ledgeTypeChanged && nativeLedgeTimer == 0U &&
               pending.LedgeClimbDelayTimer != 0U) {
        ++mStats.LedgeTimerResets;
    }
}

void NativeA32PlayerTemporalBridge::ObserveLedgeIncrementPath() {
    if (!mPendingCollision.has_value()) {
        ++mStats.UnmatchedLedgeIncrementPaths;
        return;
    }
    mPendingCollision->LedgeIncrementObserved = true;
    ++mStats.LedgeIncrementPaths;
}

void NativeA32PlayerTemporalBridge::ApplyLedgeGate(
    uint32_t pc, oot3d::recomp::a32::GuestState& state,
    const oot3d::gameplay::TimeContext& time) {
    oot3d::gameplay::PlayerLedgeGate gate;
    uint8_t wireTimer = 0;
    if (pc == kOot3dPlayerDynamicWallGateCompare) {
        ++mStats.DynamicWallGateEvaluations;
        gate = oot3d::gameplay::PlayerLedgeGate::DynamicWallClimbPrompt;
        wireTimer = static_cast<uint8_t>(state.r[1]);
    } else if (pc == kOot3dPlayerHighLedgeGateCompare) {
        ++mStats.HighLedgeGateEvaluations;
        gate = oot3d::gameplay::PlayerLedgeGate::HighLedgeClimb;
        wireTimer = static_cast<uint8_t>(state.r[2]);
    } else {
        ++mStats.LowLedgeGateEvaluations;
        gate = oot3d::gameplay::PlayerLedgeGate::LowLedgeJump;
        wireTimer = static_cast<uint8_t>(state.r[1]);
    }

    const double elapsedNativeTimeUnits =
        static_cast<double>(wireTimer) *
        static_cast<double>(time.NativeUpdateRate);
    const bool reached = oot3d::gameplay::PlayerLedgeGateReached(
        elapsedNativeTimeUnits, gate);
    if (reached) {
        ++mStats.LedgeGatesReached;
    }

    // Each hook precedes the original CMP. These sentinel operands force only
    // its condition result; all original branch bodies and side effects remain.
    if (pc == kOot3dPlayerDynamicWallGateCompare) {
        state.r[1] = reached ? 13U : 12U;
    } else if (pc == kOot3dPlayerHighLedgeGateCompare) {
        state.r[1] = 0U;
        state.r[2] = reached ? 1U : 0U;
    } else {
        state.r[0] = 0U;
        state.r[1] = reached ? 1U : 0U;
    }
}

} // namespace Oot3dNativeGame
