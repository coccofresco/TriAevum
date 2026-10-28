#include "three_ds_input.h"
#include "three_ds_pad_menu_chord.h"
#include "three_ds_pointer_press_latch.h"
#include "three_ds_touch_anchors.h"
#include "three_ds_touch_camera.h"
#include "three_ds_touch_skin.h"
#include "three_ds_virtual_pad.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>
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

void TestPointerPressLatch() {
    using ThreeDsRecomp::Input::PointerPressLatch;
    PointerPressLatch latch;
    Require(!latch.Resolve(false, true), "an idle pointer reads pressed");
    // A tap released before the poll: seen by polls until one consumes it.
    latch.ObservePress();
    Require(latch.Resolve(false, false), "a tap between polls is lost");
    Require(latch.Resolve(false, true), "the consuming poll loses the tap");
    Require(!latch.Resolve(false, true), "a consumed tap repeats");
    // A long press reads held after its press is consumed.
    latch.ObservePress();
    Require(latch.Resolve(true, true) && latch.Resolve(true, true), "a held pointer reads released");
    Require(!latch.Resolve(false, true), "a released pointer reads pressed");
    latch.ObservePress();
    latch.Reset();
    Require(!latch.Resolve(false, true), "a reset press survives");
}

void TestVirtualPad() {
    using namespace ThreeDsRecomp::Input;
    VirtualPad pad;
    const auto a = VirtualPad::Bit(DigitalControl::A);
    // A tap between polls: not held, pressed until a consuming read.
    pad.SetControl(DigitalControl::A, true);
    pad.SetControl(DigitalControl::A, false);
    Require(pad.Read(false).Held == 0 && pad.Read(false).Pressed == a, "a tap between polls is lost");
    Require(pad.Read(true).Pressed == a && pad.Read(true).Pressed == 0, "a consumed tap repeats");
    pad.SetControl(DigitalControl::Zr, true);
    Require(pad.Read(true).Held == VirtualPad::Bit(DigitalControl::Zr), "a held button reads released");
    pad.SetCirclePad(0.5F, -2.0F);
    pad.SetCStick(std::numeric_limits<float>::quiet_NaN(), 0.25F);
    const auto sticks = pad.Read(true);
    Require(sticks.CirclePad == std::array<float, 2>{0.5F, -1.0F} && sticks.CStick == std::array<float, 2>{0.0F, 0.25F},
            "virtual sticks are not clamped and sanitized");
    pad.SetControl(DigitalControl::B, true);
    pad.ReleaseAll();
    const auto released = pad.Read(true);
    Require(released.Held == 0 && released.Pressed == 0 && released.CirclePad == std::array<float, 2>{},
            "release-all leaves input behind");
    pad.RequestMenu();
    Require(pad.TakeMenuRequest() && !pad.TakeMenuRequest() && !pad.TakeSettingsRequest(), "menu request repeats");
    DigitalControl control{};
    Require(DigitalControlForHidBit(0, &control) && control == DigitalControl::A &&
                DigitalControlForHidBit(15, &control) && control == DigitalControl::Zr &&
                !DigitalControlForHidBit(12, &control) && !DigitalControlForHidBit(16, &control),
            "HID bits map to the wrong controls");
}

} // namespace

void TestTouchCameraTracker() {
    using namespace ThreeDsRecomp::Input;
    TouchCameraTracker tracker;
    std::vector<SurfaceFinger> fingers{{1, 7, 100.0F, 100.0F}};
    Require(!tracker.Update(true, fingers).Active, "a landing finger moves the camera");
    fingers[0].X = 130.0F;
    fingers[0].Y = 90.0F;
    const auto drag = tracker.Update(true, fingers);
    Require(drag.Active && drag.DeltaX == 30.0F && drag.DeltaY == -10.0F, "a drag does not move the camera");
    fingers[0].X = 140.0F;
    Require(!tracker.Update(false, fingers).Active, "a drag continues into a menu");
    Require(!tracker.Update(true, fingers).Active, "a stopped drag resumes under the same finger");
    // A finger that landed on a menu never drags.
    std::vector<SurfaceFinger> menuFinger{{1, 8, 10.0F, 10.0F}};
    Require(!tracker.Update(false, menuFinger).Active, "a menu touch moves the camera");
    menuFinger[0].X = 50.0F;
    Require(!tracker.Update(true, menuFinger).Active, "a menu touch turns into a drag");
    Require(!tracker.Update(true, {}).Active, "lifted fingers drag");
    // With a slop a finger moves the camera only past it; two quick taps on
    // the same spot are a double tap, a slow or moved one is not.
    TouchCameraTracker taps;
    std::vector<SurfaceFinger> tap{{1, 9, 200.0F, 200.0F}};
    Require(!taps.Update(true, tap, 0.0, 10.0F).Active, "a landing finger moves the camera");
    tap[0].X = 205.0F;
    Require(!taps.Update(true, tap, 0.05, 10.0F).Active, "a finger within the slop moves the camera");
    Require(!taps.Update(true, {}, 0.1, 10.0F).DoubleTap, "one tap is a double tap");
    tap = {{1, 10, 203.0F, 198.0F}};
    (void)taps.Update(true, tap, 0.25, 10.0F);
    Require(taps.Update(true, {}, 0.3, 10.0F).DoubleTap, "two quick taps are not a double tap");
    tap = {{1, 11, 200.0F, 200.0F}};
    (void)taps.Update(true, tap, 1.0, 10.0F);
    (void)taps.Update(true, {}, 1.05, 10.0F);
    tap = {{1, 12, 200.0F, 200.0F}};
    (void)taps.Update(true, tap, 1.6, 10.0F);
    Require(!taps.Update(true, {}, 1.65, 10.0F).DoubleTap, "two slow taps are a double tap");
    tap = {{1, 13, 200.0F, 200.0F}};
    (void)taps.Update(true, tap, 1.7, 10.0F);
    tap[0].X = 240.0F;
    Require(taps.Update(true, tap, 1.72, 10.0F).Active, "a finger past the slop does not drag");
    Require(!taps.Update(true, {}, 1.75, 10.0F).DoubleTap, "a drag counts as a tap");
}

void TestTouchAnchors() {
    using namespace ThreeDsRecomp::Input;
    TouchAnchors anchors;
    Require(ApplyTouchAnchorList("A=0.9,0.5,0.95,0.6;DPAD=0.02,0.4,0.14,0.62", anchors), "a valid layout is refused");
    const auto set = anchors.Snapshot();
    Require(set[static_cast<std::size_t>(TouchAnchor::A)].Present &&
                set[static_cast<std::size_t>(TouchAnchor::Dpad)].Present &&
                !set[static_cast<std::size_t>(TouchAnchor::B)].Present,
            "the layout sets the wrong anchors");
    Require(AnyTouchButtonAnchored(set), "an anchored A is not a button");
    TouchAnchors untouched;
    Require(!ApplyTouchAnchorList("A=0.9,0.5,0.95;B=1,1,1,1", untouched) && !ApplyTouchAnchorList("Q=0,0,1,1", untouched),
            "a malformed layout is accepted");
    Require(!AnyTouchButtonAnchored(untouched.Snapshot()), "a malformed layout set anchors");
    // Empty or inverted rectangles are no anchor.
    untouched.Set(TouchAnchor::X, 0.5F, 0.5F, 0.4F, 0.6F);
    Require(!untouched.Snapshot()[static_cast<std::size_t>(TouchAnchor::X)].Present, "an inverted anchor is present");
}

void TestTouchSkin() {
    using namespace ThreeDsRecomp::Input;
    for (int index = 0; index < static_cast<int>(TouchSkinShape::Count); ++index) {
        const auto shape = static_cast<TouchSkinShape>(index);
        Require(TouchSkinShapeFromSemanticName(TouchSkinSemanticName(shape)) == shape, "a skin name does not round-trip");
        const auto pixels = RasterizeTouchSkinShape(shape);
        Require(pixels.Rgba8.size() == static_cast<std::size_t>(pixels.Width) * pixels.Height * 4U && pixels.Width == 256U,
                "a skin shape has the wrong size");
        bool covered = false;
        for (std::size_t texel = 3; texel < pixels.Rgba8.size(); texel += 4) covered |= pixels.Rgba8[texel] != 0U;
        Require(covered, "a skin shape is empty");
    }
    Require(!TouchSkinShapeFromSemanticName("triaevum/touch_skin/none") &&
                !TouchSkinShapeFromSemanticName("oot3d/native/item_icons"),
            "an unknown name resolved to a skin shape");
    const auto alpha = [](const TouchSkinPixels &pixels, int x, int y) {
        return pixels.Rgba8[(static_cast<std::size_t>(y) * pixels.Width + x) * 4U + 3U];
    };
    // A disc covers its centre, not its corner; a ring the reverse of its centre.
    const auto disc = RasterizeTouchSkinShape(TouchSkinShape::Disc);
    const auto ring = RasterizeTouchSkinShape(TouchSkinShape::Ring);
    const auto thinRing = RasterizeTouchSkinShape(TouchSkinShape::RingThin);
    Require(alpha(disc, 128, 128) == 255U && alpha(disc, 0, 0) == 0U && alpha(ring, 128, 128) == 0U &&
                alpha(ring, 128, 3) == 255U,
            "the disc or ring coverage is wrong");
    Require(alpha(thinRing, 128, 128) == 0U && alpha(thinRing, 128, 3) == 255U &&
                alpha(thinRing, 128, 11) == 0U && alpha(ring, 128, 11) == 255U,
            "the thin ring lost its outer edge or is not thinner than the regular ring");
    // A pill is 2:1 and fills its middle.
    const auto pill = RasterizeTouchSkinShape(TouchSkinShape::Pill);
    Require(pill.Height == 128U && alpha(pill, 128, 64) == 255U && alpha(pill, 0, 0) == 0U, "the pill is wrong");
    const auto tab = RasterizeTouchSkinShape(TouchSkinShape::RoundedBox);
    const auto tabRing = RasterizeTouchSkinShape(TouchSkinShape::RoundedBoxRing);
    const auto tabShadow = RasterizeTouchSkinShape(TouchSkinShape::RoundedBoxSoft);
    Require(alpha(tab, 128, 128) == 255U && alpha(tab, 128, 3) == 255U && alpha(tab, 3, 3) == 0U &&
                alpha(tabRing, 128, 128) == 0U && alpha(tabRing, 128, 3) == 255U &&
                alpha(tabShadow, 128, 128) == 255U && alpha(tabShadow, 3, 3) == 0U,
            "rounded tabs lost their body, open rim or rounded corners");
}

// Button prompts: the pad's family from its SDL type (and vendor), the
// device used last, and the user's fixed choice.
void TestButtonPrompts() {
    using namespace ThreeDsRecomp::Input;
    const auto family = [](ControllerType type, std::uint16_t vendor = 0, std::uint16_t product = 0,
                           std::string_view name = {}, bool deck = false) {
        return PromptSetForController({type, vendor, product, name}, deck);
    };
    Require(family(ControllerType::Xbox360) == PromptSet::Xbox && family(ControllerType::XboxOne) == PromptSet::Xbox,
            "Xbox types");
    Require(family(ControllerType::PS3) == PromptSet::PlayStation && family(ControllerType::PS4) == PromptSet::PlayStation &&
                family(ControllerType::PS5) == PromptSet::PlayStation,
            "PlayStation types");
    Require(family(ControllerType::NintendoSwitchPro) == PromptSet::Nintendo &&
                family(ControllerType::NintendoJoyConLeft) == PromptSet::Nintendo &&
                family(ControllerType::NintendoJoyConRight) == PromptSet::Nintendo &&
                family(ControllerType::NintendoJoyConPair) == PromptSet::Nintendo,
            "Nintendo types");
    Require(family(ControllerType::AmazonLuna) == PromptSet::Xbox && family(ControllerType::GoogleStadia) == PromptSet::Xbox &&
                family(ControllerType::NvidiaShield) == PromptSet::Xbox,
            "A B X Y in Xbox places");
    // A type SDL does not know: the vendor, then labels by position.
    Require(family(ControllerType::Unknown) == PromptSet::Generic &&
                family(ControllerType::Unknown, 0x1234, 0x5678, "Some Pad") == PromptSet::Generic &&
                family(ControllerType::Virtual) == PromptSet::Generic,
            "unknown pads are generic");
    Require(family(ControllerType::Unknown, kUsbVendorSony) == PromptSet::PlayStation &&
                family(ControllerType::Virtual, kUsbVendorNintendo) == PromptSet::Nintendo &&
                family(ControllerType::Unknown, kUsbVendorMicrosoft) == PromptSet::Xbox,
            "unknown types fall back on the vendor");
    // Steam's virtual gamepad (an Xbox 360 type to SDL): Steam Deck labels in
    // a Steam Deck session, Xbox elsewhere; the Deck's own controls.
    Require(family(ControllerType::Xbox360, kUsbVendorValve, kValveSteamVirtualGamepad, "Steam Virtual Gamepad") ==
                    PromptSet::Xbox &&
                family(ControllerType::Xbox360, kUsbVendorValve, kValveSteamVirtualGamepad, "Steam Virtual Gamepad",
                       true) == PromptSet::SteamDeck,
            "the Steam virtual gamepad follows SteamDeck=1");
    Require(family(ControllerType::Xbox360, 0, 0, "steam virtual gamepad", true) == PromptSet::SteamDeck &&
                family(ControllerType::Unknown, kUsbVendorValve, kValveSteamDeckControls) == PromptSet::SteamDeck &&
                family(ControllerType::Unknown, 0, 0, "Steam Deck") == PromptSet::SteamDeck,
            "the Steam Deck's own controls");
    Require(family(ControllerType::PS5, kUsbVendorSony, 0x0CE6, "DualSense Wireless Controller", true) ==
                PromptSet::PlayStation,
            "a pad Steam describes keeps its own family on a Deck");
    const auto setSteamDeck = [](const char* value) {
#if defined(_WIN32)
        _putenv_s("SteamDeck", value);
#else
        if (*value == '\0') unsetenv("SteamDeck"); else setenv("SteamDeck", value, 1);
#endif
    };
    setSteamDeck("1");
    Require(IsSteamDeckSession(), "SteamDeck=1 is a Steam Deck session");
    setSteamDeck("0");
    Require(!IsSteamDeckSession(), "SteamDeck=0 is not");
    setSteamDeck("");
    Require(!IsSteamDeckSession(), "no SteamDeck variable is not");

    // Names and the persisted choice.
    for (std::size_t index = 0; index < kButtonPromptsCount; ++index) {
        const auto value = static_cast<ButtonPrompts>(index);
        ButtonPrompts parsed = ButtonPrompts::Automatic;
        Require(ParseButtonPrompts(ButtonPromptsName(value), &parsed) && parsed == value, "button prompts round trip");
    }
    ButtonPrompts ignored{};
    Require(!ParseButtonPrompts("dreamcast", &ignored) && !ParseButtonPrompts("Xbox", &ignored),
            "unknown button prompts rejected");
    Require(std::string_view(PromptSetLabel(PromptSet::PlayStation)) == "PlayStation" &&
                std::string_view(ButtonPromptsLabel(ButtonPrompts::SteamDeck)) == "Steam Deck" &&
                std::string_view(ButtonPromptsName(ButtonPrompts::SteamDeck)) == "steam_deck",
            "prompt labels");

    // The device used last.
    DeviceDescriptor xbox;
    xbox.InstanceId = 3;
    xbox.Selected = true;
    xbox.Prompts = PromptSet::Xbox;
    DeviceDescriptor dualSense = xbox;
    dualSense.InstanceId = 7;
    dualSense.Prompts = PromptSet::PlayStation;
    const auto resolve = [](const PromptDeviceTracker& tracker, const DeviceDescriptor* pad,
                            PromptPlatform platform = PromptPlatform::Desktop) {
        return ResolvePromptSet(ButtonPrompts::Automatic, tracker.LastUsed(), pad, platform);
    };
    PromptDeviceTracker tracker;
    Require(resolve(tracker, nullptr) == PromptSet::Keyboard && resolve(tracker, &xbox) == PromptSet::Xbox &&
                resolve(tracker, &xbox, PromptPlatform::Touch) == PromptSet::Touch &&
                resolve(tracker, nullptr, PromptPlatform::Touch) == PromptSet::Touch,
            "before any input: a desktop's pad or keyboard, a phone's touch");
    tracker.Observe(InputActivity::Keyboard, 1.0);
    Require(resolve(tracker, &xbox) == PromptSet::Keyboard, "a key press shows the keyboard with a pad connected");
    tracker.Observe(InputActivity::Controller, 2.0);
    Require(resolve(tracker, &xbox) == PromptSet::Xbox, "a pad press shows the pad");
    // Two pads: the selected one gives the family.
    Require(resolve(tracker, &dualSense) == PromptSet::PlayStation, "the selected pad's family");
    // Disconnected: the platform's own; reconnected: the pad again.
    Require(resolve(tracker, nullptr) == PromptSet::Keyboard &&
                resolve(tracker, nullptr, PromptPlatform::Touch) == PromptSet::Touch,
            "the used pad gone");
    Require(resolve(tracker, &xbox) == PromptSet::Xbox, "the pad back");
    tracker.Observe(InputActivity::None, 3.0);
    Require(tracker.LastUsed() == InputActivity::Controller, "no activity changes nothing");
    tracker.Observe(InputActivity::Touch, 4.0);
    Require(resolve(tracker, &xbox) == PromptSet::Touch, "a touch shows touch");
    tracker.Observe(InputActivity::Mouse, 4.5);
    Require(resolve(tracker, &xbox) == PromptSet::Touch, "the pointer a touch moved is not a mouse");
    tracker.Observe(InputActivity::Mouse, 4.0 + PromptDeviceTracker::kMouseAfterTouchSeconds + 0.01);
    Require(resolve(tracker, &xbox) == PromptSet::Keyboard, "a real mouse later shows the keyboard's prompts");
    tracker.Observe(InputActivity::Keyboard, 6.0);
    tracker.Observe(InputActivity::Touch, 6.0);
    Require(resolve(tracker, nullptr) == PromptSet::Touch, "touch observed last in a poll wins");
    // A fixed choice shows its family whatever is used.
    for (std::size_t index = 1; index < kButtonPromptsCount; ++index) {
        const auto fixed = static_cast<ButtonPrompts>(index);
        Require(ResolvePromptSet(fixed, InputActivity::Touch, &xbox, PromptPlatform::Touch) ==
                        static_cast<PromptSet>(index - 1U) &&
                    ResolvePromptSet(fixed, InputActivity::None, nullptr, PromptPlatform::Desktop) ==
                        static_cast<PromptSet>(index - 1U),
                "a fixed choice");
    }
}


// The input device log for support reports: no serial number, no user name,
// one summary line, a line per pad and the keyboard; written at startup and
// when the pads or the selection change.
void TestInputDeviceLog() {
    using namespace ThreeDsRecomp::Input;
    DeviceDescriptor pad;
    pad.InstanceId = 3;
    pad.Guid = "030000005e040000130b000011050000";
    pad.Name = "Xander's \"Xbox\" Controller\n";
    pad.Serial = "SN-123456";
    pad.Selected = true;
    pad.Type = ControllerType::XboxOne;
    pad.Prompts = PromptSet::Xbox;
    pad.HasRumble = true;
    pad.HasGyroscope = false;
    KeyboardDescription keyboard;
    keyboard.VideoDriver = "wayland";
    keyboard.WindowFocused = true;
    const std::vector<DeviceDescriptor> pads{pad};
    const auto lines = DescribeInputDevices("startup", pads, keyboard, "xander");
    Require(lines.size() == 3U, "summary, pad and keyboard lines");
    Require(lines[0] == "TRIAEVUM_INPUT_DEVICES reason=startup pads=1 selected=3 video_driver=wayland",
            "summary line");
    Require(lines[1] == "TRIAEVUM_INPUT_PAD id=3 selected=1 name=\"<user>'s ?Xbox? Controller?\" type=xbox_one "
                        "guid=030000005e040000130b000011050000 prompts=xbox gyro=0 accel=0 touchpads=0 rumble=1 "
                        "serial=present",
            "pad line");
    Require(lines[2] == "TRIAEVUM_INPUT_KEYBOARD enabled=1 focus=1 prompts=keyboard", "keyboard line");
    for (const auto& line : lines)
        Require(line.find("SN-123456") == std::string::npos && line.find("Xander") == std::string::npos,
                "a serial number or the user name was logged");

    InputDeviceLog log;
    Require(log.Observe(pads, keyboard).front().find("reason=startup") != std::string::npos, "first log is startup");
    Require(log.Observe(pads, keyboard).empty(), "an unchanged device list was logged again");
    keyboard.WindowFocused = false;
    Require(log.Observe(pads, keyboard).empty(), "a focus change was logged as a device change");
    auto second = pads;
    second.push_back(pad);
    second[1].InstanceId = 4;
    second[1].Selected = false;
    const auto plugged = log.Observe(second, keyboard);
    Require(!plugged.empty() && plugged.front().find("reason=change pads=2") != std::string::npos, "hot-plug");
    second[0].Selected = false;
    second[1].Selected = true;
    Require(!log.Observe(second, keyboard).empty(), "a selection change was not logged");
    Require(log.Observe({}, keyboard).front().find("pads=0 selected=-1") != std::string::npos, "unplug");
    Require(std::string_view(ControllerTypeName(ControllerType::PS5)) == "ps5" &&
                std::string_view(ControllerTypeName(static_cast<ControllerType>(200))) == "unknown",
            "controller type names");
}


int main() try {
    TestButtonPrompts();
    TestInputDeviceLog();
    TestAutomaticMotionComposition();
    TestPointerPressLatch();
    TestVirtualPad();
    TestTouchCameraTracker();
    TestTouchSkin();
    TestTouchAnchors();
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

    {
      // A click on a native page is its touch only; the press stays the
      // pointer's until released, whatever opens or closes meanwhile.
      PointerButtonOwnership ownership;
      Require(!ownership.Update(false, false) && !ownership.Update(false, true),
              "a gameplay click lost its button binding");
      Require(ownership.Update(true, true), "a page opened under a held button left it bound");
      Require(ownership.Update(false, true), "a page closed under a held click released it as a binding");
      Require(!ownership.Update(false, false), "a released click stayed the pointer's in gameplay");
      Require(ownership.Update(true, false) && ownership.Update(true, true),
              "a click on a native page was not the pointer's");
      Require(PointerButtonOwnership::OwnsBinding(MouseButton::Left, true) &&
                  !PointerButtonOwnership::OwnsBinding(MouseButton::Right, true) &&
                  !PointerButtonOwnership::OwnsBinding(MouseButton::Back, true) &&
                  !PointerButtonOwnership::OwnsBinding(MouseButton::Left, false),
              "pointer ownership took a button other than the left one");
    }

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

    {
        // R3 alone toggles the menu on release; L3+R3 toggles F1 once and
        // never the menu, whichever click comes first.
        PadMenuChord chord;
        Require(!chord.Update(false, true).ToggleMenu, "R3 must act on release");
        Require(chord.Update(false, false).ToggleMenu, "R3 release did not toggle the menu");
        auto requests = chord.Update(true, false);
        Require(!requests.ToggleSettings && !requests.ToggleMenu, "L3 alone acted");
        requests = chord.Update(true, true);
        Require(requests.ToggleSettings && !requests.ToggleMenu, "L3 then R3 did not toggle F1");
        Require(!chord.Update(true, true).ToggleSettings, "a held chord toggled F1 twice");
        requests = chord.Update(false, true);
        Require(!requests.ToggleSettings && !requests.ToggleMenu, "releasing L3 first acted");
        Require(!chord.Update(false, false).ToggleMenu, "R3 released after a chord opened the menu");
        Require(!chord.Update(false, true).ToggleMenu && chord.Update(true, true).ToggleSettings,
                "R3 then L3 did not toggle F1");
        Require(!chord.Update(false, false).ToggleMenu, "R3 then L3 opened the menu on release");
    }

    const auto belowDeadZone = ConvertHostStickToNative(5000, 5000, 25);
    const auto nearAxis = ConvertHostStickToNative(32767, 9000, 25);
    const auto squareCorner = ConvertHostStickToNative(32767, -32768, 25);
    const auto cornerLength = std::hypot(
        static_cast<double>(squareCorner.X),
        static_cast<double>(squareCorner.Y));
    Require(belowDeadZone.X == 0 && belowDeadZone.Y == 0 &&
                nearAxis.X > 140 && nearAxis.Y > 0 &&
                squareCorner.X == -squareCorner.Y &&
                cornerLength <= kNativeStickMaximum + 0.5 &&
                cornerLength >= kNativeStickMaximum - 1.0,
            "circle pad dead zone is not radial");
    for (int degrees = 0; degrees < 360; degrees += 5) {
        const double radians = degrees * 3.14159265358979323846 / 180.0;
        const auto full = ConvertHostStickToNative(
            static_cast<std::int16_t>(std::lround(32767.0 * std::cos(radians))),
            static_cast<std::int16_t>(std::lround(32767.0 * std::sin(radians))),
            18);
        const auto length = std::hypot(static_cast<double>(full.X),
                                       static_cast<double>(full.Y));
        Require(length >= kNativeStickMaximum - 1.0 &&
                    length <= kNativeStickMaximum + 1.0,
                "full circle pad deflection lost range at some angle");
    }

    return 0;
} catch (const std::exception& error) {
    std::cerr << "three_ds_recomp_input_tests: " << error.what() << '\n';
    return 1;
}
