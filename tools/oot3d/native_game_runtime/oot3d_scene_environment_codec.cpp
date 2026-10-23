#include "oot3d_scene_environment_codec.h"
#include "fast/renderer3ds/point_shadow_map.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <numbers>

namespace Oot3dNativeGame {
namespace {
using Json = nlohmann::json;
using KeyDirection = Fast::Renderer3ds::PicaEnvironmentKeyDirection;
constexpr size_t kPlacedLightNameLength = 64U;
constexpr float kPlacedLightMaximumIntensity = 16.0F;
constexpr float kPlacedLightMaximumCoordinate = 100000.0F;
constexpr float kPlacedLightMaximumSize = 20000.0F;
// Point-shadow cube maps, shared with Navi and the fairies.
constexpr size_t kPointShadowRows = Fast::Renderer3ds::kMaximumPointShadowLights;
constexpr const char *kPlacedLightTypeNames[] = {"sun", "spot", "point", "surface", "omni"};
constexpr const char *kPlacedLightShapeNames[] = {"rectangle", "disc"};
constexpr const char *kRimScopeNames[] = {"actors", "actorsAndWorld"};
constexpr const char *kRimSourceNames[] = {"independent", "sceneLights"};
constexpr float kRadians = std::numbers::pi_v<float> / 180.0F;

std::string Join(const std::string &pointer, const std::string &key) { return pointer + "/" + key; }
std::string Join(const std::string &pointer, size_t index) { return pointer + "/" + std::to_string(index); }

// A whole number within low..high: an integer, or an integral float (5.0).
std::optional<int64_t> WholeNumber(const Json &value, int64_t low, int64_t high) {
  if (value.is_number_unsigned()) {
    const auto result = value.get<uint64_t>();
    if (result <= uint64_t(INT64_MAX) && int64_t(result) >= low && int64_t(result) <= high) return int64_t(result);
    return std::nullopt;
  }
  if (value.is_number_integer()) {
    const auto result = value.get<int64_t>();
    if (result >= low && result <= high) return result;
    return std::nullopt;
  }
  if (value.is_number_float()) {
    const double result = value.get<double>();
    if (std::isfinite(result) && std::floor(result) == result && result >= double(low) && result <= double(high))
      return static_cast<int64_t>(result);
  }
  return std::nullopt;
}

std::string RangeText(float low, float high) {
  char text[64];
  std::snprintf(text, sizeof(text), "a number within %g..%g", low, high);
  return text;
}

// Reads values of one document, recording each problem at its pointer and
// returning a harmless default so the rest is still checked.
class Reader {
public:
  std::vector<SceneEnvironmentDiagnostic> Errors;

  void Error(std::string pointer, std::string message) { Errors.push_back({std::move(pointer), std::move(message)}); }

  const Json *Member(const Json &object, const std::string &pointer, const char *key) {
    if (!object.is_object()) return nullptr;
    const auto found = object.find(key);
    if (found == object.end()) {
      Error(Join(pointer, key), "is required");
      return nullptr;
    }
    return &*found;
  }
  float Scalar(const Json &object, const std::string &pointer, const char *key, float low, float high,
               float fallback = 0.0F) {
    const auto *value = Member(object, pointer, key);
    return value != nullptr ? Number(*value, Join(pointer, key), low, high, fallback) : fallback;
  }
  float Number(const Json &value, const std::string &pointer, float low, float high, float fallback) {
    if (!value.is_number()) {
      Error(pointer, "must be " + RangeText(low, high));
      return fallback;
    }
    const float result = value.get<float>();
    if (!std::isfinite(result) || result < low || result > high) {
      Error(pointer, "is " + value.dump() + "; must be " + RangeText(low, high));
      return fallback;
    }
    return result;
  }
  bool Boolean(const Json &object, const std::string &pointer, const char *key) {
    const auto *value = Member(object, pointer, key);
    if (value == nullptr) return false;
    if (!value->is_boolean()) {
      Error(Join(pointer, key), "must be true or false");
      return false;
    }
    return value->get<bool>();
  }
  std::string String(const Json &object, const std::string &pointer, const char *key) {
    const auto *value = Member(object, pointer, key);
    if (value == nullptr) return {};
    if (!value->is_string()) {
      Error(Join(pointer, key), "must be a string");
      return {};
    }
    return value->get<std::string>();
  }
  std::array<float, 3> Triple(const Json &object, const std::string &pointer, const char *key, float low,
                              float high) {
    std::array<float, 3> result{};
    const auto *values = Member(object, pointer, key);
    if (values == nullptr) return result;
    const auto path = Join(pointer, key);
    if (!values->is_array() || values->size() != 3U) {
      Error(path, "must be an array of three numbers within " + RangeText(low, high).substr(13));
      return result;
    }
    for (size_t i = 0; i < 3U; ++i) result[i] = Number((*values)[i], Join(path, i), low, high, 0.0F);
    return result;
  }
  template <class Enum, size_t Count>
  Enum Name(const Json &object, const std::string &pointer, const char *key, const char *const (&names)[Count]) {
    const auto value = String(object, pointer, key);
    for (size_t i = 0; i < Count; ++i)
      if (value == names[i]) return static_cast<Enum>(i);
    if (object.is_object() && object.contains(key) && object.at(key).is_string()) {
      std::string allowed;
      for (size_t i = 0; i < Count; ++i) allowed += (i == 0 ? "\"" : ", \"") + std::string(names[i]) + "\"";
      Error(Join(pointer, key), "is \"" + value + "\"; must be one of " + allowed);
    }
    return static_cast<Enum>(0);
  }
};

PlacedLight DecodePlacedLight(Reader &read, const Json &object, const std::string &pointer) {
  PlacedLight light;
  if (!object.is_object()) {
    read.Error(pointer, "must be an object");
    return light;
  }
  if (const auto *id = read.Member(object, pointer, "id")) {
    // A whole number 1..4294967295: no fraction, no sign, no overflow.
    if (const auto value = WholeNumber(*id, 1, int64_t{UINT32_MAX}))
      light.Id = static_cast<uint32_t>(*value);
    else
      read.Error(Join(pointer, "id"), "must be a whole number within 1..4294967295");
  }
  light.Name = read.String(object, pointer, "name");
  light.Type = read.Name<PlacedLightType>(object, pointer, "type", kPlacedLightTypeNames);
  light.Visible = read.Boolean(object, pointer, "visible");
  light.Color = read.Triple(object, pointer, "color", 0.0F, 1.0F);
  light.Intensity = read.Scalar(object, pointer, "intensity", 0.0F, kPlacedLightMaximumIntensity);
  light.Position =
      read.Triple(object, pointer, "position", -kPlacedLightMaximumCoordinate, kPlacedLightMaximumCoordinate);
  light.Rotation = read.Triple(object, pointer, "rotation", -180.0F, 180.0F);
  light.CastShadows = read.Boolean(object, pointer, "castShadows");
  light.Radius = read.Scalar(object, pointer, "radius", 1.0F, kPlacedLightMaximumSize, 1.0F);
  light.Width = read.Scalar(object, pointer, "width", 1.0F, kPlacedLightMaximumSize, 1.0F);
  light.Height = read.Scalar(object, pointer, "height", 1.0F, kPlacedLightMaximumSize, 1.0F);
  light.Shape = read.Name<PlacedLightShape>(object, pointer, "shape", kPlacedLightShapeNames);
  light.TwoSided = read.Boolean(object, pointer, "twoSided");
  // Optional: lights saved before it are diffuse surfaces.
  if (object.contains("spread")) light.Spread = read.Scalar(object, pointer, "spread", 1.0F, 180.0F, 180.0F);
  light.BeamAngle = read.Scalar(object, pointer, "beamAngle", 1.0F, 179.0F, 1.0F);
  light.BeamSoftness = read.Scalar(object, pointer, "beamSoftness", 0.0F, 1.0F);
  light.Range = read.Scalar(object, pointer, "range", 1.0F, kPlacedLightMaximumSize, 1.0F);
  return light;
}

// The float's shortest decimal form, as a JSON number.
Json Number(float value) {
  char text[32];
  const auto result = std::to_chars(text, text + sizeof(text), value);
  *result.ptr = '\0';
  return Json(std::strtod(text, nullptr));
}
Json Triple(const std::array<float, 3> &value) { return Json::array({Number(value[0]), Number(value[1]), Number(value[2])}); }

Json EncodePlacedLight(const PlacedLight &light) {
  return {{"id", light.Id},
          {"name", light.Name},
          {"type", kPlacedLightTypeNames[static_cast<size_t>(light.Type)]},
          {"visible", light.Visible},
          {"color", Triple(light.Color)},
          {"intensity", Number(light.Intensity)},
          {"position", Triple(light.Position)},
          {"rotation", Triple(light.Rotation)},
          {"castShadows", light.CastShadows},
          {"radius", Number(light.Radius)},
          {"width", Number(light.Width)},
          {"height", Number(light.Height)},
          {"shape", kPlacedLightShapeNames[static_cast<size_t>(light.Shape)]},
          {"twoSided", light.TwoSided},
          {"spread", Number(light.Spread)},
          {"beamAngle", Number(light.BeamAngle)},
          {"beamSoftness", Number(light.BeamSoftness)},
          {"range", Number(light.Range)}};
}

const char *EncodeKeyDirection(KeyDirection value) {
  switch (value) {
  case KeyDirection::SceneMain: return "scene_main";
  case KeyDirection::SceneAmbient: return "scene_ambient";
  case KeyDirection::Authored: break;
  }
  return "authored";
}

Json DirectionJson(const std::array<float, 3> &value) { return Triple(value); }
} // namespace

std::vector<SceneEnvironmentDiagnostic> ValidateSceneEnvironment(const SceneEnvironmentProfile &profile) {
  Reader read;
  const auto scalar = [&](float value, const std::string &pointer, float low, float high) {
    if (!std::isfinite(value) || value < low || value > high) read.Error(pointer, "must be " + RangeText(low, high));
  };
  const auto triple = [&](const std::array<float, 3> &value, const std::string &pointer, float low, float high) {
    for (size_t i = 0; i < 3U; ++i) scalar(value[i], Join(pointer, i), low, high);
  };
  scalar(profile.NativeDirectStrength, "/nativeDirectStrength", 0.0F, 2.0F);
  scalar(profile.NativeLocalStrength, "/nativeLocalStrength", 0.0F, 2.0F);
  scalar(profile.NativeAmbientStrength, "/nativeAmbientStrength", 0.0F, 2.0F);
  scalar(profile.AmbientIntensity, "/ambientIntensity", 0.0F, 2.0F);
  scalar(profile.FogStrength, "/fogStrength", 0.0F, 1.0F);
  scalar(profile.Rim.Intensity, "/rimLight/intensity", 0.0F, Fast::Renderer3ds::kMaximumPicaRimIntensity);
  scalar(profile.Rim.Sharpness, "/rimLight/sharpness", 1.0F, 16.0F);
  scalar(profile.Rim.Azimuth, "/rimLight/azimuth", -180.0F, 180.0F);
  scalar(profile.Rim.Coverage, "/rimLight/coverage", 0.0F, 1.0F);
  triple(profile.Rim.Color, "/rimLight/color", 0.0F, 1.0F);
  if (profile.Rim.Source > Fast::Renderer3ds::PicaRimLightSource::SceneLights)
    read.Error("/rimLight/source", "is unknown");
  if (profile.Rim.Scope > Fast::Renderer3ds::PicaRimLightScope::ActorsAndWorld)
    read.Error("/rimLight/scope", "is unknown");
  triple(profile.AmbientColor, "/ambientColor", 0.0F, 1.0F);
  for (size_t i = 0; i < profile.Lights.size(); ++i) {
    const auto &light = profile.Lights[i];
    const auto pointer = Join("/directionalLights", i);
    scalar(light.Azimuth, pointer + "/azimuth", -180.0F, 180.0F);
    scalar(light.Elevation, pointer + "/elevation", -90.0F, 90.0F);
    scalar(light.Intensity, pointer + "/intensity", 0.0F, 4.0F);
    triple(light.Color, pointer + "/color", 0.0F, 1.0F);
  }
  scalar(profile.KeyAzimuthOffset, "/keyLightAzimuthOffset", -180.0F, 180.0F);
  scalar(profile.KeyElevationOffset, "/keyLightElevationOffset", -90.0F, 90.0F);
  if (profile.KeyDirection > KeyDirection::SceneAmbient) read.Error("/keyLightDirection", "is unknown");
  if (profile.CasterFaces > Fast::Renderer3ds::ShadowCasterFaces::InteriorShells)
    read.Error("/shadowCasterFaces", "is unknown");
  for (size_t i = 0; i < profile.PointLights.size(); ++i) {
    const auto &light = profile.PointLights[i];
    const auto pointer = Join("/pointLights", i);
    scalar(light.Intensity, pointer + "/intensity", 0.0F, 8.0F);
    scalar(light.RadiusScale, pointer + "/radiusScale", 0.1F, 8.0F);
    triple(light.ColorTint, pointer + "/colorTint", 0.0F, 1.0F);
  }
  if (profile.PlacedLights.size() > kMaximumPlacedLights)
    read.Error("/placedLights", "has " + std::to_string(profile.PlacedLights.size()) + " lights; at most " +
                                    std::to_string(kMaximumPlacedLights));
  for (size_t i = 0; i < profile.PlacedLights.size(); ++i) {
    const auto &light = profile.PlacedLights[i];
    const auto pointer = Join("/placedLights", i);
    if (light.Id == 0U) read.Error(pointer + "/id", "must be a whole number within 1..4294967295");
    for (size_t j = 0; j < i; ++j)
      if (profile.PlacedLights[j].Id == light.Id && light.Id != 0U)
        read.Error(pointer + "/id", "repeats the id of /placedLights/" + std::to_string(j));
    if (light.Name.size() > kPlacedLightNameLength)
      read.Error(pointer + "/name", "is longer than " + std::to_string(kPlacedLightNameLength) + " bytes");
    if (light.Type > PlacedLightType::Omni) read.Error(pointer + "/type", "is unknown");
    if (light.Shape > PlacedLightShape::Disc) read.Error(pointer + "/shape", "is unknown");
    triple(light.Color, pointer + "/color", 0.0F, 1.0F);
    scalar(light.Intensity, pointer + "/intensity", 0.0F, kPlacedLightMaximumIntensity);
    triple(light.Position, pointer + "/position", -kPlacedLightMaximumCoordinate, kPlacedLightMaximumCoordinate);
    scalar(light.Rotation[0], pointer + "/rotation/0", -180.0F, 180.0F);
    scalar(light.Rotation[1], pointer + "/rotation/1", -90.0F, 90.0F);
    scalar(light.Rotation[2], pointer + "/rotation/2", -180.0F, 180.0F);
    scalar(light.Radius, pointer + "/radius", 1.0F, kPlacedLightMaximumSize);
    scalar(light.Width, pointer + "/width", 1.0F, kPlacedLightMaximumSize);
    scalar(light.Height, pointer + "/height", 1.0F, kPlacedLightMaximumSize);
    scalar(light.Range, pointer + "/range", 1.0F, kPlacedLightMaximumSize);
    scalar(light.BeamAngle, pointer + "/beamAngle", 1.0F, 179.0F);
    scalar(light.BeamSoftness, pointer + "/beamSoftness", 0.0F, 1.0F);
    scalar(light.Spread, pointer + "/spread", 1.0F, 180.0F);
  }
  return read.Errors;
}

std::string DescribeSceneEnvironmentErrors(const std::vector<SceneEnvironmentDiagnostic> &errors) {
  if (errors.empty()) return {};
  std::string text = (errors.front().Pointer.empty() ? std::string("document") : errors.front().Pointer) + ": " +
                     errors.front().Message;
  if (errors.size() > 1U) text += " (and " + std::to_string(errors.size() - 1U) + " more)";
  return text;
}

SceneEnvironmentDocument DecodeSceneEnvironment(const Json &doc, std::optional<uint16_t> expectedScene,
                                                std::optional<bool> expectedNight, std::optional<SceneEnvironmentGroup> expectedGroup) {
  SceneEnvironmentDocument out;
  Reader read;
  if (!doc.is_object()) {
    out.Errors.push_back({"", "must be a JSON object"});
    return out;
  }
  if (read.String(doc, "", "schema") != "triaevum.scene-environment" && doc.contains("schema"))
    read.Error("/schema", "must be \"triaevum.scene-environment\"");
  int version = 0;
  if (const auto *value = read.Member(doc, "", "version")) {
    if (const auto number = WholeNumber(*value, 1, 6))
      version = static_cast<int>(*number);
    else
      read.Error("/version", "must be a whole number within 1..6");
  }
  if (doc.contains("groupId")) {
    out.Group = ParseEnvironmentGroup(read.String(doc, "", "groupId"));
    if (!out.Group) read.Error("/groupId", "must name a supported environment group");
    if (version != 6) read.Error("/version", "group profiles require version 6");
    if (doc.contains("sceneId")) read.Error("/sceneId", "group profiles must not name a scene");
    if (expectedScene) read.Error("/groupId", "a scene file cannot contain a group standard");
  } else if (const auto *value = read.Member(doc, "", "sceneId")) {
    if (const auto number = WholeNumber(*value, 0, 0xFFFF))
      out.SceneId = static_cast<uint16_t>(*number);
    else
      read.Error("/sceneId", "must be a whole number within 0..65535");
  }
  if (out.SceneId && expectedScene && *out.SceneId != *expectedScene)
    read.Error("/sceneId", "names scene " + std::to_string(*out.SceneId) + "; the file is scene " +
                               std::to_string(*expectedScene) + "'s");
  if (expectedGroup && out.Group != expectedGroup)
    read.Error("/groupId", "must match the group named by the file");
  // A night profile names its time of day; the scene's own profile does not.
  if (const auto time = doc.find("timeOfDay"); time != doc.end()) {
    if (!time->is_string() || (*time != "day" && *time != "night"))
      read.Error("/timeOfDay", "must be \"day\" or \"night\"");
    else
      out.Night = *time == "night";
  }
  if (expectedNight && out.Night != *expectedNight)
    read.Error("/timeOfDay", *expectedNight ? "must be \"night\" in a night profile"
                                            : "must be \"day\" (or absent) in a scene's own profile");
  SceneEnvironmentProfile result;
  result.Enabled = read.Boolean(doc, "", "enabled");
  // Legacy authored profiles retain their explicit black/ambient choices.
  // Loading never rewrites them; new profiles use the scene reference.
  result.PreserveSceneBrightness = version >= 4 ? read.Boolean(doc, "", "preserveSceneBrightness") : false;
  result.NativeDirectStrength = read.Scalar(doc, "", "nativeDirectStrength", 0.0F, 2.0F);
  // Older profiles used one gain for every selected native direct source.
  result.NativeLocalStrength =
      version >= 3 ? read.Scalar(doc, "", "nativeLocalStrength", 0.0F, 2.0F) : result.NativeDirectStrength;
  result.NativeAmbientStrength = read.Scalar(doc, "", "nativeAmbientStrength", 0.0F, 2.0F);
  result.AmbientColor = read.Triple(doc, "", "ambientColor", 0.0F, 1.0F);
  result.AmbientIntensity = read.Scalar(doc, "", "ambientIntensity", 0.0F, 2.0F);
  result.FogStrength = read.Scalar(doc, "", "fogStrength", 0.0F, 1.0F);
  if (const auto *lights = read.Member(doc, "", "directionalLights")) {
    if (!lights->is_array() || lights->size() != result.Lights.size()) {
      read.Error("/directionalLights", "must be an array of two lights: the key light and the fill light");
    } else {
      for (size_t i = 0; i < result.Lights.size(); ++i) {
        auto &light = result.Lights[i];
        const auto pointer = Join("/directionalLights", i);
        if (!(*lights)[i].is_object()) {
          read.Error(pointer, "must be an object");
          continue;
        }
        light.Azimuth = read.Scalar((*lights)[i], pointer, "azimuth", -180.0F, 180.0F);
        light.Elevation = read.Scalar((*lights)[i], pointer, "elevation", -90.0F, 90.0F);
        light.Intensity = read.Scalar((*lights)[i], pointer, "intensity", 0.0F, 4.0F);
        light.Color = read.Triple((*lights)[i], pointer, "color", 0.0F, 1.0F);
      }
    }
  }
  if (version >= 5) {
    const auto key = read.String(doc, "", "keyLightDirection");
    if (key == "authored") result.KeyDirection = KeyDirection::Authored;
    else if (key == "scene_main") result.KeyDirection = KeyDirection::SceneMain;
    else if (key == "scene_ambient") result.KeyDirection = KeyDirection::SceneAmbient;
    else if (doc.contains("keyLightDirection") && doc.at("keyLightDirection").is_string())
      read.Error("/keyLightDirection", "is \"" + key + "\"; must be one of \"authored\", \"scene_main\", \"scene_ambient\"");
    result.KeyAzimuthOffset = read.Scalar(doc, "", "keyLightAzimuthOffset", -180.0F, 180.0F);
    result.KeyElevationOffset = read.Scalar(doc, "", "keyLightElevationOffset", -90.0F, 90.0F);
  }
  // New enum takes precedence; older profiles retain their exact policy.
  using Faces = Fast::Renderer3ds::ShadowCasterFaces;
  if (doc.contains("shadowCasterFaces")) {
    const auto faces = read.String(doc, "", "shadowCasterFaces");
    if (faces == "native") result.CasterFaces = Faces::Native;
    else if (faces == "both") result.CasterFaces = Faces::BothFaces;
    else if (faces == "interiorShells") result.CasterFaces = Faces::InteriorShells;
    else read.Error("/shadowCasterFaces", "must be native, both or interiorShells");
  } else if (doc.contains("shadowCasterCulling")) {
    result.CasterFaces = read.Boolean(doc, "", "shadowCasterCulling") ? Faces::Native : Faces::BothFaces;
  }
  if (version >= 2) {
    if (const auto *points = read.Member(doc, "", "pointLights")) {
      // Profiles from before the flames group list two; it keeps its defaults.
      if (!points->is_array() || points->size() < 2U || points->size() > result.PointLights.size()) {
        read.Error("/pointLights", "must be an array of 2 or " + std::to_string(result.PointLights.size()) +
                                       " point-light groups (Navi, fairies, flames, architecture)");
      } else {
        for (size_t i = 0; i < points->size(); ++i) {
          auto &light = result.PointLights[i];
          const auto pointer = Join("/pointLights", i);
          if (!(*points)[i].is_object()) {
            read.Error(pointer, "must be an object");
            continue;
          }
          light.Enabled = read.Boolean((*points)[i], pointer, "enabled");
          light.Intensity = read.Scalar((*points)[i], pointer, "intensity", 0.0F, 8.0F);
          light.RadiusScale = read.Scalar((*points)[i], pointer, "radiusScale", 0.1F, 8.0F, 1.0F);
          light.ColorTint = read.Triple((*points)[i], pointer, "colorTint", 0.0F, 1.0F);
        }
      }
    }
  }
  // Optional extension: old profiles retain their exact rim-off behavior.
  if (const auto rim = doc.find("rimLight"); rim != doc.end()) {
    if (!rim->is_object()) {
      read.Error("/rimLight", "must be an object");
    } else {
      result.Rim.Enabled = read.Boolean(*rim, "/rimLight", "enabled");
      result.Rim.Intensity = read.Scalar(*rim, "/rimLight", "intensity", 0.0F,
                                         Fast::Renderer3ds::kMaximumPicaRimIntensity);
      result.Rim.Sharpness = read.Scalar(*rim, "/rimLight", "sharpness", 1.0F, 16.0F, 4.0F);
      // Round-27 objects need no new members. Ambient is accepted/validated
      // for migration but is no longer a rendering control or encoded field.
      if (rim->contains("ambient")) (void)read.Scalar(*rim, "/rimLight", "ambient", 0.0F, 0.25F);
      if (rim->contains("source"))
        result.Rim.Source = read.Name<Fast::Renderer3ds::PicaRimLightSource>(*rim, "/rimLight", "source", kRimSourceNames);
      if (rim->contains("color")) result.Rim.Color = read.Triple(*rim, "/rimLight", "color", 0.0F, 1.0F);
      if (rim->contains("azimuth")) result.Rim.Azimuth = read.Scalar(*rim, "/rimLight", "azimuth", -180.0F, 180.0F);
      if (rim->contains("coverage")) result.Rim.Coverage = read.Scalar(*rim, "/rimLight", "coverage", 0.0F, 1.0F);
      result.Rim.Scope = read.Name<Fast::Renderer3ds::PicaRimLightScope>(*rim, "/rimLight", "scope", kRimScopeNames);
      for (auto it = rim->begin(); it != rim->end(); ++it)
        if (it.key() != "enabled" && it.key() != "intensity" && it.key() != "sharpness" &&
            it.key() != "ambient" && it.key() != "scope" && it.key() != "source" &&
            it.key() != "color" && it.key() != "azimuth" && it.key() != "coverage")
          read.Error(Join("/rimLight", it.key()), "is unknown");
    }
  }
  // Optional in version 5: a profile without placed lights omits them, and
  // a runtime that predates them ignores the field.
  if (const auto placed = doc.find("placedLights"); placed != doc.end()) {
    if (!placed->is_array()) {
      read.Error("/placedLights", "must be an array");
    } else {
      for (size_t i = 0; i < placed->size(); ++i)
        result.PlacedLights.push_back(DecodePlacedLight(read, (*placed)[i], Join("/placedLights", i)));
    }
  }
  out.Errors = std::move(read.Errors);
  // Identities, counts and names once every value read; with errors already,
  // only the repeated identities (the values were reported as read).
  if (out.Errors.empty()) {
    out.Errors = ValidateSceneEnvironment(result);
    if (out.Errors.empty()) out.Profile = std::move(result);
  } else {
    // The values that failed to read hold placeholders: report the other
    // problems (counts, names, repeated ids) at pointers not reported yet.
    const auto reported = out.Errors;
    for (auto &error : ValidateSceneEnvironment(result)) {
      const bool known = std::any_of(reported.begin(), reported.end(), [&](const auto &existing) {
        return error.Pointer == existing.Pointer || error.Pointer.starts_with(existing.Pointer + "/");
      });
      if (!known) out.Errors.push_back(std::move(error));
    }
  }
  return out;
}

SceneEnvironmentFile LoadSceneEnvironmentFile(const std::filesystem::path &path,
                                              std::optional<uint16_t> expectedScene,
                                              std::optional<bool> expectedNight, std::optional<SceneEnvironmentGroup> expectedGroup) {
  SceneEnvironmentFile file;
  std::error_code error;
  file.Exists = std::filesystem::exists(path, error);
  if (error) {
    file.Exists = true; // Unreadable is not absent; do not silently fall back.
    file.Document.Errors.push_back({"", "cannot inspect file: " + error.message()});
    return file;
  }
  if (!file.Exists) {
    file.Document.Errors.push_back({"", "does not exist"});
    return file;
  }
  const auto size = std::filesystem::file_size(path, error);
  if (error || size > kMaximumSceneEnvironmentFileBytes) {
    file.Document.Errors.push_back({"", error ? "cannot be read" : "exceeds 64 KiB"});
    return file;
  }
  std::ifstream stream(path, std::ios::binary);
  std::ostringstream text;
  text << stream.rdbuf();
  if (!stream) {
    file.Document.Errors.push_back({"", "cannot be read"});
    return file;
  }
  file.Text = text.str();
  try {
    file.Document = DecodeSceneEnvironment(Json::parse(file.Text), expectedScene, expectedNight, expectedGroup);
  } catch (const Json::parse_error &parse) {
    file.Document.Errors.push_back({"", std::string("is not valid JSON: ") + parse.what()});
  }
  return file;
}

Json EncodeGroupEnvironment(const SceneEnvironmentProfile& profile, SceneEnvironmentGroup group, bool night) {
  auto result = EncodeSceneEnvironment(profile, 0, night);
  result.erase("sceneId");
  result["version"] = 6;
  result["groupId"] = EnvironmentGroupId(group);
  return result;
}

Json EncodeSceneEnvironment(const SceneEnvironmentProfile &value, uint16_t scene, bool night) {
  Json lights = Json::array();
  for (const auto &light : value.Lights)
    lights.push_back({{"azimuth", Number(light.Azimuth)},
                      {"elevation", Number(light.Elevation)},
                      {"color", Triple(light.Color)},
                      {"intensity", Number(light.Intensity)}});
  Json points = Json::array();
  for (const auto &light : value.PointLights)
    points.push_back({{"enabled", light.Enabled},
                      {"intensity", Number(light.Intensity)},
                      {"radiusScale", Number(light.RadiusScale)},
                      {"colorTint", Triple(light.ColorTint)}});
  Json result = {{"schema", "triaevum.scene-environment"},
                 {"version", 5},
                 {"sceneId", scene},
                 {"enabled", value.Enabled},
                 {"preserveSceneBrightness", value.PreserveSceneBrightness},
                 {"nativeDirectStrength", Number(value.NativeDirectStrength)},
                 {"nativeLocalStrength", Number(value.NativeLocalStrength)},
                 {"nativeAmbientStrength", Number(value.NativeAmbientStrength)},
                 {"ambientColor", Triple(value.AmbientColor)},
                 {"ambientIntensity", Number(value.AmbientIntensity)},
                 {"fogStrength", Number(value.FogStrength)},
                 {"rimLight", {{"enabled", value.Rim.Enabled}, {"intensity", Number(value.Rim.Intensity)},
                               {"sharpness", Number(value.Rim.Sharpness)}, {"color", Triple(value.Rim.Color)},
                               {"azimuth", Number(value.Rim.Azimuth)}, {"coverage", Number(value.Rim.Coverage)},
                               {"source", value.Rim.Source == Fast::Renderer3ds::PicaRimLightSource::SceneLights
                                              ? "sceneLights" : "independent"},
                               {"scope", value.Rim.Scope == Fast::Renderer3ds::PicaRimLightScope::ActorsAndWorld
                                             ? "actorsAndWorld" : "actors"}}},
                 {"pointLights", std::move(points)},
                 {"directionalLights", std::move(lights)},
                 {"keyLightDirection", EncodeKeyDirection(value.KeyDirection)},
                 {"keyLightAzimuthOffset", Number(value.KeyAzimuthOffset)},
                 {"keyLightElevationOffset", Number(value.KeyElevationOffset)},
                 {"shadowCasterFaces", value.CasterFaces == Fast::Renderer3ds::ShadowCasterFaces::Native ? "native" :
                      value.CasterFaces == Fast::Renderer3ds::ShadowCasterFaces::BothFaces ? "both" : "interiorShells"}};
  if (night) result["timeOfDay"] = "night";
  if (!value.PlacedLights.empty()) {
    Json placed = Json::array();
    for (const auto &light : value.PlacedLights) placed.push_back(EncodePlacedLight(light));
    result["placedLights"] = std::move(placed);
  }
  return result;
}

std::string FormatSceneEnvironment(const Json &document) { return document.dump(2) + "\n"; }

uint64_t PlacedLightIdentity(uint16_t scene, bool night, uint32_t light) {
  // Tag, time of day, scene, light: the light id keeps all 32 bits.
  return 0xD1A0000000000000ULL | (night ? 1ULL << 48U : 0ULL) | (uint64_t(scene) << 32U) | light;
}

Fast::Renderer3ds::PicaPlacedLight ResolvePlacedLight(const PlacedLight &light, uint64_t identity) {
  using Kind = Fast::Renderer3ds::PicaPlacedLightKind;
  Fast::Renderer3ds::PicaPlacedLight result;
  result.SourceIdentity = identity;
  switch (light.Type) {
  case PlacedLightType::Sun: result.Kind = Kind::Sun; break;
  case PlacedLightType::Spot: result.Kind = Kind::Spot; break;
  case PlacedLightType::Point: result.Kind = Kind::Point; break;
  case PlacedLightType::Omni: result.Kind = Kind::Omni; break;
  case PlacedLightType::Surface:
    result.Kind = light.Shape == PlacedLightShape::Disc ? Kind::Disc : Kind::Rectangle;
    break;
  }
  result.PositionWorld = light.Position;
  const float yaw = light.Rotation[0] * kRadians;
  const float pitch = light.Rotation[1] * kRadians;
  const float roll = light.Rotation[2] * kRadians;
  // Yaw 0 faces +Z and 90 faces +X (the game's yaw); pitch 90 points up.
  const std::array<float, 3> axis{std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                                  std::cos(yaw) * std::cos(pitch)};
  // The horizontal axis to the light's right, turned by the roll.
  const std::array<float, 3> right{std::cos(yaw), 0.0F, -std::sin(yaw)};
  const std::array<float, 3> up{axis[1] * right[2] - axis[2] * right[1],
                                axis[2] * right[0] - axis[0] * right[2],
                                axis[0] * right[1] - axis[1] * right[0]};
  result.AxisWorld = axis;
  for (size_t i = 0; i < 3; ++i) {
    result.TangentWorld[i] = right[i] * std::cos(roll) + up[i] * std::sin(roll);
    result.Color[i] = light.Color[i] * light.Intensity;
  }
  result.Reach = light.Type == PlacedLightType::Surface ? light.Range : light.Radius;
  if (light.Type == PlacedLightType::Surface) {
    result.HalfWidth = light.Shape == PlacedLightShape::Disc ? light.Radius : light.Width * 0.5F;
    result.HalfHeight = light.Shape == PlacedLightShape::Disc ? light.Radius : light.Height * 0.5F;
    result.TwoSided = light.TwoSided;
    result.Spread = light.Spread;
  }
  const float halfAngle = light.BeamAngle * 0.5F * kRadians;
  result.CosOuter = std::cos(halfAngle);
  result.CosInner = std::cos(halfAngle * (1.0F - light.BeamSoftness));
  result.CastShadows = light.CastShadows && light.Type != PlacedLightType::Sun;
  return result;
}

Fast::Renderer3ds::PicaEnvironmentLighting ResolveSceneEnvironment(const SceneEnvironmentProfile &value,
                                                                   uint16_t scene, bool night) {
  Fast::Renderer3ds::PicaEnvironmentLighting result;
  // Day and night profiles are separate scopes (no shared shadow history).
  result.ScopeIdentity = uint64_t(scene) + 1 + (night ? 0x10000U : 0U);
  result.Enabled = value.Enabled;
  result.PreserveSceneBrightness = value.PreserveSceneBrightness;
  result.NativeDirectStrength = value.NativeDirectStrength;
  result.NativeLocalStrength = value.NativeLocalStrength;
  result.NativeAmbientStrength = value.NativeAmbientStrength;
  result.AmbientColor = value.AmbientColor;
  result.AmbientIntensity = value.AmbientIntensity;
  result.FogStrength = value.FogStrength;
  result.Rim = value.Rim;
  result.PointLights = value.PointLights;
  for (const auto &light : value.PlacedLights) {
    if (!light.Visible || result.PlacedLightCount == result.PlacedLights.size()) continue;
    result.PlacedLights[result.PlacedLightCount++] =
        ResolvePlacedLight(light, PlacedLightIdentity(scene, night, light.Id));
  }
  result.CasterFaces = value.CasterFaces;
  result.KeyDirection = value.KeyDirection;
  result.KeyAzimuthOffset = value.KeyAzimuthOffset * kRadians;
  result.KeyElevationOffset = value.KeyElevationOffset * kRadians;
  // The same axes as the authored azimuth/elevation below.
  result.WorldUp = {0.0F, 1.0F, 0.0F};
  result.WorldAzimuthZero = {0.0F, 0.0F, 1.0F};
  for (size_t i = 0; i < value.Lights.size(); ++i) {
    const auto &light = value.Lights[i];
    const float azimuth = light.Azimuth * kRadians;
    const float elevation = light.Elevation * kRadians;
    // OOT3D world is Y-up. Zero azimuth points toward +Z; +90 toward +X.
    result.Lights[i] = {{std::sin(azimuth) * std::cos(elevation), std::sin(elevation),
                         std::cos(azimuth) * std::cos(elevation)},
                        light.Color,
                        light.Intensity};
  }
  return result;
}

Json ExplainSceneEnvironment(const SceneEnvironmentProfile &profile, uint16_t scene, bool night) {
  const auto resolved = ResolveSceneEnvironment(profile, scene, night);
  Json groups = Json::array();
  static constexpr const char *kGroups[] = {"navi", "fairies", "flames"};
  for (size_t i = 0; i < resolved.PointLights.size(); ++i)
    groups.push_back({{"group", i < std::size(kGroups) ? kGroups[i] : "group"},
                      {"enabled", resolved.PointLights[i].Enabled},
                      {"intensity", Number(resolved.PointLights[i].Intensity)},
                      {"radiusScale", Number(resolved.PointLights[i].RadiusScale)},
                      {"colorTint", Triple(resolved.PointLights[i].ColorTint)}});
  Json renderer = {{"enabled", resolved.Enabled},
                   {"rimLight", EncodeSceneEnvironment(profile, scene, night).at("rimLight")},
                   {"scopeIdentity", std::to_string(resolved.ScopeIdentity)},
                   {"scenePointLightGroups", std::move(groups)},
                   {"preserveSceneBrightness", resolved.PreserveSceneBrightness},
                   {"nativeStrengths",
                    {{"direct", Number(resolved.NativeDirectStrength)},
                     {"local", Number(resolved.NativeLocalStrength)},
                     {"ambient", Number(resolved.NativeAmbientStrength)}}},
                   {"ambient", {{"color", Triple(resolved.AmbientColor)}, {"intensity", Number(resolved.AmbientIntensity)}}},
                   {"fogStrength", Number(resolved.FogStrength)},
                   {"shadowCasterFaces", resolved.CasterFaces == Fast::Renderer3ds::ShadowCasterFaces::Native ? "native" :
                      resolved.CasterFaces == Fast::Renderer3ds::ShadowCasterFaces::BothFaces ? "both" : "interiorShells"},
                   {"keyLight",
                    {{"follows", EncodeKeyDirection(resolved.KeyDirection)},
                     {"offsetDegrees", Json::array({Number(profile.KeyAzimuthOffset), Number(profile.KeyElevationOffset)})},
                     {resolved.KeyDirection == KeyDirection::Authored ? "directionTowardSource"
                                                                      : "fallbackDirectionTowardSource",
                      DirectionJson(resolved.Lights[0].DirectionTowardSource)},
                     {"color", Triple(resolved.Lights[0].Color)},
                     {"intensity", Number(resolved.Lights[0].Intensity)}}},
                   {"fillLight",
                    {{"directionTowardSource", DirectionJson(resolved.Lights[1].DirectionTowardSource)},
                     {"color", Triple(resolved.Lights[1].Color)},
                     {"intensity", Number(resolved.Lights[1].Intensity)}}}};
  Json placed = Json::array();
  static constexpr const char *kKinds[] = {"omni", "point", "spot", "rectangle", "disc", "sun"};
  for (uint32_t i = 0; i < resolved.PlacedLightCount; ++i) {
    const auto &light = resolved.PlacedLights[i];
    char identity[24];
    std::snprintf(identity, sizeof(identity), "0x%016llX", static_cast<unsigned long long>(light.SourceIdentity));
    Json entry = {{"id", static_cast<uint32_t>(light.SourceIdentity & 0xFFFFFFFFULL)},
                  {"identity", identity},
                  {"kind", kKinds[static_cast<size_t>(light.Kind)]},
                  {"positionWorld", Triple(light.PositionWorld)},
                  {"emissionAxisWorld", DirectionJson(light.AxisWorld)},
                  {"linearColorTimesIntensity", Triple(light.Color)},
                  {"reach", Number(light.Reach)},
                  {"castShadows", light.CastShadows}};
    if (light.Kind == Fast::Renderer3ds::PicaPlacedLightKind::Spot) {
      entry["beamHalfAngleDegrees"] = Number(std::acos(std::clamp(light.CosOuter, -1.0F, 1.0F)) / kRadians);
      entry["coreHalfAngleDegrees"] = Number(std::acos(std::clamp(light.CosInner, -1.0F, 1.0F)) / kRadians);
    }
    if (light.Kind == Fast::Renderer3ds::PicaPlacedLightKind::Rectangle ||
        light.Kind == Fast::Renderer3ds::PicaPlacedLightKind::Disc) {
      entry["widthAxisWorld"] = DirectionJson(light.TangentWorld);
      entry["halfExtents"] = Json::array({Number(light.HalfWidth), Number(light.HalfHeight)});
      entry["twoSided"] = light.TwoSided;
      entry["spreadDegrees"] = Number(light.Spread);
    }
    placed.push_back(std::move(entry));
  }
  renderer["placedLights"] = std::move(placed);

  Json runtime = Json::array();
  if (profile.KeyDirection == KeyDirection::SceneMain)
    runtime.push_back("keyLight: direction, colour and brightness follow the scene's main directional light at run "
                      "time (turned by the offsets; the authored colour tints it); the authored direction applies "
                      "only while the scene has none");
  if (profile.KeyDirection == KeyDirection::SceneAmbient)
    runtime.push_back("keyLight: direction follows the axis of the scene's other directional light at run time; "
                      "colour and brightness follow the scene's main light");
  if (profile.PreserveSceneBrightness)
    runtime.push_back("preserveSceneBrightness: the native low-frequency brightness of each draw is kept as a "
                      "reference independently of the gains above");

  Json notes = Json::array();
  if (!profile.Enabled)
    notes.push_back("environment disabled: no key, fill, ambient or placed light applies; the scene point-light "
                    "groups still apply in the per-pixel lighting modes");
  size_t hidden = 0, shadowing = 0;
  for (const auto &light : profile.PlacedLights) {
    if (!light.Visible) {
      ++hidden;
      continue;
    }
    const bool sun = light.Type == PlacedLightType::Sun;
    if (light.CastShadows && !sun) ++shadowing;
    if (!sun && !light.CastShadows)
      notes.push_back("placed light " + std::to_string(light.Id) + " (" + light.Name +
                      ") casts no shadows: it lights through walls within its reach");
    if (sun && light.CastShadows)
      notes.push_back("placed light " + std::to_string(light.Id) + " is a sun: suns never cast shadows");
  }
  if (hidden != 0U) notes.push_back(std::to_string(hidden) + " placed light(s) hidden: not sent to the renderer");
  if (shadowing != 0U)
    notes.push_back(std::to_string(shadowing) + " placed light(s) compete for " + std::to_string(kPointShadowRows) +
                    " point-shadow cube maps shared with Navi and the fairies; a large surface's shadow is "
                    "approximated from its centre");
  notes.push_back("not resolved offline: the graphics mode and the device's capabilities decide which of these "
                  "apply (the Native surface lighting mode uses none of them)");
  notes.push_back("placed lights apply to the whole scene (all rooms), " + std::to_string(kMaximumPlacedLights) +
                  " at most");
  notes.push_back("units: world units (Link is about 50 tall), linear RGB 0..1, angles in degrees; yaw 0 faces +Z, "
                  "90 faces +X; pitch positive up; directional lights give the direction toward their source");

  return {{"schema", "triaevum.scene-environment-explain"},
          {"version", 1},
          {"sceneId", scene},
          {"timeOfDay", night ? "night" : "day"},
          {"authored", EncodeSceneEnvironment(profile, scene, night)},
          {"renderer", std::move(renderer)},
          {"dependsOnScene", std::move(runtime)},
          {"notes", std::move(notes)}};
}

} // namespace Oot3dNativeGame
