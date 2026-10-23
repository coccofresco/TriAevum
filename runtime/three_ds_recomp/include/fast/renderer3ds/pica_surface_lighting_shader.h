#pragma once
#include "fast/renderer3ds/pica_shader_hooks.h"
#include "fast/renderer3ds/pica_surface_lighting.h"
#include "fast/renderer3ds/pica_vertex_surface.h"
#include <string>

namespace Fast::Renderer3ds {
inline constexpr std::string_view kPicaSurfaceLightingRecomposeShader = R"glsl(
vec3 pica_surface_recompose(vec4 appearance, vec3 raw, vec3 directional, float visibility) {
    // Baked colour times a dimensionless lighting ratio. Keep the selected
    // shadow term before clamping, exactly as the terrain evaluator does.
    if (appearance.a > 4.5) return clamp(raw - directional * (1.0 - visibility), 0.0, 1.0);
    if (appearance.a > 3.5) return appearance.rgb;
    vec3 response = raw - directional * (1.0 - visibility);
    if (appearance.a < 2.5) response = min(abs(response), vec3(1.0));
    return appearance.a < 1.5 ? response : min(appearance.rgb + response, vec3(1.0));
}
)glsl";

inline std::string BuildPicaSurfaceLightingVertexShader(std::string_view source,
                                                        const PicaVertexShaderHookLayout& hooks, bool surfaceLighting) {
    if (!hooks.ValidFor(source) || !hooks.Supports(PicaVertexShaderHook::MainBodyEnd) ||
        !hooks.Supports(PicaVertexShaderHook::RegisterStateEnd))
        return {};
    const auto declarations = hooks.Offset(PicaVertexShaderHook::RegisterStateEnd);
    const auto end = hooks.Offset(PicaVertexShaderHook::MainBodyEnd);
    if (declarations > end)
        return {};
    const auto surface = surfaceLighting ? BuildPicaVertexSurfaceShader(hooks) : PicaVertexSurfaceShader{};
    if (surfaceLighting && !surface.Available)
        return {};
    std::string result(source.substr(0, declarations));
    result += "\nlayout(push_constant) uniform SurfaceAtlas { uvec4 allocation; } surface_atlas;\n";
    result += surface.Declarations;
    result += source.substr(declarations, end - declarations);
    result += surface.MainBody;
    result += R"glsl(
    uint atlas_index = surface_atlas.allocation.x + uint(gl_VertexIndex);
    vec2 cell = vec2(atlas_index % surface_atlas.allocation.y, atlas_index / surface_atlas.allocation.y);
    gl_Position = vec4((cell + 0.5) / vec2(surface_atlas.allocation.yz) * 2.0 - 1.0, 0.0, 1.0);
    gl_PointSize = 1.0;
)glsl";
    result += source.substr(end);
    return result;
}
inline constexpr const char* kPicaSurfaceLightingFragmentShader = R"glsl(#version 450
layout(location=0) in vec4 pica_primary_color;
layout(location=0) out vec4 surface_primary;
void main() { surface_primary = vec4(pica_primary_color.rgb, 1.0); }
)glsl";
inline std::string BuildPicaSurfaceLightingCoefficientsShader(bool surfaceLighting) {
    std::string result = R"glsl(#version 450
layout(location=0) in vec4 pica_primary_color;
layout(location=0) out vec4 surface_primary;
layout(location=1) out vec4 surface_raw;
layout(location=2) out vec4 surface_directional;
)glsl";
    if (!surfaceLighting) {
        result += R"glsl(
void main() {
    surface_primary = vec4(pica_primary_color.rgb, 4.0);
    surface_raw = surface_directional = vec4(0.0);
}
)glsl";
        return result;
    }
    result += R"glsl(
layout(location=6) in vec3 pica_view;
layout(push_constant) uniform SurfaceAtlas {
    uvec4 allocation;
    vec4 shadow_direction;
} surface_atlas;
)glsl";
    result += PicaSurfaceLightingDeclarations();
    result += R"glsl(
vec3 pica_surface_shadow(int slot) { return vec3(1.0); }
float pica_point_shadow(float slot, vec3 light_to_fragment, float radius) { return 1.0; }
bool pica_surface_matches_shadow(vec3 direction) {
    vec3 world = vec3(dot(pica_surface.view_to_world_linear[0].xyz, direction),
                      dot(pica_surface.view_to_world_linear[1].xyz, direction),
                      dot(pica_surface.view_to_world_linear[2].xyz, direction));
    float squared = dot(world, world);
    return squared > 1.0e-6 &&
        dot(world * inversesqrt(squared), surface_atlas.shadow_direction.xyz) >= 0.999;
}
uint pica_surface_shadow_mask() {
    uint mask = 0u;
    for (uint slot = 0u; slot < 3u; ++slot) {
        PicaSurfaceLight light = pica_surface.lights[slot];
        if (light.direction_enabled.w != 0.0 && light.position_radius.w <= 0.0 &&
            pica_surface_matches_shadow(light.direction_enabled.xyz)) mask |= 1u << slot;
    }
    // The authored key casts; the fill and all point lights remain unaffected.
    if (pica_surface.environment_parameters.w != 0.0 &&
        pica_surface.environment_directions[0].w > 0.0 &&
        pica_surface_matches_shadow(pica_surface.environment_directions[0].xyz)) mask |= 1u << 8u;
    for (uint slot = 0u; slot < pica_surface.control.z; ++slot)
        if (pica_surface_matches_shadow(pica_surface.scene_directions[slot].xyz)) mask |= 1u << (10u + slot);
    return mask;
}
void main() {
    vec3 normal = pica_surface_unit(pica_surface_normal);
    vec3 raw = vec3(0.0), directional = vec3(0.0);
    vec3 appearance = pica_primary_color.rgb;
    float mode = 4.0;
    uint mask = pica_surface_shadow_mask();
    if (pica_surface.baked_reference_up.w != 0.0) {
        vec3 reference;
        raw = pica_surface_baked_response(normal, mask, reference, directional);
        vec3 anchor = appearance / max(reference, vec3(1.0e-6));
        raw *= anchor;
        directional *= anchor;
        raw += (pica_surface_scene_point_primary(normal, true) +
            pica_surface_rim_independent(normal, pica_surface_unit(pica_view))) * pica_surface_tint.rgb;
        surface_primary = vec4(appearance, 5.0);
        surface_raw = vec4(raw, 0.0);
        surface_directional = vec4(directional, 0.0);
        return;
    }
    if (pica_surface.control.x != 0u ||
        (pica_surface.environment_parameters.w != 0.0 && (pica_surface.control.w & 1u) == 0u)) {
        raw = pica_surface_native_response(normal, mask, directional);
        appearance = vec3(0.0);
        mode = 1.0;
    } else if (pica_surface.environment_parameters.w != 0.0) {
        raw = pica_surface_native_response(normal, mask, directional);
        mode = 2.0;
    } else if (pica_surface.control.y != 0u || pica_surface.control.z != 0u) {
        raw = pica_surface_scene_point_primary(normal, true) +
            pica_surface_scene_directional_response(normal, mask, directional, true);
        mode = 3.0;
    }
    surface_primary = vec4(appearance, mode);
    surface_raw = vec4(raw * pica_surface_tint.rgb, 0.0);
    surface_directional = vec4(directional * pica_surface_tint.rgb, 0.0);
}
)glsl";
    return result;
}

inline std::string BuildPicaSurfaceLightingFragmentShader(bool surfaceLighting, bool shadowCoefficients = false) {
    if (shadowCoefficients) return BuildPicaSurfaceLightingCoefficientsShader(surfaceLighting);
    if (!surfaceLighting)
        return kPicaSurfaceLightingFragmentShader;
    std::string result = "#version 450\n"
                         "layout(location=0) in vec4 pica_primary_color;\n"
                         "layout(location=6) in vec3 pica_view;\n"
                         "layout(location=0) out vec4 surface_primary;\n";
    result += PicaSurfaceLightingDeclarations();
    result += R"glsl(
vec3 pica_surface_shadow(int slot) { return vec3(1.0); }
float pica_point_shadow(float slot, vec3 light_to_fragment, float radius) { return 1.0; }
void main() {
    vec3 color = pica_primary_color.rgb;
    if (pica_surface.control.x != 0u || pica_surface.environment_parameters.w != 0.0 ||
        pica_surface.control.y != 0u || pica_surface.control.z != 0u)
        color = pica_surface_primary(color, pica_surface_unit(pica_surface_normal));
    surface_primary = vec4(color, 1.0);
}
)glsl";
    return result;
}
} // namespace Fast::Renderer3ds
