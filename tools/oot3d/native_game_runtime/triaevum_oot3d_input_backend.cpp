#include "triaevum_oot3d_input_backend.h"
#include "three_ds_sdl_controller.h"

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

ThreeDsRecomp::Input::SdlControllerSelection
SelectController(const NativeControlConfig &config, std::int32_t previousInstance) {
  auto *context = Ship::Context::GetRawInstance();
  auto controlDeck = context != nullptr ? context->GetControlDeck() : nullptr;
  auto devices = controlDeck != nullptr
                     ? controlDeck->GetConnectedPhysicalDeviceManager()
                     : nullptr;
  if (devices == nullptr) {
    return {};
  }
  return ThreeDsRecomp::Input::ResolveSdlController(
      devices->GetConnectedSDLGamepadsForPort(0), config.PreferredControllerGuid,
      config.PreferredControllerSerial, previousInstance);
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
    return ThreeDsRecomp::Input::IsSdlControllerButtonHeld(mController, binding, mTriggerThreshold);
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
  const auto selected = SelectController(mConfig, mControllerInstance);
  if (mControllerInstance != selected.InstanceId) {
    mRightStickProfile = {};
    mVirtualMotion.ResetController();
  }
  mControllerInstance = selected.InstanceId;
  SDL_GameController *controller = mConfig.ControllerEnabled ? selected.Controller : nullptr;
  const std::int16_t triggerThreshold = static_cast<std::int16_t>(
      32767 * std::clamp(mConfig.TriggerDeadZonePercent, 0, 95) / 100);
  WindowButtonSource buttonSource(window, controller, triggerThreshold);
  const ThreeDsRecomp::Input::HostDeviceEnablement enabled{
      mConfig.KeyboardEnabled, mConfig.MouseEnabled, mConfig.ControllerEnabled};
  for (std::size_t index = 0U; index < kNativeControlActionCount; ++index) {
    host.Actions[index] = ThreeDsRecomp::Input::IsHostBindingHeld(
        mConfig.Bindings[index], enabled, buttonSource);
  }

  ThreeDsRecomp::Input::SampleSdlController(controller, host, true,
      mConfig.ControllerTouchpadEnabled ? mConfig.ControllerTouchpadIndex : -1, triggerThreshold);

  const auto mouseDelta = window.GetMouseDelta();
  const bool mouseOwned = mConfig.MouseEnabled && !window.IsMouseCaptureReleased();
  host.MouseDeltaX = mouseOwned ? mouseDelta.x : 0;
  host.MouseDeltaY = mouseOwned ? mouseDelta.y : 0;
  const auto selectedDevice = std::find_if(selected.Devices.begin(), selected.Devices.end(),
      [](const auto& device) { return device.Selected; });
  const auto effective = ControlsForDevice(mConfig,
      selectedDevice != selected.Devices.end() ? &*selectedDevice : nullptr);
  auto frame =
      MapNativeControlInput(effective, host, {}, &mRightStickProfile, true, &mVirtualMotion);
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

  if (!frame.Hid.TouchPressed) {
    const auto pad = ThreeDsRecomp::Input::MapNormalizedTouch(host.ControllerTouch);
    if (pad.Pressed) {
      frame.Hid.TouchX = pad.X;
      frame.Hid.TouchY = pad.Y;
      frame.Hid.TouchPressed = true;
    }
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
