#include "oot3d_game_language_panel.h"
#include <imgui.h>
namespace Oot3dNativeGame {
namespace {
class LanguagePanel final : public Fast::Oot3d::GraphicsSettingsPanelTab {
public:
  explicit LanguagePanel(std::shared_ptr<GameLanguageSettings> settings)
      : mSettings(std::move(settings)) {}
  const char *Label() const noexcept override { return "Game"; }
  void Draw() override {
    const char *label = mSettings->Selected().c_str();
    for (const auto &lang : mSettings->Available())
      if (lang.Code == mSettings->Selected())
        label = lang.Label.c_str();
    if (ImGui::BeginCombo("Game language", label)) {
      for (const auto &lang : mSettings->Available()) {
        const bool selected = lang.Code == mSettings->Selected();
        if (ImGui::Selectable(lang.Label.c_str(), selected))
          mSettings->Select(lang.Code);
        if (selected)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
    ImGui::TextWrapped("Language changes apply on the next full game start, "
                       "not when loading a save state.");
    if (mSettings->Selected() != mSettings->BootLanguage())
      ImGui::TextUnformatted("Restart required.");
    if (!mSettings->Error().empty())
      ImGui::TextWrapped("Could not save language: %s",
                         mSettings->Error().c_str());
  }

private:
  std::shared_ptr<GameLanguageSettings> mSettings;
};
} // namespace
std::shared_ptr<Fast::Oot3d::GraphicsSettingsPanelTab>
CreateGameLanguagePanel(std::shared_ptr<GameLanguageSettings> settings) {
  return std::make_shared<LanguagePanel>(std::move(settings));
}
} // namespace Oot3dNativeGame
