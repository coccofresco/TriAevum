#pragma once
#include "fast/extension/graphics_settings_window.h"
#include "oot3d_savestate_library.h"
#include <memory>
namespace Oot3dNativeGame {
// F1 "Save states": the F5/Shift+F5 and F8/Shift+F8 actions as buttons, the
// save folder (default beside the executable) and the state F8 loads.
std::shared_ptr<Fast::Extension::GraphicsSettingsPanelTab>
CreateSavestatePanel(std::shared_ptr<SavestateLibrary> library);
} // namespace Oot3dNativeGame
