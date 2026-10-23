#pragma once

#include "native_scene_environment_state.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace oot3d::recomp::a32 {
class MemoryBus;
}

namespace Oot3dNativeGame {

// The file select keeps the title scene on the upper screen behind its cards
// (the title demo's PlayState goes on updating, Player_Update included; at
// other moments the block reads scene 0 while its View stays readable), but
// the screen is the file select's: its own scene identity for everything
// keyed by scene (the environment profiles, scene-00FF.environment.json), a
// number outside the title's scene table (0x00..0x6D).
inline constexpr uint16_t kOot3dFileSelectSceneId = 0x00FFU;
// FileSelect_Update runs every guest frame the file select is up; a guest
// frame spans at most two host refreshes. Its ownership outlives the last
// observation by this many refreshes, so the order of the title PlayState's
// update and the file select's within a refresh does not flip the identity.
inline constexpr uint32_t kOot3dFileSelectOwnershipRefreshes = 4U;

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
  std::optional<uint16_t> SceneId;
  std::optional<NativeSceneEnvironmentState> Environment;
  uint64_t WorldIdentity = 0;
  // The file select owns the view: SceneId is kOot3dFileSelectSceneId and the
  // PlayState fields (time, headers, actors) are not to be read.
  bool FileSelect = false;
};

struct NativeA32SceneViewProbeStats {
  uint64_t PerspectiveObservations = 0;
  uint64_t InvalidPerspectiveObservations = 0;
  uint64_t SnapshotsCaptured = 0;
  uint64_t ReadFailures = 0;
  uint32_t LastPlayStateAddress = 0;
  uint64_t FileSelectObservations = 0;
};

// Publishes the native hard-float frustum independently, then enriches it with
// the current PlayState View when gameplay has supplied one. The offsets are
// recovered code.bin contracts, not scene-specific data.
class NativeA32SceneViewProbe {
public:
  void ObservePlayState(uint32_t playStateAddress);
  // A title-owned producer supplies already-decoded data. Never use the
  // OoT PlayState memory layout in this mode, even after a savestate reset.
  void UseExternalSceneEnvironment();
  void BeginExternalSceneView();
  void ObserveExternalSceneEnvironment(std::optional<NativeSceneEnvironmentState> state);
  // FileSelect_Update ran outside play (a frontend SaveContext.game_mode; in
  // play PauseUi_Update calls it every frame, which is not the file select):
  // the file select owns the view for the next
  // kOot3dFileSelectOwnershipRefreshes guest refreshes, or until a reset.
  void ObserveFileSelect();
  // One host refresh of the guest begins (before it runs to its wait).
  void BeginGuestRefresh();
  void ObservePerspective(uint32_t guestFunction, uint32_t guestReturnAddress,
                          float left, float right, float bottom, float top,
                          float nearPlane, float farPlane);
  // A frustum with its camera, for a title whose camera is not read from an
  // OoT3D PlayState. The camera is kept only when it is finite and its eye
  // and target differ.
  void ObservePerspectiveCamera(uint32_t guestFunction,
                                uint32_t guestReturnAddress, float left,
                                float right, float bottom, float top,
                                float nearPlane, float farPlane,
                                const std::array<float, 3> &eye,
                                const std::array<float, 3> &at);
  std::optional<NativeA32SceneViewSnapshot>
  Capture(oot3d::recomp::a32::MemoryBus &memory);
  void Reset();

  const NativeA32SceneViewProbeStats &Stats() const;
  const std::string &LastError() const;

private:
  NativeA32SceneViewProbeStats mStats;
  uint32_t mFileSelectRefreshesLeft = 0;
  std::optional<NativeA32SceneViewSnapshot> mPerspective;
  uint64_t mNextSerial = 1;
  std::string mLastError;
  bool mExternalEnvironment = false;
  NativeSceneEnvironmentContinuity mEnvironment;
};

} // namespace Oot3dNativeGame
