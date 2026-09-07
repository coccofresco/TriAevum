#include "fast/oot3d/pica_directional_shadow_lighting.h"

#include "fast/oot3d/pica_directional_shadow_semantics.h"

#include <algorithm>
#include <string_view>

namespace Fast::Oot3d {
namespace {

uint64_t LightingVariantKey(uint64_t original, bool fragmentPrimary) {
    constexpr uint64_t kOffset = 1469598103934665603ULL;
    constexpr uint64_t kPrime = 1099511628211ULL;
    constexpr std::string_view kSalt =
        "OOT3D_PICA_DIRECTIONAL_SHADOW_LIGHTING_V2";
    uint64_t hash = kOffset;
    for (unsigned shift = 0U; shift < 64U; shift += 8U) {
        hash ^= static_cast<uint8_t>(original >> shift);
        hash *= kPrime;
    }
    for (const char value : kSalt) {
        hash ^= static_cast<uint8_t>(value);
        hash *= kPrime;
    }
    hash ^= fragmentPrimary ? 1U : 0U;
    return hash * kPrime;
}

std::string LightingDeclarations() {
    return R"glsl(layout(set=0,binding=10) uniform sampler2D oot3d_directional_shadow_map;
layout(set=0,binding=12,std140) uniform Oot3dDirectionalShadowReceiver {
    mat4 view_to_shadow_texture;
    vec4 shadow_state;
    vec4 shadowed_direct_fraction;
    uvec4 extent;
} oot3d_directional_shadow;
float oot3d_directional_shadow_texel(
    ivec2 location, ivec2 map_extent, float receiver_depth) {
    float blocker = texelFetch(
        oot3d_directional_shadow_map,
        clamp(location, ivec2(0), map_extent - ivec2(1)), 0).r;
    return receiver_depth > blocker ? 1.0 : 0.0;
}
float oot3d_directional_shadow_axis_weight(
    int offset, int radius, float fraction) {
    if (offset == -radius) {
        return 1.0 - fraction;
    }
    if (offset == radius + 1) {
        return fraction;
    }
    return 1.0;
}
vec3 oot3d_directional_shadow_visibility() {
    uint flags = oot3d_directional_shadow.extent.w;
    if ((flags & 1u) == 0u ||
        oot3d_directional_shadow.extent.x == 0u ||
        oot3d_directional_shadow.extent.y == 0u) {
        return vec3(1.0);
    }
    vec4 shadow_h =
        oot3d_directional_shadow.view_to_shadow_texture *
        vec4(-pica_view, 1.0);
    if (abs(shadow_h.w) <= 1.0e-7) {
        return vec3(1.0);
    }
    vec3 shadow = shadow_h.xyz / shadow_h.w;
    if (any(lessThan(shadow, vec3(0.0))) ||
        any(greaterThan(shadow, vec3(1.0)))) {
        return vec3(1.0);
    }
    ivec2 map_extent = textureSize(oot3d_directional_shadow_map, 0);
    vec2 texel_position =
        shadow.xy * vec2(map_extent) - vec2(0.5);
    ivec2 texel_base = ivec2(floor(texel_position));
    vec2 texel_fraction = fract(texel_position);
    int radius = int(min(oot3d_directional_shadow.extent.z, 2u));
    float weighted_occlusion = 0.0;
    float weight_sum = 0.0;
    float receiver_depth = shadow.z -
        max(oot3d_directional_shadow.shadow_state.w, 0.0);
    // Combine the bilinear footprints of the (2r+1)^2 box taps. Shared
    // texels collapse to one weighted comparison, so radius 1 needs 16
    // fetches instead of 36 while coverage remains continuous in UV.
    for (int y = -2; y <= 3; ++y) {
        if (y < -radius || y > radius + 1) {
            continue;
        }
        float weight_y = oot3d_directional_shadow_axis_weight(
            y, radius, texel_fraction.y);
        for (int x = -2; x <= 3; ++x) {
            if (x < -radius || x > radius + 1) {
                continue;
            }
            float weight_x = oot3d_directional_shadow_axis_weight(
                x, radius, texel_fraction.x);
            float weight = weight_x * weight_y;
            weighted_occlusion += weight *
                oot3d_directional_shadow_texel(
                    texel_base + ivec2(x, y), map_extent,
                    receiver_depth);
            weight_sum += weight;
        }
    }
    float coverage = weight_sum > 0.0
        ? weighted_occlusion / weight_sum : 0.0;
    float strength = clamp(
        oot3d_directional_shadow.shadow_state.x, 0.0, 1.0);
    vec3 direct_fraction = clamp(
        oot3d_directional_shadow.shadowed_direct_fraction.rgb,
        vec3(0.0), vec3(1.0));
    return vec3(1.0) - coverage * strength * direct_fraction;
}
)glsl";
}

} // namespace

PicaDirectionalShadowLightingInstrumentation
BuildPicaDirectionalShadowLightingInstrumentation(
    uint64_t originalFragmentKey, bool enabled,
    const ::Oot3d::Renderer::PicaShaderHookLayout& hooks) {
    using ::Oot3d::Renderer::PicaShaderHook;
    using ::Oot3d::Renderer::PicaShaderSemantic;

    PicaDirectionalShadowLightingInstrumentation result;
    result.FragmentKey = originalFragmentKey;
    if (!enabled) {
        return result;
    }
    if (!hooks.Valid() || !hooks.Supports(PicaShaderHook::PicaLighting)) {
        result.Eligibility =
            PicaDirectionalShadowLightingEligibility::UnsupportedShader;
        return result;
    }

    const bool fragmentPrimary =
        hooks.Has(PicaShaderSemantic::MaterialLightingPoint) &&
        hooks.Has(PicaShaderSemantic::SecondaryFragmentColor);
    const bool vertexPrimary =
        hooks.Has(PicaShaderSemantic::VertexLightingPoint) &&
        hooks.Has(PicaShaderSemantic::PrimaryColorInput) &&
        hooks.Has(PicaShaderSemantic::PrimaryColorConsumed);
    if (!fragmentPrimary && !vertexPrimary) {
        result.Eligibility =
            PicaDirectionalShadowLightingEligibility::UnsupportedShader;
        return result;
    }

    result.Declarations = LightingDeclarations();
    result.Body = fragmentPrimary
        ? "\n    primary_fragment_color.rgb *= "
          "oot3d_directional_shadow_visibility(); // OOT3D shadow lighting\n"
        : "\n    rounded_primary_color.rgb *= "
          "oot3d_directional_shadow_visibility(); // OOT3D shadow lighting\n";
    result.FragmentKey = LightingVariantKey(
        originalFragmentKey, fragmentPrimary);
    result.Eligibility = fragmentPrimary
        ? PicaDirectionalShadowLightingEligibility::FragmentPrimary
        : PicaDirectionalShadowLightingEligibility::VertexPrimary;
    return result;
}

PicaDirectionalShadowReceiverBinding
BuildPicaDirectionalShadowReceiverBinding(
    const DirectionalShadowMatrix& currentViewToWorld,
    const PicaNativeLightingState& nativeLighting,
    bool skinnedGeometry,
    const PicaDirectionalShadowHistoryState& history) noexcept {
    PicaDirectionalShadowReceiverBinding result{};
    if (!history.Valid()) {
        return result;
    }
    const auto receiverLight = ResolvePicaDirectionalShadowReceiverLight(
        nativeLighting, currentViewToWorld,
        history.WorldLightDirectionTowardSource);
    result.Classification = receiverLight.Classification;
    if (skinnedGeometry ||
        (receiverLight.Classification !=
             PicaDirectionalShadowReceiverClass::AmbientOnly &&
         receiverLight.Classification !=
             PicaDirectionalShadowReceiverClass::LightingDisabled)) {
        return result;
    }
    result.Contribution =
        PicaDirectionalShadowReceiverContribution::BakedRigidMaterial;
    auto& uniforms = result.Uniforms;
    uniforms.ViewToShadowTexture = MultiplyDirectionalShadowMatrices(
        history.WorldToShadowTexture, currentViewToWorld);
    uniforms.ShadowState = {
        std::clamp(history.Strength, 0.0F, 1.0F),
        1.0F / static_cast<float>(history.Resolution),
        0.0F,
        std::max(history.DepthBias, 0.0F),
    };
    // Baked/ambient-only rigid materials carry no separable native direct
    // term. Their whole pre-TEV primary contribution is the receiver term
    // for the optional enhanced shadow map.
    uniforms.ShadowedDirectFraction = {1.0F, 1.0F, 1.0F, 0.0F};
    uniforms.Extent = {
        history.Resolution,
        history.Resolution,
        std::min<uint32_t>(history.PcfRadius, 2U),
        1U,
    };
    return result;
}

} // namespace Fast::Oot3d
