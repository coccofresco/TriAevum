#include "fast/renderer3ds/title_shader_preparation.h"

#include <iostream>
#include <stdexcept>

int main() {
    using namespace Fast::Renderer3ds;
    const auto require = [](bool value, const char* message) {
        if (!value) throw std::runtime_error(message);
    };
    try {
        // A prepared title keeps every persistent cache and preparation path.
        constexpr auto prepared = ResolveTitleShaderPreparationPlan({true});
        static_assert(prepared.PersistentPipelineCache && prepared.AlwaysAvailablePrograms);
        // A title without prepared shaders: session-only NRI pipeline cache, and
        // draws complete their specialized programs.
        constexpr auto unprepared = ResolveTitleShaderPreparationPlan({false});
        static_assert(!unprepared.PersistentPipelineCache && !unprepared.AlwaysAvailablePrograms);

        // A host that installs nothing runs as a prepared title (OoT3D's
        // behaviour before titles declared their preparation).
        require(InstalledTitleShaderPreparation().Prepared, "default installation is not prepared");
        const auto defaultPlan = ResolveTitleShaderPreparationPlan(InstalledTitleShaderPreparation());
        require(defaultPlan.PersistentPipelineCache && defaultPlan.AlwaysAvailablePrograms,
                "default plan drops a cache or preparation path");

        // The renderer reads what the host installed last; flipping the
        // capability back restores the work.
        InstallTitleShaderPreparation({false});
        require(!InstalledTitleShaderPreparation().Prepared, "unprepared installation not kept");
        require(!ResolveTitleShaderPreparationPlan(InstalledTitleShaderPreparation()).PersistentPipelineCache,
                "unprepared title persists the pipeline cache");
        InstallTitleShaderPreparation({true});
        const auto restored = ResolveTitleShaderPreparationPlan(InstalledTitleShaderPreparation());
        require(restored.PersistentPipelineCache && restored.AlwaysAvailablePrograms,
                "prepared installation does not restore the caches");
    } catch (const std::exception& error) {
        std::cerr << "title_shader_preparation_tests: " << error.what() << '\n';
        return 1;
    }
    std::cout << "title_shader_preparation_tests: ok\n";
    return 0;
}
