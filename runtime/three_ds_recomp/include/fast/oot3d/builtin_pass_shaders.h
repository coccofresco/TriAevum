#pragma once
#include "fast/renderer/shaderc_compiler.h"

namespace Fast::Oot3d {
struct BuiltinPassShader {
    const char* Name;
    Renderer::SpirvStage Stage;
    std::string Source;
    Renderer::ShaderDefines Defines;
};

// Renderer-owned sources only. No captures, scene selection or game assets.
std::vector<BuiltinPassShader> BuildBuiltinPassShaders();
}
