#include "oot3d_native_a32_input.h"

#include "oot3d/renderer/ui_presentation_layout.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

namespace Oot3dNativeGame {
namespace {

constexpr const char* kInputTimelineSchema =
    "oot3d.native_game.input_timeline.v1";
constexpr double kCirclePadMaximum = 154.0;

struct NativeControlActionRoute {
    bool IsDigital = true;
    ThreeDsRecomp::Input::DigitalControl Digital =
        ThreeDsRecomp::Input::DigitalControl::A;
    NativeA32TopScreenPage Page = NativeA32TopScreenPage::None;
};

using ThreeDsRecomp::Input::DigitalControl;
constexpr std::array<NativeControlActionRoute, kNativeControlActionCount>
    kNativeControlActionRoutes{{
        {true, DigitalControl::CirclePadUp},
        {true, DigitalControl::CirclePadDown},
        {true, DigitalControl::CirclePadLeft},
        {true, DigitalControl::CirclePadRight},
        {true, DigitalControl::A},
        {true, DigitalControl::B},
        {true, DigitalControl::X},
        {true, DigitalControl::Y},
        {true, DigitalControl::L},
        {true, DigitalControl::R},
        {true, DigitalControl::Zl},
        {true, DigitalControl::Zr},
        {true, DigitalControl::Select},
        {true, DigitalControl::Start},
        {true, DigitalControl::DpadUp},
        {true, DigitalControl::DpadDown},
        {true, DigitalControl::DpadLeft},
        {true, DigitalControl::DpadRight},
        {false, DigitalControl::A, NativeA32TopScreenPage::Gear},
        {false, DigitalControl::A, NativeA32TopScreenPage::Map},
        {false, DigitalControl::A, NativeA32TopScreenPage::Items},
        {true, DigitalControl::CStickUp},
        {true, DigitalControl::CStickDown},
        {true, DigitalControl::CStickLeft},
        {true, DigitalControl::CStickRight},
    }};

static_assert(kNativeControlActionRoutes.size() ==
              kNativeControlActionCount);

uint32_t ReadFrame(const nlohmann::json& segment, const char* key) {
    if (!segment.contains(key) || !segment.at(key).is_number_unsigned()) {
        throw std::runtime_error(
            std::string("native input timeline segment requires unsigned ") +
            key);
    }
    const uint64_t value = segment.at(key).get<uint64_t>();
    if (value > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error(std::string("native input timeline ") + key +
                                 " exceeds u32 range");
    }
    return static_cast<uint32_t>(value);
}

int16_t ReadCircleAxis(const nlohmann::json& segment, const char* key) {
    if (!segment.contains(key)) {
        return 0;
    }
    if (!segment.at(key).is_number()) {
        throw std::runtime_error(std::string("native input timeline ") + key +
                                 " must be numeric");
    }
    const double value = segment.at(key).get<double>();
    if (!std::isfinite(value) || value < -1.0 || value > 1.0) {
        throw std::runtime_error(std::string("native input timeline ") + key +
                                 " must be in [-1, 1]");
    }
    return static_cast<int16_t>(std::lround(value * kCirclePadMaximum));
}

uint32_t ButtonMask(std::string_view name) {
    constexpr std::array<std::pair<std::string_view, NativeA32HidButton>, 16>
        buttons{{
            {"a", NativeA32HidButton::A},
            {"b", NativeA32HidButton::B},
            {"select", NativeA32HidButton::Select},
            {"start", NativeA32HidButton::Start},
            {"right", NativeA32HidButton::DpadRight},
            {"left", NativeA32HidButton::DpadLeft},
            {"up", NativeA32HidButton::DpadUp},
            {"down", NativeA32HidButton::DpadDown},
            {"r", NativeA32HidButton::R},
            {"l", NativeA32HidButton::L},
            {"x", NativeA32HidButton::X},
            {"y", NativeA32HidButton::Y},
            {"debug", NativeA32HidButton::Debug},
            {"gpio14", NativeA32HidButton::Gpio14},
            {"zl", NativeA32HidButton::Zl},
            {"zr", NativeA32HidButton::Zr},
        }};
    for (const auto& [candidate, button] : buttons) {
        if (candidate == name) {
            return NativeA32HidButtonMask(button);
        }
    }
    throw std::runtime_error("native input timeline has unknown button: " +
                             std::string(name));
}

uint32_t ReadButtons(const nlohmann::json& segment) {
    if (!segment.contains("buttons")) {
        return 0;
    }
    const auto& buttons = segment.at("buttons");
    if (!buttons.is_array()) {
        throw std::runtime_error(
            "native input timeline buttons must be an array");
    }
    uint32_t mask = 0;
    for (const auto& button : buttons) {
        if (!button.is_string()) {
            throw std::runtime_error(
                "native input timeline button names must be strings");
        }
        mask |= ButtonMask(button.get<std::string>());
    }
    return mask;
}

bool ReadOptionalBool(const nlohmann::json& segment, const char* key) {
    if (!segment.contains(key)) {
        return false;
    }
    if (!segment.at(key).is_boolean()) {
        throw std::runtime_error(std::string("native input timeline ") + key +
                                 " must be boolean");
    }
    return segment.at(key).get<bool>();
}

bool ReadMotionVector(const nlohmann::json& segment, const char* key,
                      std::array<float, 3>& output) {
    if (!segment.contains(key)) return false;
    const auto& values = segment.at(key);
    if (!values.is_array() || values.size() != output.size())
        throw std::runtime_error(std::string("native input timeline requires a 3-axis ") + key);
    for (size_t axis = 0; axis < output.size(); ++axis) {
        if (!values[axis].is_number())
            throw std::runtime_error(std::string("native input timeline requires numeric ") + key);
        const auto value = values[axis].get<float>();
        if (!std::isfinite(value))
            throw std::runtime_error(std::string("native input timeline requires finite ") + key);
        output[axis] = value;
    }
    return true;
}

void ReadTouch(const nlohmann::json& segment, NativeA32HidState& state) {
    if (!segment.contains("touch")) {
        return;
    }
    const auto& touch = segment.at("touch");
    if (!touch.is_object() || !touch.contains("pressed") ||
        !touch.at("pressed").is_boolean()) {
        throw std::runtime_error(
            "native input timeline touch requires boolean pressed");
    }
    state.TouchPressed = touch.at("pressed").get<bool>();
    if (!state.TouchPressed) {
        return;
    }
    if (!touch.contains("x") || !touch.at("x").is_number_unsigned() ||
        !touch.contains("y") || !touch.at("y").is_number_unsigned()) {
        throw std::runtime_error(
            "native input timeline pressed touch requires unsigned x/y");
    }
    const uint64_t x = touch.at("x").get<uint64_t>();
    const uint64_t y = touch.at("y").get<uint64_t>();
    if (x >= 320U || y >= 240U) {
        throw std::runtime_error(
            "native input timeline touch is outside the 320x240 lower screen");
    }
    state.TouchX = static_cast<uint16_t>(x);
    state.TouchY = static_cast<uint16_t>(y);
}

bool ActionHeld(const NativeControlHostInputState& host,
                NativeControlAction action) noexcept {
    return host.Actions[static_cast<size_t>(action)];
}

ThreeDsRecomp::Input::DigitalState BuildThreeDsDigitalState(
    const NativeControlHostInputState& host) noexcept {
    ThreeDsRecomp::Input::DigitalState state;
    for (std::size_t index = 0; index < kNativeControlActionCount; ++index) {
        const auto action = static_cast<NativeControlAction>(index);
        DigitalControl control;
        if (MapNativeControlActionToThreeDsControl(action, &control)) {
            state.SetHeld(control, ActionHeld(host, action));
        }
    }
    return state;
}

ThreeDsRecomp::Input::MappingConfig BuildThreeDsMappingConfig(
    const NativeControlConfig& config) noexcept {
    return {
        config.MovementStick,
        config.MovementStickDeadZonePercent,
        config.LookStickDeadZonePercent,
        config.NativeAimSource,
        config.FreeCameraSource,
        config.MouseAimDegreesPerPixel,
        config.MouseFreeCameraUnitsPerPixel,
        config.RightStickAimMaximumDegreesPerSecond,
        config.ControllerGyroscopeSensitivity,
        config.ControllerAccelerometerSensitivity,
        config.FreeCameraMotionSensitivity,
        config.NativeAimInvertX,
        config.NativeAimInvertY,
        config.GyroscopeBiasDegreesPerSecond,
        config.AccelerometerNeutral,
    };
}

ThreeDsRecomp::Input::AimTransform BuildThreeDsAimTransform(
    const NativeAimProfileTransform& transform) noexcept {
    return {
        transform.RightStickScale,
        transform.RightStickSmoothingCoefficient,
        transform.RightStickInvertX,
        transform.RightStickInvertY,
    };
}

} // namespace

bool MapNativeControlActionToThreeDsControl(
    NativeControlAction action, DigitalControl* control) noexcept {
    const auto index = static_cast<std::size_t>(action);
    if (control == nullptr || index >= kNativeControlActionRoutes.size() ||
        !kNativeControlActionRoutes[index].IsDigital) {
        return false;
    }
    *control = kNativeControlActionRoutes[index].Digital;
    return true;
}

NativeA32TopScreenPage ResolveNativeControlPageShortcut(
    const NativeControlHostInputState& host) noexcept {
    for (std::size_t index = 0; index < kNativeControlActionRoutes.size();
         ++index) {
        const auto& route = kNativeControlActionRoutes[index];
        if (!route.IsDigital && route.Page != NativeA32TopScreenPage::None &&
            host.Actions[index]) {
            return route.Page;
        }
    }
    return NativeA32TopScreenPage::None;
}

void ApplyNativeControlShortcutTouch(
    const NativeControlHostInputState& host,
    NativeA32InputFrame& frame) noexcept {
    const auto touch = MapTopScreenPageShortcutToNativeTouch(
        ResolveNativeControlPageShortcut(host));
    if (!touch.Pressed) {
        return;
    }
    frame.Hid.TouchX = touch.X;
    frame.Hid.TouchY = touch.Y;
    frame.Hid.TouchPressed = true;
}

bool ResolveNativeGameplayMouseOwnership(
    bool nativeFrontendTouchEnabled, bool hostGuiVisible,
    bool mouseEnabled, bool sourceUsesMouse) noexcept {
    return ThreeDsRecomp::Input::ResolveGameplayPointerOwnership(
        nativeFrontendTouchEnabled, hostGuiVisible, mouseEnabled,
        sourceUsesMouse);
}

const char* NativeA32InputTimelineFrameOriginName(
    NativeA32InputTimelineFrameOrigin origin) noexcept {
    switch (origin) {
    case NativeA32InputTimelineFrameOrigin::Guest:
        return "guest";
    case NativeA32InputTimelineFrameOrigin::Run:
        return "run";
    }
    return "guest";
}

void ObserveNativeA32PolledButton(
    bool physicalHeld, NativeA32PolledButtonLatch& state) noexcept {
    if (physicalHeld && !state.PhysicalHeld) {
        state.PendingPressed = true;
        ++state.ObservedPresses;
    }
    state.PhysicalHeld = physicalHeld;
}

bool ResolveNativeA32PolledButton(
    bool physicalHeld, NativeA32PolledButtonLatch& state) noexcept {
    if (!state.PendingPressed) {
        return physicalHeld;
    }
    if (!physicalHeld) {
        ++state.RecoveredShortPresses;
    }
    state.PendingPressed = false;
    return true;
}

NativeA32InputFrame MapNativeControlInput(
    const NativeControlConfig& config,
    const NativeControlHostInputState& host,
    const NativeAimProfileTransform& aimTransform,
    NativeRightStickProfileState* rightStickState,
    bool advanceRightStickState) noexcept {
    NativeA32InputFrame frame;
    static_cast<ThreeDsRecomp::Input::InputFrame&>(frame) =
        ThreeDsRecomp::Input::ResolveInput(
            BuildThreeDsMappingConfig(config), host,
            BuildThreeDsDigitalState(host),
            BuildThreeDsAimTransform(aimTransform), rightStickState,
            advanceRightStickState);
    return frame;
}

NativeA32TouchMapping MapHostPointerToNativeA32Touch(
    int32_t pointerX, int32_t pointerY, uint32_t hostWidth,
    uint32_t hostHeight, bool pointerPressed,
    NativeA32TouchPresentation presentation) noexcept {
    const float presentationWidth =
        presentation == NativeA32TouchPresentation::TopScreen400x240
            ? static_cast<float>(NativeA32TopScreenWidth)
            : static_cast<float>(NativeA32TouchWidth);
    const float presentationHeight =
        presentation == NativeA32TouchPresentation::TopScreen400x240
            ? static_cast<float>(NativeA32TopScreenHeight)
            : static_cast<float>(NativeA32TouchHeight);
    const auto logical = Oot3d::Renderer::MapUiPresentationPoint(
        hostWidth, hostHeight, presentationWidth, presentationHeight,
        static_cast<float>(pointerX), static_cast<float>(pointerY));
    return ThreeDsRecomp::Input::MapPresentationPointToTouch(
        logical.X, logical.Y, presentationWidth, presentationHeight,
        logical.Inside, pointerPressed);
}

NativeA32TouchMapping MapTopScreenPageShortcutToNativeTouch(
    NativeA32TopScreenPage page) noexcept {
    NativeA32TouchMapping mapped;
    switch (page) {
    case NativeA32TopScreenPage::Gear:
        mapped.X = 96U;
        break;
    case NativeA32TopScreenPage::Map:
        mapped.X = 160U;
        break;
    case NativeA32TopScreenPage::Items:
        mapped.X = 224U;
        break;
    case NativeA32TopScreenPage::None:
        return mapped;
    }
    mapped.Y = 221U;
    mapped.Inside = true;
    mapped.Pressed = true;
    return mapped;
}

void MarkTopScreenStartOpenGestureConsumed(
    TopScreenStartRoutingState& state) noexcept {
    state.OpenGestureHeld = true;
    state.CloseGestureHeld = false;
}

void MarkTopScreenStartCloseGestureConsumed(
    TopScreenStartRoutingState& state) noexcept {
    state.OpenGestureHeld = false;
    state.CloseGestureHeld = true;
}

void ReleaseTopScreenStartRoutingLatch(
    bool physicalStartHeld, TopScreenStartRoutingState& state) noexcept {
    if (!physicalStartHeld) {
        state.OpenGestureHeld = false;
        state.CloseGestureHeld = false;
    }
}

bool ResolveNativeA32TouchInputEnabled(
    bool topScreenProfile, bool nativeTouchPresentationActive,
    bool nativeFrontendPresentationActive,
    bool nativePausePageActive) noexcept {
    if (!topScreenProfile) {
        return nativeTouchPresentationActive;
    }
    return nativeFrontendPresentationActive || nativePausePageActive;
}

void ApplyTopScreenStartRouting(NativeA32InputFrame& frame,
                                bool topScreenProfile,
                                bool routeGameplayStartToItems,
                                bool nativePauseOpen,
                                bool guestRefreshWillConsume,
                                TopScreenStartRoutingState& state) noexcept {
    if (!guestRefreshWillConsume) {
        return;
    }
    const uint32_t start = NativeA32HidButtonMask(NativeA32HidButton::Start);
    const bool startHeld = (frame.Hid.Buttons & start) != 0U;
    if (!topScreenProfile) {
        state.OpenGestureHeld = false;
        state.CloseGestureHeld = false;
        return;
    }
    if (state.CloseGestureHeld) {
        if (startHeld) {
            frame.Hid.Buttons &= ~start;
            return;
        }
        state.CloseGestureHeld = false;
    }
    if (state.OpenGestureHeld) {
        if (startHeld) {
            frame.Hid.Buttons &= ~start;
            if (!nativePauseOpen && routeGameplayStartToItems) {
                const auto items = MapTopScreenPageShortcutToNativeTouch(
                    NativeA32TopScreenPage::Items);
                frame.Hid.TouchX = items.X;
                frame.Hid.TouchY = items.Y;
                frame.Hid.TouchPressed = items.Pressed;
            }
            return;
        }
        state.OpenGestureHeld = false;
    }
    if (nativePauseOpen || !routeGameplayStartToItems || !startHeld) {
        return;
    }

    frame.Hid.Buttons &= ~start;
    const auto items = MapTopScreenPageShortcutToNativeTouch(
        NativeA32TopScreenPage::Items);
    frame.Hid.TouchX = items.X;
    frame.Hid.TouchY = items.Y;
    frame.Hid.TouchPressed = items.Pressed;
    MarkTopScreenStartOpenGestureConsumed(state);
}

void NativeA32InputDiagnostics::Observe(const NativeA32InputFrame& frame) {
    ++SampledFrameCount;
    ScriptedFrameCount += frame.Scripted ? 1U : 0U;
    ActiveSegmentFrameCount += frame.ScriptSegmentIndex >= 0 ? 1U : 0U;
    ButtonFrameCount += frame.Hid.Buttons != 0U ? 1U : 0U;
    CirclePadFrameCount +=
        frame.Hid.CirclePadX != 0 || frame.Hid.CirclePadY != 0 ? 1U : 0U;
    TouchFrameCount += frame.Hid.TouchPressed ? 1U : 0U;
    LastFrame = frame;
}

NativeA32InputTimeline NativeA32InputTimeline::LoadFile(
    const std::filesystem::path& path) {
    NativeA32InputTimeline timeline;
    if (path.empty()) {
        return timeline;
    }
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to open native input timeline: " +
                                 path.string());
    }
    nlohmann::json root;
    input >> root;
    if (!root.is_object() ||
        root.value("schema", std::string{}) != kInputTimelineSchema) {
        throw std::runtime_error(
            "unsupported native input timeline schema: " + path.string());
    }
    if (!root.contains("segments") || !root.at("segments").is_array() ||
        root.at("segments").empty()) {
        throw std::runtime_error(
            "native input timeline requires at least one segment: " +
            path.string());
    }
    const auto frameOrigin = root.value("frame_origin", std::string{"guest"});
    if (frameOrigin == "guest") {
        timeline.mFrameOrigin = NativeA32InputTimelineFrameOrigin::Guest;
    } else if (frameOrigin == "run") {
        timeline.mFrameOrigin = NativeA32InputTimelineFrameOrigin::Run;
    } else {
        throw std::runtime_error(
            "native input timeline frame_origin must be guest or run: " +
            path.string());
    }

    uint32_t previousEnd = 0;
    bool first = true;
    for (const auto& source : root.at("segments")) {
        if (!source.is_object()) {
            throw std::runtime_error(
                "native input timeline segment must be an object");
        }
        Segment segment;
        segment.StartFrame = ReadFrame(source, "start_frame");
        segment.EndFrameExclusive =
            ReadFrame(source, "end_frame_exclusive");
        if (segment.EndFrameExclusive <= segment.StartFrame) {
            throw std::runtime_error(
                "native input timeline segment has an empty or reversed frame range");
        }
        if (!first && segment.StartFrame < previousEnd) {
            throw std::runtime_error(
                "native input timeline segments must be sorted and non-overlapping");
        }
        segment.Hid.Buttons = ReadButtons(source);
        segment.Hid.CirclePadX = ReadCircleAxis(source, "circle_x");
        segment.Hid.CirclePadY = ReadCircleAxis(source, "circle_y");
        ThreeDsRecomp::Input::SetButtonHeld(
            segment, ThreeDsRecomp::Input::Button::Zr,
            ThreeDsRecomp::Input::IsButtonHeld(segment, ThreeDsRecomp::Input::Button::Zr) ||
            ReadOptionalBool(source, "topscreen_zr"));
        ThreeDsRecomp::Input::SetButtonHeld(
            segment, ThreeDsRecomp::Input::Button::Zl,
            ThreeDsRecomp::Input::IsButtonHeld(segment, ThreeDsRecomp::Input::Button::Zl) ||
            ReadOptionalBool(source, "topscreen_zl"));
        segment.CStick.X = ReadCircleAxis(source, "right_x");
        segment.CStick.Y = ReadCircleAxis(source, "right_y");
        ReadTouch(source, segment.Hid);
        segment.Hid.GyroscopeValid = ReadMotionVector(source, "gyroscope_dps", segment.Hid.GyroscopeDegreesPerSecond);
        segment.Hid.AccelerometerValid = ReadMotionVector(source, "accelerometer_g", segment.Hid.Accelerometer);
        timeline.mSegments.push_back(segment);
        previousEnd = segment.EndFrameExclusive;
        first = false;
    }
    timeline.mSourcePath = std::filesystem::absolute(path).lexically_normal();
    return timeline;
}

bool NativeA32InputTimeline::Enabled() const {
    return !mSourcePath.empty();
}

size_t NativeA32InputTimeline::SegmentCount() const {
    return mSegments.size();
}

const std::filesystem::path& NativeA32InputTimeline::SourcePath() const {
    return mSourcePath;
}

NativeA32InputTimelineFrameOrigin NativeA32InputTimeline::FrameOrigin() const {
    return mFrameOrigin;
}

NativeA32InputFrame NativeA32InputTimeline::Sample(uint32_t guestFrame,
                                                   uint32_t runFrame) const {
    NativeA32InputFrame result;
    result.Scripted = Enabled();
    const uint32_t frame =
        mFrameOrigin == NativeA32InputTimelineFrameOrigin::Run ? runFrame
                                                               : guestFrame;
    for (size_t index = 0; index < mSegments.size(); ++index) {
        const auto& segment = mSegments[index];
        if (frame < segment.StartFrame) {
            break;
        }
        if (frame >= segment.EndFrameExclusive) {
            continue;
        }
        static_cast<ThreeDsRecomp::Input::InputFrame&>(result) =
            static_cast<const ThreeDsRecomp::Input::InputFrame&>(segment);
        result.ScriptSegmentIndex = static_cast<int32_t>(index);
        break;
    }
    return result;
}

} // namespace Oot3dNativeGame
