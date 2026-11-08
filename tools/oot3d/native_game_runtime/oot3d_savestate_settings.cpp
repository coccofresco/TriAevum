#include "oot3d_savestate_settings.h"

#include <nlohmann/json.hpp>

#include "fast/extension/graphics_settings_persistence.h"
#include "fast/extension/graphics_settings_runtime.h"
#include "fast/extension/presentation_settings_transaction.h"
#include "oot3d_game_language.h"
#include "oot3d_native_control_config.h"
#include "../ui_topscreen/oot3d_top_screen_config.h"

namespace Oot3dNativeGame {
namespace {

constexpr int kSettingsSchema = 1;

} // namespace

std::string CaptureSavestateSettings(const SavestateSettingsOwners& owners) {
  nlohmann::json document{
      {"schema", kSettingsSchema},
      {"graphics", Fast::Extension::SerializeGraphicsSettings(
                       Fast::Extension::GraphicsSettingsRuntime::Instance().Snapshot())},
  };
  if (!owners.TitleSettingsNotice.empty()) {
    document["title_settings_notice"] = owners.TitleSettingsNotice;
  }
  std::string text;
  if (owners.TopScreen != nullptr &&
      SerializeTopScreenUiConfigText(owners.TopScreen->Snapshot().Config, &text)) {
    document["topscreen"] = nlohmann::json::parse(text, nullptr, false);
  }
  if (owners.Controls != nullptr &&
      SerializeNativeControlConfigText(owners.Controls->Snapshot().Config, &text)) {
    document["controls"] = nlohmann::json::parse(text, nullptr, false);
  }
  if (owners.Language != nullptr) {
    document["language"] = owners.Language->Selected();
  }
  if (!owners.TitleSettingsId.empty() && owners.CaptureTitleSettings) {
    if (auto settings = owners.CaptureTitleSettings();
        settings.has_value() && settings->is_object()) {
      document["title"] = {{"id", owners.TitleSettingsId},
                           {"settings", std::move(*settings)}};
    }
  }
  return document.dump();
}

Fast::Extension::GraphicsSettings MergeSavedGraphicsSettings(
    const Fast::Extension::GraphicsSettings& saved,
    const Fast::Extension::GraphicsSettings& current) {
  auto merged = saved;
  // The presentation owner's own definition (window mode, output size,
  // VSync), plus the monitor and its refresh rate.
  Fast::Extension::SetPresentationSettings(
      merged, Fast::Extension::GetPresentationSettings(current));
  merged.DisplayIndex = current.DisplayIndex;
  merged.RefreshRate = current.RefreshRate;
  return merged;
}

SavestateSettingsApplyResult ApplySavestateSettings(
    const std::string& settingsJson, const SavestateSettingsOwners& owners) {
  SavestateSettingsApplyResult result;
  const auto fail = [&result](std::string line) {
    result.Success = false;
    result.Lines.push_back(std::move(line));
  };
  const auto document = nlohmann::json::parse(settingsJson, nullptr, false);
  // Comparing JSON values avoids throwing type_error for a corrupt string,
  // array or null schema. Reject before any live owner is mutated.
  if (!document.is_object() || !document.contains("schema") ||
      !document["schema"].is_number_integer() || document["schema"] != kSettingsSchema) {
    fail("Settings: unsupported or damaged settings block");
    return result;
  }
  if (!owners.TitleSettingsNotice.empty()) {
    result.Lines.push_back(owners.TitleSettingsNotice);
  }
  if (const auto graphics = document.find("graphics"); graphics != document.end()) {
    auto& runtime = Fast::Extension::GraphicsSettingsRuntime::Instance();
    const auto current = runtime.Snapshot();
    const auto loaded = Fast::Extension::DeserializeGraphicsSettings(*graphics, current);
    const auto validation = runtime.RestorePreferences(
        MergeSavedGraphicsSettings(loaded.Value, current));
    if (validation.Accepted()) {
      result.Lines.push_back(loaded.Issues.empty()
          ? "Graphics: applied (display mode kept)"
          : "Graphics: applied with corrected fields (display mode kept)");
    } else {
      fail("Graphics: rejected (" +
           (validation.Issues.empty() ? std::string("invalid")
                                      : validation.Issues.front().Message) +
           ")");
    }
  }
  if (const auto topscreen = document.find("topscreen"); topscreen != document.end()) {
    if (owners.TopScreen == nullptr) {
      result.Lines.push_back("TopScreen: stored, not active in this UI profile");
    } else {
      TopScreenUiConfig config;
      std::string error;
      if (ParseTopScreenUiConfigText(topscreen->dump(), &config, &error) &&
          owners.TopScreen->Apply(config, &error)) {
        result.Lines.push_back("TopScreen: applied");
      } else {
        fail("TopScreen: " + error);
      }
    }
  }
  if (const auto controls = document.find("controls"); controls != document.end() &&
                                                       owners.Controls != nullptr) {
    NativeControlConfig config;
    std::string error;
    if (ParseNativeControlConfigText(controls->dump(), &config, &error) &&
        owners.Controls->Apply(config, &error)) {
      result.Lines.push_back("Controls: applied");
    } else {
      fail("Controls: " + error);
    }
  }
  if (const auto language = document.find("language");
      language != document.end() && language->is_string() &&
      owners.Language != nullptr) {
    const auto code = language->get<std::string>();
    if (code == owners.Language->Selected()) {
      result.Lines.push_back("Language: unchanged");
    } else if (owners.Language->Select(code)) {
      result.Lines.push_back("Language: " + code + ", applies at the next full start");
    } else {
      fail("Language: " + owners.Language->Error());
    }
  }
  if (const auto title = document.find("title"); title != document.end()) {
    const bool sameTitle = title->is_object() && title->contains("id") &&
                           (*title)["id"].is_string() &&
                           !owners.TitleSettingsId.empty() &&
                           (*title)["id"] == owners.TitleSettingsId;
    if (!sameTitle || !owners.ApplyTitleSettings) {
      result.Lines.push_back(
          "Title settings: stored for another title, current kept");
    } else if (const auto settings = title->find("settings");
               settings == title->end() || !settings->is_object()) {
      fail("Title settings: damaged block, current kept");
    } else {
      std::string error;
      if (owners.ApplyTitleSettings(*settings, &error)) {
        result.Lines.push_back("Title settings: applied");
      } else {
        fail("Title settings: " + error);
      }
    }
  } else if (owners.ApplyTitleSettings) {
    result.Lines.push_back("Title settings: not in this state, current kept");
  }
  return result;
}

} // namespace Oot3dNativeGame
