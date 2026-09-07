#pragma once

#include "fast/renderer/extension_scene_publication.h"
#include "fast/renderer3ds/pica_resolved_draw_stream.h"
#include "fast/renderer3ds/pica_scene_payloads.h"
#include "fast/renderer3ds/pica_semantic_scene_view.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace Fast::Renderer3ds {

// Shared PICA scene capabilities. Each 3DS title publishes these payloads from
// its own scene/game adapter without changing their renderer-facing schemas.
enum class PicaSceneCapability : uint8_t {
    ResolvedDrawStream,
    SemanticSceneView,
    PerspectiveCamera,
    Count,
};

[[nodiscard]] ::Fast::Renderer::ExtensionSceneCapabilityIdentity
PicaSceneCapabilityIdentity(PicaSceneCapability capability) noexcept;

[[nodiscard]] std::string_view PicaSceneCapabilityName(
    PicaSceneCapability capability) noexcept;

[[nodiscard]] ::Fast::Renderer::ExtensionSceneCapabilityPublication
BuildPicaSceneCapabilityPublication(
    PicaSceneCapability capability, const void* payload,
    uint32_t payloadSchemaVersion, uint64_t generation,
    uint64_t frameId) noexcept;

[[nodiscard]] std::array<
    ::Fast::Renderer::ExtensionSceneCapabilityRequirement, 3U>
BuildPicaGeometryProviderSceneRequirements() noexcept;

} // namespace Fast::Renderer3ds
