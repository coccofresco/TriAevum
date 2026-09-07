#include "fast/oot3d/effect_geometry_provider_plan.h"
#include "fast/oot3d/pica_scene_publication_adapter.h"

#include <algorithm>
#include <array>
#include <span>

namespace Fast::Oot3d {
namespace {

[[nodiscard]] constexpr uint32_t ResourceBit(
    EffectResource resource) noexcept {
    return 1U << static_cast<uint32_t>(resource);
}

[[nodiscard]] constexpr bool IsInputUnavailable(
    EffectGeometryProviderDecisionReason reason) noexcept {
    return reason ==
               EffectGeometryProviderDecisionReason::PicaSceneFrameUnavailable ||
           reason ==
               EffectGeometryProviderDecisionReason::NativeSceneViewUnavailable ||
           reason ==
               EffectGeometryProviderDecisionReason::ScenePublicationInvalid ||
           reason ==
               EffectGeometryProviderDecisionReason::SceneCapabilitySchemaMismatch ||
           reason ==
               EffectGeometryProviderDecisionReason::WorldGeometryUnavailable;
}

[[nodiscard]] EffectGeometryProviderDecisionReason
ResolveCapabilityFailure(
    const ::Fast::Renderer::ExtensionSceneCapabilityResolution& resolution) noexcept {
    using ::Fast::Renderer::ExtensionSceneCapabilityResolutionStatus;
    using ::Fast::Renderer3ds::PicaSceneCapability;
    switch (resolution.Status) {
        case ExtensionSceneCapabilityResolutionStatus::Ready:
            return EffectGeometryProviderDecisionReason::Authorized;
        case ExtensionSceneCapabilityResolutionStatus::InvalidPublications:
            return EffectGeometryProviderDecisionReason::ScenePublicationInvalid;
        case ExtensionSceneCapabilityResolutionStatus::InvalidRequirements:
        case ExtensionSceneCapabilityResolutionStatus::IncompatibleSchema:
            return EffectGeometryProviderDecisionReason::SceneCapabilitySchemaMismatch;
        case ExtensionSceneCapabilityResolutionStatus::MissingCapability:
            if (resolution.FailedCapability ==
                ::Fast::Renderer3ds::PicaSceneCapabilityIdentity(
                    PicaSceneCapability::ResolvedDrawStream)) {
                return EffectGeometryProviderDecisionReason::PicaSceneFrameUnavailable;
            }
            return EffectGeometryProviderDecisionReason::NativeSceneViewUnavailable;
    }
    return EffectGeometryProviderDecisionReason::ScenePublicationInvalid;
}

[[nodiscard]] EffectGeometryProviderDecision ResolveAuthorization(
    const ::Fast::Renderer3ds::PicaGeometryProviderAuthorization&
        authorization) noexcept {
    using ::Fast::Renderer3ds::PicaGeometryProviderAuthorizationStatus;
    switch (authorization.Status) {
        case PicaGeometryProviderAuthorizationStatus::Authorized:
            return { EffectGeometryProviderDecisionReason::Authorized };
        case PicaGeometryProviderAuthorizationStatus::PlanUnavailable:
            return { EffectGeometryProviderDecisionReason::PlanUnavailable };
        case PicaGeometryProviderAuthorizationStatus::StageMismatch:
            return { EffectGeometryProviderDecisionReason::StageMismatch };
        case PicaGeometryProviderAuthorizationStatus::
            SceneCapabilitiesUnavailable:
            return {
                ResolveCapabilityFailure(authorization.Capabilities) };
        case PicaGeometryProviderAuthorizationStatus::
            WorldGeometryUnavailable:
            return {
                EffectGeometryProviderDecisionReason::
                    WorldGeometryUnavailable };
    }
    return { EffectGeometryProviderDecisionReason::PlanUnavailable };
}

} // namespace

bool EffectGeometryProviderPlan::Valid() const noexcept {
    return mValid;
}

EffectStage EffectGeometryProviderPlan::Stage() const noexcept {
    return mAuthorization.Stage();
}

bool EffectGeometryProviderPlan::Requires(
    EffectResource resource) const noexcept {
    const auto index = static_cast<uint32_t>(resource);
    return index < 32U && (mReadMask & ResourceBit(resource)) != 0U;
}

bool EffectGeometryProviderPlan::ProducesExtensionGeometry() const noexcept {
    return mProducesExtensionGeometry;
}

bool EffectGeometryProviderPlan::ExportsExtensionGeometry() const noexcept {
    return mExportsExtensionGeometry;
}

EffectGeometryProviderDecision EffectGeometryProviderPlan::ResolveInputs(
    const EffectGeometryProviderInputs& inputs) const noexcept {
    if (!mValid) {
        return { EffectGeometryProviderDecisionReason::PlanUnavailable };
    }
    return ResolveAuthorization(mAuthorization.ResolveInputs(inputs.Scene));
}

EffectGeometryProviderDecision
EffectGeometryProviderPlan::ResolveInvocation(
    const EffectGeometryProviderInvocation& invocation) const noexcept {
    const auto authorization = mAuthorization.ResolveInvocation(
        invocation.Inputs.Scene, invocation.Stage,
        invocation.WorldGeometryObserved);
    if (authorization.Status ==
            ::Fast::Renderer3ds::
                PicaGeometryProviderAuthorizationStatus::
                    SceneCapabilitiesUnavailable ||
        authorization.Status ==
            ::Fast::Renderer3ds::
                PicaGeometryProviderAuthorizationStatus::PlanUnavailable ||
        authorization.Status ==
            ::Fast::Renderer3ds::
                PicaGeometryProviderAuthorizationStatus::StageMismatch) {
        return ResolveAuthorization(authorization);
    }
    if (invocation.Kind ==
            EffectGeometryProviderInvocationKind::NativeComposerFallback &&
        Stage() != EffectStage::BeforeTransparent) {
        return { EffectGeometryProviderDecisionReason::FallbackUnsupported };
    }
    return ResolveAuthorization(authorization);
}

EffectGeometryProviderPlan BuildEffectGeometryProviderPlan(
    const CompiledEffectGraph& graph,
    std::string_view passName) noexcept {
    EffectGeometryProviderPlan plan;
    if (!graph.Valid()) {
        return plan;
    }
    const auto* pass = graph.FindPass(passName);
    if (pass == nullptr ||
        pass->Contract != EffectContractKind::GeometryProvider) {
        return plan;
    }

    bool unsupportedWrite = false;
    for (const auto& use : pass->Resources) {
        const auto index = static_cast<uint32_t>(use.Resource);
        if (ReadsEffectResource(use.Access) && index < 32U) {
            plan.mReadMask |= ResourceBit(use.Resource);
        }
        if (!WritesEffectResource(use.Access)) {
            continue;
        }
        if (use.Resource == EffectResource::ExtensionGeometry) {
            plan.mProducesExtensionGeometry = true;
        } else {
            unsupportedWrite = true;
        }
    }
    plan.mExportsExtensionGeometry =
        graph.ExportsResource(EffectResource::ExtensionGeometry);
    const auto sceneRequirements =
        BuildOot3dGeometryProviderSceneRequirements();
    std::array<::Fast::Renderer::ExtensionSceneCapabilityRequirement, 3U>
        selectedRequirements{};
    size_t selectedRequirementCount = 0U;
    if (plan.Requires(EffectResource::PicaSceneFrame)) {
        selectedRequirements[selectedRequirementCount++] =
            sceneRequirements[0];
    }
    if (plan.Requires(EffectResource::NativeSceneView)) {
        selectedRequirements[selectedRequirementCount++] =
            sceneRequirements[1];
        selectedRequirements[selectedRequirementCount++] =
            sceneRequirements[2];
    }
    plan.mValid = !unsupportedWrite &&
                  plan.mProducesExtensionGeometry &&
                  plan.mExportsExtensionGeometry;
    if (plan.mValid) {
        plan.mAuthorization = ::Fast::Renderer3ds::
            BuildPicaGeometryProviderAuthorizationPlan(
                pass->Stage,
                std::span<const
                    ::Fast::Renderer::ExtensionSceneCapabilityRequirement>(
                    selectedRequirements.data(),
                    selectedRequirementCount));
        plan.mValid = plan.mAuthorization.Valid();
    }
    return plan;
}

void EffectGeometryProviderExecutionLedger::Reset(
    const EffectGeometryProviderPlan& plan) noexcept {
    mSummary = {};
    mSummary.Declared = plan.Valid();
    mSummary.Stage = plan.Stage();
}

void EffectGeometryProviderExecutionLedger::RecordSourceInspection(
    const EffectGeometryProviderDecision& decision,
    bool published) noexcept {
    ++mSummary.SourceInspectionCount;
    if (!decision.Authorized()) {
        if (IsInputUnavailable(decision.Reason)) {
            ++mSummary.InputUnavailableCount;
        } else {
            ++mSummary.ScheduleRejectedCount;
        }
        return;
    }
    mSummary.SourcePublishedCount += published ? 1U : 0U;
}

void EffectGeometryProviderExecutionLedger::RecordInvocation(
    EffectGeometryProviderInvocationKind kind,
    const EffectGeometryProviderDecision& decision,
    EffectGeometryProviderExecutionOutcome outcome) noexcept {
    if (kind ==
        EffectGeometryProviderInvocationKind::NativeComposerFallback) {
        ++mSummary.FallbackInvocationCount;
    } else {
        ++mSummary.DeclaredBoundaryInvocationCount;
    }
    if (!decision.Authorized()) {
        if (IsInputUnavailable(decision.Reason)) {
            ++mSummary.InputUnavailableCount;
        } else {
            ++mSummary.ScheduleRejectedCount;
        }
        ++mSummary.SkippedCount;
        return;
    }
    switch (outcome) {
        case EffectGeometryProviderExecutionOutcome::Executed:
            ++mSummary.ExecutedCount;
            break;
        case EffectGeometryProviderExecutionOutcome::Reused:
            ++mSummary.ReusedCount;
            break;
        case EffectGeometryProviderExecutionOutcome::Skipped:
            ++mSummary.SkippedCount;
            break;
        case EffectGeometryProviderExecutionOutcome::Failed:
            ++mSummary.FailedCount;
            break;
    }
}

const EffectGeometryProviderExecutionSummary&
EffectGeometryProviderExecutionLedger::Summary() const noexcept {
    return mSummary;
}

} // namespace Fast::Oot3d
