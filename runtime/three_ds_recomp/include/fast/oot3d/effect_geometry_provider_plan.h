#pragma once

#include "fast/oot3d/effect_graph.h"
#include "fast/renderer/extension_scene_publication.h"
#include "fast/renderer3ds/pica_geometry_provider_authorization.h"

#include <cstdint>
#include <string_view>

namespace Fast::Oot3d {

enum class EffectGeometryProviderInvocationKind : uint8_t {
    DeclaredBoundary,
    NativeComposerFallback,
};

enum class EffectGeometryProviderDecisionReason : uint8_t {
    Authorized,
    PlanUnavailable,
    StageMismatch,
    FallbackUnsupported,
    PicaSceneFrameUnavailable,
    NativeSceneViewUnavailable,
    ScenePublicationInvalid,
    SceneCapabilitySchemaMismatch,
    WorldGeometryUnavailable,
};

struct EffectGeometryProviderInputs {
    ::Fast::Renderer::ExtensionScenePublicationTableView Scene;
};

struct EffectGeometryProviderInvocation {
    EffectGeometryProviderInvocationKind Kind =
        EffectGeometryProviderInvocationKind::DeclaredBoundary;
    EffectStage Stage = EffectStage::BeforeTransparent;
    EffectGeometryProviderInputs Inputs;
    bool WorldGeometryObserved = false;
};

struct EffectGeometryProviderDecision {
    EffectGeometryProviderDecisionReason Reason =
        EffectGeometryProviderDecisionReason::PlanUnavailable;

    [[nodiscard]] bool Authorized() const noexcept {
        return Reason == EffectGeometryProviderDecisionReason::Authorized;
    }
};

class EffectGeometryProviderPlan final {
  public:
    [[nodiscard]] bool Valid() const noexcept;
    [[nodiscard]] EffectStage Stage() const noexcept;
    [[nodiscard]] bool Requires(EffectResource resource) const noexcept;
    [[nodiscard]] bool ProducesExtensionGeometry() const noexcept;
    [[nodiscard]] bool ExportsExtensionGeometry() const noexcept;
    [[nodiscard]] EffectGeometryProviderDecision ResolveInputs(
        const EffectGeometryProviderInputs& inputs) const noexcept;
    [[nodiscard]] EffectGeometryProviderDecision ResolveInvocation(
        const EffectGeometryProviderInvocation& invocation) const noexcept;

  private:
    friend EffectGeometryProviderPlan BuildEffectGeometryProviderPlan(
        const CompiledEffectGraph&, std::string_view) noexcept;

    uint32_t mReadMask = 0U;
    ::Fast::Renderer3ds::PicaGeometryProviderAuthorizationPlan
        mAuthorization;
    bool mProducesExtensionGeometry = false;
    bool mExportsExtensionGeometry = false;
    bool mValid = false;
};

[[nodiscard]] EffectGeometryProviderPlan BuildEffectGeometryProviderPlan(
    const CompiledEffectGraph& graph,
    std::string_view passName) noexcept;

enum class EffectGeometryProviderExecutionOutcome : uint8_t {
    Executed,
    Reused,
    Skipped,
    Failed,
};

struct EffectGeometryProviderExecutionSummary {
    bool Declared = false;
    EffectStage Stage = EffectStage::BeforeTransparent;
    uint32_t SourceInspectionCount = 0U;
    uint32_t SourcePublishedCount = 0U;
    uint32_t DeclaredBoundaryInvocationCount = 0U;
    uint32_t FallbackInvocationCount = 0U;
    uint32_t ExecutedCount = 0U;
    uint32_t ReusedCount = 0U;
    uint32_t SkippedCount = 0U;
    uint32_t FailedCount = 0U;
    uint32_t InputUnavailableCount = 0U;
    uint32_t ScheduleRejectedCount = 0U;

    [[nodiscard]] bool Successful() const noexcept {
        return FailedCount == 0U && ScheduleRejectedCount == 0U;
    }
};

class EffectGeometryProviderExecutionLedger final {
  public:
    void Reset(const EffectGeometryProviderPlan& plan) noexcept;
    void RecordSourceInspection(
        const EffectGeometryProviderDecision& decision,
        bool published) noexcept;
    void RecordInvocation(
        EffectGeometryProviderInvocationKind kind,
        const EffectGeometryProviderDecision& decision,
        EffectGeometryProviderExecutionOutcome outcome) noexcept;
    [[nodiscard]] const EffectGeometryProviderExecutionSummary& Summary()
        const noexcept;

  private:
    EffectGeometryProviderExecutionSummary mSummary;
};

} // namespace Fast::Oot3d
