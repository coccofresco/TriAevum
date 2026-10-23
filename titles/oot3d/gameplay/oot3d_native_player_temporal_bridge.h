#pragma once

#include "oot3d_gameplay_time.h"

#include <cstdint>
#include <optional>
#include <span>

namespace oot3d::recomp::a32 {
class MemoryBus;
struct GuestState;
} // namespace oot3d::recomp::a32

namespace Oot3dNativeGame {

// Exact code.bin boundaries for Player scene collision and both callsites in
// the still-A32 Player owner. They are compatibility ABI, not source API.
inline constexpr uint32_t kOot3dPlayerSceneCollisionEntry = 0x0032EEB4U;
inline constexpr uint32_t kOot3dPlayerSceneCollisionReturnA = 0x00251708U;
inline constexpr uint32_t kOot3dPlayerSceneCollisionReturnB = 0x00251DDCU;
inline constexpr uint32_t kOot3dPlayerLedgeIncrementEntry = 0x0032F75CU;
inline constexpr uint32_t kOot3dPlayerDynamicWallGateCompare = 0x0023C8A4U;
inline constexpr uint32_t kOot3dPlayerHighLedgeGateCompare = 0x0023C8F0U;
inline constexpr uint32_t kOot3dPlayerLowLedgeGateCompare = 0x0023CAB0U;

std::span<const uint32_t> NativeA32PlayerTemporalHookPcs() noexcept;

struct NativeA32PlayerTemporalBridgeStats {
    uint64_t CollisionEntries = 0;
    uint64_t CollisionReturns = 0;
    uint64_t LogicalFrameReturns = 0;
    uint64_t IntermediateReturns = 0;
    uint64_t FloorTimerReconciliations = 0;
    uint64_t ImmediateFloorTypeChanges = 0;
    uint64_t NativeMutationMismatches = 0;
    uint64_t LedgeIncrementPaths = 0;
    uint64_t LedgeWireOverflowReconciliations = 0;
    uint64_t ImmediateLedgeTypeChanges = 0;
    uint64_t LedgeTimerResets = 0;
    uint64_t LedgeMutationMismatches = 0;
    uint64_t DynamicWallGateEvaluations = 0;
    uint64_t HighLedgeGateEvaluations = 0;
    uint64_t LowLedgeGateEvaluations = 0;
    uint64_t LedgeGatesReached = 0;
    uint64_t UnmatchedReturns = 0;
    uint64_t UnmatchedLedgeIncrementPaths = 0;
    uint64_t ReplacedPendingEntries = 0;
    uint64_t StaleEntries = 0;
    uint64_t ReadFailures = 0;
    uint64_t WriteFailures = 0;
};

// Temporary owner-boundary adapter for fields whose native A32 ABI still
// advances in 30 Hz frame units. Continuous collision executes untouched.
class NativeA32PlayerTemporalBridge final {
  public:
    void ObserveBlockEntry(
        uint32_t pc, oot3d::recomp::a32::GuestState& state,
        oot3d::recomp::a32::MemoryBus& memory,
        const oot3d::gameplay::TimeContext& time);

    void Reset() noexcept;
    const NativeA32PlayerTemporalBridgeStats& Stats() const noexcept;

  private:
    struct PendingCollision {
        uint32_t PlayerAddress = 0;
        uint64_t SimulationTick = 0;
        uint8_t FloorTypeTimer = 0;
        uint8_t PreviousFloorType = 0;
        uint8_t LedgeClimbType = 0;
        uint8_t LedgeClimbDelayTimer = 0;
        bool LedgeIncrementObserved = false;
    };

    void BeginCollision(
        const oot3d::recomp::a32::GuestState& state,
        oot3d::recomp::a32::MemoryBus& memory,
        const oot3d::gameplay::TimeContext& time);
    void FinishCollision(
        oot3d::recomp::a32::MemoryBus& memory,
        const oot3d::gameplay::TimeContext& time);
    void ObserveLedgeIncrementPath();
    void ApplyLedgeGate(
        uint32_t pc, oot3d::recomp::a32::GuestState& state,
        const oot3d::gameplay::TimeContext& time);

    NativeA32PlayerTemporalBridgeStats mStats;
    std::optional<PendingCollision> mPendingCollision;
};

} // namespace Oot3dNativeGame
