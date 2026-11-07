// MM3D's base scene environment per group (owner decision Q-P1 (b)): the
// shape native fragment lights take, applied to the Scene lights only, in
// every known group (Indoor included, as OoT3D); unknown groups stay native. Exercised through the scene environment
// runtime and the shared fragment-balance builder, with light bindings shaped
// like the native ones of the P1.2 diagnosis (no ROM, process or GPU).
#include "mm3d_scene_environment_profile.h"
#include "oot3d_scene_environment_codec.h"
#include "fast/renderer3ds/pica_fragment_balance.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
using namespace Oot3dNativeGame;
namespace R = Fast::Renderer3ds;
int gChecks = 0;
void Check(bool condition, const char *what) {
  ++gChecks;
  if (!condition) throw std::runtime_error(what);
}

SceneEnvironmentFallback Mm3dFallback() {
  SceneEnvironmentFallback fallback(NativePreservingSceneEnvironmentProfile());
  for (size_t group = 0; group < fallback.Groups.size(); ++group)
    fallback.Groups[group] = Mm3dBaseSceneEnvironmentProfile(SceneEnvironmentGroup(group));
  return fallback;
}

// Clock Town floor at noon (P1.2): slot 0 the opposite record, slot 1 the
// sun; a lamp (actor light) in slot 2 as at night.
struct Floor {
  R::PicaFragmentLightingLayout Layout;
  R::PicaNativeLightBinding Binding;
  Floor() {
    Layout.SchemaVersion = R::kPicaFragmentLightingLayoutSchemaVersion;
    Layout.ActiveLightCount = 3;
    Layout.LightPermutation = {0, 1, 2};
    Layout.Lights[0].Directional = Layout.Lights[1].Directional = true;
    auto scene = std::make_shared<R::PicaSceneDirectionalLights>();
    scene->Count = 2;
    scene->Scope = R::PicaNativeLightScope::Scene;
    scene->Lights[0] = {0x804e214U, {0.25F, 0.949F, 0.19F}, {218 / 255.F, 192 / 255.F, 178 / 255.F}, true};
    scene->Lights[1] = {0x804e22cU, {-0.25F, -0.949F, -0.19F}, {18 / 255.F, 44 / 255.F, 70 / 255.F}, true};
    Binding.SceneDirectionals = scene;
    Binding.Sources[0].SourceIdentity = 0x804e22cU;
    Binding.Sources[1].SourceIdentity = 0x804e214U;
    for (size_t i : {size_t(0), size_t(1)}) {
      Binding.Sources[i].Kind = R::PicaNativeLightSource::Type::Directional;
      Binding.Sources[i].Origin = R::PicaNativeLightOrigin::EnvironmentDirectional;
      Binding.Sources[i].Scope = R::PicaNativeLightScope::Scene;
    }
    Binding.Sources[2].Kind = R::PicaNativeLightSource::Type::Point;
    Binding.Sources[2].SourceIdentity = 0x9915f1cU;
    Binding.Sources[2].Origin = R::PicaNativeLightOrigin::Actor;
  }
};
} // namespace

int main() try {
  using Group = SceneEnvironmentGroup;
  // The base of each group.
  for (Group group : {Group::Indoor, Group::TimeFlows, Group::TimeAtEntry, Group::FixedTime}) {
    const auto base = Mm3dBaseSceneEnvironmentProfile(group);
    Check(base.has_value(), "known group without a base");
    Check(*base == *Mm3dBaseSceneEnvironmentProfile(Group::TimeFlows), "groups differ in their base");
    Check(ValidateSceneEnvironment(*base).empty(), "base does not validate");
    const auto lighting = ResolveSceneEnvironment(*base, 0x6F, false);
    Check(R::PicaFragmentBalanceProfileSupported(lighting), "base is not the native fragment-light shape");
    Check(R::PicaFragmentLitFrameEnvironment(lighting, true) == lighting, "base falls back to native");
    Check(lighting.NativeLocalStrength == 1.0F, "local lights not native");
    Check(lighting.NativeDirectStrength == 0.0F && !lighting.PreserveSceneBrightness,
          "native scene direct not OoT3D's base (0, not preserving)");
    Check(lighting.NativeAmbientStrength == kMm3dBaseNativeAmbientStrength &&
              kMm3dBaseNativeAmbientStrength == BaseSceneEnvironmentProfile().NativeAmbientStrength,
          "scene ambient gain is not OoT3D's base");
    Check(lighting.AmbientIntensity == 0.0F && lighting.Lights[1].Intensity == 0.0F &&
              lighting.PlacedLightCount == 0 && !lighting.Rim.Enabled,
          "base adds a light");
    Check(lighting.KeyDirection == R::PicaEnvironmentKeyDirection::SceneMain &&
              lighting.Lights[0].Intensity == 1.0F &&
              lighting.Lights[0].Color == std::array<float, 3>{1.0F, 1.0F, 1.0F},
          "key does not follow the native sun/moon white 1x");
    Check(lighting.FogStrength == 1.0F && lighting.CasterFaces == R::ShadowCasterFaces::Native,
          "base changes fog or caster faces");
  }
  Check(!Mm3dBaseSceneEnvironmentProfile(Group(7)), "an unknown group has a base");

  // The runtime: TimeFlows and Indoor take the base, unknown stays native.
  const auto directory = std::filesystem::temp_directory_path() /
      ("mm3d-environment-base-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(directory);
  struct Cleanup { std::filesystem::path Path; ~Cleanup() { std::error_code e; std::filesystem::remove_all(Path, e); } } cleanup{directory};
  R::PicaEnvironmentLighting published;
  SceneEnvironmentRuntime runtime(directory, [&](const auto &value) { published = value; }, {}, Mm3dFallback());
  const auto at = [](std::optional<Group> group, uint64_t world, bool driven) {
    SceneEnvironmentTime t; t.Group = group; t.AuthoredScene = world; t.TimeDriven = driven; return t; };
  runtime.ObserveScene(0x6F, at(Group::TimeFlows, 1, true));
  Check(published.Enabled && R::PicaFragmentBalanceProfileSupported(published), "South Clock Town not on its base");
  const auto noon = published;
  runtime.ObserveScene(0x61, at(Group::Indoor, 2, false));
  Check(published.Enabled && R::PicaFragmentBalanceProfileSupported(published), "Stock Pot Inn not on its base");
  const auto inn = published;
  runtime.ObserveScene(0x2D, at(std::nullopt, 3, false));
  Check(!published.Enabled, "unknown group not native");
  // The snapshot reports what was published (the F1 status reads it).
  Check(runtime.Snapshot().Published == published, "snapshot does not report the published environment");
  runtime.ObserveScene(0x6F, at(Group::TimeFlows, 4, true));
  Check(runtime.Snapshot().Published == published && published.Enabled, "snapshot lags the publication");

  // The base on a fragment-lit floor: the sun is the key, the opposite
  // record is removed, the lamp stays native, the ambient is a relative gain.
  Floor floor;
  const auto balance = R::BuildPicaFragmentBalanceUniforms(true, floor.Layout, floor.Binding, noon);
  Check(balance.has_value(), "the base does not apply to a Clock Town floor");
  Check(balance->Control[1] == 1U, "the sun is not the key");
  Check(balance->Gains[0] == std::array<float, 4>{0, 0, kMm3dBaseNativeAmbientStrength, 1},
        "the opposite scene record is not removed");
  Check(balance->Gains[2] == std::array<float, 4>{1, 1, 1, 0}, "the lamp is not native");
  Check(balance->Parameters[0] == kMm3dBaseNativeAmbientStrength, "scene ambient not a relative gain");
  // Unproved owners stay native.
  auto unproved = floor.Binding;
  unproved.Sources[0].Scope = unproved.Sources[1].Scope = R::PicaNativeLightScope::Local;
  const auto unprovedBalance=R::BuildPicaFragmentBalanceUniforms(true, floor.Layout, unproved, noon);
  Check(unprovedBalance && unprovedBalance->Control[1]==8, "unproved sun replaced");
  // An inn wall: global ambient and three lamps, no scene directional.
  R::PicaFragmentLightingLayout wall;
  wall.SchemaVersion = R::kPicaFragmentLightingLayoutSchemaVersion;
  wall.ActiveLightCount = 3;
  wall.LightPermutation = {0, 1, 2};
  R::PicaNativeLightBinding lamps;
  lamps.SceneDirectionals = floor.Binding.SceneDirectionals;
  for (uint32_t i = 0; i < 3; ++i) {
    lamps.Sources[i].Kind = R::PicaNativeLightSource::Type::Point;
    lamps.Sources[i].SourceIdentity = 0x99099c4U + i * 0x6000U;
    lamps.Sources[i].Origin = R::PicaNativeLightOrigin::Actor;
  }
  const auto wallBalance=R::BuildPicaFragmentBalanceUniforms(true, wall, lamps, inn);
  Check(wallBalance && wallBalance->Control[1]==8, "a local-only room acquired the actor key");
  Check(wallBalance->Gains[0]==std::array<float,4>{1,1,1,0}, "inn lamp not native");
  // An actor in the inn lit by the environment's records takes the key.
  const auto actor = R::BuildPicaFragmentBalanceUniforms(true, floor.Layout, floor.Binding, inn);
  Check(actor && actor->Control[1] == 1U, "an inn actor lit by the environment has no key");

  // Exercise the real shipped files through the real resource-default loader,
  // not an equivalent hard-coded document. User files remain in a private dir.
  const auto release=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() /
      "runtime/triaevum_resources/environments/mm3d";
  SceneEnvironmentRuntime shipped(directory, [&](const auto& v) { published=v; }, release, Mm3dFallback());
  R::PicaEnvironmentLighting ootPublished;
  SceneEnvironmentRuntime ootShipped(directory / "oot3d", [&](const auto& v) { ootPublished=v; },
                                     release.parent_path());
  uint64_t world=10;
  std::optional<R::PicaEnvironmentLighting> calibratedIndoor;
  for (Group group : {Group::Indoor, Group::FixedTime, Group::TimeAtEntry, Group::TimeFlows}) {
    ootShipped.ObserveScene(0x6F, at(group, world, true));
    shipped.ObserveScene(0x6F, at(group, world++, true));
    Check(published.PointLights==ootPublished.PointLights, "day point defaults differ from OoT3D");
    const auto snapshot=shipped.Snapshot();
    Check(snapshot.Error.empty(), "shipped profile invalid");
    Check(snapshot.Sources[0]==SceneEnvironmentSource::DefaultGroup, "shipped group did not beat fallback");
    Check(snapshot.SourcePaths[0].parent_path()==release, "foreign title namespace");
    Check(published.Enabled && published.Rim.Enabled, "shipped rim lost");
    Check(R::PicaFragmentBalanceProfileSupported(published), "shipped shape refused");
    Check(published.NativeLocalStrength==1, "shipped profile removes native local lamps");
    if (group==Group::Indoor || group==Group::TimeFlows) {
      // The register already includes the draw's material/room-vs-actor
      // ambient. A constant material-weighted addition cannot replace it.
      Check(!published.PreserveSceneBrightness && published.NativeAmbientStrength==1,
            "calibrated day profile loses the native ambient ledger");
      Check(published.AmbientIntensity==0, "calibrated day profile adds an absolute ambient budget");
      Check(published.NativeDirectStrength==0 && published.Lights[0].Intensity==1,
            "calibration disabled the native-following key/shadow");
      if (group==Group::Indoor) calibratedIndoor=published;
    }
    Check(shipped.SetForcedNightWeight(1), "night force failed");
    Check(ootShipped.SetForcedNightWeight(1), "OoT night force failed");
    Check(published.PointLights==ootPublished.PointLights, "night point defaults differ from OoT3D");
    if (group==Group::TimeFlows || group==Group::TimeAtEntry) {
      Check(shipped.Snapshot().Sources[1]==SceneEnvironmentSource::DefaultGroup, "night group missing");
      Check(shipped.Snapshot().NightProfileExists, "night endpoint not loaded");
      Check(published.AmbientColor[2]>published.AmbientColor[0], "night palette not loaded");
    }
    shipped.SetForcedNightWeight(std::nullopt);
    ootShipped.SetForcedNightWeight(std::nullopt);
  }
  // Scene-lit ambient regression: the active order can put Local points
  // before the proved Scene directionals, independently of hardware slots.
  // The captured global ambient is already material-weighted; losing 60%
  // is wrong even in a full key shadow. No actor/material identity enters
  // the policy. Multiplicative secondary TEV has a separate shared regression.
  Check(calibratedIndoor.has_value(), "indoor calibration was not loaded");
  auto propBinding=floor.Binding;
  auto fixed=std::make_shared<R::PicaSceneDirectionalLights>(*propBinding.SceneDirectionals);
  for (uint32_t i=0;i<fixed->Count;++i) fixed->Lights[i].SkyLight=false;
  propBinding.SceneDirectionals=fixed;
  R::PicaFragmentLightingLayout propLayout;
  propLayout.SchemaVersion=R::kPicaFragmentLightingLayoutSchemaVersion;
  propLayout.ActiveLightCount=4;propLayout.LightPermutation={2,3,0,1};
  propLayout.Lights[0].Directional=propLayout.Lights[1].Directional=true;
  for (uint32_t i=2;i<4;++i) {
    propBinding.Sources[i]={};
    propBinding.Sources[i].Kind=R::PicaNativeLightSource::Type::Point;
    propBinding.Sources[i].SourceIdentity=0x100U+i;
  }
  const auto prop=R::BuildPicaFragmentBalanceUniforms(true,propLayout,propBinding,*calibratedIndoor);
  Check(prop && prop->Control[1]==1, "a proved interior key was lost or invented on another slot");
  Check(prop->Gains[2]==std::array<float,4>{1,1,1,0} &&
        prop->Gains[3]==std::array<float,4>{1,1,1,0}, "calibration modifies local lamps");
  for (const auto& ambient : {std::array<float,3>{55/255.F,51/255.F,51/255.F},
                             std::array<float,3>{127/255.F,149/255.F,139/255.F}})
    for (float channel : ambient)
      Check(channel*prop->Parameters[0]==channel,
            "a full key shadow must not remove the prop/room's native ambient budget");
  // A local-only room still has NO actor key, but keeps that same ambient.
  const auto localRoom=R::BuildPicaFragmentBalanceUniforms(true,wall,lamps,*calibratedIndoor);
  Check(localRoom && localRoom->Control[1]==8 && localRoom->Parameters[0]==1,
        "local-only room acquired a key or lost ambient");
  // The release never writes a user profile just by loading its defaults.
  Check(std::filesystem::is_empty(directory), "loading release defaults wrote user files");
  // Independent title user files, scene/day/night and group standard use
  // the same codec. No new Graphics-preset copy of these controls exists.
  const auto user = directory / "mm3d";
  SceneEnvironmentRuntime edited(user, [&](const auto& v) { published=v; }, release, Mm3dFallback());
  edited.ObserveScene(0x6F, at(Group::TimeFlows,100,true));
  auto day = edited.Snapshot().Profile;
  day.Enabled = false;
  for (size_t i=0;i<4;++i) {
    day.PointLights[i].Enabled = i<2;
    day.PointLights[i].Intensity = .5F+float(i);
    day.PointLights[i].RadiusScale = 1+float(i);
    day.PointLights[i].ColorTint = {.25F,.5F,.75F};
  }
  Check(edited.Preview(day) && edited.Save(), "point day save failed");
  Check(edited.SaveAsGroupStandard(), "point group save failed");
  Check(edited.SelectTab(true) && edited.CopyDayToNight(), "point night draft failed");
  auto night = edited.Snapshot().Profile;
  for (auto& p : night.PointLights) p.Enabled = !p.Enabled;
  Check(edited.Preview(night) && edited.Save(), "point night save failed");
  Check(edited.Reload() && edited.Snapshot().Profile.PointLights==night.PointLights,
        "night point settings did not persist");
  Check(edited.SelectTab(false) && edited.Reload() && edited.Snapshot().Profile.PointLights==day.PointLights,
        "day point settings did not persist");
  Check(edited.SetForcedNightWeight(0) &&
        published.PointLights==BlendSceneEnvironmentProfiles(day,night,0).PointLights && !published.Enabled,
        "disabled profile lost independent point settings");
  Check(edited.SetForcedNightWeight(1) &&
        published.PointLights==BlendSceneEnvironmentProfiles(day,night,1).PointLights,
        "night point settings did not publish");
  edited.ObserveScene(0x2D,at(Group::TimeFlows,101,true));
  Check(edited.Snapshot().Sources[0]==SceneEnvironmentSource::Group &&
        edited.Snapshot().Profile.PointLights==day.PointLights,"point group fallback lost");
  Check(!std::filesystem::exists(directory/"oot3d"),"MM settings wrote OoT user files");
  std::printf("mm3d scene environment profile tests passed: %d checks\n", gChecks);
  return 0;
} catch (const std::exception &error) {
  std::fprintf(stderr, "mm3d scene environment profile tests FAILED: %s\n", error.what());
  return 1;
}
