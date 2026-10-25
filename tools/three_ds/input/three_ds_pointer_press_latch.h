#pragma once

#include <atomic>

namespace ThreeDsRecomp::Input {

// A press that begins and ends between two polls of the held state is lost to
// polling: a quick tap on a touch screen often is. The device's press events
// mark the latch, possibly from another thread (Android delivers touches on
// its UI thread). A poll sees the pointer pressed while it is held or while a
// press is pending; the poll whose state is consumed clears the press.
class PointerPressLatch {
  public:
    void ObservePress() noexcept { mPending.store(true, std::memory_order_release); }

    bool Resolve(bool held, bool consume) noexcept {
        if (!consume) return held || mPending.load(std::memory_order_acquire);
        return mPending.exchange(false, std::memory_order_acq_rel) || held;
    }

    void Reset() noexcept { mPending.store(false, std::memory_order_release); }

  private:
    std::atomic<bool> mPending{false};
};

} // namespace ThreeDsRecomp::Input
