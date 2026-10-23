#pragma once

#include "fast/oot3d/graphics_settings_window.h"
#include "oot3d_dev_free_camera.h"

#include <atomic>
#include <memory>

namespace Oot3dNativeGame {

// Session state of F1 > Photo mode, never saved: every launch starts with
// the HUD shown. Hiding it acts as TopScreen render_hud off (the TopScreen
// compositor's HUD) and leaves the game's own gameplay HUD draws (the action
// icon, the minimap) out of presentations.
struct PhotoModeState {
  std::atomic<bool> HideHud{false};
};

// F1 "Photo mode": the free camera (F6) and Hide HUD.
std::shared_ptr<Fast::Oot3d::GraphicsSettingsPanelTab>
CreatePhotoModePanel(std::shared_ptr<DevFreeCamera> freeCamera,
                     std::shared_ptr<PhotoModeState> state);

} // namespace Oot3dNativeGame
