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
               char* search = nullptr, std::size_t searchSize = 0, bool* listen = nullptr) {
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
    if (listen && ImGui::Button("Listen...", ImVec2(-1.0F, 0.0F))) {
      *listen = true;
      ImGui::CloseCurrentPopup();
      ImGui::EndCombo();
      return false;
    }
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

constexpr std::array<NativeKeyboardKey, 58> kKeyboardKeys{
    NativeKeyboardKey::None,       NativeKeyboardKey::W,
    NativeKeyboardKey::A,          NativeKeyboardKey::S,
    NativeKeyboardKey::D,          NativeKeyboardKey::Space,
    NativeKeyboardKey::Enter,      NativeKeyboardKey::Tab,
    NativeKeyboardKey::Backspace,
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
enum class BindingSlot { Primary, Alternate, Mouse, Gamepad };

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

  size_t PageCount() const noexcept override { return 7; }
  const char* PageLabel(size_t page) const noexcept override {
    static constexpr const char* labels[]{"Bindings", "Devices", "Analog sticks", "Camera", "Aiming", "Motion calibration", "Shortcuts"};
    return page < 7 ? labels[page] : "Controls";
  }
  void OnHidden() override {
    mControls->CancelBindingCapture();
    mControls->CancelMotionCalibration();
    mOpenCapture = false;
    mRetainedCapture = false;
  }
  bool CapturingInput() const override {
    using Phase = ThreeDsRecomp::Input::BindingCapturePhase;
    const auto phase = mControls->BindingCaptureStatus().Phase;
    return phase == Phase::Release || phase == Phase::Listening;
  }
  bool ReservesControllerBack() const override {
    const auto config = mControls->Snapshot().Config;
    return std::any_of(config.Bindings.begin(), config.Bindings.end(), [](const auto& binding) {
      return binding.Gamepad == NativeGamepadButton::Back;
    });
  }
  void DrawPage(size_t page) override { DrawImpl(static_cast<int>(page)); }
  void Draw() override { DrawImpl(-1); }

  void AppendSettingsPages(Fast::AppUi::Pages& pages) override {
    using namespace Fast::AppUi;
    const auto read=[r=mControls]{return r->Snapshot().Config;};
    const auto write=[r=mControls](NativeControlConfig config){config.Profile=NativeControlProfile::Custom;std::string error;r->Apply(config,&error);return error;};
    Page page{"devices","Devices",{}};
    auto add=[&](auto member,const char* id,const char* label,double lo=0,double hi=1,double step=1,std::vector<Option> options={}) {
      page.Fields.push_back(Member(id,label,member,read,write,lo,hi,step,std::move(options)));
    };
    add(&NativeControlConfig::KeyboardEnabled,"keyboard","Keyboard");
    add(&NativeControlConfig::MouseEnabled,"mouse","Mouse");
    add(&NativeControlConfig::ControllerEnabled,"controller","Controller");
    add(&NativeControlConfig::CaptureMouseInGameplay,"capture","Capture mouse during gameplay");
    Field devices{"preferred","Preferred controller",FieldKind::Choice,[read]{return read().PreferredControllerGuid;},
      [read,write](const std::string& guid){auto c=read();c.PreferredControllerGuid=guid;return write(c);}};
    devices.Options.push_back({"","Automatic"});
    for(const auto& device:mControls->DevicesSnapshot()) devices.Options.push_back({device.Guid,device.Name});
    if(!read().PreferredControllerGuid.empty() && std::none_of(devices.Options.begin(),devices.Options.end(),[&](const auto& option){return option.Value==read().PreferredControllerGuid;}))
      devices.Options.push_back({read().PreferredControllerGuid,"Saved controller (disconnected)"});
    page.Fields.push_back(std::move(devices));
    for(auto profile:kProfiles) page.Fields.push_back({std::string("preset-")+NativeControlProfileName(profile),
      std::string("Load preset: ")+NativeControlProfileName(profile),FieldKind::Action,[]{return "Apply preset";},
      [read,r=mControls,profile](const std::string&){
        const auto previous=read();auto c=NativeControlPreset(profile);
        c.PreferredControllerGuid=previous.PreferredControllerGuid;
        c.GyroscopeBiasDegreesPerSecond=previous.GyroscopeBiasDegreesPerSecond;c.AccelerometerNeutral=previous.AccelerometerNeutral;
        std::string error;r->Apply(c,&error);return error;
      }});
    page.Fields.push_back({"reload","Reload saved controls",FieldKind::Action,{},[r=mControls](const std::string&){std::string error;r->Reload(&error);return error;}});
    pages.push_back(std::move(page));page={"sticks","Analog sticks",{}};
    std::vector<Option> sticks,motion;
    for(auto value:kAnalogSticks) sticks.push_back({std::to_string(int(value)),NativeAnalogStickName(value)});
    for(auto value:kMotionSources) motion.push_back({std::to_string(int(value)),NativeMotionSourceName(value)});
    add(&NativeControlConfig::MovementStick,"movement","Movement stick",0,2,1,sticks);
    add(&NativeControlConfig::MovementStickDeadZonePercent,"movezone","Movement dead zone (%)",0,90);
    add(&NativeControlConfig::LookStickDeadZonePercent,"lookzone","Look dead zone (%)",0,90);
    add(&NativeControlConfig::TriggerDeadZonePercent,"triggerzone","Trigger dead zone (%)",0,90);
    pages.push_back(std::move(page));page={"aim","Aiming",{}};
    add(&NativeControlConfig::NativeAimSource,"source","Aim source",0,7,1,motion);
    add(&NativeControlConfig::MouseAimDegreesPerPixel,"sensitivity","Mouse degrees per pixel",.01,4,.01);
    add(&NativeControlConfig::RightStickAimMaximumDegreesPerSecond,"stick","Stick degrees per second",1,720);
    add(&NativeControlConfig::ControllerGyroscopeSensitivity,"gyro","Gyroscope sensitivity",.1,4,.1);
    add(&NativeControlConfig::ControllerAccelerometerSensitivity,"accel","Accelerometer sensitivity",.1,4,.1);
    add(&NativeControlConfig::NativeAimInvertX,"invertx","Invert aim X");
    add(&NativeControlConfig::NativeAimInvertY,"inverty","Invert aim Y");
    pages.push_back(std::move(page));page={"camera","Camera",{}};
    add(&NativeControlConfig::FreeCameraSource,"source","Free-camera source",0,7,1,motion);
    add(&NativeControlConfig::MouseFreeCameraUnitsPerPixel,"sensitivity","Free-camera mouse sensitivity",.25,16,.25);
    add(&NativeControlConfig::FreeCameraMotionSensitivity,"motion","Free-camera motion sensitivity",.1,4,.1);
    if(mTopScreen) {
      const auto topRead=[r=mTopScreen]{return r->Snapshot().Config;};
      const auto topWrite=[r=mTopScreen](const TopScreenUiConfig& c){std::string error;r->Apply(c,&error);return error;};
      const auto top=[&](auto member,const char* id,const char* label,double lo=0,double hi=1,std::vector<Option> options={}) {
        page.Fields.push_back(Member(id,label,member,topRead,topWrite,lo,hi,1,std::move(options)));
      };
      top(&TopScreenUiConfig::FreeCameraEnabled,"freecam","Free camera");
      top(&TopScreenUiConfig::FreeCameraSpeedLevel,"speed","Free-camera speed",1,5);
      top(&TopScreenUiConfig::FreeCameraSmoothing,"smoothing","Smoothing",0,4,{{"0","Off"},{"1","Light"},{"2","Medium"},{"3","Default"},{"4","Heavy"}});
      top(&TopScreenUiConfig::FreeCameraInvertX,"invertx","Invert camera X");
      top(&TopScreenUiConfig::FreeCameraInvertY,"inverty","Invert camera Y");
      top(&TopScreenUiConfig::CameraZoomPercent,"zoom","Camera zoom (%)",75,170);
      top(&TopScreenUiConfig::CameraFovPercent,"fov","Camera FOV (%)",70,140);
      pages.push_back(std::move(page));page={"shortcuts","TopScreen shortcuts",{}};
      top(&TopScreenUiConfig::CStickAimSpeedLevel,"aimspeed","C-stick aim speed",1,5);
      top(&TopScreenUiConfig::CStickAimInvertX,"aimx","Invert C-stick aim X");
      top(&TopScreenUiConfig::CStickAimInvertY,"aimy","Invert C-stick aim Y");
      for(int age=0;age<2;++age) for(size_t direction=0;direction<4;++direction) {
        const std::string name=std::string(age?"Adult ":"Child ")+std::array{"Up","Down","Left","Right"}[direction];
        Field f{name,name,FieldKind::Choice,[topRead,age,direction]{const auto c=topRead();return std::to_string(int((age?c.AdultDpad:c.ChildDpad)[direction]));},
          [topRead,topWrite,age,direction](const std::string& value){auto c=topRead();(age?c.AdultDpad:c.ChildDpad)[direction]=static_cast<TopScreenDpadAction>(std::stoi(value));return topWrite(c);}};
        for(int action=0;action<=13;++action) f.Options.push_back({std::to_string(action),TopScreenDpadActionName(static_cast<TopScreenDpadAction>(action))});
        page.Fields.push_back(std::move(f));
      }
    }
    pages.push_back(std::move(page));page={"calibration","Motion calibration",{}};
    page.Fields.push_back({"calibrate","Calibrate motion sensors",FieldKind::Action,[]{return "Start";},[r=mControls](const std::string&){r->BeginMotionCalibration();return std::string("Keep controller stationary");}});
    page.Fields.back().Enabled=[r=mControls]{const auto devices=r->DevicesSnapshot();return std::any_of(devices.begin(),devices.end(),[](const auto& device){return device.HasGyroscope||device.HasAccelerometer;});};
    page.Fields.push_back({"cancel","Cancel calibration",FieldKind::Action,{},[r=mControls](const std::string&){r->CancelMotionCalibration();return std::string();}});
    page.Fields.push_back({"reset","Reset calibration",FieldKind::Action,{},[r=mControls](const std::string&){std::string error;r->ResetMotionCalibration(&error);return error;}});
    page.Fields.push_back({"status","Calibration",FieldKind::Text,[r=mControls]{const auto s=r->CalibrationStatus();return s.Active?std::to_string(s.SamplesCollected)+" / "+std::to_string(s.SamplesRequired):s.LastCalibrationSucceeded?"Complete":"Idle";}});
    pages.push_back(std::move(page));
    for(int slot=0;slot<4;++slot) {
      page={"bindings"+std::to_string(slot),std::array{"Keyboard bindings","Alternate keys","Mouse bindings","Controller bindings"}[slot],{}};
      page.Fields.push_back({"capture-status","Assignment status",FieldKind::Text,[this]{return mRetainedCapture?std::string("Release inputs, then press the input to assign. Escape cancels."):mStatus;}});
      for(size_t action=0;action<kActionLabels.size();++action) {
        Field f{"binding"+std::to_string(action),kActionLabels[action],FieldKind::Choice,[read,action,slot]{const auto b=read().Bindings[action];return std::to_string(slot==0?int(b.KeyboardPrimary):slot==1?int(b.KeyboardSecondary):slot==2?int(b.Mouse):int(b.Gamepad));},
          [read,write,action,slot](const std::string& value){auto c=read();const int v=std::stoi(value);auto& b=c.Bindings[action];if(slot==0)b.KeyboardPrimary=static_cast<NativeKeyboardKey>(v);else if(slot==1)b.KeyboardSecondary=static_cast<NativeKeyboardKey>(v);else if(slot==2)b.Mouse=static_cast<NativeMouseButton>(v);else b.Gamepad=static_cast<NativeGamepadButton>(v);return write(c);}};
        if(slot<2)for(auto key:kKeyboardKeys) f.Options.push_back({std::to_string(int(key)),NativeKeyboardKeyName(key)});
        if(slot==2)for(auto key:kMouseButtons) f.Options.push_back({std::to_string(int(key)),NativeMouseButtonName(key)});
        if(slot==3)for(auto key:kGamepadButtons) f.Options.push_back({std::to_string(int(key)),NativeGamepadButtonName(key)});
        page.Fields.push_back(std::move(f));
        page.Fields.push_back({"listen"+std::to_string(action),std::string("Assign ")+kActionLabels[action],FieldKind::Action,[]{return "Listen";},
          [this,action,slot](const std::string&){BeginBindingCapture(action,static_cast<BindingSlot>(slot));mRetainedCapture=true;return std::string("Release inputs, then press the input to assign. Escape cancels.");}});
      }
      pages.push_back(std::move(page));
    }
  }
  void UpdateSettings() override {
    if(!mRetainedCapture)return;
    using Phase=ThreeDsRecomp::Input::BindingCapturePhase;
    const auto status=mControls->BindingCaptureStatus();
    if(status.Phase==Phase::Complete) {
      auto config=mControls->Snapshot().Config; auto& binding=config.Bindings[mCaptureAction];
      switch(mCaptureSlot) {
      case BindingSlot::Primary:binding.KeyboardPrimary=status.Binding.KeyboardPrimary;break;
      case BindingSlot::Alternate:binding.KeyboardSecondary=status.Binding.KeyboardPrimary;break;
      case BindingSlot::Mouse:binding.Mouse=status.Binding.Mouse;break;
      case BindingSlot::Gamepad:binding.Gamepad=status.Binding.Gamepad;break;
      }
      config.Profile=NativeControlProfile::Custom;
      mStatus.clear();
      if(mControls->Apply(config,&mStatus))mStatus="Binding saved";
      mControls->CancelBindingCapture();mRetainedCapture=false;
    } else if(status.Phase==Phase::Cancelled || ImGui::GetTime()-mCaptureStarted>20) {
      mControls->CancelBindingCapture();mRetainedCapture=false;mStatus="Assignment cancelled";
    }
  }

 private:
  bool mRetainedCapture=false;
  void DrawImpl(int page) {
    SynchronizeDrafts();
    const NativeControlConfig frameStartDraft = mControlDraft;
    if (mTopScreen) mTopDraft = mTopScreen->Snapshot().Config;
    const auto frameStartTopDraft = mTopDraft;

    DrawProfile();
    if (page >= 0) {
      ImGui::BeginChild("##ControlsBody", ImVec2(0.0F, std::max(1.0F,
          ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 3.0F)));
      switch (page) {
        case 0: DrawBindings(); break;
        case 1: DrawDevices(); DrawControllerSelection(); break;
        case 2: DrawAnalog(); break;
        case 3: DrawFreeCamera(); break;
        case 4: DrawNativeAim(); if (mTopScreen) DrawTopScreenStickAiming(mTopDraft); break;
        case 5: DrawCalibration(); break;
        case 6:
          if (mTopScreen) DrawTopScreenActionBindings(mTopDraft);
          else ImGui::TextDisabled("TopScreen profile is not active");
          break;
      }
      ImGui::EndChild();
    } else if (ImGui::BeginTabBar("##ControlSections")) {
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
    DrawBindingCapture();
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
  void BeginBindingCapture(std::size_t action, BindingSlot slot) {
    mCaptureAction = action;
    mCaptureSlot = slot;
    mOpenCapture = true;
    mCaptureStarted = ImGui::GetTime();
    using Device = ThreeDsRecomp::Input::BindingDevice;
    mControls->BeginBindingCapture(slot == BindingSlot::Mouse ? Device::Mouse :
        slot == BindingSlot::Gamepad ? Device::Gamepad : Device::Keyboard);
  }

  void DrawBindingCapture() {
    if (mOpenCapture) {
      ImGui::OpenPopup("Assign input");
      mOpenCapture = false;
    }
    if (!ImGui::BeginPopupModal("Assign input", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    using Phase = ThreeDsRecomp::Input::BindingCapturePhase;
    const auto status = mControls->BindingCaptureStatus();
    constexpr std::array<const char*, 4> slots{"Primary key", "Alternate key", "Mouse button", "Controller button"};
    ImGui::Text("%s: %s", kActionLabels[mCaptureAction], slots[static_cast<std::size_t>(mCaptureSlot)]);
    if (status.Phase == Phase::Complete) {
      auto& binding = mControlDraft.Bindings[mCaptureAction];
      switch (mCaptureSlot) {
        case BindingSlot::Primary: binding.KeyboardPrimary = status.Binding.KeyboardPrimary; break;
        case BindingSlot::Alternate: binding.KeyboardSecondary = status.Binding.KeyboardPrimary; break;
        case BindingSlot::Mouse: binding.Mouse = status.Binding.Mouse; break;
        case BindingSlot::Gamepad: binding.Gamepad = status.Binding.Gamepad; break;
      }
      MarkCustom(mControlDraft, mControlDirty);
      mControls->CancelBindingCapture();
      ImGui::CloseCurrentPopup();
    } else if (status.Phase == Phase::Cancelled) {
      ImGui::CloseCurrentPopup();
    } else {
      ImGui::TextUnformatted(status.Phase == Phase::Release ? "Release held inputs..." : "Press the input to assign...");
      if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
          ImGui::GetTime() - mCaptureStarted > 20.0) {
        mControls->CancelBindingCapture();
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::EndPopup();
  }

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
      bool listen = false;
      if (keyboard) {
        changed |= EnumCombo((std::string("##primary:") + kActionLabels[index]).c_str(), &binding.KeyboardPrimary, kKeyboardKeys,
                             NativeKeyboardKeyName, true, mKeySearch.data(), mKeySearch.size(), &listen);
        if (listen) BeginBindingCapture(index, BindingSlot::Primary);
        listen = false;
        ImGui::TableSetColumnIndex(2);
        ImGui::SetNextItemWidth(-1.0F);
        changed |= EnumCombo((std::string("##alternate:") + kActionLabels[index]).c_str(), &binding.KeyboardSecondary, kKeyboardKeys,
                             NativeKeyboardKeyName, true, mKeySearch.data(), mKeySearch.size(), &listen);
        if (listen) BeginBindingCapture(index, BindingSlot::Alternate);
      } else if (mBindingDevice == BindingDevice::Mouse) {
        changed |= EnumCombo((std::string("##mouse:") + kActionLabels[index]).c_str(), &binding.Mouse, kMouseButtons,
                             NativeMouseButtonName, true, mKeySearch.data(), mKeySearch.size(), &listen);
        if (listen) BeginBindingCapture(index, BindingSlot::Mouse);
      } else {
        changed |= EnumCombo((std::string("##gamepad:") + kActionLabels[index]).c_str(), &binding.Gamepad, kGamepadButtons,
                             NativeGamepadButtonName, true, mKeySearch.data(), mKeySearch.size(), &listen);
        if (listen) BeginBindingCapture(index, BindingSlot::Gamepad);
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
  std::size_t mCaptureAction = 0;
  BindingSlot mCaptureSlot = BindingSlot::Primary;
  bool mOpenCapture = false;
  double mCaptureStarted = 0.0;
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
