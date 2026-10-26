#include "triaevum_audio_settings_panel.h"

#include "ship/audio/AudioOutputPolicy.h"

#include <imgui.h>

#include <string>
#include <utility>

namespace Oot3dNativeGame {
namespace {

class AudioSettingsPanel final : public Fast::Extension::GraphicsSettingsPanelTab {
public:
  AudioSettingsPanel(AudioOutputPolicySource source, bool offerFocusMute)
      : mSource(std::move(source)), mOfferFocusMute(offerFocusMute) {}

  [[nodiscard]] const char *Label() const noexcept override { return "Audio"; }
  [[nodiscard]] const char *Group() const noexcept override {
    return Fast::Extension::kGameSettingsGroup;
  }

  void Draw() override {
    const auto policy = mSource ? mSource() : nullptr;
    ImGui::SeparatorText("Audio output");
    if (policy == nullptr) {
      ImGui::TextWrapped("Audio settings are unavailable: the host audio service has not started.");
      return;
    }
    // No draft: the section shows the owner's live settings, so a change
    // from the in-game menu is never overwritten.
    auto settings = policy->Settings();
    ImGui::BeginDisabled(settings.Muted);
    int volume = settings.MasterVolume;
    if (ImGui::SliderInt("Master volume", &volume, Ship::kAudioMasterVolumeMinimum,
                         Ship::kAudioMasterVolumeMaximum, "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
      settings.MasterVolume = volume;
      // Heard while dragged; written once when the slider is let go.
      policy->Preview(settings);
    }
    const bool released = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip(settings.Muted ? "Sound is muted." : "The volume of everything the game plays.");
    if (released) Report(policy->Save(&mError));
    bool muted = settings.Muted;
    if (ImGui::Checkbox("Mute", &muted)) {
      settings.Muted = muted;
      Report(policy->Apply(settings, &mError));
    }
    if (mOfferFocusMute) {
      bool unfocused = settings.MuteWhenUnfocused;
      if (ImGui::Checkbox("Mute when the window loses focus", &unfocused)) {
        settings.MuteWhenUnfocused = unfocused;
        Report(policy->Apply(settings, &mError));
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Silent while another window is active; the game keeps running.");
    }
    ImGui::TextDisabled("Applied to the game's mixed output. Music and sound effects have no separate volumes: "
                        "the game's mixer does not tell them apart.");
    if (!policy->OutputBound()) {
      ImGui::TextDisabled("No audio output device is open; the setting is kept for when one is.");
    }
    switch (policy->SaveState()) {
    case Ship::AudioOutputSaveState::SessionOnly:
      ImGui::TextDisabled("Audio: session only");
      break;
    case Ship::AudioOutputSaveState::Failed:
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.45F, 0.35F, 1.0F));
      ImGui::TextWrapped("Audio applied for this session, but could not be saved: %s", policy->LastError().c_str());
      ImGui::PopStyleColor();
      if (ImGui::Button("Retry saving audio")) Report(policy->Save(&mError));
      break;
    case Ship::AudioOutputSaveState::Saved:
      if (mChanged) ImGui::TextDisabled("Audio: saved");
      break;
    }
  }

private:
  void Report(bool) { mChanged = true; }

  AudioOutputPolicySource mSource;
  bool mOfferFocusMute = true;
  bool mChanged = false;
  std::string mError;
};

} // namespace

std::shared_ptr<Fast::Extension::GraphicsSettingsPanelTab>
CreateAudioSettingsPanel(AudioOutputPolicySource source, bool offerFocusMute) {
  return std::make_shared<AudioSettingsPanel>(std::move(source), offerFocusMute);
}

} // namespace Oot3dNativeGame
