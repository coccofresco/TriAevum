#include "fast/renderer3ds/title_shader_preparation.h"

#include <iostream>
#include <stdexcept>
#include <string>

// Renderer3ds::kNriPipelineCacheFilename (its header needs Vulkan).
constexpr const char *kNriCacheFile = "nri_pipeline_cache.bin";

int main() {
    using namespace Fast::Renderer3ds;
    const auto require = [](bool value, const char* message) {
        if (!value) throw std::runtime_error(message);
    };
    try {
        // A prepared title links always-available programs; an unprepared
        // one completes its specialized programs.
        require(ResolveTitleShaderPreparationPlan({true, {}}).AlwaysAvailablePrograms,
                "a prepared title loses always-available programs");
        require(!ResolveTitleShaderPreparationPlan({false, "mm3d"}).AlwaysAvailablePrograms,
                "an unprepared title keeps always-available programs");

        // A host that installs nothing runs as a prepared title with the
        // original cache file names (OoT3D's before titles declared theirs).
        require(InstalledTitleShaderPreparation().Prepared, "default installation is not prepared");
        require(InstalledTitleShaderPreparation().PipelineCacheNamespace.empty(),
                "default installation renames the cache files");

        // Pipeline cache files: the empty namespace keeps the names Forge and
        // the preparation tool write; a title's namespace gives its own files.
        require(TitlePipelineCacheFilename(kNriCacheFile, "") == kNriCacheFile,
                "OoT3D's NRI cache file name changed");
        require(TitlePipelineCacheFilename("pipeline_cache.bin", "") == "pipeline_cache.bin",
                "OoT3D's Vulkan cache file name changed");
        require(TitlePipelineCacheFilename(kNriCacheFile, "mm3d") == "nri_pipeline_cache.mm3d.bin",
                "MM3D's NRI cache file is not its own");
        require(TitlePipelineCacheFilename("pipeline_cache.bin", "mm3d") == "pipeline_cache.mm3d.bin",
                "MM3D's Vulkan cache file is not its own");
        require(TitlePipelineCacheFilename("cache", "generic") == "cache.generic", "extensionless name");
        // An invalid namespace never reaches a file (in particular not another
        // title's, or a path outside the directory).
        require(TitlePipelineCacheFilename(kNriCacheFile, "../x").empty(), "path namespace accepted");
        require(TitlePipelineCacheFilename(kNriCacheFile, "MM3D").empty(), "upper case accepted");
        require(TitlePipelineCacheFilename(kNriCacheFile, std::string(33, 'a')).empty(),
                "overlong namespace accepted");

        // The renderer reads what the host installed last; flipping the
        // capability back restores the work.
        InstallTitleShaderPreparation({false, "mm3d"});
        require(!ResolveTitleShaderPreparationPlan(InstalledTitleShaderPreparation()).AlwaysAvailablePrograms,
                "unprepared installation not kept");
        require(InstalledTitleShaderPreparation().PipelineCacheNamespace == "mm3d", "namespace not kept");
        InstallTitleShaderPreparation({true, "mm3d"});
        require(ResolveTitleShaderPreparationPlan(InstalledTitleShaderPreparation()).AlwaysAvailablePrograms,
                "prepared installation does not restore always-available programs");
    } catch (const std::exception& error) {
        std::cerr << "title_shader_preparation_tests: " << error.what() << '\n';
        return 1;
    }
    std::cout << "title_shader_preparation_tests: ok\n";
    return 0;
}
