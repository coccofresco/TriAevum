#pragma once

#include "fast/renderer/extension_contract.h"

#include <cstdint>
#include <string_view>

namespace Fast::Renderer {

struct ExtensionSurfaceIdentity {
    uint64_t Namespace = 0U;
    uint64_t Resource = 0U;
    uint64_t Signature = 0U;

    [[nodiscard]] bool Valid() const noexcept {
        return Signature != 0U;
    }

    bool operator==(const ExtensionSurfaceIdentity&) const = default;
};

enum class ExtensionScheduleBoundaryKind : uint8_t {
    BeforeSubmission,
    SurfaceEnd,
};

struct ExtensionScheduleBoundary {
    uint64_t ScheduleId = 0U;
    ExtensionSurfaceIdentity Surface;
    ExtensionStage Stage = ExtensionStage::SceneResolved;
    ExtensionScheduleBoundaryKind Kind =
        ExtensionScheduleBoundaryKind::BeforeSubmission;
    uint64_t BeforeSubmissionId = 0U;
    bool Reached = false;

    [[nodiscard]] bool StructurallyValid() const noexcept;
};

enum class ExtensionPassScheduleDecisionReason : uint8_t {
    Authorized,
    PlanUnavailable,
    BoundaryUnavailable,
    StageMismatch,
    BoundaryNotReached,
};

struct ExtensionPassAuthorization {
    uint64_t PassId = 0U;
    uint64_t ScheduleId = 0U;
    ExtensionSurfaceIdentity Surface;
    ExtensionStage Stage = ExtensionStage::SceneResolved;
    ExtensionScheduleBoundaryKind BoundaryKind =
        ExtensionScheduleBoundaryKind::BeforeSubmission;
    uint64_t BeforeSubmissionId = 0U;

    [[nodiscard]] bool Valid() const noexcept;
    [[nodiscard]] bool Matches(
        uint64_t expectedPassId, ExtensionStage expectedStage,
        const ExtensionSurfaceIdentity& expectedSurface) const noexcept;
};

struct ExtensionPassScheduleDecision {
    ExtensionPassScheduleDecisionReason Reason =
        ExtensionPassScheduleDecisionReason::PlanUnavailable;
    ExtensionPassAuthorization Authorization;

    [[nodiscard]] bool Authorized() const noexcept {
        return Reason == ExtensionPassScheduleDecisionReason::Authorized &&
               Authorization.Valid();
    }
};

class ExtensionPassSchedulePlan final {
  public:
    [[nodiscard]] bool Valid() const noexcept;
    [[nodiscard]] uint64_t PassId() const noexcept;
    [[nodiscard]] ExtensionStage Stage() const noexcept;
    [[nodiscard]] ExtensionPassScheduleDecision Resolve(
        const ExtensionScheduleBoundary& boundary) const noexcept;

  private:
    friend ExtensionPassSchedulePlan BuildExtensionPassSchedulePlan(
        uint64_t, ExtensionStage) noexcept;

    uint64_t mPassId = 0U;
    ExtensionStage mStage = ExtensionStage::SceneResolved;
    bool mValid = false;
};

[[nodiscard]] uint64_t StableExtensionPassId(
    std::string_view name) noexcept;

[[nodiscard]] ExtensionPassSchedulePlan BuildExtensionPassSchedulePlan(
    uint64_t passId, ExtensionStage stage) noexcept;

} // namespace Fast::Renderer
