#include "oot3d_scene_environment.h"
#include "oot3d_scene_environment_codec.h"
#include "oot3d_light_gizmo.h"
#include "ship/config/ConfigPersistence.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace Oot3dNativeGame {
namespace {
using KeyDirection = Fast::Renderer3ds::PicaEnvironmentKeyDirection;
Fast::Renderer3ds::PicaEnvironmentLighting HistoryInputs(Fast::Renderer3ds::PicaEnvironmentLighting value) {
  if (!value.Enabled) {
    Fast::Renderer3ds::PicaEnvironmentLighting inactive;
    inactive.ScopeIdentity = value.ScopeIdentity;
    inactive.PointLights = value.PointLights;
    inactive.CasterFaces = value.CasterFaces;
    value = inactive;
  }
  if (!value.Rim.Enabled) value.Rim = {};
  value.ContinuityEpoch = 0;
  return value;
}
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
  // Modern actors get an independent warm-white, camera-relative top rim.
  base.Rim.Enabled = true;
  // Keep interior roofs/walls two-sided while the first outer shell admits
  // the key light once its room cache is ready.
  base.CasterFaces = Fast::Renderer3ds::ShadowCasterFaces::InteriorShells;
  return base;
}

SceneEnvironmentRuntime::SceneEnvironmentRuntime(std::filesystem::path directory, Publisher publish,
                                                 std::filesystem::path defaults)
    : mDirectory(std::move(directory)), mDefaults(std::move(defaults)), mPublish(std::move(publish)) {
  if (const char* value = std::getenv("TRIAEVUM_ENVIRONMENT_NIGHT_WEIGHT")) {
    char* end = nullptr;
    const float weight = std::strtof(value, &end);
    if (end != value && *end == '\0' && std::isfinite(weight) && weight >= 0 && weight <= 1)
      mForcedNightWeight = weight;
    else std::fprintf(stderr, "TRIAEVUM_ENVIRONMENT_SELECT invalid TRIAEVUM_ENVIRONMENT_NIGHT_WEIGHT (expected 0..1)\n");
  }
}

void SceneEnvironmentRuntime::ObserveScene(std::optional<uint16_t> scene, SceneEnvironmentTime time,
                                           std::optional<ScenePlayerPlacement> player) {
  std::scoped_lock lock(mMutex);
  ++mObservations;
  const bool changedScene = scene != mState.SceneId ||
      (scene && time.Owner && mTime.Owner && time.Owner != mTime.Owner) ||
      (scene && time.AuthoredScene && mTime.AuthoredScene && time.AuthoredScene != mTime.AuthoredScene);
  if (!changedScene && !time.AuthoredScene) time.AuthoredScene = mTime.AuthoredScene;
  // Values always come straight from the new native sample, including cuts.
  // The 8-degree threshold is the existing shadow scheduler's cut threshold,
  // not a smoothing duration or a change to the native dawn/dusk curve.
  const int clockStep = std::bit_cast<int16_t>(uint16_t(time.Clock - mTime.Clock));
  if (changedScene || time.Owner != mTime.Owner || time.TimeDriven != mTime.TimeDriven ||
      std::abs(clockStep) > 1456) ++mContinuityEpoch;
  mTime = time;
  if (changedScene) SelectLocked(scene);
  else if (scene && !mState.Group && time.Group) {
    // Headers may become readable after the first camera sample. Once known,
    // the first installed room defines this visit, even in mixed dungeons.
    mState.Group = time.Group;
    for (bool night : {false, true}) {
      auto& draft = mDrafts[night ? 1 : 0];
      if (draft.Profile == draft.Saved) LoadSlotLocked(night);
    }
    mState.Revision = ++mRevision; ++mContinuityEpoch;
    AcknowledgeFilesLocked();
  }
  if (mState.Group == SceneEnvironmentGroup::FixedTime) time.TimeDriven = false;
  mState.Player = scene ? player : std::nullopt;
  mState.Night = time.Night;
  mState.TimeDriven = time.TimeDriven;
  mState.NativeNightWeight = time.TimeDriven ? NativeEnvironmentNightWeight(time.Clock) : 0;
  if (!time.TimeDriven) {
    if (mState.EditingNight) { mState.EditingNight = false; mState.Revision = ++mRevision; }
    mState.PreviewMode = SceneEnvironmentPreview::Auto;
  }
  SyncSelectionLocked();
  PublishLocked();
}

std::filesystem::path SceneEnvironmentRuntime::ProfilePathLocked(bool night) const {
  char name[48];
  std::snprintf(name, sizeof(name), night ? "scene-%04X.night.environment.json" : "scene-%04X.environment.json",
                unsigned(mState.SceneId.value_or(0)));
  return mDirectory / name;
}
std::filesystem::path SceneEnvironmentRuntime::GroupPathLocked(bool night) const {
  if (!mState.Group) return {};
  return mDirectory / (std::string("group-") + EnvironmentGroupId(*mState.Group) +
      (night ? ".night.environment.json" : ".environment.json"));
}
void SceneEnvironmentRuntime::SelectLocked(std::optional<uint16_t> scene) {
  mState = {};
  mDrafts = {};
  mState.SceneId = scene;
  mState.Group = scene ? mTime.Group : std::nullopt;
  mState.Revision = ++mRevision;
  if (scene) { LoadSlotLocked(false); LoadSlotLocked(true); }
  AcknowledgeFilesLocked();
}
void SceneEnvironmentRuntime::SyncSelectionLocked() {
  mState.NightProfileExists = mDrafts[1].Available;
  mState.NightError = mDrafts[1].Error;
  if (!mState.NightProfileExists) {
    mState.EditingNight = false;
    if (mState.PreviewMode == SceneEnvironmentPreview::Night) mState.PreviewMode = SceneEnvironmentPreview::Auto;
  }
  for (size_t i = 0; i < 2; ++i) {
    mState.DirtyTabs[i] = mDrafts[i].Profile != mDrafts[i].Saved;
    mState.Sources[i] = i == 1 && !mDrafts[1].Available ? SceneEnvironmentSource::Day : mDrafts[i].Source;
    mState.SourcePaths[i] = i == 1 && !mDrafts[1].Available ? mDrafts[0].SourcePath : mDrafts[i].SourcePath;
  }
  const auto& draft = mDrafts[mState.EditingNight ? 1 : 0];
  mState.Profile = draft.Profile;
  mState.FileExists = draft.Exists;
  mState.Error = draft.Error;
  mState.Dirty = mState.DirtyTabs[mState.EditingNight ? 1 : 0];
  mState.Path = mState.SceneId ? ProfilePathLocked(mState.EditingNight) : std::filesystem::path{};
}
SceneEnvironmentSnapshot SceneEnvironmentRuntime::Snapshot() const {
  std::scoped_lock lock(mMutex);
  return mState;
}
bool SceneEnvironmentRuntime::SelectTab(bool night) {
  std::scoped_lock lock(mMutex);
  if (!mState.SceneId || (night && (!mState.TimeDriven || !mDrafts[1].Available))) return false;
  if (mState.EditingNight != night) { mState.EditingNight = night; mState.Revision = ++mRevision; }
  SyncSelectionLocked();
  return true;
}
void SceneEnvironmentRuntime::SetPreviewMode(SceneEnvironmentPreview mode) {
  std::scoped_lock lock(mMutex);
  if (mode > SceneEnvironmentPreview::Night || !mState.TimeDriven ||
      (mode == SceneEnvironmentPreview::Night && !mDrafts[1].Available)) mode = SceneEnvironmentPreview::Auto;
  if (mState.PreviewMode == mode) return;
  mState.PreviewMode = mode;
  ++mContinuityEpoch;
  PublishLocked();
}
void SceneEnvironmentRuntime::EndPreview() { SetPreviewMode(SceneEnvironmentPreview::Auto); }
bool SceneEnvironmentRuntime::SetForcedNightWeight(std::optional<float> weight) {
  if (weight && (!std::isfinite(*weight) || *weight < 0 || *weight > 1)) return false;
  std::scoped_lock lock(mMutex);
  if (weight != mForcedNightWeight) { mForcedNightWeight = weight; ++mContinuityEpoch; PublishLocked(); }
  return true;
}
bool SceneEnvironmentRuntime::Preview(SceneEnvironmentProfile profile, std::optional<uint64_t> basedOnRevision) {
  std::scoped_lock lock(mMutex);
  if (!mState.SceneId) return false;
  auto& draft = mDrafts[mState.EditingNight ? 1 : 0];
  if (basedOnRevision && *basedOnRevision != mState.Revision) {
    draft.Error = "The editor selection changed or the profile was reloaded; that stale edit was not applied.";
    SyncSelectionLocked(); return false;
  }
  if (const auto errors = ValidateSceneEnvironment(profile); !errors.empty()) {
    draft.Error = DescribeSceneEnvironmentErrors(errors); SyncSelectionLocked(); return false;
  }
  draft.Profile = std::move(profile);
  draft.Error.clear();
  SyncSelectionLocked(); PublishLocked(true);
  return true;
}
bool SceneEnvironmentRuntime::Save() {
  std::scoped_lock lock(mMutex);
  if (!mState.SceneId) return false;
  auto& draft = mDrafts[mState.EditingNight ? 1 : 0];
  std::error_code error;
  std::filesystem::create_directories(mDirectory, error);
  if (error) { draft.Error = error.message(); SyncSelectionLocked(); return false; }
  const auto text = FormatSceneEnvironment(EncodeSceneEnvironment(draft.Profile, *mState.SceneId, mState.EditingNight));
  const auto saved = Ship::WriteConfigFileAtomically(ProfilePathLocked(mState.EditingNight), text);
  draft.Error = saved.Error;
  if (saved.Success) {
    draft.Saved = draft.Profile; draft.Exists = draft.Available = true;
    draft.Source = SceneEnvironmentSource::Scene; draft.SourcePath = ProfilePathLocked(mState.EditingNight);
    draft.AppliedHash = std::hash<std::string>{}(text);
    const size_t i = mState.EditingNight ? 1 : 0;
    mFileStamps[i] = mPendingStamps[i] = FileStampLocked(mState.EditingNight);
  }
  SyncSelectionLocked();
  return saved.Success;
}
bool SceneEnvironmentRuntime::CreateNightProfile() {
  std::scoped_lock lock(mMutex);
  if (!mState.SceneId || !mState.TimeDriven || mDrafts[1].Available) return false;
  std::error_code error;
  if (std::filesystem::exists(ProfilePathLocked(true), error) || error) {
    mDrafts[0].Error = "A night file already exists but could not be loaded. Fix that file before creating another.";
    SyncSelectionLocked(); return false;
  }
  std::filesystem::create_directories(mDirectory, error);
  if (error) { mDrafts[0].Error = error.message(); SyncSelectionLocked(); return false; }
  const auto text = FormatSceneEnvironment(EncodeSceneEnvironment(mDrafts[0].Profile, *mState.SceneId, true));
  const auto saved = Ship::WriteConfigFileAtomically(ProfilePathLocked(true), text);
  if (!saved.Success) { mDrafts[0].Error = saved.Error; SyncSelectionLocked(); return false; }
  auto& night = mDrafts[1];
  night = {mDrafts[0].Profile, mDrafts[0].Profile, {}, true, true, std::hash<std::string>{}(text)};
  night.Source = SceneEnvironmentSource::Scene; night.SourcePath = ProfilePathLocked(true);
  mState.EditingNight = true; mState.Revision = ++mRevision;
  mFileStamps[1] = mPendingStamps[1] = FileStampLocked(true);
  ++mContinuityEpoch;
  SyncSelectionLocked(); PublishLocked();
  return true;
}
bool SceneEnvironmentRuntime::CopyDayToNight() {
  std::scoped_lock lock(mMutex);
  if (!mState.TimeDriven || !mDrafts[1].Available) return false;
  mDrafts[1].Profile = mDrafts[0].Profile;
  mDrafts[1].Error.clear();
  mState.EditingNight = true; mState.Revision = ++mRevision;
  ++mContinuityEpoch;
  SyncSelectionLocked(); PublishLocked();
  return true;
}
bool SceneEnvironmentRuntime::RemoveNightProfile() {
  std::scoped_lock lock(mMutex);
  if (!mState.SceneId || !mState.TimeDriven || !mDrafts[1].Available) return false;
  std::error_code error;
  std::filesystem::remove(ProfilePathLocked(true), error);
  if (error) { mDrafts[1].Error = error.message(); SyncSelectionLocked(); return false; }
  mDrafts[1] = {};
  LoadSlotLocked(true);
  mState.EditingNight = false; mState.PreviewMode = SceneEnvironmentPreview::Auto;
  mState.Revision = ++mRevision;
  mFileStamps[1] = mPendingStamps[1] = FileStampLocked(true);
  ++mContinuityEpoch;
  SyncSelectionLocked(); PublishLocked();
  return true;
}
bool SceneEnvironmentRuntime::SaveAsGroupStandard() {
  std::scoped_lock lock(mMutex);
  if (!mState.SceneId || !mState.Group) return false;
  const bool night = mState.EditingNight;
  auto& draft = mDrafts[night ? 1 : 0];
  std::error_code error;
  std::filesystem::create_directories(mDirectory, error);
  if (error) { draft.Error = error.message(); SyncSelectionLocked(); return false; }
  const auto saved = Ship::WriteConfigFileAtomically(GroupPathLocked(night),
      FormatSceneEnvironment(EncodeGroupEnvironment(draft.Profile, *mState.Group, night)));
  if (!saved.Success) { draft.Error = saved.Error; SyncSelectionLocked(); return false; }
  draft.Error.clear();
  // Committing a fallback draft to the standard saves that draft. Own scene
  // files retain precedence, and unrelated unsaved drafts are never discarded.
  if (!draft.Exists) LoadSlotLocked(night);
  mState.Revision = ++mRevision; ++mContinuityEpoch;
  mFileStamps[night ? 1 : 0] = mPendingStamps[night ? 1 : 0] = FileStampLocked(night);
  SyncSelectionLocked(); PublishLocked();
  return true;
}
bool SceneEnvironmentRuntime::UseGroupStandard() {
  std::scoped_lock lock(mMutex);
  if (!mState.SceneId || !mState.Group) return false;
  const bool night = mState.EditingNight;
  auto& draft = mDrafts[night ? 1 : 0];
  std::error_code error;
  std::filesystem::remove(ProfilePathLocked(night), error);
  if (error) { draft.Error = error.message(); SyncSelectionLocked(); return false; }
  const bool loaded = LoadSlotLocked(night);
  mState.Revision = ++mRevision; ++mContinuityEpoch;
  mFileStamps[night ? 1 : 0] = mPendingStamps[night ? 1 : 0] = FileStampLocked(night);
  SyncSelectionLocked(); PublishLocked();
  return loaded;
}
bool SceneEnvironmentRuntime::LoadSlotLocked(bool night) {
  if (!mState.SceneId) return false;
  auto& draft = mDrafts[night ? 1 : 0];
  auto path = ProfilePathLocked(night);
  auto file = LoadSceneEnvironmentFile(path, *mState.SceneId, night);
  const bool own = file.Exists;
  SceneEnvironmentSource source = own ? SceneEnvironmentSource::Scene : SceneEnvironmentSource::Base;
  // The first existing file of: the release's scene default, the user's
  // group standard, the release's group default.
  const auto fallBackTo = [&](const std::filesystem::path& candidate, SceneEnvironmentSource kind, bool group) {
    if (file.Exists || candidate.empty()) return;
    auto next = group ? LoadSceneEnvironmentFile(candidate, std::nullopt, night, mState.Group)
                      : LoadSceneEnvironmentFile(candidate, *mState.SceneId, night);
    if (!next.Exists) return;
    path = candidate;
    file = std::move(next);
    source = kind;
  };
  const auto releaseDefault = [this](const std::filesystem::path& user) {
    return mDefaults.empty() || user.empty() ? std::filesystem::path{} : mDefaults / user.filename();
  };
  fallBackTo(releaseDefault(ProfilePathLocked(night)), SceneEnvironmentSource::DefaultScene, false);
  if (mState.Group) {
    fallBackTo(GroupPathLocked(night), SceneEnvironmentSource::Group, true);
    fallBackTo(releaseDefault(GroupPathLocked(night)), SceneEnvironmentSource::DefaultGroup, true);
  }
  if (file.Exists && !file.Document.Profile) {
    draft.Error = path.filename().string() + ": " + DescribeSceneEnvironmentErrors(file.Document.Errors);
    draft.Exists = own;
    // Keep the last accepted value on a bad reload. First-entry failure stays
    // disabled, and cannot silently fall through an invalid higher priority file.
    return false;
  }
  auto profile = file.Document.Profile ? *file.Document.Profile
      : night ? mDrafts[0].Profile : BaseSceneEnvironmentProfile();
  draft = {profile, profile, {}, own, !night || file.Exists, std::hash<std::string>{}(file.Text),
           night && !file.Exists ? SceneEnvironmentSource::Day : source,
           file.Exists ? path : std::filesystem::path{}};
  return true;
}
bool SceneEnvironmentRuntime::Reload() {
  std::scoped_lock lock(mMutex);
  if (!mState.SceneId) return false;
  const bool night = mState.EditingNight;
  const bool loaded = LoadSlotLocked(night);
  if (loaded) {
    mState.Revision = ++mRevision; ++mContinuityEpoch;
    const size_t i = night ? 1 : 0;
    mFileStamps[i] = mPendingStamps[i] = FileStampLocked(night);
  }
  SyncSelectionLocked();
  if (loaded) PublishLocked();
  return loaded;
}
void SceneEnvironmentRuntime::SetHotReload(bool enabled) {
#if defined(TRIAEVUM_DEVELOPER_TOOLS)
  std::scoped_lock lock(mMutex);
  mHotReload = enabled; AcknowledgeFilesLocked();
#else
  (void)enabled;
#endif
}
std::string SceneEnvironmentRuntime::FileStampLocked(bool night) const {
  if (!mState.SceneId) return "-";
  const auto stamp = [](const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
      // An unreadable file is not an absent one; retry/diagnose through loader.
      const bool exists = std::filesystem::exists(path, error);
      return exists || error ? std::string("unreadable") : std::string("-");
    }
    const auto time = std::filesystem::last_write_time(path, error);
    return std::to_string(size) + ":" + std::to_string(error ? 0LL : static_cast<long long>(time.time_since_epoch().count()));
  };
  auto own = stamp(ProfilePathLocked(night));
  if (own != "-") return "scene:" + own;
  if (mState.Group) return "group:" + stamp(GroupPathLocked(night));
  return "-";
}
void SceneEnvironmentRuntime::AcknowledgeFilesLocked() {
  mFileStamps = {FileStampLocked(false), FileStampLocked(true)};
  mPendingStamps = mFileStamps; mReportedStamps = {};
}
void SceneEnvironmentRuntime::PollProfileFiles() { PollProfileFiles(std::chrono::steady_clock::now()); }
void SceneEnvironmentRuntime::PollProfileFiles(std::chrono::steady_clock::time_point now) {
#if defined(TRIAEVUM_DEVELOPER_TOOLS)
  std::scoped_lock lock(mMutex);
  if (!mHotReload || !mState.SceneId || now - mLastFilePoll < std::chrono::milliseconds(250)) return;
  mLastFilePoll = now;
  bool changed = false;
  for (size_t i = 0; i < 2; ++i) {
    const bool night = i == 1;
    if (night && !mState.TimeDriven) continue;
    const auto stamp = FileStampLocked(night);
    if (stamp == mFileStamps[i]) { mPendingStamps[i] = stamp; continue; }
    if (stamp != mPendingStamps[i]) { mPendingStamps[i] = stamp; continue; }
    auto& draft = mDrafts[i];
    const auto report = [&](const char* result) {
      const auto path = stamp.starts_with("group:") ? GroupPathLocked(night) : ProfilePathLocked(night);
      std::fprintf(stderr, "TRIAEVUM_ENVIRONMENT_RELOAD scene=0x%04X time=%s result=%s file=%s error=%s\n",
          unsigned(*mState.SceneId), night ? "night" : "day", result, path.filename().string().c_str(),
          draft.Error.empty() ? "-" : draft.Error.c_str());
    };
    if (draft.Profile != draft.Saved) {
      if (mReportedStamps[i] != stamp) {
        mReportedStamps[i] = stamp;
        draft.Error = "This profile changed on disk; unsaved edits are kept. Save or revert this tab.";
        report("kept-unsaved");
      }
      continue;
    }
    mFileStamps[i] = stamp;
    if (stamp == "-" && !night && !mState.Group) { report("kept-missing"); continue; }
    const auto previous = draft;
    if (LoadSlotLocked(night)) {
      if (draft.Profile == previous.Profile && draft.Source == previous.Source &&
          draft.Available == previous.Available && draft.Exists == previous.Exists) continue;
      changed = true;
      if (night == mState.EditingNight || (night && !draft.Available)) mState.Revision = ++mRevision;
      report("applied");
    } else report("kept-invalid");
  }
  SyncSelectionLocked();
  if (changed) { ++mContinuityEpoch; PublishLocked(); }
#else
  (void)now;
#endif
}
void SceneEnvironmentRuntime::PublishLocked(bool edited) {
  float weight = 0;
  const bool blend = mState.SceneId && mState.TimeDriven && mDrafts[1].Available;
  mState.ForcedWeight = blend && mForcedNightWeight.has_value() && mState.PreviewMode == SceneEnvironmentPreview::Auto;
  if (blend) {
    weight = mState.PreviewMode == SceneEnvironmentPreview::Day ? 0
        : mState.PreviewMode == SceneEnvironmentPreview::Night ? 1
        : mForcedNightWeight.value_or(mState.NativeNightWeight);
  }
  mState.AppliedNightWeight = weight;
  Fast::Renderer3ds::PicaEnvironmentLighting resolved;
  if (mState.SceneId) {
    const auto profile = blend ? BlendSceneEnvironmentProfiles(mDrafts[0].Profile, mDrafts[1].Profile, weight)
                               : mDrafts[0].Profile;
    // Day/night are one continuous scope. Authoring identities remain in the
    // files; the blend's placed-light identities match by index.
    resolved = ResolveSceneEnvironment(profile, *mState.SceneId, false);
    if (edited && mPublished && HistoryInputs(*mPublished) != HistoryInputs(resolved)) ++mContinuityEpoch;
    resolved.ContinuityEpoch = mContinuityEpoch;
  }
#if defined(TRIAEVUM_DEVELOPER_TOOLS)
  // Full weight on each publication; unchanged native samples are silent.
  // A short record is sufficient to reproduce native/forced/preview selection.
  if (mState.SceneId && (!mPublished || *mPublished != resolved || mObservations % 120 == 0))
    std::fprintf(stderr, "TRIAEVUM_ENVIRONMENT_SELECT scene=0x%04X clock=%04X timeDriven=%d nativeNight=%d nativeWeight=%.6f weight=%.6f preview=%u forced=%d nightProfile=%d edit=%s dirty=%d,%d epoch=%llu group=%s daySource=%u nightSource=%u\n",
        unsigned(*mState.SceneId), mTime.Clock, mState.TimeDriven, mState.Night, mState.NativeNightWeight, weight,
        unsigned(mState.PreviewMode), mState.ForcedWeight, mDrafts[1].Available,
        mState.EditingNight ? "night" : "day", mState.DirtyTabs[0], mState.DirtyTabs[1],
        static_cast<unsigned long long>(mContinuityEpoch),
        mState.Group ? EnvironmentGroupId(*mState.Group) : "unknown",
        unsigned(mState.Sources[0]), unsigned(mState.Sources[1]));
#endif
  if (!mPublished || *mPublished != resolved) { mPublished = resolved; mPublish(resolved); }
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


} // namespace Oot3dNativeGame
