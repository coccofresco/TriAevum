#pragma once
#include "fast/renderer/spirv_cache.h"
#include <functional>
#include <map>
#include <utility>
#include <span>

namespace Fast::Renderer {
struct PassShaderArtifact;
// Hashes the loaded compiler and its shader-tool dependencies once per process.
// Empty means provenance was unavailable: compile normally, without disk reuse.
const std::string& ShadercCompilerContract();
std::vector<uint32_t> CompileShadercSpirv(std::string_view source, SpirvStage stage, const char* name);

// Every PICA and pass shader compilation of the process, counted at the
// compiler call independently of the caches in front of it. Release
// qualification requires zero after Forge preparation.
struct ShadercAudit {
    uint64_t Calls = 0;
    uint64_t Nanoseconds = 0;
};
ShadercAudit ShadercCompilationAudit();

using ShaderDefines = std::vector<std::pair<std::string, std::string>>;
std::span<const uint32_t> FindBuiltinPassShaderSpirv(std::string_view source,
    SpirvStage stage, const ShaderDefines& defines = {}, uint32_t vulkanMinor = 2);

// Explicit render-context owner; the same service prepares fixed pass shaders
// in Forge. Vulkan 1.2, performance/main, with exact ordered macro identity.
class CachedPassShaderCompiler {
  public:
    void Configure(std::filesystem::path directory);
    std::vector<uint32_t> Resolve(std::string_view source, SpirvStage stage,
                                  const char* name, const ShaderDefines& defines = {});
    SpirvCacheStats Stats() const;
    bool Enabled() const;
    uint64_t BuiltinArtifactHits() const { return mBuiltinArtifactHits; }
    // Programs built with a title or effect family (derived from its own
    // programs at development time), consulted after the built-in catalogue.
    void AddArtifacts(std::span<const PassShaderArtifact> artifacts);
    // Title-derived programs a publisher compiled ahead of time (the title's
    // portable shader pack), looked up by source after the catalogues and
    // before the disk cache and the compiler. Macro variants are never there.
    using TitleArtifactLookup = std::function<std::span<const uint32_t>(std::string_view, SpirvStage)>;
    void SetTitleArtifacts(TitleArtifactLookup lookup) { mTitleArtifacts = std::move(lookup); }
    uint64_t TitleArtifactHits() const { return mTitleArtifactHits; }
  private:
    std::filesystem::path mDirectory;
    std::vector<std::pair<const PassShaderArtifact*, size_t>> mArtifacts;
    std::map<std::string, SpirvCache> mVariants;
    TitleArtifactLookup mTitleArtifacts;
    uint64_t mBuiltinArtifactHits = 0;
    uint64_t mTitleArtifactHits = 0;
};
}
