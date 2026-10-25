#pragma once

#include <algorithm>
#include <cstdint>
#include <mutex>

namespace ThreeDsRecomp::Input {

// A touch overlay's own settings, owned and saved by the overlay (the
// platform UI). The overlay reports them; a host menu asks for changes and
// layout editing through requests the overlay takes up. Absent on hosts
// without an overlay.
struct TouchOverlayValues {
    bool Visible = true;
    // 0..100.
    int OpacityPercent = 50;
    // The controls' size, 50..200 percent.
    int SizePercent = 100;
    bool Vibration = true;
    // Hidden while a game controller is in use; a touch shows them again.
    bool HideWithController = true;
    bool operator==(const TouchOverlayValues&) const = default;
};

class TouchOverlaySettings {
  public:
    struct Requests {
        std::uint32_t Revision = 0;
        TouchOverlayValues Values;
        std::uint32_t EditLayoutSerial = 0;
        std::uint32_t ResetLayoutSerial = 0;
    };

    // The overlay: its current values (at start and after any change).
    void Report(const TouchOverlayValues& values) {
        std::lock_guard lock(mMutex);
        mPresent = true;
        mValues = Clean(values);
    }
    // The host menu: a change the overlay applies and reports back.
    void Request(const TouchOverlayValues& values) {
        std::lock_guard lock(mMutex);
        mValues = Clean(values);
        ++mRequests.Revision;
        mRequests.Values = mValues;
    }
    void RequestLayoutEditing() {
        std::lock_guard lock(mMutex);
        ++mRequests.EditLayoutSerial;
    }
    void RequestLayoutReset() {
        std::lock_guard lock(mMutex);
        ++mRequests.ResetLayoutSerial;
    }

    [[nodiscard]] bool Present() const {
        std::lock_guard lock(mMutex);
        return mPresent;
    }
    [[nodiscard]] TouchOverlayValues Values() const {
        std::lock_guard lock(mMutex);
        return mValues;
    }
    [[nodiscard]] Requests PendingRequests() const {
        std::lock_guard lock(mMutex);
        return mRequests;
    }

    static TouchOverlayValues Clean(TouchOverlayValues values) noexcept {
        values.OpacityPercent = std::clamp(values.OpacityPercent, 0, 100);
        values.SizePercent = std::clamp(values.SizePercent, 50, 200);
        return values;
    }

  private:
    mutable std::mutex mMutex;
    bool mPresent = false;
    TouchOverlayValues mValues;
    Requests mRequests;
};

inline TouchOverlaySettings& OverlayTouchSettings() noexcept {
    static TouchOverlaySettings settings;
    return settings;
}

} // namespace ThreeDsRecomp::Input
