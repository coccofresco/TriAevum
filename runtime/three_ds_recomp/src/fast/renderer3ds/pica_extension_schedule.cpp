#include "fast/renderer3ds/pica_extension_schedule.h"

#include <cstddef>
#include <cstdint>

namespace Fast::Renderer3ds {
namespace {

void HashValue(uint64_t& hash, uint64_t value) noexcept {
    constexpr uint64_t kPrime = 1099511628211ULL;
    for (size_t index = 0U; index < sizeof(value); ++index) {
        hash ^= static_cast<uint8_t>(value & 0xFFU);
        hash *= kPrime;
        value >>= 8U;
    }
}

[[nodiscard]] bool SameAnchor(
    const PicaCompositionStageAnchor& left,
    const PicaCompositionStageAnchor& right) noexcept {
    return left.Stage == right.Stage && left.Target == right.Target &&
           left.BeforeDrawIndex == right.BeforeDrawIndex &&
           left.BeforeSubmissionId == right.BeforeSubmissionId &&
           left.AtTargetEnd == right.AtTargetEnd;
}

} // namespace

::Fast::Renderer::ExtensionSurfaceIdentity
BuildPicaExtensionSurfaceIdentity(
    const PicaCompositionTargetReference& target) noexcept {
    uint64_t signature = 1469598103934665603ULL;
    HashValue(signature, target.RenderTargetNamespace);
    HashValue(signature, target.ColorPhysicalAddress);
    HashValue(signature, target.DepthPhysicalAddress);
    HashValue(signature, target.FramebufferWidth);
    HashValue(signature, target.FramebufferHeight);
    HashValue(signature, target.ColorFormat);
    HashValue(signature, target.DepthFormat);
    if (signature == 0U) {
        signature = 1U;
    }
    return {
        target.RenderTargetNamespace,
        target.ColorPhysicalAddress,
        signature,
    };
}

::Fast::Renderer::ExtensionScheduleBoundary
BuildPicaExtensionScheduleBoundary(
    const PicaCompositionSchedule& schedule,
    const PicaCompositionStageAnchor& anchor) noexcept {
    if (!schedule.Valid()) {
        return {};
    }
    const auto* declared = schedule.FindAnchor(anchor.Stage, anchor.Target);
    if (declared == nullptr || !SameAnchor(*declared, anchor)) {
        return {};
    }
    return {
        schedule.SequenceId(),
        BuildPicaExtensionSurfaceIdentity(anchor.Target),
        anchor.Stage,
        anchor.AtTargetEnd
            ? ::Fast::Renderer::ExtensionScheduleBoundaryKind::SurfaceEnd
            : ::Fast::Renderer::ExtensionScheduleBoundaryKind::BeforeSubmission,
        anchor.BeforeSubmissionId,
        schedule.Reached(anchor),
    };
}

} // namespace Fast::Renderer3ds
