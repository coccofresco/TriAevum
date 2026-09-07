#include "fast/renderer/extension_schedule.h"

namespace Fast::Renderer {

bool ExtensionScheduleBoundary::StructurallyValid() const noexcept {
    if (ScheduleId == 0U || !Surface.Valid() ||
        !IsValidExtensionStage(Stage)) {
        return false;
    }
    return Kind == ExtensionScheduleBoundaryKind::SurfaceEnd
        ? BeforeSubmissionId == 0U
        : BeforeSubmissionId != 0U;
}

bool ExtensionPassAuthorization::Valid() const noexcept {
    if (PassId == 0U || ScheduleId == 0U || !Surface.Valid() ||
        !IsValidExtensionStage(Stage)) {
        return false;
    }
    return BoundaryKind == ExtensionScheduleBoundaryKind::SurfaceEnd
        ? BeforeSubmissionId == 0U
        : BeforeSubmissionId != 0U;
}

bool ExtensionPassAuthorization::Matches(
    uint64_t expectedPassId, ExtensionStage expectedStage,
    const ExtensionSurfaceIdentity& expectedSurface) const noexcept {
    return Valid() && PassId == expectedPassId && Stage == expectedStage &&
           Surface == expectedSurface;
}

bool ExtensionPassSchedulePlan::Valid() const noexcept {
    return mValid;
}

uint64_t ExtensionPassSchedulePlan::PassId() const noexcept {
    return mPassId;
}

ExtensionStage ExtensionPassSchedulePlan::Stage() const noexcept {
    return mStage;
}

ExtensionPassScheduleDecision ExtensionPassSchedulePlan::Resolve(
    const ExtensionScheduleBoundary& boundary) const noexcept {
    if (!mValid) {
        return {ExtensionPassScheduleDecisionReason::PlanUnavailable, {}};
    }
    if (!boundary.StructurallyValid()) {
        return {ExtensionPassScheduleDecisionReason::BoundaryUnavailable, {}};
    }
    if (boundary.Stage != mStage) {
        return {ExtensionPassScheduleDecisionReason::StageMismatch, {}};
    }
    if (!boundary.Reached) {
        return {ExtensionPassScheduleDecisionReason::BoundaryNotReached, {}};
    }
    return {
        ExtensionPassScheduleDecisionReason::Authorized,
        {
            mPassId,
            boundary.ScheduleId,
            boundary.Surface,
            boundary.Stage,
            boundary.Kind,
            boundary.BeforeSubmissionId,
        },
    };
}

uint64_t StableExtensionPassId(std::string_view name) noexcept {
    constexpr uint64_t kOffset = 1469598103934665603ULL;
    constexpr uint64_t kPrime = 1099511628211ULL;
    uint64_t hash = kOffset;
    for (const char character : name) {
        hash ^= static_cast<uint8_t>(character);
        hash *= kPrime;
    }
    return name.empty() || hash == 0U ? 0U : hash;
}

ExtensionPassSchedulePlan BuildExtensionPassSchedulePlan(
    uint64_t passId, ExtensionStage stage) noexcept {
    ExtensionPassSchedulePlan result;
    if (passId == 0U || !IsValidExtensionStage(stage)) {
        return result;
    }
    result.mPassId = passId;
    result.mStage = stage;
    result.mValid = true;
    return result;
}

} // namespace Fast::Renderer
