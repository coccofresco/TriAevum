#include "triaevum_oot3d_input_backend.h"

#include "fast/Fast3dWindow.h"
#include "ship/Context.h"
#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/controller/physicaldevice/ConnectedPhysicalDeviceManager.h"
#include "ship/controller/physicaldevice/GlobalSDLDeviceSettings.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Oot3dNativeGame {
namespace {

struct SelectedController {
  SDL_GameController *Controller = nullptr;
  std::string Guid;
};

std::optional<SelectedController>
SelectController(const NativeControlConfig &config) {
  auto *context = Ship::Context::GetRawInstance();
  auto controlDeck = context != nullptr ? context->GetControlDeck() : nullptr;
  auto devices = controlDeck != nullptr
                     ? controlDeck->GetConnectedPhysicalDeviceManager()
                     : nullptr;
  if (devices == nullptr) {
    return std::nullopt;
  }
  auto connected = devices->GetConnectedSDLGamepadsForPort(0);
  std::vector<std::pair<std::int32_t, SDL_GameController *>> ordered(
      connected.begin(), connected.end());
  std::sort(ordered.begin(), ordered.end(),
            [](const auto &left, const auto &right) {
              return left.first < right.first;
            });
  std::optional<SelectedController> selected;
  for (const auto &[instanceId, controller] : ordered) {
    static_cast<void>(instanceId);
    if (controller == nullptr ||
        SDL_GameControllerGetAttached(controller) != SDL_TRUE) {
      continue;
    }
    SDL_Joystick *joystick = SDL_GameControllerGetJoystick(controller);
    char guidText[33]{};
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joystick), guidText,
                              static_cast<int>(sizeof(guidText)));
    const bool preferred = !config.PreferredControllerGuid.empty() &&
                           config.PreferredControllerGuid == guidText;
    if (preferred ||
        (config.PreferredControllerGuid.empty() && !selected.has_value())) {
      selected = SelectedController{controller, guidText};
    }
  }
  return selected;
}

class WindowButtonSource final : public ThreeDsRecomp::Input::HostButtonSource {
public:
  WindowButtonSource(Fast::Fast3dWindow &window, SDL_GameController *controller,
                     std::int16_t triggerThreshold)
      : mWindow(window), mController(controller),
        mTriggerThreshold(triggerThreshold) {}

  bool IsKeyboardKeyHeld(NativeKeyboardKey key) const noexcept override {
    return key != NativeKeyboardKey::None && key != NativeKeyboardKey::Escape &&
           mWindow.IsKeyDown(static_cast<std::int32_t>(key));
  }

  bool IsMouseButtonHeld(NativeMouseButton button) const noexcept override {
    return button != NativeMouseButton::None &&
           mWindow.GetMouseState(static_cast<Ship::MouseBtn>(button));
  }

  bool
  IsGamepadButtonHeld(NativeGamepadButton binding) const noexcept override {
    if (mController == nullptr || binding == NativeGamepadButton::None) {
      return false;
    }
    const auto button = [&](SDL_GameControllerButton value) {
      return SDL_GameControllerGetButton(mController, value) != 0;
    };
    switch (binding) {
    case NativeGamepadButton::A:
      return button(SDL_CONTROLLER_BUTTON_A);
    case NativeGamepadButton::B:
      return button(SDL_CONTROLLER_BUTTON_B);
    case NativeGamepadButton::X:
      return button(SDL_CONTROLLER_BUTTON_X);
    case NativeGamepadButton::Y:
      return button(SDL_CONTROLLER_BUTTON_Y);
    case NativeGamepadButton::Back:
      return button(SDL_CONTROLLER_BUTTON_BACK);
    case NativeGamepadButton::Guide:
      return button(SDL_CONTROLLER_BUTTON_GUIDE);
    case NativeGamepadButton::Start:
      return button(SDL_CONTROLLER_BUTTON_START);
    case NativeGamepadButton::LeftStick:
      return button(SDL_CONTROLLER_BUTTON_LEFTSTICK);
    case NativeGamepadButton::RightStick:
      return button(SDL_CONTROLLER_BUTTON_RIGHTSTICK);
    case NativeGamepadButton::LeftShoulder:
      return button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    case NativeGamepadButton::RightShoulder:
      return button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    case NativeGamepadButton::DpadUp:
      return button(SDL_CONTROLLER_BUTTON_DPAD_UP);
    case NativeGamepadButton::DpadDown:
      return button(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    case NativeGamepadButton::DpadLeft:
      return button(SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    case NativeGamepadButton::DpadRight:
      return button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    case NativeGamepadButton::LeftTrigger:
      return SDL_GameControllerGetAxis(mController,
                                       SDL_CONTROLLER_AXIS_TRIGGERLEFT) >
             mTriggerThreshold;
    case NativeGamepadButton::RightTrigger:
      return SDL_GameControllerGetAxis(mController,
                                       SDL_CONTROLLER_AXIS_TRIGGERRIGHT) >
             mTriggerThreshold;
    case NativeGamepadButton::None:
      return false;
    }
    return false;
  }

private:
  Fast::Fast3dWindow &mWindow;
  SDL_GameController *mController = nullptr;
  std::int16_t mTriggerThreshold = 0;
};

float NormalizeAxis(std::int16_t value) noexcept {
  return std::clamp(
      static_cast<float>(value) /
          static_cast<float>(ThreeDsRecomp::Input::kNativeStickMaximum),
      -1.0F, 1.0F);
}

} // namespace

TriAevumOot3dInputBackend::TriAevumOot3dInputBackend(NativeControlConfig config)
    : mConfig(std::move(config)) {}

bool TriAevumOot3dInputBackend::Poll(Fast::Fast3dWindow &window,
                                     double samplePeriodSeconds) {
  NativeControlHostInputState host;
  host.SamplePeriodSeconds = std::clamp(samplePeriodSeconds, 0.001, 0.25);
  const auto selected =
      mConfig.ControllerEnabled ? SelectController(mConfig) : std::nullopt;
  SDL_GameController *controller =
      selected.has_value() ? selected->Controller : nullptr;
  const std::int16_t triggerThreshold = static_cast<std::int16_t>(
      32767 * std::clamp(mConfig.TriggerDeadZonePercent, 0, 95) / 100);
  WindowButtonSource buttonSource(window, controller, triggerThreshold);
  const ThreeDsRecomp::Input::HostDeviceEnablement enabled{
      mConfig.KeyboardEnabled, mConfig.MouseEnabled, mConfig.ControllerEnabled};
  for (std::size_t index = 0U; index < kNativeControlActionCount; ++index) {
    host.Actions[index] = ThreeDsRecomp::Input::IsHostBindingHeld(
        mConfig.Bindings[index], enabled, buttonSource);
  }

  if (controller != nullptr) {
    const auto axis = [&](SDL_GameControllerAxis value) {
      return SDL_GameControllerGetAxis(controller, value);
    };
    const auto invertedAxis = [&](SDL_GameControllerAxis value) {
      return static_cast<std::int16_t>(
          std::clamp(-static_cast<std::int32_t>(axis(value)), -32767, 32767));
    };
    host.LeftStickX = axis(SDL_CONTROLLER_AXIS_LEFTX);
    host.LeftStickY = invertedAxis(SDL_CONTROLLER_AXIS_LEFTY);
    host.RightStickX = axis(SDL_CONTROLLER_AXIS_RIGHTX);
    host.RightStickY = invertedAxis(SDL_CONTROLLER_AXIS_RIGHTY);
#if SDL_VERSION_ATLEAST(2, 0, 14)
    constexpr float kGravityMetersPerSecondSquared = 9.80665F;
    constexpr float kRadiansToDegrees = 57.2957795130823208768F;
    if (SDL_GameControllerHasSensor(controller, SDL_SENSOR_ACCEL) == SDL_TRUE) {
      if (SDL_GameControllerIsSensorEnabled(controller, SDL_SENSOR_ACCEL) !=
          SDL_TRUE) {
        SDL_GameControllerSetSensorEnabled(controller, SDL_SENSOR_ACCEL,
                                           SDL_TRUE);
      }
      std::array<float, 3> sample{};
      if (SDL_GameControllerGetSensorData(
              controller, SDL_SENSOR_ACCEL, sample.data(),
              static_cast<int>(sample.size())) == 0) {
        host.ControllerMotion.Accelerometer = {
            sample[0] / kGravityMetersPerSecondSquared,
            -sample[1] / kGravityMetersPerSecondSquared,
            sample[2] / kGravityMetersPerSecondSquared};
        host.ControllerMotion.AccelerometerValid = true;
      }
    }
    if (SDL_GameControllerHasSensor(controller, SDL_SENSOR_GYRO) == SDL_TRUE) {
      if (SDL_GameControllerIsSensorEnabled(controller, SDL_SENSOR_GYRO) !=
          SDL_TRUE) {
        SDL_GameControllerSetSensorEnabled(controller, SDL_SENSOR_GYRO,
                                           SDL_TRUE);
      }
      std::array<float, 3> sample{};
      if (SDL_GameControllerGetSensorData(
              controller, SDL_SENSOR_GYRO, sample.data(),
              static_cast<int>(sample.size())) == 0) {
        host.ControllerMotion.GyroscopeDegreesPerSecond = {
            -sample[0] * kRadiansToDegrees, sample[1] * kRadiansToDegrees,
            -sample[2] * kRadiansToDegrees};
        host.ControllerMotion.GyroscopeValid = true;
      }
    }
#endif
  }

  const auto mouseDelta = window.GetMouseDelta();
  const bool mouseOwned = mConfig.MouseEnabled && !window.IsMouseCaptureReleased();
  host.MouseDeltaX = mouseOwned ? mouseDelta.x : 0;
  host.MouseDeltaY = mouseOwned ? mouseDelta.y : 0;
  auto frame =
      MapNativeControlInput(mConfig, host, {}, &mRightStickProfile, true);
  ApplyNativeControlShortcutTouch(host, frame);
  const auto pointer = window.GetMousePos();
  const auto touch = MapHostPointerToNativeA32Touch(
      pointer.x, pointer.y, window.GetWidth(), window.GetHeight(),
      window.GetMouseState(Ship::LUS_MOUSE_BTN_LEFT),
      NativeA32TouchPresentation::TopScreen400x240);
  if (!frame.Hid.TouchPressed && touch.Inside) {
    frame.Hid.TouchX = touch.X;
    frame.Hid.TouchY = touch.Y;
    frame.Hid.TouchPressed = touch.Pressed;
  }

  ++mStats.HostPolls;
  mState = {};
  mState.sampleSequence = mStats.HostPolls;
  mState.buttons = frame.Hid.Buttons;
  mState.leftStickX = NormalizeAxis(frame.Hid.CirclePadX);
  mState.leftStickY = NormalizeAxis(frame.Hid.CirclePadY);
  mState.rightStickX = NormalizeAxis(frame.CStick.X);
  mState.rightStickY = NormalizeAxis(frame.CStick.Y);
  const bool touchValid = frame.Hid.TouchPressed || touch.Inside;
  if (touchValid) {
    mState.touchX = static_cast<float>(frame.Hid.TouchX) /
                    static_cast<float>(NativeA32TouchWidth - 1U);
    mState.touchY = static_cast<float>(frame.Hid.TouchY) /
                    static_cast<float>(NativeA32TouchHeight - 1U);
    mState.flags |= TRIAEVUM_INPUT_TOUCH_VALID_V1;
    if (frame.Hid.TouchPressed) {
      mState.flags |= TRIAEVUM_INPUT_TOUCH_PRESSED_V1;
    }
  }
  if (frame.Hid.GyroscopeValid) {
    mState.gyroscopeX = frame.Hid.GyroscopeDegreesPerSecond[0];
    mState.gyroscopeY = frame.Hid.GyroscopeDegreesPerSecond[1];
    mState.gyroscopeZ = frame.Hid.GyroscopeDegreesPerSecond[2];
    mState.flags |= TRIAEVUM_INPUT_GYROSCOPE_VALID_V1;
  }
  if (frame.Hid.AccelerometerValid) {
    mState.accelerometerX = frame.Hid.Accelerometer[0];
    mState.accelerometerY = frame.Hid.Accelerometer[1];
    mState.accelerometerZ = frame.Hid.Accelerometer[2];
    mState.flags |= TRIAEVUM_INPUT_ACCELEROMETER_VALID_V1;
  }
  return window.IsRunning();
}

TriAevumModuleStatusV1
TriAevumOot3dInputBackend::ReadState(std::uint32_t playerIndex,
                                     triaevum::module::InputStateV1 *state) {
  if (state == nullptr) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  if (playerIndex != 0U) {
    return TRIAEVUM_MODULE_OPERATION_NOT_SUPPORTED_V1;
  }
  *state = mState;
  ++mStats.ServiceReads;
  return TRIAEVUM_MODULE_OK_V1;
}

const TriAevumOot3dInputStats &
TriAevumOot3dInputBackend::Stats() const noexcept {
  return mStats;
}

} // namespace Oot3dNativeGame
