#pragma once

// Draws the in-game menu model as native UI primitives on the integrated UI
// canvas: a centred translucent window with a tab bar, the rows of the
// current tab on the left, the focused row's help and choices on the right,
// a status line, the control hints and an optional modal question. Colours,
// proportions and the controller flow follow Dusklight's settings menu; the
// text comes from the baked menu fonts.

#include "oot3d_menu_model.h"
#include "oot3d_ui/ui_primitives.h"

#include <cstddef>
#include <vector>

namespace Oot3dNativeGame::Menu {

// 1 dp is one pixel of a 1080-line window; the canvas is 240 units high.
inline constexpr float kDp = 240.0F / 1080.0F;

struct Palette {
    oot3d::ui::UiColor Text{224.0F / 255.0F, 219.0F / 255.0F, 200.0F / 255.0F, 1.0F};
    oot3d::ui::UiColor Accent{194.0F / 255.0F, 164.0F / 255.0F, 45.0F / 255.0F, 1.0F};
    oot3d::ui::UiColor Border{146.0F / 255.0F, 135.0F / 255.0F, 91.0F / 255.0F, 1.0F};
    oot3d::ui::UiColor Surface{21.0F / 255.0F, 22.0F / 255.0F, 16.0F / 255.0F, 0.90F};
    oot3d::ui::UiColor Control{17.0F / 255.0F, 16.0F / 255.0F, 10.0F / 255.0F, 0.20F};
    oot3d::ui::UiColor Interactive{204.0F / 255.0F, 184.0F / 255.0F, 119.0F / 255.0F, 0.20F};
    oot3d::ui::UiColor Selected{204.0F / 255.0F, 184.0F / 255.0F, 119.0F / 255.0F, 0.40F};
    oot3d::ui::UiColor Error{204.0F / 255.0F, 68.0F / 255.0F, 68.0F / 255.0F, 1.0F};
    oot3d::ui::UiColor Success{68.0F / 255.0F, 204.0F / 255.0F, 85.0F / 255.0F, 1.0F};
    oot3d::ui::UiColor Backdrop{0.0F, 0.0F, 0.0F, 0.45F};
};

struct Hints {
    std::string Confirm = "A";
    std::string Back = "B";
    std::string Tabs = "LB / RB";
    std::string Change = "\xE2\x80\xB9 \xE2\x80\xBA"; // ‹ ›
    std::string Close = "Esc / R3";
};

// Appends the menu for one frame; tells the model how many rows are visible.
// Returns the number of primitives appended.
std::size_t AppendMenuPresentation(Model& model, float canvasWidth, float canvasHeight,
                                   std::vector<oot3d::ui::UiPrimitive>& output, const Palette& palette = {},
                                   const Hints& hints = {});

} // namespace Oot3dNativeGame::Menu
