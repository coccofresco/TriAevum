#include "three_ds_input.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
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

bool NearVector(const std::array<float, 3>& a, const std::array<float, 3>& b, float tolerance = 1e-5F) {
    for (int i = 0; i < 3; ++i) if (std::abs(a[i] - b[i]) > tolerance) return false;
    return true;
}

void TestAutomaticMotionComposition() {
    using namespace ThreeDsRecomp::Input;
    MappingConfig config;
    config.NativeMotionSource = MotionSource::Automatic;
    config.CStickSource = MotionSource::Automatic;
    VirtualMotionState state;
    PhysicalInputState input;
    input.ControllerMotion.GyroscopeValid = input.ControllerMotion.AccelerometerValid = true;
    input.ControllerMotion.Accelerometer = {0, -1, 0};
    input.ControllerMotion.GyroscopeDegreesPerSecond = {12, 0, 0};
    input.RightStickY = 20000;
    const auto mixed = ResolveInput(config, input, {}, {}, nullptr, true, &state);
    auto justStickInput = input;
    justStickInput.ControllerMotion.GyroscopeDegreesPerSecond = {};
    VirtualMotionState justStickState;
    const auto justStick = ResolveInput(config, justStickInput, {}, {}, nullptr, true, &justStickState);
    Require(std::abs(mixed.Hid.GyroscopeDegreesPerSecond[0] - justStick.Hid.GyroscopeDegreesPerSecond[0] - 12) < 1e-4F,
            "stick swallowed gyro instead of composing rates");
    input.RightStickY = 0;
    input.ControllerMotion.GyroscopeDegreesPerSecond = {};
    for (int i = 0; i < 300; ++i) {
        const auto idle = ResolveInput(config, input, {}, {}, nullptr, true, &state);
        Require(NearVector(idle.Hid.Accelerometer, mixed.Hid.Accelerometer) &&
                idle.Hid.GyroscopeDegreesPerSecond == std::array<float, 3>{},
                "stick release reverted to unrelated physical gravity");
    }
    input.MouseDeltaY = 7;
    ResolveInput(config, input, {}, {}, nullptr, true, &state);
    input.MouseDeltaY = 0;
    const auto mouseGravity = state.Composition.LastGravity;
    for (int i = 0; i < 300; ++i) {
        input.ControllerMotion.GyroscopeDegreesPerSecond = {3, -8, 2};
        input.ControllerMotion.Accelerometer = {0.4F, -0.8F, 0.3F};
        const auto idle = ResolveInput(config, input, {}, {}, nullptr, true, &state);
        Require(state.AutomaticOwner == VirtualMotionState::Owner::Mouse &&
                NearVector(idle.Hid.Accelerometer, mouseGravity) &&
                idle.Hid.GyroscopeDegreesPerSecond == std::array<float, 3>{},
                "idle mouse lost ownership to sensors");
    }
    input.ControllerMotion.Accelerometer = {0.6F, -0.8F, 0};
    input.ControllerMotion.GyroscopeDegreesPerSecond = {};
    input.ControllerButtons = 1;
    const auto handoff = ResolveInput(config, input, {}, {}, nullptr, true, &state);
    Require(state.AutomaticOwner == VirtualMotionState::Owner::Controller &&
            NearVector(handoff.Hid.Accelerometer, mouseGravity), "device handoff snapped gravity");
    input.MouseDeltaX = 4;
    ResolveInput(config, input, {}, {}, nullptr, true, &state);
    input.MouseDeltaX = 0;
    ResolveInput(config, input, {}, {}, nullptr, true, &state);
    Require(state.AutomaticOwner == VirtualMotionState::Owner::Mouse, "held button repeatedly reclaimed aim");
    input.ControllerButtons = 0;
    input.RightStickX = 100;
    ResolveInput(config, input, {}, {}, nullptr, true, &state);
    Require(state.AutomaticOwner == VirtualMotionState::Owner::Mouse, "stick deadzone noise reclaimed aim");
    input.ControllerPressed = 1;
    ResolveInput(config, input, {}, {}, nullptr, true, &state);
    Require(state.AutomaticOwner == VirtualMotionState::Owner::Controller,
            "buffered short controller tap did not reclaim aim");
    input.ControllerPressed = 0;
    input.MouseDeltaX = 2;
    ResolveInput(config, input, {}, {}, nullptr, true, &state);
    input.MouseDeltaX = 0;
    input.RightStickX = 20000;
    const auto before = state.Composition;
    ResolveInput(config, input, {}, {}, nullptr, false, &state);
    Require(state.Composition.SensorToVirtual == before.SensorToVirtual &&
            state.AutomaticOwner == VirtualMotionState::Owner::Mouse,
            "presentation-only frame advanced motion state");
    ResolveInput(config, input, {}, {}, nullptr, true, &state);
    Require(state.AutomaticOwner == VirtualMotionState::Owner::Controller, "stick did not reclaim aim");
    input.RightStickX = 0;
    const auto beforeDetach = state.Composition.LastGravity;
    state.ResetController();
    input.ControllerMotion = {};
    const auto detached = ResolveInput(config, input, {}, {}, nullptr, true, &state);
    Require(NearVector(detached.Hid.Accelerometer, beforeDetach), "disconnect snapped aim");

    for (const int hz : {30, 60, 90, 120}) {
        MotionCompositionState composition;
        ComposedMotion sample;
        for (int i = 0; i < hz; ++i)
            sample = composition.Sample({0,-1,0}, true, {}, 60, 0, 1.0/hz, true, true);
        Require(NearVector(sample.Gravity, {0,-0.5F,0.8660254F}), "composition changed speed with cadence");
        const auto rotated = composition.Sample({0,-1,0}, true, {0,60,0}, 0,0,1.0/hz,true,true);
        Require(NearVector(rotated.AngularVelocity, {0,30,-51.961524F}, 1e-4F),
                "physical gyro not transformed with gravity after virtual pitch");
        VirtualMotionState paced;
        PhysicalInputState pad;
        pad.RightStickY = -32767;
        pad.ControllerMotion.AccelerometerValid = pad.ControllerMotion.GyroscopeValid = true;
        double pending = 0;
        for (int i = 0; i < hz; ++i) {
            const bool consume = (i + 1) % (hz / 30) == 0;
            pad.SamplePeriodSeconds = ConsumeInputPeriod(1.0 / hz, consume, pending);
            ResolveInput(config, pad, {}, {}, nullptr, consume, &paced);
        }
        Require(NearVector(paced.Composition.LastGravity, {0,1,0}) && pending == 0,
                "controller integration depends on interpolated presentation frequency");
        MotionCompositionState diagonal;
        for (int i = 0; i < hz; ++i)
            sample = diagonal.Sample({0,-1,0}, true, {}, 60, 100, 1.0/hz, true, true);
        Require(NearVector(sample.Gravity, {0,-0.5F,0.8660254F}),
                "simultaneous yaw corrupted pitch gravity or introduced cadence dependence");
    }
    MotionCompositionState rolled;
    for (int i = 0; i < 120; ++i)
        Require(NearVector(rolled.Sample({0.6F,-0.8F,0}, true, {}, 0,120,1.0/60,true,true).Gravity,
                           {0.6F,-0.8F,0}), "world yaw tilts a rolled controller");
    MotionCompositionState upsideDown;
    upsideDown.RestoreGravity({0,1,0});
    Require(NearVector(upsideDown.Sample({0,-1,0}, true, {}, 0,0,1.0/60,true,true).Gravity, {0,1,0}),
            "opposite gravity handoff is singular");
}

} // namespace

int main() try {
    TestAutomaticMotionComposition();
    {
        using namespace ThreeDsRecomp::Input;
        const auto motion = ConvertSdlMotion({9.80665F, 9.80665F, -9.80665F}, true,
                                             {1, 2, 3}, true);
        Require(motion.Accelerometer == std::array<float, 3>{1, -1, -1} &&
                std::abs(motion.GyroscopeDegreesPerSecond[0] + 57.29578F) < 0.001F &&
                std::abs(motion.GyroscopeDegreesPerSecond[1] - 114.59156F) < 0.001F &&
                std::abs(motion.GyroscopeDegreesPerSecond[2] + 171.88734F) < 0.001F,
                "SDL sensors must match Azahar native 3DS coordinates/units");
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const auto invalid = ConvertSdlMotion({nan, 0, 0}, true, {0, 0, 0}, false);
        Require(!invalid.AccelerometerValid && !invalid.GyroscopeValid, "invalid sensor accepted");
        MappingConfig gyroConfig;
        gyroConfig.CStickSource = MotionSource::ControllerGyroscope;
        gyroConfig.NativeMotionSource = MotionSource::ControllerMotion;
        PhysicalInputState gyroInput;
        gyroInput.ControllerMotion.GyroscopeValid = true;
        gyroInput.ControllerMotion.GyroscopeDegreesPerSecond = {0, -30, 0};
        const auto yaw = ResolveInput(gyroConfig, gyroInput, {});
        Require(yaw.CStick.X == 0 && yaw.CStick.Y == 0, "motion leaked into camera");
        gyroConfig.NativeMotionInvertX = true;
        Require(ResolveInput(gyroConfig, gyroInput, {}).Hid.GyroscopeDegreesPerSecond[1] == 30,
                "native motion horizontal inversion omitted yaw");
        gyroInput.ControllerMotion.GyroscopeDegreesPerSecond = {0, 0, 30};
        Require(ResolveInput(gyroConfig, gyroInput, {}).CStick.X == 0, "roll steers camera yaw");
        gyroInput.ControllerMotion.AccelerometerValid = true;
        gyroInput.ControllerMotion.Accelerometer = {0.5F, -0.8F, 0.4F};
        for (const auto source : {MotionSource::Automatic, MotionSource::ControllerGyroscope,
                MotionSource::ControllerAccelerometer, MotionSource::ControllerMotion}) {
            gyroConfig.CStickSource = source;
            const auto still = ResolveInput(gyroConfig, gyroInput, {});
            Require(still.CStick.X == 0 && still.CStick.Y == 0 && still.Hid.GyroscopeValid &&
                    still.Hid.AccelerometerValid, "camera isolation disabled aim or leaked sensors");
            gyroInput.RightStickX = 16000;
            Require(ResolveInput(gyroConfig, gyroInput, {}).CStick.X > 0,
                    "legacy motion camera did not fall back to stick");
            gyroInput.RightStickX = 0;
        }
        const auto edge = MapNormalizedTouch({1, 1, true});
        const auto center = MapNormalizedTouch({0.5F, 0.5F, true});
        Require(edge.X == 319 && edge.Y == 239 && edge.Pressed &&
                center.X == 160 && center.Y == 120 &&
                !MapNormalizedTouch({nan, 0, true}).Pressed &&
                !MapNormalizedTouch({-0.1F, 0, true}).Pressed &&
                !MapNormalizedTouch({0.5F, 0.5F, false}).Pressed,
                "normalized touch bounds/release");
        MotionCalibrationAccumulator calibration;
        MotionObservation sample;
        sample.GyroscopeValid = sample.AccelerometerValid = true;
        sample.GyroscopeDegreesPerSecond = {1, -2, 3};
        sample.Accelerometer = {0, -1, 0};
        sample.GyroscopeTimestampMicroseconds = sample.AccelerometerTimestampMicroseconds = 1;
        for (int i = 0; i < 120; ++i) Require(!calibration.Observe(sample), "duplicate sensor completed calibration");
        Require(calibration.SamplesCollected() == 1, "duplicate sensor counted repeatedly");
        sample.GyroscopeDegreesPerSecond = {100, 0, 0};
        Require(!calibration.Observe(sample) && calibration.WaitingForStillness() &&
                calibration.SamplesCollected() == 0, "moving controller accepted for calibration");
        sample.GyroscopeDegreesPerSecond = {1, -2, 3};
        for (unsigned i = 1; i <= MotionCalibrationAccumulator::SamplesRequired; ++i) {
            sample.GyroscopeTimestampMicroseconds = sample.AccelerometerTimestampMicroseconds = i + 1;
            Require(calibration.Observe(sample) == (i == MotionCalibrationAccumulator::SamplesRequired),
                    "fresh calibration completion mismatch");
        }
        Require(calibration.Mean().GyroscopeDegreesPerSecond == sample.GyroscopeDegreesPerSecond,
                "calibration bias incorrect");
        calibration.Reset();
        sample.GyroscopeValid = false;
        sample.AccelerometerTimestampMicroseconds = 0;
        for (unsigned i = 1; i <= MotionCalibrationAccumulator::SamplesRequired; ++i)
            Require(calibration.Observe(sample) == (i == MotionCalibrationAccumulator::SamplesRequired),
                    "accelerometer-only timestamp-less calibration");
    }
    {
        using namespace ThreeDsRecomp::Input;
        const std::array<DeviceDescriptor, 3> devices{{
            {7, "pad", "Same model", false, false, "AA:01"},
            {3, "pad", "Same model", false, false, "AA:02"},
            {1, "virtual", "Steam Input", false, false, ""},
        }};
        Require(SelectControllerDevice(devices, "", "") == 1,
                "automatic selection must be independent of enumeration order");
        Require(SelectControllerDevice(devices, "", "", 7) == 7,
                "automatic selection switched an attached active device");
        Require(SelectControllerDevice(devices, "pad", "") == 3 &&
                SelectControllerDevice(devices, "pad", "", 7) == 7,
                "legacy GUID preference is not deterministic and sticky");
        Require(SelectControllerDevice(devices, "pad", "aa-01", 3) == 7,
                "serial preference ignored or not normalized");
        Require(SelectControllerDevice(devices, "pad", "missing", 7) == -1 &&
                SelectControllerDevice(devices, "missing", "", 7) == -1,
                "explicit missing preference silently routes another controller");
        Require(SelectControllerDevice({}, "", "", 7) == -1 &&
                SelectControllerDevice(devices, "", "", 99) == 1,
                "disconnect did not clear selection or choose automatic replacement");
        auto reconnected = devices;
        reconnected[0].InstanceId = 50;
        Require(SelectControllerDevice(reconnected, "pad", "aa01", 7) == 50,
                "reconnect persisted an obsolete session ID");
    }
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
