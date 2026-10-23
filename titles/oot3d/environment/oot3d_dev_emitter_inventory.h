#pragma once

// Developer builds: the native lights linked in a PlayState's LightContext,
// each attributed to the actor whose instance holds its LightInfo (or to the
// PlayState's environment), for agents authoring scene lighting
// (TRIAEVUM_DEV_EMITTERS=<file.jsonl>).

#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace oot3d::recomp::a32 {
class MemoryBus;
}

namespace Oot3dNativeGame {

struct NativeEmitterOwner {
  // "actor" (the LightInfo lies in an actor instance) or "environment"
  // (in the PlayState's environment context).
  std::string Kind;
  uint32_t ActorAddress = 0;
  uint16_t ActorId = 0;
  int16_t Params = 0;
  uint8_t Category = 0;
  int8_t Room = -1;
  std::array<float, 3> HomePosition{};
  // Binary angles of the actor's home rotation (0x8000 = 180 degrees).
  std::array<int16_t, 3> HomeRotation{};
  // The LightInfo's offset in the actor instance (a light slot of the actor).
  uint32_t Offset = 0;
  // "high" within the first 0x1000 bytes of the nearest actor, "medium"
  // further (large instances), never a guess across actors.
  std::string Confidence;
};

struct NativeEmitter {
  uint32_t InfoAddress = 0;
  // Native LightInfo type: 1 directional; otherwise a point light (0 or 2,
  // with or without its drawn glow).
  uint8_t Type = 0;
  std::array<float, 3> Position{};
  std::array<float, 3> Direction{};
  std::array<float, 3> Color{};
  int16_t Radius = 0;
  std::optional<NativeEmitterOwner> Owner;
};

// The LightContext walk (at most its 32-node pool), reading only records whose
// node still links back to them.
[[nodiscard]] std::vector<NativeEmitter> CaptureNativeEmitters(uint32_t playState,
                                                               oot3d::recomp::a32::MemoryBus &memory);

// A live actor: identity, room, world position (+0x28) and scale (+0x54).
struct NativeActorSnapshot {
  uint32_t Address = 0;
  uint16_t Id = 0;
  int16_t Params = 0;
  uint8_t Category = 0;
  int8_t Room = -1;
  std::array<float, 3> Position{};
  std::array<float, 3> Scale{};
};
[[nodiscard]] std::vector<NativeActorSnapshot> CaptureNativeActors(uint32_t playState,
                                                                   oot3d::recomp::a32::MemoryBus &memory);

// Appends a JSON line when the inventory changes (positions to 1 unit, an
// actor's lights by the actor's home, colours and radii exactly), at most
// every `minimumFrames` calls.
class NativeEmitterInventoryLog {
public:
  explicit NativeEmitterInventoryLog(std::string path, uint32_t minimumFrames = 30U);
  ~NativeEmitterInventoryLog();
  // The current room is read from the PlayState.
  void Observe(uint64_t frame, uint32_t playState, std::optional<uint16_t> scene,
               oot3d::recomp::a32::MemoryBus &memory);

private:
  std::FILE *mFile = nullptr;
  uint32_t mMinimumFrames = 30U;
  uint32_t mSinceLast = 0;
  std::string mLastKey;
};

} // namespace Oot3dNativeGame
