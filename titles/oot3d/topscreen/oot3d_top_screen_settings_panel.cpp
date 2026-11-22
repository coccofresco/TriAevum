#include "oot3d_top_screen_settings_panel.h"

#include <imgui.h>

#include <string>
#include <utility>

namespace Oot3dNativeGame {
namespace {

// The TopScreen section of the Display tab. The standard profile fixes the
// HUD layout; the user chooses its size and its button labels, and the
// dialogue's size and font. A choice applies and saves at once.
class TopScreenSettingsPanel final
    : public Fast::Extension::GraphicsSettingsPanelTab {
public:
  explicit TopScreenSettingsPanel(
      std::shared_ptr<TopScreenUiConfigRuntime> runtime)
      : mRuntime(std::move(runtime)) {}

  [[nodiscard]] const char *Label() const noexcept override {
    return "TopScreen";
  }
  [[nodiscard]] const char *Group() const noexcept override {
    return Fast::Extension::kDisplaySettingsGroup;
  }

  void Draw() override {
    // No draft: the section always shows the live configuration, so a change
    // from Controls or the file is never overwritten.
    auto config = mRuntime->Snapshot().Config;
    ImGui::SeparatorText("TopScreen HUD");
    const auto size = [&](const char *label, float scale) {
      if (ImGui::RadioButton(label, config.HudScale == scale) &&
          config.HudScale != scale) {
        config.HudScale = scale;
        Store(config);
      }
    };
    ImGui::TextUnformatted("HUD size");
    size("Native (100%)", kTopScreenHudScaleNative);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("The game's own HUD size.");
    ImGui::SameLine();
    size("Optimized (70%)", kTopScreenHudScaleOptimized);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("The HUD sized for the single wide screen.");
    if (config.HudScale != kTopScreenHudScaleNative &&
        config.HudScale != kTopScreenHudScaleOptimized) {
      ImGui::TextDisabled("Configuration file size: %.2fx", config.HudScale);
    }
    // The letters of the 3DS buttons on the HUD, on the ocarina's notes and
    // in the pause pages' hints: the glyphs of the inputs bound to them
    // (Controls > Bindings, in Controls > Devices > Button prompts' family)
    // or the 3DS letters.
    const auto labels = [&](const char *label, TopScreenHudButtonLabels value) {
      if (ImGui::RadioButton(label, config.HudButtonLabels == value) &&
          config.HudButtonLabels != value) {
        config.HudButtonLabels = value;
        Store(config);
      }
    };
    ImGui::TextUnformatted("HUD button labels");
    labels("Controller##hud_button_labels", TopScreenHudButtonLabels::Controller);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("The buttons and keys you bound to the 3DS buttons, drawn for the\n"
                        "device in use (Controls > Devices > Button prompts): on the HUD,\n"
                        "on the ocarina's notes and in the pause pages' hints (L, R, SELECT).\n"
                        "With touch controls the letters stay.");
    ImGui::SameLine();
    labels("Original 3DS##hud_button_labels", TopScreenHudButtonLabels::Original);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("The 3DS letters: A, B, X, Y, L, R, ZL, ZR and SELECT.");
    ImGui::SeparatorText("Dialogue");
    // The message window drawn smaller around its place on the screen; the
    // native layout and wrapping are unchanged.
    int dialogueSize = config.DialogueScalePercent;
    ImGui::SetNextItemWidth(200.0F);
    if (ImGui::SliderInt("Dialogue size", &dialogueSize, kTopScreenDialogueScaleMinimum,
                         kTopScreenDialogueScaleMaximum, "%d%%")) {
      // Commit whole 5 percent steps only (the configuration rejects others).
      const int step = kTopScreenDialogueScaleStep;
      dialogueSize = (dialogueSize + step / 2) / step * step;
      if (dialogueSize != config.DialogueScalePercent) {
        config.DialogueScalePercent = static_cast<std::uint8_t>(dialogueSize);
        Store(config);
      }
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Shrinks the dialogue boxes (the box, its text, icons and the\n"
                        "ocarina staff) around their place on the screen.\n"
                        "100%% is the game's own size.");
    const auto font = [&](const char *label, TopScreenDialogueFont value) {
      if (ImGui::RadioButton(label, config.DialogueFont == value) && config.DialogueFont != value) {
        config.DialogueFont = value;
        Store(config);
      }
    };
    ImGui::TextUnformatted("Dialogue font");
    font("Automatic##dialogue_font", TopScreenDialogueFont::Automatic);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("The SingleScreen HD font from 480 window lines, the 3DS font below them.");
    ImGui::SameLine();
    font("Original 3DS##dialogue_font", TopScreenDialogueFont::Original);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Always the game's own 3DS font.");
    ImGui::SameLine();
    font("N64 style##dialogue_font", TopScreenDialogueFont::N64);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("The N64-style font you gave Forge: put its ltn16.qbf in\n"
                        "mods/n64_font/ in the TriAevum data folder and run Forge's\n"
                        "Prepare and install. Without it, Automatic. Latin text only.");
    if (!mRuntime->Persistent()) {
      ImGui::TextDisabled("TopScreen: session only");
    } else if (!mStatus.empty()) {
      ImGui::TextWrapped("%s", mStatus.c_str());
    }
    ImGui::SeparatorText("Credits");
    ImGui::TextWrapped(
        "HUD, layout and gameplay options from SingleScreen (TopScreen) by "
        "M-1 (gamebanana.com/mods/695893). Thanks to the author.");
  }

private:
  void Store(const TopScreenUiConfig &config) {
    if (!mRuntime->Persistent()) {
      mRuntime->Preview(config);
      return;
    }
    std::string error;
    if (mRuntime->Apply(config, &error)) {
      mStatus = "TopScreen: saved";
      return;
    }
    mRuntime->Preview(config);
    mStatus = "TopScreen HUD applied for this session, but could not be saved: " +
              error;
  }

  std::shared_ptr<TopScreenUiConfigRuntime> mRuntime;
  std::string mStatus;
};

} // namespace

std::shared_ptr<Fast::Extension::GraphicsSettingsPanelTab>
CreateTopScreenSettingsPanel(
    std::shared_ptr<TopScreenUiConfigRuntime> runtime) {
  return std::make_shared<TopScreenSettingsPanel>(std::move(runtime));
}

} // namespace Oot3dNativeGame
