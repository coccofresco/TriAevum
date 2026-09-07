#pragma once

#include "fast/oot3d/graphics_settings.h"
#include "fast/oot3d/presentation_settings_transaction.h"

#include <cstdint>
#include <optional>

namespace Fast::Oot3d {

enum class RendererPresentationRequestKind : uint8_t {
    Initial = 0,
    Candidate = 1,
    Rollback = 2,
};

struct RendererPresentationRequest {
    PresentationSettingsValue Value;
    RendererPresentationRequestKind Kind =
        RendererPresentationRequestKind::Initial;

    [[nodiscard]] bool RequiresAcknowledgement() const {
        return Kind != RendererPresentationRequestKind::Initial;
    }
};

[[nodiscard]] std::optional<RendererPresentationRequest>
ResolveRendererPresentationRequest(
    const GraphicsSettings& settings,
    const PresentationTransactionStatus& transaction,
    bool rendererHasAppliedSettings);

} // namespace Fast::Oot3d
