#include "oot3d_native_a32_input.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

using namespace Oot3dNativeGame;

[[noreturn]] void Fail(std::string_view message) {
    std::cerr << "oot3d_native_a32_input_tests: " << message << '\n';
    std::exit(1);
}

void Require(bool condition, std::string_view message) {
    if (!condition) {
        Fail(message);
    }
}

void WriteText(const std::filesystem::path& path, std::string_view text) {
    std::ofstream output(path, std::ios::trunc);
    Require(output.good(), "cannot create temporary timeline");
    output << text;
    Require(output.good(), "cannot write temporary timeline");
}

void RequireTouch(const NativeA32TouchMapping& touch, bool inside,
                  bool pressed, uint16_t x, uint16_t y,
                  std::string_view message) {
    Require(touch.Inside == inside && touch.Pressed == pressed &&
                touch.X == x && touch.Y == y,
            message);
}

class ControllerButtonSource final : public ThreeDsRecomp::Input::HostButtonSource {
  public:
    NativeGamepadButton Held = NativeGamepadButton::None;
    bool IsKeyboardKeyHeld(NativeKeyboardKey) const noexcept override { return false; }
    bool IsMouseButtonHeld(NativeMouseButton) const noexcept override { return false; }
    bool IsGamepadButtonHeld(NativeGamepadButton button) const noexcept override {
        return button == Held;
    }
};

void TestShoulderMappings() {
    using ThreeDsRecomp::Input::IsHostBindingHeld;
    constexpr std::array actions{NativeControlAction::L, NativeControlAction::R,
                                 NativeControlAction::Zl, NativeControlAction::Zr};
    constexpr std::array masks{NativeA32HidButtonMask(NativeA32HidButton::L),
                               NativeA32HidButtonMask(NativeA32HidButton::R),
                               NativeA32HidButtonMask(NativeA32HidButton::Zl),
                               NativeA32HidButtonMask(NativeA32HidButton::Zr)};
    std::array sources{NativeGamepadButton::LeftShoulder, NativeGamepadButton::RightShoulder,
                       NativeGamepadButton::LeftTrigger, NativeGamepadButton::RightTrigger};
    std::sort(sources.begin(), sources.end());
    unsigned permutations = 0;
    do {
        auto config = NativeControlPreset(NativeControlProfile::Controller);
        config.Profile = NativeControlProfile::Custom;
        for (size_t i = 0; i < actions.size(); ++i)
            config.Bindings[static_cast<size_t>(actions[i])].Gamepad = sources[i];
        std::string json, error;
        NativeControlConfig loaded;
        Require(SerializeNativeControlConfigText(config, &json, &error) &&
                    ParseNativeControlConfigText(json, &loaded, &error) && loaded == config,
                "custom shoulder mapping did not survive save/reload");
        ControllerButtonSource physical;
        for (size_t i = 0; i < actions.size(); ++i) {
            // Exercise the same host-binding -> title action -> native HID path as the window.
            for (const bool held : {true, true, false}) {
                physical.Held = held ? sources[i] : NativeGamepadButton::None;
                NativeControlHostInputState host;
                for (size_t j = 0; j < host.Actions.size(); ++j)
                    host.Actions[j] = IsHostBindingHeld(loaded.Bindings[j], {}, physical);
                const auto frame = MapNativeControlInput(loaded, host);
                Require(frame.Hid.Buttons == (held ? masks[i] : 0U),
                        "remapped shoulder lost ZL/ZR or leaked L/R into the item press");
            }
        }
        ++permutations;
    } while (std::next_permutation(sources.begin(), sources.end()));
    Require(permutations == 24U, "not all shoulder/trigger assignments were exercised");
}

} // namespace

int main() {
    TestShoulderMappings();
    const auto defaults = NativeControlDefaults();
    Require(defaults.ControllerEnabled && defaults.KeyboardEnabled && defaults.MouseEnabled &&
                defaults.MovementStick == NativeAnalogStick::Left &&
                defaults.NativeAimSource == NativeMotionSource::Automatic &&
                defaults.FreeCameraSource == NativeMotionSource::Automatic &&
                defaults.PreferredControllerGuid.empty(),
            "fresh-install controls must accept all devices without a device-specific GUID");
    NativeControlHostInputState defaultGamepad;
    defaultGamepad.LeftStickX = 32767;
    defaultGamepad.RightStickY = 32767;
    const auto mappedDefaultGamepad = MapNativeControlInput(defaults, defaultGamepad);
    Require(mappedDefaultGamepad.Hid.CirclePadX == 154 && mappedDefaultGamepad.CStick.Y == 154,
            "fresh-install defaults discarded controller movement or camera axes");
    const auto keyboardMouse =
        NativeControlPreset(NativeControlProfile::KeyboardMouse);
    Require(keyboardMouse.Profile ==
                NativeControlProfile::KeyboardMouse &&
                keyboardMouse.KeyboardEnabled &&
                keyboardMouse.MouseEnabled &&
                keyboardMouse.Bindings[static_cast<size_t>(
                    NativeControlAction::MoveForward)]
                        .KeyboardPrimary == NativeKeyboardKey::W &&
                keyboardMouse.Bindings[static_cast<size_t>(
                    NativeControlAction::A)]
                        .Mouse == NativeMouseButton::Left,
            "keyboard/mouse preset lost its canonical bindings");

    std::size_t nativeDigitalActions = 0;
    std::size_t nativeTouchShortcuts = 0;
    for (std::size_t index = 0; index < kNativeControlActionCount; ++index) {
        const auto action = static_cast<NativeControlAction>(index);
        ThreeDsRecomp::Input::DigitalControl control;
        if (MapNativeControlActionToThreeDsControl(action, &control)) {
            ++nativeDigitalActions;
            Require(ThreeDsRecomp::Input::NativeRouteFor(control).Channel !=
                        ThreeDsRecomp::Input::NativeChannelKind::None,
                    "an OoT3D binding targets no native 3DS channel");
            NativeControlHostInputState host;
            host.Actions[index] = true;
            auto frame = MapNativeControlInput(keyboardMouse, host);
            ApplyNativeControlShortcutTouch(host, frame);
            Require(!frame.Hid.TouchPressed,
                    "a regular native control became a title shortcut");
            continue;
        }

        ++nativeTouchShortcuts;
        NativeControlHostInputState host;
        host.Actions[index] = true;
        const auto page = ResolveNativeControlPageShortcut(host);
        auto frame = MapNativeControlInput(keyboardMouse, host);
        Require(!frame.Hid.TouchPressed,
                "a title shortcut bypassed its native touch adapter");
        ApplyNativeControlShortcutTouch(host, frame);
        const auto expected = MapTopScreenPageShortcutToNativeTouch(page);
        Require(page != NativeA32TopScreenPage::None && expected.Pressed &&
                    frame.Hid.TouchPressed && frame.Hid.TouchX == expected.X &&
                    frame.Hid.TouchY == expected.Y,
                "an OoT3D convenience binding did not synthesize native touch");
    }
    Require(nativeDigitalActions ==
                ThreeDsRecomp::Input::kDigitalControlCount &&
                nativeTouchShortcuts == 3U,
            "OoT3D actions are duplicated or missing from native routes");

    NativeControlHostInputState mappedHost;
    mappedHost.Actions[static_cast<size_t>(
        NativeControlAction::MoveForward)] = true;
    mappedHost.Actions[static_cast<size_t>(
        NativeControlAction::MoveRight)] = true;
    mappedHost.Actions[static_cast<size_t>(NativeControlAction::A)] = true;
    mappedHost.Actions[static_cast<size_t>(
        NativeControlAction::Start)] = true;
    mappedHost.MouseDeltaX = 2;
    mappedHost.MouseDeltaY = -1;
    mappedHost.SamplePeriodSeconds = 0.01;
    const auto mappedKeyboardMouse =
        MapNativeControlInput(keyboardMouse, mappedHost);
    Require(mappedKeyboardMouse.Hid.CirclePadX == 154 &&
                mappedKeyboardMouse.Hid.CirclePadY == 154 &&
                (mappedKeyboardMouse.Hid.Buttons &
                 NativeA32HidButtonMask(NativeA32HidButton::A)) != 0U &&
                (mappedKeyboardMouse.Hid.Buttons &
                 NativeA32HidButtonMask(NativeA32HidButton::Start)) != 0U &&
                mappedKeyboardMouse.CStick.X == 8 &&
                mappedKeyboardMouse.CStick.Y == 4 &&
                mappedKeyboardMouse.CStick.Kind ==
                    NativeFreeCameraInputKind::Relative &&
                mappedKeyboardMouse.Hid.GyroscopeValid &&
                std::abs(
                    mappedKeyboardMouse.Hid
                        .GyroscopeDegreesPerSecond[0] -
                    35.0F) < 0.001F &&
                std::abs(
                    mappedKeyboardMouse.Hid
                        .GyroscopeDegreesPerSecond[2] -
                    70.0F) < 0.001F,
            "keyboard/mouse profile did not map movement, buttons and "
            "native gyro units");

    auto controllerConfig =
        NativeControlPreset(NativeControlProfile::Controller);
    controllerConfig.GyroscopeBiasDegreesPerSecond =
        {1.0F, 2.0F, 3.0F};
    NativeControlHostInputState controllerHost;
    controllerHost.LeftStickX = 32767;
    controllerHost.RightStickY = 32767;
    controllerHost.ControllerMotion.GyroscopeDegreesPerSecond =
        {11.0F, 22.0F, 33.0F};
    controllerHost.ControllerMotion.GyroscopeValid = true;
    controllerHost.ControllerMotion.Accelerometer =
        {0.25F, -0.9F, 0.1F};
    controllerHost.ControllerMotion.AccelerometerValid = true;
    const auto mappedController =
        MapNativeControlInput(controllerConfig, controllerHost);
    Require(mappedController.Hid.CirclePadX == 154 &&
                mappedController.CStick.Y == 154 &&
                mappedController.Hid.GyroscopeValid &&
                mappedController.Hid.AccelerometerValid &&
                std::abs(
                    mappedController.Hid.GyroscopeDegreesPerSecond[0] -
                    -180.0F) < 0.001F &&
                std::abs(
                    mappedController.Hid.GyroscopeDegreesPerSecond[2] -
                    0.0F) < 0.001F,
            "automatic controller profile did not prioritize active C-stick");
    auto motionOnlyControllerHost = controllerHost;
    motionOnlyControllerHost.RightStickY = 0;
    const auto mappedControllerMotion =
        MapNativeControlInput(controllerConfig, motionOnlyControllerHost);
    Require(mappedControllerMotion.Hid.GyroscopeValid &&
                mappedControllerMotion.Hid.AccelerometerValid &&
                std::abs(mappedControllerMotion.Hid
                             .GyroscopeDegreesPerSecond[0] -
                         10.0F) < 0.001F &&
                std::abs(mappedControllerMotion.Hid
                             .GyroscopeDegreesPerSecond[2] -
                         30.0F) < 0.001F,
            "automatic controller profile lost calibrated motion fallback");

    auto rightStickAimConfig = controllerConfig;
    rightStickAimConfig.NativeAimSource = NativeMotionSource::RightStick;
    rightStickAimConfig.LookStickDeadZonePercent = 0;
    rightStickAimConfig.RightStickAimMaximumDegreesPerSecond = 180.0F;
    NativeControlHostInputState rightStickAimHost;
    rightStickAimHost.RightStickX = 32767;
    rightStickAimHost.RightStickY = 32767;
    const auto transformedRightStickAim = MapNativeControlInput(
        rightStickAimConfig, rightStickAimHost,
        {.RightStickScale = 2.0F, .RightStickInvertX = true});
    Require(transformedRightStickAim.Hid.GyroscopeValid &&
                std::abs(transformedRightStickAim.Hid
                             .GyroscopeDegreesPerSecond[0] +
                         360.0F) < 0.001F &&
                std::abs(transformedRightStickAim.Hid
                             .GyroscopeDegreesPerSecond[2] +
                         360.0F) < 0.001F,
            "profile transform did not scale and invert right-stick aim");
    auto smoothedRightStickConfig = rightStickAimConfig;
    smoothedRightStickConfig.FreeCameraSource = NativeMotionSource::RightStick;
    NativeRightStickProfileState rightStickState;
    NativeAimProfileTransform smoothingTransform;
    smoothingTransform.RightStickSmoothingCoefficient = 0.5F;
    const auto firstSmoothedFrame = MapNativeControlInput(
        smoothedRightStickConfig, rightStickAimHost, smoothingTransform,
        &rightStickState, true);
    const auto heldSmoothedFrame = MapNativeControlInput(
        smoothedRightStickConfig, rightStickAimHost, smoothingTransform,
        &rightStickState, false);
    const auto secondSmoothedFrame = MapNativeControlInput(
        smoothedRightStickConfig, rightStickAimHost, smoothingTransform,
        &rightStickState, true);
    Require(firstSmoothedFrame.CStick.X == 154 &&
                heldSmoothedFrame.CStick.X == 154 &&
                secondSmoothedFrame.CStick.X == 154 &&
                std::abs(firstSmoothedFrame.Hid
                             .GyroscopeDegreesPerSecond[2] -
                         90.0F) < 0.001F,
            "C-stick aiming smoothing contaminated free-camera input");

    NativeControlHostInputState fastMouseHost;
    fastMouseHost.MouseDeltaX = 100;
    const auto fastMouseFrame = MapNativeControlInput(
        keyboardMouse, fastMouseHost);
    Require(fastMouseFrame.CStick.X == 400 &&
                fastMouseFrame.CStick.Kind ==
                    NativeFreeCameraInputKind::Relative,
            "relative free-camera mouse input was clamped as an analog stick");
    NativeFreeCameraInputAccumulator freeCameraInput;
    freeCameraInput.Observe(
        {12, -7, NativeFreeCameraInputKind::Relative});
    freeCameraInput.Observe(
        {0, 0, NativeFreeCameraInputKind::Relative});
    freeCameraInput.Observe(
        {5, 3, NativeFreeCameraInputKind::Relative});
    const auto retainedMouseInput = freeCameraInput.Consume();
    Require(retainedMouseInput.X == 17 && retainedMouseInput.Y == -4 &&
                retainedMouseInput.Kind ==
                    NativeFreeCameraInputKind::Relative &&
                freeCameraInput.Peek().X == 0 &&
                freeCameraInput.Peek().Y == 0 &&
                freeCameraInput.RelativeSamplesObserved() == 2U &&
                freeCameraInput.RelativeSamplesConsumed() == 1U,
            "relative free-camera input was lost before camera consumption");
    freeCameraInput.Observe(
        {80, -40, NativeFreeCameraInputKind::Absolute});
    const auto heldAnalogInput = freeCameraInput.Consume();
    Require(heldAnalogInput.X == 80 && heldAnalogInput.Y == -40 &&
                freeCameraInput.Peek().X == 80 &&
                freeCameraInput.Peek().Y == -40,
            "held analog free-camera input was consumed as a mouse delta");
    Require(ResolveNativeGameplayMouseOwnership(false, false, true, true) &&
                !ResolveNativeGameplayMouseOwnership(true, false, true,
                                                     true) &&
                !ResolveNativeGameplayMouseOwnership(false, true, true,
                                                     true),
            "gameplay mouse ownership did not respect frontend and host GUI");
    const auto unchangedPhysicalMotion = MapNativeControlInput(
        controllerConfig, motionOnlyControllerHost,
        {.RightStickScale = 2.0F,
         .RightStickInvertX = true,
         .RightStickInvertY = true});
    Require(std::abs(unchangedPhysicalMotion.Hid
                         .GyroscopeDegreesPerSecond[0] -
                     10.0F) < 0.001F &&
                std::abs(unchangedPhysicalMotion.Hid
                             .GyroscopeDegreesPerSecond[2] -
                         30.0F) < 0.001F,
            "right-stick profile transform contaminated physical motion");

    std::string serializedControls;
    std::string controlsError;
    Require(SerializeNativeControlConfigText(
                controllerConfig, &serializedControls, &controlsError),
            controlsError);
    NativeControlConfig parsedControls;
    Require(ParseNativeControlConfigText(
                serializedControls, &parsedControls, &controlsError) &&
                parsedControls.Profile ==
                    NativeControlProfile::Controller &&
                parsedControls.GyroscopeBiasDegreesPerSecond ==
                    controllerConfig.GyroscopeBiasDegreesPerSecond,
            "native control JSON did not round-trip");
    for (const auto& config : {defaults, keyboardMouse}) {
        Require(SerializeNativeControlConfigText(config, &serializedControls, &controlsError) &&
                    ParseNativeControlConfigText(serializedControls, &parsedControls, &controlsError) &&
                    parsedControls == config,
                "control persistence changed combined defaults or an explicit keyboard-only preference");
    }

    RequireTouch(MapHostPointerToNativeA32Touch(160, 0, 1280, 720, true),
                 true, true, 0, 0,
                 "720p presentation origin did not map to native touch");
    RequireTouch(MapHostPointerToNativeA32Touch(1119, 719, 1280, 720, true),
                 true, true, 319, 239,
                 "720p presentation extent did not map to native touch");
    RequireTouch(MapHostPointerToNativeA32Touch(640, 360, 1280, 720, true),
                 true, true, 160, 120,
                 "720p presentation center did not map to native touch");
    RequireTouch(MapHostPointerToNativeA32Touch(159, 360, 1280, 720, true),
                 false, false, 0, 0,
                 "left pillarbox accepted a native touch");
    RequireTouch(MapHostPointerToNativeA32Touch(1120, 360, 1280, 720, true),
                 false, false, 0, 0,
                 "right pillarbox accepted a native touch");
    RequireTouch(MapHostPointerToNativeA32Touch(1920, 1080, 3840, 2160, true),
                 true, true, 160, 120,
                 "4K presentation center did not map to native touch");
    RequireTouch(MapHostPointerToNativeA32Touch(799, 599, 800, 600, true),
                 true, true, 319, 239,
                 "4:3 presentation extent did not map to native touch");
    RequireTouch(MapHostPointerToNativeA32Touch(33, 350, 1000, 700, true),
                 false, false, 0, 0,
                 "fractional left pillarbox accepted a native touch");
    RequireTouch(MapHostPointerToNativeA32Touch(34, 350, 1000, 700, true),
                 true, true, 0, 120,
                 "fractional presentation origin did not map to native touch");
    RequireTouch(MapHostPointerToNativeA32Touch(966, 699, 1000, 700, true),
                 true, true, 319, 239,
                 "fractional presentation extent did not map to native touch");
    RequireTouch(MapHostPointerToNativeA32Touch(967, 350, 1000, 700, true),
                 false, false, 0, 0,
                 "fractional right pillarbox accepted a native touch");
    RequireTouch(MapHostPointerToNativeA32Touch(960, 540, 1920, 1080, false),
                 true, false, 160, 120,
                 "released pointer did not preserve its mapped position");
    RequireTouch(MapHostPointerToNativeA32Touch(0, 0, 0, 1080, true), false,
                 false, 0, 0,
                 "invalid host extent accepted a native touch");
    const auto topScreenPresentation =
        NativeA32TouchPresentation::TopScreen400x240;
    RequireTouch(MapHostPointerToNativeA32Touch(
                     400, 663, 1280, 720, true, topScreenPresentation),
                 true, true, 96, 221,
                 "widescreen TopScreen Gear target did not map to native touch");
    RequireTouch(MapHostPointerToNativeA32Touch(
                     640, 663, 1280, 720, true, topScreenPresentation),
                 true, true, 160, 221,
                 "widescreen TopScreen Map target did not map to native touch");
    RequireTouch(MapHostPointerToNativeA32Touch(
                     880, 663, 1280, 720, true, topScreenPresentation),
                 true, true, 224, 221,
                 "widescreen TopScreen Items target did not map to native touch");
    RequireTouch(MapHostPointerToNativeA32Touch(
                     308, 643, 1024, 768, true, topScreenPresentation),
                 true, true, 96, 221,
                 "4:3 TopScreen Gear target did not map to native touch");
    RequireTouch(MapHostPointerToNativeA32Touch(
                     512, 76, 1024, 768, true, topScreenPresentation),
                 false, false, 0, 0,
                 "TopScreen letterbox accepted a native touch");
    RequireTouch(MapTopScreenPageShortcutToNativeTouch(
                     NativeA32TopScreenPage::Gear),
                 true, true, 96, 221,
                 "Gear shortcut did not preserve the native touch target");
    RequireTouch(MapTopScreenPageShortcutToNativeTouch(
                     NativeA32TopScreenPage::Map),
                 true, true, 160, 221,
                 "Map shortcut did not preserve the native touch target");
    RequireTouch(MapTopScreenPageShortcutToNativeTouch(
                     NativeA32TopScreenPage::Items),
                 true, true, 224, 221,
                 "Items shortcut did not preserve the native touch target");
    RequireTouch(MapTopScreenPageShortcutToNativeTouch(
                     NativeA32TopScreenPage::None),
                 false, false, 0, 0,
                 "inactive page shortcut synthesized a touch");
    Require(IsTopScreenGameplayInputMode(0U) &&
                IsTopScreenGameplayInputMode(1U) &&
                !IsTopScreenGameplayInputMode(2U) &&
                !IsTopScreenGameplayInputMode(UINT32_MAX),
            "TopScreen gameplay input mode gate drifted");

    NativeA32PolledButtonLatch heldStart;
    ObserveNativeA32PolledButton(true, heldStart);
    Require(ResolveNativeA32PolledButton(true, heldStart) &&
                !heldStart.PendingPressed &&
                heldStart.ObservedPresses == 1U &&
                heldStart.RecoveredShortPresses == 0U,
            "held host START did not reach its first guest refresh");
    Require(ResolveNativeA32PolledButton(true, heldStart),
            "held host START became a one-refresh pulse");
    ObserveNativeA32PolledButton(false, heldStart);
    Require(!ResolveNativeA32PolledButton(false, heldStart),
            "released host START remained asserted");

    NativeA32PolledButtonLatch shortStart;
    ObserveNativeA32PolledButton(true, shortStart);
    ObserveNativeA32PolledButton(false, shortStart);
    Require(ResolveNativeA32PolledButton(false, shortStart) &&
                !shortStart.PendingPressed &&
                shortStart.ObservedPresses == 1U &&
                shortStart.RecoveredShortPresses == 1U,
            "START tap between guest refreshes was lost");
    Require(!ResolveNativeA32PolledButton(false, shortStart),
            "recovered short START tap was delivered more than once");

    NativeA32InputFrame defaultStart;
    TopScreenStartRoutingState startRouting;
    defaultStart.Hid.Buttons =
        NativeA32HidButtonMask(NativeA32HidButton::Start);
    ApplyTopScreenStartRouting(defaultStart, false, false, false, true,
                               startRouting);
    Require(defaultStart.Hid.Buttons ==
                NativeA32HidButtonMask(NativeA32HidButton::Start) &&
                !defaultStart.Hid.TouchPressed &&
                !startRouting.OpenGestureHeld &&
                !startRouting.CloseGestureHeld,
            "default profile did not preserve native START");

    Require(ResolveNativeA32TouchInputEnabled(false, true, false, false),
            "default profile ignored native touch ownership");
    Require(!ResolveNativeA32TouchInputEnabled(true, true, false, false),
            "TopScreen gameplay inherited broad native touch ownership");
    Require(ResolveNativeA32TouchInputEnabled(true, true, true, false) &&
                ResolveNativeA32TouchInputEnabled(true, true, false, true),
            "TopScreen did not retain frontend or pause-page touch ownership");

    NativeA32InputFrame gameplayStart;
    gameplayStart.Hid.Buttons =
        NativeA32HidButtonMask(NativeA32HidButton::A) |
        NativeA32HidButtonMask(NativeA32HidButton::Start);
    ApplyTopScreenStartRouting(gameplayStart, true, true, false, false,
                               startRouting);
    Require(gameplayStart.Hid.Buttons ==
                (NativeA32HidButtonMask(NativeA32HidButton::A) |
                 NativeA32HidButtonMask(NativeA32HidButton::Start)) &&
                !gameplayStart.Hid.TouchPressed &&
                !startRouting.OpenGestureHeld &&
                !startRouting.CloseGestureHeld,
            "presentation-only frame consumed TopScreen gameplay START");

    ApplyTopScreenStartRouting(gameplayStart, true, true, false, true,
                               startRouting);
    Require(gameplayStart.Hid.Buttons ==
                NativeA32HidButtonMask(NativeA32HidButton::A) &&
                gameplayStart.Hid.TouchPressed &&
                gameplayStart.Hid.TouchX == 224U &&
                gameplayStart.Hid.TouchY == 221U &&
                startRouting.OpenGestureHeld &&
                !startRouting.CloseGestureHeld,
            "TopScreen gameplay START did not route to native Items");

    NativeA32InputFrame continuedOpeningStart;
    continuedOpeningStart.Hid.Buttons =
        NativeA32HidButtonMask(NativeA32HidButton::Start);
    ApplyTopScreenStartRouting(continuedOpeningStart, true, true, false, true,
                               startRouting);
    Require(continuedOpeningStart.Hid.Buttons == 0U &&
                continuedOpeningStart.Hid.TouchPressed &&
                continuedOpeningStart.Hid.TouchX == 224U &&
                continuedOpeningStart.Hid.TouchY == 221U &&
                startRouting.OpenGestureHeld &&
                !startRouting.CloseGestureHeld,
            "held opening START did not sustain the native Items touch");

    NativeA32InputFrame heldOpeningStart;
    heldOpeningStart.Hid.Buttons =
        NativeA32HidButtonMask(NativeA32HidButton::Start);
    ApplyTopScreenStartRouting(heldOpeningStart, true, false, false, true,
                               startRouting);
    Require(heldOpeningStart.Hid.Buttons == 0U &&
                !heldOpeningStart.Hid.TouchPressed &&
                startRouting.OpenGestureHeld,
            "transient ownership gap duplicated TopScreen opening START");

    NativeA32InputFrame stillHeldOpeningStart;
    stillHeldOpeningStart.Hid.Buttons =
        NativeA32HidButtonMask(NativeA32HidButton::Start);
    ApplyTopScreenStartRouting(stillHeldOpeningStart, true, false, true, true,
                               startRouting);
    Require(stillHeldOpeningStart.Hid.Buttons == 0U &&
                !stillHeldOpeningStart.Hid.TouchPressed &&
                startRouting.OpenGestureHeld,
            "held TopScreen opening START reached the new pause owner");

    NativeA32InputFrame releasedOpeningStart;
    ApplyTopScreenStartRouting(releasedOpeningStart, true, false, true, false,
                               startRouting);
    Require(startRouting.OpenGestureHeld,
            "presentation-only release cleared TopScreen START latch");
    ApplyTopScreenStartRouting(releasedOpeningStart, true, false, true, true,
                               startRouting);
    Require(!startRouting.OpenGestureHeld,
            "TopScreen opening START latch survived release");

    NativeA32InputFrame pauseStart;
    pauseStart.Hid.Buttons =
        NativeA32HidButtonMask(NativeA32HidButton::Start);
    ApplyTopScreenStartRouting(pauseStart, true, false, true, true,
                               startRouting);
    Require(pauseStart.Hid.Buttons ==
                NativeA32HidButtonMask(NativeA32HidButton::Start) &&
                !pauseStart.Hid.TouchPressed,
            "TopScreen pause START did not remain native");

    MarkTopScreenStartCloseGestureConsumed(startRouting);
    NativeA32InputFrame heldClosingStart;
    heldClosingStart.Hid.Buttons =
        NativeA32HidButtonMask(NativeA32HidButton::Start);
    ApplyTopScreenStartRouting(heldClosingStart, true, true, false, true,
                               startRouting);
    Require(heldClosingStart.Hid.Buttons == 0U &&
                !heldClosingStart.Hid.TouchPressed &&
                !startRouting.OpenGestureHeld &&
                startRouting.CloseGestureHeld,
            "held closing START reopened the TopScreen pause page");
    NativeA32InputFrame releasedClosingStart;
    ApplyTopScreenStartRouting(releasedClosingStart, true, true, false, true,
                               startRouting);
    Require(!startRouting.OpenGestureHeld &&
                !startRouting.CloseGestureHeld,
            "closing START latch survived the physical release");

    MarkTopScreenStartOpenGestureConsumed(startRouting);
    ReleaseTopScreenStartRoutingLatch(true, startRouting);
    Require(startRouting.OpenGestureHeld &&
                !startRouting.CloseGestureHeld,
            "poll-rate latch release fired while START was still held");
    ReleaseTopScreenStartRoutingLatch(false, startRouting);
    Require(!startRouting.OpenGestureHeld &&
                !startRouting.CloseGestureHeld,
            "poll-rate latch release ignored the physical release");
    NativeA32InputFrame repressedStart;
    repressedStart.Hid.Buttons =
        NativeA32HidButtonMask(NativeA32HidButton::Start);
    ApplyTopScreenStartRouting(repressedStart, true, true, false, true,
                               startRouting);
    Require(repressedStart.Hid.TouchPressed &&
                startRouting.OpenGestureHeld &&
                !startRouting.CloseGestureHeld,
            "START re-press after a poll-rate release was swallowed");

    ReleaseTopScreenStartRoutingLatch(false, startRouting);

    const auto path = std::filesystem::temp_directory_path() /
                      "oot3d_native_a32_input_timeline.json";
    const auto controlsPath = std::filesystem::temp_directory_path() /
                              "oot3d_native_controls_test.json";
    std::filesystem::remove(controlsPath);
    {
        NativeControlConfigRuntime runtime(controlsPath, keyboardMouse);
        auto preview = keyboardMouse;
        preview.MouseFreeCameraUnitsPerPixel = 7.0F;
        const auto initialRevision = runtime.Snapshot().Revision;
        Require(runtime.Preview(preview, &controlsError) &&
                    runtime.Snapshot().Revision == initialRevision + 1U &&
                    runtime.Snapshot().Config == preview &&
                    !std::filesystem::exists(controlsPath),
                "control live preview was not isolated from persistence");
        runtime.BeginMotionCalibration();
        NativeControlMotionObservation observation;
        observation.GyroscopeDegreesPerSecond = {1.0F, -2.0F, 3.0F};
        observation.GyroscopeValid = true;
        observation.Accelerometer = {0.1F, -0.95F, 0.2F};
        observation.AccelerometerValid = true;
        for (uint32_t sample = 0; sample < 60U; ++sample) {
            runtime.ObserveMotion(observation);
        }
        const auto calibrated = runtime.Snapshot().Config;
        Require(!runtime.CalibrationStatus().Active &&
                    runtime.CalibrationStatus()
                        .LastCalibrationSucceeded &&
                    calibrated.GyroscopeBiasDegreesPerSecond ==
                        observation.GyroscopeDegreesPerSecond &&
                    calibrated.AccelerometerNeutral ==
                        observation.Accelerometer &&
                    std::filesystem::is_regular_file(controlsPath),
                "motion calibration did not average and persist samples");
    }
    WriteText(path, R"json({
  "schema": "oot3d.native_game.input_timeline.v1",
  "segments": [
    {
      "start_frame": 3,
      "end_frame_exclusive": 6,
      "buttons": ["a", "start"],
      "topscreen_zr": true,
      "circle_x": 1.0,
      "circle_y": -0.5,
      "right_x": 0.25,
      "right_y": -0.75,
      "touch": {"pressed": true, "x": 319, "y": 239}
    },
    {
      "start_frame": 8,
      "end_frame_exclusive": 9,
      "buttons": ["b"]
    }
  ]
})json");

    const auto timeline = NativeA32InputTimeline::LoadFile(path);
    Require(timeline.Enabled() && timeline.SegmentCount() == 2U,
            "valid native input timeline was not loaded");
    Require(timeline.FrameOrigin() ==
                NativeA32InputTimelineFrameOrigin::Guest,
            "legacy timeline did not retain guest-frame origin");
    const auto gap = timeline.Sample(2, 1002);
    Require(gap.Scripted && gap.ScriptSegmentIndex == -1 &&
                gap.Hid.Buttons == 0U,
            "scripted timeline gap did not resolve to neutral input");
    const auto active = timeline.Sample(3, 1003);
    Require(active.ScriptSegmentIndex == 0 &&
                active.Hid.Buttons ==
                    (NativeA32HidButtonMask(NativeA32HidButton::A) |
                     NativeA32HidButtonMask(NativeA32HidButton::Start) |
                     ThreeDsRecomp::Input::ButtonMask(
                         ThreeDsRecomp::Input::Button::Zr)) &&
                active.Hid.CirclePadX == 154 &&
                active.Hid.CirclePadY == -77 &&
                ThreeDsRecomp::Input::IsButtonHeld(
                    active, ThreeDsRecomp::Input::Button::Zr) &&
                !ThreeDsRecomp::Input::IsButtonHeld(
                    active, ThreeDsRecomp::Input::Button::Zl) &&
                active.CStick.X == 39 &&
                active.CStick.Y == -116 &&
                active.Hid.TouchPressed && active.Hid.TouchX == 319U &&
                active.Hid.TouchY == 239U,
            "native input segment payload was decoded incorrectly");

    NativeA32InputDiagnostics diagnostics;
    diagnostics.Observe(gap);
    diagnostics.Observe(active);
    Require(diagnostics.SampledFrameCount == 2U &&
                diagnostics.ScriptedFrameCount == 2U &&
                diagnostics.ActiveSegmentFrameCount == 1U &&
                diagnostics.ButtonFrameCount == 1U &&
                diagnostics.CirclePadFrameCount == 1U &&
                diagnostics.TouchFrameCount == 1U,
            "native input diagnostics did not preserve sampled states");

    WriteText(path, R"json({
  "schema": "oot3d.native_game.input_timeline.v1",
  "frame_origin": "run",
  "segments": [{"start_frame": 3, "end_frame_exclusive": 4,
                "buttons": ["a"]}]
})json");
    const auto runTimeline = NativeA32InputTimeline::LoadFile(path);
    Require(runTimeline.FrameOrigin() ==
                NativeA32InputTimelineFrameOrigin::Run &&
                runTimeline.Sample(3, 2).ScriptSegmentIndex == -1 &&
                runTimeline.Sample(1003, 3).ScriptSegmentIndex == 0,
            "run-relative input timeline used the guest frame");

    WriteText(path, R"json({
      "schema": "oot3d.native_game.input_timeline.v1",
      "segments": [{"start_frame": 0, "end_frame_exclusive": 2,
        "buttons": ["zr", "zl"], "gyroscope_dps": [10, -20, 30],
        "accelerometer_g": [0.5, -1, 0.25]}]
    })json");
    const auto sensorTimeline = NativeA32InputTimeline::LoadFile(path);
    const auto sensorFrame = sensorTimeline.Sample(0, 0);
    Require(ThreeDsRecomp::Input::IsButtonHeld(sensorFrame, ThreeDsRecomp::Input::Button::Zr) &&
            ThreeDsRecomp::Input::IsButtonHeld(sensorFrame, ThreeDsRecomp::Input::Button::Zl),
            "absent legacy TopScreen aliases cleared explicit native shoulder buttons");
    Require(sensorFrame.Hid.GyroscopeValid && sensorFrame.Hid.AccelerometerValid &&
            sensorFrame.Hid.GyroscopeDegreesPerSecond == std::array<float, 3>{10, -20, 30} &&
            sensorFrame.Hid.Accelerometer == std::array<float, 3>{0.5F, -1, 0.25F} &&
            !sensorTimeline.Sample(2, 2).Hid.GyroscopeValid,
            "timeline sensor units or sample lifetime changed");
    WriteText(path, R"json({
      "schema": "oot3d.native_game.input_timeline.v1",
      "segments": [{"start_frame": 0, "end_frame_exclusive": 2, "gyroscope_dps": [1, 2]}]
    })json");
    bool rejectedMotion = false;
    try { static_cast<void>(NativeA32InputTimeline::LoadFile(path)); }
    catch (const std::runtime_error&) { rejectedMotion = true; }
    Require(rejectedMotion, "malformed timeline motion vector was accepted");

    WriteText(path, R"json({
  "schema": "oot3d.native_game.input_timeline.v1",
  "frame_origin": "invalid",
  "segments": [{"start_frame": 0, "end_frame_exclusive": 1}]
})json");
    bool rejectedUnknownOrigin = false;
    try {
        static_cast<void>(NativeA32InputTimeline::LoadFile(path));
    } catch (const std::runtime_error&) {
        rejectedUnknownOrigin = true;
    }
    Require(rejectedUnknownOrigin,
            "unknown native input timeline frame origin was accepted");

    WriteText(path, R"json({
  "schema": "oot3d.native_game.input_timeline.v1",
  "segments": [{"start_frame": 0, "end_frame_exclusive": 1,
                "buttons": ["unknown"]}]
})json");
    bool rejectedUnknownButton = false;
    try {
        static_cast<void>(NativeA32InputTimeline::LoadFile(path));
    } catch (const std::runtime_error&) {
        rejectedUnknownButton = true;
    }
    Require(rejectedUnknownButton, "unknown native HID button was accepted");

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(controlsPath, ignored);
    std::cout << "oot3d_native_a32_input_tests: ok\n";
    return 0;
}
