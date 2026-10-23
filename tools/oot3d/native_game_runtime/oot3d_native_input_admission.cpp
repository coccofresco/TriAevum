#include "oot3d_native_input_admission.h"

#include <algorithm>

namespace Oot3dNativeGame {

bool ParseNativeInputAdmissionMode(std::string_view value,
                                   NativeInputAdmissionMode* mode) noexcept {
    if (value == "on") {
        *mode = NativeInputAdmissionMode::On;
    } else if (value == "off") {
        *mode = NativeInputAdmissionMode::Off;
    } else if (value == "auto") {
        *mode = NativeInputAdmissionMode::Auto;
    } else {
        return false;
    }
    return true;
}

const char* NativeInputAdmissionModeName(NativeInputAdmissionMode mode) noexcept {
    switch (mode) {
        case NativeInputAdmissionMode::On: return "on";
        case NativeInputAdmissionMode::Off: return "off";
        case NativeInputAdmissionMode::Auto: return "auto";
    }
    return "on";
}

NativeInputAdmissionController::NativeInputAdmissionController(
    NativeInputAdmissionMode mode, NativeInputAdmissionTuning tuning) noexcept
    : mMode(mode), mTuning(tuning) {
    mTuning.Window = std::max<uint32_t>(1U, mTuning.Window);
    mTuning.LateThreshold =
        std::clamp<uint32_t>(mTuning.LateThreshold, 1U, mTuning.Window);
    mTuning.InitialHoldPresentations =
        std::max<uint32_t>(1U, mTuning.InitialHoldPresentations);
    mTuning.MaximumHoldPresentations = std::max(
        mTuning.InitialHoldPresentations, mTuning.MaximumHoldPresentations);
    mTuning.ProbeWindow = std::max<uint32_t>(1U, mTuning.ProbeWindow);
    mTuning.ProbeLateThreshold =
        std::clamp<uint32_t>(mTuning.ProbeLateThreshold, 1U, mTuning.ProbeWindow);
    mHold = mTuning.InitialHoldPresentations;
}

bool NativeInputAdmissionController::Admitting() const noexcept {
    switch (mMode) {
        case NativeInputAdmissionMode::On: return true;
        case NativeInputAdmissionMode::Off: return false;
        case NativeInputAdmissionMode::Auto: return !mOverlapping;
    }
    return true;
}

void NativeInputAdmissionController::ObserveRefreshFrames(
    uint64_t observed, uint64_t late) noexcept {
    late = std::min(late, observed);
    mStats.ObservedRefreshFrames += observed;
    mStats.LateRefreshFrames += late;
    // Results of frames admitted before a switch may arrive after it; an
    // overlapping controller has no window to fill.
    if (mMode != NativeInputAdmissionMode::Auto || mOverlapping) return;
    observed = std::min<uint64_t>(observed, mTuning.Window);
    for (uint64_t frame = 0; frame < observed && !mOverlapping; ++frame) {
        ObserveRefreshFrame(frame < late);
    }
}

void NativeInputAdmissionController::ObserveRefreshFrame(bool late) noexcept {
    ++mWindowObserved;
    mWindowLate += late ? 1U : 0U;
    // A probe decides on a few refresh frames, so a failed one costs at most
    // ProbeLateThreshold late presentations instead of a full window.
    const uint32_t window = mProbing ? mTuning.ProbeWindow : mTuning.Window;
    const uint32_t threshold =
        mProbing ? mTuning.ProbeLateThreshold : mTuning.LateThreshold;
    if (mWindowLate >= threshold) {
        mOverlapping = true;
        ++mStats.OverlapEntries;
        // Leaving admission again soon after a passed probe means the probe
        // was lucky: back off as for a failed probe.
        if (mProbing || mConfirmRemaining > 0U) {
            mHold = mHold > mTuning.MaximumHoldPresentations / 2U
                        ? mTuning.MaximumHoldPresentations
                        : mHold * 2U;
        }
        mProbing = false;
        mConfirmRemaining = 0;
        mHoldRemaining = mHold;
        mWindowObserved = 0;
        mWindowLate = 0;
        return;
    }
    if (mConfirmRemaining > 0U && --mConfirmRemaining == 0U) {
        mHold = mTuning.InitialHoldPresentations;
    }
    if (mWindowObserved >= window) {
        if (mProbing) {
            ++mStats.SuccessfulProbes;
            mProbing = false;
            mConfirmRemaining = mTuning.ConfirmRefreshFrames;
        }
        mWindowObserved = 0;
        mWindowLate = 0;
    }
}

bool NativeInputAdmissionController::BeginFrame() noexcept {
    if (mMode == NativeInputAdmissionMode::Auto && mOverlapping) {
        if (mHoldRemaining > 0U) --mHoldRemaining;
        if (mHoldRemaining == 0U) {
            mOverlapping = false;
            mProbing = true;
            ++mStats.Probes;
            mWindowObserved = 0;
            mWindowLate = 0;
        }
    }
    const bool admit = Admitting();
    ++(admit ? mStats.AdmittedFrames : mStats.OverlappedFrames);
    return admit;
}

NativeInputSampleDelayController::NativeInputSampleDelayController(
    NativeInputSampleDelayTuning tuning) noexcept
    : mTuning(tuning) {
    mTuning.Window = std::max<uint32_t>(1U, mTuning.Window);
    mTuning.DecreasePercent = std::min<uint32_t>(mTuning.DecreasePercent, 100U);
}

void NativeInputSampleDelayController::Configure(uint32_t maximumMicroseconds) noexcept {
    mMaximum = maximumMicroseconds;
    mDelay = std::min(mDelay, mMaximum);
}

void NativeInputSampleDelayController::Reset() noexcept {
    mDelay = 0;
    mWindowObserved = 0;
    mWindowLate = 0;
}

void NativeInputSampleDelayController::ObserveRefreshFrames(
    uint64_t observed, uint64_t late) noexcept {
    late = std::min(late, observed);
    mStats.ObservedRefreshFrames += observed;
    mStats.LateRefreshFrames += late;
    for (uint64_t frame = 0; frame < observed; ++frame) {
        ++mWindowObserved;
        mWindowLate += frame < late ? 1U : 0U;
        if (mWindowLate > mTuning.TolerableLate) {
            // React within the window: the delay is the likely cause.
            mDelay = static_cast<uint32_t>(
                static_cast<uint64_t>(mDelay) * mTuning.DecreasePercent / 100U);
            ++mStats.Decreases;
            mWindowObserved = 0;
            mWindowLate = 0;
            continue;
        }
        if (mWindowObserved >= mTuning.Window) {
            if (mWindowLate == 0U && mDelay < mMaximum) {
                mDelay = std::min(mMaximum, mDelay + mTuning.IncreaseMicroseconds);
                ++mStats.Increases;
                mStats.MaximumMicroseconds = std::max(mStats.MaximumMicroseconds, mDelay);
            }
            mWindowObserved = 0;
            mWindowLate = 0;
        }
    }
}

} // namespace Oot3dNativeGame
