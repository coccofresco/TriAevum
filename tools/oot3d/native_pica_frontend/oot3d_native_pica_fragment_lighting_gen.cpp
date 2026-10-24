#include "oot3d_native_pica_fragment_lighting_gen.h"

#include "fast/extension/pica_fragment_lighting.h"
#include "fast/renderer3ds/pica_lighting_program.h"

#include <array>
#include <charconv>
#include <sstream>

namespace Oot3dNativeGame {
namespace {

void SetError(std::string *error, const std::string &message) {
  if (error != nullptr) {
    *error = message;
  }
}

std::array<float, 4> ColorVector(const Fast::Extension::PicaLightingColor &color) {
  return {color.Rgb[0], color.Rgb[1], color.Rgb[2], 0.0F};
}

// Exact GLSL literals: shortest round-trip decimal.
std::string FloatLiteral(float value) {
  std::array<char, 32> buffer{};
  const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  std::string literal(buffer.data(), result.ptr);
  if (literal.find_first_of(".e") == std::string::npos) literal += ".0";
  return literal;
}

std::string LightingProgramLiteral(const Fast::Renderer3ds::PicaLightingProgram &program) {
  std::ostringstream out;
  const auto unsignedVector = [&out](const std::array<uint32_t, 4> &value) {
    out << "uvec4(" << value[0] << "u, " << value[1] << "u, " << value[2] << "u, "
        << value[3] << "u)";
  };
  out << "PicaLightingProgram(";
  unsignedVector(program.Control);
  out << ", uvec4[8](";
  for (size_t i = 0; i < program.Lights.size(); ++i) {
    if (i != 0) out << ", ";
    unsignedVector(program.Lights[i]);
  }
  out << "), vec4[7](";
  for (size_t i = 0; i < program.Luts.size(); ++i) {
    const auto &lut = program.Luts[i];
    out << (i != 0 ? ", " : "") << "vec4(" << FloatLiteral(lut[0]) << ", "
        << FloatLiteral(lut[1]) << ", " << FloatLiteral(lut[2]) << ", "
        << FloatLiteral(lut[3]) << ")";
  }
  out << "))";
  return out.str();
}

uint8_t LutTable(Fast::Extension::PicaLightingLutTable table) {
  return static_cast<uint8_t>(table);
}

bool StandardLutEnabled(const Fast::Extension::PicaFragmentLightingState &lighting,
                        uint32_t disableBit, uint8_t table) {
  return (lighting.Config1 & (1U << disableBit)) == 0U &&
         Fast::Extension::IsPicaLightingLutSamplerSupported(
             lighting.EnvironmentConfiguration, table);
}

bool LightingUsesLuts(
    const Fast::Extension::PicaFragmentLightingState &lighting) {
  if (!lighting.Enabled) return false;
  const uint8_t environment = lighting.EnvironmentConfiguration;
  if (StandardLutEnabled(lighting, 16U,
                         LutTable(Fast::Extension::PicaLightingLutTable::Distribution0)) ||
      StandardLutEnabled(lighting, 17U,
                         LutTable(Fast::Extension::PicaLightingLutTable::Distribution1)) ||
      StandardLutEnabled(lighting, 19U,
                         LutTable(Fast::Extension::PicaLightingLutTable::Fresnel)) ||
      StandardLutEnabled(lighting, 20U,
                         LutTable(Fast::Extension::PicaLightingLutTable::ReflectRed)) ||
      StandardLutEnabled(lighting, 21U,
                         LutTable(Fast::Extension::PicaLightingLutTable::ReflectGreen)) ||
      StandardLutEnabled(lighting, 22U,
                         LutTable(Fast::Extension::PicaLightingLutTable::ReflectBlue))) {
    return true;
  }
  for (size_t slot = 0; slot < lighting.ActiveLightCount; ++slot) {
    const uint8_t lightIndex = lighting.LightPermutation[slot];
    const auto &light = lighting.Lights[lightIndex];
    if (light.DistanceAttenuationEnabled ||
        (light.SpotAttenuationEnabled &&
         Fast::Extension::IsPicaLightingLutSamplerSupported(
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
      "    precise float result = value + difference * delta;\n"
      "    return result;\n"
      "}\n"
      "float pica_lighting_lut_unsigned(int table_index, float position) {\n"
      "    int entry_index = int(clamp(floor(position * 256.0), 0.0, 255.0));\n"
      "    precise float delta = position * 256.0 - float(entry_index);\n"
      "    return pica_lighting_lut_lookup(table_index, entry_index, delta);\n"
      "}\n"
      "float pica_lighting_lut_signed(int table_index, float position) {\n"
      "    int signed_index = int(clamp(floor(position * 128.0), -128.0, 127.0));\n"
      "    precise float delta = position * 128.0 - float(signed_index);\n"
      "    int entry_index = signed_index < 0 ? signed_index + 256 : signed_index;\n"
      "    return pica_lighting_lut_lookup(table_index, entry_index, delta);\n"
      "}\n";
}

} // namespace

bool Oot3dPicaFragmentLightingEnabled(const Oot3dPicaDrawPacket &packet) {
  return Fast::Extension::DecodePicaFragmentLighting(packet.Registers).Enabled;
}

bool Oot3dPicaFragmentLightingUsesLuts(const Oot3dPicaDrawPacket &packet) {
  return LightingUsesLuts(
      Fast::Extension::DecodePicaFragmentLighting(packet.Registers));
}

bool Oot3dPicaFragmentLightingConfigurationSupported(
    const Oot3dPicaDrawPacket &packet, std::string *error,
    Oot3dPicaShaderBuildPurpose purpose) {
  const auto lighting =
      Fast::Extension::DecodePicaFragmentLighting(packet.Registers);
  if (!lighting.Enabled) {
    return true;
  }
  switch (lighting.BumpMode) {
  case Fast::Extension::PicaLightingBumpMode::None:
  case Fast::Extension::PicaLightingBumpMode::NormalMap:
  case Fast::Extension::PicaLightingBumpMode::TangentMap:
    break;
  default:
    SetError(error, "PICA fragment-lighting bump mode is invalid");
    return false;
  }
  if (lighting.BumpMode != Fast::Extension::PicaLightingBumpMode::None &&
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
  return Fast::Extension::ComputePicaFragmentLightingStructuralKey(
      packet.Registers);
}

Oot3d::Renderer::PicaFragmentLightingLayout
BuildOot3dPicaFragmentLightingLayout(const Oot3dPicaDrawPacket &packet) {
  const auto native =
      Fast::Extension::DecodePicaFragmentLighting(packet.Registers);
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
      Fast::Extension::DecodePicaFragmentLighting(packet.Registers);
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
                                             Oot3dPicaShaderBuildPurpose purpose,
                                             bool parametric, Fast::Renderer3ds::PicaNativeLightingCaptureLayout* capture) {
  if (capture) *capture = {};
  declarations.clear();
  mainBody.clear();
  const auto lighting =
      Fast::Extension::DecodePicaFragmentLighting(packet.Registers);
  if (!lighting.Enabled) {
    return true;
  }
  if (!Oot3dPicaFragmentLightingConfigurationSupported(packet, error, purpose)) {
    return false;
  }
  if (lighting.BumpMode != Fast::Extension::PicaLightingBumpMode::None &&
      bumpTextureSample.empty()) {
    SetError(error, "PICA fragment-lighting bump texture sample is unavailable");
    return false;
  }
  if (lighting.ShadowFactorEnabled && shadowTextureSample.empty()) {
    SetError(error,
             "PICA fragment-lighting shadow texture sample is unavailable");
    return false;
  }

  // One lighting text for both programs: the specialized program runs it with
  // its program as a constant (the compiler folds every branch), the
  // always-available program reads the same program from the draw's uniforms.
  // Both evaluate the same operations in the same order and produce the same
  // bits, so a draw can swap between them.
  const std::string program = parametric ? "fragment_uniforms.lighting_program"
                                         : "pica_lighting_constant_program";
  declarations = std::string(Fast::Renderer3ds::PicaLightingArithmeticFunctions) +
      "vec3 pica_quaternion_rotate(vec4 q, vec3 v) {\n"
      "    precise vec3 rotated = v + 2.0 * pica_lighting_cross(q.xyz, "
      "pica_lighting_cross(q.xyz, v) + q.w * v);\n"
      "    return rotated;\n"
      "}\n";
  if (!parametric)
    declarations += Fast::Renderer3ds::PicaLightingProgramDeclaration;
  if (LightingUsesLuts(lighting)) {
    declarations += LightingLutDeclarations();
  } else {
    // No-LUT family: no descriptor dependency, and decoded flags disable
    // every lookup. These functions are unreachable for this family.
    declarations += "float pica_lighting_lut_unsigned(int t, float p) { return 1.0; }\n"
                    "float pica_lighting_lut_signed(int t, float p) { return 1.0; }\n";
  }
  declarations += Fast::Renderer3ds::PicaLightingProgramFunctions;
  std::ostringstream source;
  // `precise`: no contraction, so folding the constant program cannot fuse
  // the specialized program's operations differently.
  source << "    precise vec4 pica_diffuse_sum = vec4(0.0, 0.0, 0.0, 1.0);\n"
            "    precise vec4 pica_specular_sum = vec4(0.0, 0.0, 0.0, 1.0);\n";
  if (!parametric) {
    source << "    const PicaLightingProgram pica_lighting_constant_program = "
           << LightingProgramLiteral(Fast::Extension::BuildPicaLightingProgram(lighting))
           << ";\n";
  }
  source << "    vec3 pica_surface_normal, pica_surface_tangent;\n"
            "    vec4 pica_shadow_factor;\n"
            "    pica_program_surface(" << program << ", "
         << (bumpTextureSample.empty() ? "vec4(0.0)" : bumpTextureSample) << ", "
         << (shadowTextureSample.empty() ? "vec4(1.0)" : shadowTextureSample)
         << ", pica_surface_normal, pica_surface_tangent, pica_shadow_factor);\n";
  source << "    precise vec4 pica_normal_quaternion = pica_lighting_normalize(pica_normquat);\n"
            "    precise vec3 pica_lighting_normal = pica_quaternion_rotate("
            "pica_normal_quaternion, pica_surface_normal);\n"
            "    precise vec3 pica_lighting_tangent = pica_quaternion_rotate("
            "pica_normal_quaternion, pica_surface_tangent);\n"
            "    vec3 normal = pica_lighting_normal;\n"
            "    vec3 tangent = pica_lighting_tangent;\n"
            "    precise vec3 pica_normalized_view = pica_lighting_normalize(pica_view);\n";
  std::string body(Fast::Renderer3ds::PicaLightingProgramBody);
  const std::string uniformProgram = "fragment_uniforms.lighting_program";
  for (size_t at = body.find(uniformProgram); at != std::string::npos;
       at = body.find(uniformProgram, at + program.size()))
    body.replace(at, uniformProgram.size(), program);
  if (capture) {
    capture->Version = 1;
    capture->Begin = static_cast<size_t>(source.tellp());
    // Only the first statement changes length between literal/uniform programs.
    const size_t beforeDiffuse = Fast::Renderer3ds::PicaLightingBodyBeforeDiffuse.size()
        - uniformProgram.size() + program.size();
    capture->Diffuse = capture->Begin + beforeDiffuse;
    capture->Specular = capture->Diffuse + Fast::Renderer3ds::PicaLightingBodyBeforeSpecular.size();
  }
  source << body;
  if (capture) capture->End = static_cast<size_t>(source.tellp());
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
