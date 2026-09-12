#include "fast/backends/gfx_vulkan.h"
#include <shaderc/shaderc.hpp>
#include <cstdio>
#include <memory>

// Link the real renderer's vtable and dependency closure, not only static
// archives. A shell probe has no Android Surface; do not call Init or claim WSI.
int main() {
    auto renderer = std::make_unique<Fast::GfxRenderingAPIVulkan>(nullptr);
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
    const auto result = compiler.CompileGlslToSpv(
        "#version 450\nvoid main(){ gl_Position=vec4(0.0,0.0,0.0,1.0); }\n",
        shaderc_vertex_shader, "android_link_probe", options);
    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        std::fprintf(stderr, "%s\n", result.GetErrorMessage().c_str());
        return 1;
    }
    std::printf("{\"renderer\":\"%s\",\"shaderc_on_device\":true,"
                "\"scope\":\"link_and_cpu_shader_compile_not_presentation\"}\n", renderer->GetName());
    return 0;
}
