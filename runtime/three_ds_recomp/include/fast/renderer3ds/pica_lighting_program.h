#pragma once
#include "fast/oot3d/pica_fragment_lighting.h"
#include <array>
#include <cstdint>
#include <string_view>

namespace Fast::Renderer3ds {
// std140 payload. LUT support is resolved by the canonical register decoder,
// never by material names. Lights remain in native accumulation order.
struct alignas(16) PicaLightingProgram {
    std::array<uint32_t, 4> Control{}; // count, environment, fresnel, flags
    std::array<std::array<uint32_t, 4>, 8> Lights{}; // index, flags, reserved
    std::array<std::array<float, 4>, 7> Luts{}; // input, absolute, scale, enabled
    bool operator==(const PicaLightingProgram&) const = default;
};
static_assert(sizeof(PicaLightingProgram) == 256);

inline PicaLightingProgram BuildPicaLightingProgram(const Oot3d::PicaFragmentLightingState& state) {
    PicaLightingProgram p;
    p.Control = {state.ActiveLightCount, state.EnvironmentConfiguration, state.FresnelSelector,
        uint32_t(state.ClampHighlights) | (uint32_t(state.ShadowPrimary) << 1) |
        (uint32_t(state.ShadowSecondary) << 2) | (uint32_t(state.ShadowAlpha) << 3) |
        (uint32_t(state.BumpMode) << 4) | (uint32_t(state.RecalculateBumpVectors) << 6) |
        (uint32_t(state.ShadowFactorEnabled) << 7) | (uint32_t(state.InvertShadow) << 8)};
    for (size_t slot = 0; slot < p.Lights.size(); ++slot) {
        const auto index = state.LightPermutation[slot];
        const auto& light = state.Lights[index];
        const bool spot = light.SpotAttenuationEnabled &&
            Oot3d::IsPicaLightingLutSamplerSupported(state.EnvironmentConfiguration, uint8_t(8 + index));
        p.Lights[slot] = {index, uint32_t(light.Directional) | (uint32_t(light.TwoSidedDiffuse) << 1) |
            (uint32_t(light.GeometricFactor0) << 2) | (uint32_t(light.GeometricFactor1) << 3) |
            (uint32_t(light.ShadowEnabled) << 4) | (uint32_t(spot) << 5) |
            (uint32_t(light.DistanceAttenuationEnabled) << 6), 0, 0};
    }
    constexpr uint8_t tables[]{0, 1, 8, 3, 4, 5, 6};
    constexpr uint8_t disableBits[]{16, 17, 0, 19, 22, 21, 20};
    for (size_t i = 0; i < p.Luts.size(); ++i) {
        const auto& lut = state.LutSamplers[i];
        const bool enabled = i == 2 || ((state.Config1 & (1U << disableBits[i])) == 0 &&
            Oot3d::IsPicaLightingLutSamplerSupported(state.EnvironmentConfiguration, tables[i]));
        p.Luts[i] = {float(lut.Input), float(lut.AbsoluteInput), lut.Scale, float(enabled)};
    }
    return p;
}

inline constexpr std::string_view PicaLightingProgramDeclaration = R"glsl(
struct PicaLightingProgram {
    uvec4 control;
    uvec4 lights[8];
    vec4 luts[7];
};
)glsl";

// Requires the canonical signed/unsigned LUT readers and fragment UBO.
inline constexpr std::string_view PicaLightingProgramFunctions = R"glsl(
void pica_program_surface(PicaLightingProgram p, vec4 bump_sample, vec4 shadow_sample,
                         out vec3 n, out vec3 t, out vec4 shadow) {
    n = vec3(0.0, 0.0, 1.0);
    t = vec3(1.0, 0.0, 0.0);
    uint mode = (p.control.w >> 4u) & 3u;
    if (mode == 1u) {
        n = 2.0 * bump_sample.rgb - vec3(1.0);
        if ((p.control.w & 64u) != 0u) n.z = sqrt(max(1.0 - dot(n.xy, n.xy), 0.0));
    } else if (mode == 2u) {
        t = 2.0 * bump_sample.rgb - vec3(1.0);
    }
    shadow = vec4(1.0);
    if ((p.control.w & 128u) != 0u)
        shadow = (p.control.w & 256u) != 0u ? vec4(1.0) - shadow_sample : shadow_sample;
}
float pica_program_lut(PicaLightingProgram p, int sampler_index, int table_index,
    bool two_sided, vec3 n, vec3 t, vec3 v, vec3 l, vec3 h, vec3 spot) {
    vec4 config = p.luts[sampler_index];
    float x = 0.0;
    switch (int(config.x)) {
    case 0: x = dot(n, normalize(h)); break;
    case 1: x = dot(v, normalize(h)); break;
    case 2: x = dot(n, v); break;
    case 3: x = dot(l, n); break;
    case 4: x = dot(l, spot); break;
    case 5:
        if (p.control.y == 8u) x = dot(normalize(h) - n * dot(n, normalize(h)), t);
        break;
    }
    if (config.y != 0.0) {
        x = two_sided ? abs(x) : max(x, 0.0);
        return config.z * pica_lighting_lut_unsigned(table_index, x);
    }
    return config.z * pica_lighting_lut_signed(table_index, x);
}
)glsl";

inline constexpr std::string_view PicaLightingProgramBody = R"glsl(
    PicaLightingProgram lp = fragment_uniforms.lighting_program;
    for (uint slot = 0u; slot < lp.control.x; ++slot) {
        uint i = lp.lights[slot].x;
        uint flags = lp.lights[slot].y;
        bool two_sided = (flags & 2u) != 0u;
        vec3 l = fragment_uniforms.lighting_position[i].xyz;
        if ((flags & 1u) == 0u) l += pica_view;
        float distance_to_light = length(l);
        l = normalize(l);
        vec3 spot = fragment_uniforms.lighting_spot_direction[i].xyz;
        vec3 h = pica_normalized_view + l;
        float ndotl = two_sided ? abs(dot(l, pica_lighting_normal)) : max(dot(l, pica_lighting_normal), 0.0);
        float highlight = (lp.control.w & 1u) != 0u ? sign(ndotl) : 1.0;
        float geo = 0.0;
        if ((flags & 12u) != 0u) {
            float hlength = dot(h, h);
            geo = hlength == 0.0 ? 0.0 : min(ndotl / hlength, 1.0);
        }
        float spot_attenuation = 1.0;
        if ((flags & 32u) != 0u)
            spot_attenuation = pica_program_lut(lp, 2, int(8u+i), two_sided,
                pica_lighting_normal, pica_lighting_tangent, pica_normalized_view, l, h, spot);
        float distance_attenuation = 1.0;
        if ((flags & 64u) != 0u)
            distance_attenuation = pica_lighting_lut_unsigned(int(16u+i),
                clamp(fragment_uniforms.lighting_attenuation[i].y * distance_to_light +
                      fragment_uniforms.lighting_attenuation[i].x, 0.0, 1.0));
        float attenuation = spot_attenuation * distance_attenuation;
        float d0 = 1.0;
        if (lp.luts[0].w != 0.0) d0 = pica_program_lut(lp, 0, 0, two_sided,
            pica_lighting_normal, pica_lighting_tangent, pica_normalized_view, l, h, spot);
        vec3 s0 = fragment_uniforms.lighting_specular0[i].rgb * d0;
        if ((flags & 4u) != 0u) s0 *= geo;
        float red = 1.0;
        if (lp.luts[6].w != 0.0) red = pica_program_lut(lp, 6, 6, two_sided,
            pica_lighting_normal, pica_lighting_tangent, pica_normalized_view, l, h, spot);
        float green = red;
        if (lp.luts[5].w != 0.0) green = pica_program_lut(lp, 5, 5, two_sided,
            pica_lighting_normal, pica_lighting_tangent, pica_normalized_view, l, h, spot);
        float blue = red;
        if (lp.luts[4].w != 0.0) blue = pica_program_lut(lp, 4, 4, two_sided,
            pica_lighting_normal, pica_lighting_tangent, pica_normalized_view, l, h, spot);
        float d1 = 1.0;
        if (lp.luts[1].w != 0.0) d1 = pica_program_lut(lp, 1, 1, two_sided,
            pica_lighting_normal, pica_lighting_tangent, pica_normalized_view, l, h, spot);
        vec3 s1 = fragment_uniforms.lighting_specular1[i].rgb * vec3(red, green, blue) * d1;
        if ((flags & 8u) != 0u) s1 *= geo;
        if (slot + 1u == lp.control.x && lp.luts[3].w != 0.0) {
            float f = pica_program_lut(lp, 3, 3, two_sided,
                pica_lighting_normal, pica_lighting_tangent, pica_normalized_view, l, h, spot);
            if ((lp.control.z & 1u) != 0u) pica_diffuse_sum.a = f;
            if ((lp.control.z & 2u) != 0u) pica_specular_sum.a = f;
        }
        vec3 diffuse = fragment_uniforms.lighting_diffuse[i].rgb * ndotl;
        if ((lp.control.w & 2u) != 0u && (flags & 16u) != 0u) diffuse *= pica_shadow_factor.rgb;
        pica_diffuse_sum.rgb += diffuse * attenuation + fragment_uniforms.lighting_ambient[i].rgb * attenuation;
        vec3 specular = (s0 + s1) * highlight * attenuation;
        if ((lp.control.w & 4u) != 0u && (flags & 16u) != 0u) specular *= pica_shadow_factor.rgb;
        pica_specular_sum.rgb += specular;
    }
    if ((lp.control.w & 8u) != 0u) {
        if ((lp.control.z & 1u) != 0u) pica_diffuse_sum.a *= pica_shadow_factor.a;
        if ((lp.control.z & 2u) != 0u) pica_specular_sum.a *= pica_shadow_factor.a;
    }
)glsl";
} // namespace Fast::Renderer3ds
