#pragma once

// The host settings a "state + settings" save carries and a "load + settings"
// applies: graphics, TopScreen, controls and game language, each through its
// own owner. The display mode (window mode, monitor, output size, refresh
// rate) belongs to the machine and its confirmation flow, so applying keeps
// the current one. Scene-environment profiles are authored files with their
// own Save, not settings, and stay out.

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "fast/extension/graphics_settings.h"

namespace Oot3dNativeGame {

class TopScreenUiConfigRuntime;
class NativeControlConfigRuntime;
class GameLanguageSettings;

struct SavestateSettingsOwners {
  // Null owners are skipped (for example TopScreen outside its profile).
  std::shared_ptr<TopScreenUiConfigRuntime> TopScreen;
  std::shared_ptr<NativeControlConfigRuntime> Controls;
  std::shared_ptr<GameLanguageSettings> Language;
  // Adapter-authored disclosure, never supplied by an untrusted state file.
  std::string TitleSettingsNotice;
  // The title's own settings owner (MM3D: dekui's presentation settings),
  // stored under "title" with TitleSettingsId; empty functions skip it.
  std::string TitleSettingsId;
  std::function<std::optional<nlohmann::json>()> CaptureTitleSettings;
  std::function<bool(const nlohmann::json &, std::string *)> ApplyTitleSettings;
};

// JSON object text.
[[nodiscard]] std::string CaptureSavestateSettings(
    const SavestateSettingsOwners& owners);

struct SavestateSettingsApplyResult {
  bool Success = true;
  // One line per owner: what was applied or why not.
  std::vector<std::string> Lines;
};

[[nodiscard]] SavestateSettingsApplyResult ApplySavestateSettings(
    const std::string& settingsJson, const SavestateSettingsOwners& owners);

// The graphics candidate a saved block produces: the saved values over the
// current ones, keeping the current display mode.
[[nodiscard]] Fast::Extension::GraphicsSettings MergeSavedGraphicsSettings(
    const Fast::Extension::GraphicsSettings& saved,
    const Fast::Extension::GraphicsSettings& current);

} // namespace Oot3dNativeGame
