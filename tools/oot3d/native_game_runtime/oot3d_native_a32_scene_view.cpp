#include "oot3d_native_a32_scene_view.h"

#include "a32_runtime.h"

#include <bit>
#include <cmath>

namespace Oot3dNativeGame {
namespace {

constexpr uint32_t kPlayViewOffset = 0x0188U;
constexpr uint32_t kViewEyeOffset = 0x0030U;
constexpr uint32_t kViewAtOffset = 0x003CU;

bool ReadFloat(oot3d::recomp::a32::MemoryBus &memory, uint32_t address,
               float &value) {
  uint32_t encoded = 0;
  if (!memory.Read32(address, &encoded)) {
    return false;
  }
  value = std::bit_cast<float>(encoded);
  return std::isfinite(value);
}

bool ValidFrustum(float left, float right, float bottom, float top,
                  float nearPlane, float farPlane) {
  return std::isfinite(left) && std::isfinite(right) && std::isfinite(bottom) &&
         std::isfinite(top) && std::isfinite(nearPlane) &&
         std::isfinite(farPlane) && left != right && bottom != top &&
         nearPlane > 0.0F && farPlane > nearPlane;
}

} // namespace

void NativeA32SceneViewProbe::ObservePlayState(uint32_t playStateAddress) {
  if (!mExternalEnvironment && playStateAddress != 0U) {
    mStats.LastPlayStateAddress = playStateAddress;
  }
}

void NativeA32SceneViewProbe::UseExternalSceneEnvironment() {
  mExternalEnvironment = true;
  mStats.LastPlayStateAddress = 0;
}

void NativeA32SceneViewProbe::BeginExternalSceneView() {
  UseExternalSceneEnvironment();
  // Invalid camera/frustum data must not inherit the last scene's view.
  mPerspective.reset();
}

void NativeA32SceneViewProbe::ObserveExternalSceneEnvironment(
    std::optional<NativeSceneEnvironmentState> state) {
  UseExternalSceneEnvironment();
  if (!mPerspective || !mPerspective->CameraAvailable) state.reset();
  mEnvironment.Observe(state);
  if (!mPerspective) return;
  mPerspective->Serial = mNextSerial++;
  mPerspective->Environment = state;
  mPerspective->SceneId = state ? std::optional(state->SceneId) : std::nullopt;
  mPerspective->WorldIdentity = state ? mEnvironment.Epoch() : 0;
}

void NativeA32SceneViewProbe::ObservePerspective(
    uint32_t guestFunction, uint32_t guestReturnAddress, float left,
    float right, float bottom, float top, float nearPlane, float farPlane) {
  ++mStats.PerspectiveObservations;
  if (!ValidFrustum(left, right, bottom, top, nearPlane, farPlane)) {
    ++mStats.InvalidPerspectiveObservations;
    mLastError = "native perspective frustum is invalid";
    return;
  }

  NativeA32SceneViewSnapshot snapshot;
  snapshot.Serial = mNextSerial++;
  snapshot.GuestFunction = guestFunction;
  snapshot.GuestReturnAddress = guestReturnAddress;
  snapshot.Left = left;
  snapshot.Right = right;
  snapshot.Bottom = bottom;
  snapshot.Top = top;
  snapshot.NearPlane = nearPlane;
  snapshot.FarPlane = farPlane;
  mPerspective = snapshot;
  mLastError.clear();
}

void NativeA32SceneViewProbe::ObservePerspectiveCamera(
    uint32_t guestFunction, uint32_t guestReturnAddress, float left,
    float right, float bottom, float top, float nearPlane, float farPlane,
    const std::array<float, 3> &eye, const std::array<float, 3> &at) {
  ObservePerspective(guestFunction, guestReturnAddress, left, right, bottom,
                     top, nearPlane, farPlane);
  if (!mPerspective.has_value()) {
    return;
  }
  bool finite = true;
  for (size_t axis = 0; axis < 3U; ++axis) {
    finite = finite && std::isfinite(eye[axis]) && std::isfinite(at[axis]);
  }
  const float deltaX = at[0] - eye[0];
  const float deltaY = at[1] - eye[1];
  const float deltaZ = at[2] - eye[2];
  if (finite && deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ > 1.0e-8F) {
    mPerspective->Eye = eye;
    mPerspective->At = at;
    mPerspective->CameraAvailable = true;
  }
}

std::optional<NativeA32SceneViewSnapshot>
NativeA32SceneViewProbe::Capture(oot3d::recomp::a32::MemoryBus &memory) {
  if (!mPerspective.has_value()) {
    mLastError = "native perspective has not been observed";
    return std::nullopt;
  }

  NativeA32SceneViewSnapshot snapshot = *mPerspective;
  if (mExternalEnvironment || mStats.LastPlayStateAddress == 0U) {
    ++mStats.SnapshotsCaptured;
    mLastError.clear();
    return snapshot;
  }

  snapshot.PlayStateAddress = mStats.LastPlayStateAddress;
  // Same PlayState identity used by native scene transitions (not a room ID).
  uint16_t sceneId = 0xFFFFU;
  if (memory.Read16(snapshot.PlayStateAddress + 0x104U, &sceneId) &&
      sceneId < 0x8000U)
    snapshot.SceneId = sceneId;
  const uint32_t view = snapshot.PlayStateAddress + kPlayViewOffset;
  const bool read =
      ReadFloat(memory, view + kViewEyeOffset, snapshot.Eye[0]) &&
      ReadFloat(memory, view + kViewEyeOffset + 4U, snapshot.Eye[1]) &&
      ReadFloat(memory, view + kViewEyeOffset + 8U, snapshot.Eye[2]) &&
      ReadFloat(memory, view + kViewAtOffset, snapshot.At[0]) &&
      ReadFloat(memory, view + kViewAtOffset + 4U, snapshot.At[1]) &&
      ReadFloat(memory, view + kViewAtOffset + 8U, snapshot.At[2]);
  const float deltaX = snapshot.At[0] - snapshot.Eye[0];
  const float deltaY = snapshot.At[1] - snapshot.Eye[1];
  const float deltaZ = snapshot.At[2] - snapshot.Eye[2];
  if (!read || deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ <= 1.0e-8F) {
    ++mStats.ReadFailures;
    mLastError = "could not read a valid native PlayState View";
    return std::nullopt;
  }

  snapshot.CameraAvailable = true;
  ++mStats.SnapshotsCaptured;
  mLastError.clear();
  return snapshot;
}

void NativeA32SceneViewProbe::Reset() {
  mStats.LastPlayStateAddress = 0U;
  mPerspective.reset();
  mEnvironment.Reset();
  mLastError.clear();
}

const NativeA32SceneViewProbeStats &NativeA32SceneViewProbe::Stats() const {
  return mStats;
}

const std::string &NativeA32SceneViewProbe::LastError() const {
  return mLastError;
}

} // namespace Oot3dNativeGame
