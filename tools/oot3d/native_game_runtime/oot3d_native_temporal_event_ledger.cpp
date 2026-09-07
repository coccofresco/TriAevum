#include "oot3d_native_temporal_event_ledger.h"

#include <cmath>
#include <stdexcept>

namespace Oot3dNativeGame {
namespace {

constexpr uint64_t kFnv1aOffset = 14695981039346656037ULL;
constexpr uint64_t kFnv1aPrime = 1099511628211ULL;

void HashValue(uint64_t& hash, uint64_t value) noexcept {
    for (uint32_t byte = 0; byte < 8U; ++byte) {
        hash ^= static_cast<uint8_t>(value >> (byte * 8U));
        hash *= kFnv1aPrime;
    }
}

uint64_t EncodeLogicalHalfFrame(double logicalFrame) {
    return static_cast<uint64_t>(std::llround(logicalFrame * 2.0));
}

} // namespace

std::string_view
NativeTemporalEventKindName(NativeTemporalEventKind kind) noexcept {
    switch (kind) {
    case NativeTemporalEventKind::PlayerActionTransition:
        return "player_action_transition";
    case NativeTemporalEventKind::PlayerAnimationTransition:
        return "player_animation_transition";
    case NativeTemporalEventKind::PlayerBackgroundFlagsTransition:
        return "player_background_flags_transition";
    case NativeTemporalEventKind::PlayerStateFlags1Transition:
        return "player_state_flags_1_transition";
    case NativeTemporalEventKind::PlayerStateFlags2Transition:
        return "player_state_flags_2_transition";
    }
    return "unknown";
}

NativeTemporalEventLedger::NativeTemporalEventLedger(
    size_t maximumRecordedEvents)
    : mMaximumRecordedEvents(maximumRecordedEvents) {
    mEvents.reserve(maximumRecordedEvents);
}

void NativeTemporalEventLedger::ObservePlayer(
    const oot3d::gameplay::TimeContext& time,
    const NativeA32PlayerTimingSnapshot& player) {
    ++mStats.PlayerObservations;
    if (!mPreviousPlayer.has_value() ||
        mPreviousPlayer->PlayerAddress != player.PlayerAddress) {
        if (mPreviousPlayer.has_value()) {
            ++mStats.PlayerIdentityChanges;
        }
        EstablishPlayerBaseline(player);
        mPreviousPlayer = player;
        return;
    }

    const auto& previous = *mPreviousPlayer;
    ObserveTransition(time, NativeTemporalEventKind::PlayerActionTransition,
                      player.PlayerAddress, previous.ActionFunction,
                      player.ActionFunction);
    ObserveTransition(time,
                      NativeTemporalEventKind::PlayerAnimationTransition,
                      player.PlayerAddress, previous.AnimationResource,
                      player.AnimationResource);
    ObserveTransition(
        time, NativeTemporalEventKind::PlayerBackgroundFlagsTransition,
        player.PlayerAddress, previous.BackgroundCheckFlags,
        player.BackgroundCheckFlags);
    ObserveTransition(time,
                      NativeTemporalEventKind::PlayerStateFlags1Transition,
                      player.PlayerAddress, previous.StateFlags1,
                      player.StateFlags1);
    ObserveTransition(time,
                      NativeTemporalEventKind::PlayerStateFlags2Transition,
                      player.PlayerAddress, previous.StateFlags2,
                      player.StateFlags2);
    mPreviousPlayer = player;
}

void NativeTemporalEventLedger::Reset() {
    mStats = {};
    mStats.BaselineFingerprint = kFnv1aOffset;
    mStats.EventFingerprint = kFnv1aOffset;
    mPreviousPlayer.reset();
    mEvents.clear();
}

const NativeTemporalEventLedgerStats&
NativeTemporalEventLedger::Stats() const noexcept {
    return mStats;
}

const std::vector<NativeTemporalEvent>&
NativeTemporalEventLedger::Events() const noexcept {
    return mEvents;
}

void NativeTemporalEventLedger::EstablishPlayerBaseline(
    const NativeA32PlayerTimingSnapshot& player) {
    HashValue(mStats.BaselineFingerprint, player.PlayerAddress);
    HashValue(mStats.BaselineFingerprint, player.ActionFunction);
    HashValue(mStats.BaselineFingerprint, player.AnimationResource);
    HashValue(mStats.BaselineFingerprint, player.BackgroundCheckFlags);
    HashValue(mStats.BaselineFingerprint, player.StateFlags1);
    HashValue(mStats.BaselineFingerprint, player.StateFlags2);
}

void NativeTemporalEventLedger::ObserveTransition(
    const oot3d::gameplay::TimeContext& time,
    NativeTemporalEventKind kind, uint32_t subjectAddress,
    uint64_t previousValue, uint64_t currentValue) {
    if (previousValue == currentValue) {
        return;
    }
    const uint64_t sequence = mStats.EventsObserved++;
    HashValue(mStats.EventFingerprint, static_cast<uint64_t>(kind));
    HashValue(mStats.EventFingerprint,
              EncodeLogicalHalfFrame(time.CurrentLogicalFrame));
    HashValue(mStats.EventFingerprint, subjectAddress);
    HashValue(mStats.EventFingerprint, previousValue);
    HashValue(mStats.EventFingerprint, currentValue);

    if (mEvents.size() >= mMaximumRecordedEvents) {
        ++mStats.EventsDropped;
        return;
    }
    mEvents.push_back({
        sequence,
        time.SimulationTick,
        time.CurrentLogicalFrame,
        time.LogicalFrameIndex,
        kind,
        subjectAddress,
        previousValue,
        currentValue,
    });
    ++mStats.EventsRecorded;
}

} // namespace Oot3dNativeGame
