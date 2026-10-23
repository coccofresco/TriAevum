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
    mWindowObserved += static_cast<uint32_t>(std::min<uint64_t>(observed, mTuning.Window));
    mWindowLate += static_cast<uint32_t>(std::min<uint64_t>(late, mTuning.Window));
    if (mWindowLate >= mTuning.LateThreshold) {
        mOverlapping = true;
        ++mStats.OverlapEntries;
        if (mProbing) {
            mHold = std::min(mTuning.MaximumHoldPresentations,
                             mHold > mTuning.MaximumHoldPresentations / 2U
                                 ? mTuning.MaximumHoldPresentations
                                 : mHold * 2U);
        }
        mProbing = false;
        mHoldRemaining = mHold;
        mWindowObserved = 0;
        mWindowLate = 0;
        return;
    }
    if (mWindowObserved >= mTuning.Window) {
        if (mProbing) {
            ++mStats.SuccessfulProbes;
            mProbing = false;
            mHold = mTuning.InitialHoldPresentations;
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

} // namespace Oot3dNativeGame
