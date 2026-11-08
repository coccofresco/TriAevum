#include "oot3d_native_pica_fragment_shader_gen.h"
#include "oot3d_native_pica_program_descriptor.h"
#include "oot3d_native_pica_canonical_hash.h"
#include "oot3d_native_pica_shader_gen.h"
#include "fast/renderer3ds/pica_surface_lighting_shader.h"
#include "fast/renderer3ds/pica_texture_coordinates.h"
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "fast/extension/pica_fragment_lighting.h"
#include "fast/renderer3ds/pica_fragment_grass_lighting.h"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

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
void RequireFragmentShaderCompiles(const std::string& source, shaderc_shader_kind kind = shaderc_fragment_shader) {
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan,
                                 shaderc_env_version_vulkan_1_1);
    const auto result = compiler.CompileGlslToSpv(
        source, kind, "native_pica_lighting", options);
    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        std::cerr << "native PICA lighting shader did not compile: "
                  << result.GetErrorMessage() << '\n';
        std::exit(1);
    }
}
#endif

// The specialized program runs the parametric lighting text with its decoded
// program as a constant: returns that program when the source carries it.
std::optional<Fast::Renderer3ds::PicaLightingProgram> ConstantLightingProgram(
    const std::string& source, const Oot3dNativeGame::Oot3dPicaDrawPacket& packet) {
    const auto program = Fast::Extension::BuildPicaLightingProgram(
        Fast::Extension::DecodePicaFragmentLighting(packet.Registers));
    std::string control = "const PicaLightingProgram pica_lighting_constant_program = "
                          "PicaLightingProgram(uvec4(";
    for (size_t i = 0; i < program.Control.size(); ++i)
        control += std::to_string(program.Control[i]) + (i + 1 < program.Control.size() ? "u, " : "u)");
    if (source.find(control) == std::string::npos) return std::nullopt;
    return program;
}

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

// The register-file ID resumes from the previous draw's fold state. Every
// value must still equal a bytewise FNV-1a of the whole file, whatever
// changed in between.
void TestIncrementalRegisterFileHash() {
    using namespace Oot3dNativeGame;
    const auto reference = [](const Oot3dPicaDrawPacket& packet) {
        uint64_t hash = 1469598103934665603ULL;
        const auto appendByte = [&](uint8_t byte) {
            hash = (hash ^ byte) * 1099511628211ULL;
        };
        const auto appendWord = [&](uint32_t word) {
            for (unsigned shift = 0; shift < 32U; shift += 8U) {
                appendByte(static_cast<uint8_t>(word >> shift));
            }
        };
        constexpr std::string_view domain = "oot3d.pica.register_file";
        appendWord(kOot3dPicaProgramDescriptorSchemaVersion);
        appendWord(static_cast<uint32_t>(domain.size()));
        for (const char c : domain) appendByte(static_cast<uint8_t>(c));
        appendWord(static_cast<uint32_t>(packet.Registers.size()));
        for (const uint32_t word : packet.Registers) appendWord(word);
        return hash == 0U ? 1U : hash;
    };
    uint32_t random = 0x9E3779B9U;
    const auto next = [&] {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        return random;
    };
    auto packet = MakePacket();
    const auto state = MakeState();
    const auto original = packet.Registers;
    for (int draw = 0; draw < 3000; ++draw) {
        switch (next() % 6U) {
        case 0:
            break;  // unchanged file
        case 1:
            packet.Registers[0] ^= next() | 1U;  // first block
            break;
        case 2:
            packet.Registers.back() = next();  // last block
            break;
        case 3:
            packet.Registers = original;  // back to a state seen before
            break;
        default:
            for (uint32_t n = next() % 4U; n != 0U; --n) {
                packet.Registers[next() % packet.Registers.size()] =
                    next() % 3U == 0U ? 0U : next();
            }
            break;
        }
        Require(BuildOot3dPicaCanonicalDrawIdentity(packet, state)
                        .FullRegisterStateId == reference(packet),
                "incremental register-file ID differs from a full bytewise hash");
    }
    std::cout << "register-file ID: 3000 incremental draws match full hashes\n";
}

} // namespace

int main(int argc, char** argv) {
    // Optional private frontend snapshot: prove a real title program's grass
    // seams and compile its two atlas stages without another gameplay run.
    if (argc == 2) {
        using namespace Oot3dNativeGame;
        std::ifstream stream(argv[1]);
        nlohmann::json fixture; stream >> fixture;
        Oot3dNativePicaFrontend frontend;
        std::string error;
        Require(frontend.RestoreState(fixture, &error), error.c_str());
        frontend.SetDiagnosticHistoryEnabled(true);
        frontend.SetCommandBufferReader([&](uint32_t address, uint32_t bytes, uint32_t* guest,
                                           std::vector<uint32_t>* words) {
            const auto found = fixture.at("grass_compile_buffers").find(std::to_string(address));
            if (found == fixture.at("grass_compile_buffers").end()) return false;
            *words = found->get<std::vector<uint32_t>>();
            if (words->size() * 4 != bytes) return false;
            *guest = address - 0x20000000U + 0x14000000U;
            return true;
        });
        Oot3dGspCommandPacket submit{};
        submit.Control = 1;
        submit.Parameters[0] = fixture.at("grass_compile_root_guest").get<uint32_t>();
        submit.Parameters[1] = fixture.at("grass_compile_root_bytes").get<uint32_t>();
        const auto words = fixture.at("grass_compile_buffers").at(std::to_string(
            submit.Parameters[0] - 0x14000000U + 0x20000000U)).get<std::vector<uint32_t>>();
        Require(frontend.SubmitGspCommand(submit, words, &error), error.c_str());
        const auto index = fixture.at("grass_compile_draw").get<size_t>();
        Require(frontend.PendingDrawPackets().size() > index, "private grass packet missing");
        const auto& packet = frontend.PendingDrawPackets()[index];
        Require(packet.Registers[0x85] * 8 == 0x184DC480U, "private terrain draw identity shifted");
        Oot3dPicaDecodedDrawState state;
        Require(DecodeOot3dPicaDrawState(packet, state, &error), error.c_str());
        Oot3dPicaGeneratedVertexShader vertex;
        Require(GenerateOot3dPicaVertexShader(packet, state, vertex, &error), error.c_str());
        const auto& hooks = vertex.TemporalProgram->Hooks;
        std::cerr << "private terrain seams: program=" << packet.VertexShader.ProgramWordCount
                  << " swizzles=" << packet.VertexShader.SwizzleWordCount
                  << " entry=" << state.ShaderInterface.VertexMainOffset
                  << " uv=" << hooks.TextureCoordinates[0].Available()
                  << " transform=" << (hooks.TransformProgram() != nullptr)
                  << " surface=" << (hooks.SurfaceProgram() != nullptr) << '\n';
        Require(hooks.TextureCoordinates[0].Available() && hooks.TransformProgram() && hooks.SurfaceProgram(),
                "private terrain missing typed grass UV/transform/surface hooks");
        const auto affine = Fast::Renderer3ds::DecodePicaAffineUv(&hooks.TextureCoordinates[0],
            vertex.Uniforms.BooleanMask, {reinterpret_cast<const uint8_t*>(vertex.Uniforms.Floats.data()), sizeof(vertex.Uniforms.Floats)});
        std::cerr << "private terrain affine status=" << int(affine.Status) << " input=" << int(affine.InputRegister)
                  << " root=" << vertex.Uniforms.Floats[89][3] << " mode=" << vertex.Uniforms.Floats[92][0] << '\n';
        Require(affine.Status == Fast::Renderer3ds::PicaAffineUvStatus::Applied,
                "private terrain UV contract rejected its native uniforms");
        for (const bool modern : {false, true}) {
            const auto atlasVertex = Fast::Renderer3ds::BuildPicaSurfaceLightingVertexShader(vertex.Source, hooks, modern);
            Require(!atlasVertex.empty(), "private terrain atlas vertex unavailable");
#if defined(OOT3D_NATIVE_PICA_TEST_SHADERC)
            RequireFragmentShaderCompiles(atlasVertex, shaderc_vertex_shader);
#endif
            for (const auto mode : {Oot3dPicaTevMode::Specialized, Oot3dPicaTevMode::Parametric}) {
                Oot3dPicaGeneratedFragmentShader fragment;
                Require(GenerateOot3dPicaFragmentShader(packet, state, fragment, &error,
                            Oot3dPicaShaderBuildPurpose::RuntimeDraw, mode), error.c_str());
                for (const bool coefficients : {false, true}) {
                    const auto atlas = Fast::Renderer3ds::BuildPicaFragmentGrassLightingShader(
                        fragment.Source, fragment.Hooks, modern, coefficients,
                        Fast::Renderer3ds::DecodePicaSurfaceColorResponse(packet.Registers).Fragment);
                    Require(!atlas.empty(), "private terrain atlas fragment unavailable");
#if defined(OOT3D_NATIVE_PICA_TEST_SHADERC)
                    RequireFragmentShaderCompiles(atlas);
#endif
                }
            }
        }
        std::cout << "Private grass terrain: UV/transform/surface hooks, 2 vertex + 8 fragment atlas variants passed\n";
        return 0;
    }
    using namespace Oot3dNativeGame;
    TestCanonicalHashZeroRuns();
    TestIncrementalRegisterFileHash();
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
    // A register-proven fragment response preserves HDR gain, unlike a white
    // texture passed through native TEV's final clamp (0.75 * 0.5 * 4 = 1.5).
    auto terrainPacket = lightingPacket;
    for (const auto b : {0xC0, 0xC8, 0xD0, 0xD8, 0xF0, 0xF8}) {
        terrainPacket.Registers[b] = 0x0FFF0FFF;
        terrainPacket.Registers[b + 1] = terrainPacket.Registers[b + 2] = terrainPacket.Registers[b + 4] = 0;
    }
    terrainPacket.Registers[0xC0] = 0x0E300E31; terrainPacket.Registers[0xC2] = 0x10001;
    terrainPacket.Registers[0xC8] = 0x0E1F0E0F; terrainPacket.Registers[0xCA] = 1; terrainPacket.Registers[0xCC] = 2;
    const auto response = Fast::Renderer3ds::DecodePicaSurfaceColorResponse(terrainPacket.Registers);
    Require(!response.Available && response.Fragment.Available && response.Fragment.TintStage == 1 &&
            response.Fragment.PackedScales == 8, "fragment TEV factorization changed the legacy capability");
    auto badResponse = terrainPacket;
    badResponse.Registers[0xC4] = 1;
    Require(!Fast::Renderer3ds::DecodePicaSurfaceColorResponse(badResponse.Registers).Fragment.Available,
            "texture-dependent saturation before tint acquired grass factorization");
    badResponse = terrainPacket; badResponse.Registers[0xC9] = 1;
    Require(!Fast::Renderer3ds::DecodePicaSurfaceColorResponse(badResponse.Registers).Fragment.Available,
            "inverted tint acquired grass factorization");
    badResponse = terrainPacket; badResponse.Registers[0xCA] = 2;
    Require(!Fast::Renderer3ds::DecodePicaSurfaceColorResponse(badResponse.Registers).Fragment.Available,
            "additive material acquired grass factorization");
    const auto noTint = [&] { auto p = terrainPacket; p.Registers[0xC8] = 0x0FFF0FFF; p.Registers[0xCA] = 0; return p; }();
    Require(Fast::Renderer3ds::DecodePicaSurfaceColorResponse(noTint.Registers).Fragment.Available &&
            Fast::Renderer3ds::DecodePicaSurfaceColorResponse(noTint.Registers).Fragment.Key() != response.Fragment.Key(),
            "parametric atlas cache key omitted tint response");
    for (const auto mode : {Oot3dPicaTevMode::Specialized, Oot3dPicaTevMode::Parametric}) {
        Oot3dPicaGeneratedFragmentShader terrain;
        Require(GenerateOot3dPicaFragmentShader(terrainPacket, state, terrain, &lightingError,
                    Oot3dPicaShaderBuildPurpose::RuntimeDraw, mode), "grass terrain fixture generation");
        for (const bool modern : {false, true}) for (const bool coefficients : {false, true}) {
            const auto atlas = Fast::Renderer3ds::BuildPicaFragmentGrassLightingShader(
                terrain.Source, terrain.Hooks, modern, coefficients, response.Fragment);
            Require(!atlas.empty() && atlas.find("#define pica_sample_texture0") != std::string::npos &&
                    atlas.find("grass_samples[grass_sample]") != std::string::npos &&
                    atlas.find("grass_response = byteround3(grass_response) * 4.0") != std::string::npos,
                    "fragment grass atlas lost typed lighting or unclipped TEV gain");
#if defined(OOT3D_NATIVE_PICA_TEST_SHADERC)
            RequireFragmentShaderCompiles(atlas);
#endif
        }
        auto invalid = terrain.Hooks; invalid.NativeLightingCapture = {};
        Require(Fast::Renderer3ds::BuildPicaFragmentGrassLightingShader(terrain.Source, invalid, true, true, response.Fragment).empty(),
                "fragment grass accepted missing capture sites");
    }
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
                    "pica_program_surface(pica_lighting_constant_program, "
                    "vec4(0.0), pica_sample_shadow2d") != std::string::npos &&
                ConstantLightingProgram(lightingShader.Source, shadowPacket)
                    .has_value() &&
                // Shadow factor, inverted, on the primary color and alpha;
                // light 0 casts it.
                (ConstantLightingProgram(lightingShader.Source, shadowPacket)
                     ->Control[3] & (128U | 256U | 2U | 8U)) ==
                    (128U | 256U | 2U | 8U) &&
                (ConstantLightingProgram(lightingShader.Source, shadowPacket)
                     ->Lights[0][1] & 16U) != 0U &&
                lightingShader.Source.find(
                    "diffuse *= pica_shadow_factor.rgb") != std::string::npos &&
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
                    "pica_program_surface(pica_lighting_constant_program, "
                    "pica_sample_texture2(vec2(pica_texcoord1.x, 1.0 - "
                    "pica_texcoord1.y)), vec4(1.0)") != std::string::npos &&
                ConstantLightingProgram(lightingShader.Source, *bumpPacket)
                    .has_value() &&
                // Normal map with the recalculated Z component.
                ((ConstantLightingProgram(lightingShader.Source, *bumpPacket)
                      ->Control[3] >> 4U) & 3U) ==
                    static_cast<uint32_t>(Fast::Extension::PicaLightingBumpMode::NormalMap) &&
                (ConstantLightingProgram(lightingShader.Source, *bumpPacket)
                     ->Control[3] & 64U) != 0U &&
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
                ConstantLightingProgram(lightingShader.Source, *bumpPacket)
                    .has_value() &&
                ((ConstantLightingProgram(lightingShader.Source, *bumpPacket)
                      ->Control[3] >> 4U) & 3U) ==
                    static_cast<uint32_t>(Fast::Extension::PicaLightingBumpMode::NormalMap) &&
                (ConstantLightingProgram(lightingShader.Source, *bumpPacket)
                     ->Control[3] & 64U) == 0U,
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
                    "pica_program_surface(pica_lighting_constant_program, "
                    "pica_sample_texture1(vec2(pica_texcoord1.x, 1.0 - "
                    "pica_texcoord1.y)), vec4(1.0)") != std::string::npos &&
                ConstantLightingProgram(lightingShader.Source, *bumpPacket)
                    .has_value() &&
                ((ConstantLightingProgram(lightingShader.Source, *bumpPacket)
                      ->Control[3] >> 4U) & 3U) ==
                    static_cast<uint32_t>(Fast::Extension::PicaLightingBumpMode::TangentMap) &&
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
                ConstantLightingProgram(lightingShader.Source, lutLightingPacket)
                    .has_value() &&
                ConstantLightingProgram(lightingShader.Source, lutLightingPacket)
                        ->Luts[0][3] != 0.0F,
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
    // Every LUT path is enabled in the constant program: D0, D1, fresnel and
    // the three reflection tables, light 0's spot and distance attenuation.
    const auto completeProgram =
        ConstantLightingProgram(lightingShader.Source, completeLutLightingPacket);
    Require(completeProgram.has_value(),
            "complete native fragment-lighting program is missing");
    for (const size_t sampler : {0U, 1U, 3U, 4U, 5U, 6U}) {
        Require(completeProgram->Luts[sampler][3] != 0.0F,
                "complete native fragment-lighting LUT path is missing");
    }
    Require((completeProgram->Lights[0][1] & (32U | 64U)) == (32U | 64U),
            "complete native fragment-lighting attenuation path is missing");
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

    // Z-buffering with scale -1 and offset 0: the PICA depth is the
    // rasterized depth, so the program leaves depth to fixed function (early
    // depth testing) and gets its own key. A depth offset, another scale or
    // W-buffering keeps the explicit depth.
    auto rasterState = state;
    rasterState.Viewport.ZBuffering = true;
    rasterState.Viewport.DepthRange = -1.0F;
    rasterState.Viewport.NearPlane = 0.0F;
    Oot3dPicaGeneratedFragmentShader rasterShader;
    std::string rasterError;
    Require(Oot3dPicaFragmentDepthIsRasterDepth(rasterState) &&
                GenerateOot3dPicaFragmentShader(packet, rasterState, rasterShader, &rasterError) &&
                rasterShader.Source.find("gl_FragDepth") == std::string::npos &&
                rasterShader.Source.find("float pica_depth = gl_FragCoord.z;") != std::string::npos &&
                rasterShader.Hooks.Outputs.Depth == Oot3d::Renderer::PicaFragmentDepthOutput::FixedFunction,
            "raster-depth fragment program still writes its depth");
    Oot3dPicaGeneratedFragmentShader parametricShader;
    Require(!Oot3dPicaFragmentDepthIsRasterDepth(rasterState, Oot3dPicaTevMode::Parametric) &&
                GenerateOot3dPicaFragmentShader(packet, rasterState, parametricShader, &rasterError,
                                                Oot3dPicaShaderBuildPurpose::RuntimeDraw,
                                                Oot3dPicaTevMode::Parametric) &&
                parametricShader.Source.find("gl_FragDepth = pica_depth;") != std::string::npos,
            "parametric program lost its explicit depth");
#if defined(OOT3D_NATIVE_PICA_TEST_SHADERC)
    RequireFragmentShaderCompiles(rasterShader.Source);
#endif
    for (int variant = 0; variant < 3; ++variant) {
        auto explicitState = rasterState;
        if (variant == 0) explicitState.Viewport.NearPlane = -3.0517578125e-05F;
        if (variant == 1) explicitState.Viewport.DepthRange = -0.5F;
        if (variant == 2) explicitState.Viewport.ZBuffering = false;
        Oot3dPicaGeneratedFragmentShader explicitShader;
        Require(!Oot3dPicaFragmentDepthIsRasterDepth(explicitState) &&
                    GenerateOot3dPicaFragmentShader(packet, explicitState, explicitShader, &rasterError) &&
                    explicitShader.Source.find("gl_FragDepth = pica_depth;") != std::string::npos &&
                    explicitShader.Hooks.Outputs.Depth ==
                        Oot3d::Renderer::PicaFragmentDepthOutput::ExplicitNative &&
                    explicitShader.StateKey != rasterShader.StateKey,
                "explicit-depth fragment program lost its depth write or key");
    }
    std::cout << "oot3d_native_pica_program_descriptor_tests: ok\n";
    return 0;
}
