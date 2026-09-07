#pragma once

#include "oot3d/renderer/ui_presentation_layout.h"

#include <cstdint>

namespace Fast {

using GfxNativePicaPresentationRect =
    ::Oot3d::Renderer::UiPresentationRect;

inline GfxNativePicaPresentationRect FitNativePicaPresentation(
    uint32_t targetWidth, uint32_t targetHeight, float logicalWidth,
    float logicalHeight) noexcept {
    return ::Oot3d::Renderer::FitUiPresentation(
        targetWidth, targetHeight, logicalWidth, logicalHeight);
}

} // namespace Fast
