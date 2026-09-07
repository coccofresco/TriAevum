#include "oot3d_top_screen_config.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <mutex>
#include <set>
#include <string>
#include <system_error>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace Oot3dNativeGame {
namespace {

void SetError(std::string *error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
}

bool HasOnlyFields(const nlohmann::json &document,
                   const std::set<std::string_view> &allowed,
                   std::string *error) {
  for (const auto &[key, value] : document.items()) {
    (void)value;
    if (!allowed.contains(key)) {
      SetError(error, "unknown TopScreen UI config field: " + key);
      return false;
    }
  }
  return true;
}

bool ReadBoolean(const nlohmann::json &document, std::string_view key,
                 bool *value, std::string *error) {
  const std::string name(key);
  if (!document.contains(name)) {
    return true;
  }
  if (!document.at(name).is_boolean()) {
    SetError(error, "TopScreen " + name + " must be boolean");
    return false;
  }
  *value = document.at(name).get<bool>();
  return true;
}

bool ReadInteger(const nlohmann::json &document, std::string_view key,
                 std::int64_t minimum, std::int64_t maximum,
                 std::int64_t *value, std::string *error) {
  const std::string name(key);
  if (!document.contains(name)) {
    return true;
  }
  if (!document.at(name).is_number_integer()) {
    SetError(error, "TopScreen " + name + " must be an integer");
    return false;
  }
  const auto parsed = document.at(name).get<std::int64_t>();
  if (parsed < minimum || parsed > maximum) {
    SetError(error, "TopScreen " + name + " must be between " +
                        std::to_string(minimum) + " and " +
                        std::to_string(maximum));
    return false;
  }
  *value = parsed;
  return true;
}

bool ReadDpadMapping(const nlohmann::json &document, std::string_view key,
                     std::array<TopScreenDpadAction, 4> *mapping,
                     std::string *error) {
  const std::string name(key);
  if (!document.contains(name)) {
    return true;
  }
  const auto &array = document.at(name);
  if (!array.is_array() || array.size() != mapping->size()) {
    SetError(error, "TopScreen " + name +
                        " must contain Up, Down, Left and Right actions");
    return false;
  }
  for (std::size_t index = 0; index < mapping->size(); ++index) {
    if (!array.at(index).is_string() ||
        !ParseTopScreenDpadAction(array.at(index).get<std::string>(),
                                  &(*mapping)[index])) {
      SetError(error, "TopScreen " + name + " contains an unknown action");
      return false;
    }
  }
  return true;
}

bool DecodeLegacyTopScreenUiConfig(const nlohmann::json &document,
                                   TopScreenUiConfig *config,
                                   std::string *error) {
  constexpr std::array<std::string_view, 9> kAllowedFields{
      "schema", "hud_layout", "hud_scale", "minimap_visible",
      "n64_camera_zoom", "free_camera_enabled", "free_camera_speed",
      "free_camera_invert_x", "free_camera_invert_y"};
  const std::set<std::string_view> allowed(kAllowedFields.begin(),
                                           kAllowedFields.end());
  if (!HasOnlyFields(document, allowed, error)) {
    return false;
  }

  TopScreenUiConfig parsed;
  if (document.contains("hud_layout") &&
      (!document.at("hud_layout").is_string() ||
       !ParseTopScreenHudLayout(document.at("hud_layout").get<std::string>(),
                                &parsed.HudLayout))) {
    SetError(error, "TopScreen hud_layout must be normal or restoration");
    return false;
  }
  if (document.contains("hud_scale")) {
    if (!document.at("hud_scale").is_number()) {
      SetError(error, "TopScreen hud_scale must be numeric");
      return false;
    }
    const double scale = document.at("hud_scale").get<double>();
    const double stepIndex =
        (scale - static_cast<double>(kTopScreenHudScaleMinimum)) /
        static_cast<double>(kTopScreenHudScaleStep);
    if (!std::isfinite(scale) || scale < 0.60 || scale > 1.25 ||
        std::abs(stepIndex - std::round(stepIndex)) > 1.0e-5) {
      SetError(error,
               "legacy TopScreen hud_scale must be between 0.60 and 1.25 "
               "in 0.05 steps");
      return false;
    }
    parsed.HudScale = static_cast<float>(std::min(scale, 1.20));
  }

  bool legacyZoom = false;
  if (!ReadBoolean(document, "minimap_visible", &parsed.MinimapVisible,
                   error) ||
      !ReadBoolean(document, "n64_camera_zoom", &legacyZoom, error) ||
      !ReadBoolean(document, "free_camera_enabled",
                   &parsed.FreeCameraEnabled, error) ||
      !ReadBoolean(document, "free_camera_invert_x",
                   &parsed.FreeCameraInvertX, error) ||
      !ReadBoolean(document, "free_camera_invert_y",
                   &parsed.FreeCameraInvertY, error)) {
    return false;
  }
  parsed.CameraZoomPercent = legacyZoom ? 130U : 100U;
  if (document.contains("free_camera_speed")) {
    constexpr std::array<std::uint8_t, 7> kLegacySpeeds{
        2U, 3U, 4U, 6U, 8U, 12U, 16U};
    std::int64_t speed = 0;
    if (!ReadInteger(document, "free_camera_speed", 0, 16, &speed, error)) {
      return false;
    }
    const auto iterator = std::find(kLegacySpeeds.begin(),
                                    kLegacySpeeds.end(), speed);
    if (iterator == kLegacySpeeds.end()) {
      SetError(error,
               "legacy TopScreen free_camera_speed is not a native value");
      return false;
    }
    parsed.CStickAimSpeedLevel = static_cast<std::uint8_t>(
        std::distance(kLegacySpeeds.begin(), iterator));
  }
  *config = parsed;
  return true;
}

bool DecodeTopScreenUiConfig(const nlohmann::json &document,
                             TopScreenUiConfig *config,
                             std::string *error) {
  if (config == nullptr) {
    SetError(error, "TopScreen UI config output is null");
    return false;
  }
  if (!document.is_object()) {
    SetError(error, "TopScreen UI config root must be an object");
    return false;
  }

  if (!document.contains("schema") || !document.at("schema").is_string()) {
    SetError(error, "TopScreen UI config schema is missing");
    return false;
  }
  const auto schema = document.at("schema").get<std::string>();
  if (schema == kTopScreenUiLegacyConfigSchema) {
    return DecodeLegacyTopScreenUiConfig(document, config, error);
  }
  if (schema != kTopScreenUiConfigSchema) {
    SetError(error, "TopScreen UI config schema must be " +
                        std::string(kTopScreenUiConfigSchema));
    return false;
  }

  constexpr std::array<std::string_view, 24> kAllowedFields{
      "schema",
      "hud_layout",
      "hud_scale",
      "hud_margin_x",
      "hud_margin_y",
      "magic_bar_y",
      "minimap_visible",
      "render_hud",
      "render_dpad_icons",
      "render_items_hint",
      "select_action",
      "exit_items_to_save_screen",
      "camera_zoom_percent",
      "camera_fov_percent",
      "dpad_child",
      "dpad_adult",
      "free_camera_enabled",
      "free_camera_speed_level",
      "free_camera_smoothing",
      "free_camera_invert_x",
      "free_camera_invert_y",
      "c_stick_aim_speed_level",
      "c_stick_aim_invert_x",
      "c_stick_aim_invert_y"};
  const std::set<std::string_view> allowed(kAllowedFields.begin(),
                                           kAllowedFields.end());
  if (!HasOnlyFields(document, allowed, error)) {
    return false;
  }

  TopScreenUiConfig parsed;
  if (document.contains("hud_layout")) {
    if (!document.at("hud_layout").is_string() ||
        !ParseTopScreenHudLayout(
            document.at("hud_layout").get<std::string>(), &parsed.HudLayout)) {
      SetError(error,
               "TopScreen hud_layout must be normal or restoration");
      return false;
    }
  }
  if (document.contains("hud_scale")) {
    if (!document.at("hud_scale").is_number()) {
      SetError(error, "TopScreen hud_scale must be numeric");
      return false;
    }
    const double scale = document.at("hud_scale").get<double>();
    const double stepIndex =
        (scale - static_cast<double>(kTopScreenHudScaleMinimum)) /
        static_cast<double>(kTopScreenHudScaleStep);
    if (!std::isfinite(scale) ||
        scale < static_cast<double>(kTopScreenHudScaleMinimum) ||
        scale > static_cast<double>(kTopScreenHudScaleMaximum) ||
        std::abs(stepIndex - std::round(stepIndex)) > 1.0e-5) {
      SetError(
          error,
          "TopScreen hud_scale must be between 0.60 and 1.20 in 0.05 steps");
      return false;
    }
    parsed.HudScale = static_cast<float>(scale);
  }
  std::int64_t integer = 0;
  if (ReadInteger(document, "hud_margin_x", kTopScreenHudMarginMinimum,
                  kTopScreenHudMarginMaximum, &integer, error)) {
    if (document.contains("hud_margin_x"))
      parsed.HudMarginX = static_cast<std::int8_t>(integer);
  } else {
    return false;
  }
  if (ReadInteger(document, "hud_margin_y", kTopScreenHudMarginMinimum,
                  kTopScreenHudMarginMaximum, &integer, error)) {
    if (document.contains("hud_margin_y"))
      parsed.HudMarginY = static_cast<std::int8_t>(integer);
  } else {
    return false;
  }
  if (ReadInteger(document, "magic_bar_y", 0, 10, &integer, error)) {
    if (document.contains("magic_bar_y"))
      parsed.MagicBarY = static_cast<std::uint8_t>(integer);
  } else {
    return false;
  }
  const auto readPercent = [&](std::string_view key, std::int64_t minimum,
                               std::int64_t maximum,
                               std::uint8_t *destination) {
    if (!ReadInteger(document, key, minimum, maximum, &integer, error))
      return false;
    if (document.contains(std::string(key))) {
      if ((integer - minimum) % 5 != 0) {
        SetError(error, "TopScreen " + std::string(key) +
                            " must use 5 percent steps");
        return false;
      }
      *destination = static_cast<std::uint8_t>(integer);
    }
    return true;
  };
  if (!readPercent("camera_zoom_percent", kTopScreenCameraZoomMinimum,
                   kTopScreenCameraZoomMaximum,
                   &parsed.CameraZoomPercent) ||
      !readPercent("camera_fov_percent", kTopScreenCameraFovMinimum,
                   kTopScreenCameraFovMaximum, &parsed.CameraFovPercent)) {
    return false;
  }
  if (document.contains("select_action") &&
      (!document.at("select_action").is_string() ||
       !ParseTopScreenSelectAction(
           document.at("select_action").get<std::string>(),
           &parsed.SelectAction))) {
    SetError(error, "TopScreen select_action is unknown");
    return false;
  }
  if (document.contains("free_camera_smoothing") &&
      (!document.at("free_camera_smoothing").is_string() ||
       !ParseTopScreenFreeCameraSmoothing(
           document.at("free_camera_smoothing").get<std::string>(),
           &parsed.FreeCameraSmoothing))) {
    SetError(error, "TopScreen free_camera_smoothing is unknown");
    return false;
  }
  if (!ReadInteger(document, "free_camera_speed_level", 0, 6, &integer,
                   error)) {
    return false;
  }
  if (document.contains("free_camera_speed_level"))
    parsed.FreeCameraSpeedLevel = static_cast<std::uint8_t>(integer);
  if (!ReadInteger(document, "c_stick_aim_speed_level", 0, 6, &integer,
                   error)) {
    return false;
  }
  if (document.contains("c_stick_aim_speed_level"))
    parsed.CStickAimSpeedLevel = static_cast<std::uint8_t>(integer);
  if (!ReadDpadMapping(document, "dpad_child", &parsed.ChildDpad, error) ||
      !ReadDpadMapping(document, "dpad_adult", &parsed.AdultDpad, error) ||
      !ReadBoolean(document, "minimap_visible", &parsed.MinimapVisible,
                   error) ||
      !ReadBoolean(document, "render_hud", &parsed.RenderHud, error) ||
      !ReadBoolean(document, "render_dpad_icons", &parsed.RenderDpadIcons,
                   error) ||
      !ReadBoolean(document, "render_items_hint", &parsed.RenderItemsHint,
                   error) ||
      !ReadBoolean(document, "exit_items_to_save_screen",
                   &parsed.ExitItemsToSaveScreen, error) ||
      !ReadBoolean(document, "free_camera_enabled",
                   &parsed.FreeCameraEnabled, error) ||
      !ReadBoolean(document, "free_camera_invert_x",
                   &parsed.FreeCameraInvertX, error) ||
      !ReadBoolean(document, "free_camera_invert_y",
                   &parsed.FreeCameraInvertY, error) ||
      !ReadBoolean(document, "c_stick_aim_invert_x",
                   &parsed.CStickAimInvertX, error) ||
      !ReadBoolean(document, "c_stick_aim_invert_y",
                   &parsed.CStickAimInvertY, error)) {
    return false;
  }

  *config = parsed;
  return true;
}

} // namespace

const char *TopScreenHudLayoutName(TopScreenHudLayout layout) noexcept {
  switch (layout) {
  case TopScreenHudLayout::Normal:
    return "normal";
  case TopScreenHudLayout::Restoration:
    return "restoration";
  }
  return "unknown";
}

bool ParseTopScreenHudLayout(std::string_view value,
                             TopScreenHudLayout *layout) noexcept {
  if (layout == nullptr) {
    return false;
  }
  if (value == "normal") {
    *layout = TopScreenHudLayout::Normal;
    return true;
  }
  if (value == "restoration") {
    *layout = TopScreenHudLayout::Restoration;
    return true;
  }
  return false;
}

const char *TopScreenSelectActionName(TopScreenSelectAction action) noexcept {
  switch (action) {
  case TopScreenSelectAction::SaveScreen:
    return "save_screen";
  case TopScreenSelectAction::MinimapToggle:
    return "minimap_toggle";
  }
  return "unknown";
}

bool ParseTopScreenSelectAction(std::string_view value,
                                TopScreenSelectAction *action) noexcept {
  if (action == nullptr)
    return false;
  if (value == "save_screen") {
    *action = TopScreenSelectAction::SaveScreen;
    return true;
  }
  if (value == "minimap_toggle") {
    *action = TopScreenSelectAction::MinimapToggle;
    return true;
  }
  return false;
}

const char *TopScreenDpadActionName(TopScreenDpadAction action) noexcept {
  switch (action) {
  case TopScreenDpadAction::None:
    return "none";
  case TopScreenDpadAction::View:
    return "view";
  case TopScreenDpadAction::Ocarina:
    return "ocarina";
  case TopScreenDpadAction::Boomerang:
    return "boomerang";
  case TopScreenDpadAction::Slingshot:
    return "slingshot";
  case TopScreenDpadAction::IronBoots:
    return "iron_boots";
  case TopScreenDpadAction::HoverBoots:
    return "hover_boots";
  case TopScreenDpadAction::ItemZr:
    return "item_zr";
  case TopScreenDpadAction::ItemZl:
    return "item_zl";
  case TopScreenDpadAction::SwordToggle:
    return "sword_toggle";
  case TopScreenDpadAction::AllBootsToggle:
    return "all_boots_toggle";
  case TopScreenDpadAction::MinimapToggle:
    return "minimap_toggle";
  case TopScreenDpadAction::TunicToggle:
    return "tunic_toggle";
  case TopScreenDpadAction::ShieldToggle:
    return "shield_toggle";
  }
  return "unknown";
}

bool ParseTopScreenDpadAction(std::string_view value,
                              TopScreenDpadAction *action) noexcept {
  if (action == nullptr)
    return false;
  constexpr std::array<TopScreenDpadAction, 14> kActions{
      TopScreenDpadAction::None,
      TopScreenDpadAction::View,
      TopScreenDpadAction::Ocarina,
      TopScreenDpadAction::Boomerang,
      TopScreenDpadAction::Slingshot,
      TopScreenDpadAction::IronBoots,
      TopScreenDpadAction::HoverBoots,
      TopScreenDpadAction::ItemZr,
      TopScreenDpadAction::ItemZl,
      TopScreenDpadAction::SwordToggle,
      TopScreenDpadAction::AllBootsToggle,
      TopScreenDpadAction::MinimapToggle,
      TopScreenDpadAction::TunicToggle,
      TopScreenDpadAction::ShieldToggle};
  const auto iterator = std::find_if(
      kActions.begin(), kActions.end(),
      [value](TopScreenDpadAction candidate) {
        return value == TopScreenDpadActionName(candidate);
      });
  if (iterator == kActions.end())
    return false;
  *action = *iterator;
  return true;
}

const char *TopScreenFreeCameraSmoothingName(
    TopScreenFreeCameraSmoothing smoothing) noexcept {
  switch (smoothing) {
  case TopScreenFreeCameraSmoothing::Off:
    return "off";
  case TopScreenFreeCameraSmoothing::Light:
    return "light";
  case TopScreenFreeCameraSmoothing::Medium:
    return "medium";
  case TopScreenFreeCameraSmoothing::Default:
    return "default";
  case TopScreenFreeCameraSmoothing::Heavy:
    return "heavy";
  }
  return "unknown";
}

bool ParseTopScreenFreeCameraSmoothing(
    std::string_view value, TopScreenFreeCameraSmoothing *smoothing) noexcept {
  if (smoothing == nullptr)
    return false;
  constexpr std::array<TopScreenFreeCameraSmoothing, 5> kValues{
      TopScreenFreeCameraSmoothing::Off,
      TopScreenFreeCameraSmoothing::Light,
      TopScreenFreeCameraSmoothing::Medium,
      TopScreenFreeCameraSmoothing::Default,
      TopScreenFreeCameraSmoothing::Heavy};
  const auto iterator = std::find_if(
      kValues.begin(), kValues.end(),
      [value](TopScreenFreeCameraSmoothing candidate) {
        return value == TopScreenFreeCameraSmoothingName(candidate);
      });
  if (iterator == kValues.end())
    return false;
  *smoothing = *iterator;
  return true;
}

bool ParseTopScreenUiConfigText(std::string_view text,
                                TopScreenUiConfig *config,
                                std::string *error) {
  try {
    return DecodeTopScreenUiConfig(nlohmann::json::parse(text), config, error);
  } catch (const nlohmann::json::exception &exception) {
    SetError(error,
             "cannot parse TopScreen UI config: " +
                 std::string(exception.what()));
    return false;
  }
}

bool LoadTopScreenUiConfig(const std::filesystem::path &path,
                           TopScreenUiConfig *config, std::string *error) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    SetError(error, "cannot open TopScreen UI config: " + path.string());
    return false;
  }
  const std::string text((std::istreambuf_iterator<char>(stream)),
                         std::istreambuf_iterator<char>());
  return ParseTopScreenUiConfigText(text, config, error);
}

bool SerializeTopScreenUiConfigText(const TopScreenUiConfig &config,
                                    std::string *text,
                                    std::string *error) {
  if (text == nullptr) {
    SetError(error, "TopScreen UI config text output is null");
    return false;
  }
  const nlohmann::json document{
      {"schema", kTopScreenUiConfigSchema},
      {"hud_layout", TopScreenHudLayoutName(config.HudLayout)},
      {"hud_scale", config.HudScale},
      {"hud_margin_x", config.HudMarginX},
      {"hud_margin_y", config.HudMarginY},
      {"magic_bar_y", config.MagicBarY},
      {"minimap_visible", config.MinimapVisible},
      {"render_hud", config.RenderHud},
      {"render_dpad_icons", config.RenderDpadIcons},
      {"render_items_hint", config.RenderItemsHint},
      {"select_action", TopScreenSelectActionName(config.SelectAction)},
      {"exit_items_to_save_screen", config.ExitItemsToSaveScreen},
      {"camera_zoom_percent", config.CameraZoomPercent},
      {"camera_fov_percent", config.CameraFovPercent},
      {"dpad_child",
       {TopScreenDpadActionName(config.ChildDpad[0]),
        TopScreenDpadActionName(config.ChildDpad[1]),
        TopScreenDpadActionName(config.ChildDpad[2]),
        TopScreenDpadActionName(config.ChildDpad[3])}},
      {"dpad_adult",
       {TopScreenDpadActionName(config.AdultDpad[0]),
        TopScreenDpadActionName(config.AdultDpad[1]),
        TopScreenDpadActionName(config.AdultDpad[2]),
        TopScreenDpadActionName(config.AdultDpad[3])}},
      {"free_camera_enabled", config.FreeCameraEnabled},
      {"free_camera_speed_level", config.FreeCameraSpeedLevel},
      {"free_camera_smoothing",
       TopScreenFreeCameraSmoothingName(config.FreeCameraSmoothing)},
      {"free_camera_invert_x", config.FreeCameraInvertX},
      {"free_camera_invert_y", config.FreeCameraInvertY},
      {"c_stick_aim_speed_level", config.CStickAimSpeedLevel},
      {"c_stick_aim_invert_x", config.CStickAimInvertX},
      {"c_stick_aim_invert_y", config.CStickAimInvertY},
  };
  TopScreenUiConfig validated;
  if (!DecodeTopScreenUiConfig(document, &validated, error)) {
    return false;
  }
  *text = document.dump(2) + "\n";
  return true;
}

bool SaveTopScreenUiConfig(const std::filesystem::path &path,
                           const TopScreenUiConfig &config,
                           std::string *error) {
  if (path.empty()) {
    SetError(error, "TopScreen UI config path is empty");
    return false;
  }
  std::string text;
  if (!SerializeTopScreenUiConfigText(config, &text, error)) {
    return false;
  }

  std::filesystem::path temporary = path;
  temporary += ".tmp";
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) {
      SetError(error, "cannot open temporary TopScreen UI config: " +
                          temporary.string());
      return false;
    }
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.flush();
    if (!stream) {
      SetError(error, "cannot write temporary TopScreen UI config: " +
                          temporary.string());
      stream.close();
      std::error_code ignored;
      std::filesystem::remove(temporary, ignored);
      return false;
    }
  }

#if defined(_WIN32)
  if (!MoveFileExW(temporary.c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    const auto code = static_cast<unsigned long>(GetLastError());
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    SetError(error, "cannot replace TopScreen UI config (Windows error " +
                        std::to_string(code) + "): " + path.string());
    return false;
  }
#else
  std::error_code replaceError;
  std::filesystem::rename(temporary, path, replaceError);
  if (replaceError) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    SetError(error, "cannot replace TopScreen UI config: " +
                        replaceError.message());
    return false;
  }
#endif
  return true;
}

TopScreenUiConfigRuntime::TopScreenUiConfigRuntime(
    std::filesystem::path path, TopScreenUiConfig initial)
    : mPath(std::move(path)), mConfig(initial) {}

TopScreenUiConfigSnapshot TopScreenUiConfigRuntime::Snapshot() const {
  std::scoped_lock lock(mMutex);
  return {mConfig, mRevision};
}

const std::filesystem::path &TopScreenUiConfigRuntime::Path() const noexcept {
  return mPath;
}

bool TopScreenUiConfigRuntime::Persistent() const noexcept {
  return !mPath.empty();
}

void TopScreenUiConfigRuntime::Preview(const TopScreenUiConfig &config) {
  std::scoped_lock lock(mMutex);
  if (mConfig == config) {
    return;
  }
  mConfig = config;
  ++mRevision;
}

bool TopScreenUiConfigRuntime::Apply(const TopScreenUiConfig &config,
                                     std::string *error) {
  if (!Persistent()) {
    SetError(error, "no external TopScreen UI config path is selected");
    return false;
  }
  if (!SaveTopScreenUiConfig(mPath, config, error)) {
    return false;
  }
  Preview(config);
  return true;
}

bool TopScreenUiConfigRuntime::Reload(std::string *error) {
  if (!Persistent()) {
    SetError(error, "no external TopScreen UI config path is selected");
    return false;
  }
  TopScreenUiConfig loaded;
  if (!LoadTopScreenUiConfig(mPath, &loaded, error)) {
    return false;
  }
  Preview(loaded);
  return true;
}

} // namespace Oot3dNativeGame
