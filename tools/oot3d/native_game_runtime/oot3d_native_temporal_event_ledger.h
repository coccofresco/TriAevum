#pragma once

#include "oot3d_gameplay_time.h"
#include "oot3d_native_a32_timing_probe.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace Oot3dNativeGame {

enum class NativeTemporalEventKind : uint8_t {
    PlayerActionTransition,
    PlayerAnimationTransition,
    PlayerBackgroundFlagsTransition,
    PlayerStateFlags1Transition,
    PlayerStateFlags2Transition,
};

std::string_view
NativeTemporalEventKindName(NativeTemporalEventKind kind) noexcept;

struct NativeTemporalEvent {
    uint64_t Sequence = 0;
    uint64_t SimulationTick = 0;
    double LogicalFrame = 0.0;
    uint64_t LogicalFrameIndex = 0;
    NativeTemporalEventKind Kind =
        NativeTemporalEventKind::PlayerActionTransition;
    uint32_t SubjectAddress = 0;
    uint64_t PreviousValue = 0;
    uint64_t CurrentValue = 0;
};

struct NativeTemporalEventLedgerStats {
    uint64_t PlayerObservations = 0;
    uint64_t PlayerIdentityChanges = 0;
    uint64_t EventsObserved = 0;
    uint64_t EventsRecorded = 0;
    uint64_t EventsDropped = 0;
    uint64_t BaselineFingerprint = 14695981039346656037ULL;
    uint64_t EventFingerprint = 14695981039346656037ULL;
};

// Validation-only semantic ledger. It records transitions rather than every
// continuous sample, so equivalent 30/60 runs can compare event order and
// logical time without requiring bit-identical floating state.
class NativeTemporalEventLedger final {
  public:
    explicit NativeTemporalEventLedger(size_t maximumRecordedEvents = 4096U);

    void ObservePlayer(
        const oot3d::gameplay::TimeContext& time,
        const NativeA32PlayerTimingSnapshot& player);
    void Reset();

    const NativeTemporalEventLedgerStats& Stats() const noexcept;
    const std::vector<NativeTemporalEvent>& Events() const noexcept;

  private:
    void EstablishPlayerBaseline(
        const NativeA32PlayerTimingSnapshot& player);
    void ObserveTransition(
        const oot3d::gameplay::TimeContext& time,
        NativeTemporalEventKind kind, uint32_t subjectAddress,
        uint64_t previousValue, uint64_t currentValue);

    size_t mMaximumRecordedEvents = 0;
    NativeTemporalEventLedgerStats mStats;
    std::optional<NativeA32PlayerTimingSnapshot> mPreviousPlayer;
    std::vector<NativeTemporalEvent> mEvents;
};

} // namespace Oot3dNativeGame
