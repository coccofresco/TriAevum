#include "fast/oot3d/reflection_debug.h"

namespace Fast::Oot3d {

std::array<float, 3> ResolveReflectionDebugColor(
    uint32_t mode, const std::array<float, 4>& material,
    const std::array<float, 4>& reflection) noexcept {
    switch (mode) {
        case 1U:
            // R = calibrated reflectivity, G = material class/coverage.
            // Material alpha is reserved for the temporal reactive mask.
            return {material[0], material[2], 0.0F};
        case 2U:
            return {material[1], material[1], material[1]};
        case 3U:
            return {reflection[0], reflection[1], reflection[2]};
        case 4U:
            return {reflection[3], reflection[3], reflection[3]};
        default:
            return {};
    }
}

std::string ReflectionDebugShaderLibrary() {
    return R"glsl(
vec3 oot3d_reflection_debug_color(
    uint mode, vec4 material, vec4 reflection) {
    if (mode == 1u)
        return vec3(material.r, material.b, 0.0);
    if (mode == 2u)
        return vec3(material.g);
    if (mode == 3u)
        return reflection.rgb;
    if (mode == 4u)
        return vec3(reflection.a);
    return vec3(0.0);
}
)glsl";
}

} // namespace Fast::Oot3d
