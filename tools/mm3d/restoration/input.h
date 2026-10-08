// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from Project Restoration (leoetlino/project-restoration 8bb2a7b8),
// source/rst/input.cpp, source/game/pad.h, ui.h and common_data.h: the
// controls that replace the lower-screen buttons. Addresses are the vanilla
// v1.1 ones (data and BSS are unchanged from the v1.0 Restoration targets).
#pragma once

#include "rst_guest.h"

namespace Mm3dRestoration {

// game::pad::Button
namespace Button {
inline constexpr u32 A = 0x1, Select = 0x4, Start = 0x8, R = 0x100, L = 0x200, X = 0x400,
                     Y = 0x800, ZL = 0x4000, ZR = 0x8000;
inline constexpr u32 Right=0x10,Left=0x20,Up=0x40,Down=0x80;
}
// game::pad::TouchscreenButton
namespace TouchscreenButton {
inline constexpr u8 I = 1 << 0, II = 1 << 1, PictographBox = 1 << 2, Ocarina = 1 << 3;
}
// game::UsableButton (indices into CommonData::usable_btns) and
// ButtonIsUsable::No.
namespace UsableButton {
inline constexpr u32 I = 3, II = 4, Ocarina = 6, PictographBox = 7;
}
inline constexpr u8 kButtonIsNotUsable = 0xFF;

// game::pad::GetControllerMgr(): ControllerMgr at 0x729000, its State at
// +0x1000 (Input: buttons +0x8, new_buttons +0xC, released_buttons +0x10)
// and its TouchscreenState at +0x10ED (buttons, new_buttons).
inline constexpr u32 kControllerMgr = 0x00729000U;
inline constexpr u32 kButtons = kControllerMgr + 0x1000U + 0x8U;
inline constexpr u32 kNewButtons = kControllerMgr + 0x1000U + 0xCU;
inline constexpr u32 kReleasedButtons = kControllerMgr + 0x1000U + 0x10U;
inline constexpr u32 kTouchscreenButtons = kControllerMgr + 0x10EDU;
inline constexpr u32 kTouchscreenNewButtons = kControllerMgr + 0x10EEU;
// game::GetCommonData() at 0x7751D8; usable_btns at +0x13740.
inline constexpr u32 kUsableButtons = 0x007751D8U + 0x13740U;
// game::ui::GetScreenContext() at 0x72F5BC: active_screen +0x8, new_screen
// +0xC. GetScreen(Ocarina) is the pointer at 0x656B1C.
inline constexpr u32 kScreenContext = 0x0072F5BCU;
inline constexpr u32 kOcarinaScreen = 0x00656B1CU;

// game::ui::CheckCurrentScreen(Ocarina).
inline bool OcarinaScreenActive(Guest& guest) {
    return guest.Read32(kScreenContext + 0x8U) == guest.Read32(kOcarinaScreen) &&
           guest.Read32(kScreenContext + 0xCU) == 0U;
}

class Input {
  public:
    // rst::UpdatePadState: ZL is the Pictograph Box; with ZR held, A plays
    // the ocarina and X/Y use the items of the I/II buttons (A stays set
    // while swimming); unusable touch buttons are cleared.
    void UpdatePadState(Guest& guest, bool isSwimming) {
        mButtons = guest.Read8(kTouchscreenButtons);
        mNewButtons = guest.Read8(kTouchscreenNewButtons);
        u32 buttons = guest.Read32(kButtons);
        u32 newButtons = guest.Read32(kNewButtons);
        const auto setTouch = [&](u32 trigger, u8 touch, bool clear) {
            if ((buttons & trigger) != 0U) {
                mButtons |= touch;
                if (clear) {
                    buttons &= ~trigger;
                }
            }
            if ((newButtons & trigger) != 0U) {
                mNewButtons |= touch;
                if (clear) {
                    newButtons &= ~trigger;
                }
            }
        };
        setTouch(Button::ZL, TouchscreenButton::PictographBox, false);
        if ((buttons & Button::ZR) != 0U) {
            setTouch(Button::A, TouchscreenButton::Ocarina, !isSwimming);
            setTouch(Button::X, TouchscreenButton::I, true);
            setTouch(Button::Y, TouchscreenButton::II, true);
        }
        guest.Write32(kButtons, buttons);
        guest.Write32(kNewButtons, newButtons);
        u8 touchButtons = mButtons;
        u8 touchNewButtons = mNewButtons;
        const auto unsetIfNotUsable = [&](u8 touch, u32 usable) {
            if (guest.Read8(kUsableButtons + usable) == kButtonIsNotUsable) {
                touchButtons &= static_cast<u8>(~touch);
                touchNewButtons &= static_cast<u8>(~touch);
            }
        };
        unsetIfNotUsable(TouchscreenButton::I, UsableButton::I);
        unsetIfNotUsable(TouchscreenButton::II, UsableButton::II);
        unsetIfNotUsable(TouchscreenButton::Ocarina, UsableButton::Ocarina);
        unsetIfNotUsable(TouchscreenButton::PictographBox, UsableButton::PictographBox);
        guest.Write8(kTouchscreenButtons, touchButtons);
        guest.Write8(kTouchscreenNewButtons, touchNewButtons);
    }

    // rst::UpdatePadStateForOcarina: on the ocarina screen ZL and ZR also
    // play L and R.
    static void UpdatePadStateForOcarina(Guest& guest) {
        if (!OcarinaScreenActive(guest)) {
            return;
        }
        for (const u32 address : {kButtons, kNewButtons, kReleasedButtons}) {
            u32 value = guest.Read32(address);
            if ((value & Button::ZL) != 0U) {
                value |= Button::L;
            }
            if ((value & Button::ZR) != 0U) {
                value |= Button::R;
            }
            guest.Write32(address, value);
        }
    }

    // rst::ui::items::IsItemAssignRequested / GetItemAssignIndex: X and Y
    // assign the selected item directly, as the I and II touch buttons do.
    // They read the touch state UpdatePadState computed, before its
    // usability filter.
    bool IsItemAssignRequested(Guest& guest) const {
        return (guest.Read32(kNewButtons) & (Button::X | Button::Y)) != 0U ||
               (mNewButtons & (TouchscreenButton::I | TouchscreenButton::II)) != 0U;
    }
    int GetItemAssignIndex(Guest& guest) const {
        const u32 newButtons = guest.Read32(kNewButtons);
        if ((newButtons & Button::X) != 0U) {
            return 1;
        }
        if ((newButtons & Button::Y) != 0U) {
            return 2;
        }
        if ((mNewButtons & TouchscreenButton::I) != 0U) {
            return 0;
        }
        if ((mNewButtons & TouchscreenButton::II) != 0U) {
            return 3;
        }
        return 1; // unreachable in Restoration
    }

    // s_touchscreen_state survives between frames in Restoration.
    u8 mButtons = 0;
    u8 mNewButtons = 0;
};

} // namespace Mm3dRestoration
