#include "fast/oot3d/renderer_presentation_controller.h"

namespace Fast::Oot3d {

std::optional<RendererPresentationRequest>
ResolveRendererPresentationRequest(
    const GraphicsSettings& settings,
    const PresentationTransactionStatus& transaction,
    bool rendererHasAppliedSettings) {
    switch (transaction.Phase) {
        case PresentationTransactionPhase::ApplyRequested:
            return RendererPresentationRequest{
                transaction.Requested,
                RendererPresentationRequestKind::Candidate,
            };
        case PresentationTransactionPhase::RollbackRequested:
            return RendererPresentationRequest{
                transaction.LastKnownGood,
                RendererPresentationRequestKind::Rollback,
            };
        case PresentationTransactionPhase::Idle:
        case PresentationTransactionPhase::AwaitingConfirmation:
            break;
    }

    if (!rendererHasAppliedSettings) {
        return RendererPresentationRequest{
            GetPresentationSettings(settings),
            RendererPresentationRequestKind::Initial,
        };
    }
    return std::nullopt;
}

} // namespace Fast::Oot3d
