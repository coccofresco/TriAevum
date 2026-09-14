// Developer build: a finite interface family derived from the maintained PICA
// equations. No ROM, register capture, gameplay inventory or driver cache input.
#include "oot3d_native_pica_fragment_shader_gen.h"
#include "fast/renderer3ds/pica_fragment_artifact.h"
#include "fast/renderer3ds/pica_native_fragment_binaries.h"
#include "fast/renderer3ds/pica_nri_shader_contract.h"
#include <shaderc/shaderc.hpp>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

using namespace Oot3dNativeGame;
using namespace Fast::Renderer3ds;

static void Check(bool ok, const std::string& error) { if (!ok) throw std::runtime_error(error); }

int main(int argc, char** argv) try {
    const bool verify = argc == 2 && std::string_view(argv[1]) == "--check";
    const bool exact = argc == 3 && std::string_view(argv[1]) == "--check-exact";
    Check(argc == 2 || exact, "usage: build_fragment_artifacts output.h | --check | --check-exact output.h");
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    std::ostringstream output, table;
    output << "// Generated from maintained PICA equations, not captured game shaders.\n"
              "// Regenerate with tools/renderer/tev_program/build_fragment_artifacts.\n"
              "// Vulkan 1.1, shaderc performance optimization; canonical and NRI interfaces.\n"
              "#pragma once\n#include \"pica_fragment_artifact.h\"\n"
              "namespace Fast::Renderer3ds {\n";
    unsigned modules = 0;
    for (unsigned lighting = 0; lighting < 2; ++lighting)
    for (unsigned integerTexture = 0; integerTexture < 2; ++integerTexture)
    for (unsigned shadowWrite = 0; shadowWrite < 2; ++shadowWrite) {
        auto packet = std::make_unique<Oot3dPicaDrawPacket>();
        Oot3dPicaDecodedDrawState state{};
        packet->Registers[0x8f] = lighting;
        packet->Registers[0x80] = 1;
        packet->Registers[0x83] = integerTexture ? 2U << 28 : 0;
        state.Textures[0].Enabled = true;
        state.Textures[0].Type = integerTexture ? 2 : 0;
        state.OutputMerger.FragmentOperationMode = shadowWrite ? 3 : 0;
        Oot3dPicaGeneratedFragmentShader shader;
        std::string error;
        Check(GenerateOot3dPicaFragmentShader(*packet, state, shader, &error,
              Oot3dPicaShaderBuildPurpose::OfflineSource, Oot3dPicaTevMode::Parametric), error);
        // Check that material data does not silently introduce another program.
        for (unsigned variant = 0; variant < 16; ++variant) {
            packet->Registers[0xe0] = (variant & 1) ? 5 : 0;
            packet->Registers[0x104] = (variant & 7) << 4 | 1;
            packet->Registers[0xc0] = (variant & 1) ? 0x00030003 : 0;
            packet->Registers[0xc2] = (variant % 6) | (variant % 6) << 16;
            packet->Registers[0xc4] = (variant % 3) | (variant % 3) << 16;
            Oot3dPicaGeneratedFragmentShader candidate;
            Check(GenerateOot3dPicaFragmentShader(*packet, state, candidate, &error,
                  Oot3dPicaShaderBuildPurpose::OfflineSource, Oot3dPicaTevMode::Parametric), error);
            Check(candidate.Source == shader.Source, "material data changed finite fragment family");
        }
        const auto nri = BuildPicaNriFragmentShaderVariant(shader.Source);
        Check(nri.Applied, nri.Error);
        for (unsigned separate = 0; separate < 2; ++separate) {
            const auto& source = separate ? nri.Source : shader.Source;
            const auto binary = compiler.CompileGlslToSpv(source, shaderc_fragment_shader,
                                                         "native_fragment", options);
            Check(binary.GetCompilationStatus() == shaderc_compilation_status_success,
                  binary.GetErrorMessage());
            const auto id = IdentifyPicaShaderSource(source);
            if (verify) {
                const auto stored = FindPicaFragmentArtifact(kNativeFragmentArtifacts, source, separate != 0);
                Check(!stored.empty(), "fragment source/interface changed: regenerate artifacts");
                bool fragmentEntry = false;
                for (size_t pos = 5; pos < stored.size();) {
                    const auto length = stored[pos] >> 16;
                    Check(length > 0 && length <= stored.size() - pos, "invalid stored SPIR-V instruction");
                    if ((stored[pos] & 0xffff) == 15 && length >= 4 && stored[pos+1] == 4)
                        fragmentEntry = true;
                    pos += length;
                }
                Check(fragmentEntry, "stored artifact is not a fragment program");
            }
            output << "inline constexpr uint32_t kNativeFragmentSpirv" << modules << "[] = {\n";
            unsigned column = 0;
            for (auto word : binary) {
                output << "0x" << std::hex << word << "U,";
                if (++column % 12 == 0) output << '\n';
            }
            output << std::dec << "\n};\n";
            table << "{{" << id.Id << "ULL," << id.SecondaryHash << "ULL," << id.Size
                  << "ULL}," << (separate ? "true" : "false") << ",kNativeFragmentSpirv" << modules << "},\n";
            ++modules;
        }
    }
    output << "inline const PicaFragmentArtifact kNativeFragmentArtifacts[] = {\n"
           << table.str() << "};\n} // namespace Fast::Renderer3ds\n";
    if (exact) {
        std::ifstream file(argv[2], std::ios::binary);
        Check(bool(file), "cannot read fragment artifacts");
        const std::string existing((std::istreambuf_iterator<char>(file)), {});
        Check(existing == output.str(), "fragment artifacts are stale; regenerate with the current compiler and generators");
    } else if (!verify) {
        std::ofstream file(argv[1], std::ios::binary | std::ios::trunc);
        Check(bool(file) && bool(file << output.str()), "cannot write fragment artifacts");
    }
    std::cout << "native_fragment_artifacts=" << modules << " material_invariance_cases=128\n";
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
