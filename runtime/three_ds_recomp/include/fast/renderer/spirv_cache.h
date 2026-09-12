#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Fast::Renderer {
enum class SpirvStage : uint32_t { Vertex = 1, Fragment = 2, Compute = 3 };

struct SpirvCacheStats {
    uint64_t Requests = 0, Hits = 0, Misses = 0, Rejected = 0;
    uint64_t Compilations = 0, CompilationFailures = 0;
    uint64_t Writes = 0, WriteFailures = 0;
    uint64_t CompileNanoseconds = 0, ReadNanoseconds = 0, WriteNanoseconds = 0;
};

// Render-thread owned. Separate instances/processes may share its directory.
// No GPU objects, title metadata, profile queue or shutdown-only persistence.
class SpirvCache {
  public:
    using Compile = std::function<std::vector<uint32_t>()>;
    static constexpr uint64_t MaximumEntryBytes = 16 * 1024 * 1024;
    void Configure(std::filesystem::path directory, std::string compilerContract);
    std::vector<uint32_t> Resolve(std::string_view source, SpirvStage stage, const Compile& compile);
    const SpirvCacheStats& Stats() const { return mStats; }
    const std::string& LastWriteError() const { return mLastWriteError; }
    bool Enabled() const { return !mDirectory.empty() && !mCompilerContract.empty(); }

  private:
    std::filesystem::path EntryPath(std::string_view source, SpirvStage stage) const;
    bool Load(const std::filesystem::path& path, std::string_view source,
              SpirvStage stage, std::vector<uint32_t>& words);
    bool Store(const std::filesystem::path& path, std::string_view source,
               SpirvStage stage, const std::vector<uint32_t>& words);
    std::filesystem::path mDirectory;
    std::string mCompilerContract, mLastWriteError;
    SpirvCacheStats mStats;
};
}
