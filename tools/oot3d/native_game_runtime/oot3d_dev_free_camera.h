#pragma once

// A camera detached from Link (F1 > Dev Tools > Free camera, F6). While it is
// active it replaces the game's Camera_Update for the main camera, so the
// game itself draws, culls and animates from the free view; Link's input is
// withheld and drives the camera instead.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>

namespace Oot3dNativeGame {

struct DevFreeCameraView {
  std::array<float, 3> Eye{};
  std::array<float, 3> At{};
  // The game's binary angles (0x8000 = 180 degrees).
  int16_t Pitch = 0;
  int16_t Yaw = 0;
};

class DevFreeCamera {
public:
  static constexpr float kLookDistance = 100.0F;

  void SetActive(bool active) {
    std::scoped_lock lock(mMutex);
    if (active && !mActive) mInitialized = false;  // start from the game's view
    mActive = active;
  }
  [[nodiscard]] bool Active() const {
    std::scoped_lock lock(mMutex);
    return mActive;
  }
  [[nodiscard]] float Speed() const {
    std::scoped_lock lock(mMutex);
    return mSpeed;
  }
  void SetSpeed(float speed) {
    std::scoped_lock lock(mMutex);
    mSpeed = std::clamp(speed, 10.0F, 10000.0F);
  }
  // A fixed pose (developer launches: TRIAEVUM_DEV_FREECAM), active at once.
  void SetPose(const std::array<float, 3> &eye, float yawDegrees, float pitchDegrees) {
    std::scoped_lock lock(mMutex);
    mEye = eye;
    mYaw = std::remainder(yawDegrees, 360.0F);
    mPitch = std::clamp(pitchDegrees, -89.0F, 89.0F);
    mActive = true;
    mInitialized = true;
  }
  // Back to the game's current view on the next camera update.
  void ResetToGame() {
    std::scoped_lock lock(mMutex);
    mInitialized = false;
  }

  // Host side, once per refresh: movement in the camera's frame (forward,
  // right, up; -1..1 each, scaled by the speed and its multiplier) and look
  // deltas in degrees.
  void Move(float forward, float right, float up, float yawDegrees, float pitchDegrees, float seconds,
            float speedMultiplier) {
    std::scoped_lock lock(mMutex);
    if (!mActive || !mInitialized) return;
    mYaw = std::remainder(mYaw + yawDegrees, 360.0F);
    mPitch = std::clamp(mPitch + pitchDegrees, -89.0F, 89.0F);
    const auto f = Forward();
    // Right of the view, horizontal: yaw 0 faces +Z, so right is -X.
    const float yaw = mYaw * kRadians;
    const std::array<float, 3> r{-std::cos(yaw), 0.0F, std::sin(yaw)};
    const float distance = mSpeed * speedMultiplier * seconds;
    for (size_t axis = 0; axis < 3; ++axis)
      mEye[axis] += (f[axis] * forward + r[axis] * right + (axis == 1 ? up : 0.0F)) * distance;
  }

  // Guest side, at the main camera's Camera_Update: the view to write, the
  // first time taken from the game's own eye and at.
  [[nodiscard]] std::optional<DevFreeCameraView> ResolveView(const std::array<float, 3> &gameEye,
                                                             const std::array<float, 3> &gameAt) {
    std::scoped_lock lock(mMutex);
    if (!mActive) return std::nullopt;
    if (!mInitialized) {
      mEye = gameEye;
      const float dx = gameAt[0] - gameEye[0], dy = gameAt[1] - gameEye[1], dz = gameAt[2] - gameEye[2];
      const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
      mYaw = length > 1.0e-3F ? std::atan2(dx, dz) / kRadians : 0.0F;
      mPitch = length > 1.0e-3F ? std::asin(std::clamp(dy / length, -1.0F, 1.0F)) / kRadians : 0.0F;
      mInitialized = true;
    }
    DevFreeCameraView view;
    view.Eye = mEye;
    const auto f = Forward();
    for (size_t axis = 0; axis < 3; ++axis) view.At[axis] = mEye[axis] + f[axis] * kLookDistance;
    view.Yaw = static_cast<int16_t>(std::lround(mYaw * 32768.0F / 180.0F));
    view.Pitch = static_cast<int16_t>(std::lround(mPitch * 32768.0F / 180.0F));
    mLast = view;
    ++mApplied;
    return view;
  }
  // How many times the game took the free view (main camera updates): a
  // cutscene camera, such as a scene's intro, does not count.
  [[nodiscard]] uint64_t AppliedCount() const {
    std::scoped_lock lock(mMutex);
    return mApplied;
  }
  // The last view written to the game (the gizmo pass and the panel show it).
  [[nodiscard]] std::optional<DevFreeCameraView> LastView() const {
    std::scoped_lock lock(mMutex);
    if (!mActive || !mInitialized) return std::nullopt;
    return mLast;
  }

private:
  static constexpr float kRadians = 3.14159265F / 180.0F;
  [[nodiscard]] std::array<float, 3> Forward() const {
    const float yaw = mYaw * kRadians, pitch = mPitch * kRadians;
    return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
  }
  mutable std::mutex mMutex;
  bool mActive = false;
  bool mInitialized = false;
  std::array<float, 3> mEye{};
  float mYaw = 0.0F;    // degrees: 0 faces +Z, 90 faces +X
  float mPitch = 0.0F;  // degrees, positive up
  float mSpeed = 400.0F;  // world units per second
  DevFreeCameraView mLast;
  uint64_t mApplied = 0;
};

} // namespace Oot3dNativeGame
