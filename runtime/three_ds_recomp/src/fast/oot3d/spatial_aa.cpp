#include "fast/oot3d/spatial_aa.h"

namespace Fast::Oot3d {

std::string BuildSpatialAaShaderLibrary() {
    return R"glsl(
float oot3d_aa_luma(vec3 color) {
    vec3 luma_color = scanout.input_linear != 0u
        ? oot3d_linear_to_srgb(color)
        : color;
    return dot(luma_color, vec3(0.299, 0.587, 0.114));
}

vec4 oot3d_fxaa(vec2 uv, vec4 center) {
    vec2 texel = scanout.inverse_size;
    vec3 northwest = oot3d_compose_at(uv + texel * vec2(-1.0, -1.0)).rgb;
    vec3 northeast = oot3d_compose_at(uv + texel * vec2( 1.0, -1.0)).rgb;
    vec3 southwest = oot3d_compose_at(uv + texel * vec2(-1.0,  1.0)).rgb;
    vec3 southeast = oot3d_compose_at(uv + texel * vec2( 1.0,  1.0)).rgb;

    float luma_center = oot3d_aa_luma(center.rgb);
    float luma_northwest = oot3d_aa_luma(northwest);
    float luma_northeast = oot3d_aa_luma(northeast);
    float luma_southwest = oot3d_aa_luma(southwest);
    float luma_southeast = oot3d_aa_luma(southeast);
    float luma_min = min(luma_center,
        min(min(luma_northwest, luma_northeast),
            min(luma_southwest, luma_southeast)));
    float luma_max = max(luma_center,
        max(max(luma_northwest, luma_northeast),
            max(luma_southwest, luma_southeast)));
    if (luma_max - luma_min < max(0.0312, luma_max * 0.125))
        return center;

    vec2 direction;
    direction.x = -((luma_northwest + luma_northeast) -
                    (luma_southwest + luma_southeast));
    direction.y =  ((luma_northwest + luma_southwest) -
                    (luma_northeast + luma_southeast));
    float direction_reduce = max(
        (luma_northwest + luma_northeast + luma_southwest +
         luma_southeast) * (0.25 * 0.125),
        1.0 / 128.0);
    float inverse_minimum = 1.0 /
        (min(abs(direction.x), abs(direction.y)) + direction_reduce);
    direction = clamp(direction * inverse_minimum, vec2(-8.0), vec2(8.0)) *
                texel;

    vec3 inner = 0.5 * (
        oot3d_compose_at(uv + direction * (1.0 / 3.0 - 0.5)).rgb +
        oot3d_compose_at(uv + direction * (2.0 / 3.0 - 0.5)).rgb);
    vec3 outer = inner * 0.5 + 0.25 * (
        oot3d_compose_at(uv + direction * -0.5).rgb +
        oot3d_compose_at(uv + direction *  0.5).rgb);
    float outer_luma = oot3d_aa_luma(outer);
    vec3 resolved = outer_luma < luma_min || outer_luma > luma_max
        ? inner
        : outer;
    return vec4(resolved, center.a);
}
)glsl";
}

} // namespace Fast::Oot3d
