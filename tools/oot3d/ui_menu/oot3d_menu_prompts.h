#pragma once

// The in-game menu's button prompts for each prompt family (Input's
// PromptSet: the pad in the user's hands, the keyboard, or touch): what the
// footer hints, the help card and the Help & folders tab show for each
// control. The text carries the glyphs of oot3d_menu_glyphs.h, drawn with
// the menu's letters.

#include "oot3d_menu_presentation.h"
#include "three_ds_input.h"

#include <cstdint>
#include <string>

namespace Oot3dNativeGame::Menu {

using PromptSet = ThreeDsRecomp::Input::PromptSet;

enum class PromptAction : std::uint8_t {
    Confirm,
    Back,
    TabPrevious,
    TabNext,
    // Left / right: change a value in place.
    Change,
    // Opens and closes this menu.
    Menu,
    // Opens F1.
    AllSettings,
    SaveState,
    LoadState,
    PhotoCamera,
    Fullscreen,
};

[[nodiscard]] bool IsPadPromptSet(PromptSet set) noexcept;

// The control's prompt in the family; empty when the family has none (the
// keyboard keys of save states on a pad, Back on touch).
[[nodiscard]] std::string PromptText(PromptSet set, PromptAction action);

// The footer's hints and the Resume button's keys in the family.
[[nodiscard]] Hints HintsFor(PromptSet set);

} // namespace Oot3dNativeGame::Menu
