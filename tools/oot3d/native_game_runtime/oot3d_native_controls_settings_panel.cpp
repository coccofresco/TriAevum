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
               const char* (*name)(Enum) noexcept, bool cell = false,
               char* search = nullptr, std::size_t searchSize = 0) {
  std::size_t selected = values.size();
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (values[index] == *value) {
      selected = index;
      break;
    }
  }
  bool changed = false;
  const std::string id = cell ? label : ControlWidgets::Field(label);
  const auto preview = ControlWidgets::DisplayName(name(*value));
  if (ImGui::BeginCombo(id.c_str(), preview.c_str())) {
    if (search) {
      if (ImGui::IsWindowAppearing()) {
        search[0] = '\0';
        ImGui::SetKeyboardFocusHere();
      }
      ImGui::SetNextItemWidth(-1.0F);
      ImGui::InputTextWithHint("##BindingSearch", "Find input...", search, searchSize);
    }
    for (std::size_t index = 0; index < values.size(); ++index) {
      const auto option = ControlWidgets::DisplayName(name(values[index]));
      if (search && !ControlWidgets::Contains(option, search)) continue;
      const bool current = index == selected;
      if (ImGui::Selectable(option.c_str(), current)) {
        *value = values[index];
        changed = true;
      }
      if (current && !search) {
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

struct BindingGroup {
  const char* Name;
  std::size_t First;
  std::size_t End;
};
constexpr std::array<BindingGroup, 5> kBindingGroups{{
    {"Movement", 0, 4}, {"Game buttons", 4, 14}, {"D-pad", 14, 18},
    {"Menu shortcuts", 18, 21}, {"Look directions", 21, 25},
}};
enum class BindingDevice { Keyboard, Mouse, Controller };

bool SharesBinding(const NativeControlBinding& a, const NativeControlBinding& b,
                   BindingDevice device) {
  const auto keyIn = [&](auto key) {
    return key != NativeKeyboardKey::None &&
        (key == b.KeyboardPrimary || key == b.KeyboardSecondary);
  };
  if (device == BindingDevice::Keyboard)
    return keyIn(a.KeyboardPrimary) || keyIn(a.KeyboardSecondary);
  if (device == BindingDevice::Mouse)
    return a.Mouse != NativeMouseButton::None && a.Mouse == b.Mouse;
  return a.Gamepad != NativeGamepadButton::None && a.Gamepad == b.Gamepad;
}

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

    DrawProfile();
    if (ImGui::BeginTabBar("##ControlSections")) {
      const auto section = [&](const char* label, auto draw) {
        if (ImGui::BeginTabItem(label)) {
          // A single scrolling body; persistence never scrolls with the fields.
          ImGui::BeginChild(label, ImVec2(0.0F, std::max(
              1.0F, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 3.0F)));
          draw();
          ImGui::EndChild();
          ImGui::EndTabItem();
        }
      };
      section("Bindings", [&] { DrawBindings(); });
      section("Camera", [&] {
        if (ImGui::CollapsingHeader("Free camera", ImGuiTreeNodeFlags_DefaultOpen)) DrawFreeCamera();
        if (ImGui::CollapsingHeader("Aiming")) DrawNativeAim();
        if (mTopScreen && ImGui::CollapsingHeader("C-stick aiming")) DrawTopScreenStickAiming(mTopDraft);
      });
      section("Devices", [&] {
        DrawDevices(); DrawControllerSelection();
        if (ImGui::CollapsingHeader("Analog sticks and triggers")) DrawAnalog();
        if (ImGui::CollapsingHeader("Motion calibration")) DrawCalibration();
      });
      section("Shortcuts", [&] {
        if (mTopScreen) DrawTopScreenActionBindings(mTopDraft);
        else ImGui::TextDisabled("TopScreen profile is not active");
      });
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
  void DrawProfile() {
    if (ImGui::Button("Presets...")) {
      if (mControlDraft.Profile != NativeControlProfile::Custom) mPresetSelection = mControlDraft.Profile;
      ImGui::OpenPopup("Load control preset");
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(ControlWidgets::DisplayName(NativeControlProfileName(mControlDraft.Profile)).c_str());
    if (ImGui::BeginPopupModal("Load control preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::PushItemWidth(300.0F);
      EnumCombo("##Preset", &mPresetSelection, kProfiles, NativeControlProfileName, true);
      ImGui::PopItemWidth();
      ImGui::TextUnformatted("Replace bindings and device settings?");
      if (ImGui::Button("Apply preset")) {
        const auto previous = mControlDraft;
        mControlDraft = NativeControlPreset(mPresetSelection);
        mControlDraft.PreferredControllerGuid = previous.PreferredControllerGuid;
        mControlDraft.GyroscopeBiasDegreesPerSecond = previous.GyroscopeBiasDegreesPerSecond;
        mControlDraft.AccelerometerNeutral = previous.AccelerometerNeutral;
        mControlDirty = true;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
  }

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
    if (!mInitialized) {
      mSavedControls = controlSnapshot.Config;
      if (mTopScreen) mSavedTop = mTopScreen->Snapshot().Config;
    }
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
    if (ImGui::BeginCombo(ControlWidgets::Field("Active controller").c_str(), preview.c_str())) {
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
        ImGui::TextWrapped(
            "%s%s%s", device.Name.c_str(),
            device.HasGyroscope ? " | gyro" : "",
            device.HasAccelerometer ? " | accelerometer" : "");
      }
    }
  }

  void DrawBindings() {
    for (const auto& [label, device] : std::array{
             std::pair{"Keyboard", BindingDevice::Keyboard},
             std::pair{"Mouse", BindingDevice::Mouse},
             std::pair{"Controller", BindingDevice::Controller}}) {
      if (device != BindingDevice::Keyboard) ImGui::SameLine();
      if (ImGui::RadioButton(label, mBindingDevice == device)) mBindingDevice = device;
    }
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##ActionFilter", "Find control...", mActionFilter.data(), mActionFilter.size());
    const bool enabled = mBindingDevice == BindingDevice::Keyboard ? mControlDraft.KeyboardEnabled :
        mBindingDevice == BindingDevice::Mouse ? mControlDraft.MouseEnabled : mControlDraft.ControllerEnabled;
    if (!enabled) ImGui::TextDisabled("Device disabled");
    if (mBindingDevice == BindingDevice::Controller && ImGui::Button("Swap shoulders / triggers")) {
      ThreeDsRecomp::Input::SwapGamepadSources(
          mControlDraft.Bindings, NativeGamepadButton::LeftShoulder,
          NativeGamepadButton::LeftTrigger);
      ThreeDsRecomp::Input::SwapGamepadSources(
          mControlDraft.Bindings, NativeGamepadButton::RightShoulder,
          NativeGamepadButton::RightTrigger);
      MarkCustom(mControlDraft, mControlDirty);
    }
    if (mBindingDevice == BindingDevice::Controller && ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Exchange both pairs of controller bindings. Keyboard and mouse bindings are unchanged.");
    }
    ImGui::PushID(static_cast<int>(mBindingDevice));
    bool any = false;
    for (const auto& group : kBindingGroups) {
      bool matches = false;
      for (auto i = group.First; i < group.End; ++i)
        matches |= ControlWidgets::Contains(kActionLabels[i], mActionFilter.data());
      if (!matches) continue;
      any = true;
      if (mActionFilter[0]) ImGui::SetNextItemOpen(true);
      if (ImGui::CollapsingHeader(group.Name, ImGuiTreeNodeFlags_DefaultOpen)) DrawBindingGroup(group);
    }
    if (!any) ImGui::TextDisabled("No matching controls");
    ImGui::PopID();
  }

  void DrawBindingGroup(const BindingGroup& group) {
    const bool keyboard = mBindingDevice == BindingDevice::Keyboard;
    ImGui::PushID(group.Name);
    // Explicit weights and no persisted auto-fit widths break the feedback loop
    // between full-width combos and content-derived column sizing.
    if (!ImGui::BeginTable(
            "##BindingsV2", keyboard ? 3 : 2,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame |
                ImGuiTableFlags_NoSavedSettings)) {
      ImGui::PopID();
      return;
    }
    ImGui::TableSetupColumn("Game control", ImGuiTableColumnFlags_WidthStretch, 1.3F);
    ImGui::TableSetupColumn(keyboard ? "Primary key" : "Button", ImGuiTableColumnFlags_WidthStretch, keyboard ? 1.0F : 2.0F);
    if (keyboard) ImGui::TableSetupColumn("Alternate key", ImGuiTableColumnFlags_WidthStretch, 1.0F);
    ImGui::TableHeadersRow();
    for (std::size_t index = group.First; index < group.End; ++index) {
      if (!ControlWidgets::Contains(kActionLabels[index], mActionFilter.data())) continue;
      auto& binding = mControlDraft.Bindings[index];
      ImGui::PushID(static_cast<int>(index));
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
      ImGui::TextWrapped("%s", kActionLabels[index]);
      std::string shared;
      for (std::size_t other = 0; other < kNativeControlActionCount; ++other) {
        if (other != index && SharesBinding(binding, mControlDraft.Bindings[other], mBindingDevice)) {
          if (!shared.empty()) shared += ", ";
          shared += kActionLabels[other];
        }
      }
      if (!shared.empty()) {
        ImGui::TextDisabled("Shared");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Also assigned to: %s", shared.c_str());
      }
      ImGui::TableSetColumnIndex(1);
      ImGui::SetNextItemWidth(-1.0F);
      bool changed = false;
      if (keyboard) {
        changed |= EnumCombo((std::string("##primary:") + kActionLabels[index]).c_str(), &binding.KeyboardPrimary, kKeyboardKeys,
                             NativeKeyboardKeyName, true, mKeySearch.data(), mKeySearch.size());
        ImGui::TableSetColumnIndex(2);
        ImGui::SetNextItemWidth(-1.0F);
        changed |= EnumCombo((std::string("##alternate:") + kActionLabels[index]).c_str(), &binding.KeyboardSecondary, kKeyboardKeys,
                             NativeKeyboardKeyName, true, mKeySearch.data(), mKeySearch.size());
      } else if (mBindingDevice == BindingDevice::Mouse) {
        changed |= EnumCombo((std::string("##mouse:") + kActionLabels[index]).c_str(), &binding.Mouse, kMouseButtons,
                             NativeMouseButtonName, true, mKeySearch.data(), mKeySearch.size());
      } else {
        changed |= EnumCombo((std::string("##gamepad:") + kActionLabels[index]).c_str(), &binding.Gamepad, kGamepadButtons,
                             NativeGamepadButtonName, true, mKeySearch.data(), mKeySearch.size());
      }
      if (changed) MarkCustom(mControlDraft, mControlDirty);
      ImGui::PopID();
    }
    ImGui::EndTable();
    ImGui::PopID();
  }

  void DrawAnalog() {
    ImGui::SeparatorText("Movement");
    bool changed =
        EnumCombo("Movement stick", &mControlDraft.MovementStick,
                  kAnalogSticks, NativeAnalogStickName);
    changed |= ControlWidgets::SliderInt(
        "Movement dead zone",
        &mControlDraft.MovementStickDeadZonePercent, 0, 50, "%d%%");
    changed |= ControlWidgets::SliderInt(
        "Look dead zone", &mControlDraft.LookStickDeadZonePercent,
        0, 50, "%d%%");
    changed |= ControlWidgets::SliderInt(
        "Trigger dead zone", &mControlDraft.TriggerDeadZonePercent,
        0, 50, "%d%%");
    if (changed) {
      MarkCustom(mControlDraft, mControlDirty);
    }
  }

  void DrawNativeAim() {
    bool changed =
        EnumCombo("Aim source", &mControlDraft.NativeAimSource,
                  kMotionSources, NativeMotionSourceName);
    if (SourceUses(mControlDraft.NativeAimSource,
                   NativeMotionSource::Mouse)) {
      changed |= ControlWidgets::SliderFloat(
          "Mouse aim sensitivity",
          &mControlDraft.MouseAimDegreesPerPixel,
          0.01F, 2.0F, "%.2f deg/px",
          ImGuiSliderFlags_Logarithmic);
    }
    if (SourceUses(mControlDraft.NativeAimSource,
                   NativeMotionSource::RightStick) ||
        SourceUses(mControlDraft.NativeAimSource,
                   NativeMotionSource::DigitalLook)) {
      changed |= ControlWidgets::SliderFloat(
          "Right-stick aim speed",
          &mControlDraft.RightStickAimMaximumDegreesPerSecond,
          30.0F, 720.0F, "%.0f deg/s");
    }
    if (SourceUses(mControlDraft.NativeAimSource,
                   NativeMotionSource::ControllerGyroscope) ||
        mControlDraft.NativeAimSource ==
            NativeMotionSource::ControllerMotion) {
      changed |= ControlWidgets::SliderFloat(
          "Gyroscope sensitivity",
          &mControlDraft.ControllerGyroscopeSensitivity,
          0.1F, 4.0F, "%.2fx");
    }
    if (SourceUses(mControlDraft.NativeAimSource,
                   NativeMotionSource::ControllerAccelerometer) ||
        mControlDraft.NativeAimSource ==
            NativeMotionSource::ControllerMotion) {
      changed |= ControlWidgets::SliderFloat(
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
  }

  void DrawFreeCamera() {
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
      controlChanged |= ControlWidgets::SliderFloat(
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
      controlChanged |= ControlWidgets::SliderFloat(
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
    if (ImGui::Button("Reset motion calibration")) {
      std::string error;
      if (!mControls->ResetMotionCalibration(&error)) {
        mStatus = std::move(error);
      } else {
        mStatus = "Motion calibration reset";
      }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Calibrate with the controller still in its neutral aiming position.");
  }

  void DrawPersistence() {
    ImGui::Separator();
    if (!mControls->Persistent()) {
      ImGui::BeginDisabled();
    }
    if (ImGui::Button("Save controls")) {
      std::string error;
      bool saved = mControls->Apply(mControlDraft, &error);
      if (saved) mSavedControls = mControlDraft;
      if (saved && mTopScreen && mCameraDirty) {
        saved = mTopScreen->Apply(mTopScreen->Snapshot().Config, &error);
        if (saved) {
          mSavedTop = mTopScreen->Snapshot().Config;
          mCameraDirty = false;
        }
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
    if (!mControls->Persistent()) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Revert changes")) {
      std::string error;
      NativeControlConfig loadedControls = mSavedControls;
      TopScreenUiConfig loadedTop = mSavedTop;
      // Before the first save, revert to the initial runtime profile. A missing
      // file is distinct from an unreadable or invalid existing configuration.
      std::error_code fileError;
      const bool controlFile = mControls->Persistent() && std::filesystem::exists(mControls->Path(), fileError);
      bool loaded = !fileError && (!controlFile || LoadNativeControlConfig(mControls->Path(), &loadedControls, &error));
      if (fileError) error = fileError.message();
      if (loaded && mTopScreen && mCameraDirty) {
        const bool topFile = mTopScreen->Persistent() && std::filesystem::exists(mTopScreen->Path(), fileError);
        loaded = !fileError && (!topFile || LoadTopScreenUiConfig(mTopScreen->Path(), &loadedTop, &error));
        if (fileError) error = fileError.message();
      }
      if (loaded) {
        // Parse both files before applying either; a failed read must not leave
        // a half-reverted live configuration.
        loaded = mControls->Preview(loadedControls, &error);
      }
      if (loaded) {
        if (mTopScreen && mCameraDirty) {
          auto currentTop = mTopScreen->Snapshot().Config;
          CopyTopScreenControlSettings(loadedTop, currentTop);
          mTopScreen->Preview(currentTop);
        }
        mCameraDirty = false;
        mControlDirty = false;
        mSavedControls = loadedControls;
        mSavedTop = loadedTop;
        mStatus = "Restored saved controls";
      } else {
        mStatus = std::move(error);
      }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("Restore saved bindings, camera and shortcuts. HUD layout is unchanged.");
    ImGui::TextUnformatted(mControlDirty || mCameraDirty ? "Unsaved changes" : "No pending changes");
    if (!mStatus.empty() && mStatus != "Live preview") {
      ImGui::TextUnformatted(mStatus.c_str());
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", mStatus.c_str());
    }
  }

  std::shared_ptr<NativeControlConfigRuntime> mControls;
  std::shared_ptr<TopScreenUiConfigRuntime> mTopScreen;
  NativeControlConfig mControlDraft;
  TopScreenUiConfig mTopDraft;
  NativeControlConfig mSavedControls;
  TopScreenUiConfig mSavedTop;
  bool mCameraDirty = false;
  std::uint64_t mObservedControlRevision = 0;
  bool mInitialized = false;
  bool mControlDirty = false;
  std::string mStatus;
  NativeControlProfile mPresetSelection = NativeControlProfile::KeyboardMouse;
  BindingDevice mBindingDevice = BindingDevice::Keyboard;
  std::array<char, 96> mActionFilter{};
  std::array<char, 64> mKeySearch{};
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
