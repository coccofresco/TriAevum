// iMMERSE module of TriAevum (see ../LICENSE.md).
// iMMERSE MXAO, Copyright (c) Pascal Gilcher. All rights reserved.
// Not covered by TriAevum's GPL: this file stays under the original iMMERSE
// licence (../LICENSE-iMMERSE.txt) and is used under the author's explicit
// permission, granted exclusively to TriAevum.
//
// Developer-only compilation of the MXAO programs into the module's own pack,
// without ROMs: build_immerse_pass_artifacts mxao_pass.spvpack|--check.
#include "fast/immerse/mxao_pass_binaries.h"
#include "fast/immerse/mxao_shader.h"
#include "fast/oot3d/ambient_occlusion_composite.h"
#include "fast/renderer/pass_shader_artifact.h"
#include "fast/renderer3ds/pica_geometry_guide.h"
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
    Check(argc == 2, "usage: build_immerse_pass_artifacts mxao_pass.spvpack|--check");
    const bool check = std::string_view(argv[1]) == "--check";
    const auto sources = Fast::Immerse::BuildMxaoPassShaders(Fast::Oot3d::AmbientOcclusionDebugShaderLibrary(),
                                                             Fast::Renderer3ds::PicaGeometryGuideShaderLibrary());
    const auto entries = ReadSpirvPack(Fast::Immerse::MxaoPassPack());
    const auto stored = PassShaderArtifactsFromPack(entries);
    shaderc::Compiler compiler;
    SpirvPackFile pack;
    for (size_t index = 0; index < sources.size(); ++index) {
        shaderc::CompileOptions options;
        options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
        options.SetOptimizationLevel(shaderc_optimization_level_performance);
        const auto result = compiler.CompileGlslToSpv(sources[index], shaderc_compute_shader, "mxao", options);
        Check(result.GetCompilationStatus() == shaderc_compilation_status_success, result.GetErrorMessage());
        if (check) {
            const auto spirv = FindPassShaderArtifact(stored, sources[index], SpirvStage::Compute, {});
            Check(spirv.size() == size_t(result.cend() - result.cbegin()) &&
                      std::equal(spirv.begin(), spirv.end(), result.cbegin()),
                  "stale/missing MXAO pass: regenerate with build_immerse_pass_artifacts");
        }
        auto& entry = pack.Add(Fast::Renderer::SpirvPackKind::Pass, {result.cbegin(), result.cend()});
        entry.Stage = uint32_t(SpirvStage::Compute);
        entry.VulkanMinor = 2;
        entry.Source = pack.Keep(sources[index]);
    }
    if (!check) pack.Write(argv[1]);
    std::cout << "immerse_pass_artifacts=" << sources.size() << '\n';
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
