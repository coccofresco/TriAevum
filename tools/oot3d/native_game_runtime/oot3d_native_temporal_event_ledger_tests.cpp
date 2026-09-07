#include "oot3d_native_temporal_event_ledger.h"

#include <stdexcept>

namespace {

void Expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

void RunNativeTemporalEventLedgerTests() {
    Oot3dNativeGame::NativeTemporalEventLedger ledger(2U);
    Oot3dNativeGame::NativeA32PlayerTimingSnapshot player;
    player.PlayerAddress = 0x1000U;
    player.ActionFunction = 0x2000U;
    player.AnimationResource = 0x3000U;
    player.BackgroundCheckFlags = 1U;
    player.StateFlags1 = 2U;
    player.StateFlags2 = 3U;

    oot3d::gameplay::TimeContext time;
    ledger.ObservePlayer(time, player);
    Expect(ledger.Stats().PlayerObservations == 1U &&
               ledger.Stats().EventsObserved == 0U &&
               ledger.Events().empty(),
           "temporal ledger baseline mismatch");

    time.SimulationTick = 1U;
    time.CurrentLogicalFrame = 0.5;
    player.ActionFunction = 0x2004U;
    ledger.ObservePlayer(time, player);
    Expect(ledger.Events().size() == 1U &&
               ledger.Events().front().Kind ==
                   Oot3dNativeGame::NativeTemporalEventKind::
                       PlayerActionTransition &&
               ledger.Events().front().LogicalFrame == 0.5,
           "temporal ledger action transition mismatch");

    time.SimulationTick = 2U;
    time.CurrentLogicalFrame = 1.0;
    time.LogicalFrameIndex = 1U;
    player.AnimationResource = 0x3004U;
    player.StateFlags1 = 6U;
    ledger.ObservePlayer(time, player);
    Expect(ledger.Stats().EventsObserved == 3U &&
               ledger.Stats().EventsRecorded == 2U &&
               ledger.Stats().EventsDropped == 1U,
           "temporal ledger bounded recording mismatch");

    Oot3dNativeGame::NativeTemporalEventLedger equivalent;
    auto baselinePlayer = player;
    baselinePlayer.ActionFunction = 0x2000U;
    baselinePlayer.AnimationResource = 0x3000U;
    baselinePlayer.StateFlags1 = 2U;
    time = {};
    equivalent.ObservePlayer(time, baselinePlayer);
    time.SimulationTick = 100U;
    time.CurrentLogicalFrame = 0.5;
    baselinePlayer.ActionFunction = 0x2004U;
    equivalent.ObservePlayer(time, baselinePlayer);
    time.SimulationTick = 200U;
    time.CurrentLogicalFrame = 1.0;
    time.LogicalFrameIndex = 1U;
    baselinePlayer.AnimationResource = 0x3004U;
    baselinePlayer.StateFlags1 = 6U;
    equivalent.ObservePlayer(time, baselinePlayer);
    Expect(ledger.Stats().BaselineFingerprint ==
               equivalent.Stats().BaselineFingerprint &&
               ledger.Stats().EventFingerprint ==
                   equivalent.Stats().EventFingerprint,
           "temporal ledger fingerprint depends on physical tick count");

    ledger.Reset();
    Expect(ledger.Stats().PlayerObservations == 0U &&
               ledger.Stats().EventsObserved == 0U &&
               ledger.Events().empty(),
           "temporal ledger reset mismatch");
}
