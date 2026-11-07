#pragma once

#include "fast/renderer3ds/pica_shader_hooks.h"
#include "fast/renderer3ds/pica_fragment_balance.h"
#include "fast/renderer3ds/pica_surface_lighting.h"
#include "fast/renderer3ds/pica_surface_color_response.h"
#include <string>

namespace Fast::Renderer3ds {
// Grass over a native fragment-lit surface replays that surface's canonical
// lighting (including its LUTs), then a register-proven multiplicative TEV
// response. Keep HDR gain until grass albedo is applied: running unit textures
// through native TEV clamps would wrongly cap (light * tint * 4) at one.
// The previous vertex-lit atlas and its shader bytes are deliberately untouched.
inline std::string BuildPicaFragmentGrassLightingShader(
    std::string_view source, const PicaShaderHookLayout& hooks,
    bool surfaceLighting, bool coefficients,
    const PicaFragmentGrassColorResponse& response) {
    if (!response.Valid() || !hooks.ValidFor(source) || !hooks.Supports(PicaShaderHook::PicaLighting) ||
        !hooks.Supports(PicaShaderHook::AfterNativeTev) ||
        !hooks.Has(PicaShaderSemantic::MaterialLightingPoint) ||
        !hooks.Has(PicaShaderSemantic::ViewVector)) return {};
    const auto decl = hooks.Offset(PicaShaderHook::GlobalDeclarations);
    const auto lighting = hooks.Offset(PicaShaderHook::PicaLighting);
    const auto tevEnd = hooks.Offset(PicaShaderHook::AfterNativeTev);
    const auto& capture = hooks.NativeLightingCapture;
    if (decl > lighting || lighting > tevEnd || tevEnd > source.size() ||
        !capture.Valid(source.size(), lighting) || capture.Begin < decl) return {};
    std::string result(source.substr(0, decl));
    if (coefficients) result += R"glsl(
layout(location=1) out vec4 grass_raw;
layout(location=2) out vec4 grass_directional;
layout(push_constant) uniform SurfaceAtlas { uvec4 allocation; vec4 shadow_direction; } surface_atlas;
)glsl";
    if (surfaceLighting) {
        result += PicaSurfaceLightingDeclarations(true, true);
        result += R"glsl(
float grass_visibility = 1.0;
bool grass_matches_direction(vec3 direction) {
)glsl";
        if (coefficients) result += R"glsl(
    vec3 world = vec3(dot(pica_surface.view_to_world_linear[0].xyz, direction),
                      dot(pica_surface.view_to_world_linear[1].xyz, direction),
                      dot(pica_surface.view_to_world_linear[2].xyz, direction));
    return dot(world, world) > 1e-6 && dot(normalize(world), surface_atlas.shadow_direction.xyz) >= 0.999;
)glsl";
        else result += "    return false;\n";
        result += R"glsl(
}
vec3 pica_surface_shadow(int slot) {
)glsl";
        if (coefficients) result += R"glsl(
    if (slot >= 0 && slot < 8 && (surface_atlas.allocation.w & (1u << uint(slot))) != 0u)
        return vec3(grass_matches_direction(fragment_uniforms.lighting_position[slot].xyz) ? grass_visibility : 1.0);
    if (slot == 8) return vec3(grass_visibility);
)glsl";
        result += R"glsl(
    return vec3(1.0);
}
float pica_point_shadow(float slot, vec3 direction, float radius) { return 1.0; }
)glsl";
    }
    // Unit textures for both TEV modes. The parametric native-light body may
    // mention bump/shadow samplers even on a draw that disables them; atlas
    // admission rejects those material classes before using this variant.
    result += R"glsl(
#define pica_sample_texture0(coord) vec4(1.0)
#define pica_sample_texture1(coord) vec4(1.0)
#define pica_sample_texture2(coord) vec4(1.0)
#define pica_native_texture(unit, tc0, tc1, tc2) vec4(1.0)
)glsl";
    // Typed capture sites belong to the canonical emitter, not text searches.
    result += source.substr(decl, capture.Begin - decl);
    if (surfaceLighting) result += kPicaFragmentBalanceCaptureBegin;
    result += source.substr(capture.Begin, capture.Diffuse - capture.Begin);
    if (surfaceLighting) result += kPicaFragmentBalanceCaptureDiffuse;
    result += source.substr(capture.Diffuse, capture.Specular - capture.Diffuse);
    if (surfaceLighting) result += kPicaFragmentBalanceCaptureSpecular;
    result += source.substr(capture.Specular, lighting - capture.Specular);
    result += R"glsl(
    vec4 grass_native_fragment = primary_fragment_color;
    vec4 grass_native_secondary = secondary_fragment_color;
    vec4 grass_native_primary = rounded_primary_color;
    vec3 grass_samples[2];
)glsl";
    result += coefficients ? "    for (int grass_sample=0; grass_sample<2; ++grass_sample) {\n"
                           : "    for (int grass_sample=0; grass_sample<1; ++grass_sample) {\n";
    result += R"glsl(
        primary_fragment_color = grass_native_fragment;
        secondary_fragment_color = grass_native_secondary;
        rounded_primary_color = grass_native_primary;
        combiner_output = combiner_buffer = vec4(0.0);
        next_combiner_buffer = fragment_uniforms.combiner_buffer_color;
)glsl";
    if (surfaceLighting) {
        result += R"glsl(
        grass_visibility = grass_sample == 0 ? 1.0 : 0.0;
        // Native fragment emitter exports its actual view-space normal.
        pica_surface_normal = normal;
        if (pica_surface.balance_control.x != 0u) {
)glsl";
        result += PicaSurfaceLightingBody(true, true, false);
        result += "        } else {\n";
        result += PicaSurfaceLightingBody(true, false, true);
        result += "        }\n";
    }
    // This factor is illumination, not a white-albedo final framebuffer color.
    // The restricted material class has no texture-dependent clamp before tint.
    // Keep native channel rounding, but do not clip HDR gain before grass albedo.
    result += "        vec3 grass_response = primary_fragment_color.rgb;\n";
    for (uint32_t stage = 0; stage < 6; ++stage) {
        if (stage == response.TintStage) result += "        grass_response *= rounded_primary_color.rgb;\n";
        const auto scale = (response.PackedScales >> (2 * stage)) & 3U;
        if (scale > 2) return {};
        result += "        grass_response = byteround3(grass_response) * " + std::to_string(1U << scale) + ".0;\n";
    }
    result += R"glsl(
#undef pica_sample_texture0
#undef pica_sample_texture1
#undef pica_sample_texture2
#undef pica_native_texture
        grass_samples[grass_sample] = grass_response;
    }
)glsl";
    if (coefficients) result += R"glsl(
    pica_color = vec4(grass_samples[0], 5.0);
    grass_raw = vec4(grass_samples[0], 0.0);
    grass_directional = vec4(grass_samples[0] - grass_samples[1], 0.0);
}
)glsl";
    else result += "    pica_color = vec4(grass_samples[0], 1.0);\n}\n";
    return result;
}
} // namespace Fast::Renderer3ds
