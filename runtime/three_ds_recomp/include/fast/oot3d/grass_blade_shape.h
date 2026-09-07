#pragma once

#include <cmath>
#include <string_view>

namespace Fast::Oot3d {

// Bound the entire animated blade about its root, independently of LOD.
inline float GrassBladeRadiusScale(float curvature, float variation,
                                   float maximumBend) {
    const float lateral = curvature * (1.0F + variation) +
                          0.35F * variation + maximumBend;
    return std::sqrt(1.0F + lateral * lateral);
}

inline constexpr std::string_view kGrassBladeShapeShader = R"glsl(
vec3 grass_blade_shape(float t, float phase, vec2 root_axis,
                      vec4 shape, out float twist) {
    // Stable per-blade variation, never camera- or time-dependent.
    float random = fract(sin(phase * 12.9898 + 4.1414) * 43758.5453);
    float signed_random = random * 2.0 - 1.0;
    float angle = signed_random * shape.z * 3.14159265;
    vec2 direction = mat2(cos(angle), sin(angle),
                         -sin(angle), cos(angle)) * root_axis;
    vec2 side = vec2(-direction.y, direction.x);
    float curve = shape.x * (1.0 + signed_random * shape.z);
    vec2 offset = direction * curve * t * t;
    offset += side * (0.35 * shape.z * sin(3.14159265 * t + phase) * t * t);
    twist = shape.w * signed_random * t * t;
    return vec3(offset.x, t - shape.y * t * t * t, offset.y);
}
)glsl";

} // namespace Fast::Oot3d
