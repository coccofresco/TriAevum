#pragma once

#include "fast/extension/graphics_settings.h"
#include "fast/extension/presentation_settings_transaction.h"

#include <cstdint>
#include <optional>

namespace Fast::Extension {

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

} // namespace Fast::Extension
