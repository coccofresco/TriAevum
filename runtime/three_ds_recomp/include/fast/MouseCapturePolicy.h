#pragma once

namespace Fast {

// Host cursor ownership is independent from guest buttons and frame pacing.
class MouseCapturePolicy {
  public:
    bool Request(bool requested) {
        mRequested = requested;
        return requested && !mReleased;
    }
    void Release() { mReleased = true; }
    bool Released() const { return mReleased; }
    bool ResumeClick(bool hostUiOwnsMouse) {
        if (!mReleased || !mRequested || hostUiOwnsMouse) return false;
        mReleased = false;
        mResumeClickHeld = true;
        return true;
    }
    bool ConsumeClickRelease() {
        const bool consumed = mResumeClickHeld;
        mResumeClickHeld = false;
        return consumed;
    }
    bool ResumeClickHeld() const { return mResumeClickHeld; }

  private:
    bool mRequested = false;
    bool mReleased = false;
    bool mResumeClickHeld = false;
};

} // namespace Fast
