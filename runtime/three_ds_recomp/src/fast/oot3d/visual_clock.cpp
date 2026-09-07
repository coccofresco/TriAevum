#include "fast/oot3d/visual_clock.h"

#include <algorithm>
#include <cmath>

namespace Fast::Oot3d {

VisualClock& VisualClock::Instance() {
    static VisualClock clock;
    return clock;
}

void VisualClock::Publish(double deltaSeconds, bool paused,
                          uint64_t sequence) {
    std::scoped_lock lock(mMutex);
    if (mHasSequence && sequence == mState.Sequence) {
        mState.Paused = paused;
        mState.DeltaSeconds = 0.0F;
        return;
    }
    if (mHasSequence && sequence < mState.Sequence) {
        mState.Seconds = 0.0;
        ++mState.Epoch;
    }
    const double sanitized = std::isfinite(deltaSeconds)
                                 ? std::clamp(deltaSeconds, 0.0, 0.1)
                                 : 0.0;
    mState.DeltaSeconds = paused ? 0.0F : static_cast<float>(sanitized);
    mState.Seconds += static_cast<double>(mState.DeltaSeconds);
    mState.Sequence = sequence;
    mState.Paused = paused;
    mState.Available = true;
    mHasSequence = true;
}

VisualClockState VisualClock::Snapshot() const {
    std::scoped_lock lock(mMutex);
    return mState;
}

void VisualClock::Reset() {
    std::scoped_lock lock(mMutex);
    const uint64_t nextEpoch = mState.Epoch + 1U;
    mState = {};
    mState.Epoch = nextEpoch;
    mHasSequence = false;
}

} // namespace Fast::Oot3d
