#pragma once

// Developer warping (developer builds only, TRIAEVUM_DEVELOPER_TOOLS):
// Ship of Harkinian's Debug Warp Screen and Warp Points over OOT3D.
//
// A warp never builds a scene: it writes the save-context fields SoH's warp
// writes (age, time of day, respawn data) and asks code.bin's own transition
// helper for the entrance, exactly like NativeScenarioBootstrap. Destinations
// are offered only when OOT3D's own global entrance table leads to the scene
// and spawn SoH expects.

#include "oot3d_dev_warp_catalog.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Oot3dNativeGame {

// Guest access used by the warp: memory, and the original transition request
// (request helper plus effect preparation). The window adapts the native
// process; tests use a fake.
class DevWarpGuest {
 public:
  virtual ~DevWarpGuest() = default;
  virtual bool Read(uint32_t address, std::span<uint8_t> bytes) = 0;
  virtual bool Write(uint32_t address, std::span<const uint8_t> bytes) = 0;
  // `accepted` is the helper's result: false when the slot is not free.
  virtual bool RequestTransition(uint32_t playState, uint16_t entrance,
                                 bool *accepted, std::string *error) = 0;
};

class NativeA32Process;
// The window's guest: code.bin's transition request (FUN_003348E8 with
// trigger 20) and effect preparation (FUN_0035DA3C), the structural
// scenario contract (docs/OOT3D_STRUCTURAL_SCENARIO_COVERAGE.md).
std::unique_ptr<DevWarpGuest> MakeDevWarpProcessGuest(NativeA32Process &process);

// SoH's WarpPoint, persisted with the same fields.
struct DevWarpPoint {
  int32_t EntranceId = 0;
  int8_t RoomNum = 0;
  float Position[3]{};
  int16_t RotY = 0;
  bool BootToPoint = false;
  bool UseSceneEntrance = false;
};

enum class DevWarpAge : uint8_t { Adult = 0, Child = 1 };

// What the game is doing, read at each guest frame.
struct DevWarpObservation {
  bool PlayState = false;
  // Normal gameplay: a game state the player controls, not the title, a
  // title/demo cutscene or a file menu.
  bool Gameplay = false;
  uint32_t Entrance = 0;
  int16_t Scene = -1;
  uint8_t Room = 0;
  int32_t Age = 0;
  int32_t AgeOnLoad = -1;  // PlayState linkAgeOnLoad
  uint16_t DayTime = 0;
  bool Player = false;
  float Position[3]{};
  int16_t RotY = 0;
  // A title with arrival axes: the guest's current value per axis.
  DevWarpSelection Arrival = kDevWarpKeepAll;
  // A title ABI's player health in its own units (-1 unknown), for the log.
  int32_t Health = -1;
};

enum class DevWarpPhase : uint8_t { Idle, Waiting, Requested, Arrived, Failed };

// The result of checking a SoH destination against code.bin's entrance table.
struct DevWarpAvailability {
  bool Known = false;      // the table has been read
  bool Available = false;
  std::string Reason;      // why not, when known and unavailable
};

struct DevWarpStatus {
  DevWarpObservation Observation;
  DevWarpPhase Phase = DevWarpPhase::Idle;
  std::string Message;
  bool EntranceTableRead = false;
};

// The Debug Warp Screen choices SoH keeps in CVars.
struct DevWarpScreenOptions {
  bool Better = true;
  bool Translation = true;
  int VanillaScene = 0;
  int BetterScene = 0;
  int BetterEntrance = 0;
  DevWarpAge Age = DevWarpAge::Child;
  bool Night = false;
};

// The zone list and arrival choices of a title with arrival axes, persisted
// beside SoH's keys. One boot target at a time: a boot zone clears the warp
// points' boot mark and the reverse.
struct DevWarpArrivalOptions {
  std::string SelectedZone;
  std::string BootZone;
  // Per axis: a choice value, kDevWarpKeep or kDevWarpPreset.
  DevWarpSelection Arrival = kDevWarpPresetAll;
};

class DevFreeCamera;
// Title-specific guest ABI. A null ABI preserves the established OoT path.
class DevWarpTitleAbi {
 public:
  virtual ~DevWarpTitleAbi() = default;
  virtual std::span<const DevWarpEntrance> Entrances() const = 0;
  virtual std::span<const DevWarpScene> Scenes() const = 0;
  virtual bool CameraAvailable() const = 0;
  virtual bool PointsAvailable() const = 0;
  virtual uint32_t PlayStateAddress(DevWarpGuest&) const = 0;
  virtual bool Observe(DevWarpGuest&, uint32_t, DevWarpObservation&, int16_t&, uint32_t&) const = 0;
  virtual bool ReadEntranceTable(DevWarpGuest&, std::vector<uint32_t>&) const = 0;
  virtual DevWarpAvailability Check(const DevWarpEntrance&, const std::vector<uint32_t>&) const = 0;
  // `arrival` holds a value or kDevWarpKeep per axis (never kDevWarpPreset).
  virtual bool Apply(DevWarpGuest&, uint32_t, uint16_t, const DevWarpPoint*, const DevWarpSelection &arrival,
                     std::string*) const = 0;
  virtual bool Arrived(const DevWarpObservation&, uint16_t, bool) const = 0;
  // Arrival options the title can write (none by default).
  virtual std::span<const DevWarpAxis> Axes() const { return {}; }
  // Curated destinations (none by default).
  virtual std::span<const DevWarpZone> Zones() const { return {}; }
  // After an arrival: empty when the requested options took effect, else
  // "field=<name> ..." naming the first that did not.
  virtual std::string ArrivalMismatch(DevWarpGuest&, const DevWarpObservation&, const DevWarpSelection&) const {
    return {};
  }
  // The observed arrival state for the status line and the log, e.g.
  // "Day 1 12:04, Human".
  virtual std::string Describe(const DevWarpObservation&) const { return {}; }
  virtual bool CameraOwnsInput(DevWarpGuest&) const = 0;
  virtual bool ApplyCamera(DevWarpGuest&, DevFreeCamera&, uint32_t, uint32_t) const = 0;
};

class DevWarpRuntime {
 public:
  // `storage` is the JSON file holding the warp points and the screen
  // choices; an empty path keeps them for the session only.
  explicit DevWarpRuntime(std::filesystem::path storage, std::shared_ptr<DevWarpTitleAbi> title = {});
  [[nodiscard]] bool HasTitleAbi() const { return bool(mTitle); }
  [[nodiscard]] bool CameraAvailable() const { return !mTitle || mTitle->CameraAvailable(); }
  [[nodiscard]] bool PointsAvailable() const { return !mTitle || mTitle->PointsAvailable(); }
  [[nodiscard]] std::span<const DevWarpScene> Scenes() const { return mTitle ? mTitle->Scenes() : DevWarpBetterScenes(); }
  [[nodiscard]] std::span<const DevWarpEntrance> Entrances() const { return mTitle ? mTitle->Entrances() : DevWarpBetterEntrances(); }
  [[nodiscard]] std::span<const DevWarpEntrance> VanillaEntrances() const { return mTitle ? mTitle->Entrances() : DevWarpVanillaEntrances(); }
  bool CameraOwnsInput(DevWarpGuest& guest) const { return !mTitle || mTitle->CameraOwnsInput(guest); }
  [[nodiscard]] std::span<const DevWarpAxis> Axes() const {
    return mTitle ? mTitle->Axes() : std::span<const DevWarpAxis>{};
  }
  [[nodiscard]] std::span<const DevWarpZone> Zones() const {
    return mTitle ? mTitle->Zones() : std::span<const DevWarpZone>{};
  }
  [[nodiscard]] const DevWarpZone *FindZone(std::string_view id) const;
  [[nodiscard]] const DevWarpEntrance *FindEntrance(uint16_t entrance) const;
  [[nodiscard]] std::string Describe(const DevWarpObservation &observation) const {
    return mTitle ? mTitle->Describe(observation) : std::string{};
  }

  // UI side.
  [[nodiscard]] DevWarpStatus Status() const;
  [[nodiscard]] DevWarpAvailability Check(const DevWarpEntrance &entrance) const;
  // Requests the destination with SoH's age and day/night choice.
  // Without `night` the time of day stays as it is (the vanilla screen).
  void WarpTo(const DevWarpEntrance &entrance, DevWarpAge age, std::optional<bool> night);
  // A title with arrival axes: the destination with a value or Keep per axis
  // (kDevWarpPreset counts as Keep outside a zone).
  void WarpTo(const DevWarpEntrance &entrance, const DevWarpSelection &arrival);
  // A curated zone: kDevWarpPreset axes take the zone's suggestion.
  bool WarpToZone(std::string_view id, const DevWarpSelection &arrival, std::string *error);
  // Why the selection cannot be written (an unproven or unknown axis), or empty.
  [[nodiscard]] std::string ArrivalError(const DevWarpSelection &arrival) const;
  // The zone's suggestion where `arrival` asks for it (Keep outside a zone,
  // and Keep for an axis the title has not proven: a preset never blocks a
  // zone, an explicit value of an unproven axis does).
  [[nodiscard]] DevWarpSelection Resolve(const DevWarpSelection &arrival, const DevWarpZone *zone) const;
  void WarpToPoint(const std::string &name);
  // A place that is not a stored point (agents' lab warps), same respawn.
  void WarpToPlace(const std::string &label, const DevWarpPoint &point);
  [[nodiscard]] std::map<std::string, DevWarpPoint> Points() const;
  // Stores the player's current place under the name (SoH's "+" button).
  bool AddPoint(const std::string &name, std::string *error);
  void RemovePoint(const std::string &name);
  // Marks the point warped to when gameplay first starts (SoH's boot point);
  // the other points lose the mark.
  void SetBootPoint(const std::string &name);
  // Marks the zone warped to when gameplay first starts, with the persisted
  // arrival choices; empty clears it. Clears the warp points' boot mark.
  void SetBootZone(std::string_view id);
  [[nodiscard]] DevWarpArrivalOptions ArrivalOptions() const;
  void SetArrivalOptions(const DevWarpArrivalOptions &options);
  [[nodiscard]] DevWarpScreenOptions Options() const;
  void SetOptions(const DevWarpScreenOptions &options);
  [[nodiscard]] const std::string &StorageError() const noexcept { return mStorageError; }

  // Frame side: called once per guest frame with the observed PlayState.
  void Advance(DevWarpGuest &guest, uint32_t playState, uint64_t guestFrame);

 private:
  enum class Kind : uint8_t { SceneEntrance, WarpPoint, Grotto };
  struct Request {
    Kind Type = Kind::SceneEntrance;
    std::string Label;
    uint16_t Entrance = 0;
    uint8_t Scene = 0;
    std::optional<DevWarpAge> Age;
    std::optional<bool> Night;
    DevWarpPoint Point;
    DevWarpGrotto Grotto{};
    // A title with arrival axes: value or Keep per axis.
    DevWarpSelection Arrival = kDevWarpKeepAll;
  };

  bool Observe(DevWarpGuest &guest, uint32_t playState, int16_t *pendingEntrance);
  void QueueLocked(Request request, const std::string &message);
  bool ZoneRequestLocked(const DevWarpZone &zone, const DevWarpSelection &arrival, Request &request,
                         std::string *error) const;
  [[nodiscard]] bool PersistsArrival() const;
  bool ReadEntranceTable(DevWarpGuest &guest);
  bool Apply(DevWarpGuest &guest, uint32_t playState, const Request &request, std::string *error);
  void Report(DevWarpGuest &guest, const char *event, uint64_t guestFrame);
  void Load();
  void SaveLocked();

  std::shared_ptr<DevWarpTitleAbi> mTitle;
  mutable std::mutex mMutex;
  std::filesystem::path mStorage;
  std::string mStorageError;
  std::map<std::string, DevWarpPoint> mPoints;
  DevWarpScreenOptions mOptions;
  DevWarpArrivalOptions mArrivalOptions;
  std::vector<uint32_t> mEntranceTable;
  DevWarpStatus mStatus;
  std::optional<Request> mPending;
  std::optional<Request> mActive;
  uint64_t mRequestFrame = 0;
  // The requested transition was seen in the PlayState's transition slot.
  bool mSawTransition = false;
  // The active warp arrived; the status phase may already show a newer request.
  bool mArrived = false;
  // Guest frames until the settled report after arrival.
  uint32_t mReportFrames = 0;
  // A warp point respawn: the void-out damage Player_Init arms is cancelled
  // once, as SoH's VB_INFLICT_VOID_DAMAGE hook does.
  bool mCancelVoidDamage = false;
  // The player actor of the last observation (0 when none).
  uint32_t mPlayerActor = 0;
  bool mBootChecked = false;
};

} // namespace Oot3dNativeGame
