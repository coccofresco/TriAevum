#include "fast/oot3d/pica_extension_schedule.h"

namespace Fast::Oot3d {

::Fast::Renderer::ExtensionPassSchedulePlan
BuildPicaEffectPassSchedulePlan(
    const CompiledEffectGraph& graph, std::string_view passName,
    EffectContractKind expectedContract) noexcept {
    if (!graph.Valid()) {
        return {};
    }
    const auto* pass = graph.FindPass(passName);
    if (pass == nullptr || pass->Contract != expectedContract) {
        return {};
    }
    return ::Fast::Renderer::BuildExtensionPassSchedulePlan(
        ::Fast::Renderer::StableExtensionPassId(pass->Name), pass->Stage);
}

} // namespace Fast::Oot3d
