#include "oot3d_native_pica_fragment_lighting_gen.h"

#include "fast/oot3d/pica_fragment_lighting.h"

#include <sstream>

namespace Oot3dNativeGame {
namespace {

constexpr size_t kLutD0 = 0U;
constexpr size_t kLutD1 = 1U;
constexpr size_t kLutSpot = 2U;
constexpr size_t kLutFresnel = 3U;
constexpr size_t kLutReflectBlue = 4U;
constexpr size_t kLutReflectGreen = 5U;
constexpr size_t kLutReflectRed = 6U;

void SetError(std::string *error, const std::string &message) {
  if (error != nullptr) {
    *error = message;
  }
}

std::array<float, 4> ColorVector(const Fast::Oot3d::PicaLightingColor &color) {
  return {color.Rgb[0], color.Rgb[1], color.Rgb[2], 0.0F};
}

uint8_t LutTable(Fast::Oot3d::PicaLightingLutTable table) {
  return static_cast<uint8_t>(table);
}

bool StandardLutEnabled(const Fast::Oot3d::PicaFragmentLightingState &lighting,
                        uint32_t disableBit, uint8_t table) {
  return (lighting.Config1 & (1U << disableBit)) == 0U &&
         Fast::Oot3d::IsPicaLightingLutSamplerSupported(
             lighting.EnvironmentConfiguration, table);
}

std::string ScaleLiteral(float scale) {
  if (scale == 0.25F) return "0.25";
  if (scale == 0.5F) return "0.5";
  if (scale == 2.0F) return "2.0";
  if (scale == 4.0F) return "4.0";
  if (scale == 8.0F) return "8.0";
  return "1.0";
}

std::string LutInputExpression(
    Fast::Oot3d::PicaLightingLutInput input, size_t slot,
    uint8_t environmentConfiguration) {
  const std::string suffix = std::to_string(slot);
  switch (input) {
  case Fast::Oot3d::PicaLightingLutInput::NormalHalf:
    return "dot(pica_lighting_normal, normalize(pica_half_vector_" +
           suffix + "))";
  case Fast::Oot3d::PicaLightingLutInput::ViewHalf:
    return "dot(pica_normalized_view, normalize(pica_half_vector_" +
           suffix + "))";
  case Fast::Oot3d::PicaLightingLutInput::NormalView:
    return "dot(pica_lighting_normal, pica_normalized_view)";
  case Fast::Oot3d::PicaLightingLutInput::LightNormal:
    return "dot(pica_light_vector_" + suffix +
           ", pica_lighting_normal)";
  case Fast::Oot3d::PicaLightingLutInput::NegatedLightSpot:
    return "dot(pica_light_vector_" + suffix + ", pica_spot_direction_" +
           suffix + ")";
  case Fast::Oot3d::PicaLightingLutInput::CosinePhi:
    if (environmentConfiguration == 8U) {
      return "dot(normalize(pica_half_vector_" + suffix +
             ") - pica_lighting_normal * dot(pica_lighting_normal, "
             "normalize(pica_half_vector_" + suffix +
             ")), pica_lighting_tangent)";
    }
    return "0.0";
  }
  return "0.0";
}

std::string LutSample(
    const Fast::Oot3d::PicaFragmentLightingState &lighting,
    const Fast::Oot3d::PicaFragmentLight &light, size_t slot,
    uint8_t table, size_t samplerIndex) {
  const auto &sampler = lighting.LutSamplers[samplerIndex];
  std::string input = LutInputExpression(
      sampler.Input, slot, lighting.EnvironmentConfiguration);
  if (sampler.AbsoluteInput) {
    input = light.TwoSidedDiffuse ? "abs(" + input + ")"
                                  : "max(" + input + ", 0.0)";
  }
  const char *lookup = sampler.AbsoluteInput
                           ? "pica_lighting_lut_unsigned"
                           : "pica_lighting_lut_signed";
  return "(" + ScaleLiteral(sampler.Scale) + " * " + lookup + "(" +
         std::to_string(table) + ", " + input + "))";
}

bool LightingUsesLuts(
    const Fast::Oot3d::PicaFragmentLightingState &lighting) {
  if (!lighting.Enabled) return false;
  const uint8_t environment = lighting.EnvironmentConfiguration;
  if (StandardLutEnabled(lighting, 16U,
                         LutTable(Fast::Oot3d::PicaLightingLutTable::Distribution0)) ||
      StandardLutEnabled(lighting, 17U,
                         LutTable(Fast::Oot3d::PicaLightingLutTable::Distribution1)) ||
      StandardLutEnabled(lighting, 19U,
                         LutTable(Fast::Oot3d::PicaLightingLutTable::Fresnel)) ||
      StandardLutEnabled(lighting, 20U,
                         LutTable(Fast::Oot3d::PicaLightingLutTable::ReflectRed)) ||
      StandardLutEnabled(lighting, 21U,
                         LutTable(Fast::Oot3d::PicaLightingLutTable::ReflectGreen)) ||
      StandardLutEnabled(lighting, 22U,
                         LutTable(Fast::Oot3d::PicaLightingLutTable::ReflectBlue))) {
    return true;
  }
  for (size_t slot = 0; slot < lighting.ActiveLightCount; ++slot) {
    const uint8_t lightIndex = lighting.LightPermutation[slot];
    const auto &light = lighting.Lights[lightIndex];
    if (light.DistanceAttenuationEnabled ||
        (light.SpotAttenuationEnabled &&
         Fast::Oot3d::IsPicaLightingLutSamplerSupported(
             environment, static_cast<uint8_t>(8U + lightIndex)))) {
      return true;
    }
  }
  return false;
}

std::string LightingLutDeclarations() {
  return
      "layout(set=0,binding=13) uniform usampler2D pica_lighting_lut;\n"
      "float pica_lighting_lut_lookup(int table_index, int entry_index, float delta) {\n"
      "    uint raw = texelFetch(pica_lighting_lut, ivec2(entry_index, table_index), 0).r;\n"
      "    float value = float(raw & 0xFFFu) / 4095.0;\n"
      "    float difference = float((raw >> 12u) & 0x7FFu) / 2047.0;\n"
      "    if ((raw & 0x800000u) != 0u) difference = -difference;\n"
      "    return value + difference * delta;\n"
      "}\n"
      "float pica_lighting_lut_unsigned(int table_index, float position) {\n"
      "    int entry_index = int(clamp(floor(position * 256.0), 0.0, 255.0));\n"
      "    float delta = position * 256.0 - float(entry_index);\n"
      "    return pica_lighting_lut_lookup(table_index, entry_index, delta);\n"
      "}\n"
      "float pica_lighting_lut_signed(int table_index, float position) {\n"
      "    int signed_index = int(clamp(floor(position * 128.0), -128.0, 127.0));\n"
      "    float delta = position * 128.0 - float(signed_index);\n"
      "    int entry_index = signed_index < 0 ? signed_index + 256 : signed_index;\n"
      "    return pica_lighting_lut_lookup(table_index, entry_index, delta);\n"
      "}\n";
}

} // namespace

bool Oot3dPicaFragmentLightingEnabled(const Oot3dPicaDrawPacket &packet) {
  return Fast::Oot3d::DecodePicaFragmentLighting(packet.Registers).Enabled;
}

bool Oot3dPicaFragmentLightingUsesLuts(const Oot3dPicaDrawPacket &packet) {
  return LightingUsesLuts(
      Fast::Oot3d::DecodePicaFragmentLighting(packet.Registers));
}

bool Oot3dPicaFragmentLightingConfigurationSupported(
    const Oot3dPicaDrawPacket &packet, std::string *error,
    Oot3dPicaShaderBuildPurpose purpose) {
  const auto lighting =
      Fast::Oot3d::DecodePicaFragmentLighting(packet.Registers);
  if (!lighting.Enabled) {
    return true;
  }
  switch (lighting.BumpMode) {
  case Fast::Oot3d::PicaLightingBumpMode::None:
  case Fast::Oot3d::PicaLightingBumpMode::NormalMap:
  case Fast::Oot3d::PicaLightingBumpMode::TangentMap:
    break;
  default:
    SetError(error, "PICA fragment-lighting bump mode is invalid");
    return false;
  }
  if (lighting.BumpMode != Fast::Oot3d::PicaLightingBumpMode::None &&
      lighting.BumpTextureUnit >= 3U) {
    SetError(error, "PICA fragment-lighting bump texture unit is invalid");
    return false;
  }
  if (lighting.EnvironmentConfiguration > 6U &&
      lighting.EnvironmentConfiguration != 8U) {
    SetError(error, "PICA fragment-lighting environment configuration is invalid");
    return false;
  }
  if (purpose == Oot3dPicaShaderBuildPurpose::RuntimeDraw &&
      LightingUsesLuts(lighting) &&
      (packet.LightingLuts == nullptr ||
       !packet.LightingLuts->ContentHashAvailable ||
       packet.LightingLuts->ContentHash == 0U)) {
    SetError(error, "PICA fragment-lighting LUT state is unavailable");
    return false;
  }
  return true;
}

uint64_t ComputeOot3dPicaFragmentLightingStructuralKey(
    const Oot3dPicaDrawPacket &packet) {
  return Fast::Oot3d::ComputePicaFragmentLightingStructuralKey(
      packet.Registers);
}

Oot3d::Renderer::PicaFragmentLightingLayout
BuildOot3dPicaFragmentLightingLayout(const Oot3dPicaDrawPacket &packet) {
  const auto native =
      Fast::Oot3d::DecodePicaFragmentLighting(packet.Registers);
  Oot3d::Renderer::PicaFragmentLightingLayout result;
  result.SchemaVersion =
      Oot3d::Renderer::kPicaFragmentLightingLayoutSchemaVersion;
  if (!native.Enabled) {
    return result;
  }

  result.ActiveLightCount = native.ActiveLightCount;
  result.LightPermutation = native.LightPermutation;
  result.EnvironmentConfiguration = native.EnvironmentConfiguration;
  result.FresnelSelector = native.FresnelSelector;
  result.BumpTextureUnit = native.BumpTextureUnit;
  result.ShadowTextureUnit = native.ShadowTextureUnit;
  result.BumpMode =
      static_cast<Oot3d::Renderer::PicaFragmentLightingBumpMode>(
          native.BumpMode);
  result.ClampHighlights = native.ClampHighlights;
  result.RecalculateBumpVectors = native.RecalculateBumpVectors;
  result.ShadowFactorEnabled = native.ShadowFactorEnabled;
  result.ShadowPrimary = native.ShadowPrimary;
  result.ShadowSecondary = native.ShadowSecondary;
  result.ShadowAlpha = native.ShadowAlpha;
  result.InvertShadow = native.InvertShadow;
  for (size_t index = 0U; index < result.Lights.size(); ++index) {
    const auto &source = native.Lights[index];
    result.Lights[index] = {
        source.Directional,
        source.TwoSidedDiffuse,
        source.GeometricFactor0,
        source.GeometricFactor1,
        source.ShadowEnabled,
        source.SpotAttenuationEnabled,
        source.DistanceAttenuationEnabled,
    };
  }
  for (size_t index = 0U; index < result.LutSamplers.size(); ++index) {
    const auto &source = native.LutSamplers[index];
    result.LutSamplers[index] = {
        static_cast<Oot3d::Renderer::PicaFragmentLightingLutInput>(
            source.Input),
        source.Scale,
        source.AbsoluteInput,
    };
  }
  return result;
}

Oot3dPicaFragmentLightingUniformState
BuildOot3dPicaFragmentLightingUniformState(const Oot3dPicaDrawPacket &packet) {
  const auto lighting =
      Fast::Oot3d::DecodePicaFragmentLighting(packet.Registers);
  Oot3dPicaFragmentLightingUniformState uniforms;
  uniforms.GlobalAmbient = ColorVector(lighting.GlobalAmbient);
  for (size_t index = 0; index < lighting.Lights.size(); ++index) {
    const auto &light = lighting.Lights[index];
    uniforms.Specular0[index] = ColorVector(light.Specular0);
    uniforms.Specular1[index] = ColorVector(light.Specular1);
    uniforms.Diffuse[index] = ColorVector(light.Diffuse);
    uniforms.Ambient[index] = ColorVector(light.Ambient);
    uniforms.Position[index] = {light.Position[0], light.Position[1],
                                light.Position[2], 0.0F};
    uniforms.SpotDirection[index] = {light.SpotDirection[0],
                                     light.SpotDirection[1],
                                     light.SpotDirection[2], 0.0F};
    uniforms.Attenuation[index] = {light.DistanceAttenuationBias,
                                   light.DistanceAttenuationScale, 0.0F, 0.0F};
  }
  return uniforms;
}

bool GenerateOot3dPicaFragmentLightingSource(const Oot3dPicaDrawPacket &packet,
                                             std::string_view bumpTextureSample,
                                             std::string_view shadowTextureSample,
                                             std::string &declarations,
                                             std::string &mainBody,
                                             std::string *error,
                                             Oot3dPicaShaderBuildPurpose purpose) {
  declarations.clear();
  mainBody.clear();
  const auto lighting =
      Fast::Oot3d::DecodePicaFragmentLighting(packet.Registers);
  if (!lighting.Enabled) {
    return true;
  }
  if (!Oot3dPicaFragmentLightingConfigurationSupported(packet, error, purpose)) {
    return false;
  }
  if (lighting.BumpMode != Fast::Oot3d::PicaLightingBumpMode::None &&
      bumpTextureSample.empty()) {
    SetError(error, "PICA fragment-lighting bump texture sample is unavailable");
    return false;
  }
  if (lighting.ShadowFactorEnabled && shadowTextureSample.empty()) {
    SetError(error,
             "PICA fragment-lighting shadow texture sample is unavailable");
    return false;
  }

  declarations =
      "vec3 pica_quaternion_rotate(vec4 q, vec3 v) {\n"
      "    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);\n"
      "}\n";
  if (LightingUsesLuts(lighting)) {
    declarations += LightingLutDeclarations();
  }
  std::ostringstream source;
  source << "    vec4 pica_diffuse_sum = vec4(0.0, 0.0, 0.0, 1.0);\n"
            "    vec4 pica_specular_sum = vec4(0.0, 0.0, 0.0, 1.0);\n";
  if (lighting.ShadowFactorEnabled) {
    source << "    vec4 pica_shadow_factor = ";
    if (lighting.InvertShadow) {
      source << "vec4(1.0) - (" << shadowTextureSample << ");\n";
    } else {
      source << shadowTextureSample << ";\n";
    }
  } else {
    source << "    vec4 pica_shadow_factor = vec4(1.0);\n";
  }
  switch (lighting.BumpMode) {
  case Fast::Oot3d::PicaLightingBumpMode::NormalMap:
    source << "    vec3 pica_surface_normal = 2.0 * ("
           << bumpTextureSample << ").rgb - vec3(1.0);\n";
    if (lighting.RecalculateBumpVectors) {
      source << "    float pica_surface_normal_z_squared = 1.0 - "
                "dot(pica_surface_normal.xy, pica_surface_normal.xy);\n"
                "    pica_surface_normal.z = sqrt(max("
                "pica_surface_normal_z_squared, 0.0));\n";
    }
    source << "    vec3 pica_surface_tangent = vec3(1.0, 0.0, 0.0);\n";
    break;
  case Fast::Oot3d::PicaLightingBumpMode::TangentMap:
    source << "    vec3 pica_surface_normal = vec3(0.0, 0.0, 1.0);\n"
              "    vec3 pica_surface_tangent = 2.0 * ("
           << bumpTextureSample << ").rgb - vec3(1.0);\n";
    break;
  default:
    source << "    vec3 pica_surface_normal = vec3(0.0, 0.0, 1.0);\n"
              "    vec3 pica_surface_tangent = vec3(1.0, 0.0, 0.0);\n";
    break;
  }
  source << "    vec4 pica_normal_quaternion = normalize(pica_normquat);\n"
            "    vec3 pica_lighting_normal = pica_quaternion_rotate("
            "pica_normal_quaternion, pica_surface_normal);\n"
            "    vec3 pica_lighting_tangent = pica_quaternion_rotate("
            "pica_normal_quaternion, pica_surface_tangent);\n"
            "    vec3 normal = pica_lighting_normal;\n"
            "    vec3 tangent = pica_lighting_tangent;\n"
            "    vec3 pica_normalized_view = normalize(pica_view);\n";
  for (size_t slot = 0; slot < lighting.ActiveLightCount; ++slot) {
    const uint32_t lightIndex = lighting.LightPermutation[slot];
    const auto &light = lighting.Lights[lightIndex];
    source << "    vec3 pica_light_vector_" << slot << " = ";
    if (light.Directional) {
      source << "fragment_uniforms.lighting_position[" << lightIndex
             << "].xyz;\n";
    } else {
      source << "fragment_uniforms.lighting_position[" << lightIndex
             << "].xyz + pica_view;\n";
    }
    source << "    float pica_light_distance_" << slot
           << " = length(pica_light_vector_" << slot << ");\n"
           << "    pica_light_vector_" << slot
           << " = normalize(pica_light_vector_" << slot << ");\n"
           << "    vec3 pica_spot_direction_" << slot
           << " = fragment_uniforms.lighting_spot_direction[" << lightIndex
           << "].xyz;\n"
           << "    vec3 pica_half_vector_" << slot
           << " = pica_normalized_view + pica_light_vector_" << slot << ";\n"
           << "    float pica_light_dot_" << slot << " = ";
    if (light.TwoSidedDiffuse) {
      source << "abs(dot(pica_light_vector_" << slot
             << ", pica_lighting_normal));\n";
    } else {
      source << "max(dot(pica_light_vector_" << slot
             << ", pica_lighting_normal), 0.0);\n";
    }
    source << "    float pica_highlight_" << slot << " = ";
    if (lighting.ClampHighlights) {
      source << "sign(pica_light_dot_" << slot << ");\n";
    } else {
      source << "1.0;\n";
    }
    if (light.GeometricFactor0 || light.GeometricFactor1) {
      source << "    float pica_half_length_" << slot
             << " = dot(pica_half_vector_" << slot << ", pica_half_vector_"
             << slot << ");\n"
             << "    float pica_geo_factor_" << slot << " = pica_half_length_"
             << slot << " == 0.0 ? 0.0 : min(pica_light_dot_" << slot
             << " / pica_half_length_" << slot << ", 1.0);\n";
    }

    std::string spotAttenuation = "1.0";
    if (light.SpotAttenuationEnabled &&
        Fast::Oot3d::IsPicaLightingLutSamplerSupported(
            lighting.EnvironmentConfiguration,
            static_cast<uint8_t>(8U + lightIndex))) {
      spotAttenuation = LutSample(
          lighting, light, slot, static_cast<uint8_t>(8U + lightIndex),
          kLutSpot);
    }
    std::string distanceAttenuation = "1.0";
    if (light.DistanceAttenuationEnabled) {
      distanceAttenuation =
          "pica_lighting_lut_unsigned(" +
          std::to_string(16U + lightIndex) +
          ", clamp(fragment_uniforms.lighting_attenuation[" +
          std::to_string(lightIndex) + "].y * pica_light_distance_" +
          std::to_string(slot) +
          " + fragment_uniforms.lighting_attenuation[" +
          std::to_string(lightIndex) + "].x, 0.0, 1.0))";
    }
    source << "    float pica_light_attenuation_" << slot << " = ("
           << spotAttenuation << ") * (" << distanceAttenuation << ");\n";

    std::string d0 = "1.0";
    if (StandardLutEnabled(
            lighting, 16U,
            LutTable(Fast::Oot3d::PicaLightingLutTable::Distribution0))) {
      d0 = LutSample(
          lighting, light, slot,
          LutTable(Fast::Oot3d::PicaLightingLutTable::Distribution0),
          kLutD0);
    }
    source << "    vec3 pica_specular0_" << slot
           << " = fragment_uniforms.lighting_specular0[" << lightIndex
           << "].rgb * (" << d0 << ")";
    if (light.GeometricFactor0) {
      source << " * pica_geo_factor_" << slot;
    }
    source << ";\n";

    std::string reflectRed = "1.0";
    if (StandardLutEnabled(
            lighting, 20U,
            LutTable(Fast::Oot3d::PicaLightingLutTable::ReflectRed))) {
      reflectRed = LutSample(
          lighting, light, slot,
          LutTable(Fast::Oot3d::PicaLightingLutTable::ReflectRed),
          kLutReflectRed);
    }
    std::string reflectGreen = reflectRed;
    if (StandardLutEnabled(
            lighting, 21U,
            LutTable(Fast::Oot3d::PicaLightingLutTable::ReflectGreen))) {
      reflectGreen = LutSample(
          lighting, light, slot,
          LutTable(Fast::Oot3d::PicaLightingLutTable::ReflectGreen),
          kLutReflectGreen);
    }
    std::string reflectBlue = reflectRed;
    if (StandardLutEnabled(
            lighting, 22U,
            LutTable(Fast::Oot3d::PicaLightingLutTable::ReflectBlue))) {
      reflectBlue = LutSample(
          lighting, light, slot,
          LutTable(Fast::Oot3d::PicaLightingLutTable::ReflectBlue),
          kLutReflectBlue);
    }
    source << "    vec3 pica_reflectance_" << slot << " = vec3("
           << reflectRed << ", " << reflectGreen << ", " << reflectBlue
           << ");\n";

    std::string d1 = "1.0";
    if (StandardLutEnabled(
            lighting, 17U,
            LutTable(Fast::Oot3d::PicaLightingLutTable::Distribution1))) {
      d1 = LutSample(
          lighting, light, slot,
          LutTable(Fast::Oot3d::PicaLightingLutTable::Distribution1),
          kLutD1);
    }
    source << "    vec3 pica_specular1_" << slot
           << " = fragment_uniforms.lighting_specular1[" << lightIndex
           << "].rgb * pica_reflectance_" << slot << " * (" << d1 << ")";
    if (light.GeometricFactor1) {
      source << " * pica_geo_factor_" << slot;
    }
    source << ";\n";

    if (slot + 1U == lighting.ActiveLightCount &&
        StandardLutEnabled(
            lighting, 19U,
            LutTable(Fast::Oot3d::PicaLightingLutTable::Fresnel))) {
      const std::string fresnel = LutSample(
          lighting, light, slot,
          LutTable(Fast::Oot3d::PicaLightingLutTable::Fresnel),
          kLutFresnel);
      if ((lighting.FresnelSelector & 1U) != 0U) {
        source << "    pica_diffuse_sum.a = " << fresnel << ";\n";
      }
      if ((lighting.FresnelSelector & 2U) != 0U) {
        source << "    pica_specular_sum.a = " << fresnel << ";\n";
      }
    }

    source << "    pica_diffuse_sum.rgb += "
              "fragment_uniforms.lighting_diffuse["
           << lightIndex << "].rgb * pica_light_dot_" << slot;
    if (lighting.ShadowPrimary && light.ShadowEnabled) {
      source << " * pica_shadow_factor.rgb";
    }
    source << " * pica_light_attenuation_" << slot
           << " + fragment_uniforms.lighting_ambient[" << lightIndex
           << "].rgb * pica_light_attenuation_" << slot << ";\n"
           << "    pica_specular_sum.rgb += (pica_specular0_" << slot
           << " + pica_specular1_" << slot << ") * pica_highlight_" << slot
           << " * pica_light_attenuation_" << slot;
    if (lighting.ShadowSecondary && light.ShadowEnabled) {
      source << " * pica_shadow_factor.rgb";
    }
    source << ";\n";
  }
  if (lighting.ShadowAlpha) {
    if ((lighting.FresnelSelector & 1U) != 0U) {
      source << "    pica_diffuse_sum.a *= pica_shadow_factor.a;\n";
    }
    if ((lighting.FresnelSelector & 2U) != 0U) {
      source << "    pica_specular_sum.a *= pica_shadow_factor.a;\n";
    }
  }
  source << "    pica_diffuse_sum.rgb += "
            "fragment_uniforms.lighting_global_ambient.rgb;\n"
            "    primary_fragment_color = clamp(pica_diffuse_sum, vec4(0.0), "
            "vec4(1.0));\n"
            "    secondary_fragment_color = clamp(pica_specular_sum, "
            "vec4(0.0), vec4(1.0));\n";
  mainBody = source.str();
  return true;
}

} // namespace Oot3dNativeGame
