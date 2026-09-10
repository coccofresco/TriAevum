#include "three_ds_input.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class TestHostButtonSource final
    : public ThreeDsRecomp::Input::HostButtonSource {
  public:
    ThreeDsRecomp::Input::KeyboardKey Keyboard =
        ThreeDsRecomp::Input::KeyboardKey::None;
    ThreeDsRecomp::Input::MouseButton Mouse =
        ThreeDsRecomp::Input::MouseButton::None;
    ThreeDsRecomp::Input::GamepadButton Gamepad =
        ThreeDsRecomp::Input::GamepadButton::None;

    bool IsKeyboardKeyHeld(
        ThreeDsRecomp::Input::KeyboardKey key) const noexcept override {
        return key == Keyboard;
    }

    bool IsMouseButtonHeld(
        ThreeDsRecomp::Input::MouseButton button) const noexcept override {
        return button == Mouse;
    }

    bool IsGamepadButtonHeld(
        ThreeDsRecomp::Input::GamepadButton button) const noexcept override {
        return button == Gamepad;
    }
};

void TestVirtualMotion() {
    using namespace ThreeDsRecomp::Input;
    constexpr double pi = 3.14159265358979323846;
    MappingConfig config;
    config.NativeMotionSource = MotionSource::Mouse;
    config.MouseMotionDegreesPerPixel = 0.25F;
    VirtualMotionState state;
    PhysicalInputState input;
    input.MouseDeltaY = 160;
    input.SamplePeriodSeconds = 1.0 / 60.0;
    const auto tilted = ResolveInput(config, input, {}, {}, nullptr, true, &state);
    Require(std::abs(state.PitchRadians - 40.0 * pi / 180.0) < 1e-6,
            "mouse displacement did not integrate into device tilt");
    Require(tilted.Hid.Accelerometer[2] > 0.64F && tilted.Hid.Accelerometer[1] > -0.77F,
            "virtual pitch still supplies neutral gravity to native sensor fusion");
    input.MouseDeltaY = 0;
    for (int i = 0; i < 600; ++i) {
        const auto stopped = ResolveInput(config, input, {}, {}, nullptr, true, &state);
        Require(stopped.Hid.GyroscopeDegreesPerSecond == std::array<float, 3>{} &&
                    stopped.Hid.Accelerometer == tilted.Hid.Accelerometer,
                "stationary mouse recentered gravity or generated a reverse rotation");
    }
    input.MouseDeltaX = -4;
    const auto yaw = ResolveInput(config, input, {}, {}, nullptr, true, &state);
    Require(std::abs(yaw.Hid.GyroscopeDegreesPerSecond[1] - 60.0F * std::cos(state.PitchRadians)) < 1e-4 &&
                std::abs(yaw.Hid.GyroscopeDegreesPerSecond[2] + 60.0F * std::sin(state.PitchRadians)) < 1e-4 &&
                yaw.Hid.Accelerometer == tilted.Hid.Accelerometer,
            "horizontal mouse motion rolls the device instead of yawing about gravity");
    VirtualMotionState neutral;
    const auto horizontal = ResolveInput(config, input, {}, {}, nullptr, true, &neutral);
    Require(std::abs(horizontal.Hid.GyroscopeDegreesPerSecond[1] - 60.0F) < 1e-4F &&
                horizontal.Hid.GyroscopeDegreesPerSecond[2] == 0.0F,
            "neutral horizontal mouse motion uses the roll axis");
    input.MouseDeltaX = 0;
    input.MouseDeltaY = 20;
    const auto beforePresentation = state.PitchRadians;
    (void)ResolveInput(config, input, {}, {}, nullptr, false, &state);
    Require(state.PitchRadians == beforePresentation, "interpolated presentation advanced virtual motion");
    for (const int hz : {30, 60, 90, 120}) {
        VirtualMotionState cadence;
        input.MouseDeltaY = 360 / hz;
        input.SamplePeriodSeconds = 1.0 / hz;
        for (int i = 0; i < hz; ++i)
            (void)ResolveInput(config, input, {}, {}, nullptr, true, &cadence);
        Require(std::abs(cadence.PitchRadians - pi / 2.0) < 1e-6,
                "mouse sensitivity changes with polling cadence");
    }
    VirtualMotionState restored;
    restored.RestoreGravity(tilted.Hid.Accelerometer);
    Require(std::abs(restored.PitchRadians - state.PitchRadians) < 1e-6,
            "restoring sensor gravity lost virtual tilt");
    restored.RestoreGravity({0.0F, 0.0F, 0.0F});
    Require(restored.PitchRadians == 0.0, "missing savestate gravity inverted the virtual device");
    input = {};
    input.MouseDeltaX = 4;
    input.MouseDeltaY = -4;
    const auto upRight = ResolveInput(config, input, {});
    Require(upRight.Hid.GyroscopeDegreesPerSecond[0] < 0.0F &&
                upRight.Hid.GyroscopeDegreesPerSecond[1] < 0.0F,
            "mouse up/right has opposite signs to native analog aim");
    config.NativeMotionInvertX = true;
    config.NativeMotionInvertY = true;
    const auto inverted = ResolveInput(config, input, {});
    Require(inverted.Hid.GyroscopeDegreesPerSecond[0] > 0.0F &&
                inverted.Hid.GyroscopeDegreesPerSecond[1] > 0.0F,
            "virtual mouse inversion was not applied once per axis");
    config.NativeMotionInvertX = false;
    config.NativeMotionInvertY = false;
    input = {};
    config.NativeMotionSource = MotionSource::Automatic;
    const auto idle = ResolveInput(config, input, {}, {}, nullptr, true, &state);
    Require(idle.Hid.Accelerometer == tilted.Hid.Accelerometer,
            "automatic source dropped virtual gravity as soon as mouse stopped");
    config.NativeMotionSource = MotionSource::ControllerMotion;
    input.ControllerMotion.GyroscopeValid = true;
    input.ControllerMotion.AccelerometerValid = true;
    input.ControllerMotion.GyroscopeDegreesPerSecond = {10, 20, 30};
    input.ControllerMotion.Accelerometer = {0.1F, -0.9F, 0.2F};
    const auto physical = ResolveInput(config, input, {}, {}, nullptr, true, &state);
    Require(physical.Hid.GyroscopeDegreesPerSecond == input.ControllerMotion.GyroscopeDegreesPerSecond &&
                physical.Hid.Accelerometer == input.ControllerMotion.Accelerometer,
            "virtual motion changed physical controller sensors");
}

} // namespace

int main() try {
    using namespace ThreeDsRecomp::Input;
    TestVirtualMotion();

    HostBindingCapture capture;
    TestHostButtonSource raw;
    capture.Begin(BindingDevice::Keyboard);
    raw.Keyboard = KeyboardKey::Enter;
    capture.Observe(raw, false);
    Require(capture.Snapshot().Phase == BindingCapturePhase::Release, "opening key was assigned");
    raw.Keyboard = KeyboardKey::None;
    capture.Observe(raw, false);
    Require(capture.Snapshot().Phase == BindingCapturePhase::Listening, "capture did not arm after release");
    raw.Mouse = MouseButton::Left;
    capture.Observe(raw, false);
    Require(capture.Active(), "wrong device completed keyboard capture");
    raw.Keyboard = KeyboardKey::RightShift;
    capture.Observe(raw, false);
    Require(capture.Snapshot().Phase == BindingCapturePhase::Complete &&
            capture.Snapshot().Binding.KeyboardPrimary == KeyboardKey::RightShift, "keyboard capture failed");
    raw.Keyboard = KeyboardKey::W;
    capture.Observe(raw, false);
    Require(capture.Snapshot().Binding.KeyboardPrimary == KeyboardKey::RightShift, "completed capture was overwritten");
    capture.Begin(BindingDevice::Mouse);
    capture.Observe(raw, false);
    Require(capture.Snapshot().Phase == BindingCapturePhase::Release, "Listen click was assigned to mouse");
    raw.Mouse = MouseButton::None;
    capture.Observe(raw, false);
    raw.Mouse = MouseButton::Forward;
    capture.Observe(raw, false);
    Require(capture.Snapshot().Binding.Mouse == MouseButton::Forward, "mouse side-button capture failed");
    capture.Begin(BindingDevice::Gamepad);
    raw.Gamepad = GamepadButton::RightTrigger;
    capture.Observe(raw, false);
    Require(capture.Snapshot().Phase == BindingCapturePhase::Release, "held trigger was assigned");
    raw.Gamepad = GamepadButton::None;
    capture.Observe(raw, false);
    raw.Gamepad = GamepadButton::LeftTrigger;
    capture.Observe(raw, false);
    Require(capture.Snapshot().Binding.Gamepad == GamepadButton::LeftTrigger, "controller trigger capture failed");
    capture.Begin(BindingDevice::Keyboard);
    capture.Observe(raw, true);
    Require(capture.Snapshot().Phase == BindingCapturePhase::Cancelled && !capture.Active(), "Escape did not cancel capture");

    const auto old3ds = CapabilitiesFor(HardwareProfile::Old3ds);
    const auto circlePadPro =
        CapabilitiesFor(HardwareProfile::Old3dsCirclePadPro);
    const auto new3ds = CapabilitiesFor(HardwareProfile::New3ds);
    Require(!old3ds.CStick &&
                (old3ds.ButtonMask & kExtraHidButtonMask) == 0U &&
                circlePadPro.CStick && new3ds.CStick &&
                (new3ds.ButtonMask & kExtraHidButtonMask) ==
                    kExtraHidButtonMask,
            "3DS hardware-profile capabilities are inconsistent");
    HardwareProfile parsedHardware{};
    Require(ParseHardwareProfile(HardwareProfileName(
                                     HardwareProfile::New3ds),
                                 &parsedHardware) &&
                parsedHardware == HardwareProfile::New3ds,
            "hardware-profile names are not reusable round trips");

    std::size_t old3dsSupported = 0;
    std::size_t extendedSupported = 0;
    MappingConfig digitalMapping;
    digitalMapping.CirclePadSource = AnalogStick::Disabled;
    digitalMapping.CStickSource = MotionSource::DigitalLook;
    digitalMapping.NativeMotionSource = MotionSource::Disabled;
    for (std::size_t index = 0; index < kDigitalControlCount; ++index) {
        const auto control = static_cast<DigitalControl>(index);
        const auto route = NativeRouteFor(control);
        Require(route.Channel != NativeChannelKind::None,
                "an abstract digital control has no native 3DS route");
        DigitalControl parsedControl{};
        Require(ParseDigitalControl(DigitalControlName(control),
                                    &parsedControl) &&
                    parsedControl == control,
                "a native digital-control name does not round trip");
        old3dsSupported += IsDigitalControlSupported(
                              HardwareProfile::Old3ds, control)
                              ? 1U
                              : 0U;
        extendedSupported += IsDigitalControlSupported(
                                  HardwareProfile::New3ds, control)
                                  ? 1U
                                  : 0U;

        DigitalState singleControl;
        singleControl.SetHeld(control);
        const auto frame = ResolveInput(
            digitalMapping, {}, singleControl);
        const auto expectedAxis = static_cast<std::int16_t>(
            static_cast<std::int32_t>(route.AxisDirection) *
            static_cast<std::int32_t>(kNativeStickMaximum));
        switch (route.Channel) {
        case NativeChannelKind::StandardHidButton:
        case NativeChannelKind::ExtraHidButton:
            Require(IsButtonHeld(frame, route.ButtonValue),
                    "a digital button did not reach its native HID bit");
            break;
        case NativeChannelKind::CirclePadX:
            Require(frame.Hid.CirclePadX == expectedAxis,
                    "a digital control did not reach native Circle Pad X");
            break;
        case NativeChannelKind::CirclePadY:
            Require(frame.Hid.CirclePadY == expectedAxis,
                    "a digital control did not reach native Circle Pad Y");
            break;
        case NativeChannelKind::CStickX:
            Require(frame.CStick.X == expectedAxis,
                    "a digital control did not reach native C-Stick X");
            break;
        case NativeChannelKind::CStickY:
            Require(frame.CStick.Y == expectedAxis,
                    "a digital control did not reach native C-Stick Y");
            break;
        case NativeChannelKind::None:
            Require(false, "unreachable unmapped native channel");
            break;
        }
    }
    Require(old3dsSupported == 16U &&
                extendedSupported == kDigitalControlCount,
            "hardware variants expose an inconsistent native control set");

    ControlProfile parsedProfile{};
    KeyboardKey parsedKey{};
    MouseButton parsedMouse{};
    GamepadButton parsedGamepad{};
    AnalogStick parsedStick{};
    MotionSource parsedMotion{};
    Require(ParseControlProfile("keyboard_mouse", &parsedProfile) &&
                parsedProfile == ControlProfile::KeyboardMouse &&
                ParseKeyboardKey("arrow_up", &parsedKey) &&
                parsedKey == KeyboardKey::ArrowUp &&
                ParseMouseButton("forward", &parsedMouse) &&
                parsedMouse == MouseButton::Forward &&
                ParseGamepadButton("right_trigger", &parsedGamepad) &&
                parsedGamepad == GamepadButton::RightTrigger &&
                ParseAnalogStick("right", &parsedStick) &&
                parsedStick == AnalogStick::Right &&
                ParseMotionSource("controller_motion", &parsedMotion) &&
                parsedMotion == MotionSource::ControllerMotion,
            "cross-title host binding names do not round trip");

    TestHostButtonSource hostButtons;
    hostButtons.Keyboard = KeyboardKey::W;
    hostButtons.Mouse = MouseButton::Left;
    hostButtons.Gamepad = GamepadButton::A;
    const HostBinding multiDeviceBinding{
        KeyboardKey::W, KeyboardKey::ArrowUp,
        MouseButton::Left, GamepadButton::A};
    Require(IsHostBindingHeld(multiDeviceBinding, {}, hostButtons) &&
                !IsHostBindingHeld(
                    multiDeviceBinding,
                    {.Keyboard = false, .Mouse = false, .Gamepad = false},
                    hostButtons) &&
                IsHostBindingHeld(
                    multiDeviceBinding,
                    {.Keyboard = false, .Mouse = true, .Gamepad = false},
                    hostButtons),
            "host bindings bypass device enablement or duplicate resolution");

    std::array<HostBinding, 5> swapBindings{};
    swapBindings[0] = multiDeviceBinding;
    swapBindings[0].Gamepad = GamepadButton::LeftShoulder;
    swapBindings[1].Gamepad = GamepadButton::LeftTrigger;
    swapBindings[2].Gamepad = GamepadButton::LeftShoulder;
    swapBindings[3].Gamepad = GamepadButton::RightTrigger;
    const auto originalBindings = swapBindings;
    SwapGamepadSources(swapBindings, GamepadButton::LeftShoulder,
                       GamepadButton::LeftTrigger);
    Require(swapBindings[0].Gamepad == GamepadButton::LeftTrigger &&
                swapBindings[1].Gamepad == GamepadButton::LeftShoulder &&
                swapBindings[2].Gamepad == GamepadButton::LeftTrigger &&
                swapBindings[3] == originalBindings[3] &&
                swapBindings[4] == originalBindings[4] &&
                swapBindings[0].KeyboardPrimary == originalBindings[0].KeyboardPrimary &&
                swapBindings[0].Mouse == originalBindings[0].Mouse,
            "source swap lost a custom binding or changed another device");
    SwapGamepadSources(swapBindings, GamepadButton::LeftShoulder,
                       GamepadButton::LeftTrigger);
    SwapGamepadSources(swapBindings, GamepadButton::None, GamepadButton::A);
    SwapGamepadSources(swapBindings, GamepadButton::A, GamepadButton::None);
    SwapGamepadSources(swapBindings, GamepadButton::A, GamepadButton::A);
    Require(swapBindings == originalBindings,
            "source swap is not reversible or assigned unbound actions");

    DigitalState digital;
    digital.SetHeld(DigitalControl::CirclePadUp);
    digital.SetHeld(DigitalControl::A);
    digital.SetHeld(DigitalControl::Zl);
    digital.SetHeld(DigitalControl::CStickLeft);
    MappingConfig mapping;
    mapping.CirclePadSource = AnalogStick::Disabled;
    mapping.CStickSource = MotionSource::DigitalLook;
    mapping.NativeMotionSource = MotionSource::DigitalLook;
    const auto native = ResolveInput(mapping, {}, digital);
    Require(native.Hid.CirclePadY == kNativeStickMaximum &&
                IsButtonHeld(native, Button::A) &&
                IsButtonHeld(native, Button::Zl) &&
                native.CStick.X == -kNativeStickMaximum &&
                native.CStick.Kind == AxisInputKind::Absolute &&
                native.Hid.GyroscopeValid,
            "digital controls did not resolve exclusively to native 3DS channels");
    const auto standardProjection = ProjectStandardHid(native);
    const auto unsupportedExtra = ProjectExtraHid(
        native, HardwareProfile::Old3ds);
    const auto extendedProjection = ProjectExtraHid(
        native, HardwareProfile::New3ds);
    Require((standardProjection.Buttons & ButtonMask(Button::A)) != 0U &&
                (standardProjection.Buttons & kExtraHidButtonMask) == 0U &&
                unsupportedExtra.Buttons == 0U &&
                unsupportedExtra.CStickX == 0 &&
                extendedProjection.Buttons == ButtonMask(Button::Zl) &&
                extendedProjection.CStickX == -kNativeStickMaximum,
            "standard and Extra HID projections duplicate or lose channels");

    PhysicalInputState mouse;
    mouse.MouseDeltaX = 5;
    mouse.MouseDeltaY = -2;
    mapping.CStickSource = MotionSource::Mouse;
    mapping.MouseCStickUnitsPerPixel = 4.0F;
    const auto mouseFrame = ResolveInput(mapping, mouse, {});
    Require(mouseFrame.CStick.X == 20 && mouseFrame.CStick.Y == 8 &&
                mouseFrame.CStick.Kind == AxisInputKind::Relative,
            "mouse did not map to the canonical C-Stick channel");

    AxisInputAccumulator accumulator;
    accumulator.Observe(mouseFrame.CStick);
    accumulator.Observe({4, -3, AxisInputKind::Relative});
    const auto accumulated = accumulator.Consume();
    Require(accumulated.X == 24 && accumulated.Y == 5 &&
                accumulator.Peek().X == 0 &&
                accumulator.RelativeSamplesObserved() == 2U &&
                accumulator.RelativeSamplesConsumed() == 1U,
            "relative canonical C-Stick samples were not retained");

    Require(ResolveGameplayPointerOwnership(false, false, true, true) &&
                !ResolveGameplayPointerOwnership(true, false, true, true) &&
                !ResolveGameplayPointerOwnership(false, true, true, true),
            "pointer ownership ignored native touch or host GUI ownership");

    const auto touch = MapPresentationPointToTouch(
        200.0F, 120.0F, 400.0F, 240.0F, true, true);
    const auto outsideTouch = MapPresentationPointToTouch(
        0.0F, 0.0F, 400.0F, 240.0F, false, true);
    Require(touch.Inside && touch.Pressed && touch.X == 160U &&
                touch.Y == 120U && !outsideTouch.Inside &&
                !outsideTouch.Pressed,
            "presentation pointer did not resolve to native 3DS touch");

    Require(ConvertHostAxisToNative(7000, 25) == 0 &&
                ConvertHostAxisToNative(-7000, 25) == 0 &&
                ConvertHostAxisToNative(32767, 25) ==
                    kNativeStickMaximum &&
                ConvertHostAxisToNative(-32768, 25) ==
                    -kNativeStickMaximum,
            "host analog conversion does not preserve the native range");

    return 0;
} catch (const std::exception& error) {
    std::cerr << "three_ds_recomp_input_tests: " << error.what() << '\n';
    return 1;
}
