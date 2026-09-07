#include "fast/renderer3ds/pica_geometry_provider_authorization.h"

namespace Fast::Renderer3ds {

bool PicaGeometryProviderAuthorizationPlan::Valid() const noexcept {
    return mValid;
}

::Fast::Renderer::ExtensionStage
PicaGeometryProviderAuthorizationPlan::Stage() const noexcept {
    return mStage;
}

std::span<const ::Fast::Renderer::ExtensionSceneCapabilityRequirement>
PicaGeometryProviderAuthorizationPlan::Requirements() const noexcept {
    return { mRequirements.data(), mRequirementCount };
}

PicaGeometryProviderAuthorization
PicaGeometryProviderAuthorizationPlan::ResolveInputs(
    ::Fast::Renderer::ExtensionScenePublicationTableView scene) const
    noexcept {
    if (!mValid) {
        return {};
    }
    auto capabilities =
        ::Fast::Renderer::ResolveExtensionSceneCapabilities(
            scene, Requirements());
    if (!capabilities.Complete()) {
        return {
            PicaGeometryProviderAuthorizationStatus::
                SceneCapabilitiesUnavailable,
            capabilities,
        };
    }
    return {
        PicaGeometryProviderAuthorizationStatus::Authorized,
        capabilities,
    };
}

PicaGeometryProviderAuthorization
PicaGeometryProviderAuthorizationPlan::ResolveInvocation(
    ::Fast::Renderer::ExtensionScenePublicationTableView scene,
    ::Fast::Renderer::ExtensionStage stage,
    bool worldGeometryObserved) const noexcept {
    auto decision = ResolveInputs(scene);
    if (!decision.Authorized()) {
        return decision;
    }
    if (stage != mStage) {
        decision.Status =
            PicaGeometryProviderAuthorizationStatus::StageMismatch;
        return decision;
    }
    if (!worldGeometryObserved) {
        decision.Status = PicaGeometryProviderAuthorizationStatus::
            WorldGeometryUnavailable;
        return decision;
    }
    return decision;
}

PicaGeometryProviderAuthorizationPlan
BuildPicaGeometryProviderAuthorizationPlan(
    ::Fast::Renderer::ExtensionStage stage,
    std::span<const ::Fast::Renderer::ExtensionSceneCapabilityRequirement>
        requirements) noexcept {
    PicaGeometryProviderAuthorizationPlan plan;
    if (!::Fast::Renderer::IsValidExtensionStage(stage) ||
        requirements.size() > plan.mRequirements.size()) {
        return plan;
    }
    for (size_t index = 0U; index < requirements.size(); ++index) {
        if (!requirements[index].Valid()) {
            return plan;
        }
        for (size_t previous = 0U; previous < index; ++previous) {
            if (requirements[previous].Capability ==
                requirements[index].Capability) {
                return plan;
            }
        }
        plan.mRequirements[index] = requirements[index];
    }
    plan.mRequirementCount = requirements.size();
    plan.mStage = stage;
    plan.mValid = true;
    return plan;
}

} // namespace Fast::Renderer3ds
