#pragma once

#include "oot3d_top_screen_config.h"
#include "oot3d_ui/ui_primitives.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Oot3dNativeGame {

struct TopScreenCameraVec3 {
  float X = 0.0F;
  float Y = 0.0F;
  float Z = 0.0F;
};

struct TopScreenCameraFovOwnershipInput {
  std::uint32_t OutputAddress = 0U;
  std::int16_t CameraSetting = 0;
  std::int16_t CameraIndex = -1;
};

struct TopScreenCameraFovOwnership {
  bool Active = false;
  std::uint32_t ValueAddress = 0U;
};

// Mirrors the 2.1.1 camera hook owner predicate. Only camera index zero and
// settings accepted by the profile may alter the main camera's FOV value.
TopScreenCameraFovOwnership ResolveTopScreenCameraFovOwnership(
    const TopScreenCameraFovOwnershipInput &input) noexcept;
float ResolveTopScreenCameraFovDegrees(float nativeDegrees,
                                       std::uint8_t percent) noexcept;

float TopScreenBinarySine(std::uint16_t angle) noexcept;
float TopScreenBinaryCosine(std::uint16_t angle) noexcept;

struct TopScreenFreeCameraState {
  bool Enabled = false;
  bool Active = false;
  bool N64StyleZoom = false;
  std::uint8_t ZoomPercent = 100U;
  std::uint8_t FovPercent = 100U;
  std::int16_t Pitch = 0;
  std::int16_t Yaw = 0;
  float Distance = 0.0F;
  std::uint8_t SpeedOption = 3;
  std::uint8_t Speed = 6;
  std::uint8_t Inversion = 0;
  std::uint8_t SpeedNotificationFrames = 0;
  std::uint8_t InversionNotificationFrames = 0;
};

struct TopScreenFreeCameraOptionsInput {
  bool ShoulderChordHeld = false;
  bool SpeedUpPressed = false;
  bool SpeedDownPressed = false;
  bool InversionPreviousPressed = false;
  bool InversionNextPressed = false;
  bool N64StyleZoomTogglePressed = false;
};

void UpdateTopScreenFreeCameraOptions(
    TopScreenFreeCameraState &state,
    const TopScreenFreeCameraOptionsInput &input) noexcept;
void ApplyTopScreenFreeCameraConfig(
    TopScreenFreeCameraState &state,
    const TopScreenUiConfig &config) noexcept;
std::size_t AppendTopScreenFreeCameraOptionPresentation(
    const TopScreenFreeCameraState &state,
    std::vector<oot3d::ui::UiPrimitive> &output);

struct TopScreenFreeCameraOwnershipInput {
  std::int32_t GameMode = 0;
  std::int32_t EntranceIndex = 0;
  std::int32_t CutsceneIndex = 0;
  std::int16_t Scene = 0;
  bool IsMainCamera = false;
  bool HasPlayer = false;
  std::uint32_t PlayerStateFlags1 = 0;
  std::uint32_t PlayerStateFlags2 = 0;
  std::int16_t CameraStatus = 0;
  std::int16_t CameraSetting = 0;
  std::int16_t CameraPitch = 0;
  std::int16_t CameraYaw = 0;
  float CameraDistance = 0.0F;
  std::int16_t RightStickX = 0;
  std::int16_t RightStickY = 0;
  bool RelativeInput = false;
};

enum TopScreenFreeCameraBlocker : std::uint32_t {
  TopScreenFreeCameraBlockerNone = 0,
  TopScreenFreeCameraBlockerGameplay = 1U << 0U,
  TopScreenFreeCameraBlockerScene = 1U << 1U,
  TopScreenFreeCameraBlockerSecondaryCamera = 1U << 2U,
  TopScreenFreeCameraBlockerMissingPlayer = 1U << 3U,
  TopScreenFreeCameraBlockerPlayerState1 = 1U << 4U,
  TopScreenFreeCameraBlockerPlayerState2 = 1U << 5U,
  TopScreenFreeCameraBlockerStatus = 1U << 6U,
  TopScreenFreeCameraBlockerSetting = 1U << 7U,
};

bool IsTopScreenFreeCameraGameplayAllowed(std::int32_t gameMode,
                                          std::int32_t entranceIndex,
                                          std::int32_t cutsceneIndex) noexcept;
bool IsTopScreenFreeCameraSettingBlocked(std::int16_t setting) noexcept;
std::uint32_t ResolveTopScreenFreeCameraBlockers(
    const TopScreenFreeCameraOwnershipInput &input) noexcept;
bool UpdateTopScreenFreeCameraOwnership(
    TopScreenFreeCameraState &state,
    const TopScreenFreeCameraOwnershipInput &input) noexcept;

struct TopScreenFreeCameraOrbitInput {
  TopScreenCameraVec3 PlayerPosition;
  TopScreenCameraVec3 CurrentAt;
  TopScreenCameraVec3 CurrentEye;
  bool ChildLink = false;
  bool Hanging = false;
  bool MasterQuest = false;
  std::int16_t RightStickX = 0;
  std::int16_t RightStickY = 0;
  bool RelativeInput = false;
};

struct TopScreenFreeCameraOrbit {
  TopScreenCameraVec3 At;
  TopScreenCameraVec3 IntendedEye;
};

TopScreenFreeCameraOrbit BuildTopScreenFreeCameraOrbit(
    TopScreenFreeCameraState &state,
    const TopScreenFreeCameraOrbitInput &input) noexcept;
TopScreenCameraVec3
InterpolateTopScreenFreeCameraPosition(const TopScreenCameraVec3 &current,
                                       const TopScreenCameraVec3 &target,
                                       float step) noexcept;

} // namespace Oot3dNativeGame
