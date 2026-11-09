#include "three_ds_input.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace ThreeDsRecomp::Input {

namespace {

constexpr std::array<const char*, 7> kPromptSetNames{
    "xbox", "playstation", "nintendo", "steam_deck", "generic", "keyboard", "touch"};
constexpr std::array<const char*, 7> kPromptSetLabels{
    "Xbox", "PlayStation", "Nintendo", "Steam Deck", "Generic", "Keyboard", "Touch"};

bool ContainsNoCase(std::string_view text, std::string_view word) noexcept {
    if (word.empty() || text.size() < word.size()) return word.empty();
    for (std::size_t start = 0; start + word.size() <= text.size(); ++start) {
        bool match = true;
        for (std::size_t index = 0; index < word.size() && match; ++index) {
            match = std::tolower(static_cast<unsigned char>(text[start + index])) ==
                    std::tolower(static_cast<unsigned char>(word[index]));
        }
        if (match) return true;
    }
    return false;
}

} // namespace

const char* PromptSetName(PromptSet set) noexcept {
    const auto index = static_cast<std::size_t>(set);
    return index < kPromptSetNames.size() ? kPromptSetNames[index] : "unknown";
}

const char* PromptSetLabel(PromptSet set) noexcept {
    const auto index = static_cast<std::size_t>(set);
    return index < kPromptSetLabels.size() ? kPromptSetLabels[index] : "Unknown";
}

const char* ButtonPromptsName(ButtonPrompts prompts) noexcept {
    if (prompts == ButtonPrompts::Automatic) return "automatic";
    return PromptSetName(static_cast<PromptSet>(static_cast<std::uint8_t>(prompts) - 1U));
}

const char* ButtonPromptsLabel(ButtonPrompts prompts) noexcept {
    if (prompts == ButtonPrompts::Automatic) return "Automatic";
    return PromptSetLabel(static_cast<PromptSet>(static_cast<std::uint8_t>(prompts) - 1U));
}

bool ParseButtonPrompts(std::string_view value, ButtonPrompts* prompts) noexcept {
    if (prompts == nullptr) return false;
    for (std::size_t index = 0; index < kButtonPromptsCount; ++index) {
        const auto candidate = static_cast<ButtonPrompts>(index);
        if (value == ButtonPromptsName(candidate)) {
            *prompts = candidate;
            return true;
        }
    }
    return false;
}

PromptSet PromptSetForController(const ControllerIdentity& controller, bool steamDeckSession) noexcept {
    if (controller.Vendor == kUsbVendorValve || ContainsNoCase(controller.Name, "steam virtual gamepad") ||
        ContainsNoCase(controller.Name, "steam deck")) {
        if (controller.Product == kValveSteamDeckControls || ContainsNoCase(controller.Name, "steam deck"))
            return PromptSet::SteamDeck;
        // The virtual gamepad, and Valve's other pads (A B X Y in Xbox places).
        return steamDeckSession ? PromptSet::SteamDeck : PromptSet::Xbox;
    }
    switch (controller.Type) {
    case ControllerType::Xbox360:
    case ControllerType::XboxOne:
    // A B X Y where an Xbox pad has them.
    case ControllerType::AmazonLuna:
    case ControllerType::GoogleStadia:
    case ControllerType::NvidiaShield:
        return PromptSet::Xbox;
    case ControllerType::PS3:
    case ControllerType::PS4:
    case ControllerType::PS5:
        return PromptSet::PlayStation;
    case ControllerType::NintendoSwitchPro:
    case ControllerType::NintendoJoyConLeft:
    case ControllerType::NintendoJoyConRight:
    case ControllerType::NintendoJoyConPair:
        return PromptSet::Nintendo;
    case ControllerType::Unknown:
    case ControllerType::Virtual:
        break;
    }
    switch (controller.Vendor) {
    case kUsbVendorMicrosoft: return PromptSet::Xbox;
    case kUsbVendorSony: return PromptSet::PlayStation;
    case kUsbVendorNintendo: return PromptSet::Nintendo;
    default: return PromptSet::Generic;
    }
}

bool IsSteamDeckSession() noexcept {
    const char* value = std::getenv("SteamDeck");
    return value != nullptr && std::string_view(value) == "1";
}

void PromptDeviceTracker::Observe(InputActivity activity, double nowSeconds) noexcept {
    switch (activity) {
    case InputActivity::None:
        return;
    case InputActivity::Touch:
        mLastTouchSeconds = nowSeconds;
        break;
    case InputActivity::Mouse:
        if (nowSeconds - mLastTouchSeconds < kMouseAfterTouchSeconds) return;
        break;
    case InputActivity::Keyboard:
    case InputActivity::Controller:
        break;
    }
    mLast = activity;
}

PromptSet ResolvePromptSet(ButtonPrompts setting, InputActivity lastUsed, const DeviceDescriptor* selectedPad,
                           PromptPlatform platform) noexcept {
    if (setting != ButtonPrompts::Automatic)
        return static_cast<PromptSet>(static_cast<std::uint8_t>(setting) - 1U);
    const PromptSet platformSet = platform == PromptPlatform::Touch ? PromptSet::Touch : PromptSet::Keyboard;
    switch (lastUsed) {
    case InputActivity::Keyboard:
    case InputActivity::Mouse:
        return PromptSet::Keyboard;
    case InputActivity::Touch:
        return PromptSet::Touch;
    case InputActivity::Controller:
        return selectedPad != nullptr ? selectedPad->Prompts : platformSet;
    case InputActivity::None:
        break;
    }
    if (platform == PromptPlatform::Desktop && selectedPad != nullptr) return selectedPad->Prompts;
    return platformSet;
}

double ConsumeInputPeriod(double pollSeconds, bool consume, double& pendingSeconds) noexcept {
    const double elapsed = std::isfinite(pollSeconds) ? std::clamp(pollSeconds, 0.0, 0.25) : 0.0;
    pendingSeconds = std::min(0.25, pendingSeconds + elapsed);
    const double period = std::max(0.001, consume ? pendingSeconds : elapsed);
    if (consume) pendingSeconds = 0.0;
    return period;
}

MotionSource NormalizeCameraSource(MotionSource source) noexcept {
    switch (source) {
    case MotionSource::ControllerGyroscope:
    case MotionSource::ControllerAccelerometer:
    case MotionSource::ControllerMotion:
        return MotionSource::RightStick;
    default:
        return source;
    }
}

// Adapted from encounter/aurora lib/input.cpp (MIT, Luke Street).
// Pinned provenance and retained license: LICENSES/Aurora-MIT.txt.
std::string NormalizeControllerSerial(std::string_view serial) {
    std::string result;
    for (char c : serial) {
        if (c == ':' || c == '-') continue;
        result += c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    }
    return result;
}

bool MatchesController(const DeviceDescriptor& device, std::string_view guid,
                       std::string_view serial) {
    return (guid.empty() || device.Guid == guid) &&
        (serial.empty() || (!device.Serial.empty() &&
         NormalizeControllerSerial(serial) == NormalizeControllerSerial(device.Serial)));
}

std::int32_t SelectControllerDevice(std::span<const DeviceDescriptor> devices,
    std::string_view guid, std::string_view serial, std::int32_t previousInstance) {
    // Among the matching devices of the highest rank: the attached active
    // device, else the lowest instance. With equal ranks (every platform but
    // Android) this is the previous rule exactly.
    std::int32_t selected = -1;
    std::int32_t bestRank = 0;
    bool keptPrevious = false;
    for (const auto& device : devices) {
        if (device.InstanceId < 0 || !MatchesController(device, guid, serial)) continue;
        if (selected < 0 || device.Rank > bestRank) {
            selected = device.InstanceId;
            bestRank = device.Rank;
            keptPrevious = device.InstanceId == previousInstance;
            continue;
        }
        if (device.Rank < bestRank || keptPrevious) continue;
        if (device.InstanceId == previousInstance) {
            selected = previousInstance;
            keptPrevious = true;
        } else if (device.InstanceId < selected) {
            selected = device.InstanceId;
        }
    }
    return selected;
}

namespace {

bool FiniteVector(const std::array<float, 3>& value) noexcept {
    return std::all_of(value.begin(), value.end(), [](float v) { return std::isfinite(v); });
}

} // namespace

MotionObservation ConvertSdlMotion(const std::array<float, 3>& accel, bool accelValid,
                                   const std::array<float, 3>& gyro, bool gyroValid) noexcept {
    // Citra/Azahar SDL sensor mapping, GPL-2.0-or-later; see THIRD_PARTY_NOTICES.md.
    MotionObservation result;
    constexpr float gravity = 9.80665F;
    constexpr float toDegrees = 57.2957795130823208768F;
    if (accelValid && FiniteVector(accel)) {
        result.Accelerometer = {accel[0] / gravity, -accel[1] / gravity, accel[2] / gravity};
        result.AccelerometerValid = FiniteVector(result.Accelerometer);
    }
    if (gyroValid && FiniteVector(gyro)) {
        result.GyroscopeDegreesPerSecond = {-gyro[0] * toDegrees, gyro[1] * toDegrees, -gyro[2] * toDegrees};
        result.GyroscopeValid = FiniteVector(result.GyroscopeDegreesPerSecond);
    }
    return result;
}

void MotionCalibrationAccumulator::Reset() noexcept { *this = {}; }

bool MotionCalibrationAccumulator::Observe(const MotionObservation& observation) noexcept {
    const auto magnitudeSquared = [](const auto& vector) {
        return vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2];
    };
    const bool gyro = observation.GyroscopeValid;
    const bool accel = observation.AccelerometerValid;
    if (!gyro && !accel) return false;
    // Manual calibration only. Reject motion/free fall, not ordinary gameplay
    // samples. Low-rate intentional rotation cannot be distinguished from bias.
    const float gravitySquared = magnitudeSquared(observation.Accelerometer);
    if ((gyro && (!FiniteVector(observation.GyroscopeDegreesPerSecond) ||
                  magnitudeSquared(observation.GyroscopeDegreesPerSecond) > 64.0F)) ||
        (accel && (!FiniteVector(observation.Accelerometer) ||
                   gravitySquared < 0.64F || gravitySquared > 1.44F))) {
        mGyroscopeSum = {}; mAccelerometerSum = {};
        mGyroscopeCount = mAccelerometerCount = 0;
        mWaitingForStillness = true;
        return false;
    }
    mWaitingForStillness = false;
    const auto fresh = [](std::uint64_t timestamp, std::uint64_t& last) {
        if (timestamp != 0 && timestamp <= last) return false;
        last = timestamp;
        return true;
    };
    if (gyro && fresh(observation.GyroscopeTimestampMicroseconds, mGyroscopeTimestamp)) {
        for (std::size_t i = 0; i < 3; ++i) mGyroscopeSum[i] += observation.GyroscopeDegreesPerSecond[i];
        ++mGyroscopeCount;
    }
    if (accel && fresh(observation.AccelerometerTimestampMicroseconds, mAccelerometerTimestamp)) {
        for (std::size_t i = 0; i < 3; ++i) mAccelerometerSum[i] += observation.Accelerometer[i];
        ++mAccelerometerCount;
    }
    return SamplesCollected() >= SamplesRequired;
}

std::uint32_t MotionCalibrationAccumulator::SamplesCollected() const noexcept {
    return mGyroscopeCount && mAccelerometerCount ? std::min(mGyroscopeCount, mAccelerometerCount)
                                                 : std::max(mGyroscopeCount, mAccelerometerCount);
}

MotionObservation MotionCalibrationAccumulator::Mean() const noexcept {
    MotionObservation result;
    result.GyroscopeValid = mGyroscopeCount != 0;
    result.AccelerometerValid = mAccelerometerCount != 0;
    for (std::size_t i = 0; i < 3; ++i) {
        if (mGyroscopeCount) result.GyroscopeDegreesPerSecond[i] = static_cast<float>(mGyroscopeSum[i] / mGyroscopeCount);
        if (mAccelerometerCount) result.Accelerometer[i] = static_cast<float>(mAccelerometerSum[i] / mAccelerometerCount);
    }
    return result;
}

TouchMapping MapNormalizedTouch(const NormalizedTouch& touch) noexcept {
    if (!touch.Pressed || !std::isfinite(touch.X) || !std::isfinite(touch.Y) ||
        touch.X < 0.0F || touch.X > 1.0F || touch.Y < 0.0F || touch.Y > 1.0F) return {};
    return {static_cast<std::uint16_t>(std::min(touch.X * kTouchWidth, float(kTouchWidth - 1))),
            static_cast<std::uint16_t>(std::min(touch.Y * kTouchHeight, float(kTouchHeight - 1))), true, true};
}

namespace {

template <typename Enum>
struct NamedValue {
    Enum Value;
    std::string_view Name;
};

template <typename Enum, std::size_t Size>
const char* NameOf(
    Enum value,
    const std::array<NamedValue<Enum>, Size>& values) noexcept {
    for (const auto& candidate : values) {
        if (candidate.Value == value) {
            return candidate.Name.data();
        }
    }
    return "unknown";
}

template <typename Enum, std::size_t Size>
bool ParseNamed(
    std::string_view value, Enum* output,
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

constexpr std::array<NamedValue<HardwareProfile>, 3> kHardwareProfiles{{
    {HardwareProfile::Old3ds, "old_3ds"},
    {HardwareProfile::Old3dsCirclePadPro, "old_3ds_circle_pad_pro"},
    {HardwareProfile::New3ds, "new_3ds"},
}};

constexpr std::array<NamedValue<DigitalControl>, kDigitalControlCount>
    kDigitalControls{{
        {DigitalControl::CirclePadUp, "circle_pad_up"},
        {DigitalControl::CirclePadDown, "circle_pad_down"},
        {DigitalControl::CirclePadLeft, "circle_pad_left"},
        {DigitalControl::CirclePadRight, "circle_pad_right"},
        {DigitalControl::A, "a"},
        {DigitalControl::B, "b"},
        {DigitalControl::X, "x"},
        {DigitalControl::Y, "y"},
        {DigitalControl::L, "l"},
        {DigitalControl::R, "r"},
        {DigitalControl::Zl, "zl"},
        {DigitalControl::Zr, "zr"},
        {DigitalControl::Select, "select"},
        {DigitalControl::Start, "start"},
        {DigitalControl::DpadUp, "dpad_up"},
        {DigitalControl::DpadDown, "dpad_down"},
        {DigitalControl::DpadLeft, "dpad_left"},
        {DigitalControl::DpadRight, "dpad_right"},
        {DigitalControl::CStickUp, "c_stick_up"},
        {DigitalControl::CStickDown, "c_stick_down"},
        {DigitalControl::CStickLeft, "c_stick_left"},
        {DigitalControl::CStickRight, "c_stick_right"},
    }};

constexpr std::array<NamedValue<ControlProfile>, 4> kControlProfiles{{
    {ControlProfile::Keyboard, "keyboard"},
    {ControlProfile::KeyboardMouse, "keyboard_mouse"},
    {ControlProfile::Controller, "controller"},
    {ControlProfile::Custom, "custom"},
}};

constexpr std::array<NamedValue<KeyboardKey>, 60> kKeyboardKeys{{
    {KeyboardKey::None, "none"},
    {KeyboardKey::Escape, "escape"},
    {KeyboardKey::Num1, "1"},
    {KeyboardKey::Num2, "2"},
    {KeyboardKey::Num3, "3"},
    {KeyboardKey::Num4, "4"},
    {KeyboardKey::Num5, "5"},
    {KeyboardKey::Num6, "6"},
    {KeyboardKey::Num7, "7"},
    {KeyboardKey::Num8, "8"},
    {KeyboardKey::Num9, "9"},
    {KeyboardKey::Num0, "0"},
    {KeyboardKey::Minus, "minus"},
    {KeyboardKey::Plus, "plus"},
    {KeyboardKey::Backspace, "backspace"},
    {KeyboardKey::Tab, "tab"},
    {KeyboardKey::Q, "q"},
    {KeyboardKey::W, "w"},
    {KeyboardKey::E, "e"},
    {KeyboardKey::R, "r"},
    {KeyboardKey::T, "t"},
    {KeyboardKey::Y, "y"},
    {KeyboardKey::U, "u"},
    {KeyboardKey::I, "i"},
    {KeyboardKey::O, "o"},
    {KeyboardKey::P, "p"},
    {KeyboardKey::Enter, "enter"},
    {KeyboardKey::Control, "control"},
    {KeyboardKey::A, "a"},
    {KeyboardKey::S, "s"},
    {KeyboardKey::D, "d"},
    {KeyboardKey::F, "f"},
    {KeyboardKey::G, "g"},
    {KeyboardKey::H, "h"},
    {KeyboardKey::J, "j"},
    {KeyboardKey::K, "k"},
    {KeyboardKey::L, "l"},
    {KeyboardKey::Shift, "shift"},
    {KeyboardKey::Z, "z"},
    {KeyboardKey::X, "x"},
    {KeyboardKey::C, "c"},
    {KeyboardKey::V, "v"},
    {KeyboardKey::B, "b"},
    {KeyboardKey::N, "n"},
    {KeyboardKey::M, "m"},
    {KeyboardKey::Comma, "comma"},
    {KeyboardKey::Period, "period"},
    {KeyboardKey::Slash, "slash"},
    {KeyboardKey::RightShift, "right_shift"},
    {KeyboardKey::Alt, "alt"},
    {KeyboardKey::Space, "space"},
    {KeyboardKey::Numpad8, "numpad_8"},
    {KeyboardKey::Numpad4, "numpad_4"},
    {KeyboardKey::Numpad6, "numpad_6"},
    {KeyboardKey::Numpad2, "numpad_2"},
    {KeyboardKey::ArrowUp, "arrow_up"},
    {KeyboardKey::ArrowLeft, "arrow_left"},
    {KeyboardKey::ArrowRight, "arrow_right"},
    {KeyboardKey::ArrowDown, "arrow_down"},
    {KeyboardKey::None, "unbound"},
}};

constexpr std::array<NamedValue<MouseButton>, 6> kMouseButtons{{
    {MouseButton::None, "none"},
    {MouseButton::Left, "left"},
    {MouseButton::Middle, "middle"},
    {MouseButton::Right, "right"},
    {MouseButton::Back, "back"},
    {MouseButton::Forward, "forward"},
}};

constexpr std::array<NamedValue<GamepadButton>, 19> kGamepadButtons{{
    {GamepadButton::None, "none"},
    {GamepadButton::A, "a"},
    {GamepadButton::B, "b"},
    {GamepadButton::X, "x"},
    {GamepadButton::Y, "y"},
    {GamepadButton::Back, "back"},
    {GamepadButton::Guide, "guide"},
    {GamepadButton::Start, "start"},
    {GamepadButton::LeftStick, "left_stick"},
    {GamepadButton::RightStick, "right_stick"},
    {GamepadButton::LeftShoulder, "left_shoulder"},
    {GamepadButton::RightShoulder, "right_shoulder"},
    {GamepadButton::DpadUp, "dpad_up"},
    {GamepadButton::DpadDown, "dpad_down"},
    {GamepadButton::DpadLeft, "dpad_left"},
    {GamepadButton::DpadRight, "dpad_right"},
    {GamepadButton::LeftTrigger, "left_trigger"},
    {GamepadButton::RightTrigger, "right_trigger"},
    {GamepadButton::None, "unbound"},
}};

constexpr std::array<NamedValue<AnalogStick>, 3> kAnalogSticks{{
    {AnalogStick::Disabled, "disabled"},
    {AnalogStick::Left, "left"},
    {AnalogStick::Right, "right"},
}};

constexpr std::array<NamedValue<MotionSource>, 8> kMotionSources{{
    {MotionSource::Disabled, "disabled"},
    {MotionSource::DigitalLook, "digital_look"},
    {MotionSource::Mouse, "mouse"},
    {MotionSource::RightStick, "right_stick"},
    {MotionSource::ControllerGyroscope, "controller_gyroscope"},
    {MotionSource::ControllerAccelerometer, "controller_accelerometer"},
    {MotionSource::ControllerMotion, "controller_motion"},
    {MotionSource::Automatic, "automatic"},
}};

constexpr std::array<DigitalControlRoute, kDigitalControlCount>
    kDigitalControlRoutes{{
        {NativeChannelKind::CirclePadY, Button::A, 1},
        {NativeChannelKind::CirclePadY, Button::A, -1},
        {NativeChannelKind::CirclePadX, Button::A, -1},
        {NativeChannelKind::CirclePadX, Button::A, 1},
        {NativeChannelKind::StandardHidButton, Button::A, 0},
        {NativeChannelKind::StandardHidButton, Button::B, 0},
        {NativeChannelKind::StandardHidButton, Button::X, 0},
        {NativeChannelKind::StandardHidButton, Button::Y, 0},
        {NativeChannelKind::StandardHidButton, Button::L, 0},
        {NativeChannelKind::StandardHidButton, Button::R, 0},
        {NativeChannelKind::ExtraHidButton, Button::Zl, 0},
        {NativeChannelKind::ExtraHidButton, Button::Zr, 0},
        {NativeChannelKind::StandardHidButton, Button::Select, 0},
        {NativeChannelKind::StandardHidButton, Button::Start, 0},
        {NativeChannelKind::StandardHidButton, Button::DpadUp, 0},
        {NativeChannelKind::StandardHidButton, Button::DpadDown, 0},
        {NativeChannelKind::StandardHidButton, Button::DpadLeft, 0},
        {NativeChannelKind::StandardHidButton, Button::DpadRight, 0},
        {NativeChannelKind::CStickY, Button::A, 1},
        {NativeChannelKind::CStickY, Button::A, -1},
        {NativeChannelKind::CStickX, Button::A, -1},
        {NativeChannelKind::CStickX, Button::A, 1},
    }};

static_assert(kDigitalControlRoutes.size() == kDigitalControlCount);

struct ResolvedDigitalChannels {
    std::uint32_t Buttons = 0;
    std::int16_t CirclePadX = 0;
    std::int16_t CirclePadY = 0;
    std::int16_t CStickX = 0;
    std::int16_t CStickY = 0;
};

std::int16_t SaturatingAdd(std::int16_t left, std::int16_t right) noexcept {
    return static_cast<std::int16_t>(std::clamp(
        static_cast<std::int32_t>(left) + static_cast<std::int32_t>(right),
        static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min()),
        static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::max())));
}

std::int16_t ClampNativeAxis(std::int32_t value) noexcept {
    return static_cast<std::int16_t>(std::clamp(
        value, -static_cast<std::int32_t>(kNativeStickMaximum),
        static_cast<std::int32_t>(kNativeStickMaximum)));
}

ResolvedDigitalChannels ResolveDigitalChannels(
    const DigitalState& digital) noexcept {
    ResolvedDigitalChannels channels;
    std::int32_t circlePadX = 0;
    std::int32_t circlePadY = 0;
    std::int32_t cStickX = 0;
    std::int32_t cStickY = 0;
    for (std::size_t index = 0; index < kDigitalControlCount; ++index) {
        const auto control = static_cast<DigitalControl>(index);
        if (!digital.IsHeld(control)) {
            continue;
        }
        const auto route = kDigitalControlRoutes[index];
        const auto axisValue =
            static_cast<std::int32_t>(route.AxisDirection) *
            static_cast<std::int32_t>(kNativeStickMaximum);
        switch (route.Channel) {
        case NativeChannelKind::StandardHidButton:
        case NativeChannelKind::ExtraHidButton:
            channels.Buttons |= ButtonMask(route.ButtonValue);
            break;
        case NativeChannelKind::CirclePadX:
            circlePadX += axisValue;
            break;
        case NativeChannelKind::CirclePadY:
            circlePadY += axisValue;
            break;
        case NativeChannelKind::CStickX:
            cStickX += axisValue;
            break;
        case NativeChannelKind::CStickY:
            cStickY += axisValue;
            break;
        case NativeChannelKind::None:
            break;
        }
    }
    channels.CirclePadX = ClampNativeAxis(circlePadX);
    channels.CirclePadY = ClampNativeAxis(circlePadY);
    channels.CStickX = ClampNativeAxis(cStickX);
    channels.CStickY = ClampNativeAxis(cStickY);
    return channels;
}

std::int16_t SelectAxisWithGreaterMagnitude(std::int16_t current,
                                            std::int16_t candidate) noexcept {
    return std::abs(static_cast<std::int32_t>(candidate)) >
                   std::abs(static_cast<std::int32_t>(current))
               ? candidate
               : current;
}

std::int16_t SelectConfiguredStickAxis(AnalogStick stick,
                                       std::int16_t left,
                                       std::int16_t right) noexcept {
    switch (stick) {
    case AnalogStick::Left:
        return left;
    case AnalogStick::Right:
        return right;
    case AnalogStick::Disabled:
        return 0;
    }
    return 0;
}

float NormalizedHostAxis(std::int16_t value,
                         std::int32_t deadZonePercent) noexcept {
    constexpr float kHostMaximum = 32767.0F;
    const float threshold =
        kHostMaximum *
        static_cast<float>(std::clamp(deadZonePercent, 0, 95)) / 100.0F;
    const float signedValue = static_cast<float>(value);
    const float magnitude = std::min(std::abs(signedValue), kHostMaximum);
    if (magnitude <= threshold) {
        return 0.0F;
    }
    const float normalized =
        (magnitude - threshold) / (kHostMaximum - threshold);
    return std::copysign(normalized, signedValue);
}

std::array<float, 2> ResolveRawCStick(
    const MappingConfig& config,
    const PhysicalInputState& physical) noexcept {
    return {
        NormalizedHostAxis(physical.RightStickX,
                           config.CStickDeadZonePercent) *
            static_cast<float>(kNativeStickMaximum),
        NormalizedHostAxis(physical.RightStickY,
                           config.CStickDeadZonePercent) *
            static_cast<float>(kNativeStickMaximum),
    };
}

std::array<float, 2> ResolveFilteredCStick(
    const std::array<float, 2>& raw, const AimTransform& transform,
    CStickFilterState* state, bool advanceState) noexcept {
    if (state == nullptr) {
        return raw;
    }
    if (advanceState) {
        const float coefficient = std::clamp(
            transform.CStickSmoothingCoefficient, 0.0F, 1.0F);
        state->X += (raw[0] - state->X) * coefficient;
        state->Y += (raw[1] - state->Y) * coefficient;
        if (state->X > -0.5F && state->X < 0.5F) {
            state->X = 0.0F;
        }
        if (state->Y > -0.5F && state->Y < 0.5F) {
            state->Y = 0.0F;
        }
    }
    return {state->X, state->Y};
}

struct ResolvedMotion {
    std::array<float, 3> Gyroscope{};
    std::array<float, 3> Accelerometer{0.0F, -1.0F, 0.0F};
    bool GyroscopeValid = false;
    bool AccelerometerValid = false;
};

ResolvedMotion ResolveMotion(const MappingConfig& config,
                             const PhysicalInputState& physical,
                             std::int16_t digitalCStickX,
                             std::int16_t digitalCStickY,
                             const AimTransform& transform,
                             const std::array<float, 2>& cStick,
                             VirtualMotionState* virtualMotion,
                             bool advanceState) noexcept {
    ResolvedMotion result;
    bool synthetic = false;
    const double seconds =
        std::clamp(physical.SamplePeriodSeconds, 1.0 / 1000.0, 0.25);
    const float invertX = config.NativeMotionInvertX ? -1.0F : 1.0F;
    const float invertY = config.NativeMotionInvertY ? -1.0F : 1.0F;
    const auto useMouse = [&]() {
        result.Gyroscope[0] =
            static_cast<float>(physical.MouseDeltaY) *
            config.MouseMotionDegreesPerPixel /
            static_cast<float>(seconds) * invertY;
        result.Gyroscope[1] =
            -static_cast<float>(physical.MouseDeltaX) *
            config.MouseMotionDegreesPerPixel /
            static_cast<float>(seconds) * invertX;
        result.GyroscopeValid = true;
        result.AccelerometerValid = true;
        synthetic = true;
    };
    const auto useCStick = [&]() {
        const float x = cStick[0] / static_cast<float>(kNativeStickMaximum);
        const float y = cStick[1] / static_cast<float>(kNativeStickMaximum);
        const float profileScale = std::clamp(transform.CStickScale, 0.0F, 8.0F);
        const float profileInvertX = transform.CStickInvertX ? -1.0F : 1.0F;
        const float profileInvertY = transform.CStickInvertY ? -1.0F : 1.0F;
        result.Gyroscope[0] =
            -y * config.CStickMotionMaximumDegreesPerSecond * profileScale *
            invertY * profileInvertY;
        result.Gyroscope[1] =
            -x * config.CStickMotionMaximumDegreesPerSecond * profileScale *
            invertX * profileInvertX;
        result.GyroscopeValid = true;
        result.AccelerometerValid = true;
        synthetic = true;
    };
    const auto useDigital = [&]() {
        const float x = static_cast<float>(digitalCStickX) /
                        static_cast<float>(kNativeStickMaximum);
        const float y = static_cast<float>(digitalCStickY) /
                        static_cast<float>(kNativeStickMaximum);
        result.Gyroscope[0] =
            -y * config.CStickMotionMaximumDegreesPerSecond * invertY;
        result.Gyroscope[1] =
            -x * config.CStickMotionMaximumDegreesPerSecond * invertX;
        result.GyroscopeValid = true;
        result.AccelerometerValid = true;
        synthetic = true;
    };
    const auto useControllerGyroscope = [&]() {
        if (!physical.ControllerMotion.GyroscopeValid) {
            return false;
        }
        for (std::size_t axis = 0; axis < 3U; ++axis) {
            result.Gyroscope[axis] =
                (physical.ControllerMotion.GyroscopeDegreesPerSecond[axis] -
                 config.GyroscopeBiasDegreesPerSecond[axis]) *
                config.ControllerGyroscopeSensitivity;
        }
        result.Gyroscope[0] *= invertY;
        result.Gyroscope[1] *= invertX;
        result.Gyroscope[2] *= invertX;
        result.GyroscopeValid = true;
        return true;
    };
    const auto useControllerAccelerometer = [&]() {
        if (!physical.ControllerMotion.AccelerometerValid) {
            return false;
        }
        for (std::size_t axis = 0; axis < 3U; ++axis) {
            result.Accelerometer[axis] =
                physical.ControllerMotion.Accelerometer[axis] *
                config.ControllerAccelerometerSensitivity;
        }
        result.Accelerometer[0] *= invertX;
        result.Accelerometer[2] *= invertY;
        result.AccelerometerValid = true;
        return true;
    };

    switch (config.NativeMotionSource) {
    case MotionSource::Disabled:
        break;
    case MotionSource::DigitalLook:
        useDigital();
        break;
    case MotionSource::Mouse:
        useMouse();
        break;
    case MotionSource::RightStick:
        useCStick();
        break;
    case MotionSource::ControllerGyroscope:
        static_cast<void>(useControllerGyroscope());
        if (physical.ControllerMotion.AccelerometerValid) {
            static_cast<void>(useControllerAccelerometer());
        }
        break;
    case MotionSource::ControllerAccelerometer:
        static_cast<void>(useControllerAccelerometer());
        break;
    case MotionSource::ControllerMotion:
        static_cast<void>(useControllerGyroscope());
        static_cast<void>(useControllerAccelerometer());
        break;
    case MotionSource::Automatic:
        {
            VirtualMotionState temporary;
            auto& state = virtualMotion ? *virtualMotion : temporary;
            auto owner = state.AutomaticOwner;
            const bool controllerIntent =
                (physical.ControllerPressed | (physical.ControllerButtons & ~state.PreviousControllerButtons)) != 0 ||
                ConvertHostAxisToNative(physical.RightStickX, config.CStickDeadZonePercent) != 0 ||
                ConvertHostAxisToNative(physical.RightStickY, config.CStickDeadZonePercent) != 0 ||
                ConvertHostAxisToNative(physical.LeftStickX, config.CirclePadDeadZonePercent) != 0 ||
                ConvertHostAxisToNative(physical.LeftStickY, config.CirclePadDeadZonePercent) != 0;
            if (physical.MouseDeltaX != 0 || physical.MouseDeltaY != 0) owner = VirtualMotionState::Owner::Mouse;
            else if (controllerIntent) owner = VirtualMotionState::Owner::Controller;
            else if (owner == VirtualMotionState::Owner::None)
                owner = state.Active ? VirtualMotionState::Owner::Mouse : VirtualMotionState::Owner::Controller;

            const bool controller = owner == VirtualMotionState::Owner::Controller;
            if (controller) useCStick(); else useMouse();
            const float pitch = result.Gyroscope[0], yaw = result.Gyroscope[1];
            result = {};
            if (controller) {
                useControllerGyroscope();
                useControllerAccelerometer();
            }
            const bool physicalBase = controller && (result.GyroscopeValid || result.AccelerometerValid ||
                (state.Composition.Initialized && state.Composition.PhysicalBase));
            const auto composed = state.Composition.Sample(result.Accelerometer, result.AccelerometerValid,
                result.Gyroscope, pitch, yaw, seconds, physicalBase, advanceState);
            result.Gyroscope = composed.AngularVelocity;
            result.Accelerometer = composed.Gravity;
            result.GyroscopeValid = result.AccelerometerValid = true;
            if (advanceState) {
                state.AutomaticOwner = owner;
                state.PreviousControllerButtons = physical.ControllerButtons;
                state.PitchRadians = std::atan2(double(result.Accelerometer[2]), -double(result.Accelerometer[1]));
                state.Active = !physicalBase;
            }
            return result;
        }
    }
    if (synthetic) {
        constexpr double radiansPerDegree = 3.14159265358979323846 / 180.0;
        const double pitch = virtualMotion != nullptr ? virtualMotion->PitchRadians : 0.0;
        const double delta = advanceState ? result.Gyroscope[0] * seconds * radiansPerDegree : 0.0;
        const double nextPitch = std::remainder(pitch + delta, 2.0 * 3.14159265358979323846);
        // HID expects body-space rates. Yaw about world up becomes a Y/Z
        // combination while pitched, not a roll about neutral Z.
        const double samplePitch = pitch + delta * 0.5;
        const float yaw = result.Gyroscope[1];
        result.Gyroscope[1] = yaw * static_cast<float>(std::cos(samplePitch));
        result.Gyroscope[2] = -yaw * static_cast<float>(std::sin(samplePitch));
        result.Accelerometer = {0.0F, -static_cast<float>(std::cos(nextPitch)),
                               static_cast<float>(std::sin(nextPitch))};
        if (virtualMotion != nullptr && advanceState) {
            virtualMotion->PitchRadians = nextPitch;
            virtualMotion->Active = true;
            virtualMotion->Composition.RestoreGravity(result.Accelerometer);
            virtualMotion->AutomaticOwner = VirtualMotionState::Owner::None;
        }
    } else if (virtualMotion != nullptr && advanceState) {
        // A physical sensor owns both vectors. Rebase a later virtual source
        // to that pose instead of resurrecting an unrelated virtual tilt.
        virtualMotion->RestoreGravity(result.Accelerometer);
        virtualMotion->Active = false;
    }
    return result;
}

std::int16_t ClampAbsoluteAxis(float value) noexcept {
    return static_cast<std::int16_t>(std::lround(std::clamp(
        value, -static_cast<float>(kNativeStickMaximum),
        static_cast<float>(kNativeStickMaximum))));
}

std::int16_t ClampRelativeAxis(float value) noexcept {
    return static_cast<std::int16_t>(std::lround(std::clamp(
        value,
        static_cast<float>(std::numeric_limits<std::int16_t>::min()),
        static_cast<float>(std::numeric_limits<std::int16_t>::max()))));
}

AxisInputSample ResolveCStick(const MappingConfig& config,
                              const PhysicalInputState& physical,
                              std::int16_t digitalCStickX,
                              std::int16_t digitalCStickY,
                              const std::array<float, 2>& rawCStick) noexcept {
    MotionSource source = NormalizeCameraSource(config.CStickSource);
    if (source == MotionSource::Automatic) {
        if (physical.MouseDeltaX != 0 || physical.MouseDeltaY != 0) {
            source = MotionSource::Mouse;
        } else {
            source = MotionSource::RightStick;
        }
    }

    float x = 0.0F;
    float y = 0.0F;
    AxisInputKind kind = AxisInputKind::Absolute;
    switch (source) {
    case MotionSource::Disabled:
        break;
    case MotionSource::DigitalLook:
        x = static_cast<float>(digitalCStickX);
        y = static_cast<float>(digitalCStickY);
        break;
    case MotionSource::Mouse:
        x = static_cast<float>(physical.MouseDeltaX) *
            config.MouseCStickUnitsPerPixel;
        y = -static_cast<float>(physical.MouseDeltaY) *
            config.MouseCStickUnitsPerPixel;
        kind = AxisInputKind::Relative;
        break;
    case MotionSource::RightStick:
        x = rawCStick[0];
        y = rawCStick[1];
        break;
    case MotionSource::ControllerGyroscope:
    case MotionSource::ControllerMotion:
    case MotionSource::ControllerAccelerometer:
        break;
    case MotionSource::Automatic:
        break;
    }
    return kind == AxisInputKind::Relative
               ? AxisInputSample{ClampRelativeAxis(x), ClampRelativeAxis(y),
                                 kind}
               : AxisInputSample{ClampAbsoluteAxis(x), ClampAbsoluteAxis(y),
                                 kind};
}

} // namespace

void VirtualMotionState::RestoreGravity(const std::array<float, 3>& gravity) noexcept {
    *this = {};
    PitchRadians = std::isfinite(gravity[1]) && std::isfinite(gravity[2]) &&
                           (gravity[1] != 0.0F || gravity[2] != 0.0F) ?
        std::atan2(static_cast<double>(gravity[2]), -static_cast<double>(gravity[1])) : 0.0;
    Active = true;
    Composition.RestoreGravity(gravity);
}

void VirtualMotionState::ResetController() noexcept {
    PreviousControllerButtons = 0;
    if (AutomaticOwner == Owner::Controller) AutomaticOwner = Owner::None;
    Composition.ResetReference();
}

void HostBindingCapture::Begin(BindingDevice device) noexcept {
    mState = {BindingCapturePhase::Release, device, {}};
}

void HostBindingCapture::Cancel() noexcept {
    mState.Phase = BindingCapturePhase::Cancelled;
}

bool HostBindingCapture::Active() const noexcept {
    return mState.Phase == BindingCapturePhase::Release || mState.Phase == BindingCapturePhase::Listening;
}

void HostBindingCapture::Observe(const HostButtonSource& source, bool cancel) noexcept {
    if (!Active()) return;
    if (cancel) { Cancel(); return; }
    HostBinding pressed;
    bool held = false;
    if (mState.Device == BindingDevice::Keyboard) {
        for (const auto& entry : kKeyboardKeys) {
            if (entry.Value != KeyboardKey::None && entry.Value != KeyboardKey::Escape &&
                source.IsKeyboardKeyHeld(entry.Value)) {
                pressed.KeyboardPrimary = entry.Value;
                held = true;
                break;
            }
        }
    } else if (mState.Device == BindingDevice::Mouse) {
        for (const auto& entry : kMouseButtons) {
            if (entry.Value != MouseButton::None && source.IsMouseButtonHeld(entry.Value)) {
                pressed.Mouse = entry.Value;
                held = true;
                break;
            }
        }
    } else {
        for (const auto& entry : kGamepadButtons) {
            if (entry.Value != GamepadButton::None && source.IsGamepadButtonHeld(entry.Value)) {
                pressed.Gamepad = entry.Value;
                held = true;
                break;
            }
        }
    }
    if (mState.Phase == BindingCapturePhase::Release) {
        if (!held) mState.Phase = BindingCapturePhase::Listening;
    } else if (held) {
        mState.Binding = pressed;
        mState.Phase = BindingCapturePhase::Complete;
    }
}

HardwareCapabilities CapabilitiesFor(HardwareProfile profile) noexcept {
    HardwareCapabilities capabilities;
    if (profile == HardwareProfile::Old3dsCirclePadPro ||
        profile == HardwareProfile::New3ds) {
        capabilities.ButtonMask |= kExtraHidButtonMask;
        capabilities.CStick = true;
    }
    return capabilities;
}

const char* HardwareProfileName(HardwareProfile profile) noexcept {
    return NameOf(profile, kHardwareProfiles);
}

bool ParseHardwareProfile(std::string_view value,
                          HardwareProfile* profile) noexcept {
    return ParseNamed(value, profile, kHardwareProfiles);
}

DigitalControlRoute NativeRouteFor(DigitalControl control) noexcept {
    const auto index = static_cast<std::size_t>(control);
    return index < kDigitalControlRoutes.size()
               ? kDigitalControlRoutes[index]
               : DigitalControlRoute{};
}

bool IsDigitalControlSupported(HardwareProfile profile,
                               DigitalControl control) noexcept {
    const auto capabilities = CapabilitiesFor(profile);
    const auto route = NativeRouteFor(control);
    switch (route.Channel) {
    case NativeChannelKind::StandardHidButton:
    case NativeChannelKind::ExtraHidButton:
        return (capabilities.ButtonMask & ButtonMask(route.ButtonValue)) != 0U;
    case NativeChannelKind::CirclePadX:
    case NativeChannelKind::CirclePadY:
        return capabilities.CirclePad;
    case NativeChannelKind::CStickX:
    case NativeChannelKind::CStickY:
        return capabilities.CStick;
    case NativeChannelKind::None:
        return false;
    }
    return false;
}

const char* DigitalControlName(DigitalControl control) noexcept {
    return NameOf(control, kDigitalControls);
}

bool ParseDigitalControl(std::string_view value,
                         DigitalControl* control) noexcept {
    return ParseNamed(value, control, kDigitalControls);
}

const char* ControlProfileName(ControlProfile profile) noexcept {
    return NameOf(profile, kControlProfiles);
}

const char* KeyboardKeyName(KeyboardKey key) noexcept {
    return NameOf(key, kKeyboardKeys);
}

const char* MouseButtonName(MouseButton button) noexcept {
    return NameOf(button, kMouseButtons);
}

const char* GamepadButtonName(GamepadButton button) noexcept {
    return NameOf(button, kGamepadButtons);
}

const char* AnalogStickName(AnalogStick stick) noexcept {
    return NameOf(stick, kAnalogSticks);
}

const char* MotionSourceName(MotionSource source) noexcept {
    return NameOf(source, kMotionSources);
}

bool ParseControlProfile(std::string_view value,
                         ControlProfile* profile) noexcept {
    return ParseNamed(value, profile, kControlProfiles);
}

bool ParseKeyboardKey(std::string_view value,
                      KeyboardKey* key) noexcept {
    return ParseNamed(value, key, kKeyboardKeys);
}

bool ParseMouseButton(std::string_view value,
                      MouseButton* button) noexcept {
    return ParseNamed(value, button, kMouseButtons);
}

bool ParseGamepadButton(std::string_view value,
                        GamepadButton* button) noexcept {
    return ParseNamed(value, button, kGamepadButtons);
}

bool ParseAnalogStick(std::string_view value,
                      AnalogStick* stick) noexcept {
    return ParseNamed(value, stick, kAnalogSticks);
}

bool ParseMotionSource(std::string_view value,
                       MotionSource* source) noexcept {
    return ParseNamed(value, source, kMotionSources);
}

void SwapGamepadSources(std::span<HostBinding> bindings,
                        GamepadButton first, GamepadButton second) noexcept {
    if (first == GamepadButton::None || second == GamepadButton::None) {
        return;
    }
    for (auto& binding : bindings) {
        if (binding.Gamepad == first) {
            binding.Gamepad = second;
        } else if (binding.Gamepad == second) {
            binding.Gamepad = first;
        }
    }
}

bool IsHostBindingHeld(const HostBinding& binding,
                       const HostDeviceEnablement& enabled,
                       const HostButtonSource& source) noexcept {
    return (enabled.Keyboard &&
            ((binding.KeyboardPrimary != KeyboardKey::None &&
              source.IsKeyboardKeyHeld(binding.KeyboardPrimary)) ||
             (binding.KeyboardSecondary != KeyboardKey::None &&
              source.IsKeyboardKeyHeld(binding.KeyboardSecondary)))) ||
           (enabled.Mouse && binding.Mouse != MouseButton::None &&
            source.IsMouseButtonHeld(binding.Mouse)) ||
           (enabled.Gamepad && binding.Gamepad != GamepadButton::None &&
            source.IsGamepadButtonHeld(binding.Gamepad));
}

bool DigitalState::IsHeld(DigitalControl control) const noexcept {
    const auto index = static_cast<std::size_t>(control);
    return index < Held.size() && Held[index];
}

void DigitalState::SetHeld(DigitalControl control, bool held) noexcept {
    const auto index = static_cast<std::size_t>(control);
    if (index < Held.size()) {
        Held[index] = held;
    }
}

TouchMapping MapPresentationPointToTouch(
    float pointX, float pointY, float presentationWidth,
    float presentationHeight, bool inside,
    bool pointerPressed) noexcept {
    if (!inside || !std::isfinite(pointX) || !std::isfinite(pointY) ||
        !std::isfinite(presentationWidth) ||
        !std::isfinite(presentationHeight) || presentationWidth <= 0.0F ||
        presentationHeight <= 0.0F) {
        return {};
    }
    const float nativeX = std::clamp(
        pointX * static_cast<float>(kTouchWidth) / presentationWidth,
        0.0F, static_cast<float>(kTouchWidth - 1U));
    const float nativeY = std::clamp(
        pointY * static_cast<float>(kTouchHeight) / presentationHeight,
        0.0F, static_cast<float>(kTouchHeight - 1U));
    return {
        static_cast<std::uint16_t>(nativeX),
        static_cast<std::uint16_t>(nativeY),
        true,
        pointerPressed,
    };
}

HidState ProjectStandardHid(const InputFrame& frame) noexcept {
    HidState projected = frame.Hid;
    projected.Buttons &= kStandardHidButtonMask;
    return projected;
}

ExtraHidState ProjectExtraHid(const InputFrame& frame,
                              HardwareProfile profile) noexcept {
    const auto capabilities = CapabilitiesFor(profile);
    if (!capabilities.CStick) {
        return {};
    }
    return {
        frame.Hid.Buttons & kExtraHidButtonMask,
        ClampNativeAxis(frame.CStick.X),
        ClampNativeAxis(frame.CStick.Y),
    };
}

bool IsButtonHeld(const InputFrame& frame, Button button) noexcept {
    return (frame.Hid.Buttons & ButtonMask(button)) != 0U;
}

void SetButtonHeld(InputFrame& frame, Button button, bool held) noexcept {
    if (held) {
        frame.Hid.Buttons |= ButtonMask(button);
    } else {
        frame.Hid.Buttons &= ~ButtonMask(button);
    }
}

std::int16_t ConvertHostAxisToNative(std::int16_t value,
                                     std::int32_t deadZonePercent) noexcept {
    constexpr std::int32_t kHostAxisMaximum = 32767;
    const std::int32_t clampedDeadZone =
        std::clamp(deadZonePercent, 0, 99);
    const std::int32_t threshold =
        kHostAxisMaximum * clampedDeadZone / 100;
    const std::int32_t signedValue = static_cast<std::int32_t>(value);
    const std::int32_t magnitude =
        std::min(std::abs(signedValue), kHostAxisMaximum);
    if (magnitude <= threshold) {
        return 0;
    }
    const std::int32_t scaled =
        (magnitude - threshold) *
        static_cast<std::int32_t>(kNativeStickMaximum) /
        (kHostAxisMaximum - threshold);
    return static_cast<std::int16_t>(signedValue < 0 ? -scaled : scaled);
}

// Radial dead zone after Ocarina ReTuned (OTPR26/Ocarina-ReTuned c43fff59).
AxisInputSample ConvertHostStickToNative(
    std::int16_t x, std::int16_t y, std::int32_t deadZonePercent) noexcept {
    constexpr double kHostAxisMaximum = 32767.0;
    const double unitX =
        std::clamp(static_cast<double>(x), -kHostAxisMaximum,
                   kHostAxisMaximum) / kHostAxisMaximum;
    const double unitY =
        std::clamp(static_cast<double>(y), -kHostAxisMaximum,
                   kHostAxisMaximum) / kHostAxisMaximum;
    const double magnitude = std::hypot(unitX, unitY);
    const double deadZone = std::clamp(deadZonePercent, 0, 99) / 100.0;
    if (magnitude <= deadZone) {
        return {};
    }
    // Issue #128: physical sticks rarely report the full host range at full
    // tilt (worn or circular-gated sticks stop near 85-95 percent), and the
    // guest only runs near the circle pad maximum. The outer band saturates
    // to the native maximum, so a stick pushed to its rim always reaches it;
    // the range below the band stays linear. Native range unchanged.
    const double outer =
        kHostStickOuterSaturation > deadZone ? kHostStickOuterSaturation : 1.0;
    const double scale = (std::min(magnitude, outer) - deadZone) /
                         (outer - deadZone) *
                         static_cast<double>(kNativeStickMaximum) /
                         magnitude;
    return {
        static_cast<std::int16_t>(std::lround(unitX * scale)),
        static_cast<std::int16_t>(std::lround(unitY * scale)),
    };
}

InputFrame ResolveInput(const MappingConfig& config,
                        const PhysicalInputState& physical,
                        const DigitalState& digital,
                        const AimTransform& aimTransform,
                        CStickFilterState* cStickFilter,
                        bool advanceCStickFilter,
                        VirtualMotionState* virtualMotion) noexcept {
    InputFrame frame;
    const auto digitalChannels = ResolveDigitalChannels(digital);
    const auto analog = ConvertHostStickToNative(
        SelectConfiguredStickAxis(config.CirclePadSource,
                                  physical.LeftStickX,
                                  physical.RightStickX),
        SelectConfiguredStickAxis(config.CirclePadSource,
                                  physical.LeftStickY,
                                  physical.RightStickY),
        config.CirclePadDeadZonePercent);
    frame.Hid.CirclePadX = SelectAxisWithGreaterMagnitude(
        digitalChannels.CirclePadX, analog.X);
    frame.Hid.CirclePadY = SelectAxisWithGreaterMagnitude(
        digitalChannels.CirclePadY, analog.Y);
    frame.Hid.Buttons = digitalChannels.Buttons;

    const auto rawCStick = ResolveRawCStick(config, physical);
    const auto filteredCStick = ResolveFilteredCStick(
        rawCStick, aimTransform, cStickFilter, advanceCStickFilter);
    frame.CStick = ResolveCStick(
        config, physical, digitalChannels.CStickX,
        digitalChannels.CStickY, rawCStick);
    const auto motion = ResolveMotion(
        config, physical, digitalChannels.CStickX,
        digitalChannels.CStickY, aimTransform, filteredCStick,
        virtualMotion, advanceCStickFilter);
    frame.Hid.GyroscopeDegreesPerSecond = motion.Gyroscope;
    frame.Hid.Accelerometer = motion.Accelerometer;
    frame.Hid.GyroscopeValid = motion.GyroscopeValid;
    frame.Hid.AccelerometerValid = motion.AccelerometerValid;
    return frame;
}

void AxisInputAccumulator::Observe(const AxisInputSample& sample) noexcept {
    if (sample.Kind == AxisInputKind::Relative) {
        if (mSample.Kind != AxisInputKind::Relative) {
            mSample = {};
            mSample.Kind = AxisInputKind::Relative;
        }
        mSample.X = SaturatingAdd(mSample.X, sample.X);
        mSample.Y = SaturatingAdd(mSample.Y, sample.Y);
        if (sample.X != 0 || sample.Y != 0) {
            ++mRelativeSamplesObserved;
        }
        return;
    }
    mSample = sample;
}

AxisInputSample AxisInputAccumulator::Consume() noexcept {
    const auto sample = mSample;
    if (mSample.Kind == AxisInputKind::Relative) {
        if (mSample.X != 0 || mSample.Y != 0) {
            ++mRelativeSamplesConsumed;
        }
        mSample.X = 0;
        mSample.Y = 0;
    }
    return sample;
}

void AxisInputAccumulator::Reset() noexcept {
    mSample = {};
}

AxisInputSample AxisInputAccumulator::Peek() const noexcept {
    return mSample;
}

std::uint64_t AxisInputAccumulator::RelativeSamplesObserved() const noexcept {
    return mRelativeSamplesObserved;
}

std::uint64_t AxisInputAccumulator::RelativeSamplesConsumed() const noexcept {
    return mRelativeSamplesConsumed;
}

bool ResolveGameplayPointerOwnership(bool nativeTouchOwned,
                                     bool hostGuiVisible,
                                     bool mouseEnabled,
                                     bool mappingUsesMouse) noexcept {
    return !nativeTouchOwned && !hostGuiVisible && mouseEnabled &&
           mappingUsesMouse;
}

const char* ControllerTypeName(ControllerType type) noexcept {
    constexpr std::array<const char*, 14> names{
        "unknown", "xbox_360", "xbox_one", "ps3", "ps4", "switch_pro", "virtual", "ps5",
        "amazon_luna", "google_stadia", "nvidia_shield", "joycon_left", "joycon_right", "joycon_pair"};
    const auto index = static_cast<std::size_t>(type);
    return index < names.size() ? names[index] : "unknown";
}

namespace {

// A device name fit for one log line: printable, no quotes, at most 64
// bytes, the user's name replaced.
std::string LogDeviceName(std::string_view name, std::string_view userName) {
    std::string text;
    for (std::size_t index = 0; index < name.size() && text.size() < 64;) {
        if (userName.size() >= 2 && index + userName.size() <= name.size() &&
            ContainsNoCase(name.substr(index, userName.size()), userName)) {
            text += "<user>";
            index += userName.size();
            continue;
        }
        const auto c = static_cast<unsigned char>(name[index++]);
        text += c < 0x20 || c == 0x7F || c == '"' || c == '\\' ? '?' : static_cast<char>(c);
    }
    return text;
}

} // namespace

std::vector<std::string> DescribeInputDevices(std::string_view reason, std::span<const DeviceDescriptor> pads,
                                              const KeyboardDescription& keyboard, std::string_view userName) {
    std::vector<std::string> lines;
    std::int32_t selected = -1;
    for (const auto& pad : pads) if (pad.Selected) selected = pad.InstanceId;
    lines.push_back("TRIAEVUM_INPUT_DEVICES reason=" + std::string(reason) + " pads=" + std::to_string(pads.size()) +
                    " selected=" + std::to_string(selected) + " video_driver=" +
                    (keyboard.VideoDriver.empty() ? std::string("unknown") : LogDeviceName(keyboard.VideoDriver, {})));
    for (const auto& pad : pads) {
        lines.push_back("TRIAEVUM_INPUT_PAD id=" + std::to_string(pad.InstanceId) +
                        " selected=" + (pad.Selected ? "1" : "0") +
                        " name=\"" + LogDeviceName(pad.Name, userName) + "\"" +
                        " type=" + ControllerTypeName(pad.Type) +
                        " guid=" + LogDeviceName(pad.Guid, {}) +
                        " prompts=" + PromptSetName(pad.Prompts) +
                        " gyro=" + (pad.HasGyroscope ? "1" : "0") +
                        " accel=" + (pad.HasAccelerometer ? "1" : "0") +
                        " touchpads=" + std::to_string(pad.TouchpadCount) +
                        " rumble=" + (pad.HasRumble ? "1" : "0") +
                        " serial=" + (pad.Serial.empty() ? "none" : "present"));
    }
    lines.push_back(std::string("TRIAEVUM_INPUT_KEYBOARD enabled=") + (keyboard.Enabled ? "1" : "0") +
                    " focus=" + (keyboard.WindowFocused ? "1" : "0") + " prompts=" +
                    PromptSetName(PromptSet::Keyboard));
    return lines;
}

std::vector<std::string> InputDeviceLog::Observe(std::span<const DeviceDescriptor> pads,
                                                 const KeyboardDescription& keyboard, std::string_view userName) {
    std::string signature = keyboard.Enabled ? "k1" : "k0";
    for (const auto& pad : pads) {
        signature += '|' + std::to_string(pad.InstanceId) + (pad.Selected ? "*" : "") + pad.Guid + pad.Name;
    }
    if (mLogged && signature == mSignature) return {};
    const bool first = !mLogged;
    mLogged = true;
    mSignature = std::move(signature);
    return DescribeInputDevices(first ? "startup" : "change", pads, keyboard, userName);
}

const char* RumbleStrengthName(RumbleStrength strength) noexcept {
    switch (strength) {
    case RumbleStrength::Off: return "off";
    case RumbleStrength::Light: return "light";
    case RumbleStrength::Strong: return "strong";
    }
    return "unknown";
}

const char* RumbleStrengthLabel(RumbleStrength strength) noexcept {
    switch (strength) {
    case RumbleStrength::Off: return "Off";
    case RumbleStrength::Light: return "Light";
    case RumbleStrength::Strong: return "Strong";
    }
    return "Unknown";
}

bool ParseRumbleStrength(std::string_view value, RumbleStrength* strength) noexcept {
    if (strength == nullptr) return false;
    for (std::size_t index = 0; index < kRumbleStrengthCount; ++index) {
        const auto candidate = static_cast<RumbleStrength>(index);
        if (value == RumbleStrengthName(candidate)) {
            *strength = candidate;
            return true;
        }
    }
    return false;
}

RumbleCommand ResolveRumble(RumblePulse pulse, RumbleStrength strength) noexcept {
    RumbleCommand command;
    const float intensity = std::isfinite(pulse.Intensity) ? std::clamp(pulse.Intensity, 0.0F, 1.0F) : 0.0F;
    const float scale = strength == RumbleStrength::Strong ? 1.0F : strength == RumbleStrength::Light ? 0.45F : 0.0F;
    const float low = intensity * scale;
    if (low <= 0.0F || pulse.DurationMilliseconds == 0) return command;
    command.Play = true;
    command.LowFrequency = static_cast<std::uint16_t>(std::lround(low * 65535.0F));
    command.HighFrequency = static_cast<std::uint16_t>(std::lround(low * 0.5F * 65535.0F));
    command.DurationMilliseconds = std::min<std::uint32_t>(pulse.DurationMilliseconds, 1000U);
    return command;
}

void RumbleRequests::Request(RumblePulse pulse) noexcept {
    if (!std::isfinite(pulse.Intensity) || pulse.Intensity <= 0.0F || pulse.DurationMilliseconds == 0) return;
    if (!Pending() || pulse.Intensity > mPending.Intensity ||
        (pulse.Intensity == mPending.Intensity && pulse.DurationMilliseconds > mPending.DurationMilliseconds))
        mPending = pulse;
}

RumblePulse RumbleRequests::Take() noexcept {
    const auto pulse = mPending;
    mPending = {};
    return pulse;
}

} // namespace ThreeDsRecomp::Input
