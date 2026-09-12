#include "oot3d_native_control_config.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace Oot3dNativeGame {
namespace {

template <typename Enum>
struct NamedValue {
  Enum Value;
  std::string_view Name;
};

constexpr std::array<NamedValue<NativeControlAction>,
                     kNativeControlActionCount>
    kActions{{
        {NativeControlAction::MoveForward, "move_forward"},
        {NativeControlAction::MoveBackward, "move_backward"},
        {NativeControlAction::MoveLeft, "move_left"},
        {NativeControlAction::MoveRight, "move_right"},
        {NativeControlAction::A, "a"},
        {NativeControlAction::B, "b"},
        {NativeControlAction::X, "x"},
        {NativeControlAction::Y, "y"},
        {NativeControlAction::L, "l"},
        {NativeControlAction::R, "r"},
        {NativeControlAction::Zl, "zl"},
        {NativeControlAction::Zr, "zr"},
        {NativeControlAction::Select, "select"},
        {NativeControlAction::Start, "start"},
        {NativeControlAction::DpadUp, "dpad_up"},
        {NativeControlAction::DpadDown, "dpad_down"},
        {NativeControlAction::DpadLeft, "dpad_left"},
        {NativeControlAction::DpadRight, "dpad_right"},
        {NativeControlAction::Gear, "gear"},
        {NativeControlAction::Map, "map"},
        {NativeControlAction::Items, "items"},
        {NativeControlAction::LookUp, "look_up"},
        {NativeControlAction::LookDown, "look_down"},
        {NativeControlAction::LookLeft, "look_left"},
        {NativeControlAction::LookRight, "look_right"},
    }};

template <typename Enum, std::size_t Size>
const char* NameOf(Enum value,
                   const std::array<NamedValue<Enum>, Size>& values) noexcept {
  for (const auto& candidate : values) {
    if (candidate.Value == value) {
      return candidate.Name.data();
    }
  }
  return "unknown";
}

template <typename Enum, std::size_t Size>
bool ParseNamed(std::string_view value, Enum* output,
                const std::array<NamedValue<Enum>, Size>& values) noexcept {
  if (output == nullptr) {
    return false;
  }
  for (const auto& candidate : values) {
    if (candidate.Name == value) {
      *output = candidate.Value;
      return true;
    }
  }
  return false;
}

void SetError(std::string* error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
}

std::size_t ActionIndex(NativeControlAction action) {
  return static_cast<std::size_t>(action);
}

void SetKeyboardBinding(NativeControlConfig& config,
                        NativeControlAction action,
                        NativeKeyboardKey primary,
                        NativeKeyboardKey secondary =
                            NativeKeyboardKey::None) {
  auto& binding = config.Bindings[ActionIndex(action)];
  binding.KeyboardPrimary = primary;
  binding.KeyboardSecondary = secondary;
}

void SetGamepadBinding(NativeControlConfig& config,
                       NativeControlAction action,
                       NativeGamepadButton button) {
  config.Bindings[ActionIndex(action)].Gamepad = button;
}

bool FiniteInRange(float value, float minimum, float maximum) {
  return std::isfinite(value) && value >= minimum && value <= maximum;
}

template <typename T>
bool ReadNamedField(const nlohmann::json& object, const char* field,
                    T* value, bool (*parse)(std::string_view, T*) noexcept,
                    std::string* error) {
  if (!object.contains(field)) {
    return true;
  }
  if (!object.at(field).is_string() ||
      !parse(object.at(field).get_ref<const std::string&>(), value)) {
    SetError(error, std::string("invalid native control field: ") + field);
    return false;
  }
  return true;
}

bool DecodeVector3(const nlohmann::json& object, const char* field,
                   std::array<float, 3>* output, std::string* error) {
  if (!object.contains(field)) {
    return true;
  }
  const auto& value = object.at(field);
  if (!value.is_array() || value.size() != 3U) {
    SetError(error, std::string(field) + " must contain three numbers");
    return false;
  }
  for (std::size_t index = 0; index < 3U; ++index) {
    if (!value[index].is_number()) {
      SetError(error, std::string(field) + " must contain three numbers");
      return false;
    }
    const double component = value[index].get<double>();
    if (!std::isfinite(component) || std::abs(component) > 10000.0) {
      SetError(error, std::string(field) + " contains an invalid component");
      return false;
    }
    (*output)[index] = static_cast<float>(component);
  }
  return true;
}

bool DecodeNativeControlConfig(const nlohmann::json& document,
                               NativeControlConfig* config,
                               std::string* error) {
  if (config == nullptr || !document.is_object()) {
    SetError(error, "native control config root/output is invalid");
    return false;
  }
  constexpr std::array<std::string_view, 10> kAllowedFields{
      "schema", "profile", "devices", "bindings", "analog", "aim",
      "free_camera", "controller_guid", "calibration", "capture_mouse"};
  const std::set<std::string_view> allowed(kAllowedFields.begin(),
                                           kAllowedFields.end());
  for (const auto& [key, value] : document.items()) {
    static_cast<void>(value);
    if (!allowed.contains(key)) {
      SetError(error, "unknown native control config field: " + key);
      return false;
    }
  }
  if (document.value("schema", std::string{}) !=
      std::string(kNativeControlConfigSchema)) {
    SetError(error, "native control config schema must be " +
                        std::string(kNativeControlConfigSchema));
    return false;
  }

  NativeControlProfile profile = NativeControlProfile::KeyboardMouse;
  if (!ReadNamedField(document, "profile", &profile,
                      ParseNativeControlProfile, error)) {
    return false;
  }
  NativeControlConfig parsed =
      profile == NativeControlProfile::Custom
          ? NativeControlPreset(NativeControlProfile::KeyboardMouse)
          : NativeControlPreset(profile);
  parsed.Profile = profile;

  if (document.contains("devices")) {
    const auto& devices = document.at("devices");
    if (!devices.is_object()) {
      SetError(error, "native control devices must be an object");
      return false;
    }
    const auto readBool = [&](const char* key, bool* value) {
      if (!devices.contains(key)) {
        return true;
      }
      if (!devices.at(key).is_boolean()) {
        SetError(error, std::string("native control devices.") + key +
                            " must be boolean");
        return false;
      }
      *value = devices.at(key).get<bool>();
      return true;
    };
    if (!readBool("keyboard", &parsed.KeyboardEnabled) ||
        !readBool("mouse", &parsed.MouseEnabled) ||
        !readBool("controller", &parsed.ControllerEnabled)) {
      return false;
    }
  }
  if (document.contains("capture_mouse")) {
    if (!document.at("capture_mouse").is_boolean()) {
      SetError(error, "capture_mouse must be boolean");
      return false;
    }
    parsed.CaptureMouseInGameplay =
        document.at("capture_mouse").get<bool>();
  }
  if (document.contains("controller_guid")) {
    if (!document.at("controller_guid").is_string()) {
      SetError(error, "controller_guid must be a string");
      return false;
    }
    parsed.PreferredControllerGuid =
        document.at("controller_guid").get<std::string>();
  }

  if (document.contains("bindings")) {
    const auto& bindings = document.at("bindings");
    if (!bindings.is_object()) {
      SetError(error, "native control bindings must be an object");
      return false;
    }
    for (const auto& [name, bindingDocument] : bindings.items()) {
      NativeControlAction action{};
      if (!ParseNativeControlAction(name, &action) ||
          !bindingDocument.is_object()) {
        SetError(error, "invalid native control action binding: " + name);
        return false;
      }
      auto& binding = parsed.Bindings[ActionIndex(action)];
      if (!ReadNamedField(bindingDocument, "keyboard_primary",
                          &binding.KeyboardPrimary, ParseNativeKeyboardKey,
                          error) ||
          !ReadNamedField(bindingDocument, "keyboard_secondary",
                          &binding.KeyboardSecondary, ParseNativeKeyboardKey,
                          error) ||
          !ReadNamedField(bindingDocument, "mouse", &binding.Mouse,
                          ParseNativeMouseButton, error) ||
          !ReadNamedField(bindingDocument, "gamepad", &binding.Gamepad,
                          ParseNativeGamepadButton, error)) {
        return false;
      }
    }
  }

  if (document.contains("analog")) {
    const auto& analog = document.at("analog");
    if (!analog.is_object() ||
        !ReadNamedField(analog, "movement_stick", &parsed.MovementStick,
                        ParseNativeAnalogStick, error)) {
      SetError(error, error != nullptr && !error->empty()
                          ? *error
                          : "native control analog must be an object");
      return false;
    }
    const auto readInteger = [&](const char* key, std::int32_t* value) {
      if (!analog.contains(key)) {
        return true;
      }
      if (!analog.at(key).is_number_integer()) {
        SetError(error, std::string("native control analog.") + key +
                            " must be an integer");
        return false;
      }
      *value = analog.at(key).get<std::int32_t>();
      return true;
    };
    if (!readInteger("movement_dead_zone_percent",
                     &parsed.MovementStickDeadZonePercent) ||
        !readInteger("look_dead_zone_percent",
                     &parsed.LookStickDeadZonePercent) ||
        !readInteger("trigger_dead_zone_percent",
                     &parsed.TriggerDeadZonePercent)) {
      return false;
    }
  }

  if (document.contains("aim")) {
    const auto& aim = document.at("aim");
    if (!aim.is_object() ||
        !ReadNamedField(aim, "source", &parsed.NativeAimSource,
                        ParseNativeMotionSource, error)) {
      SetError(error, error != nullptr && !error->empty()
                          ? *error
                          : "native control aim must be an object");
      return false;
    }
    parsed.MouseAimDegreesPerPixel =
        aim.value("mouse_degrees_per_pixel",
                  parsed.MouseAimDegreesPerPixel);
    parsed.RightStickAimMaximumDegreesPerSecond =
        aim.value("right_stick_max_degrees_per_second",
                  parsed.RightStickAimMaximumDegreesPerSecond);
    parsed.ControllerGyroscopeSensitivity =
        aim.value("gyroscope_sensitivity",
                  parsed.ControllerGyroscopeSensitivity);
    parsed.ControllerAccelerometerSensitivity =
        aim.value("accelerometer_sensitivity",
                  parsed.ControllerAccelerometerSensitivity);
    parsed.NativeAimInvertX =
        aim.value("invert_x", parsed.NativeAimInvertX);
    parsed.NativeAimInvertY =
        aim.value("invert_y", parsed.NativeAimInvertY);
  }

  if (document.contains("free_camera")) {
    const auto& camera = document.at("free_camera");
    if (!camera.is_object() ||
        !ReadNamedField(camera, "source", &parsed.FreeCameraSource,
                        ParseNativeMotionSource, error)) {
      SetError(error, error != nullptr && !error->empty()
                          ? *error
                          : "native control free_camera must be an object");
      return false;
    }
    parsed.MouseFreeCameraUnitsPerPixel =
        camera.value("mouse_units_per_pixel",
                     parsed.MouseFreeCameraUnitsPerPixel);
    parsed.FreeCameraMotionSensitivity =
        camera.value("motion_sensitivity",
                     parsed.FreeCameraMotionSensitivity);
  }

  if (document.contains("calibration")) {
    const auto& calibration = document.at("calibration");
    if (!calibration.is_object() ||
        !DecodeVector3(calibration, "gyroscope_bias_dps",
                       &parsed.GyroscopeBiasDegreesPerSecond, error) ||
        !DecodeVector3(calibration, "accelerometer_neutral",
                       &parsed.AccelerometerNeutral, error)) {
      SetError(error, error != nullptr && !error->empty()
                          ? *error
                          : "native control calibration must be an object");
      return false;
    }
  }
  if (!ValidateNativeControlConfig(parsed, error)) {
    return false;
  }
  *config = std::move(parsed);
  return true;
}

} // namespace

const char* NativeControlProfileName(NativeControlProfile profile) noexcept {
  return ThreeDsRecomp::Input::ControlProfileName(profile);
}

const char* NativeControlActionName(NativeControlAction action) noexcept {
  return NameOf(action, kActions);
}

const char* NativeKeyboardKeyName(NativeKeyboardKey key) noexcept {
  return ThreeDsRecomp::Input::KeyboardKeyName(key);
}

const char* NativeMouseButtonName(NativeMouseButton button) noexcept {
  return ThreeDsRecomp::Input::MouseButtonName(button);
}

const char* NativeGamepadButtonName(NativeGamepadButton button) noexcept {
  return ThreeDsRecomp::Input::GamepadButtonName(button);
}

const char* NativeAnalogStickName(NativeAnalogStick stick) noexcept {
  return ThreeDsRecomp::Input::AnalogStickName(stick);
}

const char* NativeMotionSourceName(NativeMotionSource source) noexcept {
  return ThreeDsRecomp::Input::MotionSourceName(source);
}

bool ParseNativeControlProfile(std::string_view value,
                               NativeControlProfile* profile) noexcept {
  return ThreeDsRecomp::Input::ParseControlProfile(value, profile);
}

bool ParseNativeControlAction(std::string_view value,
                              NativeControlAction* action) noexcept {
  return ParseNamed(value, action, kActions);
}

bool ParseNativeKeyboardKey(std::string_view value,
                            NativeKeyboardKey* key) noexcept {
  return ThreeDsRecomp::Input::ParseKeyboardKey(value, key);
}

bool ParseNativeMouseButton(std::string_view value,
                            NativeMouseButton* button) noexcept {
  return ThreeDsRecomp::Input::ParseMouseButton(value, button);
}

bool ParseNativeGamepadButton(std::string_view value,
                              NativeGamepadButton* button) noexcept {
  return ThreeDsRecomp::Input::ParseGamepadButton(value, button);
}

bool ParseNativeAnalogStick(std::string_view value,
                            NativeAnalogStick* stick) noexcept {
  return ThreeDsRecomp::Input::ParseAnalogStick(value, stick);
}

bool ParseNativeMotionSource(std::string_view value,
                             NativeMotionSource* source) noexcept {
  return ThreeDsRecomp::Input::ParseMotionSource(value, source);
}

NativeControlConfig NativeControlPreset(NativeControlProfile profile) {
  NativeControlConfig config;
  config.Profile = profile;
  config.KeyboardEnabled = profile != NativeControlProfile::Controller;
  config.MouseEnabled = profile == NativeControlProfile::KeyboardMouse;
  config.ControllerEnabled = profile == NativeControlProfile::Controller;
  config.CaptureMouseInGameplay =
      profile == NativeControlProfile::KeyboardMouse;
  config.MovementStick =
      profile == NativeControlProfile::Controller
          ? NativeAnalogStick::Left
          : NativeAnalogStick::Disabled;
  config.NativeAimSource =
      profile == NativeControlProfile::Keyboard
          ? NativeMotionSource::DigitalLook
          : (profile == NativeControlProfile::Controller
                 ? NativeMotionSource::RightStick
                 : NativeMotionSource::Mouse);
  config.FreeCameraSource =
      profile == NativeControlProfile::Keyboard
          ? NativeMotionSource::DigitalLook
          : (profile == NativeControlProfile::Controller
                 ? NativeMotionSource::RightStick
                 : NativeMotionSource::Mouse);

  SetKeyboardBinding(config, NativeControlAction::MoveForward,
                     NativeKeyboardKey::W);
  SetKeyboardBinding(config, NativeControlAction::MoveBackward,
                     NativeKeyboardKey::S);
  SetKeyboardBinding(config, NativeControlAction::MoveLeft,
                     NativeKeyboardKey::A);
  SetKeyboardBinding(config, NativeControlAction::MoveRight,
                     NativeKeyboardKey::D);
  SetKeyboardBinding(config, NativeControlAction::A,
                     NativeKeyboardKey::Space, NativeKeyboardKey::Z);
  SetKeyboardBinding(config, NativeControlAction::B, NativeKeyboardKey::X);
  SetKeyboardBinding(config, NativeControlAction::X, NativeKeyboardKey::C);
  SetKeyboardBinding(config, NativeControlAction::Y, NativeKeyboardKey::V);
  SetKeyboardBinding(config, NativeControlAction::L, NativeKeyboardKey::Q);
  SetKeyboardBinding(config, NativeControlAction::R, NativeKeyboardKey::E);
  SetKeyboardBinding(config, NativeControlAction::Zl, NativeKeyboardKey::Num2);
  SetKeyboardBinding(config, NativeControlAction::Zr, NativeKeyboardKey::Num1);
  SetKeyboardBinding(config, NativeControlAction::Select,
                     NativeKeyboardKey::Tab, NativeKeyboardKey::Backspace);
  SetKeyboardBinding(config, NativeControlAction::Start,
                     NativeKeyboardKey::Enter);
  SetKeyboardBinding(config, NativeControlAction::DpadUp,
                     NativeKeyboardKey::ArrowUp);
  SetKeyboardBinding(config, NativeControlAction::DpadDown,
                     NativeKeyboardKey::ArrowDown);
  SetKeyboardBinding(config, NativeControlAction::DpadLeft,
                     NativeKeyboardKey::ArrowLeft);
  SetKeyboardBinding(config, NativeControlAction::DpadRight,
                     NativeKeyboardKey::ArrowRight);
  SetKeyboardBinding(config, NativeControlAction::Gear, NativeKeyboardKey::G);
  SetKeyboardBinding(config, NativeControlAction::Map, NativeKeyboardKey::M);
  SetKeyboardBinding(config, NativeControlAction::Items, NativeKeyboardKey::I);
  SetKeyboardBinding(config, NativeControlAction::LookUp,
                     NativeKeyboardKey::Numpad8);
  SetKeyboardBinding(config, NativeControlAction::LookDown,
                     NativeKeyboardKey::Numpad2);
  SetKeyboardBinding(config, NativeControlAction::LookLeft,
                     NativeKeyboardKey::Numpad4);
  SetKeyboardBinding(config, NativeControlAction::LookRight,
                     NativeKeyboardKey::Numpad6);

  SetGamepadBinding(config, NativeControlAction::A,
                    NativeGamepadButton::A);
  SetGamepadBinding(config, NativeControlAction::B,
                    NativeGamepadButton::B);
  SetGamepadBinding(config, NativeControlAction::X,
                    NativeGamepadButton::X);
  SetGamepadBinding(config, NativeControlAction::Y,
                    NativeGamepadButton::Y);
  SetGamepadBinding(config, NativeControlAction::L,
                    NativeGamepadButton::LeftShoulder);
  SetGamepadBinding(config, NativeControlAction::R,
                    NativeGamepadButton::RightShoulder);
  SetGamepadBinding(config, NativeControlAction::Zl,
                    NativeGamepadButton::LeftTrigger);
  SetGamepadBinding(config, NativeControlAction::Zr,
                    NativeGamepadButton::RightTrigger);
  SetGamepadBinding(config, NativeControlAction::Select,
                    NativeGamepadButton::Back);
  SetGamepadBinding(config, NativeControlAction::Start,
                    NativeGamepadButton::Start);
  SetGamepadBinding(config, NativeControlAction::DpadUp,
                    NativeGamepadButton::DpadUp);
  SetGamepadBinding(config, NativeControlAction::DpadDown,
                    NativeGamepadButton::DpadDown);
  SetGamepadBinding(config, NativeControlAction::DpadLeft,
                    NativeGamepadButton::DpadLeft);
  SetGamepadBinding(config, NativeControlAction::DpadRight,
                    NativeGamepadButton::DpadRight);

  if (profile == NativeControlProfile::KeyboardMouse) {
    config.Bindings[ActionIndex(NativeControlAction::A)].Mouse =
        NativeMouseButton::Left;
    config.Bindings[ActionIndex(NativeControlAction::B)].Mouse =
        NativeMouseButton::Back;
    config.Bindings[ActionIndex(NativeControlAction::R)].Mouse =
        NativeMouseButton::Right;
    config.Bindings[ActionIndex(NativeControlAction::X)].Mouse =
        NativeMouseButton::Forward;
  }
  if (profile == NativeControlProfile::Custom) {
    config.Profile = NativeControlProfile::Custom;
    config.KeyboardEnabled = true;
    config.MouseEnabled = true;
    config.ControllerEnabled = true;
    config.MovementStick = NativeAnalogStick::Left;
  }
  return config;
}

NativeControlConfig NativeControlDefaults() {
  auto config = NativeControlPreset(NativeControlProfile::KeyboardMouse);
  config.Profile = NativeControlProfile::Custom;
  config.ControllerEnabled = true;
  config.MovementStick = NativeAnalogStick::Left;
  config.NativeAimSource = NativeMotionSource::Automatic;
  config.FreeCameraSource = NativeMotionSource::Automatic;
  return config;
}

bool ValidateNativeControlConfig(const NativeControlConfig& config,
                                 std::string* error) {
  if (config.MovementStickDeadZonePercent < 0 ||
      config.MovementStickDeadZonePercent > 95 ||
      config.LookStickDeadZonePercent < 0 ||
      config.LookStickDeadZonePercent > 95 ||
      config.TriggerDeadZonePercent < 0 ||
      config.TriggerDeadZonePercent > 95) {
    SetError(error, "native control dead zones must be in [0, 95]");
    return false;
  }
  if (!FiniteInRange(config.MouseAimDegreesPerPixel, 0.01F, 20.0F) ||
      !FiniteInRange(config.MouseFreeCameraUnitsPerPixel, 0.1F, 40.0F) ||
      !FiniteInRange(config.RightStickAimMaximumDegreesPerSecond, 10.0F,
                     1080.0F) ||
      !FiniteInRange(config.ControllerGyroscopeSensitivity, 0.01F, 10.0F) ||
      !FiniteInRange(config.ControllerAccelerometerSensitivity, 0.01F,
                     10.0F) ||
      !FiniteInRange(config.FreeCameraMotionSensitivity, 0.01F, 10.0F)) {
    SetError(error, "native control sensitivity is outside its valid range");
    return false;
  }
  for (const float value : config.GyroscopeBiasDegreesPerSecond) {
    if (!FiniteInRange(value, -10000.0F, 10000.0F)) {
      SetError(error, "native control gyroscope calibration is invalid");
      return false;
    }
  }
  for (const float value : config.AccelerometerNeutral) {
    if (!FiniteInRange(value, -100.0F, 100.0F)) {
      SetError(error, "native control accelerometer calibration is invalid");
      return false;
    }
  }
  return true;
}

bool ParseNativeControlConfigText(std::string_view text,
                                  NativeControlConfig* config,
                                  std::string* error) {
  try {
    return DecodeNativeControlConfig(nlohmann::json::parse(text), config,
                                     error);
  } catch (const nlohmann::json::exception& exception) {
    SetError(error, "cannot parse native control config: " +
                        std::string(exception.what()));
    return false;
  }
}

bool LoadNativeControlConfig(const std::filesystem::path& path,
                             NativeControlConfig* config,
                             std::string* error) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    SetError(error, "cannot open native control config: " + path.string());
    return false;
  }
  const std::string text((std::istreambuf_iterator<char>(stream)),
                         std::istreambuf_iterator<char>());
  return ParseNativeControlConfigText(text, config, error);
}

bool SerializeNativeControlConfigText(const NativeControlConfig& config,
                                      std::string* text,
                                      std::string* error) {
  if (text == nullptr || !ValidateNativeControlConfig(config, error)) {
    if (text == nullptr) {
      SetError(error, "native control config text output is null");
    }
    return false;
  }
  nlohmann::json bindings = nlohmann::json::object();
  for (const auto& namedAction : kActions) {
    const auto& binding =
        config.Bindings[ActionIndex(namedAction.Value)];
    bindings[std::string(namedAction.Name)] = {
        {"keyboard_primary",
         NativeKeyboardKeyName(binding.KeyboardPrimary)},
        {"keyboard_secondary",
         NativeKeyboardKeyName(binding.KeyboardSecondary)},
        {"mouse", NativeMouseButtonName(binding.Mouse)},
        {"gamepad", NativeGamepadButtonName(binding.Gamepad)},
    };
  }
  const nlohmann::json document{
      {"schema", kNativeControlConfigSchema},
      {"profile", NativeControlProfileName(config.Profile)},
      {"devices",
       {{"keyboard", config.KeyboardEnabled},
        {"mouse", config.MouseEnabled},
        {"controller", config.ControllerEnabled}}},
      {"capture_mouse", config.CaptureMouseInGameplay},
      {"controller_guid", config.PreferredControllerGuid},
      {"bindings", std::move(bindings)},
      {"analog",
       {{"movement_stick", NativeAnalogStickName(config.MovementStick)},
        {"movement_dead_zone_percent",
         config.MovementStickDeadZonePercent},
        {"look_dead_zone_percent", config.LookStickDeadZonePercent},
        {"trigger_dead_zone_percent", config.TriggerDeadZonePercent}}},
      {"aim",
       {{"source", NativeMotionSourceName(config.NativeAimSource)},
        {"mouse_degrees_per_pixel", config.MouseAimDegreesPerPixel},
        {"right_stick_max_degrees_per_second",
         config.RightStickAimMaximumDegreesPerSecond},
        {"gyroscope_sensitivity",
         config.ControllerGyroscopeSensitivity},
        {"accelerometer_sensitivity",
         config.ControllerAccelerometerSensitivity},
        {"invert_x", config.NativeAimInvertX},
        {"invert_y", config.NativeAimInvertY}}},
      {"free_camera",
       {{"source", NativeMotionSourceName(config.FreeCameraSource)},
        {"mouse_units_per_pixel",
         config.MouseFreeCameraUnitsPerPixel},
        {"motion_sensitivity",
         config.FreeCameraMotionSensitivity}}},
      {"calibration",
       {{"gyroscope_bias_dps", config.GyroscopeBiasDegreesPerSecond},
        {"accelerometer_neutral", config.AccelerometerNeutral}}},
  };
  NativeControlConfig validated;
  if (!DecodeNativeControlConfig(document, &validated, error)) {
    return false;
  }
  *text = document.dump(2) + "\n";
  return true;
}

bool SaveNativeControlConfig(const std::filesystem::path& path,
                             const NativeControlConfig& config,
                             std::string* error) {
  if (path.empty()) {
    SetError(error, "native control config path is empty");
    return false;
  }
  std::string text;
  if (!SerializeNativeControlConfigText(config, &text, error)) {
    return false;
  }
  std::error_code directoryError;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), directoryError);
    if (directoryError) {
      SetError(error, "cannot create native control config directory: " +
                          directoryError.message());
      return false;
    }
  }
  std::filesystem::path temporary = path;
  temporary += ".tmp";
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) {
      SetError(error, "cannot open temporary native control config: " +
                          temporary.string());
      return false;
    }
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.flush();
    if (!stream) {
      std::error_code ignored;
      std::filesystem::remove(temporary, ignored);
      SetError(error, "cannot write temporary native control config");
      return false;
    }
  }
#if defined(_WIN32)
  if (!MoveFileExW(temporary.c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    const auto code = static_cast<unsigned long>(GetLastError());
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    SetError(error, "cannot replace native control config (Windows error " +
                        std::to_string(code) + ")");
    return false;
  }
#else
  std::error_code replaceError;
  std::filesystem::rename(temporary, path, replaceError);
  if (replaceError) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    SetError(error, "cannot replace native control config: " +
                        replaceError.message());
    return false;
  }
#endif
  return true;
}

NativeControlConfigRuntime::NativeControlConfigRuntime(
    std::filesystem::path path, NativeControlConfig initial)
    : mPath(std::move(path)), mConfig(std::move(initial)) {}

NativeControlConfigSnapshot NativeControlConfigRuntime::Snapshot() const {
  std::scoped_lock lock(mMutex);
  return {mConfig, mRevision};
}

const std::filesystem::path& NativeControlConfigRuntime::Path() const noexcept {
  return mPath;
}

bool NativeControlConfigRuntime::Persistent() const noexcept {
  return !mPath.empty();
}

bool NativeControlConfigRuntime::Preview(const NativeControlConfig& config,
                                         std::string* error) {
  if (!ValidateNativeControlConfig(config, error)) {
    return false;
  }
  std::scoped_lock lock(mMutex);
  if (mConfig == config) {
    return true;
  }
  mConfig = config;
  ++mRevision;
  return true;
}

bool NativeControlConfigRuntime::Apply(const NativeControlConfig& config,
                                       std::string* error) {
  if (!Persistent()) {
    SetError(error, "no native control config path is selected");
    return false;
  }
  if (!SaveNativeControlConfig(mPath, config, error)) {
    return false;
  }
  return Preview(config, error);
}

bool NativeControlConfigRuntime::Reload(std::string* error) {
  if (!Persistent()) {
    SetError(error, "no native control config path is selected");
    return false;
  }
  NativeControlConfig loaded;
  if (!LoadNativeControlConfig(mPath, &loaded, error)) {
    return false;
  }
  std::scoped_lock lock(mMutex);
  mConfig = std::move(loaded);
  ++mRevision;
  return true;
}

void NativeControlConfigRuntime::ObserveDevices(
    std::vector<NativeControlDeviceDescriptor> devices) {
  std::sort(devices.begin(), devices.end(),
            [](const auto& lhs, const auto& rhs) {
              return lhs.InstanceId < rhs.InstanceId;
            });
  std::scoped_lock lock(mMutex);
  mDevices = std::move(devices);
}

std::vector<NativeControlDeviceDescriptor>
NativeControlConfigRuntime::DevicesSnapshot() const {
  std::scoped_lock lock(mMutex);
  return mDevices;
}

void NativeControlConfigRuntime::ObserveMotion(
    const NativeControlMotionObservation& observation) {
  NativeControlConfig completed;
  bool saveCompleted = false;
  {
    std::scoped_lock lock(mMutex);
    if (!mCalibration.Active) {
      return;
    }
    if (observation.GyroscopeValid) {
      for (std::size_t axis = 0; axis < 3U; ++axis) {
        mCalibrationGyroscopeSum[axis] +=
            observation.GyroscopeDegreesPerSecond[axis];
      }
      ++mCalibrationGyroscopeSamples;
    }
    if (observation.AccelerometerValid) {
      for (std::size_t axis = 0; axis < 3U; ++axis) {
        mCalibrationAccelerometerSum[axis] +=
            observation.Accelerometer[axis];
      }
      ++mCalibrationAccelerometerSamples;
    }
    mCalibration.SamplesCollected =
        std::max(mCalibrationGyroscopeSamples,
                 mCalibrationAccelerometerSamples);
    if (mCalibration.SamplesCollected < mCalibration.SamplesRequired) {
      return;
    }
    if (mCalibrationGyroscopeSamples != 0U) {
      for (std::size_t axis = 0; axis < 3U; ++axis) {
        mConfig.GyroscopeBiasDegreesPerSecond[axis] =
            static_cast<float>(mCalibrationGyroscopeSum[axis] /
                               mCalibrationGyroscopeSamples);
      }
    }
    if (mCalibrationAccelerometerSamples != 0U) {
      for (std::size_t axis = 0; axis < 3U; ++axis) {
        mConfig.AccelerometerNeutral[axis] =
            static_cast<float>(mCalibrationAccelerometerSum[axis] /
                               mCalibrationAccelerometerSamples);
      }
    }
    mCalibration.Active = false;
    mCalibration.LastCalibrationSucceeded =
        mCalibrationGyroscopeSamples != 0U ||
        mCalibrationAccelerometerSamples != 0U;
    ++mRevision;
    completed = mConfig;
    saveCompleted = Persistent() && mCalibration.LastCalibrationSucceeded;
  }
  if (saveCompleted) {
    std::string ignored;
    SaveNativeControlConfig(mPath, completed, &ignored);
  }
}

void NativeControlConfigRuntime::BeginMotionCalibration() noexcept {
  std::scoped_lock lock(mMutex);
  mCalibration = {};
  mCalibration.Active = true;
  mCalibration.SamplesRequired = 60U;
  mCalibrationGyroscopeSum = {};
  mCalibrationAccelerometerSum = {};
  mCalibrationGyroscopeSamples = 0U;
  mCalibrationAccelerometerSamples = 0U;
}

void NativeControlConfigRuntime::CancelMotionCalibration() noexcept {
  std::scoped_lock lock(mMutex);
  mCalibration.Active = false;
}

bool NativeControlConfigRuntime::ResetMotionCalibration(std::string* error) {
  NativeControlConfig reset;
  {
    std::scoped_lock lock(mMutex);
    reset = mConfig;
  }
  reset.GyroscopeBiasDegreesPerSecond = {};
  reset.AccelerometerNeutral = {0.0F, -1.0F, 0.0F};
  return Apply(reset, error);
}

void NativeControlConfigRuntime::BeginBindingCapture(ThreeDsRecomp::Input::BindingDevice device) {
  std::scoped_lock lock(mMutex);
  mBindingCapture.Begin(device);
}

void NativeControlConfigRuntime::CancelBindingCapture() {
  std::scoped_lock lock(mMutex);
  mBindingCapture.Cancel();
}

void NativeControlConfigRuntime::ObserveBindingCapture(const ThreeDsRecomp::Input::HostButtonSource& source, bool cancel) {
  std::scoped_lock lock(mMutex);
  mBindingCapture.Observe(source, cancel);
}

ThreeDsRecomp::Input::BindingCaptureSnapshot NativeControlConfigRuntime::BindingCaptureStatus() const {
  std::scoped_lock lock(mMutex);
  return mBindingCapture.Snapshot();
}

NativeControlCalibrationStatus
NativeControlConfigRuntime::CalibrationStatus() const {
  std::scoped_lock lock(mMutex);
  return mCalibration;
}

} // namespace Oot3dNativeGame
