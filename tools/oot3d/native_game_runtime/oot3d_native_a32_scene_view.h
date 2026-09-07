#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace oot3d::recomp::a32 {
class MemoryBus;
}

namespace Oot3dNativeGame {

struct NativeA32SceneViewSnapshot {
  uint64_t Serial = 0;
  uint32_t PlayStateAddress = 0;
  uint32_t GuestFunction = 0;
  uint32_t GuestReturnAddress = 0;
  float Left = 0.0F;
  float Right = 0.0F;
  float Bottom = 0.0F;
  float Top = 0.0F;
  float NearPlane = 0.0F;
  float FarPlane = 0.0F;
  std::array<float, 3> Eye{};
  std::array<float, 3> At{};
  bool CameraAvailable = false;
};

struct NativeA32SceneViewProbeStats {
  uint64_t PerspectiveObservations = 0;
  uint64_t InvalidPerspectiveObservations = 0;
  uint64_t SnapshotsCaptured = 0;
  uint64_t ReadFailures = 0;
  uint32_t LastPlayStateAddress = 0;
};

// Publishes the native hard-float frustum independently, then enriches it with
// the current PlayState View when gameplay has supplied one. The offsets are
// recovered code.bin contracts, not scene-specific data.
class NativeA32SceneViewProbe {
public:
  void ObservePlayState(uint32_t playStateAddress);
  void ObservePerspective(uint32_t guestFunction, uint32_t guestReturnAddress,
                          float left, float right, float bottom, float top,
                          float nearPlane, float farPlane);
  std::optional<NativeA32SceneViewSnapshot>
  Capture(oot3d::recomp::a32::MemoryBus &memory);
  void Reset();

  const NativeA32SceneViewProbeStats &Stats() const;
  const std::string &LastError() const;

private:
  NativeA32SceneViewProbeStats mStats;
  std::optional<NativeA32SceneViewSnapshot> mPerspective;
  uint64_t mNextSerial = 1;
  std::string mLastError;
};

} // namespace Oot3dNativeGame
