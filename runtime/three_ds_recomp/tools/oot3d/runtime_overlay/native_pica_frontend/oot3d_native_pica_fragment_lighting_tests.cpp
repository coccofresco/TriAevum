#include "oot3d_native_pica_draw_state.h"
#include "oot3d_native_pica_fragment_lighting.h"
#include "oot3d_native_pica_fragment_shader_gen.h"

#include <shaderc/shaderc.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

Oot3dNativeGame::Oot3dPicaDrawPacket LightingPacket() {
    Oot3dNativeGame::Oot3dPicaDrawPacket packet;
    packet.LightingLuts =
        std::make_shared<Oot3dNativeGame::Oot3dPicaLightingLutState>();
    packet.Registers[0x08F] = 1U;
    packet.Registers[0x1C6] = 0U;
    packet.Registers[0x1C2] = 0U;
    packet.Registers[0x1C3] = 0U;
    packet.Registers[0x1C4] = (1U << 8U) | (1U << 16U) |
                              (1U << 17U) | (1U << 19U) |
                              (7U << 20U) | (1U << 24U);
    packet.Registers[0x1D9] = 0U;
    packet.Registers[0x140] = 0x0FF00000U;
    packet.Registers[0x141] = 0x0003FC00U;
    packet.Registers[0x142] = 0x000000FFU;
    packet.Registers[0x143] = 0x04411044U;
    packet.Registers[0x144] = 0x00003C00U;
    packet.Registers[0x145] = 0x00000000U;
    packet.Registers[0x148] = 1U;
    packet.Registers[0x1C0] = 0x02208822U;
    return packet;
}

} // namespace

int main() {
    using namespace Oot3dNativeGame;
    std::string error;
    auto packet = LightingPacket();
    Oot3dPicaDecodedDrawState drawState;
    Require(DecodeOot3dPicaDrawState(packet, drawState, &error), error);
    Oot3dPicaFragmentLightingShader lighting;
    Require(BuildOot3dPicaFragmentLightingShader(packet, drawState, lighting, &error),
            error);
    Require(lighting.Enabled &&
                lighting.UniformDeclarations.find("lighting_lut_raw[1408]") !=
                    std::string::npos &&
                lighting.Helpers.find("oot3d_lighting_lut") !=
                    std::string::npos &&
                lighting.Body.find("primary_fragment_color = clamp") !=
                    std::string::npos &&
                lighting.Body.find("secondary_fragment_color = clamp") !=
                    std::string::npos &&
                lighting.Body.find("OOT3D_PICA_MATERIAL_TOON_POINT") !=
                    std::string::npos &&
                lighting.Body.find("oot3d_normal_guide = normal") !=
                    std::string::npos,
            "fragment lighting did not produce the modular GLSL contract");

    auto lutPacket = LightingPacket();
    lutPacket.Registers[0x1C4] = 0U;
    Require(BuildOot3dPicaFragmentLightingShader(
                lutPacket, drawState, lighting, &error), error);
    Require(lighting.Body.find("distribution0 = 1.0 * oot3d") !=
                    std::string::npos &&
                lighting.Body.find("distribution1 = 1.0 * oot3d") ==
                    std::string::npos &&
                lighting.Body.find("reflection.r = 1.0 * oot3d") !=
                    std::string::npos &&
                lighting.Body.find("reflection.g = reflection.r") !=
                    std::string::npos &&
                lighting.Body.find("spot = 1.0 * oot3d") !=
                    std::string::npos &&
                lighting.Body.find("diffuse_sum.a =") == std::string::npos,
            "lighting environment 0 exposed the wrong PICA LUT set");

    Require(BuildOot3dPicaFragmentLightingShader(packet, drawState, lighting, &error),
            error);
    const uint64_t structuralKey = lighting.StructuralKey;
    packet.Registers[0x142] ^= 0x000000FFU;
    Require(BuildOot3dPicaFragmentLightingShader(packet, drawState, lighting, &error) &&
                lighting.StructuralKey == structuralKey,
            "dynamic light colors changed the structural shader key");
    packet.Registers[0x148] = 3U;
    Require(BuildOot3dPicaFragmentLightingShader(packet, drawState, lighting, &error) &&
                lighting.StructuralKey != structuralKey,
            "light routing failed to change the structural shader key");

    packet = LightingPacket();
    Require(DecodeOot3dPicaDrawState(packet, drawState, &error), error);
    Oot3dPicaGeneratedFragmentShader generated;
    Require(GenerateOot3dPicaFragmentShader(
                packet, drawState, generated, &error), error);
    const auto lightingPoint = generated.Source.find(
        "OOT3D_PICA_MATERIAL_TOON_POINT");
    const auto firstTev = generated.Source.find("color_output_0");
    Require(generated.Uniforms.LightingEnabled &&
                generated.Source.find(
                    "layout(location=1) out vec4 pica_normal_guide") !=
                    std::string::npos &&
                generated.Source.find(
                    "pica_normal_guide = vec4(oot3d_normal_guide") !=
                    std::string::npos &&
                generated.Source.find(
                    "OOT3D_PICA_AMBIENT_OCCLUSION_GUIDE_READY") !=
                    std::string::npos &&
                generated.Source.find(
                    "ambient_sum += fragment_uniforms.light_ambient") !=
                    std::string::npos &&
                generated.Source.find("float oot3d_ao_response") !=
                    std::string::npos &&
                generated.Source.find("vec3 oot3d_ao_response_rgb") !=
                    std::string::npos &&
                generated.Source.find(
                    "max(clamp(diffuse_sum.rgb, vec3(0.0), vec3(1.0)), vec3(0.0001))") !=
                    std::string::npos &&
                generated.Source.find(
                    "layout(location=2) out vec4 pica_material_guide") !=
                    std::string::npos &&
                generated.Source.find("oot3d_specular_signal") !=
                    std::string::npos &&
                lightingPoint != std::string::npos &&
                firstTev != std::string::npos && lightingPoint < firstTev,
            "fragment lighting was not inserted before the TEV program");
    shaderc::Compiler compiler;
    shaderc::CompileOptions compileOptions;
    compileOptions.SetTargetEnvironment(shaderc_target_env_vulkan,
                                        shaderc_env_version_vulkan_1_0);
    const auto spirv = compiler.CompileGlslToSpv(
        generated.Source, shaderc_fragment_shader,
        "oot3d_pica_fragment_lighting_test.frag", compileOptions);
    Require(spirv.GetCompilationStatus() == shaderc_compilation_status_success,
            std::string("generated PICA lighting GLSL failed shaderc: ") +
                spirv.GetErrorMessage());

    packet = LightingPacket();
    packet.Registers[0x080] = 1U << 1U;
    packet.Registers[0x1C3] = (1U << 28U) | (1U << 22U);
    Require(DecodeOot3dPicaDrawState(packet, drawState, &error), error);
    Require(BuildOot3dPicaFragmentLightingShader(
                packet, drawState, lighting, &error), error);
    Require(lighting.Body.find(
                "surface_normal = 2.0 * texture(pica_texture1") !=
                std::string::npos &&
                lighting.Body.find("surface_normal.z = sqrt") !=
                std::string::npos &&
                lighting.Body.find("OOT3D_PICA_NORMAL_GUIDE_READY") !=
                std::string::npos,
            "PICA normal-map bump path was not generated");
    Require(GenerateOot3dPicaFragmentShader(
                packet, drawState, generated, &error), error);
    const auto normalMapSpirv = compiler.CompileGlslToSpv(
        generated.Source, shaderc_fragment_shader,
        "oot3d_pica_normal_map_test.frag", compileOptions);
    Require(normalMapSpirv.GetCompilationStatus() ==
                shaderc_compilation_status_success,
            std::string("generated PICA normal-map GLSL failed shaderc: ") +
                normalMapSpirv.GetErrorMessage());

    packet = LightingPacket();
    packet.Registers[0x080] = (1U << 2U) | (1U << 13U);
    packet.Registers[0x1C3] = (2U << 28U) | (2U << 22U);
    Require(DecodeOot3dPicaDrawState(packet, drawState, &error), error);
    Require(BuildOot3dPicaFragmentLightingShader(
                packet, drawState, lighting, &error), error);
    Require(lighting.Body.find(
                "surface_tangent = 2.0 * texture(pica_texture2, vec2(pica_texcoord1") !=
                std::string::npos,
            "PICA tangent-map path ignored the texture2 coordinate route");
    Require(GenerateOot3dPicaFragmentShader(
                packet, drawState, generated, &error), error);
    const auto tangentMapSpirv = compiler.CompileGlslToSpv(
        generated.Source, shaderc_fragment_shader,
        "oot3d_pica_tangent_map_test.frag", compileOptions);
    Require(tangentMapSpirv.GetCompilationStatus() ==
                shaderc_compilation_status_success,
            std::string("generated PICA tangent-map GLSL failed shaderc: ") +
                tangentMapSpirv.GetErrorMessage());

    packet = LightingPacket();
    packet.Registers[0x080] = 1U << 1U;
    packet.Registers[0x1C3] = 1U | (1U << 2U) | (1U << 16U) |
                              (1U << 17U) | (1U << 18U) |
                              (1U << 19U) | (1U << 24U);
    Require(DecodeOot3dPicaDrawState(packet, drawState, &error), error);
    Require(BuildOot3dPicaFragmentLightingShader(
                packet, drawState, lighting, &error), error);
    Require(lighting.Body.find(
                "lighting_shadow = vec4(1.0) - texture(pica_texture1") !=
                std::string::npos &&
                lighting.Body.find("diffuse_factor * lighting_shadow.rgb") !=
                std::string::npos &&
                lighting.Body.find("attenuation * lighting_shadow.rgb") !=
                std::string::npos &&
                lighting.Body.find("diffuse_sum.a *= lighting_shadow.a") !=
                std::string::npos,
            "PICA lighting shadow factor routing is incomplete");
    Require(GenerateOot3dPicaFragmentShader(
                packet, drawState, generated, &error), error);
    const auto shadowFactorSpirv = compiler.CompileGlslToSpv(
        generated.Source, shaderc_fragment_shader,
        "oot3d_pica_shadow_factor_test.frag", compileOptions);
    Require(shadowFactorSpirv.GetCompilationStatus() ==
                shaderc_compilation_status_success,
            std::string("generated PICA shadow-factor GLSL failed shaderc: ") +
                shadowFactorSpirv.GetErrorMessage());

    packet = LightingPacket();
    packet.Registers[0x080] = 1U;
    packet.Registers[0x083] = 5U << 28U;
    packet.Registers[0x08B] = (77U << 1U) | 1U;
    packet.Registers[0x1C3] = 1U | (1U << 16U);
    Require(DecodeOot3dPicaDrawState(packet, drawState, &error), error);
    Require(BuildOot3dPicaFragmentLightingShader(
                packet, drawState, lighting, &error), error);
    Require(lighting.Body.find(
                "lighting_shadow = oot3d_sample_shadow2d(") !=
                std::string::npos,
            "special PICA Shadow2D was not routed through its packed-depth sampler");
    Require(GenerateOot3dPicaFragmentShader(
                packet, drawState, generated, &error), error);
    Require(generated.Source.find(
                "uniform usampler2D pica_texture0") != std::string::npos &&
                generated.Source.find("oot3d_compare_shadow2d") !=
                    std::string::npos &&
                generated.Source.find("shadow_texture_bias") !=
                    std::string::npos &&
                generated.Source.find("shadow_orthographic") !=
                    std::string::npos &&
                generated.Uniforms.ShadowTextureBias == 77 &&
                generated.Uniforms.ShadowOrthographic == 1,
            "special PICA Shadow2D shader or uniform ABI is incomplete");
    const auto shadow2dSpirv = compiler.CompileGlslToSpv(
        generated.Source, shaderc_fragment_shader,
        "oot3d_pica_shadow2d_test.frag", compileOptions);
    Require(shadow2dSpirv.GetCompilationStatus() ==
                shaderc_compilation_status_success,
            std::string("generated PICA Shadow2D GLSL failed shaderc: ") +
                shadow2dSpirv.GetErrorMessage());

    packet = LightingPacket();
    packet.Registers[0x100] = 3U;
    packet.Registers[0x130] = 0x38003C00U;
    Require(DecodeOot3dPicaDrawState(packet, drawState, &error), error);
    Require(GenerateOot3dPicaFragmentShader(
                packet, drawState, generated, &error), error);
    Require(generated.Source.find(
                "layout(set=0,binding=5,r32ui) uniform uimage2D") !=
                    std::string::npos &&
                generated.Source.find("oot3d_update_shadow") !=
                    std::string::npos &&
                generated.Source.find("imageAtomicCompSwap") !=
                    std::string::npos &&
                generated.Uniforms.ShadowBiasConstant == 1.0F &&
                generated.Uniforms.ShadowBiasLinear == 0.5F,
            "PICA Shadow2D producer shader or bias ABI is incomplete");
    const auto shadowProducerSpirv = compiler.CompileGlslToSpv(
        generated.Source, shaderc_fragment_shader,
        "oot3d_pica_shadow_producer_test.frag", compileOptions);
    Require(shadowProducerSpirv.GetCompilationStatus() ==
                shaderc_compilation_status_success,
            std::string("generated PICA Shadow2D producer GLSL failed shaderc: ") +
                shadowProducerSpirv.GetErrorMessage());

    std::cout << "oot3d_native_pica_fragment_lighting_tests: ok\n";
    return 0;
}
