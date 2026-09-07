#include "oot3d_top_screen_camera.h"

#include "oot3d_ui/ui_backend.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <utility>

namespace Oot3dNativeGame {
namespace {

constexpr std::int32_t kRightStickDeadzoneSquared = 900;
constexpr std::uint8_t kOptionNotificationFrames = 30U;
constexpr std::string_view kCameraOptionGlyphSemantic =
    "oot3d/topscreen/camera_option_glyphs";

constexpr std::array<std::string_view, 4> kInversionLabels{
    "Normal", "Inverted X", "Inverted Y", "Both Inverted"};
constexpr std::array<std::string_view, 7> kSpeedLabels{
    "|......", ".|.....", "..|....", "...|...",
    "....|..", ".....|.", "......|"};

void AppendOptionLabel(std::string_view label, float y,
                       std::vector<oot3d::ui::UiPrimitive> &output) {
  constexpr float kAtlasWidth = 128.0F;
  constexpr float kAtlasHeight = 256.0F;
  constexpr float kCellWidth = 8.0F;
  constexpr float kCellHeight = 16.0F;
  constexpr float kGlyphWidth = 6.0F;
  constexpr float kGlyphHeight = 10.0F;
  constexpr float kLabelX = 10.0F;

  for (std::size_t index = 0; index < label.size(); ++index) {
    const auto glyph = static_cast<std::uint8_t>(label[index]);
    oot3d::ui::UiPrimitive primitive;
    primitive.subsystem = oot3d::ui::UiSubsystem::GameplayHud;
    primitive.role = oot3d::ui::UiPrimitiveRole::PauseText;
    primitive.owner_address = 0x005D024CU;
    primitive.source_quad = static_cast<std::uint32_t>(index);
    primitive.texture.semantic_name = kCameraOptionGlyphSemantic;
    primitive.destination = {kLabelX + static_cast<float>(index) * kGlyphWidth,
                             y, kGlyphWidth, kGlyphHeight};
    primitive.uv = {
        static_cast<float>(glyph & 0x0FU) * kCellWidth / kAtlasWidth,
        static_cast<float>(glyph >> 4U) * kCellHeight / kAtlasHeight,
        kGlyphWidth / kAtlasWidth, kGlyphHeight / kAtlasHeight};
    primitive.layer = 30U;
    output.push_back(std::move(primitive));
  }
}

} // namespace

float TopScreenBinarySine(std::uint16_t angle) noexcept {
  if (angle <= 0x4000U) {
    float theta = static_cast<float>(angle) * 0.0000958737992429F;
    const float theta2 = theta * theta;
    float result = theta;
    theta *= theta2 * 0.166666666667F;
    result -= theta;
    theta *= theta2 * 0.05F;
    result += theta;
    theta *= theta2 * 0.0238095238095F;
    return result - theta;
  }
  if (angle <= 0x8000U) {
    return TopScreenBinarySine(static_cast<std::uint16_t>(0x8000U - angle));
  }
  return -TopScreenBinarySine(static_cast<std::uint16_t>(angle - 0x8000U));
}

float TopScreenBinaryCosine(std::uint16_t angle) noexcept {
  return TopScreenBinarySine(static_cast<std::uint16_t>(angle + 0x4000U));
}

TopScreenCameraFovOwnership ResolveTopScreenCameraFovOwnership(
    const TopScreenCameraFovOwnershipInput &input) noexcept {
  TopScreenCameraFovOwnership ownership;
  ownership.Active = input.OutputAddress != 0U && input.CameraIndex == 0 &&
                     !IsTopScreenFreeCameraSettingBlocked(
                         input.CameraSetting);
  if (ownership.Active) {
    // Official 2.1.1 stores output + 0x198 and compares it with the value
    // address loaded by the camera projection owner.
    ownership.ValueAddress = input.OutputAddress + 0x198U;
  }
  return ownership;
}

float ResolveTopScreenCameraFovDegrees(float nativeDegrees,
                                       std::uint8_t percent) noexcept {
  if (!std::isfinite(nativeDegrees)) {
    return nativeDegrees;
  }
  const float multiplier = static_cast<float>(percent) * 0.01F;
  return std::clamp(nativeDegrees * multiplier, 20.0F, 110.0F);
}

namespace {

std::int16_t ClampPitch(float value) noexcept {
  return static_cast<std::int16_t>(std::clamp(value, -12288.0F, 12288.0F));
}

bool OutsideDeadzone(std::int16_t x, std::int16_t y) noexcept {
  const auto wideX = static_cast<std::int32_t>(x);
  const auto wideY = static_cast<std::int32_t>(y);
  return wideX * wideX + wideY * wideY > kRightStickDeadzoneSquared;
}

bool HasCameraInput(std::int16_t x, std::int16_t y,
                    bool relativeInput) noexcept {
  return relativeInput ? x != 0 || y != 0 : OutsideDeadzone(x, y);
}

} // namespace

bool IsTopScreenFreeCameraGameplayAllowed(std::int32_t gameMode,
                                          std::int32_t,
                                          std::int32_t cutsceneIndex) noexcept {
  // The PC profile never takes ownership of the title/demo player. Native
  // camera/player blockers below also exclude non-interactive gameplay states.
  return gameMode == 0 && cutsceneIndex < 0xFFF0;
}

void UpdateTopScreenFreeCameraOptions(
    TopScreenFreeCameraState &state,
    const TopScreenFreeCameraOptionsInput &input) noexcept {
  constexpr std::array<std::uint8_t, 7> kSpeeds{2U, 3U, 4U, 6U, 8U, 12U, 16U};
  if (input.N64StyleZoomTogglePressed) {
    state.ZoomPercent = state.ZoomPercent == 130U ? 100U : 130U;
    state.N64StyleZoom = state.ZoomPercent != 100U;
  }
  if (input.ShoulderChordHeld) {
    if (input.SpeedUpPressed && state.SpeedOption + 1U < kSpeeds.size()) {
      ++state.SpeedOption;
      state.SpeedNotificationFrames = kOptionNotificationFrames;
    }
    if (input.SpeedDownPressed && state.SpeedOption != 0U) {
      --state.SpeedOption;
      state.SpeedNotificationFrames = kOptionNotificationFrames;
    }
    if (input.InversionPreviousPressed) {
      --state.Inversion;
      state.InversionNotificationFrames = kOptionNotificationFrames;
    }
    if (input.InversionNextPressed) {
      ++state.Inversion;
      state.InversionNotificationFrames = kOptionNotificationFrames;
    }
    state.Inversion &= 3U;
    state.Speed = kSpeeds[state.SpeedOption];
  }
  if (state.SpeedNotificationFrames != 0U) {
    --state.SpeedNotificationFrames;
  }
  if (state.InversionNotificationFrames != 0U) {
    --state.InversionNotificationFrames;
  }
}

void ApplyTopScreenFreeCameraConfig(
    TopScreenFreeCameraState &state,
    const TopScreenUiConfig &config) noexcept {
  constexpr std::array<std::uint8_t, 7> kSpeeds{2U, 3U, 4U, 6U,
                                                8U, 12U, 16U};
  state.SpeedOption = static_cast<std::uint8_t>(std::min<std::size_t>(
      config.FreeCameraSpeedLevel, kSpeeds.size() - 1U));
  state.Speed = kSpeeds[state.SpeedOption];
  state.Inversion = static_cast<std::uint8_t>(
      (config.FreeCameraInvertX ? 1U : 0U) |
      (config.FreeCameraInvertY ? 2U : 0U));
  state.Enabled = config.FreeCameraEnabled;
  if (!state.Enabled) {
    state.Active = false;
  }
  state.ZoomPercent = config.CameraZoomPercent;
  state.FovPercent = config.CameraFovPercent;
  state.N64StyleZoom = state.ZoomPercent != 100U;
  state.SpeedNotificationFrames = 0U;
  state.InversionNotificationFrames = 0U;
}

std::size_t AppendTopScreenFreeCameraOptionPresentation(
    const TopScreenFreeCameraState &state,
    std::vector<oot3d::ui::UiPrimitive> &output) {
  const auto initialSize = output.size();
  if (state.InversionNotificationFrames != 0U) {
    AppendOptionLabel(kInversionLabels[state.Inversion & 3U], 10.0F, output);
  }
  if (state.SpeedNotificationFrames != 0U) {
    const float y = state.InversionNotificationFrames != 0U ? 20.0F : 10.0F;
    AppendOptionLabel(kSpeedLabels[std::min<std::size_t>(
                          state.SpeedOption, kSpeedLabels.size() - 1U)],
                      y, output);
  }
  return output.size() - initialSize;
}

bool IsTopScreenFreeCameraSettingBlocked(std::int16_t setting) noexcept {
  switch (setting) {
  case 0x14:
  case 0x15:
  case 0x19:
  case 0x1A:
  case 0x1B:
  case 0x1D:
  case 0x23:
  case 0x40:
  case 0x46:
    return true;
  default:
    return false;
  }
}

std::uint32_t ResolveTopScreenFreeCameraBlockers(
    const TopScreenFreeCameraOwnershipInput &input) noexcept {
  std::uint32_t blockers = TopScreenFreeCameraBlockerNone;
  if (!IsTopScreenFreeCameraGameplayAllowed(input.GameMode, input.EntranceIndex,
                                            input.CutsceneIndex)) {
    blockers |= TopScreenFreeCameraBlockerGameplay;
  }
  if (input.Scene == 0x45)
    blockers |= TopScreenFreeCameraBlockerScene;
  if (!input.IsMainCamera)
    blockers |= TopScreenFreeCameraBlockerSecondaryCamera;
  if (!input.HasPlayer)
    blockers |= TopScreenFreeCameraBlockerMissingPlayer;
  if ((input.PlayerStateFlags1 & 0x20938230U) != 0U)
    blockers |= TopScreenFreeCameraBlockerPlayerState1;
  if ((input.PlayerStateFlags2 & 0x00040000U) != 0U)
    blockers |= TopScreenFreeCameraBlockerPlayerState2;
  if (input.CameraStatus != 7)
    blockers |= TopScreenFreeCameraBlockerStatus;
  if (IsTopScreenFreeCameraSettingBlocked(input.CameraSetting))
    blockers |= TopScreenFreeCameraBlockerSetting;
  return blockers;
}

bool UpdateTopScreenFreeCameraOwnership(
    TopScreenFreeCameraState &state,
    const TopScreenFreeCameraOwnershipInput &input) noexcept {
  state.Distance = input.CameraDistance;
  if (!state.Enabled) {
    state.Active = false;
    return false;
  }
  if (!state.Active) {
    state.Pitch = input.CameraPitch;
    state.Yaw = input.CameraYaw;
  }
  if (HasCameraInput(input.RightStickX, input.RightStickY,
                     input.RelativeInput)) {
    state.Active = true;
  }
  if (ResolveTopScreenFreeCameraBlockers(input) !=
      TopScreenFreeCameraBlockerNone) {
    state.Active = false;
  }
  return state.Active;
}

TopScreenFreeCameraOrbit BuildTopScreenFreeCameraOrbit(
    TopScreenFreeCameraState &state,
    const TopScreenFreeCameraOrbitInput &input) noexcept {
  if (HasCameraInput(input.RightStickX, input.RightStickY,
                     input.RelativeInput)) {
    const std::int32_t invertX =
        (((state.Inversion & 1U) != 0U) != input.MasterQuest) ? -1 : 1;
    const std::int32_t invertY = (state.Inversion & 2U) != 0U ? -1 : 1;
    state.Yaw = static_cast<std::int16_t>(
        state.Yaw - input.RightStickX * state.Speed * invertX);
    state.Pitch = ClampPitch(static_cast<float>(state.Pitch) +
                             input.RightStickY * state.Speed * invertY);
  }

  TopScreenFreeCameraOrbit result;
  result.At = input.PlayerPosition;
  const float headHeight = input.ChildLink ? 38.0F : 50.0F;
  result.At.Y += headHeight * (input.Hanging ? 0.5F : 1.0F);
  result.IntendedEye = result.At;

  const auto pitch = static_cast<std::uint16_t>(state.Pitch);
  const auto yaw = static_cast<std::uint16_t>(state.Yaw);
  const float nativeTargetDistance =
      (input.ChildLink ? 150.0F : 200.0F) -
      50.0F * TopScreenBinarySine(pitch);
  const float targetDistance = nativeTargetDistance *
      static_cast<float>(state.ZoomPercent) * 0.01F;
  state.Distance += (targetDistance - state.Distance) * 0.5F;
  result.IntendedEye.X -=
      state.Distance * TopScreenBinaryCosine(pitch) * TopScreenBinarySine(yaw);
  result.IntendedEye.Y -= state.Distance * TopScreenBinarySine(pitch);
  result.IntendedEye.Z -= state.Distance * TopScreenBinaryCosine(pitch) *
                          TopScreenBinaryCosine(yaw);
  return result;
}

TopScreenCameraVec3
InterpolateTopScreenFreeCameraPosition(const TopScreenCameraVec3 &current,
                                       const TopScreenCameraVec3 &target,
                                       float step) noexcept {
  return {current.X + (target.X - current.X) * step,
          current.Y + (target.Y - current.Y) * step,
          current.Z + (target.Z - current.Z) * step};
}

} // namespace Oot3dNativeGame
