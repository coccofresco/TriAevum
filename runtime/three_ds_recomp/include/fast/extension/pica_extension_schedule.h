#pragma once

#include "fast/extension/effect_graph.h"
#include "fast/renderer3ds/pica_extension_schedule.h"

#include <string_view>

namespace Fast::Extension {

using ::Fast::Renderer3ds::BuildPicaExtensionScheduleBoundary;
using ::Fast::Renderer3ds::BuildPicaExtensionSurfaceIdentity;
using ::Fast::Renderer3ds::FindPicaEffectPassAnchor;

[[nodiscard]] ::Fast::Renderer::ExtensionPassSchedulePlan
BuildPicaEffectPassSchedulePlan(
    const CompiledEffectGraph& graph, std::string_view passName,
    EffectContractKind expectedContract) noexcept;


} // namespace Fast::Extension
