#include "oot3d_native_a32_scene_view.h"

#include "a32_runtime.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace {

class SceneViewMemoryBus final : public oot3d::recomp::a32::MemoryBus {
public:
  uint32_t Reads = 0;
  bool Read32(uint32_t address, uint32_t *value) override {
    ++Reads;
    if (value == nullptr || address + sizeof(uint32_t) > Bytes.size()) {
      return false;
    }
    *value = static_cast<uint32_t>(Bytes[address]) |
             (static_cast<uint32_t>(Bytes[address + 1U]) << 8U) |
             (static_cast<uint32_t>(Bytes[address + 2U]) << 16U) |
             (static_cast<uint32_t>(Bytes[address + 3U]) << 24U);
    return true;
  }

  bool Write32(uint32_t address, uint32_t value) override {
    if (address + sizeof(uint32_t) > Bytes.size()) {
      return false;
    }
    for (uint32_t index = 0; index < sizeof(uint32_t); ++index) {
      Bytes[address + index] = static_cast<uint8_t>(value >> (index * 8U));
    }
    return true;
  }

  void WriteFloat(uint32_t address, float value) {
    if (!Write32(address, std::bit_cast<uint32_t>(value))) {
      throw std::runtime_error("scene-view test write failed");
    }
  }

  std::array<uint8_t, 0x8000U> Bytes{};
};

void ExpectNear(float actual, float expected, const char *role) {
  if (std::abs(actual - expected) > 1.0e-6F) {
    throw std::runtime_error(role);
  }
}

} // namespace

void RunNativeA32SceneViewProbeTests() {
  Oot3dNativeGame::NativeA32SceneViewProbe probe;
  SceneViewMemoryBus memory;
  if (probe.Capture(memory).has_value()) {
    throw std::runtime_error("scene-view probe captured without evidence");
  }

  constexpr uint32_t playState = 0x1000U;
  constexpr uint32_t view = playState + 0x0188U;
  probe.ObservePerspective(0x002FDE9CU, 0x00300F50U, -0.04F, 0.04F, -0.03F,
                           0.03F, 0.1F, 5000.0F);
  const auto perspectiveOnly = probe.Capture(memory);
  if (!perspectiveOnly.has_value() || perspectiveOnly->CameraAvailable ||
      perspectiveOnly->PlayStateAddress != 0U) {
    throw std::runtime_error("scene-view probe did not publish frustum-only state");
  }

  probe.ObservePlayState(playState);
  memory.Write32(playState + 0x104U, 0x55U);
  memory.WriteFloat(view + 0x30U, 10.0F);
  memory.WriteFloat(view + 0x34U, 20.0F);
  memory.WriteFloat(view + 0x38U, 30.0F);
  memory.WriteFloat(view + 0x3CU, 11.0F);
  memory.WriteFloat(view + 0x40U, 22.0F);
  memory.WriteFloat(view + 0x44U, 33.0F);

  const auto snapshot = probe.Capture(memory);
  if (!snapshot.has_value() || snapshot->PlayStateAddress != playState ||
      !snapshot->CameraAvailable ||
      snapshot->SceneId != 0x55U ||
      snapshot->GuestFunction != 0x002FDE9CU ||
      snapshot->GuestReturnAddress != 0x00300F50U || snapshot->Serial == 0U) {
    throw std::runtime_error("native scene-view identity mismatch");
  }
  ExpectNear(snapshot->Left, -0.04F, "scene-view left");
  ExpectNear(snapshot->FarPlane, 5000.0F, "scene-view far");
  ExpectNear(snapshot->Eye[1], 20.0F, "scene-view eye");
  ExpectNear(snapshot->At[2], 33.0F, "scene-view at");

  probe.ObservePerspective(0U, 0U, 1.0F, 1.0F, -1.0F, 1.0F, 0.1F, 10.0F);
  if (probe.Stats().PerspectiveObservations != 2U ||
      probe.Stats().InvalidPerspectiveObservations != 1U ||
      probe.Stats().SnapshotsCaptured != 2U ||
      probe.Stats().ReadFailures != 0U) {
    throw std::runtime_error("native scene-view probe stats mismatch");
  }

  memory.Write32(playState + 0x104U, 0xFFFFU);
  const auto unavailableScene = probe.Capture(memory);
  if (!unavailableScene || unavailableScene->SceneId)
    throw std::runtime_error("invalid native scene identity inherited the previous scene");

  // The file select runs over the PlayState's block: the title demo's scene
  // while that PlayState still updates (Player_Update included), scene 0 (the
  // Deku Tree's id) otherwise, the View readable either way. It is its own
  // identity while FileSelect_Update keeps running, whatever the order of the
  // two updates within a refresh; it lapses a few refreshes after the last.
  probe.ObserveFileSelect();
  memory.Write32(playState + 0x104U, 0x6BU);
  const auto fileSelect = probe.Capture(memory);
  if (!fileSelect || !fileSelect->FileSelect || !fileSelect->CameraAvailable ||
      fileSelect->SceneId != Oot3dNativeGame::kOot3dFileSelectSceneId ||
      probe.Stats().FileSelectObservations != 1U)
    throw std::runtime_error("file select did not take its own scene identity");
  probe.BeginGuestRefresh();
  probe.ObservePlayState(playState);
  memory.Write32(playState + 0x104U, 0x00U);
  if (const auto held = probe.Capture(memory); !held || !held->FileSelect)
    throw std::runtime_error("the title PlayState's update took the file select's identity");
  for (uint32_t refresh = 2; refresh < Oot3dNativeGame::kOot3dFileSelectOwnershipRefreshes; ++refresh) {
    probe.BeginGuestRefresh();
    if (const auto held = probe.Capture(memory); !held || !held->FileSelect)
      throw std::runtime_error("file select identity lapsed within its refresh window");
  }
  probe.BeginGuestRefresh();
  const auto dekuTree = probe.Capture(memory);
  if (!dekuTree || dekuTree->FileSelect || dekuTree->SceneId != 0x00U)
    throw std::runtime_error("gameplay scene 0 confused with the file select");
  probe.ObserveFileSelect();
  probe.Reset();
  if (probe.Capture(memory).has_value()) {
    throw std::runtime_error("scene-view probe survived reset");
  }
  probe.ObservePerspective(0x002FDE9CU, 0x00300F50U, -0.04F, 0.04F, -0.03F, 0.03F, 0.1F, 5000.0F);
  probe.ObservePlayState(playState);
  if (const auto afterReset = probe.Capture(memory); !afterReset || afterReset->FileSelect)
    throw std::runtime_error("file select ownership survived reset");

  probe.UseExternalSceneEnvironment();
  probe.ObservePlayState(playState); // OoT hooks cannot opt this title back in.
  probe.BeginExternalSceneView();
  probe.ObservePerspectiveCamera(0x123U, 0x456U, -1, 1, -1, 1, 1, 100,
                                {100,200,300}, {100,200,301});
  Oot3dNativeGame::NativeSceneEnvironmentState state;
  state.ContextIdentity=0x10000000U; state.GuestFrame=100; state.SceneId=0x2A;
  state.Night=true; state.Day=2; state.Clock=0xD000;
  state.Player=Oot3dNativeGame::ScenePlayerPlacement{{1,2,3},90};
  probe.ObserveExternalSceneEnvironment(state);
  const auto reads=memory.Reads;
  const auto external=probe.Capture(memory);
  if (!external || !external->Environment || external->SceneId!=0x2A ||
      external->Environment->Night!=true || external->WorldIdentity==0 ||
      external->Eye!=std::array<float,3>{100,200,300} || memory.Reads!=reads ||
      probe.Stats().LastPlayStateAddress!=0)
    throw std::runtime_error("external title data read the OoT layout or replaced the camera");
  const auto epoch=external->WorldIdentity;
  const auto serial=external->Serial;
  probe.ObserveExternalSceneEnvironment(std::nullopt);
  const auto missing=probe.Capture(memory);
  if (!missing || missing->Environment || missing->SceneId || missing->WorldIdentity!=0 ||
      missing->Serial==serial)
    throw std::runtime_error("invalid external context retained scene state or publication serial");
  probe.Reset();
  probe.ObservePlayState(playState);
  probe.BeginExternalSceneView();
  probe.ObservePerspectiveCamera(0x123U,0x456U,-1,1,-1,1,1,100,{1,2,3},{1,2,4});
  probe.ObserveExternalSceneEnvironment(state);
  const auto restored=probe.Capture(memory);
  if (!restored || restored->WorldIdentity<=epoch || probe.Stats().LastPlayStateAddress!=0)
    throw std::runtime_error("restore reused world identity or enabled OoT reads");
  probe.BeginExternalSceneView();
  probe.ObserveExternalSceneEnvironment(state);
  if (probe.Capture(memory))
    throw std::runtime_error("missing camera inherited a previous scene view");
}
