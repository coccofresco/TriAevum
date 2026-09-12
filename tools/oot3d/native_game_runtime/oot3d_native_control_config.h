#pragma once

#include "three_ds_input.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace Oot3dNativeGame {

inline constexpr std::string_view kNativeControlConfigSchema =
    "oot3d_native_controls_v1";

using NativeControlProfile = ThreeDsRecomp::Input::ControlProfile;

enum class NativeControlAction : std::uint8_t {
  MoveForward,
  MoveBackward,
  MoveLeft,
  MoveRight,
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
  Gear,
  Map,
  Items,
  LookUp,
  LookDown,
  LookLeft,
  LookRight,
  Count,
};

inline constexpr std::size_t kNativeControlActionCount =
    static_cast<std::size_t>(NativeControlAction::Count);

using NativeKeyboardKey = ThreeDsRecomp::Input::KeyboardKey;
using NativeMouseButton = ThreeDsRecomp::Input::MouseButton;
using NativeGamepadButton = ThreeDsRecomp::Input::GamepadButton;
using NativeAnalogStick = ThreeDsRecomp::Input::AnalogStick;
using NativeMotionSource = ThreeDsRecomp::Input::MotionSource;
using NativeControlBinding = ThreeDsRecomp::Input::HostBinding;

struct NativeControlConfig {
  NativeControlProfile Profile = NativeControlProfile::KeyboardMouse;
  bool KeyboardEnabled = true;
  bool MouseEnabled = true;
  bool ControllerEnabled = true;
  bool CaptureMouseInGameplay = true;
  std::array<NativeControlBinding, kNativeControlActionCount> Bindings{};
  NativeAnalogStick MovementStick = NativeAnalogStick::Left;
  std::int32_t MovementStickDeadZonePercent = 18;
  std::int32_t LookStickDeadZonePercent = 20;
  std::int32_t TriggerDeadZonePercent = 20;
  NativeMotionSource NativeAimSource = NativeMotionSource::Automatic;
  NativeMotionSource FreeCameraSource = NativeMotionSource::Mouse;
  float MouseAimDegreesPerPixel = 0.35F;
  float MouseFreeCameraUnitsPerPixel = 4.0F;
  float RightStickAimMaximumDegreesPerSecond = 180.0F;
  float ControllerGyroscopeSensitivity = 1.0F;
  float ControllerAccelerometerSensitivity = 1.0F;
  float FreeCameraMotionSensitivity = 1.0F;
  bool NativeAimInvertX = false;
  bool NativeAimInvertY = false;
  std::string PreferredControllerGuid;
  std::array<float, 3> GyroscopeBiasDegreesPerSecond{};
  std::array<float, 3> AccelerometerNeutral{0.0F, -1.0F, 0.0F};

  bool operator==(const NativeControlConfig&) const = default;
};

const char* NativeControlProfileName(NativeControlProfile profile) noexcept;
const char* NativeControlActionName(NativeControlAction action) noexcept;
const char* NativeKeyboardKeyName(NativeKeyboardKey key) noexcept;
const char* NativeMouseButtonName(NativeMouseButton button) noexcept;
const char* NativeGamepadButtonName(NativeGamepadButton button) noexcept;
const char* NativeAnalogStickName(NativeAnalogStick stick) noexcept;
const char* NativeMotionSourceName(NativeMotionSource source) noexcept;

bool ParseNativeControlProfile(std::string_view value,
                               NativeControlProfile* profile) noexcept;
bool ParseNativeControlAction(std::string_view value,
                              NativeControlAction* action) noexcept;
bool ParseNativeKeyboardKey(std::string_view value,
                            NativeKeyboardKey* key) noexcept;
bool ParseNativeMouseButton(std::string_view value,
                            NativeMouseButton* button) noexcept;
bool ParseNativeGamepadButton(std::string_view value,
                              NativeGamepadButton* button) noexcept;
bool ParseNativeAnalogStick(std::string_view value,
                            NativeAnalogStick* stick) noexcept;
bool ParseNativeMotionSource(std::string_view value,
                             NativeMotionSource* source) noexcept;

NativeControlConfig NativeControlPreset(NativeControlProfile profile);
NativeControlConfig NativeControlDefaults();
bool ValidateNativeControlConfig(const NativeControlConfig& config,
                                 std::string* error = nullptr);
bool ParseNativeControlConfigText(std::string_view text,
                                  NativeControlConfig* config,
                                  std::string* error = nullptr);
bool LoadNativeControlConfig(const std::filesystem::path& path,
                             NativeControlConfig* config,
                             std::string* error = nullptr);
bool SerializeNativeControlConfigText(const NativeControlConfig& config,
                                      std::string* text,
                                      std::string* error = nullptr);
bool SaveNativeControlConfig(const std::filesystem::path& path,
                             const NativeControlConfig& config,
                             std::string* error = nullptr);

using NativeControlDeviceDescriptor =
    ThreeDsRecomp::Input::DeviceDescriptor;
using NativeControlMotionObservation =
    ThreeDsRecomp::Input::MotionObservation;

struct NativeControlCalibrationStatus {
  bool Active = false;
  std::uint32_t SamplesCollected = 0;
  std::uint32_t SamplesRequired = 60;
  bool LastCalibrationSucceeded = false;
};

struct NativeControlConfigSnapshot {
  NativeControlConfig Config;
  std::uint64_t Revision = 0;
};

class NativeControlConfigRuntime final {
 public:
  NativeControlConfigRuntime(std::filesystem::path path,
                             NativeControlConfig initial);

  [[nodiscard]] NativeControlConfigSnapshot Snapshot() const;
  [[nodiscard]] const std::filesystem::path& Path() const noexcept;
  [[nodiscard]] bool Persistent() const noexcept;
  bool Preview(const NativeControlConfig& config,
               std::string* error = nullptr);
  bool Apply(const NativeControlConfig& config,
             std::string* error = nullptr);
  bool Reload(std::string* error = nullptr);

  void ObserveDevices(std::vector<NativeControlDeviceDescriptor> devices);
  [[nodiscard]] std::vector<NativeControlDeviceDescriptor>
  DevicesSnapshot() const;
  void ObserveMotion(const NativeControlMotionObservation& observation);
  void BeginMotionCalibration() noexcept;
  void CancelMotionCalibration() noexcept;
  bool ResetMotionCalibration(std::string* error = nullptr);
  [[nodiscard]] NativeControlCalibrationStatus CalibrationStatus() const;

  void BeginBindingCapture(ThreeDsRecomp::Input::BindingDevice device);
  void CancelBindingCapture();
  void ObserveBindingCapture(const ThreeDsRecomp::Input::HostButtonSource& source, bool cancel);
  [[nodiscard]] ThreeDsRecomp::Input::BindingCaptureSnapshot BindingCaptureStatus() const;

 private:
  std::filesystem::path mPath;
  mutable std::mutex mMutex;
  NativeControlConfig mConfig;
  std::uint64_t mRevision = 1;
  std::vector<NativeControlDeviceDescriptor> mDevices;
  NativeControlCalibrationStatus mCalibration;
  ThreeDsRecomp::Input::HostBindingCapture mBindingCapture;
  std::array<double, 3> mCalibrationGyroscopeSum{};
  std::array<double, 3> mCalibrationAccelerometerSum{};
  std::uint32_t mCalibrationGyroscopeSamples = 0;
  std::uint32_t mCalibrationAccelerometerSamples = 0;
};

} // namespace Oot3dNativeGame
