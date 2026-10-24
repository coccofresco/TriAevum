// Developer-only compilation of the maintained pass catalogue, without ROMs.
#include "fast/extension/builtin_pass_shaders.h"
#include "fast/extension/pica_scanout_effects.h"
#include "fast/renderer/builtin_pass_binaries.h"
#include "fast/renderer/compatibility_shader_sources.h"
#include "spirv_pack_file.h"
#include <shaderc/shaderc.hpp>
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace Fast::Renderer;
static void Check(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

int main(int argc, char** argv) try {
    Check(argc == 2, "usage: build_pass_artifacts builtin_pass.spvpack|--check");
    const bool check = std::string_view(argv[1]) == "--check";
    auto passes = Fast::Extension::BuildBuiltinPassShaders();
    const auto nativePassCount = passes.size();
    // The compatibility scanout uses the older Vulkan 1.1 descriptor contract.
    passes.push_back({"compat_scanout.vert", SpirvStage::Vertex, Fast::Extension::BuildPicaScanoutVertexShader(), {}});
    passes.push_back({"compat_scanout.frag", SpirvStage::Fragment, Fast::Extension::BuildPicaScanoutFragmentShader(false, 0), {}});
    // Cartesian family of direct color, direct texture and texture modulation.
    // RGB and alpha are independent; enumerate the contract, not observed IDs.
    const uint64_t terms[] = {
        uint64_t(SHADER_INPUT_1) << 12,
        uint64_t(SHADER_TEXEL0) << 12,
        uint64_t(SHADER_TEXEL0) | (uint64_t(SHADER_INPUT_1) << 8),
    };
    const auto addCompatibility = [&](const char* name, SpirvStage stage, std::string source) {
        const auto duplicate = std::find_if(passes.begin() + nativePassCount, passes.end(),
            [&](const auto& pass) { return pass.Stage == stage && pass.Source == source; });
        if (duplicate == passes.end()) passes.push_back({name, stage, std::move(source), {}});
    };
    for (uint64_t rgb : terms) for (uint64_t alpha : terms) {
        for (bool hasAlpha : {false, true}) {
            CCFeatures features{};
            DecodeCompatibilityCombiner(rgb | (alpha << 16), hasAlpha ? SHADER_OPT(ALPHA) : 0, &features);
            addCompatibility("compat_combiner.vert", SpirvStage::Vertex,
                             BuildCompatibilityVertexShader(features));
            for (bool srgb : {false, true}) {
                addCompatibility("compat_combiner.frag", SpirvStage::Fragment,
                                 BuildCompatibilityFragmentShader(features, srgb));
            }
        }
    }
    SpirvPackFile pack;
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
            const auto stored = FindPassShaderArtifact(BuiltinPassArtifacts(), pass.Source, pass.Stage, pass.Defines, minor);
            Check(!stored.empty(), std::string("stale/missing pass: ") + pass.Name);
            Check(FindPassShaderArtifact(BuiltinPassArtifacts(), pass.Source, pass.Stage, pass.Defines, 3).empty(),
                  "wrong Vulkan target accepted");
            const auto wrongStage = pass.Stage == SpirvStage::Compute ? SpirvStage::Vertex : SpirvStage::Compute;
            Check(FindPassShaderArtifact(BuiltinPassArtifacts(), pass.Source, wrongStage, pass.Defines, minor).empty(),
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
            Check(FindPassShaderArtifact(BuiltinPassArtifacts(), pass.Source + "\n", pass.Stage, pass.Defines).empty(), "source mutation accepted");
            auto wrongDefines = pass.Defines;
            wrongDefines.emplace_back("UNDECLARED_VARIANT", "1");
            Check(FindPassShaderArtifact(BuiltinPassArtifacts(), pass.Source, pass.Stage, wrongDefines).empty(), "macro mutation accepted");
        }
        auto& entry = pack.Add(Fast::Renderer::SpirvPackKind::Pass, {result.cbegin(), result.cend()});
        entry.Stage = uint32_t(pass.Stage);
        entry.VulkanMinor = minor;
        entry.Source = pack.Keep(pass.Source);
        for (const auto& [key, value] : pass.Defines) entry.Defines.emplace_back(pack.Keep(key), pack.Keep(value));
        ++index;
    }
    if (check) {
        Check(index == BuiltinPassArtifacts().size(), "pass catalogue count changed");
        Check(runtimeResolver.BuiltinArtifactHits() == nativePassCount && runtimeResolver.Stats().Requests == 0,
              "built-in resolution touched compiler/disk cache");
    }
    else pack.Write(argv[1]);
    std::cout << "builtin_pass_artifacts=" << index << '\n';
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
