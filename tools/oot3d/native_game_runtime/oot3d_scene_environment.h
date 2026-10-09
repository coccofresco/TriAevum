#pragma once

#include "fast/oot3d/graphics_settings_window.h"
#include "fast/renderer3ds/pica_environment_lighting.h"
#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace Oot3dNativeGame {

struct SceneDirectionalLight {
  float Azimuth = 45.0F;
  float Elevation = 45.0F;
  std::array<float, 3> Color{1.0F, 1.0F, 1.0F};
  float Intensity = 0.0F;
  bool operator==(const SceneDirectionalLight &) const = default;
};
// A light placed in the scene from F1 (Scene lights > Placed lights).
enum class PlacedLightType : uint8_t { Sun, Spot, Point, Surface, Omni };
enum class PlacedLightShape : uint8_t { Rectangle, Disc };
inline constexpr size_t kMaximumPlacedLights = 16U;
struct PlacedLight {
  // Stable within the profile: the light's identity (shadow rows).
  uint32_t Id = 0;
  std::string Name;
  PlacedLightType Type = PlacedLightType::Point;
  bool Visible = true;
  std::array<float, 3> Color{1.0F, 1.0F, 1.0F};
  float Intensity = 1.0F;
  // World units (OOT3D: Y up).
  std::array<float, 3> Position{};
  // Degrees: yaw about +Y (0 faces +Z, 90 faces +X), pitch (positive up),
  // roll about the light's own axis. The light emits along that axis.
  std::array<float, 3> Rotation{};
  bool CastShadows = false;
  // Point, omni and spot: the distance it reaches; surface (disc): its radius.
  float Radius = 300.0F;
  // Surface (rectangle).
  float Width = 100.0F;
  float Height = 100.0F;
  PlacedLightShape Shape = PlacedLightShape::Rectangle;
  // Surface: emits from both faces.
  bool TwoSided = false;
  // Spot: the full beam angle and the soft edge (a fraction of it).
  float BeamAngle = 45.0F;
  float BeamSoftness = 0.2F;
  // Surface: the distance it reaches.
  float Range = 300.0F;
  bool operator==(const PlacedLight &) const = default;
};
// The renderer's light for a placed light of this scene: the emission axis
// from yaw and pitch, a rectangle's width axis turned by the roll.
[[nodiscard]] Fast::Renderer3ds::PicaPlacedLight
ResolvePlacedLight(const PlacedLight &light, uint64_t identity);

struct SceneEnvironmentProfile {
  bool Enabled = false;
  bool PreserveSceneBrightness = true;
  float NativeDirectStrength = 0.0F;
  float NativeLocalStrength = 0.0F;
  float NativeAmbientStrength = 0.0F;
  std::array<float, 3> AmbientColor{1.0F, 1.0F, 1.0F};
  float AmbientIntensity = 0.2F;
  float FogStrength = 1.0F;
  std::array<SceneDirectionalLight, 2> Lights{
      {{45.0F, 45.0F, {1.0F, 1.0F, 1.0F}, 1.0F},
       {-135.0F, 30.0F, {1.0F, 1.0F, 1.0F}, 0.0F}}};
  // Where the key light points: its own azimuth/elevation, the scene's main
  // light turned by the offsets below, or the axis of the scene's other
  // (ambient) light on the main light's side. Its own direction applies
  // while the scene has no main directional light.
  Fast::Renderer3ds::PicaEnvironmentKeyDirection KeyDirection =
      Fast::Renderer3ds::PicaEnvironmentKeyDirection::Authored;
  float KeyAzimuthOffset = 0.0F;
  float KeyElevationOffset = 0.0F;
  std::array<Fast::Renderer3ds::PicaScenePointLightSettings,
             Fast::Renderer3ds::kPicaScenePointLightGroups> PointLights{};
  std::vector<PlacedLight> PlacedLights;
  // The directional shadow casters' face culling (off: both faces cast).
  bool ShadowCasterCulling = true;
  bool operator==(const SceneEnvironmentProfile &) const = default;
};

// Where Link is, for placing a light: world position and facing (degrees).
struct ScenePlayerPlacement {
  std::array<float, 3> Position{};
  float Yaw = 0.0F;
  bool operator==(const ScenePlayerPlacement &) const = default;
};
// The environment of every scene without its own profile: the title intro's
// (scene 0x6B) tuned lights. Saving stores a profile for the current scene only.
[[nodiscard]] SceneEnvironmentProfile BaseSceneEnvironmentProfile();

struct SceneEnvironmentSnapshot {
  std::optional<uint16_t> SceneId;
  SceneEnvironmentProfile Profile;
  // The profile file shown and saved: the night one while the scene has a
  // night profile and the game's time is night, otherwise the scene's own.
  std::filesystem::path Path;
  std::string Error;
  bool Dirty = false;
  bool FileExists = false;
  // The game's time of day (SaveContext nightFlag).
  bool Night = false;
  // The scene keeps a separate night profile (scene-XXXX.night.environment.json).
  bool NightProfileExists = false;
  // The night profile is the one in use.
  bool EditingNight = false;
  // The player in this scene, when known.
  std::optional<ScenePlayerPlacement> Player;
  // Advances whenever the profile is replaced from outside the editor (scene
  // change, reload, a file applied from disk): an edit made on an older
  // revision is refused rather than overwriting what replaced it.
  uint64_t Revision = 0;
};

class SceneEnvironmentRuntime final {
public:
  using Publisher =
      std::function<void(const Fast::Renderer3ds::PicaEnvironmentLighting &)>;
  SceneEnvironmentRuntime(std::filesystem::path directory, Publisher publish);
  // A scene with a night profile switches to it while `night`; any unsaved
  // preview is dropped then, as when the scene changes.
  void ObserveScene(std::optional<uint16_t> scene, bool night = false,
                    std::optional<ScenePlayerPlacement> player = std::nullopt);
  [[nodiscard]] SceneEnvironmentSnapshot Snapshot() const;
  // False (and an error) when the profile does not validate, or when
  // `basedOnRevision` is older than the profile in use.
  bool Preview(SceneEnvironmentProfile profile, std::optional<uint64_t> basedOnRevision = std::nullopt);
  bool Save();
  bool Reload();
  // Saves the values shown as the scene's night profile; at night it becomes
  // the profile in use.
  bool CreateNightProfile();
  // Deletes the night profile: the scene's own profile applies day and night.
  bool RemoveNightProfile();
  // Light gizmos: the light the placed-light editor selected and whether it
  // wants gizmos, told while the editor is drawn.
  void EditLightGizmo(uint32_t selectedLight, bool enabled);
  // Shows or hides the gizmos without selecting a light (agents' captures).
  void ShowLightGizmos(bool shown);
  // Renderer order, once per frame: the placed lights' gizmos (world debug
  // lines) while the editor was drawn within the last half second or while
  // `forced` (the free camera), none otherwise. `eye`: the scene's camera.
  void PublishLightGizmo(const std::optional<std::array<float, 3>> &eye, bool forced = false);
  // Developer builds (TRIAEVUM_ENVIRONMENT_HOT_RELOAD): the scene's profile
  // files are re-read when they change on disk, so an agent can edit them
  // while the game runs. A file that does not validate, or is missing,
  // keeps the profile in use; unsaved F1 edits are never replaced. Each
  // outcome is reported on stderr (TRIAEVUM_ENVIRONMENT_RELOAD).
  void SetHotReload(bool enabled);
  // Renderer order, once per frame; looks at the files at most every 250 ms
  // and acts on a change once two looks agree (a file still being written
  // changes between them). No-op outside developer builds.
  void PollProfileFiles();
  void PollProfileFiles(std::chrono::steady_clock::time_point now);

private:
  bool ReloadLocked();
  // Reads the day or night file and, when it validates (or is absent: the
  // base environment), makes it the profile in use together with that
  // selection; otherwise only records the error.
  bool LoadSelectionLocked(bool editingNight, bool nightExists);
  // A file as it is on disk: "-" when absent, otherwise size and time.
  [[nodiscard]] std::string FileStampLocked(bool night) const;
  void AcknowledgeFilesLocked();
  void PublishLocked();
  [[nodiscard]] std::filesystem::path ProfilePathLocked(bool night) const;
  void SelectLocked(std::optional<uint16_t> scene, bool night);
  std::filesystem::path mDirectory;
  Publisher mPublish;
  mutable std::mutex mMutex;
  SceneEnvironmentSnapshot mState;
  SceneEnvironmentProfile mSaved;
  uint32_t mGizmoSelected = 0;
  bool mGizmoEnabled = true;
  std::chrono::steady_clock::time_point mGizmoEdited{};
  bool mGizmoPublished = false;
  bool mHotReload = false;
  std::chrono::steady_clock::time_point mLastFilePoll{};
  // [day, night]: the stamps handled, the last look, the last reported as
  // waiting for unsaved edits.
  std::array<std::string, 2> mFileStamps{};
  std::array<std::string, 2> mPendingStamps{};
  std::array<std::string, 2> mReportedStamps{};
  // The content of the file in use (unchanged content is no change).
  size_t mAppliedHash = 0;
  uint64_t mRevision = 0;
};

std::shared_ptr<Fast::Oot3d::GraphicsSettingsPanelTab>
CreateSceneEnvironmentPanel(std::shared_ptr<SceneEnvironmentRuntime> runtime);

} // namespace Oot3dNativeGame
