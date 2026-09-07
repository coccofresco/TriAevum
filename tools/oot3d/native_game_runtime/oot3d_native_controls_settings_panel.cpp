#include "oot3d_native_controls_settings_panel.h"
#include "oot3d_top_screen_control_widgets.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

namespace Oot3dNativeGame {
namespace {

template <typename Enum, std::size_t Size>
bool EnumCombo(const char* label, Enum* value,
               const std::array<Enum, Size>& values,
               const char* (*name)(Enum) noexcept) {
  std::size_t selected = values.size();
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (values[index] == *value) {
      selected = index;
      break;
    }
  }
  bool changed = false;
  if (ImGui::BeginCombo(label, name(*value))) {
    for (std::size_t index = 0; index < values.size(); ++index) {
      const bool current = index == selected;
      if (ImGui::Selectable(name(values[index]), current)) {
        *value = values[index];
        changed = true;
      }
      if (current) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
  return changed;
}

constexpr std::array<NativeControlProfile, 3> kProfiles{
    NativeControlProfile::Keyboard,
    NativeControlProfile::KeyboardMouse,
    NativeControlProfile::Controller,
};

constexpr std::array<NativeKeyboardKey, 59> kKeyboardKeys{
    NativeKeyboardKey::None,       NativeKeyboardKey::W,
    NativeKeyboardKey::A,          NativeKeyboardKey::S,
    NativeKeyboardKey::D,          NativeKeyboardKey::Space,
    NativeKeyboardKey::Enter,      NativeKeyboardKey::Tab,
    NativeKeyboardKey::Backspace,  NativeKeyboardKey::Escape,
    NativeKeyboardKey::Control,    NativeKeyboardKey::Shift,
    NativeKeyboardKey::RightShift, NativeKeyboardKey::Alt,
    NativeKeyboardKey::Q,          NativeKeyboardKey::E,
    NativeKeyboardKey::R,          NativeKeyboardKey::T,
    NativeKeyboardKey::Y,          NativeKeyboardKey::U,
    NativeKeyboardKey::I,          NativeKeyboardKey::O,
    NativeKeyboardKey::P,          NativeKeyboardKey::F,
    NativeKeyboardKey::G,          NativeKeyboardKey::H,
    NativeKeyboardKey::J,          NativeKeyboardKey::K,
    NativeKeyboardKey::L,          NativeKeyboardKey::Z,
    NativeKeyboardKey::X,          NativeKeyboardKey::C,
    NativeKeyboardKey::V,          NativeKeyboardKey::B,
    NativeKeyboardKey::N,          NativeKeyboardKey::M,
    NativeKeyboardKey::Num1,       NativeKeyboardKey::Num2,
    NativeKeyboardKey::Num3,       NativeKeyboardKey::Num4,
    NativeKeyboardKey::Num5,       NativeKeyboardKey::Num6,
    NativeKeyboardKey::Num7,       NativeKeyboardKey::Num8,
    NativeKeyboardKey::Num9,       NativeKeyboardKey::Num0,
    NativeKeyboardKey::Minus,      NativeKeyboardKey::Plus,
    NativeKeyboardKey::Comma,      NativeKeyboardKey::Period,
    NativeKeyboardKey::Slash,      NativeKeyboardKey::Numpad8,
    NativeKeyboardKey::Numpad4,    NativeKeyboardKey::Numpad6,
    NativeKeyboardKey::Numpad2,    NativeKeyboardKey::ArrowUp,
    NativeKeyboardKey::ArrowDown,  NativeKeyboardKey::ArrowLeft,
    NativeKeyboardKey::ArrowRight,
};

constexpr std::array<NativeMouseButton, 6> kMouseButtons{
    NativeMouseButton::None, NativeMouseButton::Left,
    NativeMouseButton::Middle, NativeMouseButton::Right,
    NativeMouseButton::Back, NativeMouseButton::Forward,
};

constexpr std::array<NativeGamepadButton, 18> kGamepadButtons{
    NativeGamepadButton::None,
    NativeGamepadButton::A,
    NativeGamepadButton::B,
    NativeGamepadButton::X,
    NativeGamepadButton::Y,
    NativeGamepadButton::Back,
    NativeGamepadButton::Guide,
    NativeGamepadButton::Start,
    NativeGamepadButton::LeftStick,
    NativeGamepadButton::RightStick,
    NativeGamepadButton::LeftShoulder,
    NativeGamepadButton::RightShoulder,
    NativeGamepadButton::DpadUp,
    NativeGamepadButton::DpadDown,
    NativeGamepadButton::DpadLeft,
    NativeGamepadButton::DpadRight,
    NativeGamepadButton::LeftTrigger,
    NativeGamepadButton::RightTrigger,
};

constexpr std::array<NativeAnalogStick, 3> kAnalogSticks{
    NativeAnalogStick::Disabled,
    NativeAnalogStick::Left,
    NativeAnalogStick::Right,
};

constexpr std::array<NativeMotionSource, 8> kMotionSources{
    NativeMotionSource::Disabled,
    NativeMotionSource::DigitalLook,
    NativeMotionSource::Mouse,
    NativeMotionSource::RightStick,
    NativeMotionSource::ControllerGyroscope,
    NativeMotionSource::ControllerAccelerometer,
    NativeMotionSource::ControllerMotion,
    NativeMotionSource::Automatic,
};

constexpr std::array<const char*, kNativeControlActionCount> kActionLabels{
    "Move forward", "Move backward", "Move left", "Move right",
    "A", "B", "X", "Y", "L", "R", "ZL", "ZR", "Select", "Start",
    "D-pad up", "D-pad down", "D-pad left", "D-pad right",
    "Gear page", "Map page", "Items page",
    "Look up", "Look down", "Look left", "Look right",
};

void MarkCustom(NativeControlConfig& config, bool& dirty) {
  config.Profile = NativeControlProfile::Custom;
  dirty = true;
}

bool SourceUses(NativeMotionSource configured,
                NativeMotionSource source) noexcept {
  return configured == source || configured == NativeMotionSource::Automatic;
}

class NativeControlsSettingsPanel final
    : public Fast::Oot3d::GraphicsSettingsPanelTab {
 public:
  NativeControlsSettingsPanel(
      std::shared_ptr<NativeControlConfigRuntime> controls,
      std::shared_ptr<TopScreenUiConfigRuntime> topScreen)
      : mControls(std::move(controls)), mTopScreen(std::move(topScreen)) {}

  [[nodiscard]] const char* Label() const noexcept override {
    return "Controls";
  }

  void Draw() override {
    SynchronizeDrafts();
    const NativeControlConfig frameStartDraft = mControlDraft;
    if (mTopScreen) mTopDraft = mTopScreen->Snapshot().Config;
    const auto frameStartTopDraft = mTopDraft;

    ImGui::TextUnformatted("Profile");
    NativeControlProfile presetSelection = mControlDraft.Profile;
    if (EnumCombo("Preset", &presetSelection, kProfiles,
                  NativeControlProfileName)) {
      const auto preferredController =
          mControlDraft.PreferredControllerGuid;
      const auto gyroBias = mControlDraft.GyroscopeBiasDegreesPerSecond;
      const auto accelNeutral = mControlDraft.AccelerometerNeutral;
      mControlDraft = NativeControlPreset(presetSelection);
      mControlDraft.PreferredControllerGuid = preferredController;
      mControlDraft.GyroscopeBiasDegreesPerSecond = gyroBias;
      mControlDraft.AccelerometerNeutral = accelNeutral;
      mControlDirty = true;
    }
    if (ImGui::BeginTabBar("##ControlSections")) {
      const auto section = [&](const char* label, auto draw) {
        if (ImGui::BeginTabItem(label)) {
          ImGui::BeginChild(label, ImVec2(0.0F, std::max(
              80.0F, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 4.0F)));
          ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.45F);
          draw();
          ImGui::PopItemWidth();
          ImGui::EndChild();
          ImGui::EndTabItem();
        }
      };
      section("Devices", [&] { DrawDevices(); DrawControllerSelection(); DrawAnalog(); });
      section("Bindings", [&] { DrawBindings(); });
      section("Aiming", [&] {
        DrawNativeAim();
        if (mTopScreen) DrawTopScreenStickAiming(mTopDraft);
      });
      section("Camera", [&] { DrawFreeCamera(); });
      section("Actions", [&] {
        if (mTopScreen) DrawTopScreenActionBindings(mTopDraft);
      });
      section("Motion", [&] { DrawCalibration(); });
      ImGui::EndTabBar();
    }
    if (mControlDraft != frameStartDraft) {
      std::string error;
      if (mControls->Preview(mControlDraft, &error)) {
        const auto preview = mControls->Snapshot();
        mControlDraft = preview.Config;
        mObservedControlRevision = preview.Revision;
        mStatus = "Live preview";
      } else {
        mStatus = std::move(error);
      }
    }
    if (mTopScreen && mTopDraft != frameStartTopDraft) {
      mTopScreen->Preview(mTopDraft);
      mTopDraft = mTopScreen->Snapshot().Config;
      mCameraDirty = true;
      mStatus = "Live preview";
    }
    DrawPersistence();
  }

 private:
  void DrawDevices() {
    bool deviceSelectionChanged = false;
    deviceSelectionChanged |=
        ImGui::Checkbox("Keyboard", &mControlDraft.KeyboardEnabled);
    deviceSelectionChanged |=
        ImGui::Checkbox("Mouse", &mControlDraft.MouseEnabled);
    deviceSelectionChanged |=
        ImGui::Checkbox("Controller", &mControlDraft.ControllerEnabled);
    if (deviceSelectionChanged) {
      MarkCustom(mControlDraft, mControlDirty);
    }
    if (ImGui::Checkbox("Capture mouse during gameplay",
                        &mControlDraft.CaptureMouseInGameplay)) {
      MarkCustom(mControlDraft, mControlDirty);
    }

  }

  void SynchronizeDrafts() {
    const auto controlSnapshot = mControls->Snapshot();
    if (!mInitialized ||
        controlSnapshot.Revision != mObservedControlRevision) {
      mControlDraft = controlSnapshot.Config;
      mObservedControlRevision = controlSnapshot.Revision;
    }
    mInitialized = true;
  }

  void DrawControllerSelection() {
    ImGui::SeparatorText("Controller");
    const auto devices = mControls->DevicesSnapshot();
    std::string preview = mControlDraft.PreferredControllerGuid.empty()
        ? "Automatic" : "Configured controller (disconnected)";
    for (const auto& device : devices) {
      if (!mControlDraft.PreferredControllerGuid.empty() &&
          device.Guid == mControlDraft.PreferredControllerGuid) {
        preview = device.Name;
        break;
      }
    }
    if (ImGui::BeginCombo("Active controller", preview.c_str())) {
      const bool automatic =
          mControlDraft.PreferredControllerGuid.empty();
      if (ImGui::Selectable("Automatic", automatic)) {
        mControlDraft.PreferredControllerGuid.clear();
        MarkCustom(mControlDraft, mControlDirty);
      }
      for (const auto& device : devices) {
        const bool selected =
            device.Guid == mControlDraft.PreferredControllerGuid;
        std::string label = device.Name + "##" +
                            std::to_string(device.InstanceId);
        if (ImGui::Selectable(label.c_str(), selected)) {
          mControlDraft.PreferredControllerGuid = device.Guid;
          MarkCustom(mControlDraft, mControlDirty);
        }
      }
      ImGui::EndCombo();
    }
    if (devices.empty()) {
      ImGui::TextDisabled("No SDL controller connected");
    } else {
      for (const auto& device : devices) {
        ImGui::BulletText(
            "%s%s%s", device.Name.c_str(),
            device.HasGyroscope ? " | gyro" : "",
            device.HasAccelerometer ? " | accelerometer" : "");
      }
    }
  }

  void DrawBindings() {
    if (!ImGui::CollapsingHeader("Bindings",
                                 ImGuiTreeNodeFlags_DefaultOpen)) {
      return;
    }
    size_t duplicates = 0;
    for (size_t a = 0; a < kNativeControlActionCount; ++a) {
      const auto& first = mControlDraft.Bindings[a];
      for (size_t b = a + 1; b < kNativeControlActionCount; ++b) {
        const auto& second = mControlDraft.Bindings[b];
        const auto sameKey = [](auto key, const auto& binding) {
          return key != NativeKeyboardKey::None &&
              (key == binding.KeyboardPrimary || key == binding.KeyboardSecondary);
        };
        if (sameKey(first.KeyboardPrimary, second) || sameKey(first.KeyboardSecondary, second) ||
            (first.Mouse != NativeMouseButton::None && first.Mouse == second.Mouse) ||
            (first.Gamepad != NativeGamepadButton::None && first.Gamepad == second.Gamepad)) {
          ++duplicates;
          ImGui::TextWrapped("Shared binding: %s / %s", kActionLabels[a], kActionLabels[b]);
        }
      }
    }
    if (duplicates != 0) ImGui::Separator();
    if (!ImGui::BeginTable(
            "##NativeControlBindings", 5,
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_SizingStretchProp |
                ImGuiTableFlags_ScrollY,
            ImVec2(0.0F, std::max(120.0F, ImGui::GetContentRegionAvail().y)))) {
      return;
    }
    ImGui::TableSetupColumn("Action");
    ImGui::TableSetupColumn("Key 1");
    ImGui::TableSetupColumn("Key 2");
    ImGui::TableSetupColumn("Mouse");
    ImGui::TableSetupColumn("Controller");
    ImGui::TableHeadersRow();
    for (std::size_t index = 0; index < kNativeControlActionCount; ++index) {
      auto& binding = mControlDraft.Bindings[index];
      ImGui::PushID(static_cast<int>(index));
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextWrapped("%s", kActionLabels[index]);
      ImGui::TableSetColumnIndex(1);
      ImGui::SetNextItemWidth(-1.0F);
      if (EnumCombo("##key1", &binding.KeyboardPrimary, kKeyboardKeys,
                    NativeKeyboardKeyName)) {
        MarkCustom(mControlDraft, mControlDirty);
      }
      ImGui::TableSetColumnIndex(2);
      ImGui::SetNextItemWidth(-1.0F);
      if (EnumCombo("##key2", &binding.KeyboardSecondary, kKeyboardKeys,
                    NativeKeyboardKeyName)) {
        MarkCustom(mControlDraft, mControlDirty);
      }
      ImGui::TableSetColumnIndex(3);
      ImGui::SetNextItemWidth(-1.0F);
      if (EnumCombo("##mouse", &binding.Mouse, kMouseButtons,
                    NativeMouseButtonName)) {
        MarkCustom(mControlDraft, mControlDirty);
      }
      ImGui::TableSetColumnIndex(4);
      ImGui::SetNextItemWidth(-1.0F);
      if (EnumCombo("##gamepad", &binding.Gamepad, kGamepadButtons,
                    NativeGamepadButtonName)) {
        MarkCustom(mControlDraft, mControlDirty);
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  void DrawAnalog() {
    ImGui::SeparatorText("Movement");
    bool changed =
        EnumCombo("Movement stick", &mControlDraft.MovementStick,
                  kAnalogSticks, NativeAnalogStickName);
    changed |= ImGui::SliderInt(
        "Movement dead zone",
        &mControlDraft.MovementStickDeadZonePercent, 0, 50, "%d%%");
    changed |= ImGui::SliderInt(
        "Look dead zone", &mControlDraft.LookStickDeadZonePercent,
        0, 50, "%d%%");
    changed |= ImGui::SliderInt(
        "Trigger dead zone", &mControlDraft.TriggerDeadZonePercent,
        0, 50, "%d%%");
    if (changed) {
      MarkCustom(mControlDraft, mControlDirty);
    }
  }

  void DrawNativeAim() {
    ImGui::SeparatorText("Native gyro aiming");
    bool changed =
        EnumCombo("Aim source", &mControlDraft.NativeAimSource,
                  kMotionSources, NativeMotionSourceName);
    if (SourceUses(mControlDraft.NativeAimSource,
                   NativeMotionSource::Mouse)) {
      changed |= ImGui::SliderFloat(
          "Mouse aim sensitivity",
          &mControlDraft.MouseAimDegreesPerPixel,
          0.01F, 2.0F, "%.2f deg/px",
          ImGuiSliderFlags_Logarithmic);
    }
    if (SourceUses(mControlDraft.NativeAimSource,
                   NativeMotionSource::RightStick) ||
        SourceUses(mControlDraft.NativeAimSource,
                   NativeMotionSource::DigitalLook)) {
      changed |= ImGui::SliderFloat(
          "Right-stick aim speed",
          &mControlDraft.RightStickAimMaximumDegreesPerSecond,
          30.0F, 720.0F, "%.0f deg/s");
    }
    if (SourceUses(mControlDraft.NativeAimSource,
                   NativeMotionSource::ControllerGyroscope) ||
        mControlDraft.NativeAimSource ==
            NativeMotionSource::ControllerMotion) {
      changed |= ImGui::SliderFloat(
          "Gyroscope sensitivity",
          &mControlDraft.ControllerGyroscopeSensitivity,
          0.1F, 4.0F, "%.2fx");
    }
    if (SourceUses(mControlDraft.NativeAimSource,
                   NativeMotionSource::ControllerAccelerometer) ||
        mControlDraft.NativeAimSource ==
            NativeMotionSource::ControllerMotion) {
      changed |= ImGui::SliderFloat(
          "Accelerometer sensitivity",
          &mControlDraft.ControllerAccelerometerSensitivity,
          0.1F, 4.0F, "%.2fx");
    }
    changed |= ImGui::Checkbox(
        "Invert native aim X", &mControlDraft.NativeAimInvertX);
    changed |= ImGui::Checkbox(
        "Invert native aim Y", &mControlDraft.NativeAimInvertY);
    if (changed) {
      MarkCustom(mControlDraft, mControlDirty);
    }
    ImGui::TextDisabled(
        "Samples are supplied through the native 3DS HID gyro and "
        "accelerometer rings.");
  }

  void DrawFreeCamera() {
    ImGui::SeparatorText("Free-camera input");
    if (mTopScreen == nullptr) {
      ImGui::TextDisabled("TopScreen profile is not active");
      return;
    }
    DrawTopScreenCameraBehavior(mTopDraft);
    const bool enabled = mTopDraft.FreeCameraEnabled;
    bool controlChanged =
        EnumCombo("Free-camera source",
                  &mControlDraft.FreeCameraSource,
                  kMotionSources, NativeMotionSourceName);
    if (SourceUses(mControlDraft.FreeCameraSource,
                   NativeMotionSource::Mouse)) {
      controlChanged |= ImGui::SliderFloat(
          "Free-camera mouse sensitivity",
          &mControlDraft.MouseFreeCameraUnitsPerPixel,
          0.25F, 16.0F, "%.2f",
          ImGuiSliderFlags_Logarithmic);
    }
    if (SourceUses(mControlDraft.FreeCameraSource,
                   NativeMotionSource::ControllerGyroscope) ||
        SourceUses(mControlDraft.FreeCameraSource,
                   NativeMotionSource::ControllerAccelerometer) ||
        mControlDraft.FreeCameraSource ==
            NativeMotionSource::ControllerMotion) {
      controlChanged |= ImGui::SliderFloat(
          "Free-camera motion sensitivity",
          &mControlDraft.FreeCameraMotionSensitivity,
          0.1F, 4.0F, "%.2fx");
    }
    if (controlChanged) {
      MarkCustom(mControlDraft, mControlDirty);
    }
    if (!enabled) {
      ImGui::TextDisabled("Free camera disabled");
    }
  }

  void DrawCalibration() {
    ImGui::SeparatorText("Motion calibration");
    const auto calibration = mControls->CalibrationStatus();
    if (!calibration.Active) {
      if (ImGui::Button("Calibrate controller motion")) {
        mControls->BeginMotionCalibration();
      }
    } else {
      ImGui::ProgressBar(
          static_cast<float>(calibration.SamplesCollected) /
              static_cast<float>(
                  std::max(1U, calibration.SamplesRequired)),
          ImVec2(-1.0F, 0.0F));
      if (ImGui::Button("Cancel calibration")) {
        mControls->CancelMotionCalibration();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset motion calibration")) {
      std::string error;
      if (!mControls->ResetMotionCalibration(&error)) {
        mStatus = std::move(error);
      } else {
        mStatus = "Motion calibration reset";
      }
    }
    ImGui::TextDisabled(
        "Keep the controller still in its neutral aiming position.");
  }

  void DrawPersistence() {
    ImGui::Separator();
    if (!mControls->Persistent()) {
      ImGui::BeginDisabled();
    }
    if (ImGui::Button("Save controls")) {
      std::string error;
      bool saved = mControls->Apply(mControlDraft, &error);
      if (saved && mTopScreen && mCameraDirty) {
        saved = mTopScreen->Apply(mTopScreen->Snapshot().Config, &error);
        if (saved) mCameraDirty = false;
      }
      if (saved) {
        mObservedControlRevision = mControls->Snapshot().Revision;
        mControlDirty = false;
        mStatus = "Saved";
      } else {
        mStatus = std::move(error);
      }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("%s", mControls->Path().string().c_str());
    ImGui::SameLine();
    if (ImGui::Button("Reload controls")) {
      std::string error;
      bool loaded = mControls->Reload(&error);
      if (loaded && mTopScreen) {
        loaded = mTopScreen->Reload(&error);
        if (loaded) mCameraDirty = false;
      }
      if (loaded) {
        mControlDirty = false;
        mStatus = "Reloaded";
      } else {
        mStatus = std::move(error);
      }
    }
    if (!mControls->Persistent()) {
      ImGui::EndDisabled();
    }
    if (mControlDirty || mCameraDirty) {
      ImGui::TextDisabled("Unsaved changes");
    }
    if (!mStatus.empty()) {
      ImGui::TextWrapped("%s", mStatus.c_str());
    }
  }

  std::shared_ptr<NativeControlConfigRuntime> mControls;
  std::shared_ptr<TopScreenUiConfigRuntime> mTopScreen;
  NativeControlConfig mControlDraft;
  TopScreenUiConfig mTopDraft;
  bool mCameraDirty = false;
  std::uint64_t mObservedControlRevision = 0;
  bool mInitialized = false;
  bool mControlDirty = false;
  std::string mStatus;
};

} // namespace

std::shared_ptr<Fast::Oot3d::GraphicsSettingsPanelTab>
CreateNativeControlsSettingsPanel(
    std::shared_ptr<NativeControlConfigRuntime> controls,
    std::shared_ptr<TopScreenUiConfigRuntime> topScreen) {
  return std::make_shared<NativeControlsSettingsPanel>(
      std::move(controls), std::move(topScreen));
}

} // namespace Oot3dNativeGame
