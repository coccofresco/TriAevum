#include "oot3d_dev_emitter_inventory.h"
#include "recomp/a32_runtime.h"
#include "oot3d/actor_spawn.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>

namespace Oot3dNativeGame {
namespace {
// EUR: LightContext list head (PlayState +0xA70), 32-node pool, nodes
// {LightInfo*, prev, next}; LightInfo {u8 type; ... ; LightNode* node @+0x18}.
// Points: float position +4, RGB +0x10, radius s16 +0x14. Directionals: s8
// direction +4 (toward the light), RGB +7. The environment's own records
// live in the PlayState (+0x31BC, +0x31D4).
constexpr uint32_t kLightContextHead = 0xA70U;
constexpr uint32_t kLightPool = 32U;
constexpr uint32_t kEnvironmentBegin = 0x3190U;
constexpr uint32_t kEnvironmentEnd = 0x3200U;
// Actor instances are attributed within this distance of their base.
constexpr uint32_t kActorInstanceReach = 0x3000U;

bool ReadFloat3(oot3d::recomp::a32::MemoryBus &memory, uint32_t address, std::array<float, 3> &value) {
  for (uint32_t axis = 0; axis < 3U; ++axis) {
    uint32_t bits = 0;
    if (!memory.Read32(address + axis * 4U, &bits)) return false;
    value[axis] = std::bit_cast<float>(bits);
    if (!std::isfinite(value[axis])) return false;
  }
  return true;
}
bool ReadColor(oot3d::recomp::a32::MemoryBus &memory, uint32_t address, std::array<float, 3> &value) {
  for (uint32_t channel = 0; channel < 3U; ++channel) {
    uint8_t byte = 0;
    if (!memory.Read8(address + channel, &byte)) return false;
    value[channel] = static_cast<float>(byte) / 255.0F;
  }
  return true;
}

struct ActorSpan {
  uint32_t Address = 0;
  uint16_t Id = 0;
  int16_t Params = 0;
  uint8_t Category = 0;
  int8_t Room = -1;
  std::array<float, 3> Home{};
  std::array<int16_t, 3> HomeRotation{};
};

std::vector<ActorSpan> CaptureActors(uint32_t playState, oot3d::recomp::a32::MemoryBus &memory) {
  std::vector<ActorSpan> actors;
  const uint32_t context = playState + OOT3D_ACTOR_CONTEXT_PLAY_OFFSET;
  for (uint32_t category = 0; category < OOT3D_ACTOR_CATEGORY_COUNT; ++category) {
    const uint32_t list = context + offsetof(Oot3dActorContext, lists) + category * sizeof(Oot3dActorListEntry);
    uint32_t count = 0, actor = 0;
    if (!memory.Read32(list, &count) || !memory.Read32(list + 4U, &actor)) continue;
    for (uint32_t i = 0; actor != 0U && i < std::min(count, 255U); ++i) {
      if (actor > std::numeric_limits<uint32_t>::max() - sizeof(Oot3dActor)) break;
      ActorSpan span;
      span.Address = actor;
      uint8_t actualCategory = 0, room = 0;
      uint16_t params = 0;
      uint32_t next = 0;
      if (!memory.Read16(actor + offsetof(Oot3dActor, id), &span.Id) ||
          !memory.Read8(actor + offsetof(Oot3dActor, category), &actualCategory) || actualCategory != category ||
          !memory.Read8(actor + offsetof(Oot3dActor, room), &room) ||
          !memory.Read16(actor + offsetof(Oot3dActor, params), &params) ||
          !ReadFloat3(memory, actor + offsetof(Oot3dActor, homePos), span.Home) ||
          !memory.Read32(actor + offsetof(Oot3dActor, next), &next))
        break;
      for (uint32_t axis = 0; axis < 3U; ++axis) {
        uint16_t angle = 0;
        if (memory.Read16(actor + offsetof(Oot3dActor, homeRot) + axis * 2U, &angle))
          span.HomeRotation[axis] = std::bit_cast<int16_t>(angle);
      }
      span.Params = std::bit_cast<int16_t>(params);
      span.Category = static_cast<uint8_t>(category);
      span.Room = std::bit_cast<int8_t>(room);
      actors.push_back(span);
      actor = next;
    }
  }
  std::sort(actors.begin(), actors.end(), [](const auto &a, const auto &b) { return a.Address < b.Address; });
  return actors;
}
} // namespace

std::vector<NativeEmitter> CaptureNativeEmitters(uint32_t playState, oot3d::recomp::a32::MemoryBus &memory) {
  std::vector<NativeEmitter> emitters;
  if (playState == 0U || playState > std::numeric_limits<uint32_t>::max() - 0x6000U) return emitters;
  const auto actors = CaptureActors(playState, memory);
  uint32_t node = 0;
  if (!memory.Read32(playState + kLightContextHead, &node)) return emitters;
  for (uint32_t visited = 0; node != 0U && visited < kLightPool; ++visited) {
    uint32_t info = 0, next = 0, back = 0;
    if (node > std::numeric_limits<uint32_t>::max() - 12U || !memory.Read32(node, &info) ||
        !memory.Read32(node + 8U, &next))
      break;
    NativeEmitter emitter;
    emitter.InfoAddress = info;
    bool valid = info != 0U && info <= std::numeric_limits<uint32_t>::max() - 0x1CU && memory.Read8(info, &emitter.Type);
    const bool environment = info >= playState + kEnvironmentBegin && info < playState + kEnvironmentEnd;
    // Records outside the environment carry their node: it must lead back.
    if (valid && !environment) valid = memory.Read32(info + 0x18U, &back) && back == node;
    if (valid && emitter.Type == 1U) {
      for (uint32_t axis = 0; valid && axis < 3U; ++axis) {
        uint8_t component = 0;
        valid = memory.Read8(info + 4U + axis, &component);
        emitter.Direction[axis] = float(std::bit_cast<int8_t>(component)) / 127.0F;
      }
      valid = valid && ReadColor(memory, info + 7U, emitter.Color);
    } else if (valid) {
      uint16_t radius = 0;
      valid = ReadFloat3(memory, info + 4U, emitter.Position) && ReadColor(memory, info + 0x10U, emitter.Color) &&
              memory.Read16(info + 0x14U, &radius);
      emitter.Radius = std::bit_cast<int16_t>(radius);
    }
    if (valid) {
      if (environment) {
        emitter.Owner = NativeEmitterOwner{.Kind = "environment", .Offset = info - playState, .Confidence = "high"};
      } else {
        // The nearest actor at or below the record, within an instance's reach.
        const auto above = std::upper_bound(actors.begin(), actors.end(), info,
                                            [](uint32_t address, const ActorSpan &span) { return address < span.Address; });
        if (above != actors.begin()) {
          const auto &span = *std::prev(above);
          const uint32_t offset = info - span.Address;
          if (offset >= sizeof(Oot3dActor) && offset < kActorInstanceReach)
            emitter.Owner = NativeEmitterOwner{"actor", span.Address, span.Id, span.Params, span.Category, span.Room,
                                               span.Home, span.HomeRotation, offset,
                                               offset < 0x1000U ? "high" : "medium"};
        }
      }
      emitters.push_back(emitter);
    }
    node = next;
  }
  return emitters;
}

std::vector<NativeActorSnapshot> CaptureNativeActors(uint32_t playState, oot3d::recomp::a32::MemoryBus &memory) {
  std::vector<NativeActorSnapshot> result;
  if (playState == 0U || playState > std::numeric_limits<uint32_t>::max() - 0x6000U) return result;
  for (const auto &span : CaptureActors(playState, memory)) {
    NativeActorSnapshot actor;
    actor.Address = span.Address;
    actor.Id = span.Id;
    actor.Params = span.Params;
    actor.Category = span.Category;
    actor.Room = span.Room;
    if (ReadFloat3(memory, span.Address + 0x28U, actor.Position) && ReadFloat3(memory, span.Address + 0x54U, actor.Scale))
      result.push_back(actor);
  }
  return result;
}

NativeEmitterInventoryLog::NativeEmitterInventoryLog(std::string path, uint32_t minimumFrames)
    : mFile(std::fopen(path.c_str(), "ab")), mMinimumFrames(minimumFrames) {
  if (mFile == nullptr) std::fprintf(stderr, "TRIAEVUM_DEV_EMITTERS cannot open %s\n", path.c_str());
}

NativeEmitterInventoryLog::~NativeEmitterInventoryLog() {
  if (mFile != nullptr) std::fclose(mFile);
}

void NativeEmitterInventoryLog::Observe(uint64_t frame, uint32_t playState, std::optional<uint16_t> scene,
                                        oot3d::recomp::a32::MemoryBus &memory) {
  if (mFile == nullptr || !scene || playState == 0U || ++mSinceLast < mMinimumFrames) return;
  mSinceLast = 0;
  uint8_t currentRoom = 0xFFU;
  const int room = memory.Read8(playState + OOT3D_CURRENT_ROOM_PLAY_OFFSET, &currentRoom)
                       ? static_cast<int>(std::bit_cast<int8_t>(currentRoom))
                       : -1;
  const auto emitters = CaptureNativeEmitters(playState, memory);
  std::string lights;
  std::string key = std::to_string(*scene) + "/" + std::to_string(room);
  char text[640];
  for (const auto &e : emitters) {
    std::snprintf(text, sizeof(text),
                  "%s{\"info\":\"0x%08X\",\"type\":%u,\"position\":[%.1f,%.1f,%.1f],\"direction\":[%.3f,%.3f,%.3f],"
                  "\"color\":[%.4f,%.4f,%.4f],\"radius\":%d",
                  lights.empty() ? "" : ",", e.InfoAddress, e.Type, e.Position[0], e.Position[1], e.Position[2],
                  e.Direction[0], e.Direction[1], e.Direction[2], e.Color[0], e.Color[1], e.Color[2], e.Radius);
    lights += text;
    if (e.Owner && e.Owner->Kind == "actor") {
      std::snprintf(text, sizeof(text),
                    ",\"owner\":{\"kind\":\"actor\",\"actorId\":%u,\"params\":%d,\"category\":%u,\"room\":%d,"
                    "\"home\":[%.1f,%.1f,%.1f],\"homeRotation\":[%d,%d,%d],\"offset\":\"0x%X\",\"confidence\":\"%s\"}}",
                    e.Owner->ActorId, e.Owner->Params, e.Owner->Category, e.Owner->Room, e.Owner->HomePosition[0],
                    e.Owner->HomePosition[1], e.Owner->HomePosition[2], e.Owner->HomeRotation[0],
                    e.Owner->HomeRotation[1], e.Owner->HomeRotation[2], e.Owner->Offset, e.Owner->Confidence.c_str());
    } else if (e.Owner) {
      std::snprintf(text, sizeof(text), ",\"owner\":{\"kind\":\"environment\",\"offset\":\"0x%X\"}}", e.Owner->Offset);
    } else {
      std::snprintf(text, sizeof(text), ",\"owner\":null}");
    }
    lights += text;
    // Change detection: what, where (to a unit), colour and reach. A light an
    // actor carries (the fairy following Link) counts where the actor spawned:
    // its own movement is not a change.
    const bool carried = e.Owner && e.Owner->Kind == "actor";
    const auto &where = carried ? e.Owner->HomePosition : e.Position;
    std::snprintf(text, sizeof(text), "|%u:%.0f,%.0f,%.0f:%.3f,%.3f,%.3f:%d:%u", e.Type, where[0], where[1], where[2],
                  e.Color[0], e.Color[1], e.Color[2], e.Radius, e.Owner ? e.Owner->ActorId : 0xFFFFU);
    key += text;
  }
  if (key == mLastKey) return;
  mLastKey = key;
  std::fprintf(mFile,
               "{\"schema\":\"triaevum.native-emitters\",\"version\":1,\"frame\":%llu,\"sceneId\":%u,\"room\":%d,"
               "\"lights\":[%s]}\n",
               static_cast<unsigned long long>(frame), unsigned(*scene), room, lights.c_str());
  std::fflush(mFile);
}

} // namespace Oot3dNativeGame
