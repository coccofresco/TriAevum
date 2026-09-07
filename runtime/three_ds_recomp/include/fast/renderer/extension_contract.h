#pragma once

#include <cstdint>

namespace Fast::Renderer {

// Renderer-neutral extension stages. A title frontend maps its native
// composition boundaries onto these stages; providers never infer them from
// target dimensions or backend state.
enum class ExtensionStage : uint8_t {
    SceneResolved,
    BeforeDepth,
    AfterDepth,
    BeforeOpaque,
    NativeLighting,
    AfterOpaque,
    BeforeTransparent,
    AfterTransparent,
    Atmosphere,
    BeforeUi,
    AfterUi,
    Count,
};

enum class ExtensionContractKind : uint8_t {
    Observer,
    AuxiliaryOutput,
    GeometryProvider,
    LightingContributor,
    WorldPassReplacement,
    ComposerPass,
};

[[nodiscard]] constexpr bool IsValidExtensionStage(
    ExtensionStage stage) noexcept {
    return stage < ExtensionStage::Count;
}

} // namespace Fast::Renderer
