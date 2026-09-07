#include "fast/renderer3ds/pica_scene_capabilities.h"

namespace Fast::Renderer3ds {
namespace {

inline constexpr uint64_t kPica3dsSceneCapabilityNamespace =
    0x3344535049434143ULL; // 3DSPICAC
inline constexpr uint64_t kPica3dsSceneObjectNamespace =
    0x334453504943414FULL; // 3DSPICAO
inline constexpr uint32_t kPica3dsSceneObjectIdentitySchemaVersion = 1U;

} // namespace

::Fast::Renderer::ExtensionSceneCapabilityIdentity
PicaSceneCapabilityIdentity(PicaSceneCapability capability) noexcept {
    if (capability == PicaSceneCapability::Count) {
        return {};
    }
    return {
        kPica3dsSceneCapabilityNamespace,
        static_cast<uint64_t>(capability) + 1U,
    };
}

std::string_view PicaSceneCapabilityName(
    PicaSceneCapability capability) noexcept {
    switch (capability) {
        case PicaSceneCapability::ResolvedDrawStream:
            return "ResolvedDrawStream";
        case PicaSceneCapability::SemanticSceneView:
            return "SemanticSceneView";
        case PicaSceneCapability::PerspectiveCamera:
            return "PerspectiveCamera";
        case PicaSceneCapability::Count:
            break;
    }
    return "Unknown";
}

::Fast::Renderer::ExtensionSceneCapabilityPublication
BuildPicaSceneCapabilityPublication(
    PicaSceneCapability capability, const void* payload,
    uint32_t payloadSchemaVersion, uint64_t generation,
    uint64_t frameId) noexcept {
    if (payload == nullptr) {
        return {};
    }
    return {
        PicaSceneCapabilityIdentity(capability),
        {
            kPica3dsSceneObjectNamespace,
            static_cast<uint64_t>(reinterpret_cast<uintptr_t>(payload)),
            kPica3dsSceneObjectIdentitySchemaVersion,
        },
        payload,
        payloadSchemaVersion,
        generation,
        frameId,
    };
}

std::array<::Fast::Renderer::ExtensionSceneCapabilityRequirement, 3U>
BuildPicaGeometryProviderSceneRequirements() noexcept {
    const auto exact = [](
                           ::Fast::Renderer::
                               ExtensionSceneCapabilityIdentity capability,
                           uint32_t schemaVersion) {
        return ::Fast::Renderer::ExtensionSceneCapabilityRequirement{
            capability, schemaVersion, schemaVersion };
    };
    return {
        exact(PicaSceneCapabilityIdentity(
                  PicaSceneCapability::ResolvedDrawStream),
              kPicaResolvedDrawStreamSchemaVersion),
        exact(PicaSceneCapabilityIdentity(
                  PicaSceneCapability::SemanticSceneView),
              kPicaSemanticSceneViewSchemaVersion),
        exact(PicaSceneCapabilityIdentity(
                  PicaSceneCapability::PerspectiveCamera),
              kPicaPerspectiveCameraSchemaVersion),
    };
}

} // namespace Fast::Renderer3ds
