#include "oot3d_photo_mode_panel.h"

#include <imgui.h>

#include <utility>

namespace Oot3dNativeGame {
namespace {

class PhotoModePanel final : public Fast::Oot3d::GraphicsSettingsPanelTab {
public:
  PhotoModePanel(std::shared_ptr<DevFreeCamera> freeCamera, std::shared_ptr<PhotoModeState> state)
      : mFreeCamera(std::move(freeCamera)), mState(std::move(state)) {}

  [[nodiscard]] const char *Label() const noexcept override { return "Photo mode"; }

  void Draw() override {
    bool active = mFreeCamera->Active();
    if (ImGui::Checkbox("Free camera (F6)", &active)) mFreeCamera->SetActive(active);
    Tooltip("A camera detached from Link: the game draws from it and Link stays still. "
            "WASD move, Q/E down/up, hold the left or right mouse button and drag to look, "
            "Shift faster, Ctrl slower; pad: left stick moves, right stick looks, "
            "L/R shoulders down/up.");
    ImGui::SameLine();
    bool hideHud = mState->HideHud.load(std::memory_order_relaxed);
    if (ImGui::Checkbox("Hide HUD", &hideHud)) mState->HideHud.store(hideHud, std::memory_order_relaxed);
    Tooltip("Hearts, magic, rupees, buttons, the action icon and the minimap, for this session only. "
            "Dialogue and menus stay.");
    float speed = mFreeCamera->Speed();
    if (ImGui::SliderFloat("Free camera speed", &speed, 10.0F, 10000.0F, "%.0f units/s",
                           ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp))
      mFreeCamera->SetSpeed(speed);
    ImGui::BeginDisabled(!active);
    if (ImGui::Button("Back to the game's view")) mFreeCamera->ResetToGame();
    ImGui::EndDisabled();
  }

private:
  static void Tooltip(const char *text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", text);
  }

  std::shared_ptr<DevFreeCamera> mFreeCamera;
  std::shared_ptr<PhotoModeState> mState;
};

} // namespace

std::shared_ptr<Fast::Oot3d::GraphicsSettingsPanelTab>
CreatePhotoModePanel(std::shared_ptr<DevFreeCamera> freeCamera, std::shared_ptr<PhotoModeState> state) {
  return std::make_shared<PhotoModePanel>(std::move(freeCamera), std::move(state));
}

} // namespace Oot3dNativeGame
