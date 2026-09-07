#pragma once

#include "oot3d/renderer/pica_render_backend.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace Fast::Oot3d {

enum class PicaReactiveCoverage : uint8_t {
    None = 0,
    SourceAlpha,
    SourceColor,
    Full,
};

struct PicaReactiveDrawInfo {
    ::Oot3d::Renderer::NativeBlendState Blend;
    ::Oot3d::Renderer::PicaCompositionDomain CompositionDomain =
        ::Oot3d::Renderer::PicaCompositionDomain::Unknown;
    uint8_t FragmentOperationMode = 0;
    bool DepthTestEnabled = false;
    uint8_t ColorWriteMask = 0;
};

struct PicaReactiveMaskPlan {
    PicaReactiveCoverage Coverage = PicaReactiveCoverage::None;

    [[nodiscard]] bool Reactive() const noexcept {
        return Coverage != PicaReactiveCoverage::None;
    }
};

struct PicaReactiveShaderVariant {
    uint64_t FragmentKey = 0;
    std::string Source;
    bool Reactive = false;
};

// A draw is reactive only when its native RGB blend operation actually reads
// the existing framebuffer. PICA commonly enables blending with ONE/ZERO,
// which is an opaque replacement and must retain temporal history.
[[nodiscard]] PicaReactiveMaskPlan ResolvePicaReactiveMaskPlan(
    const PicaReactiveDrawInfo& draw) noexcept;

// Builds the typed fragment epilogue shared by the production composer and
// the compatibility shader helper. Source-dependent blends use native output
// alpha/color as continuous coverage instead of rejecting an entire draw.
[[nodiscard]] std::string BuildPicaReactiveMaskAssignment(
    const PicaReactiveMaskPlan& plan);

[[nodiscard]] uint64_t ResolvePicaReactiveFragmentKey(
    uint64_t fragmentKey, PicaReactiveCoverage coverage) noexcept;

[[nodiscard]] PicaReactiveShaderVariant BuildPicaReactiveShaderVariant(
    std::string_view source, uint64_t fragmentKey,
    const PicaReactiveDrawInfo& draw);

} // namespace Fast::Oot3d
