#pragma once

#include "fast/oot3d/reflection_material_profile.h"
#include "oot3d/renderer/pica_render_backend.h"
#include "oot3d/renderer/pica_shader_hooks.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace Fast::Oot3d {

struct PicaReflectionMaterialDrawInfo {
    ::Oot3d::Renderer::PicaCompositionDomain CompositionDomain =
        ::Oot3d::Renderer::PicaCompositionDomain::Unknown;
    uint8_t FragmentOperationMode = 0;
    bool DepthTestEnabled = false;
    bool DepthWriteEnabled = false;
    uint8_t ColorWriteMask = 0;
};

enum class PicaReflectionMaterialEligibility : uint8_t {
    Eligible,
    Disabled,
    OutsideScene,
    NonColorOperation,
    NoDepth,
    NoRgbOutput,
    UnsupportedShader,
    NoSpecularTevUse,
    ExplicitTextureProfile,
};

struct PicaReflectionMaterialShaderVariant {
    std::string Source;
    uint64_t FragmentKey = 0;
    PicaReflectionMaterialEligibility Eligibility =
        PicaReflectionMaterialEligibility::Disabled;

    [[nodiscard]] bool Applied() const {
        return Eligibility ==
                   PicaReflectionMaterialEligibility::Eligible ||
               Eligibility ==
                   PicaReflectionMaterialEligibility::
                       ExplicitTextureProfile;
    }
};

[[nodiscard]] PicaReflectionMaterialEligibility
ClassifyPicaReflectionMaterialDraw(
    std::string_view source,
    const PicaReflectionMaterialDrawInfo& draw,
    bool reflectionsEnabled,
    const ReflectionMaterialParameters* explicitProfile = nullptr);

// Production typed path. Instrumented MRT ownership is deliberately absent
// from the canonical shader and therefore is not an eligibility condition.
[[nodiscard]] PicaReflectionMaterialEligibility
ClassifyPicaReflectionMaterialDraw(
    const ::Oot3d::Renderer::PicaShaderHookLayout& hooks,
    const PicaReflectionMaterialDrawInfo& draw,
    bool reflectionsEnabled,
    const ReflectionMaterialParameters* explicitProfile = nullptr);

// Replaces the highlight-only guide with a stable material-level baseline
// only when the original PICA TEV actually consumes its specular secondary
// color. Color/texture output is never modified.
[[nodiscard]] PicaReflectionMaterialShaderVariant
BuildPicaReflectionMaterialShaderVariant(
    std::string_view source, uint64_t originalFragmentKey,
    const PicaReflectionMaterialDrawInfo& draw,
    bool reflectionsEnabled,
    const ReflectionMaterialParameters* explicitProfile = nullptr);

} // namespace Fast::Oot3d
