#include "fast/oot3d/presentation_settings_transaction.h"

#include <algorithm>
#include <limits>

namespace Fast::Oot3d {

PresentationSettingsValue GetPresentationSettings(
    const GraphicsSettings& settings) {
    return {
        settings.Window,
        settings.OutputWidth,
        settings.OutputHeight,
        settings.VSync,
    };
}

void SetPresentationSettings(
    GraphicsSettings& settings,
    const PresentationSettingsValue& presentation) {
    settings.Window = presentation.Window;
    settings.OutputWidth = presentation.Width;
    settings.OutputHeight = presentation.Height;
    settings.VSync = presentation.VSync;
}

bool RequiresPresentationConfirmation(
    const PresentationSettingsValue& currentApplied,
    const PresentationSettingsValue& requested) {
    return currentApplied.Window != requested.Window ||
           requested.Window == WindowMode::ExclusiveFullscreen;
}

PresentationSettingsTransaction::PresentationSettingsTransaction(
    uint32_t confirmationTimeoutMilliseconds)
    : mConfirmationTimeoutMilliseconds(
          std::max(confirmationTimeoutMilliseconds, 1U)) {
}

bool PresentationSettingsTransaction::Begin(
    const PresentationSettingsValue& currentApplied,
    const PresentationSettingsValue& requested) {
    if (mPhase == PresentationTransactionPhase::Idle) {
        if (requested == currentApplied)
            return false;
        mLastKnownGood = currentApplied;
    }

    mRequested = requested;
    mDeadlineMilliseconds = 0;
    mPhase = requested == mLastKnownGood
        ? PresentationTransactionPhase::RollbackRequested
        : PresentationTransactionPhase::ApplyRequested;
    return true;
}

bool PresentationSettingsTransaction::MarkApplied(
    const PresentationSettingsValue& applied,
    uint64_t nowMilliseconds, bool waitForVisibleConfirmation) {
    if (mPhase == PresentationTransactionPhase::ApplyRequested &&
        applied == mRequested) {
        if (!RequiresPresentationConfirmation(mLastKnownGood, mRequested)) {
            mLastKnownGood = mRequested;
            mPhase = PresentationTransactionPhase::Idle;
            mDeadlineMilliseconds = 0;
            return true;
        }
        mPhase = PresentationTransactionPhase::AwaitingConfirmation;
        mDeadlineMilliseconds = 0;
        if (!waitForVisibleConfirmation) ConfirmationVisible(nowMilliseconds);
        return true;
    }
    if (mPhase == PresentationTransactionPhase::RollbackRequested &&
        applied == mLastKnownGood) {
        CompleteRollback();
        return true;
    }
    return false;
}

bool PresentationSettingsTransaction::ConfirmationVisible(uint64_t nowMilliseconds) {
    if (mPhase != PresentationTransactionPhase::AwaitingConfirmation || mDeadlineMilliseconds != 0) return false;
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    mDeadlineMilliseconds = nowMilliseconds > maximum - mConfirmationTimeoutMilliseconds
        ? maximum : nowMilliseconds + mConfirmationTimeoutMilliseconds;
    return true;
}

bool PresentationSettingsTransaction::Confirm() {
    if (mPhase !=
        PresentationTransactionPhase::AwaitingConfirmation) {
        return false;
    }
    mLastKnownGood = mRequested;
    mPhase = PresentationTransactionPhase::Idle;
    mDeadlineMilliseconds = 0;
    return true;
}

bool PresentationSettingsTransaction::RequestRollback() {
    if (mPhase == PresentationTransactionPhase::Idle ||
        mPhase == PresentationTransactionPhase::RollbackRequested) {
        return false;
    }
    mPhase = PresentationTransactionPhase::RollbackRequested;
    mDeadlineMilliseconds = 0;
    return true;
}

bool PresentationSettingsTransaction::Advance(
    uint64_t nowMilliseconds) {
    if (mPhase !=
            PresentationTransactionPhase::AwaitingConfirmation ||
        mDeadlineMilliseconds == 0 ||
        nowMilliseconds < mDeadlineMilliseconds) {
        return false;
    }
    return RequestRollback();
}

PresentationTransactionStatus
PresentationSettingsTransaction::Status(
    uint64_t nowMilliseconds) const {
    PresentationTransactionStatus status;
    status.Phase = mPhase;
    status.Requested = mRequested;
    status.LastKnownGood = mLastKnownGood;
    if (mPhase == PresentationTransactionPhase::AwaitingConfirmation && mDeadlineMilliseconds == 0)
        status.RemainingMilliseconds = mConfirmationTimeoutMilliseconds;
    if (mPhase ==
            PresentationTransactionPhase::AwaitingConfirmation &&
        nowMilliseconds < mDeadlineMilliseconds) {
        status.RemainingMilliseconds = static_cast<uint32_t>(
            std::min<uint64_t>(
                mDeadlineMilliseconds - nowMilliseconds,
                std::numeric_limits<uint32_t>::max()));
    }
    return status;
}

const PresentationSettingsValue&
PresentationSettingsTransaction::LastKnownGood() const {
    return mLastKnownGood;
}

const PresentationSettingsValue&
PresentationSettingsTransaction::Target() const {
    return mPhase == PresentationTransactionPhase::RollbackRequested
        ? mLastKnownGood
        : mRequested;
}

void PresentationSettingsTransaction::CompleteRollback() {
    mRequested = mLastKnownGood;
    mPhase = PresentationTransactionPhase::Idle;
    mDeadlineMilliseconds = 0;
}

} // namespace Fast::Oot3d
