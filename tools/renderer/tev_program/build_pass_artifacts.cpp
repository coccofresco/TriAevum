// Developer-only compilation of the maintained pass catalogue, without ROMs.
#include "fast/oot3d/builtin_pass_shaders.h"
#include "fast/oot3d/pica_scanout_effects.h"
#include "fast/renderer/builtin_pass_binaries.h"
#include <shaderc/shaderc.hpp>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace Fast::Renderer;
static void Check(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}
int main(int argc, char** argv) try {
    Check(argc == 2, "usage: build_pass_artifacts output.h|--check");
    const bool check = std::string_view(argv[1]) == "--check";
    auto passes = Fast::Oot3d::BuildBuiltinPassShaders();
    const auto nativePassCount = passes.size();
    // The compatibility scanout uses the older Vulkan 1.1 descriptor contract.
    passes.push_back({"compat_scanout.vert", SpirvStage::Vertex, Fast::Oot3d::BuildPicaScanoutVertexShader(), {}});
    passes.push_back({"compat_scanout.frag", SpirvStage::Fragment, Fast::Oot3d::BuildPicaScanoutFragmentShader(false, 0), {}});
    std::ostringstream output, table;
    output << "// Generated from maintained renderer pass sources; no game assets or captures.\n"
              "// Explicit Vulkan target per artifact, main, performance. Preserve source donor notices.\n"
              "#pragma once\n#include \"fast/renderer/pass_shader_artifact.h\"\n#include <array>\n"
              "namespace Fast::Renderer {\n";
    shaderc::Compiler compiler;
    CachedPassShaderCompiler runtimeResolver;
    size_t index = 0;
    for (const auto& pass : passes) {
        const uint32_t minor = index < nativePassCount ? 2 : 1;
        shaderc::CompileOptions options;
        options.SetTargetEnvironment(shaderc_target_env_vulkan, minor == 2 ? shaderc_env_version_vulkan_1_2 : shaderc_env_version_vulkan_1_1);
        options.SetOptimizationLevel(shaderc_optimization_level_performance);
        for (const auto& [key, value] : pass.Defines) options.AddMacroDefinition(key, value);
        const auto kind = pass.Stage == SpirvStage::Vertex ? shaderc_vertex_shader :
                          pass.Stage == SpirvStage::Fragment ? shaderc_fragment_shader : shaderc_compute_shader;
        const auto result = compiler.CompileGlslToSpv(pass.Source, kind, pass.Name, options);
        Check(result.GetCompilationStatus() == shaderc_compilation_status_success, result.GetErrorMessage());
        if (check) {
            const auto stored = FindPassShaderArtifact(kBuiltinPassArtifacts, pass.Source, pass.Stage, pass.Defines, minor);
            Check(!stored.empty(), std::string("stale/missing pass: ") + pass.Name);
            Check(FindPassShaderArtifact(kBuiltinPassArtifacts, pass.Source, pass.Stage, pass.Defines, 3).empty(),
                  "wrong Vulkan target accepted");
            const auto wrongStage = pass.Stage == SpirvStage::Compute ? SpirvStage::Vertex : SpirvStage::Compute;
            Check(FindPassShaderArtifact(kBuiltinPassArtifacts, pass.Source, wrongStage, pass.Defines, minor).empty(),
                  "wrong shader stage accepted");
            const auto resolved = minor == 2 ? runtimeResolver.Resolve(pass.Source, pass.Stage, pass.Name, pass.Defines) :
                std::vector<uint32_t>(FindBuiltinPassShaderSpirv(pass.Source, pass.Stage, pass.Defines, minor).begin(),
                                      FindBuiltinPassShaderSpirv(pass.Source, pass.Stage, pass.Defines, minor).end());
            Check(resolved.size() == stored.size() && std::equal(resolved.begin(), resolved.end(), stored.begin()),
                  "runtime did not return the stored artifact");
            bool entry = false;
            const uint32_t model = pass.Stage == SpirvStage::Vertex ? 0 : pass.Stage == SpirvStage::Fragment ? 4 : 5;
            for (size_t cursor = 5; cursor < stored.size();) {
                const auto words = stored[cursor] >> 16;
                Check(words && words <= stored.size() - cursor, "invalid SPIR-V extent");
                if ((stored[cursor] & 65535U) == 15U) {
                    Check(words >= 4, "invalid entry point");
                    entry |= stored[cursor + 1] == model;
                }
                cursor += words;
            }
            Check(entry, "wrong shader execution model");
            Check(FindPassShaderArtifact(kBuiltinPassArtifacts, pass.Source + "\n", pass.Stage, pass.Defines).empty(), "source mutation accepted");
            auto wrongDefines = pass.Defines;
            wrongDefines.emplace_back("UNDECLARED_VARIANT", "1");
            Check(FindPassShaderArtifact(kBuiltinPassArtifacts, pass.Source, pass.Stage, wrongDefines).empty(), "macro mutation accepted");
        }
        output << "inline constexpr uint32_t kPassWords" << index << "[] = {\n";
        size_t column = 0;
        for (uint32_t word : result) {
            output << "0x" << std::hex << word << "U,";
            if (++column % 12 == 0) output << '\n';
        }
        output << std::dec << "\n};\ninline constexpr std::array<std::pair<std::string_view,std::string_view>,"
               << pass.Defines.size() << "> kPassDefines" << index << "{{";
        for (const auto& [key, value] : pass.Defines)
            output << "{" << std::quoted(key) << "," << std::quoted(value) << "},";
        output << "}};\n";
        Check(pass.Source.find(")passsrc\"") == std::string::npos, "raw source delimiter collision");
        table << "{static_cast<SpirvStage>(" << static_cast<unsigned>(pass.Stage) << "),R\"passsrc("
              << pass.Source << ")passsrc\",kPassDefines" << index << ",kPassWords" << index << "," << minor << "},\n";
        ++index;
    }
    output << "inline const PassShaderArtifact kBuiltinPassArtifacts[] = {\n" << table.str() << "};\n}\n";
    if (check) {
        Check(index == std::size(kBuiltinPassArtifacts), "pass catalogue count changed");
        Check(runtimeResolver.BuiltinArtifactHits() == nativePassCount && runtimeResolver.Stats().Requests == 0,
              "built-in resolution touched compiler/disk cache");
    }
    else {
        std::ofstream file(argv[1], std::ios::binary | std::ios::trunc);
        Check(bool(file) && bool(file << output.str()), "cannot write artifacts");
    }
    std::cout << "builtin_pass_artifacts=" << index << '\n';
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
