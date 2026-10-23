#pragma once

#include "fast/renderer3ds/room_shadow_shell.h"
#include "oot3d_native_static_emitters.h"
#include <optional>
#include "oot3d_environment_group.h"

namespace Oot3dNativeGame {

// Bounded version-6 CMB adapter. Nothing is published on failure. Rigid bind
// transforms follow Rz*Ry*Rx*S and the parent chain. Unsupported/skinned opaque
// geometry makes replacement incomplete, so its native draws remain active.
struct NativeRoomShadowGeometry {
  std::vector<Fast::Renderer3ds::RoomShadowTriangle> Triangles;
  bool Complete = true;
};
[[nodiscard]] std::optional<NativeRoomShadowGeometry>
ParseNativeRoomShadowCmb(std::span<const uint8_t> bytes);
// Reads command 0x0A's bounded mesh entries; CMB sizes bound the only bulk
// reads. Guest memory is copied here, never accessed by the worker.
// The room loader relocates mesh+4 to an absolute guest pointer. Command
// arguments and CMB entry offsets remain relative to CommandsAddress.
enum class NativeRoomShadowSource { Installed, File };
// Authored selected headers, never the mutable live environment flags.
struct NativeSunHeader {
  uint32_t CommandOffset = 0;
  std::optional<uint32_t> MeshOffset;
  std::optional<uint8_t> SunMoonDisabled, Skybox, EnvironmentLightMode;
  std::optional<uint8_t> TimeHour, TimeMinute, TimeSpeed;
};
[[nodiscard]] std::optional<NativeSunHeader>
ReadNativeSunHeader(uint32_t commandsBase, uint32_t setup,
                    oot3d::recomp::a32::MemoryBus &memory);
[[nodiscard]] bool
NativeRoomIsInterior(const std::optional<NativeSunHeader> &room,
                     const std::optional<NativeSunHeader> &scene);
[[nodiscard]] std::optional<SceneEnvironmentGroup>
ClassifyNativeEnvironmentGroup(const std::optional<NativeSunHeader>& room,
                              const std::optional<NativeSunHeader>& scene);
// Current installed room first; the environment owner latches the first
// readable authored class for the visit, so mixed-room scenes never flip it.
[[nodiscard]] std::optional<SceneEnvironmentGroup>
CaptureNativeEnvironmentGroup(uint32_t play, oot3d::recomp::a32::MemoryBus& memory,
                              uint64_t* authoredScene = nullptr);
[[nodiscard]] uint64_t NativeRoomSunFingerprint(uint64_t roomFingerprint,
                                                uint32_t commandOffset);
// Include copied CMB content: standard/MQ rooms can share the first 128 header
// bytes while containing different geometry. Also invalidates edited RomFS.
[[nodiscard]] uint64_t
NativeRoomSunContentFingerprint(uint64_t setupFingerprint,
                                std::span<const std::vector<uint8_t>> cmbs);
[[nodiscard]] std::optional<std::vector<std::vector<uint8_t>>>
CaptureNativeRoomShadowCmbs(
    const NativeLoadedRoom &room, oot3d::recomp::a32::MemoryBus &memory,
    NativeRoomShadowSource source = NativeRoomShadowSource::Installed,
    std::string *error = nullptr, uint32_t setup = 0);

class NativeRoomShadowShells {
public:
  NativeRoomShadowShells();
  ~NativeRoomShadowShells();
  NativeRoomShadowShells(const NativeRoomShadowShells &) = delete;
  NativeRoomShadowShells &operator=(const NativeRoomShadowShells &) = delete;
  // Disabled: no directory lookup, guest reads, allocations or worker start.
  [[nodiscard]] std::shared_ptr<const Fast::Renderer3ds::RoomShadowShellScene>
  Capture(bool enabled, uint64_t scope, std::span<const NativeLoadedRoom> rooms,
          oot3d::recomp::a32::MemoryBus &memory, uint32_t playState = 0);

private:
  struct Impl;
  std::unique_ptr<Impl> mImpl;
};
} // namespace Oot3dNativeGame
