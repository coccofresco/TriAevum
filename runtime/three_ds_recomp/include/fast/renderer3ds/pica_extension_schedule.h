#pragma once

#include "fast/renderer/extension_schedule.h"
#include "fast/renderer3ds/pica_composition_schedule.h"

namespace Fast::Renderer3ds {

[[nodiscard]] ::Fast::Renderer::ExtensionSurfaceIdentity
BuildPicaExtensionSurfaceIdentity(
    const PicaCompositionTargetReference& target) noexcept;

// Adapts one exact compiled PICA boundary to the renderer-neutral extension
// scheduler. Copied, stale or fabricated anchors are rejected.
[[nodiscard]] ::Fast::Renderer::ExtensionScheduleBoundary
BuildPicaExtensionScheduleBoundary(
    const PicaCompositionSchedule& schedule,
    const PicaCompositionStageAnchor& anchor) noexcept;

} // namespace Fast::Renderer3ds
