#pragma once

#include "fast/oot3d/graphics_settings_window.h"
#include "oot3d_top_screen_config.h"

#include <memory>

namespace Oot3dNativeGame {

std::shared_ptr<Fast::Oot3d::GraphicsSettingsPanelTab>
CreateTopScreenSettingsPanel(
    std::shared_ptr<TopScreenUiConfigRuntime> runtime);

} // namespace Oot3dNativeGame
