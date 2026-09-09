#include "oot3d_native_pica_fragment_shader_gen.h"
#include "oot3d_native_pica_program_descriptor.h"
#include "oot3d_native_pica_canonical_hash.h"

#include <cstdlib>
#include <iostream>

#if defined(OOT3D_NATIVE_PICA_TEST_SHADERC)
#include <shaderc/shaderc.hpp>
#endif

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "oot3d_native_pica_program_descriptor_tests: " << message
                  << '\n';
        std::exit(1);
    }
}

#if defined(OOT3D_NATIVE_PICA_TEST_SHADERC)
void RequireFragmentShaderCompiles(const std::string& source) {
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan,
                                 shaderc_env_version_vulkan_1_1);
    const auto result = compiler.CompileGlslToSpv(
        source, shaderc_fragment_shader, "native_pica_lighting.frag", options);
    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        std::cerr << "native PICA lighting shader did not compile: "
                  << result.GetErrorMessage() << '\n';
        std::exit(1);
    }
}
#endif

Oot3dNativeGame::Oot3dPicaDrawPacket MakePacket() {
    Oot3dNativeGame::Oot3dPicaDrawPacket packet;
    packet.VertexShader.ProgramWordCount = 2;
    packet.VertexShader.Program[0] = 0x12345678U;
    packet.VertexShader.Program[1] = 0x9ABCDEF0U;
    packet.VertexShader.SwizzleWordCount = 1;
    packet.VertexShader.Swizzles[0] = 0x10203040U;
    packet.Registers[0x04FU] = 1U;
    packet.Registers[0x050U] = 0x03020100U;
    packet.Registers[0x080U] = 1U;
    packet.Registers[0x0C0U] = 0x00000F03U;
    packet.Registers[0x0C1U] = 0U;
    packet.Registers[0x0C2U] = 0U;
    packet.Registers[0x0C3U] = 0xFFFFFFFFU;
    packet.Registers[0x0C4U] = 0U;
    packet.Registers[0x103U] = 0x00000F01U;
    packet.Registers[0x104U] = 0x00000031U;
    packet.Registers[0x116U] = 0U;
    packet.Registers[0x117U] = 0U;
    return packet;
}

Oot3dNativeGame::Oot3dPicaDecodedDrawState MakeState() {
    Oot3dNativeGame::Oot3dPicaDecodedDrawState state;
    state.ShaderInterface.VertexMainOffset = 0U;
    state.ShaderInterface.MaximumInputAttribute = 0U;
    state.ShaderInterface.OutputMask = 1U;
    state.VertexInput.AttributeCount = 1U;
    state.VertexInput.Attributes[0].Default = false;
    state.VertexInput.Attributes[0].Format =
        Oot3dNativeGame::Oot3dPicaVertexFormat::Float;
    state.VertexInput.Attributes[0].ComponentCount = 3U;
    state.VertexInput.Loaders[0].ByteStride = 12U;
    state.VertexInput.Loaders[0].ComponentCount = 1U;
    state.VertexInput.Loaders[0].Components[0] = 0U;
    state.VertexInput.VertexCount = 3U;
    state.Framebuffer.Width = 240U;
    state.Framebuffer.Height = 400U;
    state.Framebuffer.ColorFormat = 0U;
    state.Framebuffer.DepthFormat = 0U;
    state.OutputMerger.ColorWriteMask = 0x0FU;
    return state;
}

void TestCanonicalHashZeroRuns() {
    using namespace Oot3dNativeGame;
    uint32_t random = 0x12345678U;
    const auto next = [&] {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        return random;
    };
    constexpr size_t wordCases = 1U << 20U;
    for (size_t i = 0; i < wordCases; ++i) {
        const uint64_t high = next();
        const uint64_t initial = (high << 32U) | next();
        const uint32_t word = i % 4 == 0 ? 0 : next();
        uint64_t reference = initial;
        for (unsigned shift = 0; shift < 32; shift += 8) {
            reference = (reference ^ static_cast<uint8_t>(word >> shift)) * 1099511628211ULL;
        }
        Require(CanonicalHashDetail::AppendWord(initial, word) == reference,
                "canonical word hash differs from original bytewise algorithm");
    }
    size_t byteCases = 0;
    for (size_t pattern = 0; pattern < 3; ++pattern) {
        std::array<uint8_t, 8200> bytes{};
        for (size_t i = 0; i < bytes.size(); ++i) {
            const uint32_t value = next();
            if (pattern == 0 || (pattern == 1 && i % 64 < 8)) {
                bytes[i] = static_cast<uint8_t>(value);
            }
        }
        for (size_t alignment = 0; alignment < 8; ++alignment) {
            for (size_t size = 0; size <= 8192; size += size < 80 ? 1 : 31) {
                const auto view = std::span(bytes).subspan(alignment, size);
                uint64_t reference = 1469598103934665603ULL;
                for (const uint8_t byte : view) {
                    reference = (reference ^ byte) * 1099511628211ULL;
                }
                if (reference == 0) reference = 1;
                Require(HashOot3dPicaCanonicalBytes(view) == reference,
                        "canonical byte hash differs from original bytewise algorithm");
                ++byteCases;
            }
        }
    }
    std::cout << "canonical hash: " << wordCases << " word and " << byteCases
              << " byte-stream differential cases passed\n";
}

} // namespace

int main() {
    using namespace Oot3dNativeGame;
    TestCanonicalHashZeroRuns();
    auto packet = MakePacket();
    auto state = MakeState();
    const auto baseline = BuildOot3dPicaCanonicalDrawIdentity(packet, state);
    const auto baselineFeatures =
        AnalyzeOot3dPicaFragmentFeatures(packet, state);
    Require(baselineFeatures.FullySupported(),
            "baseline fragment feature classification is unsupported");
    Require(baseline == BuildOot3dPicaCanonicalDrawIdentity(packet, state),
            "identity is not deterministic");
    Require(baseline.VertexProgramId != 0U &&
                baseline.FragmentProgramId != 0U &&
                baseline.RasterStateId != 0U &&
                baseline.PipelineId != 0U && baseline.DynamicStateId != 0U &&
                baseline.FullRegisterStateId != 0U,
            "zero identity escaped the unavailable sentinel");

    auto uniformPacket = packet;
    uniformPacket.VertexShader.FloatUniforms[4][2] = 3.5F;
    const auto uniform =
        BuildOot3dPicaCanonicalDrawIdentity(uniformPacket, state);
    Require(uniform.VertexProgramId == baseline.VertexProgramId &&
                uniform.FragmentProgramId == baseline.FragmentProgramId &&
                uniform.RasterStateId == baseline.RasterStateId &&
                uniform.PipelineId == baseline.PipelineId,
            "uniform data changed a structural identity");
    Require(uniform.DynamicStateId != baseline.DynamicStateId,
            "uniform data did not change dynamic identity");

    auto tevPacket = packet;
    tevPacket.Registers[0x0C2U] ^= 1U;
    const auto tev = BuildOot3dPicaCanonicalDrawIdentity(tevPacket, state);
    Require(tev.VertexProgramId == baseline.VertexProgramId &&
                tev.FragmentProgramId != baseline.FragmentProgramId &&
                tev.RasterStateId == baseline.RasterStateId &&
                tev.PipelineId != baseline.PipelineId,
            "TEV operation did not change the complete pipeline identity");

    auto depthState = state;
    depthState.OutputMerger.Depth.TestEnabled = true;
    depthState.OutputMerger.Depth.WriteEnabled = true;
    depthState.OutputMerger.Depth.Compare = Oot3dPicaCompareFunction::Less;
    const auto depth =
        BuildOot3dPicaCanonicalDrawIdentity(packet, depthState);
    Require(depth.VertexProgramId == baseline.VertexProgramId &&
                depth.FragmentProgramId == baseline.FragmentProgramId &&
                depth.RasterStateId != baseline.RasterStateId &&
                depth.PipelineId != baseline.PipelineId,
            "depth state did not change only pipeline structure");

    auto unknownRegisterPacket = packet;
    unknownRegisterPacket.Registers[0x2F0U] = 0xBAADF00DU;
    const auto unknown =
        BuildOot3dPicaCanonicalDrawIdentity(unknownRegisterPacket, state);
    Require(unknown.FullRegisterStateId != baseline.FullRegisterStateId,
            "full register collision guard ignored a register mutation");

    Require(FormatOot3dPicaCanonicalId(baseline.VertexProgramId).size() == 18U,
            "canonical ID text is not fixed-width hexadecimal");

    auto lightingPacket = packet;
    lightingPacket.Registers[0x08FU] = 1U;
    lightingPacket.Registers[0x1C4U] = 0xFF7FFFFFU;
    lightingPacket.Registers[0x149U] = 1U;
    lightingPacket.Registers[0x142U] = 0x0FF00000U;
    const auto lighting =
        AnalyzeOot3dPicaFragmentFeatures(lightingPacket, state);
    Oot3dPicaGeneratedFragmentShader lightingShader;
    std::string lightingError;
    Require(lighting.FragmentLightingEnabled &&
                lighting.FragmentLighting.Valid() &&
                lighting.FragmentLighting.ActiveLightCount == 1U &&
                lighting.FragmentLighting.LightPermutation[0] == 0U &&
                lighting.FragmentLighting.Lights[0].Directional &&
                lighting.FullySupported() &&
                GenerateOot3dPicaFragmentShader(lightingPacket, state,
                                                lightingShader, &lightingError) &&
                lightingShader.Source.find("pica_diffuse_sum") !=
                    std::string::npos &&
                lightingShader.Hooks.Has(
                    Oot3d::Renderer::PicaShaderSemantic::MaterialLightingPoint) &&
                lightingShader.Uniforms.Lighting.Diffuse[0][0] == 1.0F,
            "direct native fragment lighting was not generated");
#if defined(OOT3D_NATIVE_PICA_TEST_SHADERC)
    RequireFragmentShaderCompiles(lightingShader.Source);
#endif
    auto shadowState = state;
    shadowState.Textures[0].Enabled = true;
    shadowState.Textures[0].Type = 2U;
    shadowState.Textures[0].Width = 32U;
    shadowState.Textures[0].Height = 32U;
    auto shadowPacket = lightingPacket;
    shadowPacket.Registers[0x080U] |= 1U;
    shadowPacket.Registers[0x08BU] = 0x0002468BU;
    shadowPacket.Registers[0x130U] = 0x38003C00U;
    shadowPacket.Registers[0x1C3U] =
        1U | (3U << 2U) | (1U << 16U) | (1U << 17U) |
        (1U << 18U) | (1U << 19U);
    shadowPacket.Registers[0x1C4U] &= ~1U;
    const auto shadowFeatures =
        AnalyzeOot3dPicaFragmentFeatures(shadowPacket, shadowState);
    Require(shadowFeatures.FullySupported() &&
                shadowFeatures.Texture0Type == 2U &&
                (shadowFeatures.ReferencedTextureMask & 1U) != 0U &&
                GenerateOot3dPicaFragmentShader(
                    shadowPacket, shadowState, lightingShader,
                    &lightingError) &&
                lightingShader.Source.find(
                    "uniform usampler2D pica_texture0") !=
                    std::string::npos &&
                lightingShader.Source.find(
                    "return value.x > z ? float(value.y)") !=
                    std::string::npos &&
                lightingShader.Source.find(
                    "vec4(1.0) - (pica_sample_shadow2d") !=
                    std::string::npos &&
                lightingShader.Source.find(
                    "lighting_diffuse[0].rgb * pica_light_dot_0 * "
                    "pica_shadow_factor.rgb") != std::string::npos &&
                lightingShader.Source.find(
                    "lighting_ambient[0].rgb * pica_light_attenuation_0") !=
                    std::string::npos &&
                lightingShader.Source.find(
                    "pica_specular_sum.a *= pica_shadow_factor.a") !=
                    std::string::npos &&
                lightingShader.Hooks.SamplesTexture(0U) &&
                lightingShader.Hooks.TextureSample(0U) != nullptr &&
                lightingShader.Hooks.TextureSample(0U)->Operation ==
                    Oot3d::Renderer::
                        PicaTextureCoordinateOperation::Shadow2DNative &&
                lightingShader.Uniforms.ShadowTextureBias == 0x0002468A &&
                lightingShader.Uniforms.ShadowOrthographic == 1 &&
                lightingShader.Uniforms.ShadowBiasConstant == 1.0F &&
                lightingShader.Uniforms.ShadowBiasLinear == 0.5F,
            "native PICA Shadow2D sampling or lighting factor is wrong");
#if defined(OOT3D_NATIVE_PICA_TEST_SHADERC)
    RequireFragmentShaderCompiles(lightingShader.Source);
#endif
    auto shadowProducerState = state;
    shadowProducerState.OutputMerger.FragmentOperationMode = 3U;
    auto shadowProducerPacket = packet;
    shadowProducerPacket.Registers[0x100U] = 3U;
    shadowProducerPacket.Registers[0x130U] = 0x38003C00U;
    const auto shadowProducerFeatures =
        AnalyzeOot3dPicaFragmentFeatures(shadowProducerPacket,
                                         shadowProducerState);
    Require(shadowProducerFeatures.FullySupported() &&
                GenerateOot3dPicaFragmentShader(
                    shadowProducerPacket, shadowProducerState,
                    lightingShader, &lightingError) &&
                lightingShader.Source.find(
                    "binding=5,r32ui) uniform uimage2D "
                    "pica_shadow_buffer") != std::string::npos &&
                lightingShader.Source.find("pica_update_shadow2d") !=
                    std::string::npos &&
                lightingShader.Source.find("imageAtomicCompSwap") !=
                    std::string::npos &&
                lightingShader.Source.find("gl_FragDepth = pica_depth") ==
                    std::string::npos &&
                lightingShader.Hooks.Outputs.Depth ==
                    Oot3d::Renderer::
                        PicaFragmentDepthOutput::FixedFunction,
            "native PICA Shadow2D producer pass is wrong");
#if defined(OOT3D_NATIVE_PICA_TEST_SHADERC)
    RequireFragmentShaderCompiles(lightingShader.Source);
#endif
    auto bumpState = state;
    bumpState.Textures[2].Enabled = true;
    bumpState.Textures[2].Width = 16U;
    bumpState.Textures[2].Height = 16U;
    bumpState.Texture2UsesCoordinate1 = true;
    auto bumpPacket =
        std::make_unique<Oot3dPicaDrawPacket>(lightingPacket);
    bumpPacket->Registers[0x080U] |= (1U << 2U) | (1U << 13U);
    bumpPacket->Registers[0x1C3U] =
        (2U << 22U) | (1U << 28U);
    const auto normalMapFeatures =
        AnalyzeOot3dPicaFragmentFeatures(*bumpPacket, bumpState);
    Require(normalMapFeatures.FullySupported() &&
                (normalMapFeatures.ReferencedTextureMask & (1U << 2U)) !=
                    0U &&
                GenerateOot3dPicaFragmentShader(
                    *bumpPacket, bumpState, lightingShader,
                    &lightingError) &&
                lightingShader.Source.find(
                    "vec3 pica_surface_normal = 2.0 * ("
                    "pica_sample_texture2(vec2(pica_texcoord1.x, 1.0 - "
                    "pica_texcoord1.y))).rgb - vec3(1.0)") !=
                    std::string::npos &&
                lightingShader.Source.find(
                    "pica_surface_normal_z_squared") !=
                    std::string::npos &&
                lightingShader.Source.find(
                    "pica_normal_quaternion, pica_surface_normal") !=
                    std::string::npos &&
                lightingShader.Source.find(
                    "vec3 normal = pica_lighting_normal") !=
                    std::string::npos &&
                lightingShader.Hooks.SamplesTexture(2U) &&
                lightingShader.Hooks.TextureSample(2U) != nullptr &&
                lightingShader.Hooks.TextureSample(2U)->Coordinate == 1U &&
                lightingShader.Hooks.Has(
                    Oot3d::Renderer::PicaShaderSemantic::MaterialNormal),
            "native PICA normal-map bump lighting was not generated");
#if defined(OOT3D_NATIVE_PICA_TEST_SHADERC)
    RequireFragmentShaderCompiles(lightingShader.Source);
#endif
    bumpPacket->Registers[0x1C3U] |= 1U << 30U;
    Require(GenerateOot3dPicaFragmentShader(
                *bumpPacket, bumpState, lightingShader,
                &lightingError) &&
                lightingShader.Source.find(
                    "vec3 pica_surface_normal = 2.0 * (") !=
                    std::string::npos &&
                lightingShader.Source.find(
                    "pica_surface_normal_z_squared") ==
                    std::string::npos,
            "PICA disable-bump-renormalization bit was ignored");
#if defined(OOT3D_NATIVE_PICA_TEST_SHADERC)
    RequireFragmentShaderCompiles(lightingShader.Source);
#endif
    auto tangentState = state;
    tangentState.Textures[1].Enabled = true;
    tangentState.Textures[1].Width = 16U;
    tangentState.Textures[1].Height = 16U;
    *bumpPacket = lightingPacket;
    bumpPacket->Registers[0x080U] |= 1U << 1U;
    bumpPacket->Registers[0x1C3U] =
        (1U << 22U) | (2U << 28U);
    Require(GenerateOot3dPicaFragmentShader(
                *bumpPacket, tangentState, lightingShader,
                &lightingError) &&
                lightingShader.Source.find(
                    "vec3 pica_surface_normal = vec3(0.0, 0.0, 1.0)") !=
                    std::string::npos &&
                lightingShader.Source.find(
                    "vec3 pica_surface_tangent = 2.0 * ("
                    "pica_sample_texture1(vec2(pica_texcoord1.x, 1.0 - "
                    "pica_texcoord1.y))).rgb - vec3(1.0)") !=
                    std::string::npos &&
                lightingShader.Source.find(
                    "pica_surface_normal_z_squared") ==
                    std::string::npos &&
                lightingShader.Hooks.SamplesTexture(1U),
            "native PICA tangent-map bump lighting was not generated");
#if defined(OOT3D_NATIVE_PICA_TEST_SHADERC)
    RequireFragmentShaderCompiles(lightingShader.Source);
#endif
    *bumpPacket = lightingPacket;
    bumpPacket->Registers[0x1C3U] = 3U << 28U;
    Require(!Oot3dPicaFragmentLightingConfigurationSupported(
                *bumpPacket, &lightingError),
            "reserved PICA bump mode was accepted");
    bumpPacket->Registers[0x1C3U] =
        (3U << 22U) | (1U << 28U);
    Require(!Oot3dPicaFragmentLightingConfigurationSupported(
                *bumpPacket, &lightingError),
            "reserved PICA bump texture selector was accepted");
    auto lutLightingPacket = lightingPacket;
    lutLightingPacket.Registers[0x1C4U] &= ~(1U << 16U);
    const auto missingLutLighting =
        AnalyzeOot3dPicaFragmentFeatures(lutLightingPacket, state);
    Require(!missingLutLighting.FullySupported(),
            "fragment-lighting accepted unavailable LUT state");
    auto lutState = std::make_shared<Oot3dPicaLightingLutState>();
    lutState->PackedEntries.fill(0x00000FFFU);
    lutState->ContentHash =
        ComputeOot3dPicaLightingLutContentHash(*lutState);
    lutState->ContentHashAvailable = true;
    lutLightingPacket.LightingLuts = lutState;
    const auto lutLighting =
        AnalyzeOot3dPicaFragmentFeatures(lutLightingPacket, state);
    Require(lutLighting.FullySupported() &&
                GenerateOot3dPicaFragmentShader(
                    lutLightingPacket, state, lightingShader,
                    &lightingError) &&
                lightingShader.Source.find(
                    "binding=13) uniform usampler2D pica_lighting_lut") !=
                    std::string::npos &&
                lightingShader.Source.find(
                    "pica_lighting_lut_unsigned(0") != std::string::npos,
            "native fragment-lighting LUT shader was not generated");
#if defined(OOT3D_NATIVE_PICA_TEST_SHADERC)
    RequireFragmentShaderCompiles(lightingShader.Source);
#endif
    auto offlineLutPacket = lutLightingPacket;
    offlineLutPacket.LightingLuts.reset();
    Oot3dPicaGeneratedFragmentShader offlineLutShader;
    Require(!GenerateOot3dPicaFragmentShader(offlineLutPacket, state,
                offlineLutShader, &lightingError),
            "runtime generation must still reject missing lighting LUTs");
    Require(GenerateOot3dPicaFragmentShader(offlineLutPacket, state,
                offlineLutShader, &lightingError,
                Oot3dPicaShaderBuildPurpose::OfflineSource) &&
                offlineLutShader.Source == lightingShader.Source &&
                offlineLutShader.SourceIdentity == lightingShader.SourceIdentity,
            "offline preparation changed native lighting shader source");
    auto offlineProcPacket = offlineLutPacket;
    offlineProcPacket.Registers[0x080U] |= 1U << 10U;
    offlineProcPacket.Registers[0x0C0U] = 6U;
    Require(!GenerateOot3dPicaFragmentShader(offlineProcPacket, state,
                offlineLutShader, &lightingError,
                Oot3dPicaShaderBuildPurpose::OfflineSource) &&
                lightingError.find("procedural LUT payload") != std::string::npos,
            "offline preparation fabricated an embedded procedural LUT");
    auto completeLutLightingPacket = lightingPacket;
    completeLutLightingPacket.Registers[0x1C3U] =
        (8U << 4U) | (3U << 2U);
    completeLutLightingPacket.Registers[0x1C4U] = 0U;
    completeLutLightingPacket.LightingLuts = lutState;
    const auto completeLutLighting =
        AnalyzeOot3dPicaFragmentFeatures(completeLutLightingPacket, state);
    Require(completeLutLighting.FullySupported() &&
                GenerateOot3dPicaFragmentShader(
                    completeLutLightingPacket, state, lightingShader,
                    &lightingError),
            "complete native fragment-lighting LUT configuration failed");
    for (const char* expectedCall : {
             "pica_lighting_lut_unsigned(0",
             "pica_lighting_lut_unsigned(1",
             "pica_lighting_lut_unsigned(3",
             "pica_lighting_lut_unsigned(4",
             "pica_lighting_lut_unsigned(5",
             "pica_lighting_lut_unsigned(6",
             "pica_lighting_lut_unsigned(8",
             "pica_lighting_lut_unsigned(16",
         }) {
        Require(lightingShader.Source.find(expectedCall) != std::string::npos,
                "complete native fragment-lighting LUT path is missing");
    }
#if defined(OOT3D_NATIVE_PICA_TEST_SHADERC)
    RequireFragmentShaderCompiles(lightingShader.Source);
#endif
    const auto lutIdentity =
        BuildOot3dPicaCanonicalDrawIdentity(lutLightingPacket, state);
    auto changedLutState =
        std::make_shared<Oot3dPicaLightingLutState>(*lutState);
    changedLutState->Entry(0U, 0U) ^= 1U;
    changedLutState->ContentHash =
        ComputeOot3dPicaLightingLutContentHash(*changedLutState);
    lutLightingPacket.LightingLuts = changedLutState;
    const auto changedLutIdentity =
        BuildOot3dPicaCanonicalDrawIdentity(lutLightingPacket, state);
    Require(changedLutIdentity.FragmentProgramId ==
                lutIdentity.FragmentProgramId &&
                changedLutIdentity.DynamicStateId !=
                    lutIdentity.DynamicStateId &&
                changedLutIdentity.FullRegisterStateId !=
                    lutIdentity.FullRegisterStateId,
            "lighting LUT data changed structure or escaped dynamic identity");

    auto fogPacket = packet;
    fogPacket.Registers[0x0E0U] = 5U | (1U << 16U);
    const auto fog = AnalyzeOot3dPicaFragmentFeatures(fogPacket, state);
    Require(fog.FogEnabled && fog.FogFlip && fog.FogMode == 5U,
            "native fog mode and LUT direction were not classified");

    auto proceduralPacket = packet;
    proceduralPacket.Registers[0x080U] |= 1U << 10U;
    proceduralPacket.Registers[0x0C0U] = 0x00000F06U;
    const auto procedural =
        AnalyzeOot3dPicaFragmentFeatures(proceduralPacket, state);
    Require(procedural.ProceduralTextureEnabled &&
                procedural.ProceduralTextureReferenced &&
                !procedural.FullySupported(),
            "procedural-texture coverage gap was not classified");
    std::cout << "oot3d_native_pica_program_descriptor_tests: ok\n";
    return 0;
}
