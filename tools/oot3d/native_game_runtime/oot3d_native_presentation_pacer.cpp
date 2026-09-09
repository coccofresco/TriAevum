#include "oot3d_native_presentation_pacer.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace Oot3dNativeGame {
namespace {

constexpr auto kMaximumSpinDuration = std::chrono::microseconds(500);

#ifdef _WIN32
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
constexpr DWORD CREATE_WAITABLE_TIMER_HIGH_RESOLUTION = 0x00000002;
#endif
#endif

} // namespace

NativePacerDeadlineAction ResolveNativePacerDeadlineAction(
    std::chrono::nanoseconds lateness,
    std::chrono::nanoseconds presentationPeriod,
    uint32_t maximumRecoverablePeriods) noexcept {
    if (lateness.count() < 0 || presentationPeriod.count() <= 0) {
        return NativePacerDeadlineAction::Wait;
    }
    const auto recoverablePeriods =
        std::max<uint32_t>(1U, maximumRecoverablePeriods);
    const auto maximumRecoverableLateness =
        presentationPeriod * recoverablePeriods;
    return lateness <= maximumRecoverableLateness
               ? NativePacerDeadlineAction::CarryDebt
               : NativePacerDeadlineAction::Resync;
}

NativeRealtimeRefreshPacer::NativeRealtimeRefreshPacer(
    bool pacingAllowed, uint32_t targetRateHz)
    : mPacingAllowed(pacingAllowed),
      mStart(std::chrono::steady_clock::now()),
      mNextDeadline(mStart) {
#ifdef _WIN32
    mHighResolutionTimer = CreateWaitableTimerExW(
        nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
        TIMER_MODIFY_STATE | SYNCHRONIZE);
#endif
    Configure(true, targetRateHz);
}

NativeRealtimeRefreshPacer::~NativeRealtimeRefreshPacer() {
#ifdef _WIN32
    if (mHighResolutionTimer != nullptr) {
        CloseHandle(static_cast<HANDLE>(mHighResolutionTimer));
    }
#endif
}

void NativeRealtimeRefreshPacer::Configure(
    bool enabled, uint32_t targetRateHz) noexcept {
    const bool resolvedEnabled =
        mPacingAllowed && enabled && targetRateHz != 0U;
    if (mEnabled == resolvedEnabled && mTargetRateHz == targetRateHz) {
        return;
    }
    mEnabled = resolvedEnabled;
    mTargetRateHz = targetRateHz;
    ResetDeadline();
}

std::chrono::nanoseconds NativeRealtimeRefreshPacer::NextPeriod() noexcept {
    mNanosecondRemainder += 1'000'000'000ULL;
    const uint64_t nanoseconds = mNanosecondRemainder / mTargetRateHz;
    mNanosecondRemainder %= mTargetRateHz;
    return std::chrono::nanoseconds(nanoseconds);
}

void NativeRealtimeRefreshPacer::SleepUntil(
    std::chrono::steady_clock::time_point deadline) noexcept {
#ifdef _WIN32
    if (mHighResolutionTimer != nullptr) {
        const auto now = std::chrono::steady_clock::now();
        if (deadline <= now) {
            return;
        }
        const auto remaining =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                deadline - now);
        LARGE_INTEGER dueTime{};
        dueTime.QuadPart = -std::max<int64_t>(
            1, (remaining.count() + 99) / 100);
        if (SetWaitableTimer(static_cast<HANDLE>(mHighResolutionTimer),
                             &dueTime, 0, nullptr, nullptr, FALSE) != FALSE) {
            WaitForSingleObject(static_cast<HANDLE>(mHighResolutionTimer),
                                INFINITE);
            return;
        }
    }
#endif
    std::this_thread::sleep_until(deadline);
}

void NativeRealtimeRefreshPacer::WaitForNextRefresh() {
    if (!mEnabled) {
        return;
    }
    if (!mStarted) {
        mStarted = true;
        mNextDeadline = std::chrono::steady_clock::now();
        return;
    }

    const auto period = NextPeriod();
    mNextDeadline += period;
    auto now = std::chrono::steady_clock::now();
    if (now >= mNextDeadline) {
        const auto latenessDuration =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                now - mNextDeadline);
        const double lateness =
            std::chrono::duration<double>(latenessDuration).count();
        mStats.MaximumLatenessSeconds =
            std::max(mStats.MaximumLatenessSeconds, lateness);
        ++mStats.DeadlineMisses;
        // Recover a short late interpolation sample, not a quarter second
        // of presentations. The simulation clock already accounts for real
        // elapsed time; retaining a long presentation debt creates bursts.
        if (ResolveNativePacerDeadlineAction(
                latenessDuration, period) ==
            NativePacerDeadlineAction::Resync) {
            ++mStats.DeadlineResyncs;
            mNextDeadline = now;
            mNanosecondRemainder = 0;
        } else {
            ++mStats.CarriedDeadlineDebt;
        }
        return;
    }

    const auto spinDuration = std::min(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            kMaximumSpinDuration),
        period / 8);
    const auto coarseDeadline = mNextDeadline - spinDuration;
    if (now < coarseDeadline) {
        const auto sleepStart = now;
        SleepUntil(coarseDeadline);
        now = std::chrono::steady_clock::now();
        mStats.SleepSeconds +=
            std::chrono::duration<double>(now - sleepStart).count();
    }

    const auto spinStart = now;
    while ((now = std::chrono::steady_clock::now()) < mNextDeadline) {
        std::atomic_signal_fence(std::memory_order_seq_cst);
    }
    mStats.SpinSeconds +=
        std::chrono::duration<double>(now - spinStart).count();
    ++mStats.Waits;
}

void NativeRealtimeRefreshPacer::ObservePresentation(
    std::chrono::steady_clock::time_point presentationTime) noexcept {
    if (!mHasPresentationTime) {
        mHasPresentationTime = true;
        mLastPresentationTime = presentationTime;
        return;
    }
    const double interval =
        std::chrono::duration<double>(presentationTime -
                                      mLastPresentationTime)
            .count();
    mLastPresentationTime = presentationTime;
    if (!std::isfinite(interval) || interval < 0.0) {
        return;
    }
    ++mStats.PresentationIntervals;
    mStats.TotalIntervalSeconds += interval;
    if (mStats.PresentationIntervals == 1U) {
        mStats.MinimumIntervalSeconds = interval;
        mStats.MaximumIntervalSeconds = interval;
    } else {
        mStats.MinimumIntervalSeconds =
            std::min(mStats.MinimumIntervalSeconds, interval);
        mStats.MaximumIntervalSeconds =
            std::max(mStats.MaximumIntervalSeconds, interval);
    }
    if (mTargetRateHz != 0U) {
        const double target = 1.0 / static_cast<double>(mTargetRateHz);
        const double error = interval - target;
        mStats.MaximumIntervalErrorSeconds =
            std::max(mStats.MaximumIntervalErrorSeconds, std::abs(error));
        mStats.SquaredIntervalErrorSeconds += error * error;
    }
}

void NativeRealtimeRefreshPacer::ResetDeadline() noexcept {
    mStarted = false;
    mHasPresentationTime = false;
    mNextDeadline = std::chrono::steady_clock::now();
    mNanosecondRemainder = 0;
}

bool NativeRealtimeRefreshPacer::Enabled() const noexcept {
    return mEnabled;
}

bool NativeRealtimeRefreshPacer::HighResolutionWaitAvailable() const noexcept {
#ifdef _WIN32
    return mHighResolutionTimer != nullptr;
#else
    return false;
#endif
}

uint32_t NativeRealtimeRefreshPacer::TargetRateHz() const noexcept {
    return mTargetRateHz;
}

double NativeRealtimeRefreshPacer::ElapsedSeconds() const noexcept {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                         mStart)
        .count();
}

double NativeRealtimeRefreshPacer::MeanIntervalSeconds() const noexcept {
    return mStats.PresentationIntervals == 0U
               ? 0.0
               : mStats.TotalIntervalSeconds /
                     static_cast<double>(mStats.PresentationIntervals);
}

double NativeRealtimeRefreshPacer::RmsIntervalErrorSeconds() const noexcept {
    return mStats.PresentationIntervals == 0U
               ? 0.0
               : std::sqrt(mStats.SquaredIntervalErrorSeconds /
                           static_cast<double>(mStats.PresentationIntervals));
}

const NativeRealtimeRefreshPacerStats&
NativeRealtimeRefreshPacer::Stats() const noexcept {
    return mStats;
}

} // namespace Oot3dNativeGame
