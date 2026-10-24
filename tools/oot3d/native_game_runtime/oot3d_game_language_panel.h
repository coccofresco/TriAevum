#pragma once
#include "fast/extension/graphics_settings_window.h"
#include "oot3d_game_language.h"
#include <memory>
namespace Oot3dNativeGame {
std::shared_ptr<Fast::Extension::GraphicsSettingsPanelTab>
CreateGameLanguagePanel(std::shared_ptr<GameLanguageSettings> settings);
}
