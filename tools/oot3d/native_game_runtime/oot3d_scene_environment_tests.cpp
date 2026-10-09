// Scene environment runtime with developer hot reload: files changed on disk
// while the game runs (built with TRIAEVUM_DEVELOPER_TOOLS).
#include "oot3d_scene_environment.h"
#include "oot3d_scene_environment_codec.h"
#include "triaevum_title_adapter.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {
using namespace Oot3dNativeGame;
int gChecks = 0;

void Check(bool condition, const char *what) {
  ++gChecks;
  if (!condition) throw std::runtime_error(what);
}

void Write(const std::filesystem::path &path, const std::string &text) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << text;
}

std::string Profile(float ambient, bool night) {
  auto profile = BaseSceneEnvironmentProfile();
  profile.AmbientIntensity = ambient;
  return FormatSceneEnvironment(EncodeSceneEnvironment(profile, 0x55, night));
}

struct Fixture {
  std::filesystem::path Directory;
  std::filesystem::path Day, Night;
  SceneEnvironmentRuntime Runtime;
  std::chrono::steady_clock::time_point Now = std::chrono::steady_clock::now();
  int Publications = 0;

  explicit Fixture(const std::filesystem::path &directory)
      : Directory(directory), Day(directory / "scene-0055.environment.json"),
        Night(directory / "scene-0055.night.environment.json"),
        Runtime(directory, [this](const Fast::Renderer3ds::PicaEnvironmentLighting &) { ++Publications; }) {}

  // Two looks 300 ms apart: a change is acted on once they agree.
  void Settle() {
    for (int i = 0; i < 2; ++i) {
      Now += std::chrono::milliseconds(300);
      Runtime.PollProfileFiles(Now);
    }
  }
  float Ambient() const { return Runtime.Snapshot().Profile.AmbientIntensity; }
};
} // namespace

int main() try {
  const auto directory = std::filesystem::temp_directory_path() / "triaevum_environment_tests";
  std::filesystem::remove_all(directory);
  std::filesystem::create_directories(directory);
  Fixture f(directory);
  Write(f.Day, Profile(0.3F, false));
  f.Runtime.ObserveScene(0x55, false);
  f.Runtime.SetHotReload(true);
  Check(f.Ambient() == 0.3F, "the scene's profile was not loaded");

  // A half-written file is never applied; a whole one is.
  auto revision = f.Runtime.Snapshot().Revision;
  Write(f.Day, "{\"schema\":");
  f.Settle();
  Check(f.Ambient() == 0.3F && !f.Runtime.Snapshot().Error.empty() && f.Runtime.Snapshot().Revision == revision,
        "an invalid file replaced the profile in use");
  Write(f.Day, Profile(0.65F, false));
  f.Settle();
  Check(f.Ambient() == 0.65F && f.Runtime.Snapshot().Error.empty() && f.Runtime.Snapshot().Revision > revision,
        "a valid file was not applied");

  // The same content written again is no change.
  revision = f.Runtime.Snapshot().Revision;
  const int publications = f.Publications;
  Write(f.Day, Profile(0.65F, false));
  f.Settle();
  Check(f.Runtime.Snapshot().Revision == revision && f.Publications == publications,
        "an unchanged file was applied again");

  // One look is not enough: a file still changing is not read.
  Write(f.Day, Profile(0.7F, false));
  f.Now += std::chrono::milliseconds(300);
  f.Runtime.PollProfileFiles(f.Now);
  Check(f.Ambient() == 0.65F, "a change was applied before two looks agreed");
  f.Now += std::chrono::milliseconds(300);
  f.Runtime.PollProfileFiles(f.Now);
  Check(f.Ambient() == 0.7F, "a settled change was not applied");

  // Unsaved edits are kept; an explicit reload takes the file.
  auto edited = f.Runtime.Snapshot().Profile;
  edited.AmbientIntensity = 1.25F;
  Check(f.Runtime.Preview(edited, f.Runtime.Snapshot().Revision) && f.Runtime.Snapshot().Dirty,
        "an edit on the current revision was refused");
  Write(f.Day, Profile(0.45F, false));
  f.Settle();
  Check(f.Ambient() == 1.25F && f.Runtime.Snapshot().Dirty, "a file on disk replaced unsaved edits");
  Check(f.Runtime.Reload() && f.Ambient() == 0.45F && !f.Runtime.Snapshot().Dirty,
        "reload did not take the file that changed during the edit");

  // An edit made on a replaced profile is refused.
  const auto stale = f.Runtime.Snapshot();
  Write(f.Day, Profile(0.555F, false));
  f.Settle();
  edited = stale.Profile;
  edited.AmbientIntensity = 1.5F;
  Check(!f.Runtime.Preview(edited, stale.Revision) && f.Ambient() == 0.555F,
        "an edit based on a replaced profile overwrote it");

  // The game's own save is not picked up as a change.
  edited = f.Runtime.Snapshot().Profile;
  edited.AmbientIntensity = 0.85F;
  Check(f.Runtime.Preview(edited, f.Runtime.Snapshot().Revision) && f.Runtime.Save(), "the edit was not saved");
  revision = f.Runtime.Snapshot().Revision;
  f.Settle();
  Check(f.Runtime.Snapshot().Revision == revision && f.Ambient() == 0.85F, "its own save was reloaded");

  // Night: the day file until a night file appears; an invalid night file
  // changes neither the profile nor which file a save writes.
  f.Runtime.ObserveScene(0x55, true);
  Check(!f.Runtime.Snapshot().EditingNight && f.Ambient() == 0.85F, "night without a night file left the day file");
  Write(f.Night, "{");
  f.Settle();
  Check(!f.Runtime.Snapshot().EditingNight && f.Runtime.Snapshot().Path == f.Day && f.Ambient() == 0.85F,
        "an invalid night file changed the profile or the file saved");
  Write(f.Night, Profile(0.15F, true));
  f.Settle();
  Check(f.Runtime.Snapshot().EditingNight && f.Runtime.Snapshot().Path == f.Night && f.Ambient() == 0.15F,
        "a night file created on disk was not used at night");
  std::filesystem::remove(f.Night);
  f.Settle();
  Check(!f.Runtime.Snapshot().EditingNight && f.Runtime.Snapshot().Path == f.Day && f.Ambient() == 0.85F,
        "removing the night file did not return the night to the day file");

  // A missing file keeps the profile in use.
  f.Runtime.ObserveScene(0x55, false);
  std::filesystem::remove(f.Day);
  f.Settle();
  Check(f.Ambient() == 0.85F, "a missing file replaced the profile in use");

  // The shadow casters' face culling: on without the key, off read, written
  // and handed to the renderer.
  {
    auto document = EncodeSceneEnvironment(BaseSceneEnvironmentProfile(), 0x55, false);
    document.erase("shadowCasterCulling");
    const auto older = DecodeSceneEnvironment(document);
    Check(older.Profile && older.Profile->ShadowCasterCulling, "a profile without the key lost the culling");
    auto profile = BaseSceneEnvironmentProfile();
    profile.ShadowCasterCulling = false;
    const auto decoded = DecodeSceneEnvironment(EncodeSceneEnvironment(profile, 0x55, false));
    Check(decoded.Profile && !decoded.Profile->ShadowCasterCulling, "the culling switch did not round trip");
    Check(!ResolveSceneEnvironment(profile, 0x55, false).ShadowCasterCulling,
          "the renderer did not receive the culling switch");
    document["shadowCasterCulling"] = 1;
    Check(!DecodeSceneEnvironment(document).Errors.empty(), "a non-boolean culling switch was accepted");
  }

  std::filesystem::remove_all(directory);
  // Two titles share a configuration root and a scene number. The legacy
  // file belongs to OoT only; MM3D missing/reload/error paths use its fallback.
  {
    const auto config=directory/"settings.json";
    const auto oot=TitleSceneEnvironmentDirectory(config,TitleAdapter::Oot3dEur);
    const auto mm=TitleSceneEnvironmentDirectory(config,TitleAdapter::Mm3d);
    std::filesystem::create_directories(mm);
    Write(oot/"scene-0055.environment.json",Profile(0.95F,false));
    const auto fallback=NativePreservingSceneEnvironmentProfile();
    Fast::Renderer3ds::PicaEnvironmentLighting published;
    SceneEnvironmentRuntime runtime(mm,[&](const auto& v){published=v;},fallback);
    runtime.ObserveScene(0x55,false,std::nullopt,10);
    Check(runtime.Snapshot().Profile==fallback && !published.Enabled &&
          runtime.Snapshot().Path.parent_path()==mm && !runtime.Snapshot().FileExists,
          "MM3D inherited OoT's file or default");
    Check(fallback.NativeDirectStrength==1 && fallback.NativeLocalStrength==1 &&
          fallback.NativeAmbientStrength==1 && fallback.AmbientIntensity==0 &&
          fallback.Lights[0].Intensity==0 && fallback.Lights[1].Intensity==0,
          "MM3D base contains authored replacement illumination");
    Check(runtime.Reload() && runtime.Snapshot().Profile==fallback,"missing reload used global OoT fallback");
    Check(runtime.Save() && std::filesystem::exists(mm/"scene-0055.environment.json"),
          "MM3D save did not use its namespace");
    auto edited=fallback;edited.Enabled=true;edited.AmbientIntensity=0.7F;
    Check(runtime.Preview(edited),"MM3D profile preview rejected");
    const auto previousRevision=runtime.Snapshot().Revision;
    runtime.ObserveScene(0x55,false,std::nullopt,11);
    Check(runtime.Snapshot().Revision>previousRevision && runtime.Snapshot().Profile==fallback,
          "same-scene restore retained stale preview");
    Check(runtime.Preview(edited) && runtime.CreateNightProfile(),"night profile creation failed");
    runtime.ObserveScene(0x55,true,std::nullopt,11);
    Check(runtime.Snapshot().EditingNight && runtime.Snapshot().Profile==edited,"night profile not selected");
    runtime.ObserveScene(0x55,std::nullopt,std::nullopt,11);
    Check(!runtime.Snapshot().NightKnown && !runtime.Snapshot().EditingNight &&
          runtime.Snapshot().Profile==fallback,"unknown night retained night profile or became known day");
    runtime.ObserveScene(0x55,true,std::nullopt,11);
    Check(runtime.RemoveNightProfile() && runtime.Snapshot().Profile==fallback,
          "night removal did not return to MM3D day profile");
    Write(mm/"scene-0056.environment.json","{invalid");
    runtime.ObserveScene(0x56,false,std::nullopt,12);
    Check(runtime.Snapshot().Profile==fallback && !published.Enabled && !runtime.Snapshot().Error.empty(),
          "invalid new-scene file leaked old or OoT profile");
    runtime.ObserveScene(std::nullopt,std::nullopt);
    Check(!runtime.Snapshot().SceneId && !runtime.Snapshot().Player && !published.Enabled,
          "lost context kept active environment");
    SceneEnvironmentRuntime legacy(oot,[](const auto&){});
    legacy.ObserveScene(0x55);
    Check(legacy.Snapshot().Profile.AmbientIntensity==0.95F,"OoT legacy file stopped working");
    legacy.ObserveScene(0x57);
    Check(legacy.Snapshot().Profile==BaseSceneEnvironmentProfile(),"OoT default changed");
    std::filesystem::remove_all(directory);
  }
  std::printf("scene environment tests passed: %d checks\n", gChecks);
  return 0;
} catch (const std::exception &error) {
  std::fprintf(stderr, "scene environment tests FAILED: %s\n", error.what());
  return 1;
}
