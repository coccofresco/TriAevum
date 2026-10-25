#pragma once

#include "three_ds_input.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>

namespace ThreeDsRecomp::Input {

// On-screen controls (the Android touch overlay). The platform UI thread
// writes them and the input poll reads them; the poll merges them with the
// physical devices before its own bindings and edge handling, so they are one
// more source of the same 3DS controls, not a second input owner.
// Buttons keep a held and a pending-press bit per DigitalControl, so a tap
// shorter than a poll still reaches the poll the guest consumes. Sticks keep
// their last native vector (-1..1, Y up). Host actions are requests the poll
// takes once.
class VirtualPad {
  public:
    struct Sample {
        std::uint32_t Held = 0;
        std::uint32_t Pressed = 0;
        std::array<float, 2> CirclePad{};
        std::array<float, 2> CStick{};
    };

    static constexpr std::uint32_t Bit(DigitalControl control) noexcept {
        return 1U << static_cast<std::uint32_t>(control);
    }

    void SetControl(DigitalControl control, bool pressed) noexcept {
        const std::uint32_t bit = Bit(control);
        if (pressed) {
            mHeld.fetch_or(bit, std::memory_order_acq_rel);
            mPending.fetch_or(bit, std::memory_order_acq_rel);
        } else {
            mHeld.fetch_and(~bit, std::memory_order_acq_rel);
        }
    }
    void SetCirclePad(float x, float y) noexcept { mCirclePad.store(Pack(x, y), std::memory_order_release); }
    void SetCStick(float x, float y) noexcept { mCStick.store(Pack(x, y), std::memory_order_release); }
    void RequestMenu() noexcept { mMenuRequested.store(true, std::memory_order_release); }
    void RequestSettings() noexcept { mSettingsRequested.store(true, std::memory_order_release); }

    // Cancel, focus loss, pause: nothing stays held and no press is delivered.
    void ReleaseAll() noexcept {
        mHeld.store(0, std::memory_order_release);
        mPending.store(0, std::memory_order_release);
        mCirclePad.store(0, std::memory_order_release);
        mCStick.store(0, std::memory_order_release);
    }

    Sample Read(bool consume) noexcept {
        Sample sample;
        sample.Held = mHeld.load(std::memory_order_acquire);
        sample.Pressed = consume ? mPending.exchange(0, std::memory_order_acq_rel)
                                 : mPending.load(std::memory_order_acquire);
        sample.CirclePad = Unpack(mCirclePad.load(std::memory_order_acquire));
        sample.CStick = Unpack(mCStick.load(std::memory_order_acquire));
        return sample;
    }
    bool TakeMenuRequest() noexcept { return mMenuRequested.exchange(false, std::memory_order_acq_rel); }
    bool TakeSettingsRequest() noexcept { return mSettingsRequested.exchange(false, std::memory_order_acq_rel); }

  private:
    static std::uint64_t Pack(float x, float y) noexcept {
        const auto clean = [](float value) { return std::isfinite(value) ? std::clamp(value, -1.0F, 1.0F) : 0.0F; };
        return std::bit_cast<std::uint32_t>(clean(x)) |
               (static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(clean(y))) << 32U);
    }
    static std::array<float, 2> Unpack(std::uint64_t packed) noexcept {
        return {std::bit_cast<float>(static_cast<std::uint32_t>(packed)),
                std::bit_cast<float>(static_cast<std::uint32_t>(packed >> 32U))};
    }

    std::atomic<std::uint32_t> mHeld{0};
    std::atomic<std::uint32_t> mPending{0};
    std::atomic<std::uint64_t> mCirclePad{0};
    std::atomic<std::uint64_t> mCStick{0};
    std::atomic<bool> mMenuRequested{false};
    std::atomic<bool> mSettingsRequested{false};
};

static_assert(kDigitalControlCount <= 32U, "VirtualPad keeps one bit per DigitalControl");

// The 3DS HID PAD bits (A, B, Select, Start, Right, Left, Up, Down, R, L, X,
// Y, -, -, ZL, ZR) as DigitalControls. False for bits without a control.
inline bool DigitalControlForHidBit(unsigned bit, DigitalControl* control) noexcept {
    constexpr std::array<DigitalControl, 16> kControls{
        DigitalControl::A,        DigitalControl::B,         DigitalControl::Select,  DigitalControl::Start,
        DigitalControl::DpadRight, DigitalControl::DpadLeft, DigitalControl::DpadUp,  DigitalControl::DpadDown,
        DigitalControl::R,        DigitalControl::L,         DigitalControl::X,       DigitalControl::Y,
        DigitalControl::Count,    DigitalControl::Count,     DigitalControl::Zl,      DigitalControl::Zr,
    };
    if (bit >= kControls.size() || kControls[bit] == DigitalControl::Count) return false;
    *control = kControls[bit];
    return true;
}

// The process's on-screen controls. Inert (never written) on hosts without a
// touch overlay.
inline VirtualPad& TouchOverlayPad() noexcept {
    static VirtualPad pad;
    return pad;
}

} // namespace ThreeDsRecomp::Input
