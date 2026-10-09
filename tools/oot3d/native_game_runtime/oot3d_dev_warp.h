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

class DevWarpRuntime {
 public:
  // `storage` is the JSON file holding the warp points and the screen
  // choices; an empty path keeps them for the session only.
  explicit DevWarpRuntime(std::filesystem::path storage);

  // UI side.
  [[nodiscard]] DevWarpStatus Status() const;
  [[nodiscard]] DevWarpAvailability Check(const DevWarpEntrance &entrance) const;
  // Requests the destination with SoH's age and day/night choice.
  // Without `night` the time of day stays as it is (the vanilla screen).
  void WarpTo(const DevWarpEntrance &entrance, DevWarpAge age, std::optional<bool> night);
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
  };

  bool Observe(DevWarpGuest &guest, uint32_t playState, int16_t *pendingEntrance);
  bool ReadEntranceTable(DevWarpGuest &guest);
  bool Apply(DevWarpGuest &guest, uint32_t playState, const Request &request, std::string *error);
  void Report(DevWarpGuest &guest, const char *event, uint64_t guestFrame);
  void Load();
  void SaveLocked();

  mutable std::mutex mMutex;
  std::filesystem::path mStorage;
  std::string mStorageError;
  std::map<std::string, DevWarpPoint> mPoints;
  DevWarpScreenOptions mOptions;
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
