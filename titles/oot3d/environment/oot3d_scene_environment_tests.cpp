// Scene environment runtime with developer hot reload: files changed on disk
// while the game runs (built with TRIAEVUM_DEVELOPER_TOOLS).
#include "oot3d_scene_environment.h"
#include "oot3d_scene_environment_codec.h"
#include "triaevum_title_adapter.h"
#include <chrono>
#include <cmath>
#include <unordered_map>
#include "fast/renderer3ds/virtual_shadow_keyframes.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
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
  file.close();
  if (!file) throw std::runtime_error("fixture write failed: " + path.string());
  // Settle advances the poll clock, not the filesystem clock. Rapid equal-size
  // writes can otherwise retain the same timestamp and correctly look unchanged
  // to the production watcher. Model distinct edits without sleeps, including
  // filesystems with coarse (two-second) timestamp resolution.
  static auto stamp = std::filesystem::file_time_type::clock::now();
  stamp += std::chrono::seconds(2);
  std::filesystem::last_write_time(path, stamp);
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
  Fast::Renderer3ds::PicaEnvironmentLighting Applied;

  explicit Fixture(const std::filesystem::path &directory)
      : Directory(directory), Day(directory / "scene-0055.environment.json"),
        Night(directory / "scene-0055.night.environment.json"),
        Runtime(directory, [this](const Fast::Renderer3ds::PicaEnvironmentLighting &value) { ++Publications; Applied = value; }) {}

  // Two looks 300 ms apart: a change is acted on once they agree.
  void Settle() {
    for (int i = 0; i < 2; ++i) {
      Now += std::chrono::milliseconds(300);
      Runtime.PollProfileFiles(Now);
    }
  }
  float Ambient() const { return Runtime.Snapshot().Profile.AmbientIntensity; }
};

void Near(float actual, float expected, const char* what) { Check(std::abs(actual - expected) < 2e-6F, what); }
void CheckNativeTime() {
  // Independent fixture: the audited native table, decoded from code.bin,
  // includes all weather modes. Test every clock tick against its component
  // lerp rather than duplicating the implementation's four breakpoints.
  auto path = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() /
      "tools/oot3d/native_demo_host/lighting_tables/oot3d_pica_lighting_semantics.json";
  std::ifstream file(path);
  nlohmann::json document; file >> document;
  for (const auto& mode : document["runtime_environment_light_settings_consumer"]["transition_modes"]) {
    for (unsigned clock = 0; clock < 65536; ++clock) {
      bool found = false;
      for (const auto& e : mode["entries"]) {
        const unsigned start = e["start_angle"], end = e["end_angle"];
        if (clock < start || (clock >= end && end != 65535)) continue;
        const float t = 1.0F - float(end - clock) / float(end - start);
        const float a = (e["from_light_setting_index"].get<unsigned>() & 3) == 3 ? 1.0F : 0.0F;
        const float b = (e["to_light_setting_index"].get<unsigned>() & 3) == 3 ? 1.0F : 0.0F;
        Near(NativeEnvironmentNightWeight(uint16_t(clock)), std::lerp(a, b, t), "native curve mismatch");
        found = true; break;
      }
      Check(found, "native table has a clock hole");
    }
  }
  Near(NativeEnvironmentNightWeight(0x3556), 0.5F, "dawn midpoint");
  Near(NativeEnvironmentNightWeight(0), NativeEnvironmentNightWeight(65535), "midnight wrap discontinuity");
  struct Bus {
    std::unordered_map<uint32_t, uint32_t> Values;
    bool Read8(uint32_t at, uint8_t* out) { if (!Values.contains(at)) return false; *out = uint8_t(Values.at(at)); return true; }
    bool Read16(uint32_t at, uint16_t* out) { if (!Values.contains(at)) return false; *out = uint16_t(Values.at(at)); return true; }
    bool Read32(uint32_t at, uint32_t* out) { if (!Values.contains(at)) return false; *out = Values.at(at); return true; }
  } bus;
  constexpr uint32_t play = 0x10000000;
  bus.Values = {{play + 0x31B0, 0}, {play + 0x3237, 0xFF}, {0x588F00, 0x3556}, {0x587968, 0}, {0x587964, 0x8000}};
  auto sample = CaptureSceneEnvironmentTime(play, bus);
  Check(sample && sample->TimeDriven && sample->Clock == 0x3556 && !sample->Night && sample->Owner == play,
        "adapter did not use effective clock independent of save clock/night flag");
  bus.Values[play + 0x31B0] = 1;
  Check(!CaptureSceneEnvironmentTime(play, bus)->TimeDriven, "fixed envLightMode got a clock");
  bus.Values[play + 0x31B0] = 0; bus.Values[play + 0x3237] = 2;
  Check(!CaptureSceneEnvironmentTime(play, bus)->TimeDriven, "setting override got a clock");
  bus.Values.erase(0x588F00);
  Check(!CaptureSceneEnvironmentTime(play, bus) && !CaptureSceneEnvironmentTime(0, bus) &&
            !CaptureSceneEnvironmentTime(0xFFFFFF00, bus), "bad native time read accepted");
}
void CheckBlend() {
  using namespace Fast::Renderer3ds;
  auto day = BaseSceneEnvironmentProfile(), night = day;
  day.Lights[0].Azimuth = day.Rim.Azimuth = day.KeyAzimuthOffset = 170;
  night.Lights[0].Azimuth = night.Rim.Azimuth = night.KeyAzimuthOffset = -170;
  day.AmbientColor = day.Lights[0].Color = day.Rim.Color = {0,0,0};
  night.AmbientColor = night.Lights[0].Color = night.Rim.Color = {1,1,1};
  day.AmbientIntensity = 0; night.AmbientIntensity = 2;
  day.FogStrength = 0; night.FogStrength = 1;
  day.NativeDirectStrength = 0; night.NativeDirectStrength = 2;
  day.NativeLocalStrength = 1; night.NativeLocalStrength = 3;
  day.NativeAmbientStrength = 0; night.NativeAmbientStrength = 1;
  day.Rim.Enabled = false; night.Rim.Enabled = true; night.Rim.Intensity = 2;
  day.Rim.Sharpness = 2; night.Rim.Sharpness = 8;
  day.Rim.Coverage = 0; night.Rim.Coverage = 1;
  day.Rim.Source = PicaRimLightSource::Independent; night.Rim.Source = PicaRimLightSource::SceneLights;
  day.Rim.Scope = PicaRimLightScope::Actors; night.Rim.Scope = PicaRimLightScope::ActorsAndWorld;
  day.CasterFaces = ShadowCasterFaces::Native; night.CasterFaces = ShadowCasterFaces::BothFaces;
  day.Enabled = true; night.Enabled = false;
  day.PointLights[0].Enabled = false; night.PointLights[0].Intensity = 3;
  day.PointLights[0].ColorTint = {0,0,0}; night.PointLights[0].ColorTint = {1,1,1};
  day.PointLights[0].RadiusScale = 1; night.PointLights[0].RadiusScale = 3;
  night.PlacedLights.push_back({.Id=999, .Name="night-only", .Intensity=4});
  const auto mid = BlendSceneEnvironmentProfiles(day, night, 0.5F);
  Near(std::abs(mid.Lights[0].Azimuth), 180, "key long arc");
  Near(std::abs(mid.Rim.Azimuth), 180, "rim long arc");
  Near(std::abs(mid.KeyAzimuthOffset), 180, "offset long arc");
  Near(mid.AmbientColor[0], 0.5F, "ambient not blended in linear light");
  Near(mid.Lights[0].Color[0], 0.5F, "key not blended in linear light");
  Near(mid.Rim.Color[0], 0.5F, "rim not blended in linear light");
  Near(mid.PointLights[0].ColorTint[0], 0.5F, "point tint not blended in linear light");
  Check(mid.AmbientIntensity == 1 && mid.FogStrength == 0.5F && mid.NativeDirectStrength == 1 &&
        mid.NativeLocalStrength == 2 && mid.NativeAmbientStrength == 0.5F, "scalar blend");
  Check(mid.Rim.Enabled && mid.Rim.Intensity == 1 && mid.Rim.Sharpness == 5 && mid.Rim.Coverage == 0.5F,
        "rim enable did not fade");
  Check(mid.PointLights[0].Enabled && mid.PointLights[0].Intensity == 1.5F && mid.PointLights[0].RadiusScale == 2,
        "point enable did not fade");
  Check(mid.CasterFaces == night.CasterFaces && mid.Rim.Source == night.Rim.Source && mid.Rim.Scope == night.Rim.Scope &&
        mid.Enabled == night.Enabled, "discrete midpoint switch");
  Check(BlendSceneEnvironmentProfiles(day,night,0.499F).CasterFaces == day.CasterFaces, "early discrete switch");
  Check(mid.PlacedLights.size() == 1 && mid.PlacedLights[0].Visible && mid.PlacedLights[0].Intensity == 2 &&
        mid.PlacedLights[0].Id == 1, "missing placed light did not fade from zero with index identity");
  Check(!BlendSceneEnvironmentProfiles(day,night,0).PlacedLights[0].Visible, "missing endpoint emits");
  Check(BlendSceneEnvironmentProfiles(day,night,1).PlacedLights[0].Id == 1, "placed identity changed at endpoint");
  Near(BlendSceneEnvironmentProfiles(day,night,0).Lights[0].Azimuth, 170, "day endpoint");
  Near(BlendSceneEnvironmentProfiles(day,night,1).Lights[0].Azimuth, -170, "night endpoint");
  day.PlacedLights = night.PlacedLights;
  day.PlacedLights[0].Position = {0,0,0}; night.PlacedLights[0].Position = {4,8,12};
  day.PlacedLights[0].Rotation = {170,0,170}; night.PlacedLights[0].Rotation = {-170,20,-170};
  const auto placed = BlendSceneEnvironmentProfiles(day,night,0.5F).PlacedLights[0];
  Check(placed.Position == (std::array<float,3>{2,4,6}) && std::abs(placed.Rotation[0]) == 180 &&
        placed.Rotation[1] == 10 && std::abs(placed.Rotation[2]) == 180, "placed transform blend");
}
void CheckProfileRuntime(const std::filesystem::path& directory) {
  std::filesystem::create_directory(directory);
  Fixture f(directory);
  Write(f.Day, Profile(0.2F, false)); Write(f.Night, Profile(1.2F, true));
  f.Runtime.ObserveScene(0x55, {true,0x3556,false,0x1000});
  Near(f.Applied.AmbientIntensity, 0.7F, "runtime did not apply native half weight");
  Check(!f.Runtime.Snapshot().EditingNight && f.Runtime.Snapshot().AppliedNightWeight == 0.5F, "time chose editor");
  const auto scope = f.Applied.ScopeIdentity;
  const auto epoch = f.Applied.ContinuityEpoch;
  f.Runtime.ObserveScene(0x55, {true,0x355A,false,0x1000});
  Check(f.Applied.ContinuityEpoch == epoch && f.Runtime.Snapshot().AppliedNightWeight < 0.5F,
        "ordinary native time movement reset continuity");
  f.Runtime.ObserveScene(0x55, {true,0x3556,false,0x1000});
  auto day = f.Runtime.Snapshot().Profile; day.AmbientIntensity = 0.4F;
  f.Runtime.Preview(day);
  Check(f.Runtime.SelectTab(true), "night unavailable by day");
  auto night = f.Runtime.Snapshot().Profile; night.AmbientIntensity = 1.4F;
  f.Runtime.Preview(night);
  const auto revision = f.Runtime.Snapshot().Revision;
  f.Runtime.ObserveScene(0x55, {true,0,true,0x1000});
  Check(f.Runtime.Snapshot().Revision == revision && f.Runtime.Snapshot().EditingNight &&
        f.Runtime.Snapshot().DirtyTabs == (std::array<bool,2>{true,true}), "time discarded drafts/editor revision");
  Near(f.Applied.AmbientIntensity, 1.4F, "time warp did not snap to night draft");
  Check(f.Applied.ContinuityEpoch > epoch, "clock jump did not cut history");
  Check(f.Applied.ScopeIdentity == scope, "day/night changed shadow scope");
  f.Runtime.SetPreviewMode(SceneEnvironmentPreview::Day);
  Near(f.Applied.AmbientIntensity, 0.4F, "force day preview");
  f.Runtime.EndPreview();
  Near(f.Applied.AmbientIntensity, 1.4F, "end preview did not restore Auto");
  Check(f.Runtime.SetForcedNightWeight(0.25F), "valid forced weight rejected");
  Near(f.Applied.AmbientIntensity, 0.65F, "forced weight not applied");
  Check(!f.Runtime.SetForcedNightWeight(-1) && !f.Runtime.SetForcedNightWeight(std::numeric_limits<float>::quiet_NaN()),
        "invalid forced weight accepted");
  f.Runtime.SetForcedNightWeight(std::nullopt);
  f.Runtime.Save();
  Check(f.Runtime.Snapshot().DirtyTabs[0] && !f.Runtime.Snapshot().DirtyTabs[1], "saving night affected day draft");
  f.Runtime.SelectTab(false);
  Near(f.Ambient(), 0.4F, "switch lost unsaved day");
  f.Runtime.Reload();
  Near(f.Ambient(), 0.2F, "revert did not read day file");
  f.Runtime.SelectTab(true);
  Near(f.Ambient(), 1.4F, "revert day affected night");
  f.Runtime.ObserveScene(0x55, {false,0,true,0x1000});
  Check(!f.Runtime.Snapshot().TimeDriven && !f.Runtime.Snapshot().EditingNight && f.Runtime.Snapshot().AppliedNightWeight == 0,
        "fixed scene selected night");
  Near(f.Applied.AmbientIntensity, 0.2F, "fixed lighting blended");
  Check(!f.Runtime.SelectTab(true) && !f.Runtime.CreateNightProfile(), "fixed lighting offers night edits");
  f.Runtime.ObserveScene(std::nullopt); // The real savestate reset boundary.
  f.Runtime.ObserveScene(0x55, {true,0,true,0x1000});
  Near(f.Applied.AmbientIntensity, 1.4F, "savestate did not snap to saved night");
  Check(f.Runtime.Snapshot().PreviewMode == SceneEnvironmentPreview::Auto, "savestate retained preview");
  f.Runtime.RemoveNightProfile();
  Check(!f.Runtime.Snapshot().NightProfileExists && f.Runtime.Snapshot().AppliedNightWeight == 0, "removed night still blends");
  Near(f.Applied.AmbientIntensity, 0.2F, "no-night scene was blended");
}
void CheckDraftHotReload(const std::filesystem::path& directory) {
  std::filesystem::create_directory(directory);
  Fixture f(directory);
  Write(f.Day, Profile(0.2F,false)); Write(f.Night, Profile(1.2F,true));
  f.Runtime.ObserveScene(0x55,{true,0x8000,false});
  f.Runtime.SetHotReload(true);
  auto day = f.Runtime.Snapshot().Profile; day.AmbientIntensity = 0.4F; f.Runtime.Preview(day);
  f.Runtime.SelectTab(true);
  auto night = f.Runtime.Snapshot().Profile; night.AmbientIntensity = 1.4F; f.Runtime.Preview(night);
  f.Runtime.SelectTab(false);
  Write(f.Day,Profile(0.8F,false)); Write(f.Night,Profile(0.6F,true));
  f.Settle();
  Near(f.Ambient(),0.4F,"hot reload replaced dirty day");
  f.Runtime.SelectTab(true); Near(f.Ambient(),1.4F,"hot reload replaced dirty inactive night");
  Check(f.Runtime.Snapshot().DirtyTabs == (std::array<bool,2>{true,true}), "hot reload lost dirty flags");
  f.Runtime.SelectTab(false); f.Runtime.Reload();
  Near(f.Ambient(),0.8F,"day revert did not take latest file");
  f.Runtime.SelectTab(true); Near(f.Ambient(),1.4F,"day revert lost dirty night");
  f.Runtime.Save(); f.Runtime.SelectTab(false); f.Settle();
  Check(f.Runtime.Snapshot().DirtyTabs == (std::array<bool,2>{false,false}), "independent save/revert did not settle drafts");
  f.Runtime.ObserveScene(std::nullopt);
  Write(f.Night,"{");
  f.Runtime.ObserveScene(0x55,{true,0,true});
  Check(!f.Runtime.Snapshot().NightProfileExists && !f.Runtime.Snapshot().NightError.empty(), "invalid night enabled blending");
  Check(!f.Runtime.CreateNightProfile(), "Create night overwrote an invalid existing file");
  std::ifstream invalid(f.Night); std::string text; std::getline(invalid,text);
  Check(text == "{", "failed Create night modified existing bytes");
}

void CheckGroupProfiles(const std::filesystem::path& directory) {
  std::filesystem::create_directories(directory);
  Fixture f(directory);
  const auto group=SceneEnvironmentGroup::TimeAtEntry;
  const auto day=directory/"group-outdoor-time-at-entry.environment.json";
  const auto night=directory/"group-outdoor-time-at-entry.night.environment.json";
  auto standard=BaseSceneEnvironmentProfile(); standard.AmbientIntensity=.4F;
  const auto encode=[&](float ambient,bool isNight) {
    auto p=standard; p.AmbientIntensity=ambient;
    return FormatSceneEnvironment(EncodeGroupEnvironment(p,group,isNight));
  };
  Write(day,encode(.4F,false)); Write(night,encode(.8F,true));
  const SceneEnvironmentTime time{true,0x8000,false,123,group};
  f.Runtime.ObserveScene(0x55,time); f.Runtime.SetHotReload(true);
  Check(f.Runtime.Snapshot().Group==group,"group not published");
  Near(f.Ambient(),.4F,"group day fallback");
  Check(f.Runtime.Snapshot().Sources[0]==SceneEnvironmentSource::Group &&
        f.Runtime.Snapshot().Sources[1]==SceneEnvironmentSource::Group,"group sources missing");
  Check(f.Runtime.Snapshot().NightProfileExists && !f.Runtime.Snapshot().FileExists,"group night unavailable or own-file confusion");
  f.Runtime.SelectTab(true); Near(f.Ambient(),.8F,"group night fallback");
  f.Runtime.SelectTab(false);
  auto edited=f.Runtime.Snapshot().Profile; edited.AmbientIntensity=.6F;
  f.Runtime.Preview(edited); Check(f.Runtime.Save(),"save fallback as scene file");
  Check(std::filesystem::exists(f.Day) && f.Runtime.Snapshot().Sources[0]==SceneEnvironmentSource::Scene,"save mutated group instead of creating own scene");
  f.Runtime.ObserveScene(0x55,{true,0,true,123,group});
  Near(f.Applied.AmbientIntensity,.8F,"own day incorrectly hid group night");
  f.Runtime.SelectTab(true); edited=f.Runtime.Snapshot().Profile; edited.AmbientIntensity=.9F;
  f.Runtime.Preview(edited); Check(f.Runtime.Save(),"save own night");
  Write(night,encode(.7F,true)); f.Settle(); Near(f.Ambient(),.9F,"group reload overrode own night");
  Check(f.Runtime.UseGroupStandard(),"remove own night for group");
  Near(f.Ambient(),.7F,"night group not adopted"); Check(!std::filesystem::exists(f.Night),"own night not removed");
  Write(night,encode(.3F,true)); f.Settle(); Near(f.Ambient(),.3F,"group night live reload");
  Near(f.Applied.AmbientIntensity,.3F,"reloaded group not published");
  edited=f.Runtime.Snapshot().Profile; edited.AmbientIntensity=1.2F; f.Runtime.Preview(edited);
  Write(night,encode(.2F,true)); f.Settle(); Near(f.Ambient(),1.2F,"group reload discarded draft");
  Check(f.Runtime.SaveAsGroupStandard(),"save group standard failed");
  auto file=LoadSceneEnvironmentFile(night,std::nullopt,true,group);
  Check(file.Document.Profile && file.Document.Group==group && !file.Document.SceneId,"group codec identity");
  Near(file.Document.Profile->AmbientIntensity,1.2F,"standard did not persist draft");
  Check(!f.Runtime.Snapshot().Dirty,"standard save left fallback dirty");
  f.Runtime.ObserveScene(0x52,{true,0,true,123,group});
  Near(f.Applied.AmbientIntensity,1.2F,"group standard not shared by another scene");
  f.Runtime.ObserveScene(0x55,time); Near(f.Ambient(),.6F,"own day lost precedence");
  Check(f.Runtime.UseGroupStandard(),"remove own day"); Near(f.Ambient(),.4F,"group day after removal");
  Write(day,encode(.55F,false)); f.Settle(); Near(f.Ambient(),.55F,"group day live reload");
  Write(day,"{"); f.Settle(); Near(f.Ambient(),.55F,"invalid group replaced last good");
  Check(!f.Runtime.Snapshot().Error.empty(),"invalid standard not diagnosed");
  std::filesystem::remove(day); f.Settle();
  Check(f.Runtime.Snapshot().Profile==BaseSceneEnvironmentProfile() && f.Runtime.Snapshot().Sources[0]==SceneEnvironmentSource::Base,"missing group day did not use base");
  std::filesystem::remove(night); f.Settle();
  Check(!f.Runtime.Snapshot().NightProfileExists && f.Runtime.Snapshot().Sources[1]==SceneEnvironmentSource::Day,"missing group night did not use day");
  // A mixed scene's later room has another class: keep this visit's group.
  f.Runtime.ObserveScene(0x55,{true,0x8000,false,123,SceneEnvironmentGroup::Indoor});
  Check(f.Runtime.Snapshot().Group==group,"mixed scene flipped group on room change");
  auto resource=time; resource.AuthoredScene=1; f.Runtime.ObserveScene(0x55,resource);
  resource.AuthoredScene=2; resource.Group=SceneEnvironmentGroup::Indoor;
  f.Runtime.ObserveScene(0x55,resource);
  Check(f.Runtime.Snapshot().Group==SceneEnvironmentGroup::Indoor,"new selected resource/setup did not reclassify same scene");
  f.Runtime.ObserveScene(std::nullopt);
  f.Runtime.ObserveScene(0x55,{true,0x8000,false,123,SceneEnvironmentGroup::FixedTime});
  Check(f.Runtime.Snapshot().Group==SceneEnvironmentGroup::FixedTime && !f.Runtime.Snapshot().TimeDriven,"fixed-time group still exposes blending");
  f.Runtime.ObserveScene(0x56,{true,0x8000,false});
  Check(!f.Runtime.Snapshot().Group && !f.Runtime.SaveAsGroupStandard(),"unknown group persisted standard");
  f.Runtime.ObserveScene(0x56,time);
  Check(f.Runtime.Snapshot().Group==group,"late authored headers not accepted");
  auto doc=EncodeGroupEnvironment(standard,group,false);
  Check(!DecodeSceneEnvironment(doc,0x55).Profile,"group accepted as scene profile");
  doc["groupId"]="no-such-group"; Check(!DecodeSceneEnvironment(doc).Profile,"unknown group ID accepted");
  doc=EncodeGroupEnvironment(standard,group,false); doc["sceneId"]=0x55;
  Check(!DecodeSceneEnvironment(doc).Profile,"both group/scene identities accepted");
  doc=EncodeGroupEnvironment(standard,group,false); doc["groupId"]=true;
  Check(!DecodeSceneEnvironment(doc).Profile,"mistyped group ID accepted");
  doc=EncodeGroupEnvironment(standard,group,false); doc["version"]=5;
  Check(!DecodeSceneEnvironment(doc).Profile,"legacy group schema accepted");
  // Write failure is surfaced and preserves the draft.
  std::filesystem::create_directory(day);
  Check(!f.Runtime.SaveAsGroupStandard() && !f.Runtime.Snapshot().Error.empty(),"failed standard write hidden");
}
// The release's profiles sit after the user's file of the same level.
void CheckReleaseDefaults(const std::filesystem::path& root) {
  const auto user = root / "user", defaults = root / "defaults";
  std::filesystem::create_directories(user);
  std::filesystem::create_directories(defaults);
  const auto group = SceneEnvironmentGroup::TimeAtEntry;
  const auto groupProfile = [&](float ambient) {
    auto p = BaseSceneEnvironmentProfile(); p.AmbientIntensity = ambient;
    return FormatSceneEnvironment(EncodeGroupEnvironment(p, group, false));
  };
  Fast::Renderer3ds::PicaEnvironmentLighting applied;
  SceneEnvironmentRuntime runtime(user, [&](const Fast::Renderer3ds::PicaEnvironmentLighting& value) { applied = value; },
                                  defaults);
  const SceneEnvironmentTime time{true, 0x8000, false, 0, group};
  const auto ambient = [&] { return runtime.Snapshot().Profile.AmbientIntensity; };
  const auto source = [&] { return runtime.Snapshot().Sources[0]; };
  runtime.ObserveScene(0x55, time);
  Check(source() == SceneEnvironmentSource::Base, "no file should resolve the base profile");
  Write(defaults / "group-outdoor-time-at-entry.environment.json", groupProfile(0.1F));
  runtime.ObserveScene(std::nullopt); runtime.ObserveScene(0x55, time);
  Check(source() == SceneEnvironmentSource::DefaultGroup && std::abs(ambient() - 0.1F) < 1e-6F,
        "the release's group default was not used");
  Write(user / "group-outdoor-time-at-entry.environment.json", groupProfile(0.2F));
  runtime.ObserveScene(std::nullopt); runtime.ObserveScene(0x55, time);
  Check(source() == SceneEnvironmentSource::Group && std::abs(ambient() - 0.2F) < 1e-6F,
        "the user's group standard does not precede the release's");
  Write(defaults / "scene-0055.environment.json", Profile(0.3F, false));
  runtime.ObserveScene(std::nullopt); runtime.ObserveScene(0x55, time);
  Check(source() == SceneEnvironmentSource::DefaultScene && std::abs(ambient() - 0.3F) < 1e-6F &&
            !runtime.Snapshot().FileExists,
        "the release's scene default does not precede group standards or reads as the user's file");
  auto edited = runtime.Snapshot().Profile; edited.AmbientIntensity = 0.4F;
  runtime.Preview(edited);
  Check(runtime.Save() && std::filesystem::exists(user / "scene-0055.environment.json") &&
            source() == SceneEnvironmentSource::Scene,
        "saving a default did not write the user's own scene file");
  Check(std::abs(ambient() - 0.4F) < 1e-6F, "the user's scene file does not precede the defaults");
  Check(runtime.UseGroupStandard() && source() == SceneEnvironmentSource::DefaultScene &&
            std::abs(ambient() - 0.3F) < 1e-6F,
        "removing the user's scene file did not return to the release's scene default");
}

void CheckShadowScheduling() {
  using namespace Fast::Renderer3ds;
  auto day = BaseSceneEnvironmentProfile(), night = day;
  day.KeyDirection = night.KeyDirection = PicaEnvironmentKeyDirection::Authored;
  day.Lights[0].Azimuth = 170; night.Lights[0].Azimuth = -170;
  VirtualShadowKeyframes scheduler;
  PicaEnvironmentLighting before;
  unsigned resets = 0, cuts = 0, starts = 0, rotations = 0, pending = 0, peakBudget = 0, frames = 0;
  for (unsigned clock = 0xC001; clock <= 0xD556; clock += 4) {
    auto after = ResolveSceneEnvironment(BlendSceneEnvironmentProfiles(day,night,NativeEnvironmentNightWeight(uint16_t(clock))),0x55,false);
    after.ContinuityEpoch = 9;
    if (!ContinuousPicaEnvironmentChange(before,after)) { ++resets; scheduler.Reset(); }
    const auto plan = scheduler.Begin(after.Lights[0].DirectionTowardSource, 1, 1, 256);
    cuts += plan.Cut; starts += plan.ConstructionStarted;
    if (plan.ConstructionStarted) pending = 256;
    peakBudget = std::max(peakBudget, plan.ConstructionBudget);
    pending -= std::min(pending, plan.ConstructionBudget);
    rotations += scheduler.EndConstruction(pending).Rotated;
    before = after; ++frames;
  }
  Check(resets == 1 && cuts == 1 && rotations > 1 && rotations < frames / 4 && peakBudget <= 64,
        "continuous blend thrashed shadow resets or exceeded existing page budget");
  auto edited = before; ++edited.ContinuityEpoch;
  Check(!ContinuousPicaEnvironmentChange(before,edited), "manual edit/cut reused stale history");
  edited = before; edited.CasterFaces = ShadowCasterFaces::Native;
  Check(!ContinuousPicaEnvironmentChange(before,edited), "discrete caster policy reused stale history");
  std::printf("Profile blend shadow schedule: %u frames, %u reset, %u cut, %u starts, %u rotations, peak page budget %u\n",
              frames,resets,cuts,starts,rotations,peakBudget);
}

} // namespace

int main() try {
  const auto directory = std::filesystem::temp_directory_path() / "triaevum_environment_tests";
  std::filesystem::remove_all(directory);
  std::filesystem::create_directories(directory);
  CheckNativeTime();
  CheckBlend();
  CheckShadowScheduling();
  CheckProfileRuntime(directory / "blend");
  CheckDraftHotReload(directory / "reload");
  CheckGroupProfiles(directory / "groups");
  CheckReleaseDefaults(directory / "defaults");
  Fixture f(directory);
  Write(f.Day, Profile(0.3F, false));
  f.Runtime.ObserveScene(0x55, {true, 0x8000, false});
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
  f.Runtime.ObserveScene(0x55, {true, 0, true});
  Check(!f.Runtime.Snapshot().EditingNight && f.Ambient() == 0.85F, "night without a night file left the day file");
  Write(f.Night, "{");
  f.Settle();
  Check(!f.Runtime.Snapshot().EditingNight && f.Runtime.Snapshot().Path == f.Day && f.Ambient() == 0.85F,
        "an invalid night file changed the profile or the file saved");
  Write(f.Night, Profile(0.15F, true));
  f.Settle();
  Check(f.Runtime.SelectTab(true), "cannot select the loaded night draft");
  Check(f.Runtime.Snapshot().EditingNight && f.Runtime.Snapshot().Path == f.Night && f.Ambient() == 0.15F,
        "a night file created on disk was not used at night");
  std::filesystem::remove(f.Night);
  f.Settle();
  Check(!f.Runtime.Snapshot().EditingNight && f.Runtime.Snapshot().Path == f.Day && f.Ambient() == 0.85F,
        "removing the night file did not return the night to the day file");

  // A missing file keeps the profile in use.
  f.Runtime.ObserveScene(0x55, {true, 0x8000, false});
  std::filesystem::remove(f.Day);
  f.Settle();
  Check(f.Ambient() == 0.85F, "a missing file replaced the profile in use");

  {
    using Faces = Fast::Renderer3ds::ShadowCasterFaces;
    Check(BaseSceneEnvironmentProfile().CasterFaces == Faces::InteriorShells, "base profile does not pass interior shells");
    auto document = EncodeSceneEnvironment(BaseSceneEnvironmentProfile(), 0x55, false);
    Check(document["shadowCasterFaces"] == "interiorShells" && !document.contains("shadowCasterCulling"),
          "new enum was not encoded canonically");
    document.erase("shadowCasterFaces");
    const auto older = DecodeSceneEnvironment(document);
    Check(older.Profile && older.Profile->CasterFaces == Faces::Native, "missing legacy key lost native culling");
    for (bool enabled : {true, false}) {
      document["shadowCasterCulling"] = enabled;
      const auto decoded = DecodeSceneEnvironment(document);
      Check(decoded.Profile && decoded.Profile->CasterFaces == (enabled ? Faces::Native : Faces::BothFaces),
            "legacy boolean policy changed");
    }
    for (auto mode : {Faces::Native, Faces::BothFaces, Faces::InteriorShells}) {
      auto profile = BaseSceneEnvironmentProfile(); profile.CasterFaces = mode;
      auto encoded = EncodeSceneEnvironment(profile, 0x55, false);
      encoded["shadowCasterCulling"] = true; // explicit new setting takes precedence
      const auto decoded = DecodeSceneEnvironment(encoded);
      Check(decoded.Profile && decoded.Profile->CasterFaces == mode, "caster faces did not round trip");
      Check(ResolveSceneEnvironment(profile, 0x55, false).CasterFaces == mode, "renderer lost caster faces");
    }
    for (const auto& invalid : {nlohmann::json(1), nlohmann::json(true), nlohmann::json("bad"), nlohmann::json(nullptr)}) {
      document["shadowCasterFaces"] = invalid;
      Check(!DecodeSceneEnvironment(document).Errors.empty(), "invalid enum accepted");
    }
    document.erase("shadowCasterFaces"); document["shadowCasterCulling"] = 1;
    Check(!DecodeSceneEnvironment(document).Errors.empty(), "non-boolean legacy setting accepted");
  }

  {
    using Scope = Fast::Renderer3ds::PicaRimLightScope;
    auto profile = BaseSceneEnvironmentProfile();
    Check(profile.Rim.Enabled && profile.Rim.Scope == Scope::Actors && profile.Rim.Intensity == 1.0F &&
              profile.Rim.Sharpness == 4.0F && profile.Rim.Source == Fast::Renderer3ds::PicaRimLightSource::Independent &&
              profile.Rim.Color == (std::array<float, 3>{1, 0.96F, 0.9F}) && profile.Rim.Azimuth == 0 &&
              profile.Rim.Coverage == 0.25F, "base rim defaults changed");
    auto document = EncodeSceneEnvironment(profile, 0x55, false);
    const auto decoded = DecodeSceneEnvironment(document);
    Check(decoded.Profile && decoded.Profile->Rim == profile.Rim, "rim did not round trip");
    Check(ResolveSceneEnvironment(profile, 0x55, false).Rim == profile.Rim, "renderer lost rim settings");
    auto round27 = document;
    for (const auto* key : {"source", "color", "azimuth", "coverage"}) round27["rimLight"].erase(key);
    round27["rimLight"]["ambient"] = 0.05;
    round27["rimLight"]["intensity"] = 0.35;
    const auto migrated = DecodeSceneEnvironment(round27);
    Check(migrated.Profile && migrated.Profile->Rim.Source == Fast::Renderer3ds::PicaRimLightSource::Independent &&
              migrated.Profile->Rim.Color == profile.Rim.Color && migrated.Profile->Rim.Coverage == 0.25F &&
              migrated.Profile->Rim.Azimuth == 0 && migrated.Profile->Rim.Intensity == 0.35F,
          "round-27 rim did not acquire independent defaults or lost its saved intensity");
    Check(!EncodeSceneEnvironment(*migrated.Profile, 0x55, false)["rimLight"].contains("ambient"),
          "retired ambient control was saved again");
    for (const auto* key : {"source", "color", "azimuth", "coverage"}) {
      auto optional = document; optional["rimLight"].erase(key);
      Check(DecodeSceneEnvironment(optional).Profile->Rim == profile.Rim, "optional rim member lost its default");
      optional = document; optional["rimLight"][key] = nullptr;
      Check(!DecodeSceneEnvironment(optional).Profile, "null optional rim member accepted");
    }
    document.erase("rimLight");
    const auto legacy = DecodeSceneEnvironment(document);
    Check(legacy.Profile && !legacy.Profile->Rim.Enabled, "legacy profile acquired a rim");
    profile.Rim.Scope = Scope::ActorsAndWorld;
    profile.Rim.Source = Fast::Renderer3ds::PicaRimLightSource::SceneLights;
    profile.Rim.Color = {0.4F, 0.5F, 0.8F};
    profile.Rim.Azimuth = -90;
    profile.Rim.Coverage = 0.75F;
    Check(DecodeSceneEnvironment(EncodeSceneEnvironment(profile, 0x55, true)).Profile->Rim == profile.Rim,
          "world scope or night rim did not round trip");
    document = EncodeSceneEnvironment(profile, 0x55, false);
    for (const auto& invalid : {nlohmann::json(1), nlohmann::json(nullptr), nlohmann::json("on")}) {
      auto bad = document; bad["rimLight"] = invalid;
      Check(!DecodeSceneEnvironment(bad).Profile, "mistyped rim accepted");
    }
    for (const auto& key : {"enabled", "intensity", "sharpness", "scope"}) {
      auto bad = document; bad["rimLight"].erase(key);
      const auto result = DecodeSceneEnvironment(bad);
      Check(!result.Profile && result.Errors.front().Pointer == std::string("/rimLight/") + key,
            "missing rim member not diagnosed at owning boundary");
      bad = document; bad["rimLight"][key] = nlohmann::json::array();
      Check(!DecodeSceneEnvironment(bad).Profile, "mistyped rim member accepted");
    }
    for (const auto& [key, badValue] : std::vector<std::pair<std::string, nlohmann::json>>{
        {"enabled", 1}, {"intensity", 8.01}, {"intensity", -0.1}, {"sharpness", 0}, {"sharpness", 17},
        {"ambient", 0.26}, {"scope", "all"}, {"shaprness", 4}, {"source", "moon"},
        {"source", 1}, {"color", nlohmann::json::array({1, 0})}, {"color", nlohmann::json::array({1, 1.1, 0})},
        {"color", nlohmann::json::array({1, "white", 0})}, {"azimuth", 181}, {"azimuth", -181},
        {"azimuth", false}, {"coverage", -0.1}, {"coverage", 1.1}, {"coverage", "wide"}}) {
      auto bad = document; bad["rimLight"][key] = badValue;
      Check(!DecodeSceneEnvironment(bad).Profile, "invalid rim value accepted");
    }
    for (float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
      auto bad = profile; bad.Rim.Intensity = invalid;
      Check(!ValidateSceneEnvironment(bad).empty(), "nonfinite direct rim accepted");
      bad = profile; bad.Rim.Sharpness = invalid;
      Check(!ValidateSceneEnvironment(bad).empty(), "nonfinite rim exponent accepted");
      bad = profile; bad.Rim.Coverage = invalid;
      Check(!ValidateSceneEnvironment(bad).empty(), "nonfinite rim coverage accepted");
      bad = profile; bad.Rim.Azimuth = invalid;
      Check(!ValidateSceneEnvironment(bad).empty(), "nonfinite rim azimuth accepted");
      bad = profile; bad.Rim.Color[2] = invalid;
      Check(!ValidateSceneEnvironment(bad).empty(), "nonfinite rim color accepted");
      auto badJson = document; badJson["rimLight"]["intensity"] = invalid;
      Check(!DecodeSceneEnvironment(badJson).Profile, "nonfinite JSON rim accepted");
    }
    Check(f.Runtime.Preview(profile) && f.Runtime.Save() && f.Runtime.Reload() &&
              f.Runtime.Snapshot().Profile.Rim == profile.Rim, "runtime did not save/reload rim");
    const auto previous = f.Runtime.Snapshot();
    profile.Rim.Intensity = 9.0F;
    Check(!f.Runtime.Preview(profile) && f.Runtime.Snapshot().Profile == previous.Profile,
          "invalid rim preview replaced the accepted profile");
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
    // MM3D's adapter publishes SceneEnvironmentTime from its own state: the
    // world identity stands for the authored scene, the clock for the blend.
    const auto at=[](uint16_t clock,bool night,uint64_t world){SceneEnvironmentTime t;t.TimeDriven=true;t.Clock=clock;t.Night=night;t.AuthoredScene=world;return t;};
    const auto unknown=[](uint64_t world){SceneEnvironmentTime t;t.AuthoredScene=world;return t;};
    Fast::Renderer3ds::PicaEnvironmentLighting published;
    SceneEnvironmentRuntime runtime(mm,[&](const auto& v){published=v;},{},fallback);
    runtime.ObserveScene(0x55,at(0x8000,false,10));
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
    runtime.ObserveScene(0x55,at(0x8000,false,11));
    Check(runtime.Snapshot().Revision>previousRevision && runtime.Snapshot().Profile==fallback,
          "same-scene restore retained stale preview");
    runtime.ObserveScene(0x55,unknown(11));
    Check(!runtime.Snapshot().TimeDriven && !runtime.Snapshot().EditingNight &&
          runtime.Snapshot().Profile==fallback,"unknown time selected a night profile or lost the day profile");
    Write(mm/"scene-0056.environment.json","{invalid");
    runtime.ObserveScene(0x56,at(0x8000,false,12));
    // A first-entry failure stays disabled and keeps nothing of the previous
    // scene or of OoT3D's profile; the error names the file.
    Check(!runtime.Snapshot().Profile.Enabled && !published.Enabled && !runtime.Snapshot().Error.empty() &&
              runtime.Snapshot().Profile.AmbientIntensity!=0.95F,
          "invalid new-scene file leaked old or OoT profile");
    runtime.ObserveScene(std::nullopt);
    Check(!runtime.Snapshot().SceneId && !runtime.Snapshot().Player && !published.Enabled,
          "lost context kept active environment");
    SceneEnvironmentRuntime legacy(oot,[](const auto&){});
    legacy.ObserveScene(0x55);
    Check(legacy.Snapshot().Profile.AmbientIntensity==0.95F,"OoT legacy file stopped working");
    legacy.ObserveScene(0x57);
    Check(legacy.Snapshot().Profile==BaseSceneEnvironmentProfile(),"OoT default changed");
    std::filesystem::remove_all(directory);
  }
  // The release's defaults are title-scoped like the user's profiles: OoT3D's
  // shipped scene-0055 (Ganon's Castle) must not apply to MM3D's scene 0x55
  // (Music Box House), and OoT3D keeps reading its shipped root.
  {
    const auto resources=directory/"resources";
    Check(TitleReleaseSceneEnvironmentDirectory({},TitleAdapter::Mm3d).empty() &&
          TitleReleaseSceneEnvironmentDirectory({},TitleAdapter::Oot3dEur).empty(),
          "no resource root named release defaults");
    const auto ootDefaults=TitleReleaseSceneEnvironmentDirectory(resources,TitleAdapter::Oot3dEur);
    const auto mmDefaults=TitleReleaseSceneEnvironmentDirectory(resources,TitleAdapter::Mm3d);
    Check(ootDefaults==resources/"environments" && mmDefaults==resources/"environments"/"mm3d",
          "release default namespaces moved");
    std::filesystem::create_directories(ootDefaults);
    Write(ootDefaults/"scene-0055.environment.json",Profile(0.95F,false));
    const auto config=directory/"settings.json";
    const auto mmUser=TitleSceneEnvironmentDirectory(config,TitleAdapter::Mm3d);
    const auto ootUser=TitleSceneEnvironmentDirectory(config,TitleAdapter::Oot3dEur);
    std::filesystem::create_directories(mmUser);
    const auto fallback=NativePreservingSceneEnvironmentProfile();
    Fast::Renderer3ds::PicaEnvironmentLighting published;
    SceneEnvironmentTime clock;clock.TimeDriven=true;clock.Clock=0x8000;clock.AuthoredScene=10;
    SceneEnvironmentRuntime mm(mmUser,[&](const auto& v){published=v;},mmDefaults,fallback);
    mm.ObserveScene(0x55,clock);
    Check(mm.Snapshot().Profile==fallback && mm.Snapshot().Sources[0]==SceneEnvironmentSource::Base &&
              !published.Enabled,
          "MM3D took OoT3D's release scene default");
    SceneEnvironmentRuntime oot(ootUser,[](const auto&){},ootDefaults);
    oot.ObserveScene(0x55,{true,0x8000,false});
    Check(oot.Snapshot().Sources[0]==SceneEnvironmentSource::DefaultScene &&
              std::abs(oot.Snapshot().Profile.AmbientIntensity-0.95F)<1e-6F,
          "OoT3D lost its release scene default");
    std::filesystem::remove_all(directory);
  }
  // A title's base per environment group: a scene without files takes its
  // group's base, an unknown group or a group without its own takes the
  // shared fallback; a group that becomes known later reloads an unedited
  // slot; files keep precedence; an invalid base is refused.
  {
    const auto user=directory/"group-base";
    std::filesystem::create_directories(user);
    SceneEnvironmentFallback base(NativePreservingSceneEnvironmentProfile());
    auto outdoor=NativePreservingSceneEnvironmentProfile();
    outdoor.Enabled=true;outdoor.NativeDirectStrength=0;outdoor.NativeAmbientStrength=0.6F;
    outdoor.PreserveSceneBrightness=false;outdoor.Lights[0].Intensity=1;
    base.Groups[size_t(SceneEnvironmentGroup::TimeFlows)]=outdoor;
    Check(base.For(std::nullopt)==base.Unknown && base.For(SceneEnvironmentGroup::Indoor)==base.Unknown &&
              base.For(SceneEnvironmentGroup::TimeFlows)==outdoor,"group base lookup");
    Fast::Renderer3ds::PicaEnvironmentLighting published;
    SceneEnvironmentRuntime runtime(user,[&](const auto& v){published=v;},{},base);
    const auto at=[](std::optional<SceneEnvironmentGroup> group,uint64_t world){
      SceneEnvironmentTime t;t.Group=group;t.AuthoredScene=world;return t;};
    runtime.ObserveScene(0x6F,at(SceneEnvironmentGroup::TimeFlows,1));
    Check(runtime.Snapshot().Profile==outdoor && runtime.Snapshot().Sources[0]==SceneEnvironmentSource::Base &&
              published.Enabled,"group base not applied");
    runtime.ObserveScene(0x61,at(SceneEnvironmentGroup::Indoor,2));
    Check(runtime.Snapshot().Profile==base.Unknown && !published.Enabled,"group without base not native");
    runtime.ObserveScene(0x2D,at(std::nullopt,3));
    Check(runtime.Snapshot().Profile==base.Unknown && !published.Enabled,"unknown group not native");
    runtime.ObserveScene(0x2D,at(SceneEnvironmentGroup::TimeFlows,3));
    Check(runtime.Snapshot().Profile==outdoor && published.Enabled,"late group did not take its base");
    auto own=outdoor;own.NativeAmbientStrength=0.25F;
    Write(user/"scene-0050.environment.json",
          FormatSceneEnvironment(EncodeSceneEnvironment(own,0x50,false)));
    runtime.ObserveScene(0x50,at(SceneEnvironmentGroup::TimeFlows,4));
    Check(runtime.Snapshot().Profile==own && runtime.Snapshot().Sources[0]==SceneEnvironmentSource::Scene,
          "scene file lost precedence over the group base");
    auto invalid=base;
    invalid.Groups[size_t(SceneEnvironmentGroup::FixedTime)]=outdoor;
    invalid.Groups[size_t(SceneEnvironmentGroup::FixedTime)]->NativeAmbientStrength=std::nanf("");
    bool refused=false;
    try { SceneEnvironmentRuntime bad(user,[](const auto&){},{},invalid); } catch (const std::invalid_argument&) { refused=true; }
    Check(refused,"invalid group base accepted");
    std::filesystem::remove_all(directory);
  }
  std::printf("scene environment tests passed: %d checks\n", gChecks);
  return 0;
} catch (const std::exception &error) {
  std::fprintf(stderr, "scene environment tests FAILED: %s\n", error.what());
  return 1;
}
