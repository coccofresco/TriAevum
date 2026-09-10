#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace ThreeDsRecomp::Input {

inline constexpr std::int16_t kNativeStickMaximum = 154;
inline constexpr std::uint16_t kTouchWidth = 320U;
inline constexpr std::uint16_t kTouchHeight = 240U;

enum class HardwareProfile : std::uint8_t {
    Old3ds,
    Old3dsCirclePadPro,
    New3ds,
};

enum class Button : std::uint32_t {
    A = 1U << 0U,
    B = 1U << 1U,
    Select = 1U << 2U,
    Start = 1U << 3U,
    DpadRight = 1U << 4U,
    DpadLeft = 1U << 5U,
    DpadUp = 1U << 6U,
    DpadDown = 1U << 7U,
    R = 1U << 8U,
    L = 1U << 9U,
    X = 1U << 10U,
    Y = 1U << 11U,
    Debug = 1U << 12U,
    Gpio14 = 1U << 13U,
    Zl = 1U << 14U,
    Zr = 1U << 15U,
};

constexpr std::uint32_t ButtonMask(Button button) noexcept {
    return static_cast<std::uint32_t>(button);
}

inline constexpr std::uint32_t kStandardHidButtonMask = 0x00003FFFU;
inline constexpr std::uint32_t kExtraHidButtonMask =
    ButtonMask(Button::Zl) | ButtonMask(Button::Zr);

struct HardwareCapabilities {
    std::uint32_t ButtonMask = kStandardHidButtonMask;
    bool CirclePad = true;
    bool CStick = false;
    bool Touch = true;
    bool Accelerometer = true;
    bool Gyroscope = true;
};

HardwareCapabilities CapabilitiesFor(HardwareProfile profile) noexcept;
const char* HardwareProfileName(HardwareProfile profile) noexcept;
bool ParseHardwareProfile(std::string_view value,
                          HardwareProfile* profile) noexcept;

enum class DigitalControl : std::uint8_t {
    CirclePadUp,
    CirclePadDown,
    CirclePadLeft,
    CirclePadRight,
    A,
    B,
    X,
    Y,
    L,
    R,
    Zl,
    Zr,
    Select,
    Start,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    CStickUp,
    CStickDown,
    CStickLeft,
    CStickRight,
    Count,
};

inline constexpr std::size_t kDigitalControlCount =
    static_cast<std::size_t>(DigitalControl::Count);

// Every abstract digital control terminates on one real 3DS input channel.
// Title adapters may rename controls for their UI, but must not introduce
// parallel gameplay channels beside these routes.
enum class NativeChannelKind : std::uint8_t {
    None,
    StandardHidButton,
    ExtraHidButton,
    CirclePadX,
    CirclePadY,
    CStickX,
    CStickY,
};

struct DigitalControlRoute {
    NativeChannelKind Channel = NativeChannelKind::None;
    Button ButtonValue = Button::A;
    std::int8_t AxisDirection = 0;
};

[[nodiscard]] DigitalControlRoute NativeRouteFor(
    DigitalControl control) noexcept;
[[nodiscard]] bool IsDigitalControlSupported(
    HardwareProfile profile, DigitalControl control) noexcept;
const char* DigitalControlName(DigitalControl control) noexcept;
bool ParseDigitalControl(std::string_view value,
                         DigitalControl* control) noexcept;

struct DigitalState {
    std::array<bool, kDigitalControlCount> Held{};

    [[nodiscard]] bool IsHeld(DigitalControl control) const noexcept;
    void SetHeld(DigitalControl control, bool held = true) noexcept;
};

enum class ControlProfile : std::uint8_t {
    Keyboard,
    KeyboardMouse,
    Controller,
    Custom,
};

// Values intentionally match Ship::KbScancode. The cross-title input model
// remains independent from the retained runtime implementation and SDL.
enum class KeyboardKey : std::int32_t {
    None = 0,
    Escape = 1,
    Num1 = 2,
    Num2 = 3,
    Num3 = 4,
    Num4 = 5,
    Num5 = 6,
    Num6 = 7,
    Num7 = 8,
    Num8 = 9,
    Num9 = 10,
    Num0 = 11,
    Minus = 12,
    Plus = 13,
    Backspace = 14,
    Tab = 15,
    Q = 16,
    W = 17,
    E = 18,
    R = 19,
    T = 20,
    Y = 21,
    U = 22,
    I = 23,
    O = 24,
    P = 25,
    Enter = 28,
    Control = 29,
    A = 30,
    S = 31,
    D = 32,
    F = 33,
    G = 34,
    H = 35,
    J = 36,
    K = 37,
    L = 38,
    Shift = 42,
    Z = 44,
    X = 45,
    C = 46,
    V = 47,
    B = 48,
    N = 49,
    M = 50,
    Comma = 51,
    Period = 52,
    Slash = 53,
    RightShift = 54,
    Alt = 56,
    Space = 57,
    Numpad8 = 72,
    Numpad4 = 75,
    Numpad6 = 77,
    Numpad2 = 80,
    ArrowUp = 328,
    ArrowLeft = 331,
    ArrowRight = 333,
    ArrowDown = 336,
};

enum class MouseButton : std::int8_t {
    None = -1,
    Left = 0,
    Middle = 1,
    Right = 2,
    Back = 3,
    Forward = 4,
};

enum class GamepadButton : std::int8_t {
    None = -1,
    A,
    B,
    X,
    Y,
    Back,
    Guide,
    Start,
    LeftStick,
    RightStick,
    LeftShoulder,
    RightShoulder,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    LeftTrigger,
    RightTrigger,
};

enum class AnalogStick : std::uint8_t {
    Disabled,
    Left,
    Right,
};

enum class MotionSource : std::uint8_t {
    Disabled,
    DigitalLook,
    Mouse,
    RightStick,
    ControllerGyroscope,
    ControllerAccelerometer,
    ControllerMotion,
    Automatic,
};

const char* ControlProfileName(ControlProfile profile) noexcept;
const char* KeyboardKeyName(KeyboardKey key) noexcept;
const char* MouseButtonName(MouseButton button) noexcept;
const char* GamepadButtonName(GamepadButton button) noexcept;
const char* AnalogStickName(AnalogStick stick) noexcept;
const char* MotionSourceName(MotionSource source) noexcept;

bool ParseControlProfile(std::string_view value,
                         ControlProfile* profile) noexcept;
bool ParseKeyboardKey(std::string_view value,
                      KeyboardKey* key) noexcept;
bool ParseMouseButton(std::string_view value,
                      MouseButton* button) noexcept;
bool ParseGamepadButton(std::string_view value,
                        GamepadButton* button) noexcept;
bool ParseAnalogStick(std::string_view value,
                      AnalogStick* stick) noexcept;
bool ParseMotionSource(std::string_view value,
                       MotionSource* source) noexcept;

struct HostBinding {
    KeyboardKey KeyboardPrimary = KeyboardKey::None;
    KeyboardKey KeyboardSecondary = KeyboardKey::None;
    MouseButton Mouse = MouseButton::None;
    GamepadButton Gamepad = GamepadButton::None;

    bool operator==(const HostBinding&) const = default;
};

struct HostDeviceEnablement {
    bool Keyboard = true;
    bool Mouse = true;
    bool Gamepad = true;
};

class HostButtonSource {
  public:
    virtual ~HostButtonSource() = default;
    [[nodiscard]] virtual bool IsKeyboardKeyHeld(
        KeyboardKey key) const noexcept = 0;
    [[nodiscard]] virtual bool IsMouseButtonHeld(
        MouseButton button) const noexcept = 0;
    [[nodiscard]] virtual bool IsGamepadButtonHeld(
        GamepadButton button) const noexcept = 0;
};

enum class BindingDevice : std::uint8_t { Keyboard, Mouse, Gamepad };
enum class BindingCapturePhase : std::uint8_t { Idle, Release, Listening, Complete, Cancelled };

struct BindingCaptureSnapshot {
    BindingCapturePhase Phase = BindingCapturePhase::Idle;
    BindingDevice Device = BindingDevice::Keyboard;
    HostBinding Binding;
};

// Reads the existing host poll before device enablement and gameplay/UI filtering.
// The opening gesture must be released before a new press can be assigned.
class HostBindingCapture {
  public:
    void Begin(BindingDevice device) noexcept;
    void Cancel() noexcept;
    void Observe(const HostButtonSource& source, bool cancel) noexcept;
    [[nodiscard]] BindingCaptureSnapshot Snapshot() const noexcept { return mState; }
    [[nodiscard]] bool Active() const noexcept;
  private:
    BindingCaptureSnapshot mState;
};

// Exchange physical sources, including every use in a customized mapping.
// None is not a source: exchanging it would bind every unassigned action.
void SwapGamepadSources(std::span<HostBinding> bindings,
                        GamepadButton first, GamepadButton second) noexcept;

[[nodiscard]] bool IsHostBindingHeld(
    const HostBinding& binding,
    const HostDeviceEnablement& enabled,
    const HostButtonSource& source) noexcept;

struct DeviceDescriptor {
    std::int32_t InstanceId = -1;
    std::string Guid;
    std::string Name;
    bool HasGyroscope = false;
    bool HasAccelerometer = false;
};

struct MotionObservation {
    std::array<float, 3> GyroscopeDegreesPerSecond{};
    std::array<float, 3> Accelerometer{0.0F, -1.0F, 0.0F};
    bool GyroscopeValid = false;
    bool AccelerometerValid = false;
};

struct PhysicalInputState {
    std::int16_t LeftStickX = 0;
    std::int16_t LeftStickY = 0;
    std::int16_t RightStickX = 0;
    std::int16_t RightStickY = 0;
    std::int32_t MouseDeltaX = 0;
    std::int32_t MouseDeltaY = 0;
    double SamplePeriodSeconds = 1.0 / 60.0;
    MotionObservation ControllerMotion;
};

struct MappingConfig {
    AnalogStick CirclePadSource = AnalogStick::Left;
    std::int32_t CirclePadDeadZonePercent = 18;
    std::int32_t CStickDeadZonePercent = 20;
    MotionSource NativeMotionSource = MotionSource::Automatic;
    MotionSource CStickSource = MotionSource::Mouse;
    float MouseMotionDegreesPerPixel = 0.35F;
    float MouseCStickUnitsPerPixel = 4.0F;
    float CStickMotionMaximumDegreesPerSecond = 180.0F;
    float ControllerGyroscopeSensitivity = 1.0F;
    float ControllerAccelerometerSensitivity = 1.0F;
    float CStickSensorSensitivity = 1.0F;
    bool NativeMotionInvertX = false;
    bool NativeMotionInvertY = false;
    std::array<float, 3> GyroscopeBiasDegreesPerSecond{};
    std::array<float, 3> AccelerometerNeutral{0.0F, -1.0F, 0.0F};
};

struct AimTransform {
    float CStickScale = 1.0F;
    float CStickSmoothingCoefficient = 1.0F;
    bool CStickInvertX = false;
    bool CStickInvertY = false;
};

struct CStickFilterState {
    float X = 0.0F;
    float Y = 0.0F;
};

// A virtual upright device: pitch about its local X, yaw about world up.
// Retain gravity when input stops; a fixed neutral accelerometer would make
// the guest sensor fusion undo the simulated rotation.
struct VirtualMotionState {
    double PitchRadians = 0.0;
    bool Active = false;
    void RestoreGravity(const std::array<float, 3>& gravity) noexcept;
};

enum class AxisInputKind : std::uint8_t {
    Absolute,
    Relative,
};

struct AxisInputSample {
    std::int16_t X = 0;
    std::int16_t Y = 0;
    AxisInputKind Kind = AxisInputKind::Absolute;
};

struct HidState {
    std::uint32_t Buttons = 0;
    std::int16_t CirclePadX = 0;
    std::int16_t CirclePadY = 0;
    std::uint16_t TouchX = 0;
    std::uint16_t TouchY = 0;
    bool TouchPressed = false;
    std::array<float, 3> Accelerometer{0.0F, -1.0F, 0.0F};
    std::array<float, 3> GyroscopeDegreesPerSecond{};
    bool AccelerometerValid = false;
    bool GyroscopeValid = false;
};

struct TouchMapping {
    std::uint16_t X = 0;
    std::uint16_t Y = 0;
    bool Inside = false;
    bool Pressed = false;
};

[[nodiscard]] TouchMapping MapPresentationPointToTouch(
    float pointX, float pointY, float presentationWidth,
    float presentationHeight, bool inside,
    bool pointerPressed) noexcept;

// This is the canonical cross-title input boundary. Standard HID and the
// Extra HID channels used by Circle Pad Pro/New 3DS are represented once.
struct InputFrame {
    HidState Hid;
    AxisInputSample CStick;
};

struct ExtraHidState {
    std::uint32_t Buttons = 0;
    std::int16_t CStickX = 0;
    std::int16_t CStickY = 0;
};

[[nodiscard]] HidState ProjectStandardHid(
    const InputFrame& frame) noexcept;
[[nodiscard]] ExtraHidState ProjectExtraHid(
    const InputFrame& frame, HardwareProfile profile) noexcept;

[[nodiscard]] bool IsButtonHeld(const InputFrame& frame,
                                Button button) noexcept;
void SetButtonHeld(InputFrame& frame, Button button,
                   bool held = true) noexcept;

InputFrame ResolveInput(const MappingConfig& config,
                        const PhysicalInputState& physical,
                        const DigitalState& digital,
                        const AimTransform& aimTransform = {},
                        CStickFilterState* cStickFilter = nullptr,
                        bool advanceCStickFilter = true,
                        VirtualMotionState* virtualMotion = nullptr) noexcept;

std::int16_t ConvertHostAxisToNative(std::int16_t value,
                                     std::int32_t deadZonePercent) noexcept;

class AxisInputAccumulator {
  public:
    void Observe(const AxisInputSample& sample) noexcept;
    AxisInputSample Consume() noexcept;
    void Reset() noexcept;

    [[nodiscard]] AxisInputSample Peek() const noexcept;
    [[nodiscard]] std::uint64_t RelativeSamplesObserved() const noexcept;
    [[nodiscard]] std::uint64_t RelativeSamplesConsumed() const noexcept;

  private:
    AxisInputSample mSample;
    std::uint64_t mRelativeSamplesObserved = 0;
    std::uint64_t mRelativeSamplesConsumed = 0;
};

bool ResolveGameplayPointerOwnership(bool nativeTouchOwned,
                                     bool hostGuiVisible,
                                     bool mouseEnabled,
                                     bool mappingUsesMouse) noexcept;

} // namespace ThreeDsRecomp::Input
