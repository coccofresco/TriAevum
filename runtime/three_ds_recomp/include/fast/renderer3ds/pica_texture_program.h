#pragma once
#include <string_view>

namespace Fast::Renderer3ds {
// Float and integer image interfaces cannot be switched through a uniform.
// All other texture selection remains native per-draw data.
inline constexpr std::string_view PicaFloatTexture0Program = R"glsl(
vec4 pica_native_texture0() {
    uint type = (fragment_uniforms.fragment_control.w >> 28u) & 7u;
    if (type == 5u) return vec4(0.0);
    if (type == 3u) return textureProj(pica_texture0,
        vec3(pica_texcoord0.x, pica_texcoord0_w - pica_texcoord0.y, pica_texcoord0_w));
    return pica_sample_texture0(vec2(pica_texcoord0.x, 1.0 - pica_texcoord0.y));
}
)glsl";
inline constexpr std::string_view PicaIntegerTexture0Program = R"glsl(
vec4 pica_native_texture0() {
    return pica_sample_shadow2d(pica_texcoord0, pica_texcoord0_w);
}
)glsl";
inline constexpr std::string_view PicaTextureSelectionProgram = R"glsl(
vec4 pica_native_texture(uint unit) {
    if (unit == 3u) return pica_native_texture3();
    if ((fragment_uniforms.fragment_control.z & (1u << unit)) == 0u) return vec4(0.0);
    if (unit == 0u) return pica_native_texture0();
    if (unit == 1u) return pica_sample_texture1(vec2(pica_texcoord1.x, 1.0 - pica_texcoord1.y));
    vec2 uv = (fragment_uniforms.fragment_control.z & 8192u) != 0u ? pica_texcoord1 : pica_texcoord2;
    return pica_sample_texture2(vec2(uv.x, 1.0 - uv.y));
}
)glsl";
} // namespace Fast::Renderer3ds
