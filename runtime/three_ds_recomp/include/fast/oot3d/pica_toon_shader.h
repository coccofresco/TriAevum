#pragma once

#include "fast/oot3d/graphics_settings.h"
#include "oot3d/renderer/pica_render_backend.h"
#include "oot3d/renderer/pica_shader_hooks.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace Fast::Oot3d {

struct PicaToonDrawInfo {
    ::Oot3d::Renderer::PicaCompositionDomain CompositionDomain =
        ::Oot3d::Renderer::PicaCompositionDomain::Unknown;
    bool DepthTestEnabled = false;
    bool DepthWriteEnabled = false;
    bool BlendEnabled = false;
    uint8_t ColorWriteMask = 0;
};

enum class PicaToonEligibility : uint8_t {
    Eligible,
    Disabled,
    OutsideScene,
    NoDepth,
    Blended,
    NoRgbOutput,
    UnsupportedShader,
};

struct PicaToonShaderVariant {
    std::string Source;
    uint64_t FragmentKey = 0;
    PicaToonEligibility Eligibility = PicaToonEligibility::Disabled;
    bool MaterialPath = false;

    [[nodiscard]] bool Applied() const {
        return Eligibility == PicaToonEligibility::Eligible;
    }
};

struct PicaToonInstrumentation {
    std::string Declarations;
    std::string Body;
    uint64_t FragmentKey = 0;
    ::Oot3d::Renderer::PicaShaderHook InsertionHook =
        ::Oot3d::Renderer::PicaShaderHook::BeforeDepth;
    PicaToonEligibility Eligibility = PicaToonEligibility::Disabled;
    bool MaterialPath = false;

    [[nodiscard]] bool Applied() const {
        return Eligibility == PicaToonEligibility::Eligible;
    }
};

[[nodiscard]] PicaToonEligibility ClassifyPicaToonDraw(
    const PicaToonDrawInfo& draw, ToonMode mode);

[[nodiscard]] PicaToonShaderVariant BuildPicaToonShaderVariant(
    std::string_view source, uint64_t originalFragmentKey,
    const PicaToonDrawInfo& draw, ToonMode mode,
    const ToonStyleSettings& style);

[[nodiscard]] PicaToonInstrumentation BuildPicaToonInstrumentation(
    uint64_t originalFragmentKey, const PicaToonDrawInfo& draw,
    ToonMode mode, const ToonStyleSettings& style,
    const ::Oot3d::Renderer::PicaShaderHookLayout& hooks);

} // namespace Fast::Oot3d
