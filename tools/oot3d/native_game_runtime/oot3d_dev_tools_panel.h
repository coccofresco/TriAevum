#pragma once

#include "fast/oot3d/graphics_settings_window.h"
#include "oot3d_dev_warp.h"

#include <memory>
#include <string>

namespace Oot3dNativeGame {

// F1 "Dev Tools" (developer builds only): Ship of Harkinian's Dev Tools >
// Warping column. The Debug Warp Screen lists (vanilla, or Better with
// entrances, age and time of day) and the Warp Points. The free camera is in
// Photo mode (oot3d_photo_mode_panel.h), in every build.
std::shared_ptr<Fast::Oot3d::GraphicsSettingsPanelTab>
CreateDevToolsPanel(std::shared_ptr<DevWarpRuntime> warp, std::string language);

} // namespace Oot3dNativeGame
