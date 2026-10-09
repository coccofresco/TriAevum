#include "oot3d_scene_environment.h"
#include "oot3d_scene_environment_codec.h"
#include "oot3d_light_gizmo.h"
#include "ship/config/ConfigPersistence.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>
#include <numbers>
#include <stdexcept>

namespace Oot3dNativeGame {
namespace {
using Json = nlohmann::json;
using KeyDirection = Fast::Renderer3ds::PicaEnvironmentKeyDirection;
} // namespace

SceneEnvironmentProfile BaseSceneEnvironmentProfile() {
  // Captured 2026-10-06 from the Kokiri play configuration's title intro
  // profile (scene-006B.environment.json).
  SceneEnvironmentProfile base;
  base.Enabled = true;
  base.PreserveSceneBrightness = false;
  base.NativeDirectStrength = 0.0F;
  base.NativeLocalStrength = 0.0F;
  base.NativeAmbientStrength = 0.6F;
  base.AmbientColor = {1.0F, 1.0F, 1.0F};
  base.AmbientIntensity = 0.0F;
  base.FogStrength = 1.0F;
  base.Lights = {{{45.0F, 45.0F, {1.0F, 1.0F, 1.0F}, 1.0F},
                  {-135.0F, 30.0F, {1.0F, 1.0F, 1.0F}, 0.0F}}};
  base.KeyDirection = KeyDirection::SceneMain;
  base.KeyAzimuthOffset = 0.0F;
  base.KeyElevationOffset = 0.0F;
  base.PointLights = {};
  return base;
}

SceneEnvironmentRuntime::SceneEnvironmentRuntime(
    std::filesystem::path directory, Publisher publish)
    : mDirectory(std::move(directory)), mPublish(std::move(publish)) {}

void SceneEnvironmentRuntime::ObserveScene(std::optional<uint16_t> scene,
                                           bool night,
                                           std::optional<ScenePlayerPlacement> player) {
  std::scoped_lock lock(mMutex);
  mState.Player = scene ? player : std::nullopt;
  if (mState.SceneId == scene && mState.Night == night)
    return;
  // The time changed: only a scene with a night profile changes profile.
  if (mState.SceneId == scene && scene &&
      (night && mState.NightProfileExists) == mState.EditingNight) {
    mState.Night = night;
    return;
  }
  SelectLocked(scene, night);
  mState.Player = scene ? player : std::nullopt;
}
std::filesystem::path
SceneEnvironmentRuntime::ProfilePathLocked(bool night) const {
  char name[48];
  std::snprintf(name, sizeof(name),
                night ? "scene-%04X.night.environment.json"
                      : "scene-%04X.environment.json",
                unsigned(mState.SceneId.value_or(0)));
  return mDirectory / name;
}
void SceneEnvironmentRuntime::SelectLocked(std::optional<uint16_t> scene,
                                           bool night) {
  mState = {};
  mSaved = {};
  mState.SceneId = scene;
  mState.Night = night;
  mState.Revision = ++mRevision;
  if (scene) {
    std::error_code error;
    mState.NightProfileExists =
        std::filesystem::exists(ProfilePathLocked(true), error);
    mState.EditingNight = night && mState.NightProfileExists;
    mState.Path = ProfilePathLocked(mState.EditingNight);
    LoadSelectionLocked(mState.EditingNight, mState.NightProfileExists);
  }
  AcknowledgeFilesLocked();
  PublishLocked();
#if defined(TRIAEVUM_DEVELOPER_TOOLS)
  // Which profile a scene took, for agents reading the log.
  if (scene)
    std::fprintf(stderr, "TRIAEVUM_ENVIRONMENT_SELECT scene=0x%04X night=%d file=%s enabled=%d placed=%zu error=%s\n",
                 unsigned(*scene), mState.EditingNight ? 1 : 0, mState.FileExists ? mState.Path.string().c_str() : "-",
                 mState.Profile.Enabled ? 1 : 0, mState.Profile.PlacedLights.size(),
                 mState.Error.empty() ? "-" : mState.Error.c_str());
#endif
}
void SceneEnvironmentRuntime::EditLightGizmo(uint32_t selectedLight, bool enabled) {
  std::scoped_lock lock(mMutex);
  mGizmoSelected = selectedLight;
  mGizmoEnabled = enabled;
  mGizmoEdited = std::chrono::steady_clock::now();
}

void SceneEnvironmentRuntime::ShowLightGizmos(bool shown) {
  std::scoped_lock lock(mMutex);
  mGizmoEnabled = shown;
}

void SceneEnvironmentRuntime::PublishLightGizmo(const std::optional<std::array<float, 3>> &eye,
                                                bool forced) {
  std::optional<Fast::Oot3d::WorldDebugLineSet> lines;
  std::scoped_lock lock(mMutex);
  const bool editing =
      std::chrono::steady_clock::now() - mGizmoEdited < std::chrono::milliseconds(500);
  if (mGizmoEnabled && (editing || forced) && mState.SceneId && eye &&
      !mState.Profile.PlacedLights.empty())
    lines = BuildPlacedLightGizmo(mState.Profile.PlacedLights, mGizmoSelected, *eye);
  if (lines) {
    Fast::Oot3d::WorldDebugLineRuntime::Instance().Publish(std::move(*lines));
    mGizmoPublished = true;
  } else if (mGizmoPublished) {
    Fast::Oot3d::WorldDebugLineRuntime::Instance().Clear();
    mGizmoPublished = false;
  }
}

void SceneEnvironmentRuntime::SetHotReload(bool enabled) {
#if defined(TRIAEVUM_DEVELOPER_TOOLS)
  std::scoped_lock lock(mMutex);
  mHotReload = enabled;
  AcknowledgeFilesLocked();
#else
  (void)enabled;
#endif
}

std::string SceneEnvironmentRuntime::FileStampLocked(bool night) const {
  if (!mState.SceneId)
    return "-";
  std::error_code error;
  const auto path = ProfilePathLocked(night);
  const auto size = std::filesystem::file_size(path, error);
  if (error)
    return "-";
  const auto time = std::filesystem::last_write_time(path, error);
  return std::to_string(size) + ":" + std::to_string(error ? 0 : time.time_since_epoch().count());
}

void SceneEnvironmentRuntime::AcknowledgeFilesLocked() {
  mFileStamps = {FileStampLocked(false), FileStampLocked(true)};
  mPendingStamps = mFileStamps;
  mReportedStamps = {};
}

void SceneEnvironmentRuntime::PollProfileFiles() { PollProfileFiles(std::chrono::steady_clock::now()); }

void SceneEnvironmentRuntime::PollProfileFiles(std::chrono::steady_clock::time_point now) {
#if defined(TRIAEVUM_DEVELOPER_TOOLS)
  std::scoped_lock lock(mMutex);
  if (!mHotReload || !mState.SceneId || now - mLastFilePoll < std::chrono::milliseconds(250))
    return;
  mLastFilePoll = now;
  const std::array<std::string, 2> stamps{FileStampLocked(false), FileStampLocked(true)};
  if (stamps == mFileStamps) {
    mPendingStamps = stamps;
    return;
  }
  // Act once two looks agree: a file still being written changes between them.
  if (stamps != mPendingStamps) {
    mPendingStamps = stamps;
    return;
  }
  const bool nightExists = stamps[1] != "-";
  const bool editingNight = mState.Night && nightExists;
  const auto path = ProfilePathLocked(editingNight);
  const auto report = [&](const char *result, const std::string &detail) {
    std::fprintf(stderr, "TRIAEVUM_ENVIRONMENT_RELOAD scene=0x%04X time=%s result=%s file=%s%s%s\n",
                 unsigned(*mState.SceneId), editingNight ? "night" : "day", result,
                 path.filename().string().c_str(), detail.empty() ? "" : " detail=", detail.c_str());
  };
  if (mState.Dirty) {
    // Not handled: applied once the edits here are saved or reloaded.
    if (mReportedStamps != stamps) {
      mReportedStamps = stamps;
      mState.Error = "The profile changed on disk; the unsaved edits here are kept (save or reload).";
      report("kept-unsaved", "");
    }
    return;
  }
  mFileStamps = stamps;
  if (stamps[editingNight ? 1U : 0U] == "-") {
    report("kept-missing", "");
    return;
  }
  auto file = LoadSceneEnvironmentFile(path, *mState.SceneId, editingNight);
  // The same content in the same file (a file touched, not changed).
  if (editingNight == mState.EditingNight && file.Exists && !file.Text.empty() &&
      std::hash<std::string>{}(file.Text) == mAppliedHash) {
    mState.NightProfileExists = nightExists;
    return;
  }
  if (!file.Document.Profile) {
    const auto detail = DescribeSceneEnvironmentErrors(file.Document.Errors);
    mState.Error = detail;
    report("kept-invalid", detail);
    return;
  }
  const auto hash = std::hash<std::string>{}(file.Text);
  mState.NightProfileExists = nightExists;
  mState.EditingNight = editingNight;
  mState.Path = path;
  mSaved = mState.Profile = std::move(*file.Document.Profile);
  mState.FileExists = true;
  mState.Dirty = false;
  mState.Error.clear();
  mState.Revision = ++mRevision;
  mAppliedHash = hash;
  PublishLocked();
  report("applied", "");
#else
  (void)now;
#endif
}

SceneEnvironmentSnapshot SceneEnvironmentRuntime::Snapshot() const {
  std::scoped_lock lock(mMutex);
  return mState;
}
bool SceneEnvironmentRuntime::Preview(SceneEnvironmentProfile profile, std::optional<uint64_t> basedOnRevision) {
  std::scoped_lock lock(mMutex);
  if (!mState.SceneId)
    return false;
  if (basedOnRevision && *basedOnRevision != mState.Revision) {
    mState.Error = "The profile was replaced (scene change or a file reloaded) while editing; that edit was not applied.";
    return false;
  }
  // Validate callers as well as files, without publishing partial state.
  if (const auto errors = ValidateSceneEnvironment(profile); !errors.empty()) {
    mState.Error = DescribeSceneEnvironmentErrors(errors);
    return false;
  }
  mState.Profile = profile;
  mState.Dirty = mState.Profile != mSaved;
  PublishLocked();
  return true;
}
bool SceneEnvironmentRuntime::Save() {
  std::scoped_lock lock(mMutex);
  if (!mState.SceneId)
    return false;
  std::error_code error;
  std::filesystem::create_directories(mDirectory, error);
  if (error) {
    mState.Error = error.message();
    return false;
  }
  const auto text =
      FormatSceneEnvironment(EncodeSceneEnvironment(mState.Profile, *mState.SceneId, mState.EditingNight));
  const auto saved = Ship::WriteConfigFileAtomically(mState.Path, text);
  mState.Error = saved.Error;
  if (saved.Success) {
    mSaved = mState.Profile;
    mState.Dirty = false;
    mState.FileExists = true;
    // Its own write is not a change to pick up; the other file's state is
    // left as it was seen.
    const size_t index = mState.EditingNight ? 1U : 0U;
    mFileStamps[index] = mPendingStamps[index] = FileStampLocked(mState.EditingNight);
    mAppliedHash = std::hash<std::string>{}(text);
  }
  return saved.Success;
}
bool SceneEnvironmentRuntime::CreateNightProfile() {
  std::scoped_lock lock(mMutex);
  if (!mState.SceneId)
    return false;
  std::error_code error;
  std::filesystem::create_directories(mDirectory, error);
  if (error) {
    mState.Error = error.message();
    return false;
  }
  const auto path = ProfilePathLocked(true);
  const auto text = FormatSceneEnvironment(EncodeSceneEnvironment(mState.Profile, *mState.SceneId, true));
  const auto saved = Ship::WriteConfigFileAtomically(path, text);
  mState.Error = saved.Error;
  if (!saved.Success)
    return false;
  mFileStamps[1] = mPendingStamps[1] = FileStampLocked(true);
  mState.NightProfileExists = true;
  if (mState.Night && !mState.EditingNight) {
    // The values shown become the night profile in use; the scene's own
    // profile keeps what it last saved.
    mState.EditingNight = true;
    mState.Path = path;
    mSaved = mState.Profile;
    mState.Dirty = false;
    mState.FileExists = true;
    mAppliedHash = std::hash<std::string>{}(text);
    PublishLocked();
  }
  return true;
}
bool SceneEnvironmentRuntime::RemoveNightProfile() {
  std::scoped_lock lock(mMutex);
  if (!mState.SceneId || !mState.NightProfileExists)
    return false;
  std::error_code error;
  std::filesystem::remove(ProfilePathLocked(true), error);
  if (error) {
    mState.Error = error.message();
    return false;
  }
  mFileStamps[1] = mPendingStamps[1] = "-";
  mState.NightProfileExists = false;
  if (mState.EditingNight) {
    mState.EditingNight = false;
    mState.Path = ProfilePathLocked(false);
    if (LoadSelectionLocked(false, false))
      PublishLocked();
  }
  return true;
}
bool SceneEnvironmentRuntime::Reload() {
  std::scoped_lock lock(mMutex);
  if (!mState.SceneId)
    return false;
  // Which file the time of day uses is found again, as the files are now.
  std::error_code error;
  const bool nightExists = std::filesystem::exists(ProfilePathLocked(true), error);
  if (!LoadSelectionLocked(mState.Night && nightExists, nightExists))
    return false;
  AcknowledgeFilesLocked();
  PublishLocked();
  return true;
}
bool SceneEnvironmentRuntime::ReloadLocked() {
  return LoadSelectionLocked(mState.EditingNight, mState.NightProfileExists);
}
bool SceneEnvironmentRuntime::LoadSelectionLocked(bool editingNight, bool nightExists) {
  if (!mState.SceneId)
    return false;
  const auto path = ProfilePathLocked(editingNight);
  auto file = LoadSceneEnvironmentFile(path, *mState.SceneId, editingNight);
  // A scene without its own profile takes the base environment.
  auto candidate = BaseSceneEnvironmentProfile();
  if (file.Exists) {
    if (!file.Document.Profile) {
      mState.Error = DescribeSceneEnvironmentErrors(file.Document.Errors);
      return false;
    }
    candidate = std::move(*file.Document.Profile);
  }
  // The selection and its profile change together.
  mState.NightProfileExists = nightExists;
  mState.EditingNight = editingNight;
  mState.Path = path;
  mSaved = mState.Profile = std::move(candidate);
  mState.FileExists = file.Exists;
  mState.Dirty = false;
  mState.Error.clear();
  mState.Revision = ++mRevision;
  mAppliedHash = std::hash<std::string>{}(file.Text);
  return true;
}
void SceneEnvironmentRuntime::PublishLocked() {
  mPublish(mState.SceneId
               ? ResolveSceneEnvironment(mState.Profile, *mState.SceneId, mState.EditingNight)
               : Fast::Renderer3ds::PicaEnvironmentLighting{});
}
} // namespace Oot3dNativeGame
