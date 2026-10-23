// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from Project Restoration (leoetlino/project-restoration 8bb2a7b8),
// source/rst/main.cpp, game/states/state.h, game/ui.cpp and
// game/ui/screens/*.h: what rst::Calc reads and writes. Its guest calls are
// made by the runtime glue (mm3d_restoration.cpp); these helpers only touch
// memory. Addresses are vanilla v1.1 (data unchanged; code through V110).
#pragma once

#include "input.h"
#include "rst_guest.h"
#include "v110_address.h"

#include <optional>

namespace Mm3dRestoration {

// game::State: type (StateType, u8) at +0x13D, pad_state (pad::State) at
// +0x28 (its input.new_buttons at +0x28 + 0xC).
inline constexpr u32 kStateType = 0x13DU;
inline constexpr u32 kStateNewButtons = 0x28U + 0xCU;
inline constexpr u8 kStateFirstGame = 1;
inline constexpr u8 kStatePlay = 4;

// game::ui::GetScreen: the screen pointers at 0x656B00 + 4 * ScreenType.
inline constexpr u32 kGearScreen = 0x00656B0CU;    // ScreenType::Quest (Gear)
inline constexpr u32 kItemsScreen = 0x00656B10U;   // ScreenType::Items
inline constexpr u32 kMapScreen = 0x00656B14U;     // ScreenType::Map
inline constexpr u32 kScheduleScreen = 0x00656B08U; // ScreenType::Schedule
// GearScreen::pressed_btn_option (+0x198).
inline constexpr u32 kGearPressedOption = 0x198U;
// The ocarina screen: layouts at +0x10 (ocarina) and +0x14 (song list),
// whether the ocarina layout is the current menu at +0x40, transitioning at
// +0x41.
inline constexpr u32 kOcarinaLayout = 0x10U, kSongListLayout = 0x14U, kOcarinaIsMenu = 0x40U,
                     kOcarinaTransitioning = 0x41U;

// Guest functions (v1.0 reference -> v1.1).
// game::sound::PlayEffect(EffectId) (ref 0x2006E4).
inline constexpr u32 kPlayEffect = Mm3dDekui::V110(0x002006E4U);
// The ocarina layout's and the song list layout's close transitions
// (ref 0x5F54DC, 0x5F7DC8).
inline constexpr u32 kCloseOcarinaLayout = Mm3dDekui::V110(0x005F54DCU);
inline constexpr u32 kCloseSongListLayout = Mm3dDekui::V110(0x005F7DC8U);
static_assert(kPlayEffect == 0x002004D4U && kCloseOcarinaLayout != 0 && kCloseSongListLayout != 0);

inline constexpr u32 NA_SE_SY_DECIDE = 0x01000774U;
inline constexpr u32 NA_SE_SY_QUEST_CLEAR = 0x010007DDU;

// game::ui::CheckCurrentScreen for a screen pointer address.
inline bool ScreenActive(Guest& guest, u32 screenPointer) {
    return guest.Read32(kScreenContext + 0x8U) == guest.Read32(screenPointer) &&
           guest.Read32(kScreenContext + 0xCU) == 0U;
}

// rst::IsStartOrSelectPressed.
inline bool StartOrSelectPressed(Guest& guest, u32 gctx) {
    return gctx != 0U && (guest.Read32(gctx + kStateNewButtons) & (Button::Start | Button::Select)) != 0U;
}

// rst::UiGearScreenUpdate: Start or Select leaves the Gear screen as its
// option button does.
inline void UiGearScreenUpdate(Guest& guest, u32 gctx) {
    if (!ScreenActive(guest, kGearScreen) || !StartOrSelectPressed(guest, gctx)) {
        return;
    }
    guest.Write8(guest.Read32(kGearScreen) + kGearPressedOption, 1U);
}

// rst::UiOcarinaScreenUpdate, up to its guest calls: on Start or Select the
// ocarina screen starts transitioning and the current layout is closed (the
// ocarina's, which opens the song list, or the song list's). Returns the
// close function and its layout, or nothing.
struct LayoutCall {
    u32 Function = 0;
    u32 Layout = 0;
};
inline std::optional<LayoutCall> UiOcarinaScreenUpdate(Guest& guest, u32 gctx) {
    if (!ScreenActive(guest, kOcarinaScreen) || !StartOrSelectPressed(guest, gctx)) {
        return std::nullopt;
    }
    const u32 screen = guest.Read32(kOcarinaScreen);
    guest.Write8(screen + kOcarinaTransitioning, 1U);
    if (guest.Read8(screen + kOcarinaIsMenu) != 0U) {
        return LayoutCall{kCloseOcarinaLayout, guest.Read32(screen + kOcarinaLayout)};
    }
    return LayoutCall{kCloseSongListLayout, guest.Read32(screen + kSongListLayout)};
}

} // namespace Mm3dRestoration
