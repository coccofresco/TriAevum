#include "oot3d_dev_tools_panel.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>

namespace Oot3dNativeGame {
namespace {

class DevToolsPanel final : public Fast::Extension::GraphicsSettingsPanelTab {
 public:
  DevToolsPanel(std::shared_ptr<DevWarpRuntime> warp, std::string language)
      : mWarp(std::move(warp)), mLanguage(std::move(language)) {}

  const char *Label() const noexcept override { return "Dev Tools"; }

  void Draw() override {
    auto options = mWarp->Options();
    const auto status = mWarp->Status();
    ImGui::SeparatorText("Warping");
    // A title with curated zones and arrival options (MM3D): zones first.
    auto arrival = mWarp->ArrivalOptions();
    if (!mWarp->Zones().empty()) DrawZones(arrival, status);
    if (!mWarp->Axes().empty()) DrawArrival(arrival);
    if (!mWarp->Zones().empty() || !mWarp->Axes().empty()) mWarp->SetArrivalOptions(arrival);
    ImGui::Checkbox("Better Debug Warp Screen", &options.Better);
    Tooltip("Optimized Debug Warp Screen, with the added ability to chose entrances and time of day.");
    if (!mWarp->HasTitleAbi()) {
      ImGui::Checkbox("Debug Warp Screen Translation", &options.Translation);
      Tooltip("Translate the Debug Warp Screen based on the game language.");
    }
    DrawWarpScreen(options, status, arrival.Arrival);
    mWarp->SetOptions(options);
    DrawWarpPoints(status);
    if (!mWarp->StorageError().empty())
      ImGui::TextColored(ImVec4(1.0F, 0.45F, 0.35F, 1.0F), "%s", mWarp->StorageError().c_str());
  }

 private:
  static void Tooltip(const char *text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", text);
  }

  // The label of an axis value: Keep, the zone preset, a choice or the raw value.
  std::string ValueLabel(size_t axis, int32_t value) const {
    if (value == kDevWarpKeep) return "keep";
    if (value == kDevWarpPreset) return "zone preset";
    const auto axes = mWarp->Axes();
    if (axis < axes.size())
      for (const auto &choice : axes[axis].Choices)
        if (choice.Value == value) return choice.Label;
    char text[16];
    std::snprintf(text, sizeof(text), "0x%04X", static_cast<unsigned>(value));
    return text;
  }

  std::string PresetText(const DevWarpZone &zone) const {
    std::string text;
    const auto axes = mWarp->Axes();
    for (size_t axis = 0; axis < axes.size() && axis < kDevWarpMaxAxes; ++axis) {
      if (zone.Preset[axis] == kDevWarpKeep) continue;
      if (!text.empty()) text += ", ";
      text += ValueLabel(axis, zone.Preset[axis]);
    }
    return text.empty() ? "keeps day, time, form" : text;
  }

  void DrawZones(DevWarpArrivalOptions &arrival, const DevWarpStatus &status) {
    ImGui::SeparatorText("Zones");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##ZoneFilter", "Filter zones...", mFilter.data(), mFilter.size());
    std::string filter(mFilter.data());
    std::transform(filter.begin(), filter.end(), filter.begin(), [](unsigned char c) { return std::tolower(c); });
    const auto zones = mWarp->Zones();
    const DevWarpZone *selected = mWarp->FindZone(arrival.SelectedZone);
    if (ImGui::BeginChild("##Zones", ImVec2(-FLT_MIN, ImGui::GetTextLineHeightWithSpacing() * 12.0F),
                          ImGuiChildFlags_Borders)) {
      const char *category = nullptr;
      bool open = false;
      for (const auto &zone : zones) {
        std::string name(zone.Name);
        std::string lower = name + " " + zone.Category;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
        if (!filter.empty() && lower.find(filter) == std::string::npos) continue;
        if (category == nullptr || std::strcmp(category, zone.Category) != 0) {
          category = zone.Category;
          ImGui::SetNextItemOpen(true, ImGuiCond_Once);
          open = ImGui::CollapsingHeader(category) || !filter.empty();
        }
        if (!open) continue;
        const auto *row = mWarp->FindEntrance(zone.Entrance);
        const auto availability = row != nullptr ? mWarp->Check(*row) : DevWarpAvailability{true, false, "no catalog row"};
        ImGui::BeginDisabled(availability.Known && !availability.Available);
        if (arrival.BootZone == zone.Id) name += "  [Boot]";
        if (!zone.Proven) name += "  (unverified)";
        ImGui::PushID(zone.Id);
        if (ImGui::Selectable(name.c_str(), selected == &zone)) {
          arrival.SelectedZone = zone.Id;
          selected = &zone;
        }
        ImGui::PopID();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
          char entrance[16];
          std::snprintf(entrance, sizeof(entrance), "0x%04X", zone.Entrance);
          std::string tip = std::string("Entrance ") + entrance + ": " + PresetText(zone);
          if (*zone.Note) tip += "\n" + std::string(zone.Note);
          if (!zone.Proven) tip += "\nArrival not yet verified in game.";
          if (!availability.Reason.empty()) tip += "\n" + availability.Reason;
          ImGui::SetTooltip("%s", tip.c_str());
        }
      }
    }
    ImGui::EndChild();
    if (selected == nullptr) {
      ImGui::TextDisabled("Select a zone.");
      return;
    }
    const auto *row = mWarp->FindEntrance(selected->Entrance);
    const auto availability = row != nullptr ? mWarp->Check(*row) : DevWarpAvailability{true, false, "no catalog row"};
    const auto resolved = DevWarpRuntime::Resolve(arrival.Arrival, selected);
    const auto arrivalError = mWarp->ArrivalError(resolved);
    const bool gameplay = status.Observation.Gameplay;
    ImGui::BeginDisabled(!gameplay || (availability.Known && !availability.Available) || !arrivalError.empty());
    if (ImGui::Button("Warp##Zone")) {
      std::string error;
      mWarp->WarpToZone(selected->Id, arrival.Arrival, &error);
    }
    ImGui::EndDisabled();
    if (!gameplay) Tooltip("Warps start from normal gameplay: load a file first.");
    else if (!availability.Reason.empty()) Tooltip(availability.Reason.c_str());
    else if (!arrivalError.empty()) Tooltip(arrivalError.c_str());
    ImGui::SameLine();
    const bool boot = arrival.BootZone == selected->Id;
    if (ImGui::Button(boot ? "Clear launch zone" : "Start here on launch")) arrival.BootZone = boot ? "" : selected->Id;
    Tooltip("Warp to this zone, with the arrival options below, when gameplay first starts in a session "
            "(after loading a file or a savestate). Replaces a boot warp point.");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", PresetText(*selected).c_str());
  }

  void DrawArrival(DevWarpArrivalOptions &arrival) {
    ImGui::SeparatorText("Arrival");
    const auto axes = mWarp->Axes();
    for (size_t axis = 0; axis < axes.size() && axis < kDevWarpMaxAxes; ++axis) {
      const auto &definition = axes[axis];
      auto &value = arrival.Arrival[axis];
      ImGui::PushID(definition.Id);
      ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0F);
      if (ImGui::BeginCombo(definition.Label, ValueLabel(axis, value).c_str())) {
        if (ImGui::Selectable("keep", value == kDevWarpKeep)) value = kDevWarpKeep;
        Tooltip("Leave the current value as it is.");
        if (ImGui::Selectable("zone preset", value == kDevWarpPreset)) value = kDevWarpPreset;
        Tooltip("The selected zone's suggestion (keep on the Debug Warp Screen).");
        ImGui::BeginDisabled(!definition.Proven);
        for (const auto &choice : definition.Choices) {
          if (ImGui::Selectable(choice.Label, value == choice.Value)) value = choice.Value;
        }
        ImGui::EndDisabled();
        ImGui::EndCombo();
      }
      if (!definition.Proven) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", definition.Pending);
      }
      ImGui::PopID();
    }
  }

  template <class T> const char *Name(const T &entry, bool translate) const {
    if (translate && mLanguage == "de") return entry.German;
    if (translate && mLanguage == "fr") return entry.French;
    return entry.English;
  }

  void DrawWarpScreen(DevWarpScreenOptions &options, const DevWarpStatus &status, const DevWarpSelection &arrival) {
    ImGui::SeparatorText("Debug Warp Screen");
    const bool translate = options.Translation;
    const DevWarpEntrance *target = nullptr;
    if (options.Better) {
      const auto scenes = mWarp->Scenes();
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
      const auto entrances = mWarp->Entrances().subspan(scene.First, scene.Count);
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
      if (!mWarp->HasTitleAbi()) {
      int age = options.Age == DevWarpAge::Adult ? 0 : 1;
      ImGui::RadioButton("Child", &age, 1);
      ImGui::SameLine();
      ImGui::RadioButton("Adult", &age, 0);
      options.Age = age == 0 ? DevWarpAge::Adult : DevWarpAge::Child;
      }
      if (!mWarp->HasTitleAbi()) {
      int night = options.Night ? 1 : 0;
      ImGui::RadioButton("Day", &night, 0);
      ImGui::SameLine();
      ImGui::RadioButton("Night", &night, 1);
      options.Night = night != 0;
      } else if (mWarp->Axes().empty()) ImGui::TextDisabled("Current day, time and form are preserved.");
      else ImGui::TextDisabled("Arrival options apply; a zone preset keeps the current value here.");
    } else {
      const auto scenes = mWarp->VanillaEntrances();
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
      if (!mWarp->HasTitleAbi()) {
      int age = options.Age == DevWarpAge::Adult ? 0 : 1;
      ImGui::RadioButton("Child", &age, 1);
      ImGui::SameLine();
      ImGui::RadioButton("Adult", &age, 0);
      options.Age = age == 0 ? DevWarpAge::Adult : DevWarpAge::Child;
      }
    }
    const auto availability = mWarp->Check(*target);
    const bool gameplay = status.Observation.Gameplay;
    const auto arrivalError = mWarp->ArrivalError(DevWarpRuntime::Resolve(arrival, nullptr));
    const bool titleArrival = mWarp->HasTitleAbi() && !mWarp->Axes().empty();
    ImGui::BeginDisabled(!gameplay || (availability.Known && !availability.Available) ||
                         (titleArrival && !arrivalError.empty()));
    if (ImGui::Button("Warp")) {
      if (titleArrival) mWarp->WarpTo(*target, arrival);
      // The vanilla screen keeps the current time of day, as SoH's does.
      else mWarp->WarpTo(*target, options.Age, options.Better && !mWarp->HasTitleAbi() ? std::optional<bool>(options.Night) : std::nullopt);
    }
    ImGui::EndDisabled();
    if (!gameplay) Tooltip("Warps start from normal gameplay: load a file first.");
    else if (!availability.Reason.empty()) Tooltip(availability.Reason.c_str());
    else if (titleArrival && !arrivalError.empty()) Tooltip(arrivalError.c_str());
    if (!status.Message.empty()) {
      const bool failed = status.Phase == DevWarpPhase::Failed;
      ImGui::PushStyleColor(ImGuiCol_Text, failed ? ImVec4(1.0F, 0.45F, 0.35F, 1.0F)
                                                  : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      ImGui::TextWrapped("%s", status.Message.c_str());
      ImGui::PopStyleColor();
    }
    if (mWarp->HasTitleAbi() && status.Observation.PlayState) {
      const auto state = mWarp->Describe(status.Observation);
      ImGui::TextDisabled("Scene %d, room %u, entrance 0x%04X%s%s", status.Observation.Scene,
                          status.Observation.Room, status.Observation.Entrance, state.empty() ? "" : ", ",
                          state.c_str());
    }
    else if (status.Observation.PlayState)
      ImGui::TextDisabled("Scene %d, room %u, entrance 0x%04X, %s", status.Observation.Scene, status.Observation.Room,
                          status.Observation.Entrance, status.Observation.Age == 0 ? "adult" : "child");
  }

  void DrawWarpPoints(const DevWarpStatus &status) {
    ImGui::SeparatorText("Warp Points");
    if (!mWarp->PointsAvailable()) { ImGui::TextDisabled("Warp points are awaiting gameplay validation."); return; }
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
  std::string mLanguage;
  std::array<char, 64> mName{};
  std::array<char, 64> mFilter{};
};

} // namespace

std::shared_ptr<Fast::Extension::GraphicsSettingsPanelTab> CreateDevToolsPanel(std::shared_ptr<DevWarpRuntime> warp,
                                                                           std::string language) {
  return std::make_shared<DevToolsPanel>(std::move(warp), std::move(language));
}

} // namespace Oot3dNativeGame
