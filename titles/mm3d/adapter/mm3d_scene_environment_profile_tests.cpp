// MM3D's base scene environment per group (owner decision Q-P1 (b)): the
// shape native fragment lights take, applied to the Scene lights only; Indoor
// and unknown groups stay native. Exercised through the scene environment
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
  for (Group group : {Group::TimeFlows, Group::TimeAtEntry, Group::FixedTime}) {
    const auto base = Mm3dBaseSceneEnvironmentProfile(group);
    Check(base.has_value(), "outdoor group without a base");
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
  Check(!Mm3dBaseSceneEnvironmentProfile(Group::Indoor), "Indoor is not native");
  Check(!Mm3dBaseSceneEnvironmentProfile(Group(7)), "an unknown group has a base");

  // The runtime: TimeFlows takes the base, Indoor and unknown stay native.
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
  Check(!published.Enabled, "Stock Pot Inn not native");
  runtime.ObserveScene(0x2D, at(std::nullopt, 3, false));
  Check(!published.Enabled, "unknown group not native");

  // The base on a fragment-lit floor: the sun is the key, the opposite
  // record is removed, the lamp stays native, the ambient is a relative gain.
  Floor floor;
  const auto balance = R::BuildPicaFragmentBalanceUniforms(true, floor.Layout, floor.Binding, noon);
  Check(balance.has_value(), "the base does not apply to a Clock Town floor");
  Check(balance->Control[1] == 1U, "the sun is not the key");
  Check(balance->Gains[0] == std::array<float, 4>{0, 0, kMm3dBaseNativeAmbientStrength, 1},
        "the opposite scene record is not removed");
  Check(balance->Gains[2] == std::array<float, 4>{1, 1, kMm3dBaseNativeAmbientStrength, 0},
        "the lamp is not native");
  Check(balance->Parameters[0] == kMm3dBaseNativeAmbientStrength, "scene ambient not a relative gain");
  // Unproved owners stay native.
  auto unproved = floor.Binding;
  unproved.Sources[0].Scope = unproved.Sources[1].Scope = R::PicaNativeLightScope::Local;
  Check(!R::BuildPicaFragmentBalanceUniforms(true, floor.Layout, unproved, noon), "unproved sun replaced");
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
  Check(!R::BuildPicaFragmentBalanceUniforms(true, wall, lamps, noon), "a room without a scene key changed");

  std::printf("mm3d scene environment profile tests passed: %d checks\n", gChecks);
  return 0;
} catch (const std::exception &error) {
  std::fprintf(stderr, "mm3d scene environment profile tests FAILED: %s\n", error.what());
  return 1;
}
