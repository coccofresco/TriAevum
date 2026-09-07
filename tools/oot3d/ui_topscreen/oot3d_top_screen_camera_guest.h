#pragma once

#include "oot3d_top_screen_camera.h"

#include <cstdint>
#include <string>

namespace Oot3dNativeGame {

class NativeA32Memory;

enum class TopScreenCameraGuestPhase : std::uint8_t {
  Idle,
  Water,
  Interface,
  Collision,
  Quake,
  CameraData,
};

struct TopScreenFreeCameraGuestRuntime {
  TopScreenFreeCameraState Camera;
  TopScreenFreeCameraOwnershipInput LastOwnershipInput;
  std::uint32_t LastOwnershipBlockers = TopScreenFreeCameraBlockerNone;
  TopScreenCameraGuestPhase Phase = TopScreenCameraGuestPhase::Idle;
  std::uint16_t DistortionPhase = 0;
  std::uint32_t OutAddress = 0;
  std::uint32_t CameraAddress = 0;
  std::uint32_t GlobalContextAddress = 0;
  std::uint32_t PlayerAddress = 0;
  std::uint32_t OriginalStackAddress = 0;
  std::uint32_t ScratchAddress = 0;
  std::int16_t RightStickX = 0;
  std::int16_t RightStickY = 0;
  bool RelativeInput = false;
};

bool BeginTopScreenFreeCameraGuestUpdate(
    NativeA32Memory &memory, std::uint32_t outAddress,
    std::uint32_t cameraAddress, std::uint32_t stackAddress,
    std::int16_t rightStickX, std::int16_t rightStickY,
    bool relativeInput,
    TopScreenFreeCameraGuestRuntime *runtime, bool *active,
    std::string *error = nullptr);

bool ApplyTopScreenFreeCameraEnvironment(
    NativeA32Memory &memory, TopScreenFreeCameraGuestRuntime *runtime,
    std::string *error = nullptr);
bool PrepareTopScreenFreeCameraCollision(
    NativeA32Memory &memory, TopScreenFreeCameraGuestRuntime *runtime,
    std::string *error = nullptr);
bool CommitTopScreenFreeCameraCollision(
    NativeA32Memory &memory, TopScreenFreeCameraGuestRuntime *runtime,
    std::string *error = nullptr);
bool ApplyTopScreenFreeCameraQuake(
    NativeA32Memory &memory, TopScreenFreeCameraGuestRuntime *runtime,
    bool quakeActive, std::string *error = nullptr);
bool ApplyTopScreenFreeCameraData(
    NativeA32Memory &memory, TopScreenFreeCameraGuestRuntime *runtime,
    std::int16_t cameraDataIndex, std::string *error = nullptr);

void ResetTopScreenFreeCameraGuestUpdate(
    TopScreenFreeCameraGuestRuntime *runtime) noexcept;

inline constexpr std::uint32_t kTopScreenCameraCheckWater = 0x002D06A0U;
inline constexpr std::uint32_t kTopScreenCameraUpdateInterface = 0x00330D84U;
inline constexpr std::uint32_t kTopScreenCameraBgCheckInfo = 0x003553FCU;
inline constexpr std::uint32_t kTopScreenCameraQuakeUpdate = 0x004787E8U;
inline constexpr std::uint32_t kTopScreenCameraGetDataId = 0x0047BFF8U;

} // namespace Oot3dNativeGame
