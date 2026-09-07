#include "oot3d_top_screen_camera_guest.h"

#include "oot3d_native_a32_memory.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <span>
#include <utility>

namespace Oot3dNativeGame {
namespace {

constexpr std::uint32_t kSaveContext = 0x00587958U;
constexpr std::uint32_t kGyroHudIcon = 0x004FC648U;
constexpr std::uint32_t kScratchAllocation = 0x100U;
constexpr std::uint32_t kAtScratchOffset = 0x20U;
constexpr std::uint32_t kEyeScratchOffset = 0x30U;
constexpr std::uint32_t kShakeScratchOffset = 0x60U;

void SetError(std::string *error, std::string message) {
  if (error != nullptr) *error = std::move(message);
}

bool ReadFloat(NativeA32Memory &memory, std::uint32_t address, float *value) {
  std::uint32_t bits = 0;
  if (value == nullptr || !memory.Read32(address, &bits)) return false;
  *value = std::bit_cast<float>(bits);
  return true;
}

bool WriteFloat(NativeA32Memory &memory, std::uint32_t address, float value) {
  return memory.Write32(address, std::bit_cast<std::uint32_t>(value));
}

bool ReadVec3(NativeA32Memory &memory, std::uint32_t address,
              TopScreenCameraVec3 *value) {
  return value != nullptr && ReadFloat(memory, address, &value->X) &&
         ReadFloat(memory, address + 4U, &value->Y) &&
         ReadFloat(memory, address + 8U, &value->Z);
}

bool WriteVec3(NativeA32Memory &memory, std::uint32_t address,
               const TopScreenCameraVec3 &value) {
  return WriteFloat(memory, address, value.X) &&
         WriteFloat(memory, address + 4U, value.Y) &&
         WriteFloat(memory, address + 8U, value.Z);
}

float Distance(const TopScreenCameraVec3 &a,
               const TopScreenCameraVec3 &b) noexcept {
  const float x = a.X - b.X;
  const float y = a.Y - b.Y;
  const float z = a.Z - b.Z;
  return std::sqrt(x * x + y * y + z * z);
}

std::int16_t DegreesToBinary(float degrees) noexcept {
  return static_cast<std::int16_t>(
      static_cast<std::int32_t>(degrees * 182.04167F + 0.5F));
}

bool ReadSigned16(NativeA32Memory &memory, std::uint32_t address,
                  std::int16_t *value) {
  std::uint16_t raw = 0;
  if (value == nullptr || !memory.Read16(address, &raw)) return false;
  *value = static_cast<std::int16_t>(raw);
  return true;
}

} // namespace

bool BeginTopScreenFreeCameraGuestUpdate(
    NativeA32Memory &memory, std::uint32_t outAddress,
    std::uint32_t cameraAddress, std::uint32_t stackAddress,
    std::int16_t rightStickX, std::int16_t rightStickY,
    bool relativeInput,
    TopScreenFreeCameraGuestRuntime *runtime, bool *active,
    std::string *error) {
  if (runtime == nullptr || active == nullptr || outAddress == 0U ||
      cameraAddress == 0U) {
    SetError(error, "invalid TopScreen camera guest update arguments");
    return false;
  }
  TopScreenFreeCameraOwnershipInput input;
  std::uint32_t globalContext = 0U;
  std::uint32_t player = 0U;
  TopScreenCameraVec3 at;
  TopScreenCameraVec3 eye;
  std::uint16_t scene = 0U;
  std::uint16_t status = 0U;
  std::uint16_t setting = 0U;
  std::uint16_t pitch = 0U;
  std::uint16_t yaw = 0U;
  std::uint32_t gameMode = 0U;
  std::uint32_t entrance = 0U;
  std::uint32_t cutscene = 0U;
  if (!memory.Read32(cameraAddress + 0xD4U, &globalContext) ||
      !memory.Read32(cameraAddress + 0xD8U, &player) ||
      !memory.Read16(globalContext + 0x104U, &scene) ||
      !memory.Read16(cameraAddress + 0x188U, &status) ||
      !memory.Read16(cameraAddress + 0x18AU, &setting) ||
      !memory.Read16(cameraAddress + 0x182U, &pitch) ||
      !memory.Read16(cameraAddress + 0x184U, &yaw) ||
      !ReadVec3(memory, cameraAddress + 0x80U, &at) ||
      !ReadVec3(memory, cameraAddress + 0x8CU, &eye) ||
      !memory.Read32(kSaveContext, &entrance) ||
      !memory.Read32(kSaveContext + 8U, &cutscene) ||
      !memory.Read32(kSaveContext + 0x14E4U, &gameMode)) {
    SetError(error, "cannot decode native TopScreen camera ownership state");
    return false;
  }
  input.GameMode = static_cast<std::int32_t>(gameMode);
  input.EntranceIndex = static_cast<std::int32_t>(entrance);
  input.CutsceneIndex = static_cast<std::int32_t>(cutscene);
  input.Scene = static_cast<std::int16_t>(scene);
  input.IsMainCamera = cameraAddress == globalContext + 0x364U;
  input.HasPlayer = player != 0U;
  input.CameraStatus = static_cast<std::int16_t>(status);
  input.CameraSetting = static_cast<std::int16_t>(setting);
  input.CameraPitch = static_cast<std::int16_t>(pitch);
  input.CameraYaw = static_cast<std::int16_t>(yaw);
  input.CameraDistance = Distance(at, eye);
  input.RightStickX = rightStickX;
  input.RightStickY = rightStickY;
  input.RelativeInput = relativeInput;
  if (player != 0U &&
      (!memory.Read32(player + 0x1710U, &input.PlayerStateFlags1) ||
       !memory.Read32(player + 0x1714U, &input.PlayerStateFlags2))) {
    SetError(error, "cannot decode native TopScreen player camera flags");
    return false;
  }

  runtime->LastOwnershipInput = input;
  runtime->LastOwnershipBlockers =
      ResolveTopScreenFreeCameraBlockers(input);
  *active = UpdateTopScreenFreeCameraOwnership(runtime->Camera, input);
  runtime->OutAddress = outAddress;
  runtime->CameraAddress = cameraAddress;
  runtime->GlobalContextAddress = globalContext;
  runtime->PlayerAddress = player;
  runtime->RightStickX = rightStickX;
  runtime->RightStickY = rightStickY;
  runtime->RelativeInput = relativeInput;
  runtime->OriginalStackAddress = stackAddress;
  runtime->ScratchAddress = stackAddress - kScratchAllocation;
  runtime->Phase = *active ? TopScreenCameraGuestPhase::Water
                           : TopScreenCameraGuestPhase::Idle;
  if (*active && !memory.IsWritable(runtime->ScratchAddress,
                                    kScratchAllocation)) {
    SetError(error, "TopScreen camera scratch stack is not writable");
    runtime->Phase = TopScreenCameraGuestPhase::Idle;
    return false;
  }
  return true;
}

bool ApplyTopScreenFreeCameraEnvironment(
    NativeA32Memory &memory, TopScreenFreeCameraGuestRuntime *runtime,
    std::string *error) {
  if (runtime == nullptr || runtime->Phase != TopScreenCameraGuestPhase::Water) {
    SetError(error, "TopScreen camera environment phase mismatch");
    return false;
  }
  const auto camera = runtime->CameraAddress;
  const auto global = runtime->GlobalContextAddress;
  std::uint16_t distortionFlags = 0U;
  std::uint16_t stateFlags = 0U;
  std::uint8_t hotRoomPrimary = 0U;
  std::uint8_t hotRoomSecondary = 0U;
  if (!memory.Read16(camera + 0x19AU, &distortionFlags) ||
      !memory.Read16(camera + 0x194U, &stateFlags) ||
      !memory.Read8(global + 0x4C32U, &hotRoomPrimary) ||
      !memory.Read8(global + 0x4C37U, &hotRoomSecondary)) {
    SetError(error, "cannot decode native TopScreen distortion state");
    return false;
  }
  distortionFlags &= 0xFFFEU;
  if (hotRoomPrimary == 3U || hotRoomSecondary != 0U) distortionFlags |= 1U;
  if (!memory.Write16(camera + 0x19AU, distortionFlags)) return false;

  if (distortionFlags != 0U) {
    float phaseStep = 0.0F;
    float xScale = 0.0F;
    float yScale = 0.0F;
    float speed = 0.0F;
    float scaleFactor = 1.0F;
    float speedFactor = 1.0F;
    if ((distortionFlags & 4U) != 0U) {
      std::uint16_t timer = 0U;
      if (!memory.Read16(camera + 0x198U, &timer)) return false;
      phaseStep = 170.0F;
      xScale = -0.01F;
      yScale = 0.01F;
      speed = 0.6F;
      scaleFactor = static_cast<float>(timer) / 60.0F;
    } else if ((distortionFlags & 8U) != 0U) {
      std::uint16_t timer = 0U;
      if (!memory.Read16(camera + 0x198U, &timer)) return false;
      phaseStep = -90.0F;
      xScale = -0.22F;
      yScale = 0.12F;
      speed = 0.1F;
      scaleFactor = static_cast<float>(timer) / 80.0F;
    } else if ((distortionFlags & 2U) != 0U) {
      float waterY = 0.0F;
      float eyeY = 0.0F;
      float speedRatio = 0.0F;
      if (!ReadFloat(memory, camera + 0x15CU, &waterY) ||
          !ReadFloat(memory, camera + 0x90U, &eyeY) ||
          !ReadFloat(memory, camera + 0x128U, &speedRatio)) return false;
      phaseStep = -18.5F;
      xScale = 0.09F;
      yScale = 0.09F;
      speed = 0.08F;
      scaleFactor = waterY - eyeY;
      scaleFactor = (scaleFactor > 150.0F ? 1.0F : scaleFactor / 150.0F) *
                        0.45F +
                    speedRatio * 0.45F;
      speedFactor = scaleFactor;
    } else if ((distortionFlags & 1U) != 0U) {
      phaseStep = 150.0F;
      xScale = -0.01F;
      yScale = 0.01F;
      speed = 0.6F;
    } else {
      runtime->Phase = TopScreenCameraGuestPhase::Interface;
      return true;
    }
    runtime->DistortionPhase = static_cast<std::uint16_t>(
        runtime->DistortionPhase + DegreesToBinary(phaseStep * 2.0F / 3.0F));
    speedFactor *= 2.0F / 3.0F;
    const float x = xScale * scaleFactor *
                        TopScreenBinarySine(runtime->DistortionPhase) +
                    1.0F;
    const float y = yScale * scaleFactor *
                        TopScreenBinaryCosine(runtime->DistortionPhase) +
                    1.0F;
    if (!WriteFloat(memory, global + 0x338U, x) ||
        !WriteFloat(memory, global + 0x33CU, y) ||
        !WriteFloat(memory, global + 0x35CU, speed * speedFactor) ||
        !memory.Write16(camera + 0x194U,
                        static_cast<std::uint16_t>(stateFlags | 0x40U))) {
      return false;
    }
  } else if ((stateFlags & 0x40U) != 0U) {
    if (!WriteFloat(memory, global + 0x338U, 1.0F) ||
        !WriteFloat(memory, global + 0x33CU, 1.0F) ||
        !WriteFloat(memory, global + 0x35CU, 1.0F) ||
        !memory.Write16(camera + 0x194U,
                        static_cast<std::uint16_t>(stateFlags & ~0x40U))) {
      return false;
    }
  }
  runtime->Phase = TopScreenCameraGuestPhase::Interface;
  return true;
}

bool PrepareTopScreenFreeCameraCollision(
    NativeA32Memory &memory, TopScreenFreeCameraGuestRuntime *runtime,
    std::string *error) {
  if (runtime == nullptr ||
      runtime->Phase != TopScreenCameraGuestPhase::Interface) {
    SetError(error, "TopScreen camera collision preparation phase mismatch");
    return false;
  }
  if (!memory.Write8(kGyroHudIcon, 0U)) return false;
  if (runtime->PlayerAddress == 0U) {
    runtime->Phase = TopScreenCameraGuestPhase::CameraData;
    return true;
  }
  const auto player = runtime->PlayerAddress;
  const auto camera = runtime->CameraAddress;
  std::uint32_t floorPoly = 0U;
  std::uint32_t flags1 = 0U;
  std::uint32_t linkAge = 0U;
  std::uint8_t masterQuest = 0U;
  TopScreenCameraVec3 position;
  TopScreenCameraVec3 currentAt;
  TopScreenCameraVec3 currentEye;
  if (!memory.Read32(player + 0x7CU, &floorPoly) ||
      !memory.Read32(player + 0x1710U, &flags1) ||
      !memory.Read32(kSaveContext + 4U, &linkAge) ||
      !memory.Read8(kSaveContext + 0xEU, &masterQuest) ||
      !ReadVec3(memory, player + 0x28U, &position) ||
      !ReadVec3(memory, camera + 0x80U, &currentAt) ||
      !ReadVec3(memory, camera + 0x8CU, &currentEye)) {
    SetError(error, "cannot decode native TopScreen orbit state");
    return false;
  }
  if (floorPoly == 0U &&
      (!memory.Write16(camera + 0x1A6U, 1U) ||
       !memory.Write16(camera + 0x192U, 0U))) return false;
  std::array<std::uint8_t, 20> world{};
  if (!memory.ReadBytes(player + 0x28U, world) ||
      !memory.WriteBytes(camera + 0xDCU, world)) return false;
  const auto orbit = BuildTopScreenFreeCameraOrbit(
      runtime->Camera,
      {position, currentAt, currentEye, linkAge != 0U,
       (flags1 & 0x2000U) != 0U, masterQuest != 0U,
       runtime->RightStickX, runtime->RightStickY,
       runtime->RelativeInput});
  const auto atAddress = runtime->ScratchAddress + kAtScratchOffset;
  const auto eyeAddress = runtime->ScratchAddress + kEyeScratchOffset;
  if (!WriteVec3(memory, atAddress, orbit.At) ||
      !WriteVec3(memory, eyeAddress, orbit.IntendedEye)) return false;
  runtime->Phase = TopScreenCameraGuestPhase::Collision;
  return true;
}

bool CommitTopScreenFreeCameraCollision(
    NativeA32Memory &memory, TopScreenFreeCameraGuestRuntime *runtime,
    std::string *error) {
  if (runtime == nullptr ||
      runtime->Phase != TopScreenCameraGuestPhase::Collision) {
    SetError(error, "TopScreen camera collision commit phase mismatch");
    return false;
  }
  TopScreenCameraVec3 intendedAt;
  TopScreenCameraVec3 intendedEye;
  TopScreenCameraVec3 currentAt;
  TopScreenCameraVec3 currentEye;
  const auto camera = runtime->CameraAddress;
  const auto global = runtime->GlobalContextAddress;
  if (!ReadVec3(memory, runtime->ScratchAddress + kAtScratchOffset,
                &intendedAt) ||
      !ReadVec3(memory, runtime->ScratchAddress + kEyeScratchOffset,
                &intendedEye) ||
      !ReadVec3(memory, camera + 0x80U, &currentAt) ||
      !ReadVec3(memory, camera + 0x8CU, &currentEye)) return false;
  const auto at = InterpolateTopScreenFreeCameraPosition(
      currentAt, intendedAt, 0.3F);
  const auto eye = InterpolateTopScreenFreeCameraPosition(
      currentEye, intendedEye, 0.3F);
  if (!WriteVec3(memory, camera + 0x80U, at) ||
      !WriteVec3(memory, camera + 0x8CU, eye) ||
      !WriteVec3(memory, camera + 0xA4U, eye) ||
      !WriteVec3(memory, global + 0x1C4U, at) ||
      !WriteVec3(memory, global + 0x1B8U, eye)) return false;
  std::array<std::uint8_t, 0x24> zeroShake{};
  if (!memory.WriteBytes(runtime->ScratchAddress + kShakeScratchOffset,
                         zeroShake)) return false;
  runtime->Phase = TopScreenCameraGuestPhase::Quake;
  return true;
}

bool ApplyTopScreenFreeCameraQuake(
    NativeA32Memory &memory, TopScreenFreeCameraGuestRuntime *runtime,
    bool quakeActive, std::string *error) {
  if (runtime == nullptr || runtime->Phase != TopScreenCameraGuestPhase::Quake) {
    SetError(error, "TopScreen camera quake phase mismatch");
    return false;
  }
  const auto global = runtime->GlobalContextAddress;
  std::uint16_t cameraSetting = 0U;
  float viewFov = 0.0F;
  if (!memory.Read16(runtime->CameraAddress + 0x18AU, &cameraSetting) ||
      !ReadFloat(memory, runtime->CameraAddress + 0x144U, &viewFov)) {
    return false;
  }
  const bool applyQuake = quakeActive && cameraSetting != 0x38U;
  if (applyQuake) {
    TopScreenCameraVec3 at;
    TopScreenCameraVec3 eye;
    TopScreenCameraVec3 atOffset;
    TopScreenCameraVec3 eyeOffset;
    std::int16_t fovOffset = 0;
    if (!ReadVec3(memory, global + 0x1C4U, &at) ||
        !ReadVec3(memory, global + 0x1B8U, &eye) ||
        !ReadVec3(memory, runtime->ScratchAddress + kShakeScratchOffset,
                  &atOffset) ||
        !ReadVec3(memory, runtime->ScratchAddress + kShakeScratchOffset + 0xCU,
                  &eyeOffset) ||
        !ReadSigned16(memory,
                      runtime->ScratchAddress + kShakeScratchOffset + 0x1CU,
                      &fovOffset)) return false;
    at = {at.X + atOffset.X, at.Y + atOffset.Y, at.Z + atOffset.Z};
    eye = {eye.X + eyeOffset.X, eye.Y + eyeOffset.Y, eye.Z + eyeOffset.Z};
    viewFov += static_cast<float>(fovOffset) * 0.00549325F;
    if (!WriteVec3(memory, global + 0x1C4U, at) ||
        !WriteVec3(memory, global + 0x1B8U, eye)) return false;
  }
  viewFov = ResolveTopScreenCameraFovDegrees(
      viewFov, runtime->Camera.FovPercent);
  if (!WriteVec3(memory, global + 0x1D0U, {0.0F, 1.0F, 0.0F}) ||
      !WriteFloat(memory, global + 0x198U, viewFov) ||
      !memory.Write16(runtime->OutAddress,
                      static_cast<std::uint16_t>(runtime->Camera.Pitch)) ||
      !memory.Write16(runtime->OutAddress + 2U,
                      static_cast<std::uint16_t>(runtime->Camera.Yaw)) ||
      !memory.Write16(runtime->OutAddress + 4U, 0U) ||
      !memory.Write16(runtime->CameraAddress + 0x17CU,
                      static_cast<std::uint16_t>(runtime->Camera.Pitch)) ||
      !memory.Write16(runtime->CameraAddress + 0x17EU,
                      static_cast<std::uint16_t>(runtime->Camera.Yaw)) ||
      !memory.Write16(runtime->CameraAddress + 0x180U, 0U) ||
      !memory.Write16(runtime->CameraAddress + 0x182U,
                      static_cast<std::uint16_t>(runtime->Camera.Pitch)) ||
      !memory.Write16(runtime->CameraAddress + 0x184U,
                      static_cast<std::uint16_t>(runtime->Camera.Yaw)) ||
      !memory.Write16(runtime->CameraAddress + 0x186U, 0U)) return false;
  runtime->Phase = TopScreenCameraGuestPhase::CameraData;
  return true;
}

bool ApplyTopScreenFreeCameraData(
    NativeA32Memory &memory, TopScreenFreeCameraGuestRuntime *runtime,
    std::int16_t cameraDataIndex, std::string *error) {
  if (runtime == nullptr ||
      runtime->Phase != TopScreenCameraGuestPhase::CameraData) {
    SetError(error, "TopScreen camera-data phase mismatch");
    return false;
  }
  if (runtime->PlayerAddress != 0U && cameraDataIndex != -1) {
    std::uint8_t bgId = 0U;
    float playerY = 0.0F;
    float floorHeight = 0.0F;
    std::uint32_t linkAge = 0U;
    if (!memory.Read8(runtime->PlayerAddress + 0x81U, &bgId) ||
        !ReadFloat(memory, runtime->PlayerAddress + 0x2CU, &playerY) ||
        !ReadFloat(memory, runtime->PlayerAddress + 0x84U, &floorHeight) ||
        !memory.Read32(kSaveContext + 4U, &linkAge)) return false;
    std::uint16_t newSetting = 0U;
    if (bgId == 0x32U) {
      std::uint32_t collisionHeader = 0U;
      std::uint32_t cameraDataList = 0U;
      const auto collision = runtime->GlobalContextAddress + 0xA98U;
      if (!memory.Read32(collision, &collisionHeader) ||
          collisionHeader == 0U ||
          !memory.Read32(collisionHeader + 0x24U, &cameraDataList) ||
          cameraDataList == 0U ||
          !memory.Read16(cameraDataList +
                             static_cast<std::uint32_t>(cameraDataIndex) * 8U,
                         &newSetting)) return false;
    }
    if (newSetting != 0U && (newSetting != 0x35U || linkAge != 0U) &&
        playerY - floorHeight < 2.0F) {
      std::uint16_t oldSetting = 0U;
      if (!memory.Read16(runtime->CameraAddress + 0x18AU, &oldSetting) ||
          !memory.Write16(runtime->CameraAddress + 0x190U,
                          static_cast<std::uint16_t>(cameraDataIndex))) {
        return false;
      }
      if (newSetting != oldSetting &&
          (!memory.Write16(runtime->CameraAddress + 0x19CU, oldSetting) ||
           !memory.Write16(runtime->CameraAddress + 0x18AU, newSetting))) {
        return false;
      }
    }
  }
  runtime->Phase = TopScreenCameraGuestPhase::Idle;
  return true;
}

void ResetTopScreenFreeCameraGuestUpdate(
    TopScreenFreeCameraGuestRuntime *runtime) noexcept {
  if (runtime == nullptr) return;
  runtime->Phase = TopScreenCameraGuestPhase::Idle;
  runtime->OutAddress = 0U;
  runtime->CameraAddress = 0U;
  runtime->GlobalContextAddress = 0U;
  runtime->PlayerAddress = 0U;
  runtime->OriginalStackAddress = 0U;
  runtime->ScratchAddress = 0U;
  runtime->RightStickX = 0;
  runtime->RightStickY = 0;
  runtime->RelativeInput = false;
}

} // namespace Oot3dNativeGame
