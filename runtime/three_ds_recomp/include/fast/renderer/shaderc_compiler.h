#pragma once
#include "fast/renderer/spirv_cache.h"
#include <map>
#include <utility>

namespace Fast::Renderer {
// Hashes the loaded compiler and its shader-tool dependencies once per process.
// Empty means provenance was unavailable: compile normally, without disk reuse.
const std::string& ShadercCompilerContract();
std::vector<uint32_t> CompileShadercSpirv(std::string_view source, SpirvStage stage, const char* name);

using ShaderDefines = std::vector<std::pair<std::string, std::string>>;

// Explicit render-context owner; the same service prepares fixed pass shaders
// in Forge. Vulkan 1.2, performance/main, with exact ordered macro identity.
class CachedPassShaderCompiler {
  public:
    void Configure(std::filesystem::path directory);
    std::vector<uint32_t> Resolve(std::string_view source, SpirvStage stage,
                                  const char* name, const ShaderDefines& defines = {});
    SpirvCacheStats Stats() const;
    bool Enabled() const;
  private:
    std::filesystem::path mDirectory;
    std::map<std::string, SpirvCache> mVariants;
};
}
