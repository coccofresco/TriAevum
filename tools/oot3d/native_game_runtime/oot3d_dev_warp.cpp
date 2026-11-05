#include "oot3d_dev_warp.h"

#include "oot3d/actor_spawn.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace Oot3dNativeGame {
namespace {

// SaveContext (code.bin data): the first fields keep the N64 layout.
constexpr uint32_t kSaveContext = 0x00587958U;
constexpr uint32_t kSaveEntrance = kSaveContext + 0x00U;     // s32, Save_GetEntranceIndex
constexpr uint32_t kSaveAge = kSaveContext + 0x04U;          // s32: 0 adult, 1 child
constexpr uint32_t kSaveCutscene = kSaveContext + 0x08U;     // s32
constexpr uint32_t kSaveDayTime = kSaveContext + 0x0CU;      // u16
constexpr uint32_t kSaveGameMode = kSaveContext + 0x14E4U;   // s32, 0 = normal gameplay
constexpr uint32_t kSaveHealth = kSaveContext + 0x44U;       // s16 (log only)
// The N64 respawn state, 0x188 further than the N64 SaveContext (gameMode
// 0x135C/0x14E4): Gameplay_SetupRespawnPoint (0x0036C494) writes the
// RespawnData rows at +0x14F0 (0x1C each: pos, yaw +0x0C, playerParams +0x0E,
// entranceIndex +0x10, roomIndex +0x12, data +0x13), Gameplay_TriggerVoidOut
// (0x0036EBDC) sets respawnFlag +0x14EC to 1 and requests the DOWN row's
// entrance, the grotto exit (0x0033DE98) requests the RETURN row's.
constexpr uint32_t kSaveRespawnFlag = kSaveContext + 0x14ECU;  // s32
constexpr uint32_t kSaveRespawn = kSaveContext + 0x14F0U;
constexpr uint32_t kRespawnSize = 0x1CU;
constexpr uint32_t kRespawnDown = 0U;
constexpr uint32_t kRespawnReturn = 1U;
constexpr uint32_t kRespawnYaw = 0x0CU;          // s16
constexpr uint32_t kRespawnPlayerParams = 0x0EU; // s16
constexpr uint32_t kRespawnEntrance = 0x10U;     // s16
constexpr uint32_t kRespawnRoom = 0x12U;         // u8
constexpr uint32_t kRespawnData = 0x13U;         // s8
// Player_Init (0x00191844) arms the void-out damage with -2 here for a
// respawnFlag of 1 or -1; the damage lands when it is nonzero (N64 unk_A86).
constexpr uint32_t kPlayerVoidDamage = 0x249EU;  // s8
// PlayState fields (structural scenario contract and decomp).
constexpr uint32_t kPlaySceneId = 0x104U;          // s16
constexpr uint32_t kPlayPendingEntrance = 0x5C32U; // s16, negative when free
// u8 linkAgeOnLoad: the N64 PlayState tail maps onto OOT3D's at -0xC1E8
// (transitionTrigger 0x11E15/0x5C2D, nextEntranceIndex 0x11E1A/0x5C32,
// transitionType 0x11E5E/0x5C76, numSetupActors 0x11DEB/0x5C03,
// setupActorList 0x11DF8/0x5C10), so linkAgeOnLoad 0x11DE8 is 0x5C00.
constexpr uint32_t kPlayLinkAgeOnLoad = 0x5C00U;
// The player list head of the actor context (category 2).
constexpr uint32_t kPlayPlayerHead = OOT3D_ACTOR_CONTEXT_PLAY_OFFSET +
                                     offsetof(Oot3dActorContext, lists) + 2U * sizeof(Oot3dActorListEntry) +
                                     offsetof(Oot3dActorListEntry, head);
constexpr uint32_t kActorWorldPosition = 0x28U;
constexpr uint32_t kActorShapeYaw = 0xBEU;
// code.bin's global entrance table: 4-byte rows, scene id then spawn.
constexpr uint32_t kEntranceTable = 0x00543BB8U;
constexpr uint32_t kEntranceCount = 1586U;
constexpr uint64_t kArrivalTimeoutFrames = 900U;
constexpr uint32_t kReportFrames = 90U;

template <class T> bool ReadValue(DevWarpGuest &guest, uint32_t address, T &value) {
  std::array<uint8_t, sizeof(T)> bytes{};
  if (!guest.Read(address, bytes)) return false;
  std::memcpy(&value, bytes.data(), sizeof(T));
  return true;
}

template <class T> bool WriteValue(DevWarpGuest &guest, uint32_t address, T value) {
  std::array<uint8_t, sizeof(T)> bytes{};
  std::memcpy(bytes.data(), &value, sizeof(T));
  return guest.Write(address, bytes);
}

std::string Hex4(uint32_t value) {
  char text[8];
  std::snprintf(text, sizeof(text), "0x%04X", value);
  return text;
}

} // namespace

DevWarpRuntime::DevWarpRuntime(std::filesystem::path storage, std::shared_ptr<DevWarpTitleAbi> title)
    : mTitle(std::move(title)), mStorage(std::move(storage)) { Load(); }

DevWarpStatus DevWarpRuntime::Status() const {
  std::scoped_lock lock(mMutex);
  return mStatus;
}

DevWarpAvailability DevWarpRuntime::Check(const DevWarpEntrance &entrance) const {
  std::scoped_lock lock(mMutex);
  if (mTitle) return mTitle->Check(entrance, mEntranceTable);
  DevWarpAvailability result;
  if (mEntranceTable.empty()) return result;
  result.Known = true;
  const auto check = [&](uint16_t index, uint8_t scene, uint8_t spawn) -> bool {
    if (index >= kEntranceCount) {
      result.Reason = "OOT3D has no entrance " + Hex4(index) + ".";
      return false;
    }
    const uint32_t row = mEntranceTable[index];
    const uint8_t actualScene = static_cast<uint8_t>(row & 0xFFU);
    const uint8_t actualSpawn = static_cast<uint8_t>((row >> 8U) & 0xFFU);
    if (actualScene != scene || actualSpawn != spawn) {
      result.Reason = "OOT3D's entrance " + Hex4(index) + " leads to scene " + std::to_string(actualScene) +
                      " spawn " + std::to_string(actualSpawn) + "; SoH expects scene " + std::to_string(scene) +
                      " spawn " + std::to_string(spawn) + ".";
      return false;
    }
    return true;
  };
  if (entrance.Grotto >= 0) {
    // The grotto and the place its exit returns to.
    const auto &grotto = DevWarpGrottos()[static_cast<size_t>(entrance.Grotto)];
    result.Available = check(grotto.Entrance, grotto.Scene, grotto.Spawn) &&
                       check(grotto.ReturnEntrance, grotto.ReturnScene, grotto.ReturnSpawn);
    return result;
  }
  result.Available = check(entrance.Entrance, entrance.Scene, entrance.Spawn);
  return result;
}

void DevWarpRuntime::WarpTo(const DevWarpEntrance &entrance, DevWarpAge age, std::optional<bool> night) {
  Request request;
  request.Label = entrance.English;
  request.Age = age;
  request.Night = night;
  if (entrance.Grotto >= 0) {
    const auto &grotto = DevWarpGrottos()[static_cast<size_t>(entrance.Grotto)];
    request.Type = Kind::Grotto;
    request.Entrance = grotto.Entrance;
    request.Scene = grotto.Scene;
    request.Grotto = grotto;
  } else {
    request.Entrance = entrance.Entrance;
    request.Scene = entrance.Scene;
  }
  std::scoped_lock lock(mMutex);
  mPending = std::move(request);
  mStatus.Phase = DevWarpPhase::Waiting;
  mStatus.Message = "Warping to " + mPending->Label + "...";
}

const DevWarpZone *DevWarpRuntime::FindZone(std::string_view id) const {
  for (const auto &zone : Zones())
    if (id == zone.Id) return &zone;
  return nullptr;
}

const DevWarpEntrance *DevWarpRuntime::FindEntrance(uint16_t entrance) const {
  for (const auto &row : Entrances())
    if (row.Entrance == entrance && row.Grotto < 0) return &row;
  return nullptr;
}

DevWarpSelection DevWarpRuntime::Resolve(const DevWarpSelection &arrival, const DevWarpZone *zone) const {
  const auto axes = Axes();
  DevWarpSelection resolved = arrival;
  for (size_t axis = 0; axis < kDevWarpMaxAxes; ++axis) {
    if (resolved[axis] != kDevWarpPreset) continue;
    const bool proven = axis < axes.size() && axes[axis].Proven;
    resolved[axis] = zone != nullptr && proven ? zone->Preset[axis] : kDevWarpKeep;
  }
  return resolved;
}

std::string DevWarpRuntime::ArrivalError(const DevWarpSelection &arrival) const {
  const auto axes = Axes();
  for (size_t axis = 0; axis < kDevWarpMaxAxes; ++axis) {
    if (arrival[axis] == kDevWarpKeep || arrival[axis] == kDevWarpPreset) continue;
    if (axis >= axes.size()) return "this title has no arrival option " + std::to_string(axis);
    if (arrival[axis] < 0) return std::string(axes[axis].Label) + " has no value " + std::to_string(arrival[axis]);
    if (!axes[axis].Proven)
      return std::string(axes[axis].Label) + ": " + (*axes[axis].Pending ? axes[axis].Pending : "awaiting proof");
  }
  return {};
}

void DevWarpRuntime::QueueLocked(Request request, const std::string &message) {
  mPending = std::move(request);
  mStatus.Phase = DevWarpPhase::Waiting;
  mStatus.Message = message;
}

void DevWarpRuntime::WarpTo(const DevWarpEntrance &entrance, const DevWarpSelection &arrival) {
  Request request;
  // "<scene>, <spawn>": a title's spawn rows are named within their scene.
  request.Label = entrance.English;
  const auto rows = Entrances();
  for (const auto &scene : Scenes()) {
    if (scene.First + scene.Count > rows.size()) continue;
    const auto slice = rows.subspan(scene.First, scene.Count);
    if (std::any_of(slice.begin(), slice.end(), [&](const auto &row) { return row.Entrance == entrance.Entrance; })) {
      request.Label = std::string(scene.English) + ", " + entrance.English;
      break;
    }
  }
  request.Entrance = entrance.Entrance;
  request.Scene = entrance.Scene;
  request.Arrival = Resolve(arrival, nullptr);
  const auto message = "Warping to " + request.Label + "...";
  std::scoped_lock lock(mMutex);
  QueueLocked(std::move(request), message);
}

bool DevWarpRuntime::ZoneRequestLocked(const DevWarpZone &zone, const DevWarpSelection &arrival, Request &request,
                                       std::string *error) const {
  const auto *row = FindEntrance(zone.Entrance);
  if (row == nullptr) {
    if (error != nullptr) *error = std::string("zone ") + zone.Id + " names no catalog entrance";
    return false;
  }
  request.Label = zone.Name;
  request.Entrance = row->Entrance;
  request.Scene = row->Scene;
  request.Arrival = Resolve(arrival, &zone);
  return true;
}

bool DevWarpRuntime::WarpToZone(std::string_view id, const DevWarpSelection &arrival, std::string *error) {
  const auto *zone = FindZone(id);
  if (zone == nullptr) {
    if (error != nullptr) *error = "unknown zone " + std::string(id);
    return false;
  }
  Request request;
  std::scoped_lock lock(mMutex);
  if (!ZoneRequestLocked(*zone, arrival, request, error)) return false;
  QueueLocked(std::move(request), "Warping to " + std::string(zone->Name) + "...");
  return true;
}

void DevWarpRuntime::WarpToPoint(const std::string &name) {
  std::scoped_lock lock(mMutex);
  const auto found = mPoints.find(name);
  if (found == mPoints.end()) return;
  Request request;
  request.Type = Kind::WarpPoint;
  request.Label = name;
  request.Entrance = static_cast<uint16_t>(found->second.EntranceId);
  request.Point = found->second;
  if (found->second.EntranceId >= 0 && static_cast<uint32_t>(found->second.EntranceId) < mEntranceTable.size())
    request.Scene = static_cast<uint8_t>(mEntranceTable[static_cast<size_t>(found->second.EntranceId)] & 0xFFU);
  mPending = std::move(request);
  mStatus.Phase = DevWarpPhase::Waiting;
  mStatus.Message = "Warping to " + name + "...";
}

void DevWarpRuntime::WarpToPlace(const std::string &label, const DevWarpPoint &point) {
  std::scoped_lock lock(mMutex);
  Request request;
  request.Type = Kind::WarpPoint;
  request.Label = label;
  request.Entrance = static_cast<uint16_t>(point.EntranceId);
  request.Point = point;
  if (point.EntranceId >= 0 && static_cast<uint32_t>(point.EntranceId) < mEntranceTable.size())
    request.Scene = static_cast<uint8_t>(mEntranceTable[static_cast<size_t>(point.EntranceId)] & 0xFFU);
  mPending = std::move(request);
  mStatus.Phase = DevWarpPhase::Waiting;
  mStatus.Message = "Warping to " + label + "...";
}

std::map<std::string, DevWarpPoint> DevWarpRuntime::Points() const {
  std::scoped_lock lock(mMutex);
  return mPoints;
}

bool DevWarpRuntime::AddPoint(const std::string &name, std::string *error) {
  std::scoped_lock lock(mMutex);
  const auto &observation = mStatus.Observation;
  if (name.empty()) {
    if (error != nullptr) *error = "Enter a warp point name.";
    return false;
  }
  if (!observation.Gameplay || !observation.Player) {
    if (error != nullptr) *error = "Warp points are saved during gameplay.";
    return false;
  }
  DevWarpPoint point;
  point.EntranceId = static_cast<int32_t>(observation.Entrance);
  point.RoomNum = static_cast<int8_t>(observation.Room);
  std::copy(std::begin(observation.Position), std::end(observation.Position), point.Position);
  point.RotY = observation.RotY;
  // SoH replaces a point of the same name; its boot mark is kept.
  if (const auto existing = mPoints.find(name); existing != mPoints.end())
    point.BootToPoint = existing->second.BootToPoint;
  mPoints[name] = point;
  SaveLocked();
  return true;
}

void DevWarpRuntime::RemovePoint(const std::string &name) {
  std::scoped_lock lock(mMutex);
  if (mPoints.erase(name) != 0U) SaveLocked();
}

void DevWarpRuntime::SetBootPoint(const std::string &name) {
  std::scoped_lock lock(mMutex);
  for (auto &[key, point] : mPoints) point.BootToPoint = key == name;
  mArrivalOptions.BootZone.clear();
  SaveLocked();
}

void DevWarpRuntime::SetBootZone(std::string_view id) {
  std::scoped_lock lock(mMutex);
  mArrivalOptions.BootZone = std::string(id);
  if (!id.empty())
    for (auto &[key, point] : mPoints) point.BootToPoint = false;
  SaveLocked();
}

DevWarpArrivalOptions DevWarpRuntime::ArrivalOptions() const {
  std::scoped_lock lock(mMutex);
  return mArrivalOptions;
}

void DevWarpRuntime::SetArrivalOptions(const DevWarpArrivalOptions &options) {
  std::scoped_lock lock(mMutex);
  if (options.SelectedZone == mArrivalOptions.SelectedZone && options.BootZone == mArrivalOptions.BootZone &&
      options.Arrival == mArrivalOptions.Arrival)
    return;
  const bool bootChanged = options.BootZone != mArrivalOptions.BootZone;
  mArrivalOptions = options;
  if (bootChanged && !options.BootZone.empty())
    for (auto &[key, point] : mPoints) point.BootToPoint = false;
  SaveLocked();
}

bool DevWarpRuntime::PersistsArrival() const {
  return mTitle && (!mTitle->Axes().empty() || !mTitle->Zones().empty());
}

DevWarpScreenOptions DevWarpRuntime::Options() const {
  std::scoped_lock lock(mMutex);
  return mOptions;
}

void DevWarpRuntime::SetOptions(const DevWarpScreenOptions &options) {
  std::scoped_lock lock(mMutex);
  const auto same = [](const DevWarpScreenOptions &a, const DevWarpScreenOptions &b) {
    return a.Better == b.Better && a.Translation == b.Translation && a.VanillaScene == b.VanillaScene &&
           a.BetterScene == b.BetterScene && a.BetterEntrance == b.BetterEntrance && a.Age == b.Age &&
           a.Night == b.Night;
  };
  if (same(mOptions, options)) return;
  mOptions = options;
  SaveLocked();
}

bool DevWarpRuntime::Observe(DevWarpGuest &guest, uint32_t playState, int16_t *pendingEntrance) {
  if (mTitle) return mTitle->Observe(guest, playState, mStatus.Observation, *pendingEntrance, mPlayerActor);
  DevWarpObservation observation;
  *pendingEntrance = -1;
  int32_t cutscene = 0;
  int32_t gameMode = 0;
  observation.PlayState = playState != 0U;
  mPlayerActor = 0U;
  if (!ReadValue(guest, kSaveEntrance, observation.Entrance) || !ReadValue(guest, kSaveAge, observation.Age) ||
      !ReadValue(guest, kSaveCutscene, cutscene) || !ReadValue(guest, kSaveDayTime, observation.DayTime) ||
      !ReadValue(guest, kSaveGameMode, gameMode)) {
    mStatus.Observation = observation;
    return false;
  }
  if (observation.PlayState) {
    uint32_t player = 0;
    uint8_t ageOnLoad = 0;
    if (!ReadValue(guest, playState + kPlaySceneId, observation.Scene) ||
        !ReadValue(guest, playState + OOT3D_CURRENT_ROOM_PLAY_OFFSET, observation.Room) ||
        !ReadValue(guest, playState + kPlayPendingEntrance, *pendingEntrance) ||
        !ReadValue(guest, playState + kPlayLinkAgeOnLoad, ageOnLoad) ||
        !ReadValue(guest, playState + kPlayPlayerHead, player)) {
      mStatus.Observation = observation;
      return false;
    }
    observation.AgeOnLoad = ageOnLoad;
    mPlayerActor = player;
    if (player != 0U) {
      uint32_t position[3]{};
      observation.Player = ReadValue(guest, player + kActorWorldPosition, position[0]) &&
                           ReadValue(guest, player + kActorWorldPosition + 4U, position[1]) &&
                           ReadValue(guest, player + kActorWorldPosition + 8U, position[2]) &&
                           ReadValue(guest, player + kActorShapeYaw, observation.RotY);
      for (int axis = 0; axis < 3; ++axis) observation.Position[axis] = std::bit_cast<float>(position[axis]);
    }
    // The same admission as the TopScreen free camera: normal game mode and
    // no title/demo cutscene index.
    observation.Gameplay = gameMode == 0 && cutscene < 0xFFF0;
  }
  mStatus.Observation = observation;
  return true;
}

bool DevWarpRuntime::ReadEntranceTable(DevWarpGuest &guest) {
  if (mTitle) {
    mStatus.EntranceTableRead = mTitle->ReadEntranceTable(guest, mEntranceTable);
    return mStatus.EntranceTableRead;
  }
  std::vector<uint8_t> bytes(kEntranceCount * 4U);
  if (!guest.Read(kEntranceTable, bytes)) return false;
  // Row 0 is the Deku Tree entrance: scene 0, spawn 0. Anything else means
  // this is not the table the catalog was checked against.
  if (bytes[0] != 0U || bytes[1] != 0U) return false;
  mEntranceTable.resize(kEntranceCount);
  for (uint32_t index = 0; index < kEntranceCount; ++index)
    mEntranceTable[index] = static_cast<uint32_t>(bytes[index * 4U]) |
                            static_cast<uint32_t>(bytes[index * 4U + 1U]) << 8U;
  mStatus.EntranceTableRead = true;
  return true;
}

bool DevWarpRuntime::Apply(DevWarpGuest &guest, uint32_t playState, const Request &request, std::string *error) {
  const auto fail = [&](const char *what) {
    if (error != nullptr) *error = what;
    return false;
  };
  if (mTitle) {
    if (request.Type == Kind::Grotto) return fail("This title has no OoT grotto ABI");
    if (request.Night.has_value()) return fail("this title's time of day is an arrival option");
    if (const auto reason = ArrivalError(request.Arrival); !reason.empty()) {
      if (error != nullptr) *error = reason;
      return false;
    }
    return mTitle->Apply(guest, playState, request.Entrance,
                        request.Type == Kind::WarpPoint ? &request.Point : nullptr, request.Arrival, error);
  }
  // SoH's Debug Warp Screen sets the age and the time of day it shows. In
  // play the age changes the native way, through the PlayState's
  // linkAgeOnLoad (as SoH's SwitchAge): the game applies it when the scene
  // ends, swapping the age equipment. Writing the save age under a live
  // player faults the title.
  if (request.Age.has_value() &&
      !WriteValue(guest, playState + kPlayLinkAgeOnLoad, static_cast<uint8_t>(*request.Age)))
    return fail("cannot write the age on load");
  // Day 0x8000 (noon), night 0x0000 (midnight); the scene's environment
  // derives the night state from the time when it initializes.
  if (request.Night.has_value() &&
      !WriteValue(guest, kSaveDayTime, static_cast<uint16_t>(*request.Night ? 0x0000U : 0x8000U)))
    return fail("cannot write the time of day");
  const auto writeRow = [&](uint32_t mode, uint16_t entrance, uint8_t room, const float (&position)[3],
                            std::optional<int16_t> yaw, int16_t playerParams) {
    const uint32_t row = kSaveRespawn + mode * kRespawnSize;
    bool ok = true;
    for (uint32_t axis = 0; axis < 3U; ++axis) ok = ok && WriteValue(guest, row + axis * 4U, position[axis]);
    if (yaw.has_value()) ok = ok && WriteValue(guest, row + kRespawnYaw, *yaw);
    return ok && WriteValue(guest, row + kRespawnPlayerParams, playerParams) &&
           WriteValue(guest, row + kRespawnEntrance, static_cast<int16_t>(entrance)) &&
           WriteValue(guest, row + kRespawnRoom, room);
  };
  if (request.Type == Kind::WarpPoint) {
    // SoH: the point becomes the void-out respawn (DOWN row, playerParams
    // 0xDFF, respawnFlag 1); with useSceneEntrance the entrance's own spawn.
    const auto &point = request.Point;
    if (point.UseSceneEntrance) {
      if (!WriteValue(guest, kSaveRespawnFlag, int32_t{0})) return fail("cannot write the respawn flag");
    } else if (!writeRow(kRespawnDown, request.Entrance, static_cast<uint8_t>(point.RoomNum), point.Position,
                         point.RotY, static_cast<int16_t>(0x0DFF)) ||
               !WriteValue(guest, kSaveRespawnFlag, int32_t{1})) {
      return fail("cannot write the respawn point");
    }
  } else if (request.Type == Kind::Grotto) {
    // SoH's Select_Grotto_LoadGame: the content byte and the way back
    // (entrance, room, position, playerParams 0x4FF) in the RETURN row; the
    // grotto's own spawn (respawnFlag 0).
    const auto &grotto = request.Grotto;
    if (!writeRow(kRespawnReturn, grotto.ReturnEntrance, static_cast<uint8_t>(grotto.ReturnRoom),
                  grotto.ReturnPosition, std::nullopt, static_cast<int16_t>(0x04FF)) ||
        !WriteValue(guest, kSaveRespawn + kRespawnReturn * kRespawnSize + kRespawnData, grotto.Data) ||
        !WriteValue(guest, kSaveRespawnFlag, int32_t{0}))
      return fail("cannot write the grotto return point");
  } else if (!WriteValue(guest, kSaveRespawnFlag, int32_t{0})) {
    return fail("cannot write the respawn flag");
  }
  bool accepted = false;
  if (!guest.RequestTransition(playState, request.Entrance, &accepted, error)) return false;
  if (!accepted) return fail("the native transition helper refused the request");
  return true;
}

void DevWarpRuntime::Report(DevWarpGuest &guest, const char *event, uint64_t guestFrame) {
  if (mTitle) {
    const auto& o = mStatus.Observation;
    std::string arrival;
    if (mActive.has_value()) {
      const auto axes = mTitle->Axes();
      for (size_t axis = 0; axis < axes.size() && axis < kDevWarpMaxAxes; ++axis)
        arrival += std::string(axis == 0U ? " arrival=" : ",") + axes[axis].Id + ":" +
                   std::to_string(mActive->Arrival[axis]);
    }
    const auto state = mTitle->Describe(o);
    std::fprintf(stderr,
                 "TRIAEVUM_DEV_WARP %s target=\"%s\" frame=%llu scene=%d room=%u entrance=0x%04X player=%d "
                 "pos=%.1f,%.1f,%.1f yaw=%d%s state=\"%s\"\n",
                 event, mActive.has_value() ? mActive->Label.c_str() : "",
                 static_cast<unsigned long long>(guestFrame), o.Scene, o.Room, o.Entrance, o.Player ? 1 : 0,
                 o.Position[0], o.Position[1], o.Position[2], o.RotY, arrival.c_str(), state.c_str());
    return;
  }
  // Developer log: what the warp observed, for checking it outside the menu.
  // +0x10 is reported for the night state SoH also writes (N64 nightFlag).
  int16_t health = 0;
  int32_t nightWord = 0;
  int32_t respawnFlag = 0;
  int8_t voidDamage = 0;
  ReadValue(guest, kSaveHealth, health);
  ReadValue(guest, kSaveContext + 0x10U, nightWord);
  ReadValue(guest, kSaveRespawnFlag, respawnFlag);
  if (mPlayerActor != 0U) ReadValue(guest, mPlayerActor + kPlayerVoidDamage, voidDamage);
  const auto &o = mStatus.Observation;
  std::fprintf(stderr,
               "TRIAEVUM_DEV_WARP %s target=\"%s\" entrance=0x%04X frame=%llu scene=%d room=%u save_entrance=0x%04X "
               "age=%d age_on_load=%d day_time=0x%04X save_0x10=%d health=%d player=%d pos=%.1f,%.1f,%.1f yaw=%d "
               "respawn_flag=%d void_damage=%d\n",
               event, mActive.has_value() ? mActive->Label.c_str() : "", mActive.has_value() ? mActive->Entrance : 0U,
               static_cast<unsigned long long>(guestFrame), o.Scene, o.Room, o.Entrance, o.Age, o.AgeOnLoad, o.DayTime, nightWord,
               health, o.Player ? 1 : 0, o.Position[0], o.Position[1], o.Position[2], o.RotY, respawnFlag, voidDamage);
}

void DevWarpRuntime::Advance(DevWarpGuest &guest, uint32_t playState, uint64_t guestFrame) {
  std::scoped_lock lock(mMutex);
  if (mTitle) playState = mTitle->PlayStateAddress(guest);
  int16_t pendingEntrance = -1;
  const bool observed = Observe(guest, playState, &pendingEntrance);
  if (observed && mEntranceTable.empty() && mStatus.Observation.Gameplay) ReadEntranceTable(guest);
  const auto &observation = mStatus.Observation;

  // SoH's boot point: the first time gameplay starts in this session.
  if (!mBootChecked && observation.Gameplay) {
    mBootChecked = true;
    const auto *bootZone = mArrivalOptions.BootZone.empty() ? nullptr : FindZone(mArrivalOptions.BootZone);
    if (!mPending.has_value() && !mActive.has_value() && bootZone != nullptr) {
      Request request;
      std::string error;
      if (ZoneRequestLocked(*bootZone, mArrivalOptions.Arrival, request, &error))
        QueueLocked(std::move(request), "Boot zone: warping to " + std::string(bootZone->Name) + "...");
      else
        std::fprintf(stderr, "TRIAEVUM_DEV_WARP failed target=\"%s\" reason=%s\n", bootZone->Id, error.c_str());
    } else if (!mPending.has_value() && !mActive.has_value()) {
      for (const auto &[name, point] : mPoints) {
        if (!point.BootToPoint) continue;
        Request request;
        request.Type = Kind::WarpPoint;
        request.Label = name;
        request.Entrance = static_cast<uint16_t>(point.EntranceId);
        request.Point = point;
        if (static_cast<uint32_t>(point.EntranceId) < mEntranceTable.size())
          request.Scene = static_cast<uint8_t>(mEntranceTable[static_cast<size_t>(point.EntranceId)] & 0xFFU);
        mPending = std::move(request);
        mStatus.Phase = DevWarpPhase::Waiting;
        mStatus.Message = "Boot warp point: warping to " + name + "...";
        break;
      }
    }
  }

  if (mActive.has_value()) {
    if (pendingEntrance >= 0) mSawTransition = true;
    // The respawned player in the requested scene: cancel the void-out
    // damage Player_Init armed, once (SoH's hook unregisters itself).
    if (mCancelVoidDamage && mSawTransition && pendingEntrance < 0 && mPlayerActor != 0U &&
        observation.Scene == mActive->Scene) {
      int8_t armed = 0;
      if (ReadValue(guest, mPlayerActor + kPlayerVoidDamage, armed) && armed != 0 &&
          WriteValue(guest, mPlayerActor + kPlayerVoidDamage, int8_t{0})) {
        mCancelVoidDamage = false;
        std::fprintf(stderr, "TRIAEVUM_DEV_WARP void_damage_cancelled frame=%llu armed=%d\n",
                     static_cast<unsigned long long>(guestFrame), armed);
      }
    }
    if (!mArrived) {
      if (mSawTransition && pendingEntrance < 0 && observation.Scene == mActive->Scene &&
          observation.Player && (!mTitle || mTitle->Arrived(observation, mActive->Entrance,
              mActive->Type == Kind::WarpPoint && !mActive->Point.UseSceneEntrance))) {
        mStatus.Phase = DevWarpPhase::Arrived;
        mArrived = true;
        mReportFrames = kReportFrames;
        mStatus.Message = "Arrived at " + mActive->Label + ".";
        Report(guest, "arrived", guestFrame);
        // The title's arrival options must have taken effect.
        const auto mismatch = mTitle ? mTitle->ArrivalMismatch(guest, observation, mActive->Arrival) : std::string{};
        if (!mismatch.empty()) {
          mStatus.Phase = DevWarpPhase::Failed;
          mStatus.Message = "Arrived at " + mActive->Label + ", but the arrival options did not apply: " + mismatch;
          std::fprintf(stderr, "TRIAEVUM_DEV_WARP failed target=\"%s\" frame=%llu %s\n", mActive->Label.c_str(),
                       static_cast<unsigned long long>(guestFrame), mismatch.c_str());
          mActive.reset();
        }
      } else if (guestFrame - mRequestFrame > kArrivalTimeoutFrames) {
        mStatus.Phase = DevWarpPhase::Failed;
        mStatus.Message = "The warp to " + mActive->Label + " did not arrive.";
        Report(guest, "failed", guestFrame);
        mActive.reset();
      }
    } else {
      if (mReportFrames != 0U && --mReportFrames == 0U) Report(guest, "settled", guestFrame);
      if (mReportFrames == 0U) mActive.reset();
    }
    return;
  }

  if (!mPending.has_value()) return;
  if (!observed || !observation.Gameplay) {
    mStatus.Phase = DevWarpPhase::Waiting;
    mStatus.Message = "Waiting for gameplay: warps start from normal play (not the title, a cutscene or a menu).";
    return;
  }
  if (pendingEntrance >= 0) {
    mStatus.Phase = DevWarpPhase::Waiting;
    mStatus.Message = "Waiting for the current transition to finish...";
    return;
  }
  auto request = std::move(*mPending);
  mPending.reset();
  std::string error;
  if (!Apply(guest, playState, request, &error)) {
    mStatus.Phase = DevWarpPhase::Failed;
    mStatus.Message = "Warp to " + request.Label + " failed: " + error;
    std::fprintf(stderr, "TRIAEVUM_DEV_WARP failed target=\"%s\" reason=%s\n", request.Label.c_str(),
                 error.c_str());
    return;
  }
  mRequestFrame = guestFrame;
  mSawTransition = false;
  mArrived = false;
  mCancelVoidDamage = !mTitle && request.Type == Kind::WarpPoint && !request.Point.UseSceneEntrance;
  mStatus.Phase = DevWarpPhase::Requested;
  mStatus.Message = "Warping to " + request.Label + " (entrance " + Hex4(request.Entrance) + ")...";
  mActive = std::move(request);
  Report(guest, "requested", guestFrame);
}

void DevWarpRuntime::Load() {
  if (mStorage.empty()) return;
  std::error_code exists;
  if (!std::filesystem::exists(mStorage, exists)) return;
  try {
    std::ifstream stream(mStorage, std::ios::binary);
    const auto document = nlohmann::json::parse(stream);
    if (const auto points = document.find("WarpPoints"); points != document.end() && points->is_object()) {
      for (const auto &[name, value] : points->items()) {
        DevWarpPoint point;
        point.EntranceId = value.at("entranceId").get<int32_t>();
        point.RoomNum = value.at("roomNum").get<int8_t>();
        const auto &position = value.at("pos");
        point.Position[0] = position.at("x").get<float>();
        point.Position[1] = position.at("y").get<float>();
        point.Position[2] = position.at("z").get<float>();
        point.RotY = value.at("rotY").get<int16_t>();
        point.BootToPoint = value.at("bootToPoint").get<bool>();
        point.UseSceneEntrance = value.value("useSceneEntrance", false);
        mPoints[name] = point;
      }
    }
    if (const auto screen = document.find("DebugWarpScreen"); screen != document.end() && screen->is_object()) {
      mOptions.Better = screen->value("Better", mOptions.Better);
      mOptions.Translation = screen->value("Translation", mOptions.Translation);
      mOptions.VanillaScene = screen->value("VanillaScene", 0);
      mOptions.BetterScene = screen->value("BetterScene", 0);
      mOptions.BetterEntrance = screen->value("BetterEntrance", 0);
      mOptions.Age = screen->value("LinkAge", 1) == 0 ? DevWarpAge::Adult : DevWarpAge::Child;
      mOptions.Night = screen->value("Night", false);
    }
    if (PersistsArrival()) {
      if (const auto zones = document.find("Zones"); zones != document.end() && zones->is_object()) {
        mArrivalOptions.SelectedZone = zones->value("Selected", std::string{});
        if (const auto boot = zones->find("Boot"); boot != zones->end() && boot->is_string())
          mArrivalOptions.BootZone = boot->get<std::string>();
      }
      if (const auto arrival = document.find("Arrival"); arrival != document.end() && arrival->is_object()) {
        const auto axes = mTitle->Axes();
        for (size_t axis = 0; axis < axes.size() && axis < kDevWarpMaxAxes; ++axis)
          mArrivalOptions.Arrival[axis] = arrival->value(axes[axis].Id, kDevWarpPreset);
      }
    }
  } catch (const std::exception &exception) {
    mStorageError = "Cannot read " + mStorage.string() + ": " + exception.what();
  }
}

void DevWarpRuntime::SaveLocked() {
  if (mStorage.empty()) return;
  nlohmann::json points = nlohmann::json::object();
  for (const auto &[name, point] : mPoints)
    points[name] = {{"entranceId", point.EntranceId},
                    {"roomNum", point.RoomNum},
                    {"pos", {{"x", point.Position[0]}, {"y", point.Position[1]}, {"z", point.Position[2]}}},
                    {"rotY", point.RotY},
                    {"bootToPoint", point.BootToPoint},
                    {"useSceneEntrance", point.UseSceneEntrance}};
  nlohmann::json document = {
      {"WarpPoints", std::move(points)},
      {"DebugWarpScreen",
       {{"Better", mOptions.Better},
        {"Translation", mOptions.Translation},
        {"VanillaScene", mOptions.VanillaScene},
        {"BetterScene", mOptions.BetterScene},
        {"BetterEntrance", mOptions.BetterEntrance},
        {"LinkAge", static_cast<int>(mOptions.Age)},
        {"Night", mOptions.Night}}}};
  // A title with arrival axes or zones; OoT3D's file keeps SoH's keys only.
  if (PersistsArrival()) {
    document["Zones"] = {{"Selected", mArrivalOptions.SelectedZone},
                         {"Boot", mArrivalOptions.BootZone.empty() ? nlohmann::json(nullptr)
                                                                   : nlohmann::json(mArrivalOptions.BootZone)}};
    nlohmann::json arrival = nlohmann::json::object();
    const auto axes = mTitle->Axes();
    for (size_t axis = 0; axis < axes.size() && axis < kDevWarpMaxAxes; ++axis)
      arrival[axes[axis].Id] = mArrivalOptions.Arrival[axis];
    document["Arrival"] = std::move(arrival);
  }
  const auto temporary = mStorage.string() + ".tmp";
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    stream << document.dump(2) << '\n';
    if (!stream) {
      mStorageError = "Cannot write " + temporary;
      return;
    }
  }
  std::error_code renamed;
  std::filesystem::rename(temporary, mStorage, renamed);
  mStorageError = renamed ? "Cannot replace " + mStorage.string() + ": " + renamed.message() : std::string{};
}

} // namespace Oot3dNativeGame
