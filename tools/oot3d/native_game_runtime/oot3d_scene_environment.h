#pragma once

#include "fast/oot3d/graphics_settings_window.h"
#include "native_scene_environment_state.h"
#include "fast/renderer3ds/pica_environment_lighting.h"
#include "oot3d_environment_time.h"
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
  // Surface: the full angle (degrees, 1..180) within which each of its points
  // emits around its axis; 180 is diffuse, less straightens its light.
  float Spread = 180.0F;
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
  Fast::Renderer3ds::PicaRimLightSettings Rim;
  Fast::Renderer3ds::ShadowCasterFaces CasterFaces = Fast::Renderer3ds::ShadowCasterFaces::Native;
  bool operator==(const SceneEnvironmentProfile &) const = default;
};

// Final day fallback when neither scene nor group has a profile: the title
// intro's (scene 0x6B) tuned lights. Normal Save writes the current scene only.
[[nodiscard]] SceneEnvironmentProfile BaseSceneEnvironmentProfile();
// A title without authored tuning: disabled, native gains one, no added light.
[[nodiscard]] SceneEnvironmentProfile NativePreservingSceneEnvironmentProfile();

// Profile RGB is already linear and is interpolated directly. Directions/
// rotations take the shortest angular route. Placed
// lights match by index, with stable blend identities and missing lights faded.
[[nodiscard]] SceneEnvironmentProfile BlendSceneEnvironmentProfiles(
    const SceneEnvironmentProfile& day, const SceneEnvironmentProfile& night, float weight);
enum class SceneEnvironmentPreview : uint8_t { Auto, Day, Night };

struct SceneEnvironmentSnapshot {
  std::optional<uint16_t> SceneId;
  SceneEnvironmentProfile Profile;
  std::optional<SceneEnvironmentGroup> Group;
  std::array<SceneEnvironmentSource, 2> Sources{SceneEnvironmentSource::Base, SceneEnvironmentSource::Day};
  std::array<std::filesystem::path, 2> SourcePaths;
  // The explicitly selected editor tab, independent of native time.
  std::filesystem::path Path;
  std::string Error;
  std::string NightError;
  bool Dirty = false;
  bool FileExists = false;
  // The game's time of day (SaveContext nightFlag).
  bool Night = false;
  // A separate night endpoint exists, from either the scene or its group.
  bool NightProfileExists = false;
  // The night profile is the one being edited, independent of applied weight.
  bool EditingNight = false;
  bool TimeDriven = false;
  float NativeNightWeight = 0;
  float AppliedNightWeight = 0;
  SceneEnvironmentPreview PreviewMode = SceneEnvironmentPreview::Auto;
  bool ForcedWeight = false;
  std::array<bool, 2> DirtyTabs{};
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
  // `defaults`: the release's read-only profiles, named as in `directory`.
  // A slot resolves the user's scene file, the default scene file, the
  // user's group file, the default group file, then the base profile; edits
  // are always saved to `directory`.
  // `fallback`: the base profile when no file resolves (OoT3D's tuned intro
  // lights, or a title's native-preserving profile).
  SceneEnvironmentRuntime(std::filesystem::path directory, Publisher publish,
                          std::filesystem::path defaults = {},
                          SceneEnvironmentProfile fallback = BaseSceneEnvironmentProfile());
  // Time selects the blend, never the editable draft. Scene changes reload.
  void ObserveScene(std::optional<uint16_t> scene, SceneEnvironmentTime time = {},
                    std::optional<ScenePlayerPlacement> player = std::nullopt);
  [[nodiscard]] SceneEnvironmentSnapshot Snapshot() const;
  // False (and an error) when the profile does not validate, or when
  // `basedOnRevision` is older than the profile in use.
  bool Preview(SceneEnvironmentProfile profile, std::optional<uint64_t> basedOnRevision = std::nullopt);
  bool Save();
  bool SaveAsGroupStandard();
  bool UseGroupStandard();
  bool Reload();
  bool SelectTab(bool night);
  void SetPreviewMode(SceneEnvironmentPreview mode);
  // Preview is session-only. Closing F1 or pressing F2 returns to Auto.
  void EndPreview();
  // Deterministic developer captures. Does not modify the files or clock.
  bool SetForcedNightWeight(std::optional<float> weight);
  // Creates the night file from the day draft and selects its editor tab.
  bool CreateNightProfile();
  // Copies the day draft into the night draft; Save night commits it.
  bool CopyDayToNight();
  // Deletes the scene's night file, resolving group night then the day result.
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
  // Developer builds (TRIAEVUM_ENVIRONMENT_HOT_RELOAD): scene files and their
  // group fallbacks are re-read on disk changes. Invalid files keep the last
  // accepted value; removal resolves the next fallback (legacy unknown-group
  // day removal keeps its last value). Unsaved F1 edits are never replaced. Each
  // outcome is reported on stderr (TRIAEVUM_ENVIRONMENT_RELOAD).
  void SetHotReload(bool enabled);
  // Renderer order, once per frame; looks at the files at most every 250 ms
  // and acts on a change once two looks agree (a file still being written
  // changes between them). No-op outside developer builds.
  void PollProfileFiles();
  void PollProfileFiles(std::chrono::steady_clock::time_point now);

private:
  bool LoadSlotLocked(bool night);
  void SyncSelectionLocked();
  [[nodiscard]] std::filesystem::path GroupPathLocked(bool night) const;
  // A file as it is on disk: "-" when absent, otherwise size and time.
  [[nodiscard]] std::string FileStampLocked(bool night) const;
  void AcknowledgeFilesLocked();
  void PublishLocked(bool edited = false);
  [[nodiscard]] std::filesystem::path ProfilePathLocked(bool night) const;
  void SelectLocked(std::optional<uint16_t> scene);
  std::filesystem::path mDirectory;
  std::filesystem::path mDefaults;
  const SceneEnvironmentProfile mFallback;
  Publisher mPublish;
  mutable std::mutex mMutex;
  SceneEnvironmentSnapshot mState;
  struct Draft {
    SceneEnvironmentProfile Profile, Saved;
    std::string Error;
    bool Exists = false, Available = false;
    size_t AppliedHash = 0;
    SceneEnvironmentSource Source = SceneEnvironmentSource::Base;
    std::filesystem::path SourcePath;
  };
  std::array<Draft, 2> mDrafts;
  SceneEnvironmentTime mTime;
  std::optional<float> mForcedNightWeight;
  std::optional<Fast::Renderer3ds::PicaEnvironmentLighting> mPublished;
  uint64_t mContinuityEpoch = 0;
  uint64_t mObservations = 0;
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
  uint64_t mRevision = 0;
};

std::shared_ptr<Fast::Oot3d::GraphicsSettingsPanelTab>
CreateSceneEnvironmentPanel(std::shared_ptr<SceneEnvironmentRuntime> runtime);

} // namespace Oot3dNativeGame
