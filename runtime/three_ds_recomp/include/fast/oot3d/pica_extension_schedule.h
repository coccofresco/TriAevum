#pragma once

#include "fast/oot3d/effect_graph.h"
#include "fast/renderer3ds/pica_extension_schedule.h"

#include <string_view>

namespace Fast::Oot3d {

using ::Fast::Renderer3ds::BuildPicaExtensionScheduleBoundary;
using ::Fast::Renderer3ds::BuildPicaExtensionSurfaceIdentity;

[[nodiscard]] ::Fast::Renderer::ExtensionPassSchedulePlan
BuildPicaEffectPassSchedulePlan(
    const CompiledEffectGraph& graph, std::string_view passName,
    EffectContractKind expectedContract) noexcept;

} // namespace Fast::Oot3d
