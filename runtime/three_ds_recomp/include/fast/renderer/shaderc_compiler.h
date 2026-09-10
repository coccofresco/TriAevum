#pragma once
#include "fast/renderer/spirv_cache.h"

namespace Fast::Renderer {
// Hashes the loaded compiler and its shader-tool dependencies once per process.
// Empty means provenance was unavailable: compile normally, without disk reuse.
const std::string& ShadercCompilerContract();
std::vector<uint32_t> CompileShadercSpirv(std::string_view source, SpirvStage stage, const char* name);
}
