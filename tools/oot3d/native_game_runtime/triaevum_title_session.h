#pragma once

// What a title adapter adds to the native A32 window session beyond the
// shared CTR host, A32 runtime and renderer: its hooks and guest patches, its
// scene view and environment, its interface frame, its developer tools and its
// run summary. The session calls a title's session at fixed points; every
// member defaults to the session's own behaviour, so a title without one runs
// exactly as before. OoT3D's adapter is still wired into the session itself.
// Which title sessions a build contains is TRIAEVUM_TITLES.

#include "native_scene_environment_state.h"
#include "oot3d_environment_time.h"
#include "oot3d_static_shadow_actor_catalog.h"
#include "triaevum_title_adapter.h"
#include "fast/extension/grass_types.h"
#include "gameplay_rumble.h"
#include "triaevum_title_presentation.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace oot3d::recomp::a32 {
struct GuestState;
class MemoryBus;
} // namespace oot3d::recomp::a32

namespace Oot3dNativeGame {

class DevFreeCamera;
class DevWarpRuntime;
class DevWarpTitleAbi;
class NativeA32Process;
class NativeA32SceneViewProbe;
class Oot3dPicaCommandListCompositionProvider;
struct NativeA32SceneViewSnapshot;
struct Oot3dPicaViewportState;
struct SceneEnvironmentProfile;

// The latest perspective frustum a title's builder produced (s0..s5), before
// the presentation policy widens it.
struct TitleSceneFrustum {
  uint32_t Pc = 0, ReturnAddress = 0;
  float Left = 0, Right = 0, Bottom = 0, Top = 0, NearPlane = 0, FarPlane = 0;
};

// What the session lends a title while it installs its hooks.
struct TitleSessionHookSetup {
  std::vector<uint32_t> &BlockEntryHooks;
  // The title's perspective frustum builder, and whether each frustum it
  // builds is published as the scene view.
  uint32_t &PerspectiveFrustumEntry;
  bool &FrustumIsSceneView;
  NativeA32SceneViewProbe &SceneView;
  // The deterministic presentation clock (microseconds).
  std::function<int64_t()> PresentationClock;
  // Developer builds: the title's guest ABI for the shared developer tools,
  // and the free camera its hooks drive.
  std::shared_ptr<DevWarpTitleAbi> *DevToolsTitle = nullptr;
  std::shared_ptr<DevFreeCamera> FreeCamera;
  // Local launcher preparation, never serialized into a game savestate.
  std::filesystem::path MessageEditsPath;
};

// What the session lends a title at an observed block entry.
struct TitleBlockEntry {
  uint32_t Pc = 0;
  oot3d::recomp::a32::GuestState &State;
  oot3d::recomp::a32::MemoryBus &Memory;
  NativeA32SceneViewProbe *SceneView = nullptr;
  const std::optional<TitleSceneFrustum> &PendingSceneFrustum;
  uint32_t HostFrame = 0;
};

class TitleSession {
public:
  virtual ~TitleSession() = default;

  // The game's name in capability messages.
  [[nodiscard]] virtual const char *GameName() const = 0;
  // The composition provider the CTR host feeds, or nullptr for the
  // session's own.
  virtual Oot3dPicaCommandListCompositionProvider *CompositionProvider() { return nullptr; }
  // The shared renderer's native fragment lighting operand and lit shadow
  // receivers, which this title's shaders need.
  [[nodiscard]] virtual bool NativeFragmentLighting() const { return false; }
  // Display name of point group zero, not a separate settings owner.
  [[nodiscard]] virtual std::string_view CompanionLightName() const { return "Navi"; }
  // Title-owned settings not serialized by the shared state+settings owners.
  // Shown before saving/loading and retained in the state metadata.
  [[nodiscard]] virtual std::string_view SavestateSettingsNotice() const { return {}; }
  // The title's own mod settings a "state + settings" save carries (as
  // OoT3D's TopScreen settings are): a JSON object, or nullopt for none.
  [[nodiscard]] virtual std::optional<nlohmann::json>
  CaptureSavestateTitleSettings() const {
    return std::nullopt;
  }
  // Applies a block CaptureSavestateTitleSettings produced. False (with an
  // error) leaves the current settings unchanged.
  virtual bool ApplySavestateTitleSettings(const nlohmann::json &settings,
                                           std::string *error) {
    (void)settings;
    if (error != nullptr)
      *error = "this title has no state settings";
    return false;
  }
  // The actor profiles (the title's own ids) whose rigid geometry may be
  // cached in static shadow layers. None by default: every actor is dynamic.
  [[nodiscard]] virtual StaticShadowActorPolicy StaticShadowActors() const { return {}; }
  // The grass source and mask rules ({"Sources", "Masks"}) a configuration
  // older than grass rules takes, or none.
  [[nodiscard]] virtual std::optional<nlohmann::json> GrassRuleDefaults() const { return std::nullopt; }

  // Optional independent authoring files below <config>/grass/<namespace>.
  // Empty keeps the existing configuration storage (including OoT3D).
  [[nodiscard]] virtual std::string_view GrassRuleNamespace() const { return {}; }
  // Completed visual source frame: title reads its live native colliders.
  // Values only cross this seam; the renderer sees no guest layout or ids.
  [[nodiscard]] virtual std::vector<Fast::Extension::GrassInteractor> ActorInteractions(
      oot3d::recomp::a32::MemoryBus& memory, uint64_t sourceFrame) const {
    (void)memory; (void)sourceFrame;
    return {};
  }

  // Whether the title has prepared shaders: a vertex-program family and
  // fragment-family packs built in, and a scenario corpus and Forge
  // preparation behind the persistent pipeline cache
  // (fast/renderer3ds/title_shader_preparation.h). Without them the renderer
  // uses no always-available programs; every program a draw uses is still
  // compiled when the draw needs it. OoT3D (no session) has them.
  [[nodiscard]] virtual bool PreparedShaders() const { return false; }
  // The title's pipeline cache namespace: its own pipeline cache files in
  // the renderer cache directory, never another title's (non-empty; lower
  // case letters, digits, '_' and '-'). OoT3D (no session) keeps the
  // original file names.
  [[nodiscard]] virtual std::string_view PipelineCacheNamespace() const = 0;

  [[nodiscard]] virtual bool IsGameplayHud(uint32_t sourcePc, uint32_t nativeValue) const {
    (void)sourcePc; (void)nativeValue; return false;
  }
  virtual void SetPresentation(const TitlePresentationSettings& settings) { (void)settings; }
  // A title may apply the renderer/window policy at its native scene projection
  // loads. Its unrelated stereo/base-frustum helper must then stay untouched.
  [[nodiscard]] virtual bool OwnsSceneProjection() const { return false; }

  // Optional product photo-camera input gate. nullopt preserves legacy title
  // handling; false releases gameplay input to menus/cutscenes/transitions.
  [[nodiscard]] virtual std::optional<bool> PhotoCameraOwnsInput(
      oot3d::recomp::a32::MemoryBus& memory) const { (void)memory; return std::nullopt; }

  // Input owns admission/strength/playback. Disabled must reset history without
  // reading guest memory; absent producers return no events.
  [[nodiscard]] virtual std::vector<ThreeDsRecomp::Input::RumblePulse> RumblePulses(
      oot3d::recomp::a32::MemoryBus& memory, bool enabled) {
    (void)memory; (void)enabled;
    return {};
  }

  // Adds the title's block-entry hooks and guest patches.
  virtual void InstallHooks(TitleSessionHookSetup &setup) { (void)setup; }
  // The process image is mounted: checks it and arms the guest patches.
  virtual void ImageMounted(NativeA32Process &process) { (void)process; }
  // Each observed block entry, after the session's own observers.
  virtual void ObserveBlockEntry(TitleBlockEntry &entry) { (void)entry; }

  // The vertical extension of the frame the interface keeps on a draw with
  // this viewport, when the title lays its interface out itself.
  [[nodiscard]] virtual std::optional<float> UiFrameExtensionY(const Oot3dPicaViewportState &viewport) const {
    (void)viewport;
    return std::nullopt;
  }
  // Whether the environment a scene view snapshot carries is still current.
  [[nodiscard]] virtual bool SceneEnvironmentStillCurrent(oot3d::recomp::a32::MemoryBus &memory,
                                                          const NativeSceneEnvironmentState &state) const {
    (void)memory;
    (void)state;
    return true;
  }
  // The time and player the scene environment follows, when the title's
  // environment supplies them (false keeps the session's).
  virtual bool SceneEnvironmentInputs(const NativeA32SceneViewSnapshot &snapshot,
                                      std::optional<SceneEnvironmentTime> &time,
                                      std::optional<ScenePlayerPlacement> &player) const {
    (void)snapshot;
    (void)time;
    (void)player;
    return false;
  }

  // The title's base scene environment of a group: the profile a scene of
  // that group takes when neither the user nor the release has a file for it
  // (false keeps the session's fallback for that group).
  virtual bool BaseSceneEnvironment(SceneEnvironmentGroup group, SceneEnvironmentProfile &profile) const {
    (void)group;
    (void)profile;
    return false;
  }

  // TRIAEVUM_DEV_WARP in the title's own syntax.
  virtual void RunDevWarpScript(std::string_view script, DevWarpRuntime &devWarp) {
    (void)script;
    (void)devWarp;
  }
  // A savestate was restored.
  virtual void Reset() {}
  // Host-held title work that a state cannot carry is in flight (a host
  // continuation of guest code): the save waits for the next boundary.
  [[nodiscard]] virtual bool SavestateCapturePending() const { return false; }

  // runtime.json: the title's composition and guest patch statistics.
  [[nodiscard]] virtual nlohmann::json CompositionSummary() const { return nullptr; }
  [[nodiscard]] virtual nlohmann::json GuestPatchSummary() const { return nullptr; }
};

// The session of a title this build contains, or nullptr for a title whose
// adapter needs none (OoT3D EUR, generic titles). Throws when the title's
// adapter is not part of this build.
std::unique_ptr<TitleSession> CreateTitleSession(TitleAdapter adapter);

// Whether the running title has prepared shaders (TitleSession::PreparedShaders):
// its session says so; OoT3D's adapter, still wired into the window session
// itself, has them; a generic title has none.
[[nodiscard]] inline bool TitlePreparedShaders(TitleAdapter adapter, const TitleSession *session) {
  return session != nullptr ? session->PreparedShaders() : adapter == TitleAdapter::Oot3dEur;
}

// The running title's pipeline cache namespace (TitleSession::
// PipelineCacheNamespace): its session's; OoT3D keeps the original file
// names (empty); a generic title has its own files, apart from OoT3D's.
[[nodiscard]] inline std::string_view TitlePipelineCacheNamespace(TitleAdapter adapter,
                                                                 const TitleSession *session) {
  if (session != nullptr) return session->PipelineCacheNamespace();
  return adapter == TitleAdapter::Oot3dEur ? std::string_view{} : std::string_view{"generic"};
}

} // namespace Oot3dNativeGame
