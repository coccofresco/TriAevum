#pragma once

#include "fast/renderer3ds/virtual_shadow_map.h"
#include <string_view>

namespace Fast::Renderer3ds {

// Shared raster/compute receiver. The caller supplies input-space position and
// geometric derivatives; only the raster adapter is allowed to use dFdx/dFdy.
// The block holds two layouts: layout 0 is the newest; layout 1 is the older
// keyframe of a moving light, sampled only while the dissolve runs (flag 2).
// Both live in regions of one atlas; a receiver that defines
// PICA_VSM_LAYOUT_ATLASES reads layout 1 from its own atlas (binding 11).
inline std::string_view VirtualShadowReceiverShader() noexcept {
    static_assert(kVirtualShadowPageTexels == 128 && kVirtualShadowPageBorder == 3 &&
                  kVirtualShadowPhysicalPageTexels == 134 && kVirtualShadowGridPages == 8 &&
                  kVirtualShadowLevelCount == 10 && kVirtualShadowTableEntries == 640);
    return R"glsl(
layout(set=0,binding=10) uniform sampler2D pica_vsm_atlas;
#ifdef PICA_VSM_LAYOUT_ATLASES
layout(set=0,binding=11) uniform sampler2D pica_vsm_older_atlas;
vec4 pica_vsm_gather(int layout_index, vec2 uv) {
    return layout_index == 0 ? textureGather(pica_vsm_atlas, uv, 0) : textureGather(pica_vsm_older_atlas, uv, 0);
}
#else
vec4 pica_vsm_gather(int layout_index, vec2 uv) { return textureGather(pica_vsm_atlas, uv, 0); }
#endif
struct PicaVirtualShadowLayout {
    mat4 input_to_light;
    vec4 camera_light;
    vec4 levels[10];
    uvec4 page_table[160];
};
layout(set=0,binding=12,std140) uniform PicaVirtualShadowReceiver {
    vec4 shadow_state;
    // x: smooth-shaded receivers' normal bias (texels); y: 1 with contact
    // shadows; w: light mask.
    vec4 receiver;
    uvec4 extent;
    vec4 dissolve;
    mat4 contact_projection;
    PicaVirtualShadowLayout layouts[2];
} pica_vsm;
uint pica_vsm_page(int layout_index, int level, ivec2 cell) {
    ivec2 relative = cell - ivec2(pica_vsm.layouts[layout_index].levels[level].xy);
    if (any(lessThan(relative, ivec2(0))) || any(greaterThanEqual(relative, ivec2(8)))) return 0u;
    int index = level * 64 + relative.y * 8 + relative.x;
    return pica_vsm.layouts[layout_index].page_table[index >> 2][index & 3];
}
ivec2 pica_vsm_physical(int layout_index, uint entry, ivec2 local) {
    uint side = pica_vsm.extent.x / 134u;
    uint page = entry - 1u;
    // Positive-height Vulkan viewport: increasing light Y increases texel Y.
    return ivec2(page % side, page / side) * 134 + local + ivec2(3) +
        ivec2(int(pica_vsm.dissolve[2 + layout_index]), 0);
}
float pica_vsm_axis_weight(int offset, int radius, float fraction) {
    return offset == -radius ? 1.0 - fraction : (offset == radius + 1 ? fraction : 1.0);
}
vec2 pica_vsm_depth_gradient(vec3 dx, vec3 dy) {
    float determinant = dx.x * dy.y - dx.y * dy.x;
    return abs(determinant) > 1.0e-12
        ? vec2(dx.z * dy.y - dy.z * dx.y, dx.x * dy.z - dy.x * dx.z) / determinant
        : vec2(0.0);
}
float pica_vsm_filter(int layout_index, int level, vec3 light, vec2 depth_gradient) {
    float page_world = pica_vsm.layouts[layout_index].levels[level].z;
    ivec2 center = ivec2(floor(light.xy / page_world));
    uint entry = pica_vsm_page(layout_index, level, center);
    if (entry == 0u) return -1.0;
    if (entry == 0xffffffffu) return 1.0;
    vec2 position = light.xy * (128.0 / page_world) - vec2(0.5);
    ivec2 base = ivec2(floor(position));
    vec2 fraction = fract(position);
    int radius = int(min(pica_vsm.extent.z, 2u));
    float texel_world = page_world / 128.0;
    float inverse_depth = 1.0 / (2.0 * max(pica_vsm.shadow_state.z * 4.0, 65536.0));
    float depth = light.z - max(2.0 * 5.960464477539063e-8, 0.25 * texel_world * inverse_depth) -
        pica_vsm.shadow_state.w * exp2(float(level));
    vec2 sample_gradient = depth_gradient * texel_world;
    ivec2 local = base - center * 128;
    // The three-texel gutter holds the entire PCF footprint: one translation
    // per level and 1/4/9 gathers for radius 0/1/2, even at page boundaries.
    vec2 physical_base = vec2(pica_vsm_physical(layout_index, entry, local));
    vec2 inverse_atlas = vec2(pica_vsm.shadow_state.y, pica_vsm.dissolve.y);
    float visibility = 0.0;
    for (int iy = 0; iy < 3; ++iy) {
        if (iy > radius) break;
        int y = -radius + iy * 2;
        vec2 wy = vec2(pica_vsm_axis_weight(y, radius, fraction.y),
                       pica_vsm_axis_weight(y + 1, radius, fraction.y));
        for (int ix = 0; ix < 3; ++ix) {
            if (ix > radius) break;
            int x = -radius + ix * 2;
            vec2 wx = vec2(pica_vsm_axis_weight(x, radius, fraction.x),
                           pica_vsm_axis_weight(x + 1, radius, fraction.x));
            vec4 weights = vec4(wx.x*wy.y, wx.y*wy.y, wx.y*wy.x, wx.x*wy.x);
            vec2 uv = (physical_base + vec2(x + 1, y + 1)) * inverse_atlas;
            vec4 sample_depth = vec4(depth) +
                sample_gradient.x * (vec4(x, x + 1, x + 1, x) - fraction.x) +
                sample_gradient.y * (vec4(y + 1, y + 1, y, y) - fraction.y);
            visibility += dot(step(sample_depth, pica_vsm_gather(layout_index, uv)), weights);
        }
    }
    float width = float(2 * radius + 1);
    return visibility / (width * width);
}
// The finest level that holds a light-space position.
int pica_vsm_first_level(int layout_index, vec3 light) {
    vec3 relative = light - pica_vsm.layouts[layout_index].camera_light.xyz;
    float distance_plane = max(abs(relative.x), abs(relative.y));
    int count = int(pica_vsm.layouts[layout_index].camera_light.w);
    float finest_page = pica_vsm.layouts[layout_index].levels[0].z;
    return clamp(int(ceil(log2(max(distance_plane / (3.0 * finest_page), 1.0)))), 0, count - 1);
}
vec2 pica_vsm_occlusion(int layout_index, vec3 light, vec2 depth_gradient) {
    if ((pica_vsm.extent.w & 1u) == 0u || light.z < 0.0 || light.z > 1.0) return vec2(0.0);
    vec3 relative = light - pica_vsm.layouts[layout_index].camera_light.xyz;
    float maximum_distance = pica_vsm.shadow_state.z;
    float depth_extent = max(maximum_distance * 4.0, 65536.0);
    float world_distance = length(vec3(relative.xy, relative.z * (2.0 * depth_extent)));
    if (world_distance >= maximum_distance) return vec2(0.0);
    int count = int(pica_vsm.layouts[layout_index].camera_light.w);
    float distance_plane = max(abs(relative.x), abs(relative.y));
    int first = pica_vsm_first_level(layout_index, light);
    float visibility = 1.0;
    for (int level = first; level < count; ++level) {
        float sample_visibility = pica_vsm_filter(layout_index, level, light, depth_gradient);
        if (sample_visibility < 0.0) continue;
        visibility = sample_visibility;
        float page_world = pica_vsm.layouts[layout_index].levels[level].z;
        float blend = smoothstep(2.75 * page_world, 3.0 * page_world, distance_plane);
        if (blend > 0.0) {
            for (int coarse = level + 1; coarse < count; ++coarse) {
                float coarse_visibility = pica_vsm_filter(layout_index, coarse, light, depth_gradient);
                if (coarse_visibility < 0.0) continue;
                visibility = mix(visibility, coarse_visibility, blend);
                break;
            }
        }
        break;
    }
    float fade = 1.0 - smoothstep(0.9 * maximum_distance, maximum_distance, world_distance);
    return vec2(1.0 - visibility, fade);
}
// Visibility of an input-space position in one layout; dx and dy span the
// receiver's surface in input space for a receiver-plane depth slope (zero:
// none); normal is its vertex normal in input space (zero: no normal bias).
float pica_vsm_layout_visibility(int layout_index, vec3 position, vec3 dx, vec3 dy, vec3 normal) {
    vec4 light = pica_vsm.layouts[layout_index].input_to_light * vec4(position, 1.0);
    if (abs(light.w) <= 1.0e-7) return 1.0;
    // Differentiate before normalized-depth translation to retain D32 slopes.
    mat3 linear = mat3(pica_vsm.layouts[layout_index].input_to_light);
    // Godot's directional normal bias (scene_forward_clustered.glsl): along
    // the vertex normal, weighted by 1 - max(0, -N.L), its part along the
    // light removed; texels of the level the position reads.
    float normal_length = dot(normal, normal);
    if (pica_vsm.receiver.x > 0.0 && normal_length > 1.0e-12) {
        vec3 unit = normal * inversesqrt(normal_length);
        vec3 axis = vec3(linear[0].z, linear[1].z, linear[2].z);
        // Depth grows away from the light.
        vec3 toward = -axis * inversesqrt(max(dot(axis, axis), 1.0e-30));
        vec3 bias = unit * (1.0 - max(0.0, dot(toward, -unit)));
        bias -= toward * dot(toward, bias);
        int level = pica_vsm_first_level(layout_index, light.xyz / light.w);
        float texel_world = pica_vsm.layouts[layout_index].levels[level].z / 128.0;
        light += pica_vsm.layouts[layout_index].input_to_light *
                 vec4(bias * (pica_vsm.receiver.x * texel_world), 0.0);
    }
    vec2 sample_value = pica_vsm_occlusion(layout_index, light.xyz / light.w,
                                           pica_vsm_depth_gradient(linear * dx, linear * dy));
    return max(0.0, 1.0 - sample_value.x * clamp(pica_vsm.shadow_state.x, 0.0, 4.0) * sample_value.y);
}
float pica_vsm_visibility(vec3 position, vec3 dx, vec3 dy, vec3 normal) {
    float visibility = pica_vsm_layout_visibility(0, position, dx, dy, normal);
    if ((pica_vsm.extent.w & 2u) != 0u)
        visibility = mix(pica_vsm_layout_visibility(1, position, dx, dy, normal), visibility, pica_vsm.dissolve.x);
    return visibility;
}
)glsl";
}

} // namespace Fast::Renderer3ds
