#pragma once
#include "fast/renderer3ds/pica_shader_hooks.h"
#include <string>

namespace Fast::Renderer3ds {
inline std::string BuildPicaSurfaceLightingVertexShader(std::string_view source,
    const PicaVertexShaderHookLayout& hooks) {
    if (!hooks.ValidFor(source) || !hooks.Supports(PicaVertexShaderHook::MainBodyEnd) ||
        !hooks.Supports(PicaVertexShaderHook::RegisterStateEnd)) return {};
    const auto declarations = hooks.Offset(PicaVertexShaderHook::RegisterStateEnd);
    const auto end = hooks.Offset(PicaVertexShaderHook::MainBodyEnd);
    if (declarations > end) return {};
    std::string result(source.substr(0, declarations));
    result += "\nlayout(push_constant) uniform SurfaceAtlas { uvec4 allocation; } surface_atlas;\n";
    result += source.substr(declarations, end - declarations);
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
} // namespace Fast::Renderer3ds
