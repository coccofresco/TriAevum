// Host input polling of the A32 window and the TopScreen pause transitions it drives.
#include "oot3d_native_a32_window_shared.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string>

namespace Oot3dA32Window {

namespace {

// The user's account name, removed from device names in the device log.
const std::string &AccountName() {
  static const std::string name = [] {
    for (const char *variable : {"USERNAME", "USER", "LOGNAME"})
      if (const char *value = std::getenv(variable); value != nullptr && value[0] != '\0') return std::string(value);
    return std::string();
  }();
  return name;
}

} // namespace

Oot3dNativeGame::NativeA32InputFrame
PollNativeA32Input(Fast::Fast3dWindow &window,
                   const Fast::Extension::PresentationPicture &picture,
                   bool nativeFrontendTouchEnabled,
                   bool hostGuiVisible,
                   bool topScreenUiProfile,
                   bool topScreenFreeCameraEnabled,
                   const Oot3dNativeGame::NativeA32TouchSurface *topScreenTouchSurface,
                   Oot3dNativeGame::NativeControlConfigRuntime &controls,
                   const Oot3dNativeGame::NativeAimProfileTransform
                       &aimTransform,
                   double samplePeriodSeconds,
                   bool guestRefreshWillConsume,
                   NativeControlPollingState &pollingState) {
  using namespace Oot3dNativeGame;
  if (!pollingState.HostPresses)
    pollingState.HostPresses = std::make_unique<ThreeDsRecomp::Input::SdlHostPressWatch>();
  auto &hostPresses = *pollingState.HostPresses;
  auto config = controls.Snapshot().Config;
  NativeControlHostInputState host;
  if (hostGuiVisible) pollingState.PendingInputSeconds = 0.0;
  host.SamplePeriodSeconds = ThreeDsRecomp::Input::ConsumeInputPeriod(
      samplePeriodSeconds, guestRefreshWillConsume || hostGuiVisible, pollingState.PendingInputSeconds);
  const auto &io = ImGui::GetIO();
  const bool keyboardCaptured = hostGuiVisible || io.WantCaptureKeyboard;
  const bool mouseCaptured = hostGuiVisible;

  ThreeDsRecomp::Input::SdlControllerSelection selectedController;
  auto *context = Ship::Context::GetRawInstance();
  auto controlDeck = context != nullptr ? context->GetControlDeck() : nullptr;
  if (controlDeck != nullptr) {
    auto devices = controlDeck->GetConnectedPhysicalDeviceManager();
    if (devices != nullptr) {
      selectedController = ThreeDsRecomp::Input::ResolveSdlController(
          devices->GetConnectedSDLGamepadsForPort(0), config.PreferredControllerGuid,
          config.PreferredControllerSerial, pollingState.ControllerInstance);
    }
  }
  if (pollingState.ControllerInstance != selectedController.InstanceId) {
    pollingState.RightStickProfile = {};
    pollingState.VirtualMotion.ResetController();
    pollingState.PreviousControllerButtons = pollingState.PendingControllerButtons = 0;
  }
  pollingState.ControllerInstance = selectedController.InstanceId;
  const auto selectedDevice = std::find_if(selectedController.Devices.begin(), selectedController.Devices.end(),
      [](const auto& device) { return device.Selected; });
  config = ControlsForDevice(config, selectedDevice != selectedController.Devices.end() ? &*selectedDevice : nullptr);
  // The input device log for support reports: the pads and the keyboard at
  // startup and whenever a pad comes, goes or is selected (no serials). At
  // warning level, the level a release build's game log keeps (as the Vulkan
  // device line), so it reaches the support bundle.
  {
    ThreeDsRecomp::Input::KeyboardDescription keyboard;
    keyboard.Enabled = config.KeyboardEnabled;
    if (const char *driver = SDL_GetCurrentVideoDriver()) keyboard.VideoDriver = driver;
    keyboard.WindowFocused = SDL_GetKeyboardFocus() != nullptr;
    for (const auto &line : pollingState.DeviceLog.Observe(selectedController.Devices, keyboard, AccountName()))
      SPDLOG_WARN("{}", line);
  }
  // TRIAEVUM_INPUT_TRACE=1: the SDL key events since the last poll (and,
  // below, each bound key's edges).
  if (!pollingState.KeyTraceChecked) {
    pollingState.KeyTraceChecked = true;
    if (ThreeDsRecomp::Input::SdlKeyTrace::Requested()) {
      pollingState.KeyTrace = std::make_unique<ThreeDsRecomp::Input::SdlKeyTrace>();
      SPDLOG_WARN("TRIAEVUM_INPUT_TRACE enabled video_driver={}",
                  SDL_GetCurrentVideoDriver() != nullptr ? SDL_GetCurrentVideoDriver() : "none");
    }
  }
  if (pollingState.KeyTrace)
    for (const auto &line : pollingState.KeyTrace->Drain()) SPDLOG_WARN("{}", line);
  controls.ObserveDevices(std::move(selectedController.Devices));

  const int16_t triggerThreshold = static_cast<int16_t>(
      32767 * std::clamp(config.TriggerDeadZonePercent, 0, 95) / 100);
  class LusHostButtonSource final
      : public ThreeDsRecomp::Input::HostButtonSource {
   public:
    LusHostButtonSource(Fast::Fast3dWindow &window,
                        bool keyboardCaptured,
                        bool mouseCaptured,
                        SDL_GameController *controller,
                        int16_t triggerThreshold,
                        bool pointerOwnsLeft = false)
        : mWindow(window), mKeyboardCaptured(keyboardCaptured),
          mMouseCaptured(mouseCaptured), mController(controller),
          mTriggerThreshold(triggerThreshold), mPointerOwnsLeft(pointerOwnsLeft) {}

    bool IsKeyboardKeyHeld(
        NativeKeyboardKey key) const noexcept override {
      return !mKeyboardCaptured && key != NativeKeyboardKey::None && key != NativeKeyboardKey::Escape &&
             mWindow.IsKeyDown(static_cast<int32_t>(key));
    }

    bool IsMouseButtonHeld(
        NativeMouseButton button) const noexcept override {
      return !mMouseCaptured && button != NativeMouseButton::None &&
             !ThreeDsRecomp::Input::PointerButtonOwnership::OwnsBinding(button, mPointerOwnsLeft) &&
             mWindow.GetMouseState(static_cast<Ship::MouseBtn>(button));
    }

    bool IsGamepadButtonHeld(
        NativeGamepadButton binding) const noexcept override {
      return ThreeDsRecomp::Input::IsSdlControllerButtonHeld(mController, binding, mTriggerThreshold);
    }

   private:
    Fast::Fast3dWindow &mWindow;
    bool mKeyboardCaptured = false;
    bool mMouseCaptured = false;
    SDL_GameController *mController = nullptr;
    int16_t mTriggerThreshold = 0;
    bool mPointerOwnsLeft = false;
  };
  // Remapping observes the same physical source, before gameplay's device and
  // UI filters. It works even for a currently disabled device.
  LusHostButtonSource rawButtonSource(window, false, false,
      selectedController.Controller,
      triggerThreshold);
  if (hostGuiVisible) {
    controls.ObserveBindingCapture(rawButtonSource, window.IsKeyDown(Ship::LUS_KB_ESCAPE));
  } else {
    const auto phase = controls.BindingCaptureStatus().Phase;
    if (phase != ThreeDsRecomp::Input::BindingCapturePhase::Idle &&
        phase != ThreeDsRecomp::Input::BindingCapturePhase::Cancelled)
      controls.CancelBindingCapture();
  }
  // The touch screen is fitted into the presentation picture like every UI
  // canvas, so the pointer is mapped through the picture.
  const auto mapPointerToTouch = [&](bool pressed) {
    const auto pointer = window.GetMousePos();
    return Oot3dNativeGame::MapHostPointerToNativeA32Touch(
        pointer.x - static_cast<int32_t>(picture.X), pointer.y - static_cast<int32_t>(picture.Y),
        picture.Width, picture.Height, pressed,
        topScreenUiProfile ? Oot3dNativeGame::NativeA32TouchPresentation::TopScreen400x240
                           : Oot3dNativeGame::NativeA32TouchPresentation::NativeLowerScreen320x240,
        topScreenTouchSurface);
  };
  // A click on a native page that owns the pointer is that page's touch
  // (below), so the left button is not also the button bound to it there.
  const bool pointerOnPage = nativeFrontendTouchEnabled && !hostGuiVisible && mapPointerToTouch(true).Inside;
  const bool pointerOwnsLeft =
      pollingState.PointerButton.Update(pointerOnPage, window.GetMouseState(Ship::LUS_MOUSE_BTN_LEFT));
  LusHostButtonSource buttonSource(
      window, keyboardCaptured, mouseCaptured,
      !hostGuiVisible ? selectedController.Controller : nullptr,
      triggerThreshold, pointerOwnsLeft);
  const ThreeDsRecomp::Input::HostDeviceEnablement enabledDevices{
      config.KeyboardEnabled,
      config.MouseEnabled,
      config.ControllerEnabled,
  };

  // On-screen controls are one more source of the same 3DS controls; a host
  // surface owns the pointer, so they are dropped while one is up.
  using ThreeDsRecomp::Input::VirtualPad;
  auto touchControls = ThreeDsRecomp::Input::TouchOverlayPad().Read(guestRefreshWillConsume || hostGuiVisible);
  if (hostGuiVisible) touchControls = {};

  for (size_t index = 0; index < kNativeControlActionCount; ++index) {
    if (hostGuiVisible) pollingState.PendingPressedActions[index] = false;
    const auto &binding = config.Bindings[index];
    ThreeDsRecomp::Input::DigitalControl control{};
    const uint32_t touchBit =
        MapNativeControlActionToThreeDsControl(static_cast<NativeControlAction>(index), &control)
            ? VirtualPad::Bit(control)
            : 0U;
    const bool physicalHeld = ThreeDsRecomp::Input::IsHostBindingHeld(
        binding, enabledDevices, buttonSource) || (touchControls.Held & touchBit) != 0U;
    if (pollingState.KeyTrace) {
      // Each bound key's edges: the key state the window keeps from SDL
      // (raw), and what gameplay reads from it after the keyboard switch and
      // a host menu's capture (effective).
      const auto keyDown = [&](NativeKeyboardKey key) {
        return key != NativeKeyboardKey::None && window.IsKeyDown(static_cast<int32_t>(key));
      };
      const bool raw = keyDown(binding.KeyboardPrimary) || keyDown(binding.KeyboardSecondary);
      const bool effective = config.KeyboardEnabled && (buttonSource.IsKeyboardKeyHeld(binding.KeyboardPrimary) ||
                                                        buttonSource.IsKeyboardKeyHeld(binding.KeyboardSecondary));
      const uint8_t state = static_cast<uint8_t>((raw ? 1U : 0U) | (effective ? 2U : 0U));
      if (state != pollingState.TraceKeyState[index]) {
        pollingState.TraceKeyState[index] = state;
        SPDLOG_WARN("TRIAEVUM_INPUT_TRACE action={} 3ds={} keys={}/{} raw={} effective={} keyboard_enabled={} "
                    "captured={}",
                    NativeControlActionName(static_cast<NativeControlAction>(index)),
                    touchBit != 0U ? ThreeDsRecomp::Input::DigitalControlName(control) : "none",
                    NativeKeyboardKeyName(binding.KeyboardPrimary), NativeKeyboardKeyName(binding.KeyboardSecondary),
                    raw ? 1 : 0, effective ? 1 : 0, config.KeyboardEnabled ? 1 : 0, keyboardCaptured ? 1 : 0);
      }
    }
    if (((physicalHeld && !pollingState.PhysicalActions[index]) || (touchControls.Pressed & touchBit) != 0U) &&
        static_cast<NativeControlAction>(index) !=
            NativeControlAction::Start) {
      pollingState.PendingPressedActions[index] = true;
    }
    pollingState.PhysicalActions[index] = physicalHeld;
    host.Actions[index] = physicalHeld;
    // Start keeps no pending press (its own latch routes it); an on-screen tap
    // shorter than a poll still reaches it as a one-poll press.
    if (static_cast<NativeControlAction>(index) == NativeControlAction::Start &&
        (touchControls.Pressed & touchBit) != 0U)
      host.Actions[index] = true;
    if (guestRefreshWillConsume &&
        static_cast<NativeControlAction>(index) !=
            NativeControlAction::Start) {
      host.Actions[index] =
          physicalHeld || pollingState.PendingPressedActions[index];
      pollingState.PendingPressedActions[index] = false;
    }
  }

  ThreeDsRecomp::Input::SampleSdlController(
      config.ControllerEnabled ? selectedController.Controller : nullptr, host, !hostGuiVisible,
      config.ControllerTouchpadEnabled ? config.ControllerTouchpadIndex : -1, triggerThreshold);
  controls.ObserveMotion(host.ControllerMotion);
  // The stronger of the pad's and the on-screen stick, before dead zones.
  const auto mergeTouchStick = [](int16_t &x, int16_t &y, const std::array<float, 2> &touch) {
    const float padX = static_cast<float>(x) / 32767.0F;
    const float padY = static_cast<float>(y) / 32767.0F;
    if (touch[0] * touch[0] + touch[1] * touch[1] <= padX * padX + padY * padY) return;
    x = static_cast<int16_t>(std::lround(touch[0] * 32767.0F));
    y = static_cast<int16_t>(std::lround(touch[1] * 32767.0F));
  };
  mergeTouchStick(host.LeftStickX, host.LeftStickY, touchControls.CirclePad);
  mergeTouchStick(host.RightStickX, host.RightStickY, touchControls.CStick);
  pollingState.PendingControllerButtons |= host.ControllerButtons & ~pollingState.PreviousControllerButtons;
  pollingState.PreviousControllerButtons = host.ControllerButtons;
  if (guestRefreshWillConsume) {
    host.ControllerPressed = pollingState.PendingControllerButtons;
    pollingState.PendingControllerButtons = 0;
  }
  if (hostGuiVisible) {
    pollingState.PendingControllerButtons = 0;
    host.ControllerMotion = {};
    pollingState.RightStickProfile = {};
  }

  const bool sourceUsesMouse =
      config.NativeAimSource == NativeMotionSource::Mouse ||
      config.NativeAimSource == NativeMotionSource::Automatic ||
      (topScreenUiProfile && topScreenFreeCameraEnabled &&
       (config.FreeCameraSource == NativeMotionSource::Mouse ||
        config.FreeCameraSource == NativeMotionSource::Automatic));
  const bool gameplayMouseEligible = ResolveNativeGameplayMouseOwnership(
      nativeFrontendTouchEnabled, hostGuiVisible, config.MouseEnabled,
      sourceUsesMouse);
  const bool wasCaptured = window.IsMouseCaptured();
  window.SetMouseCapture(gameplayMouseEligible && config.CaptureMouseInGameplay);
  window.SetCursorVisibility(!window.IsMouseCaptured());
  const bool gameplayMouseOwned = gameplayMouseEligible && !window.IsMouseCaptureReleased();
  bool mouseCaptureChanged =
      gameplayMouseOwned != pollingState.GameplayMouseOwned || wasCaptured != window.IsMouseCaptured();
  pollingState.MouseEligiblePolls += gameplayMouseEligible ? 1U : 0U;
  pollingState.MouseReleasedPolls += window.IsMouseCaptureReleased() ? 1U : 0U;
  pollingState.MouseHostUiPolls += hostGuiVisible ? 1U : 0U;
  pollingState.MouseNativeUiPolls += nativeFrontendTouchEnabled ? 1U : 0U;
  pollingState.MouseCaptureTransitions += mouseCaptureChanged ? 1U : 0U;
  pollingState.GameplayMouseOwned = gameplayMouseOwned;
  // Fingers on the game surface (a touch overlay keeps its own controls'
  // fingers): with a touch overlay, they drag the free camera during gameplay.
  std::vector<ThreeDsRecomp::Input::SurfaceFinger> fingers;
  const float windowWidth = static_cast<float>(window.GetWidth());
  const float windowHeight = static_cast<float>(window.GetHeight());
  for (int device = 0; device < SDL_GetNumTouchDevices(); ++device) {
    const SDL_TouchID touchId = SDL_GetTouchDevice(device);
    for (int index = 0; index < SDL_GetNumTouchFingers(touchId); ++index) {
      if (const SDL_Finger *finger = SDL_GetTouchFinger(touchId, index))
        fingers.push_back({touchId, finger->id, finger->x * windowWidth, finger->y * windowHeight});
    }
  }
  const bool touchCameraGameplay = !hostGuiVisible && !nativeFrontendTouchEnabled && topScreenUiProfile &&
                                   topScreenFreeCameraEnabled &&
                                   ThreeDsRecomp::Input::AnyTouchAnchored(
                                       ThreeDsRecomp::Input::OverlayTouchAnchors().Snapshot());
  const double touchSeconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  // A drag starts past 2% of the window's short side; a tap stays within it.
  const float touchSlop = 0.02F * std::min(windowWidth, windowHeight);
  const auto touchCamera = pollingState.TouchCamera.Update(touchCameraGameplay, fingers, touchSeconds, touchSlop);
  pollingState.GameplayTouchLookOwned = touchCamera.Active;
  pollingState.TouchCameraReleaseRequested |= touchCamera.DoubleTap;
  // SDL relative motion is an accumulator. Drain it even while ImGui owns the
  // pointer so menu movement and capture warps cannot leak into gameplay.
  const auto mouseDelta = window.GetMouseDelta();
  pollingState.MouseMovementPolls += mouseDelta.x != 0 || mouseDelta.y != 0 ? 1U : 0U;
  pollingState.DevMouseDeltaX += mouseDelta.x;
  pollingState.DevMouseDeltaY += mouseDelta.y;
  pollingState.DevMouseLookHeld = !hostGuiVisible && (window.GetMouseState(Ship::LUS_MOUSE_BTN_LEFT) ||
                                                      window.GetMouseState(Ship::LUS_MOUSE_BTN_RIGHT));
  {
    SDL_GameController *pad = selectedController.Controller;
    const auto axis = [&](SDL_GameControllerAxis which, float sign) {
      if (pad == nullptr) return 0.0F;
      const float value = static_cast<float>(SDL_GameControllerGetAxis(pad, which)) / 32767.0F * sign;
      constexpr float kDeadZone = 0.2F;
      return std::abs(value) < kDeadZone ? 0.0F
          : std::clamp((value - std::copysign(kDeadZone, value)) / (1.0F - kDeadZone), -1.0F, 1.0F);
    };
    pollingState.DevPadLeftStick = {axis(SDL_CONTROLLER_AXIS_LEFTX, 1.0F), axis(SDL_CONTROLLER_AXIS_LEFTY, -1.0F)};
    pollingState.DevPadRightStick = {axis(SDL_CONTROLLER_AXIS_RIGHTX, 1.0F), axis(SDL_CONTROLLER_AXIS_RIGHTY, -1.0F)};
    pollingState.DevPadLeftShoulder =
        pad != nullptr && SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) != 0;
    pollingState.DevPadRightShoulder =
        pad != nullptr && SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) != 0;
    // The in-game menu's controls, from the raw devices: the menu itself
    // decides whether they act (it is closed, or F1 owns the input).
    const auto padButton = [&](SDL_GameControllerButton which) {
      return pad != nullptr && SDL_GameControllerGetButton(pad, which) != 0;
    };
    const auto key = [&](Ship::KbScancode scancode) { return window.IsKeyDown(scancode); };
    constexpr float kMenuStickThreshold = 0.5F;
    auto &menu = pollingState.Menu;
    menu.Up = key(Ship::KbScancode::LUS_KB_ARROWKEY_UP) || padButton(SDL_CONTROLLER_BUTTON_DPAD_UP) ||
              pollingState.DevPadLeftStick[1] > kMenuStickThreshold;
    menu.Down = key(Ship::KbScancode::LUS_KB_ARROWKEY_DOWN) || padButton(SDL_CONTROLLER_BUTTON_DPAD_DOWN) ||
                pollingState.DevPadLeftStick[1] < -kMenuStickThreshold;
    menu.Left = key(Ship::KbScancode::LUS_KB_ARROWKEY_LEFT) || padButton(SDL_CONTROLLER_BUTTON_DPAD_LEFT) ||
                pollingState.DevPadLeftStick[0] < -kMenuStickThreshold;
    menu.Right = key(Ship::KbScancode::LUS_KB_ARROWKEY_RIGHT) || padButton(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) ||
                 pollingState.DevPadLeftStick[0] > kMenuStickThreshold;
    menu.Confirm = key(Ship::KbScancode::LUS_KB_ENTER) || key(Ship::KbScancode::LUS_KB_SPACE) ||
                   padButton(SDL_CONTROLLER_BUTTON_A);
    menu.Back = key(Ship::KbScancode::LUS_KB_BACKSPACE) || padButton(SDL_CONTROLLER_BUTTON_B);
    menu.TabPrevious = key(Ship::KbScancode::LUS_KB_Q) || padButton(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    menu.TabNext = key(Ship::KbScancode::LUS_KB_E) || padButton(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    const auto chord = pollingState.MenuChord.Update(padButton(SDL_CONTROLLER_BUTTON_LEFTSTICK),
                                                     padButton(SDL_CONTROLLER_BUTTON_RIGHTSTICK));
    auto &touchPad = ThreeDsRecomp::Input::TouchOverlayPad();
    pollingState.SettingsToggleRequested = chord.ToggleSettings || touchPad.TakeSettingsRequest();
    // The menu reacts to a rising edge: the release pulse lasts one poll.
    menu.Toggle = key(Ship::KbScancode::LUS_KB_ESCAPE) || chord.ToggleMenu || hostPresses.Back.Resolve(false, true) ||
                  touchPad.TakeMenuRequest();
    // The on-screen buttons drive the menu like a pad's.
    const auto touchHeld = [&](ThreeDsRecomp::Input::DigitalControl which) {
      return (touchControls.Held & VirtualPad::Bit(which)) != 0U;
    };
    using ThreeDsRecomp::Input::DigitalControl;
    menu.Up = menu.Up || touchHeld(DigitalControl::DpadUp);
    menu.Down = menu.Down || touchHeld(DigitalControl::DpadDown);
    menu.Left = menu.Left || touchHeld(DigitalControl::DpadLeft);
    menu.Right = menu.Right || touchHeld(DigitalControl::DpadRight);
    menu.Confirm = menu.Confirm || touchHeld(DigitalControl::A);
    menu.Back = menu.Back || touchHeld(DigitalControl::B);
    menu.TabPrevious = menu.TabPrevious || touchHeld(DigitalControl::L);
    menu.TabNext = menu.TabNext || touchHeld(DigitalControl::R);
    // The mouse for the menu: its window position with the presentation
    // picture the menu is drawn in, the left button, and the wheel while a
    // host surface is up (gameplay never sees it then). The window's wheel
    // is an accumulator cleared by the read, and this poll is its only
    // reader in the native game: the legacy WheelHandler is never updated
    // here (its coordinates are not even initialized), so it must not be
    // read.
    const auto pointer = window.GetMousePos();
    menu.PointerPresent = !window.IsMouseCaptured();
    menu.PointerX = static_cast<float>(pointer.x);
    menu.PointerY = static_cast<float>(pointer.y);
    menu.PictureX = picture.X;
    menu.PictureY = picture.Y;
    menu.PictureWidth = picture.Width;
    menu.PictureHeight = picture.Height;
    menu.PointerPressed = hostPresses.HostMenu.Resolve(window.GetMouseState(Ship::LUS_MOUSE_BTN_LEFT), true);
    menu.PointerWheel = 0.0F;
    if (hostGuiVisible) {
      const float wheel = window.GetMouseWheel().y;
      menu.PointerWheel = std::isfinite(wheel) ? std::clamp(wheel, -8.0F, 8.0F) : 0.0F;
    }
    // The device used last, for the button prompts (Input owns the choice).
    // Touch comes last: a finger also moves the system's pointer.
    using Activity = ThreeDsRecomp::Input::InputActivity;
    if (ThreeDsRecomp::Input::IsSdlKeyboardActive()) controls.ObserveInputActivity(Activity::Keyboard, touchSeconds);
    const bool mouseMoved = !mouseCaptureChanged && std::abs(mouseDelta.x) + std::abs(mouseDelta.y) >= 3;
    if (mouseMoved || window.GetMouseState(Ship::LUS_MOUSE_BTN_LEFT) ||
        window.GetMouseState(Ship::LUS_MOUSE_BTN_RIGHT) || window.GetMouseState(Ship::LUS_MOUSE_BTN_MIDDLE))
      controls.ObserveInputActivity(Activity::Mouse, touchSeconds);
    if (ThreeDsRecomp::Input::IsSdlControllerActive(pad)) controls.ObserveInputActivity(Activity::Controller, touchSeconds);
    if (!fingers.empty() || touchControls.Held != 0U || touchControls.Pressed != 0U)
      controls.ObserveInputActivity(Activity::Touch, touchSeconds);
  }
  const bool mouseLook = gameplayMouseOwned && !mouseCaptureChanged;
  if (mouseLook || touchCamera.Active) {
    if (mouseLook) {
      pollingState.PendingMouseDeltaX += mouseDelta.x;
      pollingState.PendingMouseDeltaY += mouseDelta.y;
    }
    // A drag is a mouse look of the same window distance.
    pollingState.PendingMouseDeltaX += std::lround(touchCamera.DeltaX);
    pollingState.PendingMouseDeltaY += std::lround(touchCamera.DeltaY);
    if (guestRefreshWillConsume) {
      host.MouseDeltaX = static_cast<int32_t>(std::clamp<int64_t>(
          pollingState.PendingMouseDeltaX, INT32_MIN, INT32_MAX));
      host.MouseDeltaY = static_cast<int32_t>(std::clamp<int64_t>(
          pollingState.PendingMouseDeltaY, INT32_MIN, INT32_MAX));
      pollingState.PendingMouseDeltaX = 0;
      pollingState.PendingMouseDeltaY = 0;
    }
  } else {
    pollingState.PendingMouseDeltaX = 0;
    pollingState.PendingMouseDeltaY = 0;
  }

  auto frame = MapNativeControlInput(config, host, aimTransform,
                                     &pollingState.RightStickProfile,
                                     guestRefreshWillConsume,
                                     &pollingState.VirtualMotion);
  if (nativeFrontendTouchEnabled && !hostGuiVisible) {
    if (window.IsMouseCaptured()) {
      window.SetMouseCapture(false);
    }
    window.SetCursorVisibility(true);
    const auto touch = mapPointerToTouch(
        hostPresses.NativeTouch.Resolve(window.GetMouseState(Ship::LUS_MOUSE_BTN_LEFT), guestRefreshWillConsume));
    frame.Hid.TouchX = touch.X;
    frame.Hid.TouchY = touch.Y;
    frame.Hid.TouchPressed = touch.Pressed;
    if (!frame.Hid.TouchPressed) {
      const auto pad = ThreeDsRecomp::Input::MapNormalizedTouch(host.ControllerTouch);
      if (pad.Pressed) {
        frame.Hid.TouchX = pad.X;
        frame.Hid.TouchY = pad.Y;
        frame.Hid.TouchPressed = true;
      }
    }
  } else {
    // A gameplay tap must not touch the next touch-screen page.
    hostPresses.NativeTouch.Reset();
  }
  ApplyNativeControlShortcutTouch(host, frame);
  return frame;
}

Oot3dNativeGame::NativeA32InputFrame ResolveNativeA32GuestInput(
    const Oot3dNativeGame::NativeA32InputFrame &physical,
    const Oot3dNativeGame::NativeA32InputTimeline &timeline,
    uint32_t guestFrameIndex, uint32_t runFrameIndex,
    bool nativeFrontendTouchEnabled,
    bool topScreenStartRoutingEligible, bool topScreenUiProfile,
    Oot3dNativeGame::NativeA32PolledButtonLatch &startButtonLatch,
    Oot3dNativeGame::TopScreenStartRoutingState &startRoutingState) {
  auto resolved = physical;
  if (timeline.Enabled()) {
    resolved = timeline.Sample(guestFrameIndex, runFrameIndex);
    resolved.Exit = physical.Exit;
  } else {
    const uint32_t start = Oot3dNativeGame::NativeA32HidButtonMask(
        Oot3dNativeGame::NativeA32HidButton::Start);
    const bool physicalStartHeld = (resolved.Hid.Buttons & start) != 0U;
    if (Oot3dNativeGame::ResolveNativeA32PolledButton(
            physicalStartHeld, startButtonLatch)) {
      resolved.Hid.Buttons |= start;
    } else {
      resolved.Hid.Buttons &= ~start;
    }
  }
  Oot3dNativeGame::ApplyTopScreenStartRouting(
      resolved, topScreenUiProfile, topScreenStartRoutingEligible,
      nativeFrontendTouchEnabled, true, startRoutingState);
  return resolved;
}

bool ResolveNativeA32TouchInputEnabled(
    Oot3dNativeGame::NativeA32Memory &memory,
    const Oot3dNativeGame::Oot3dNativeUiLifecycleBridge &uiLifecycleBridge,
    bool topScreenUiProfile) {
  bool nativePausePageActive = false;
  if (topScreenUiProfile) {
    // The relocated lower-screen pages that take touches on the upper
    // screen: the pause pages, the Visions lists and the Game Over choices.
    Oot3dNativeGame::TopScreenPauseDrawInputs pauseInputs;
    bool gameOverChoice = false;
    nativePausePageActive =
        Oot3dNativeGame::ReadTopScreenPauseDrawInputs(memory, &pauseInputs) &&
        (pauseInputs.AnyPageGateActive ||
         Oot3dNativeGame::IsTopScreenVisionsListPresentation(pauseInputs) ||
         (Oot3dNativeGame::ReadTopScreenGameOverChoiceVisible(memory, pauseInputs, &gameOverChoice) &&
          gameOverChoice));
  }
  return Oot3dNativeGame::ResolveNativeA32TouchInputEnabled(
      topScreenUiProfile,
      uiLifecycleBridge.NativeTouchPresentationActive(),
      // A touch on the title also reveals the withheld version choice.
      uiLifecycleBridge.NativeFrontendPresentationActive() ||
          uiLifecycleBridge.NativeVersionChoiceAwaitingReveal(),
      nativePausePageActive);
}

} // namespace Oot3dA32Window
