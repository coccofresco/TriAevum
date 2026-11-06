#pragma once

#include <string>
#include <string_view>
#include <utility>

// Whether the running title brings shader preparation of its own, and what the
// renderer's startup does about it.
//
// A title with prepared shaders ships a translated vertex-program family and
// fragment-family packs, and its scenario corpus and Forge preparation fill
// the persistent NRI pipeline cache ahead of play. A title without them (one
// whose corpus does not exist yet, generic titles) compiles every
// program its draws use during the session: no always-available program can
// be linked for its draws, so they complete their own (specialized) programs.
//
// Each title keeps its own pipeline cache files in the renderer cache
// directory: a title's pipelines never match another title's draws, and one
// shared file would be replaced by whichever title ran last. The title's
// pipeline cache namespace names its files; the empty namespace keeps the
// original file names (the first title's, which Forge and the pipeline
// preparation tool write). Content-addressed caches (SPIR-V, pass shaders)
// are not part of it: their entries are per program and never collide
// between titles.
//
// The host installs the title's preparation before the renderer starts (as it
// installs the graphics window title); the renderer reads it when it
// initializes. The default is a prepared title with the empty namespace: a
// host that installs nothing keeps every cache, file name and preparation
// path.

namespace Fast::Renderer3ds {

struct TitleShaderPreparation {
    bool Prepared = true;
    // Lower-case letters, digits, '_' and '-'; empty for the original names.
    std::string PipelineCacheNamespace;
};

struct TitleShaderPreparationPlan {
    // Draws may carry an always-available (parametric) program next to the
    // specialized one (hybrid or uber preparation). Otherwise the specialized
    // program completes, as with TRIAEVUM_SHADER_PREPARATION=complete; the
    // renderer applies it before device creation, so the device and its
    // pipeline library policy are negotiated exactly as in that mode.
    bool AlwaysAvailablePrograms = true;
};

[[nodiscard]] inline TitleShaderPreparationPlan ResolveTitleShaderPreparationPlan(
    const TitleShaderPreparation& title) noexcept {
    return {title.Prepared};
}

[[nodiscard]] constexpr bool ValidPipelineCacheNamespace(std::string_view name) noexcept {
    if (name.size() > 32U) return false;
    for (const char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}

// The title's file for a pipeline cache file name ("nri_pipeline_cache.bin"
// becomes "nri_pipeline_cache.abc.bin" in namespace "abc"). Empty for an
// invalid namespace: the cache then lives in memory only, never in another
// title's file.
[[nodiscard]] inline std::string TitlePipelineCacheFilename(std::string_view filename,
                                                           std::string_view nameSpace) {
    if (!ValidPipelineCacheNamespace(nameSpace)) return {};
    if (nameSpace.empty()) return std::string(filename);
    const auto dot = filename.rfind('.');
    const auto stem = dot == std::string_view::npos ? filename : filename.substr(0, dot);
    const auto extension = dot == std::string_view::npos ? std::string_view{} : filename.substr(dot);
    std::string result(stem);
    result += '.';
    result += nameSpace;
    result += extension;
    return result;
}

namespace Detail {
inline TitleShaderPreparation& InstalledTitleShaderPreparationStorage() noexcept {
    static TitleShaderPreparation installed;
    return installed;
}
} // namespace Detail

inline void InstallTitleShaderPreparation(TitleShaderPreparation title) {
    Detail::InstalledTitleShaderPreparationStorage() = std::move(title);
}

[[nodiscard]] inline const TitleShaderPreparation& InstalledTitleShaderPreparation() noexcept {
    return Detail::InstalledTitleShaderPreparationStorage();
}

} // namespace Fast::Renderer3ds
