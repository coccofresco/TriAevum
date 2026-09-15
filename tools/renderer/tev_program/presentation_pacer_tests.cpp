#include "oot3d_native_presentation_pacer.h"
#include <chrono>
#include <thread>

int main() {
    using namespace Oot3dNativeGame;
    using namespace std::chrono_literals;
    for (unsigned rate : {30U, 60U, 90U}) {
        const auto period = std::chrono::nanoseconds(1'000'000'000 / rate);
        if (ResolveNativePacerDeadlineAction(-1ns, period) != NativePacerDeadlineAction::Wait) return 1;
        if (ResolveNativePacerDeadlineAction(period * 2, period) != NativePacerDeadlineAction::CarryDebt) return 2;
        if (ResolveNativePacerDeadlineAction(period * 2 + 1ns, period) != NativePacerDeadlineAction::Resync) return 3;
        if (ResolveNativePacerDeadlineAction(200ms, period) != NativePacerDeadlineAction::Resync) return 4;
    }
    // Exercise the real caller: the old 250 ms override passed the pure-policy
    // tests but retained this entire stall as presentation catch-up debt.
    NativeRealtimeRefreshPacer pacer(true, 90);
    const bool schedulingAvailable = pacer.MultimediaSchedulingActive();
    pacer.WaitForNextRefresh();
    std::this_thread::sleep_for(60ms);
    const double elapsedBefore = pacer.ElapsedSeconds();
    pacer.WaitForNextRefresh();
    if (pacer.Stats().DeadlineResyncs != 1 || pacer.Stats().CarriedDeadlineDebt != 0) return 5;
    if (pacer.ElapsedSeconds() < elapsedBefore) return 6;
    pacer.Configure(false, 90);
    pacer.WaitForNextRefresh();
    if (pacer.Stats().DeadlineResyncs != 1) return 7;
    if (pacer.MultimediaSchedulingActive()) return 8;
    pacer.Configure(true, 90);
    if (schedulingAvailable && !pacer.MultimediaSchedulingActive()) return 9;
    NativeRealtimeRefreshPacer unpaced(false, 90);
    if (unpaced.MultimediaSchedulingActive()) return 10;
    return 0;
}
