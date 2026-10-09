#include "oot3d_dev_tools_panel.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <span>

namespace Oot3dNativeGame {
namespace {

class DevToolsPanel final : public Fast::Oot3d::GraphicsSettingsPanelTab {
 public:
  DevToolsPanel(std::shared_ptr<DevWarpRuntime> warp, std::shared_ptr<DevFreeCamera> freeCamera,
                std::string language)
      : mWarp(std::move(warp)), mFreeCamera(std::move(freeCamera)), mLanguage(std::move(language)) {}

  const char *Label() const noexcept override { return "Dev Tools"; }

  void Draw() override {
    DrawFreeCamera();
    auto options = mWarp->Options();
    const auto status = mWarp->Status();
    ImGui::SeparatorText("Warping");
    ImGui::Checkbox("Better Debug Warp Screen", &options.Better);
    Tooltip("Optimized Debug Warp Screen, with the added ability to chose entrances and time of day.");
    ImGui::Checkbox("Debug Warp Screen Translation", &options.Translation);
    Tooltip("Translate the Debug Warp Screen based on the game language.");
    DrawWarpScreen(options, status);
    mWarp->SetOptions(options);
    DrawWarpPoints(status);
    if (!mWarp->StorageError().empty())
      ImGui::TextColored(ImVec4(1.0F, 0.45F, 0.35F, 1.0F), "%s", mWarp->StorageError().c_str());
  }

 private:
  void DrawFreeCamera() {
    ImGui::SeparatorText("Free camera");
    bool active = mFreeCamera->Active();
    if (ImGui::Checkbox("Free camera (F6)", &active)) mFreeCamera->SetActive(active);
    Tooltip("A camera detached from Link: the game draws from it and Link stays still. "
            "WASD move, Q/E down/up, right mouse button held to look, Shift faster, Ctrl slower; "
            "pad: left stick moves, right stick looks, L/R shoulders down/up.");
    float speed = mFreeCamera->Speed();
    if (ImGui::SliderFloat("Free camera speed", &speed, 10.0F, 10000.0F, "%.0f units/s",
                           ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp))
      mFreeCamera->SetSpeed(speed);
    ImGui::BeginDisabled(!active);
    if (ImGui::Button("Back to the game's view")) mFreeCamera->ResetToGame();
    ImGui::EndDisabled();
    if (const auto view = mFreeCamera->LastView())
      ImGui::TextDisabled("Eye %.1f, %.1f, %.1f", view->Eye[0], view->Eye[1], view->Eye[2]);
  }

  static void Tooltip(const char *text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", text);
  }

  template <class T> const char *Name(const T &entry, bool translate) const {
    if (translate && mLanguage == "de") return entry.German;
    if (translate && mLanguage == "fr") return entry.French;
    return entry.English;
  }

  void DrawWarpScreen(DevWarpScreenOptions &options, const DevWarpStatus &status) {
    ImGui::SeparatorText("Debug Warp Screen");
    const bool translate = options.Translation;
    const DevWarpEntrance *target = nullptr;
    if (options.Better) {
      const auto scenes = DevWarpBetterScenes();
      options.BetterScene = std::clamp(options.BetterScene, 0, static_cast<int>(scenes.size()) - 1);
      const auto &scene = scenes[static_cast<size_t>(options.BetterScene)];
      if (ImGui::BeginListBox("##BetterScenes", ImVec2(-FLT_MIN, ImGui::GetTextLineHeightWithSpacing() * 12.0F))) {
        for (int index = 0; index < static_cast<int>(scenes.size()); ++index) {
          if (ImGui::Selectable(Name(scenes[static_cast<size_t>(index)], translate),
                                index == options.BetterScene)) {
            options.BetterScene = index;
            options.BetterEntrance = 0;
          }
          if (index == options.BetterScene && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY();
        }
        ImGui::EndListBox();
      }
      const auto entrances = DevWarpBetterEntrances().subspan(scene.First, scene.Count);
      options.BetterEntrance = std::clamp(options.BetterEntrance, 0, static_cast<int>(entrances.size()) - 1);
      target = &entrances[static_cast<size_t>(options.BetterEntrance)];
      if (ImGui::BeginCombo("Entrance", Name(*target, translate))) {
        for (int index = 0; index < static_cast<int>(entrances.size()); ++index) {
          const auto &entrance = entrances[static_cast<size_t>(index)];
          const auto availability = mWarp->Check(entrance);
          ImGui::BeginDisabled(availability.Known && !availability.Available);
          ImGui::PushID(index);
          if (ImGui::Selectable(Name(entrance, translate), index == options.BetterEntrance))
            options.BetterEntrance = index;
          ImGui::PopID();
          ImGui::EndDisabled();
          if (!availability.Reason.empty()) Tooltip(availability.Reason.c_str());
        }
        ImGui::EndCombo();
      }
      int age = options.Age == DevWarpAge::Adult ? 0 : 1;
      ImGui::RadioButton("Child", &age, 1);
      ImGui::SameLine();
      ImGui::RadioButton("Adult", &age, 0);
      options.Age = age == 0 ? DevWarpAge::Adult : DevWarpAge::Child;
      int night = options.Night ? 1 : 0;
      ImGui::RadioButton("Day", &night, 0);
      ImGui::SameLine();
      ImGui::RadioButton("Night", &night, 1);
      options.Night = night != 0;
    } else {
      const auto scenes = DevWarpVanillaEntrances();
      options.VanillaScene = std::clamp(options.VanillaScene, 0, static_cast<int>(scenes.size()) - 1);
      if (ImGui::BeginListBox("##VanillaScenes", ImVec2(-FLT_MIN, ImGui::GetTextLineHeightWithSpacing() * 12.0F))) {
        for (int index = 0; index < static_cast<int>(scenes.size()); ++index) {
          const auto &entrance = scenes[static_cast<size_t>(index)];
          const auto availability = mWarp->Check(entrance);
          ImGui::BeginDisabled(availability.Known && !availability.Available);
          if (ImGui::Selectable(Name(entrance, translate), index == options.VanillaScene))
            options.VanillaScene = index;
          ImGui::EndDisabled();
          if (!availability.Reason.empty()) Tooltip(availability.Reason.c_str());
        }
        ImGui::EndListBox();
      }
      target = &scenes[static_cast<size_t>(options.VanillaScene)];
      int age = options.Age == DevWarpAge::Adult ? 0 : 1;
      ImGui::RadioButton("Child", &age, 1);
      ImGui::SameLine();
      ImGui::RadioButton("Adult", &age, 0);
      options.Age = age == 0 ? DevWarpAge::Adult : DevWarpAge::Child;
    }
    const auto availability = mWarp->Check(*target);
    const bool gameplay = status.Observation.Gameplay;
    ImGui::BeginDisabled(!gameplay || (availability.Known && !availability.Available));
    if (ImGui::Button("Warp")) {
      // The vanilla screen keeps the current time of day, as SoH's does.
      mWarp->WarpTo(*target, options.Age, options.Better ? std::optional<bool>(options.Night) : std::nullopt);
    }
    ImGui::EndDisabled();
    if (!gameplay) Tooltip("Warps start from normal gameplay: load a file first.");
    else if (!availability.Reason.empty()) Tooltip(availability.Reason.c_str());
    if (!status.Message.empty()) {
      const bool failed = status.Phase == DevWarpPhase::Failed;
      ImGui::PushStyleColor(ImGuiCol_Text, failed ? ImVec4(1.0F, 0.45F, 0.35F, 1.0F)
                                                  : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      ImGui::TextWrapped("%s", status.Message.c_str());
      ImGui::PopStyleColor();
    }
    if (status.Observation.PlayState)
      ImGui::TextDisabled("Scene %d, room %u, entrance 0x%04X, %s", status.Observation.Scene, status.Observation.Room,
                          status.Observation.Entrance, status.Observation.Age == 0 ? "adult" : "child");
  }

  void DrawWarpPoints(const DevWarpStatus &status) {
    ImGui::SeparatorText("Warp Points");
    if (status.Observation.Gameplay && status.Observation.Player) {
      ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 50.0F);
      ImGui::InputTextWithHint("##WarpPointNameInput", "Enter warp point name...", mName.data(), mName.size());
      ImGui::SameLine();
      const bool empty = mName[0] == '\0';
      ImGui::BeginDisabled(empty);
      if (ImGui::Button("+")) {
        std::string error;
        if (mWarp->AddPoint(mName.data(), &error)) mName.fill('\0');
      }
      ImGui::EndDisabled();
      Tooltip("Save the current position as a warp point.");
    }
    for (const auto &[name, point] : mWarp->Points()) {
      ImGui::PushID(name.c_str());
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(name.c_str());
      if (point.BootToPoint) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.85F, 0.55F, 0.0F, 1.0F), "[Boot]");
      }
      ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionAvail().x - 150.0F));
      ImGui::BeginDisabled(!status.Observation.Gameplay);
      if (ImGui::SmallButton("Warp")) mWarp->WarpToPoint(name);
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::SmallButton("Boot")) mWarp->SetBootPoint(name);
      Tooltip("Warp to this point when gameplay first starts in a session.");
      ImGui::SameLine();
      if (ImGui::SmallButton("Delete")) mWarp->RemovePoint(name);
      ImGui::PopID();
    }
  }

  std::shared_ptr<DevWarpRuntime> mWarp;
  std::shared_ptr<DevFreeCamera> mFreeCamera;
  std::string mLanguage;
  std::array<char, 64> mName{};
};

} // namespace

std::shared_ptr<Fast::Oot3d::GraphicsSettingsPanelTab> CreateDevToolsPanel(std::shared_ptr<DevWarpRuntime> warp,
                                                                           std::shared_ptr<DevFreeCamera> freeCamera,
                                                                           std::string language) {
  return std::make_shared<DevToolsPanel>(std::move(warp), std::move(freeCamera), std::move(language));
}

} // namespace Oot3dNativeGame
