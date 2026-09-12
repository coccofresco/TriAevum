#pragma once

#include "oot3d_top_screen_mod_profile.h"
#include "three_ds_digital_accumulator.h"
#include "three_ds_input.h"

namespace Oot3dNativeGame {

// Original PauseInput_UpdateTouchState return to the native UI update owner.
// A compiled basic-block boundary, unlike an instruction inside a block.
inline constexpr uint32_t kTopScreenInputUpdateBoundary = 0x0041E988U;

class TopScreenInputCadence {
  public:
    // The host input layer already carries short polls into a guest refresh.
    // This owner bridges only refresh -> native update, never presentation.
    void ObserveGuest(const TopScreenExtendedInputFrame& input) noexcept {
        using namespace ThreeDsRecomp::Input;
        Latest = input;
        uint32_t held = 0;
        if (input.ZrHeld) held |= ButtonMask(Button::Zr);
        if (input.ZlHeld) held |= ButtonMask(Button::Zl);
        if (input.XHeld) held |= ButtonMask(Button::X);
        if (input.YHeld) held |= ButtonMask(Button::Y);
        if (input.DpadLeftHeld) held |= ButtonMask(Button::DpadLeft);
        if (input.DpadRightHeld) held |= ButtonMask(Button::DpadRight);
        if (input.DpadUpHeld) held |= ButtonMask(Button::DpadUp);
        Guest.Observe(held);
    }

    TopScreenExtendedInputFrame Advance() noexcept {
        using namespace ThreeDsRecomp::Input;
        const auto pressed = Guest.Consume().Pressed;
        auto result = Latest;
        result.ZrPressed = (pressed & ButtonMask(Button::Zr)) != 0;
        result.ZlPressed = (pressed & ButtonMask(Button::Zl)) != 0;
        result.XPressed = (pressed & ButtonMask(Button::X)) != 0;
        result.YPressed = (pressed & ButtonMask(Button::Y)) != 0;
        result.DpadLeftPressed = (pressed & ButtonMask(Button::DpadLeft)) != 0;
        result.DpadRightPressed = (pressed & ButtonMask(Button::DpadRight)) != 0;
        result.DpadUpPressed = (pressed & ButtonMask(Button::DpadUp)) != 0;
        ++Updates;
        ZrPressUpdates += result.ZrPressed;
        ZlPressUpdates += result.ZlPressed;
        return result;
    }

    void Reset() noexcept { Guest.Reset(); Latest = {}; }
    uint64_t Updates = 0;
    uint64_t ZrPressUpdates = 0;
    uint64_t ZlPressUpdates = 0;

  private:
    ThreeDsRecomp::Input::DigitalInputAccumulator Guest;
    TopScreenExtendedInputFrame Latest;
};

} // namespace Oot3dNativeGame
