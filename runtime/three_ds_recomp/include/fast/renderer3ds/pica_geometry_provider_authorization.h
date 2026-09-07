#pragma once

#include "fast/renderer/extension_contract.h"
#include "fast/renderer/extension_scene_publication.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Fast::Renderer3ds {

inline constexpr size_t kMaximumPicaGeometryProviderRequirements = 3U;

enum class PicaGeometryProviderAuthorizationStatus : uint8_t {
    Authorized,
    PlanUnavailable,
    StageMismatch,
    SceneCapabilitiesUnavailable,
    WorldGeometryUnavailable,
};

struct PicaGeometryProviderAuthorization {
    PicaGeometryProviderAuthorizationStatus Status =
        PicaGeometryProviderAuthorizationStatus::PlanUnavailable;
    ::Fast::Renderer::ExtensionSceneCapabilityResolution Capabilities;

    [[nodiscard]] bool Authorized() const noexcept {
        return Status == PicaGeometryProviderAuthorizationStatus::Authorized &&
               Capabilities.Complete();
    }
};

// Immutable per-provider authorization compiled from requirements selected by
// one 3DS title adapter. Runtime resolution is allocation-free and does not
// infer missing scene facts from backend state.
class PicaGeometryProviderAuthorizationPlan final {
  public:
    [[nodiscard]] bool Valid() const noexcept;
    [[nodiscard]] ::Fast::Renderer::ExtensionStage Stage() const noexcept;
    [[nodiscard]] std::span<const
        ::Fast::Renderer::ExtensionSceneCapabilityRequirement>
    Requirements() const noexcept;
    [[nodiscard]] PicaGeometryProviderAuthorization ResolveInputs(
        ::Fast::Renderer::ExtensionScenePublicationTableView scene) const
        noexcept;
    [[nodiscard]] PicaGeometryProviderAuthorization ResolveInvocation(
        ::Fast::Renderer::ExtensionScenePublicationTableView scene,
        ::Fast::Renderer::ExtensionStage stage,
        bool worldGeometryObserved) const noexcept;

  private:
    friend PicaGeometryProviderAuthorizationPlan
    BuildPicaGeometryProviderAuthorizationPlan(
        ::Fast::Renderer::ExtensionStage,
        std::span<const
            ::Fast::Renderer::ExtensionSceneCapabilityRequirement>) noexcept;

    std::array<::Fast::Renderer::ExtensionSceneCapabilityRequirement,
               kMaximumPicaGeometryProviderRequirements>
        mRequirements{};
    size_t mRequirementCount = 0U;
    ::Fast::Renderer::ExtensionStage mStage =
        ::Fast::Renderer::ExtensionStage::BeforeTransparent;
    bool mValid = false;
};

[[nodiscard]] PicaGeometryProviderAuthorizationPlan
BuildPicaGeometryProviderAuthorizationPlan(
    ::Fast::Renderer::ExtensionStage stage,
    std::span<const ::Fast::Renderer::ExtensionSceneCapabilityRequirement>
        requirements) noexcept;

} // namespace Fast::Renderer3ds
