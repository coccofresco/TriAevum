#pragma once

#include "fast/oot3d/graphics_settings_window.h"
#include "oot3d_native_control_config.h"
#include "oot3d_top_screen_config.h"

#include <memory>

namespace Oot3dNativeGame {

std::shared_ptr<Fast::Oot3d::GraphicsSettingsPanelTab>
CreateNativeControlsSettingsPanel(
    std::shared_ptr<NativeControlConfigRuntime> controls,
    std::shared_ptr<TopScreenUiConfigRuntime> topScreen);

} // namespace Oot3dNativeGame
