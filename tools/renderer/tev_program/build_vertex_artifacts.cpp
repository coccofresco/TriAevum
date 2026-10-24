// Developer-only title build from original SHBIN output tables. No capture input.
#include "oot3d_native_pica_shader_gen.h"
#include "oot3d_native_vertex_binaries.h"
#include "fast/extension/pica_directional_shadow_caster.h"
#include "fast/extension/pica_rigid_motion.h"
#include "fast/renderer/pass_shader_artifact.h"
#include "fast/renderer3ds/pica_surface_lighting_shader.h"
#include "spirv_pack_file.h"
#include <shaderc/shaderc.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <vector>

using namespace Oot3dNativeGame;
static void Check(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }

int main(int argc, char** argv) try {
    Check(argc >= 3, "usage: build_vertex_artifacts oot3d_native_vertex.spvpack|--check input.shbin [...]");
    const bool check = std::string_view(argv[1]) == "--check";
    // Vertex programs first, then the pass programs, each in emission order.
    SpirvPackFile vertexPack, passPack;
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    unsigned modules = 0, passModules = 0;
    // Pass programs derived from the title's vertex programs (shadow casters,
    // surface lighting): the pass compiler's Vulkan 1.2 target.
    const auto emitPass = [&](const std::string& source) {
        shaderc::CompileOptions passOptions;
        passOptions.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
        passOptions.SetOptimizationLevel(shaderc_optimization_level_performance);
        const auto spirv = compiler.CompileGlslToSpv(source, shaderc_vertex_shader, "title_pass", passOptions);
        Check(spirv.GetCompilationStatus() == shaderc_compilation_status_success, spirv.GetErrorMessage());
        if (check) {
            const auto stored = Fast::Renderer::FindPassShaderArtifact(
                TitlePassArtifacts(), source, Fast::Renderer::SpirvStage::Vertex, {});
            Check(stored.size() == size_t(spirv.cend() - spirv.cbegin()) &&
                      std::equal(stored.begin(), stored.end(), spirv.cbegin()),
                  "missing/stale title pass program");
        }
        auto& entry = passPack.Add(Fast::Renderer::SpirvPackKind::Pass, {spirv.cbegin(), spirv.cend()});
        entry.Stage = uint32_t(Fast::Renderer::SpirvStage::Vertex);
        entry.VulkanMinor = 2;
        entry.Source = passPack.Keep(source);
        ++passModules;
    };
    const auto emitVertex = [&](const std::string& source) {
        const auto spirv = compiler.CompileGlslToSpv(source, shaderc_vertex_shader, "native_vertex", options);
        Check(spirv.GetCompilationStatus() == shaderc_compilation_status_success, spirv.GetErrorMessage());
        const auto id = Fast::Renderer3ds::IdentifyPicaShaderSource(source);
        if (check) {
            const auto artifact = Fast::Renderer3ds::FindPicaVertexArtifact(TitleVertexArtifacts(), source);
            Check(!artifact.Spirv.empty(), "missing/stale compiled vertex source identity");
            // Validate the stored module independently of compiler version and layout.
            bool vertexEntry = false;
            for (size_t cursor = 5; cursor < artifact.Spirv.size();) {
                const uint32_t instruction = artifact.Spirv[cursor];
                const size_t words = instruction >> 16;
                Check(words && words <= artifact.Spirv.size() - cursor, "invalid SPIR-V instruction extent");
                if ((instruction & 65535U) == 15U) {
                    Check(words >= 4, "invalid SPIR-V entry point");
                    vertexEntry |= artifact.Spirv[cursor + 1] == 0U;
                }
                cursor += words;
            }
            Check(vertexEntry, "compiled artifact has no vertex entry point");
        }
        vertexPack.Add(Fast::Renderer::SpirvPackKind::PicaVertex, {spirv.cbegin(), spirv.cend()}).Identity =
            {id.Id, id.SecondaryHash, id.Size};
        std::cout << "vertex_source=" << id.Id << '\n';
        ++modules;
    };
    for (int arg = 2; arg < argc; ++arg) {
        std::ifstream file(argv[arg], std::ios::binary);
        Check(bool(file), "cannot open SHBIN");
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
        const auto word = [&](size_t p) {
            Check(p <= bytes.size() && bytes.size() - p >= 4, "truncated SHBIN");
            return uint32_t(bytes[p]) | uint32_t(bytes[p+1])<<8 |
                   uint32_t(bytes[p+2])<<16 | uint32_t(bytes[p+3])<<24;
        };
        Check(word(0) == 0x424c5644U, "not DVLB");
        const size_t count = word(4);
        Check(count && count <= (bytes.size()-8)/4, "invalid DVLE table");
        const size_t p = 8 + count*4;
        Check(word(p) == 0x504c5644U, "not DVLP");
        auto packet = std::make_unique<Oot3dPicaDrawPacket>();
        packet->VertexShader.ProgramWordCount = word(p+12);
        packet->VertexShader.SwizzleWordCount = word(p+20);
        Check(packet->VertexShader.ProgramWordCount <= packet->VertexShader.Program.size() &&
              packet->VertexShader.SwizzleWordCount <= packet->VertexShader.Swizzles.size(), "invalid program capacity");
        for (size_t i=0; i<packet->VertexShader.ProgramWordCount; ++i)
            packet->VertexShader.Program[i] = word(p+word(p+8)+4*i);
        for (size_t i=0; i<packet->VertexShader.SwizzleWordCount; ++i)
            packet->VertexShader.Swizzles[i] = word(p+word(p+16)+8*i);
        for (size_t entry=0; entry<count; ++entry) {
            const size_t d = word(8+4*entry);
            Check(word(d) == 0x454c5644U && ((word(d+4)>>16)&255) == 0, "not a vertex DVLE");
            Oot3dPicaDecodedDrawState state{};
            state.ShaderInterface.VertexMainOffset = word(d+8);
            std::array<uint32_t,16> maps;
            maps.fill(0x1f1f1f1fU);
            const size_t offset = d + word(d+40), outputs = word(d+44);
            Check(outputs <= bytes.size()/8, "invalid output table extent");
            for (size_t i=0; i<outputs; ++i) {
                const auto descriptor = word(offset+8*i);
                const unsigned kind = descriptor & 0xffff, reg = descriptor >> 16;
                const auto mask = word(offset+8*i+4);
                Check(reg < 16 && mask && !(mask & ~15U), "invalid output register or mask");
                // Native output semantic bases (position, quaternion, color,
                // UV0, UV0.w, UV1, UV2, reserved, view).
                constexpr unsigned bases[]{0,4,8,12,16,14,22,31,18};
                constexpr unsigned lengths[]{4,4,4,2,1,2,2,0,4};
                Check(kind < std::size(bases) && lengths[kind], "unsupported SHBIN output semantic");
                unsigned component = 0;
                for (unsigned lane=0; lane<4; ++lane) if (mask & (1U<<lane)) {
                    Check(component < lengths[kind], "output semantic exceeds native width");
                    const auto shift = lane*8;
                    Check(((maps[reg]>>shift)&31) == 31, "overlapping output semantics");
                    maps[reg] = (maps[reg] & ~(255U<<shift)) | ((bases[kind]+component++)<<shift);
                }
                state.ShaderInterface.OutputMask |= 1U<<reg;
            }
            size_t packed = 0;
            for (size_t reg=0; reg<maps.size(); ++reg) if (state.ShaderInterface.OutputMask & (1U<<reg)) {
                Check(packed < 7, "output map exceeds PICA interface");
                packet->Registers[0x50+packed++] = maps[reg];
            }
            packet->Registers[0x4f] = static_cast<uint32_t>(packed);
            packet->Registers[0x2bd] = state.ShaderInterface.OutputMask;
            packet->Registers[0x2ba] = state.ShaderInterface.VertexMainOffset;
            Oot3dPicaGeneratedVertexShader translated, reference;
            std::string error;
            Check(GenerateOot3dPicaVertexShader(*packet,state,translated,&error,true), error);
            Check(GenerateOot3dPicaVertexShader(*packet,state,reference,&error), error);
            Check(translated.Source == reference.Source, "offline/native vertex source mismatch");
            emitVertex(translated.Source);
            const auto& temporalProgram = *translated.TemporalProgram;
            const ::Oot3d::Renderer::PicaTemporalVertexProgramView view{
                temporalProgram.Hooks, temporalProgram.PreviousRegisterState, temporalProgram.PreviousMainBody};
            const auto temporal = Fast::Extension::BuildPicaVertexInstrumentation(
                translated.Source, translated.StateKey, view, true, false);
            Check(temporal.Applied && temporal.UsedProvidedProgram, "typed temporal vertex emission failed");
            emitVertex(temporal.Source);
            // Per-pixel lighting and typed normal guides read the authored
            // surface normal and tint: the always-available programs pair
            // with these exactly as draws request them.
            for (bool temporalOutputs : {false, true}) {
                const auto surface = Fast::Extension::BuildPicaVertexInstrumentation(
                    translated.Source, translated.StateKey, view, temporalOutputs, true);
                if (surface.Applied && surface.SurfaceApplied) emitVertex(surface.Source);
            }
            // The passes derive from the program and its typed hooks exactly
            // as they do at draw time; a program a pass rejects at draw time
            // (no caster or lighting contract) has none.
            const auto directional = Fast::Extension::BuildPicaDirectionalShadowCasterShader(
                translated.Source, translated.StateKey, temporalProgram.Hooks);
            if (directional.Applied()) emitPass(directional.Source);
            const auto point = Fast::Extension::BuildPicaPointShadowCasterShader(
                translated.Source, translated.StateKey, temporalProgram.Hooks);
            if (point.Applied()) emitPass(point.Source);
            for (bool surfaceLighting : {false, true}) {
                const auto lighting = Fast::Renderer3ds::BuildPicaSurfaceLightingVertexShader(
                    translated.Source, temporalProgram.Hooks, surfaceLighting);
                if (!lighting.empty()) emitPass(lighting);
            }
            std::cout << "title_pass_derivations directional=" << directional.Applied()
                      << " point=" << point.Applied() << '\n';
        }
    }
    if (check) {
        Check(modules == TitleVertexArtifacts().size(), "compiled vertex family coverage changed");
        Check(passModules == TitlePassArtifacts().size(), "title pass program coverage changed");
    } else {
        vertexPack.Entries.insert(vertexPack.Entries.end(), passPack.Entries.begin(), passPack.Entries.end());
        vertexPack.Write(argv[1]);
    }
    std::cout << "native_vertex_artifacts=" << modules << " title_pass_artifacts=" << passModules << '\n';
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
