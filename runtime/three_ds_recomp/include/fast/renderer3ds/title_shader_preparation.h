#pragma once

// Whether the running title brings shader preparation of its own, and what the
// renderer's startup does about it.
//
// A title with prepared shaders (OoT3D) ships a translated vertex-program
// family, fragment-family packs and title pass programs, and its scenario
// corpus and Forge preparation fill the persistent NRI pipeline cache ahead
// of play. A title without them (MM3D until its corpus exists, generic
// titles) compiles every program its draws use during the session: no
// always-available program can be linked for its draws, and the single NRI
// pipeline cache file in the renderer cache directory holds another title's
// pipelines, which its draws never match and which its own store would
// replace. For such a title the renderer keeps that cache in memory for the
// session and its draws complete their own (specialized) programs.
//
// The host installs the title's preparation before the renderer starts (as it
// installs the graphics window title); the renderer reads it when it
// initializes. The default is a prepared title: a host that installs nothing
// keeps every cache and preparation path. Content-addressed caches (SPIR-V,
// pass shaders) are not part of it: their entries are per program, never
// collide between titles, and serve the title's own next launch.

namespace Fast::Renderer3ds {

struct TitleShaderPreparation {
    bool Prepared = true;
};

struct TitleShaderPreparationPlan {
    // Load the persistent NRI pipeline cache, flush it while pipelines are
    // created and store it at exit. Otherwise the cache lives in memory for
    // the session (pipelines are still reused within it).
    bool PersistentPipelineCache = true;
    // Draws may carry an always-available (parametric) program next to the
    // specialized one (hybrid or uber preparation). Otherwise the specialized
    // program completes, as when no pipeline libraries are available.
    bool AlwaysAvailablePrograms = true;
};

[[nodiscard]] constexpr TitleShaderPreparationPlan ResolveTitleShaderPreparationPlan(
    TitleShaderPreparation title) noexcept {
    return {title.Prepared, title.Prepared};
}

namespace Detail {
inline TitleShaderPreparation& InstalledTitleShaderPreparationStorage() noexcept {
    static TitleShaderPreparation installed;
    return installed;
}
} // namespace Detail

inline void InstallTitleShaderPreparation(TitleShaderPreparation title) noexcept {
    Detail::InstalledTitleShaderPreparationStorage() = title;
}

[[nodiscard]] inline TitleShaderPreparation InstalledTitleShaderPreparation() noexcept {
    return Detail::InstalledTitleShaderPreparationStorage();
}

} // namespace Fast::Renderer3ds
