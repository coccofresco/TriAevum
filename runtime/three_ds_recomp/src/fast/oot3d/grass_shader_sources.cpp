#include "fast/oot3d/grass_shader_sources.h"
#include "fast/oot3d/grass_blade_shape.h"
#include "fast/oot3d/grass_distant_tuft.h"
#include "fast/oot3d/grass_indexed_topology.h"
#include "fast/oot3d/grass_instance_layout.h"
#include "fast/oot3d/grass_world_placement_cache.h"
#include "fast/oot3d/toon_surface_response.h"
#include "fast/renderer3ds/pica_geometry_guide.h"
#include "fast/renderer3ds/pica_surface_lighting_shader.h"
#include "fast/renderer3ds/virtual_shadow_receiver_shader.h"

namespace Fast::Oot3d {
namespace {
constexpr char kGrassShadowRefinementShader[] = R"glsl(
struct GrassShadowVisibility { float visibility; uint patches; };
struct GrassShadowPatch {
    vec4 a; vec4 b; vec4 c; vec4 normal;
    vec4 footprint; vec4 grid; uvec4 address;
};
vec2 grass_shadow_plane(vec3 p, uint axis) {
    return axis == 0u ? p.yz : axis == 1u ? p.zx : p.xy;
}
)glsl";
} // namespace

std::string BuildGrassVertexShader() {
    return
            std::string("#version 450\n") + std::string(kToonSurfaceResponseShader) + std::string(kGrassBladeShapeShader) +
            std::string(kGrassDistantTuftShader) + std::string(kGrassIndexedVertexShader) +
            std::string(Renderer3ds::kPicaSurfaceLightingRecomposeShader) +
            std::string(kGrassShadowRefinementShader) + R"glsl(
layout(location=0) in vec4 attr_base_height;
layout(location=1) in vec4 attr_bend_half_width;
layout(location=2) in vec2 attr_width_axis;
layout(location=3) in vec4 attr_world_normal;
layout(location=4) in uint attr_surface_color;
layout(location=5) in uvec2 attr_surface_reference;
layout(std430,set=0,binding=2) readonly buffer GrassPreparedRoots { uint words[]; } prepared_roots;
layout(std430,set=0,binding=3) readonly buffer GrassPreparedGroups { uvec2 entries[]; } prepared_groups;
layout(set=0,binding=1) uniform sampler2DArray native_surface_lighting;
layout(std430,set=0,binding=4) readonly buffer GrassShadowVisibilityGrid {
    GrassShadowVisibility values[];
} shadow_visibility_grid;
struct GrassShadowReference { uint first; uint second; float weight; };
layout(std430,set=0,binding=5) readonly buffer GrassShadowReferences { GrassShadowReference values[]; } shadow_references;
layout(std430,set=0,binding=6) readonly buffer GrassVisibleRoots { uint values[]; } visible_roots;
layout(std430,set=0,binding=7) readonly buffer GrassShadowPatches { GrassShadowPatch values[]; } shadow_patches;

float grass_local_shadow(float coarse, vec3 root, vec3 root_normal, uint packed_patches) {
    if (packed_patches == 0u) return coarse;
    float result = coarse, strongest = 0.0;
    for (uint actor = 0u; actor < 2u; ++actor) {
        uint id = (packed_patches >> (actor * 16u)) & 65535u;
        if (id == 0u) continue;
        GrassShadowPatch tile = shadow_patches.values[id - 1u];
        // Remove the native root offset along its captured normal, without
        // flattening slopes or sampling a neighbouring overlapping surface.
        vec3 native_root = root;
        float denominator = dot(tile.normal.xyz, root_normal);
        if (abs(denominator) > 1.0e-6)
            native_root -= root_normal * (dot(tile.normal.xyz, root - tile.a.xyz) / denominator);
        vec2 position = grass_shadow_plane(native_root, floatBitsToUint(tile.normal.w));
        vec2 outside = max(tile.footprint.xy - position, position - tile.footprint.zw);
        float weight = 1.0 - smoothstep(0.0, tile.b.w, max(outside.x, outside.y));
        if (weight <= strongest) continue;
        vec2 coordinate = clamp((position - tile.grid.xy) * tile.grid.z,
                                vec2(0.0), vec2(tile.address.yz) - 1.0);
        uvec2 cell = min(uvec2(floor(coordinate)), tile.address.yz - 2u);
        vec2 fraction = coordinate - vec2(cell);
        uint first = tile.address.x + cell.y * tile.address.y + cell.x;
        float a = mix(shadow_visibility_grid.values[first].visibility,
                      shadow_visibility_grid.values[first + 1u].visibility, fraction.x);
        float b = mix(shadow_visibility_grid.values[first + tile.address.y].visibility,
                      shadow_visibility_grid.values[first + tile.address.y + 1u].visibility, fraction.x);
        result = mix(coarse, mix(a, b, fraction.y), weight);
        strongest = weight;
        // Overlapping actor patches share the same world lattice and final
        // atlas. Select coverage, never multiply shadow visibility twice.
        if (weight >= 1.0) break;
    }
    return result;
}

struct GrassEnvironmentRecord {
    vec4 color_and_mode;
    vec2 lut[128];
    vec4 wind_direction_time;
    vec4 wind_primary;
    vec4 wind_detail;
    vec4 camera_position;
    vec4 appearance_root;
    vec4 appearance_tip;
    vec4 texture_color;
    vec4 texture_brightness_flags;
    vec4 light_directions[3];
    vec4 light_diffuse[3];
    vec4 light_ambient[3];
    vec4 view_forward;
    vec4 view_side;
    vec4 view_up;
    vec4 blade_shape;
    vec4 tuft_lod;
    vec4 distance_lod;
    vec4 tuft_style;
    ToonSurfaceParameters toon;
    uvec4 native_lighting;
    vec4 rim_distance;
    mat4 previous_position_to_clip;
    vec4 previous_camera_time;
    vec4 motion_state;
};
layout(std430, set=0, binding=0) readonly buffer GrassEnvironmentState {
    GrassEnvironmentRecord records[];
} environment_state;
layout(std430,set=0,binding=8) readonly buffer GrassPreviousBends { vec2 values[]; } previous_bends;
layout(push_constant) uniform GrassState {
    mat4 position_to_clip;
    vec4 jitter_ndc;
    vec4 depth_state;
    uvec4 flags;
} grass;
layout(location=0) out vec4 blade_color;
layout(location=1) out vec4 blade_normal_guide;
layout(location=2) out vec4 blade_ambient_guide;
layout(location=3) out vec4 tuft_sample;
layout(location=4) flat out float lod_visibility;
layout(location=6) out vec3 blade_view_direction;
layout(location=7) flat out float blade_rim_weight;
layout(location=8) out vec4 blade_current_clip;
layout(location=9) out vec4 blade_previous_clip;
layout(location=10) out vec3 blade_lit_ceiling;

void evaluate_shading(
    uint environment_index, vec3 world_normal,
    out vec3 lighting, out vec3 ambient_response) {
    vec4 flags =
        environment_state.records[
            environment_index].texture_brightness_flags;
    uint light_count =
        uint(clamp(flags.w, 0.0, 3.0));
    if (flags.z <= 0.5 || light_count == 0u) {
        lighting = vec3(1.0);
        ambient_response = vec3(1.0);
        return;
    }
    vec3 ambient = vec3(0.0);
    vec3 direct = vec3(0.0);
    for (uint index = 0u; index < light_count; ++index) {
        vec4 direction =
            environment_state.records[
                environment_index].light_directions[index];
        if (direction.w <= 0.5)
            continue;
        float diffuse_factor = abs(clamp(
            dot(world_normal, direction.xyz), -1.0, 1.0));
        ambient +=
            environment_state.records[
                environment_index].light_ambient[index].rgb;
        direct +=
            environment_state.records[
                environment_index].light_diffuse[index].rgb *
            diffuse_factor;
    }
    vec3 total = ambient + direct;
    lighting = clamp(total, 0.0, 1.0);
    ambient_response = vec3(
        total.x > 1.0e-6
            ? clamp(ambient.x / total.x, 0.0, 1.0) : 1.0,
        total.y > 1.0e-6
            ? clamp(ambient.y / total.y, 0.0, 1.0) : 1.0,
        total.z > 1.0e-6
            ? clamp(ambient.z / total.z, 0.0, 1.0) : 1.0);
}

vec3 grass_native_material_rgb(vec3 base_color, vec3 lighting, uvec4 response) {
    if (response.w == 0u) return floor(clamp(base_color * lighting, 0.0, 1.0) * 255.0 + 0.5) / 255.0;
    vec3 color = base_color * (floor(lighting * 255.0 + 0.5) / 255.0);
    color = floor(clamp(color, 0.0, 1.0) * 255.0 + 0.5) / 255.0;
    return clamp(color * float(response.z), 0.0, 1.0);
}

vec4 grass_surface_sample(uint index, float visibility, bool coefficients, out vec3 original) {
    uint width = uint(textureSize(native_surface_lighting, 0).x);
    ivec3 cell = ivec3(index % width, index / width, 0);
    vec4 appearance = texelFetch(native_surface_lighting, cell, 0);
    if (!coefficients) { original = appearance.rgb; return appearance; }
    vec3 raw = texelFetch(native_surface_lighting, cell + ivec3(0,0,1), 0).rgb;
    vec3 directional = texelFetch(native_surface_lighting, cell + ivec3(0,0,2), 0).rgb;
    original = pica_surface_recompose(appearance, raw, directional, 1.0);
    return vec4(pica_surface_recompose(appearance, raw, directional, visibility), appearance.a);
}

vec3 evaluate_blade_color(
    uint environment_index, float height_factor,
    vec3 lighting, uint in_surface_color) {
    vec3 root =
        environment_state.records[
            environment_index].appearance_root.rgb;
    vec3 tip =
        environment_state.records[
            environment_index].appearance_tip.rgb;
    vec4 texture =
        environment_state.records[
            environment_index].texture_color;
    vec4 flags =
        environment_state.records[
            environment_index].texture_brightness_flags;
    vec3 surface_color = (in_surface_color >> 24u) != 0u
        ? unpackUnorm4x8(in_surface_color).rgb : texture.rgb;
    uvec4 response = environment_state.records[environment_index].native_lighting;
    vec3 root_color = grass_native_material_rgb(mix(root, surface_color * flags.x, texture.w), lighting, response);
    vec3 tip_color = grass_native_material_rgb(mix(tip, surface_color * flags.y, texture.w), lighting, response);
    return mix(root_color, tip_color, height_factor);
}

vec2 evaluate_wind(
    uint environment_index,
    vec3 world_position, float anchor_phase, float time) {
    vec4 wind_direction_time =
        environment_state.records[
            environment_index].wind_direction_time;
    wind_direction_time.z = time;
    vec4 wind_primary =
        environment_state.records[
            environment_index].wind_primary;
    vec4 wind_detail =
        environment_state.records[
            environment_index].wind_detail;
    if (wind_detail.w <= 0.5)
        return vec2(0.0);
    vec2 direction = wind_direction_time.xy;
    vec2 lateral =
        vec2(-direction.y, direction.x);
    float spatial =
        dot(world_position.xz, direction) * 0.01 *
        wind_primary.y;
    float random_phase =
        anchor_phase *
        clamp(wind_detail.y, 0.0, 1.0);
    float primary = sin(
        wind_direction_time.z * wind_primary.x +
        spatial + random_phase);
    float gust_phase =
        wind_direction_time.z *
            wind_primary.w * 6.28318530718 +
        spatial * 0.21 + random_phase * 0.37;
    float gust = 0.5 + 0.5 * sin(gust_phase);
    float turbulence = sin(
        wind_direction_time.z *
            (wind_primary.x * 1.71 + 0.31) -
        world_position.x * 0.017 +
        world_position.z * 0.013 +
        random_phase * 2.13);
    float directional_amount =
        wind_direction_time.w *
        (0.55 + primary * 0.30 +
         gust * wind_primary.z * 0.45);
    float lateral_amount =
        wind_direction_time.w *
        wind_detail.x * turbulence * 0.35;
    vec2 bend =
        direction * directional_amount +
        lateral * lateral_amount;
    float bend_length = length(bend);
    if (bend_length > wind_detail.z &&
        bend_length > 1.0e-6)
        bend *= wind_detail.z / bend_length;
    return bend;
}

// Far tufts spread their blades with the camera distance.
float grass_tuft_coverage(float tuft_weight, float lod_seed, vec4 lod, vec4 tuft_style) {
    float choice = grass_lod_choice(lod_seed);
    float growth = clamp((tuft_weight-choice)/max(1.0-choice,1.0e-6),0.0,1.0);
    return 1.0 + (lod.z-1.0) * growth * tuft_style.x;
}

// A blade point for one frame's camera (single-plane blades face it), wind
// time and interaction bend. Everything else about the blade is fixed.
vec3 grass_blade_position(
    uint environment_index, vec3 camera, float wind_time,
    vec2 dynamic_bend, float tuft_coverage, vec4 base_height,
    vec4 bend_half_width, vec2 root_axis, bool tuft, uint plane,
    float height_factor, float width_sign) {
    vec2 width_axis = root_axis;
    if (grass.flags.z == 1u) {
        vec2 camera_delta = base_height.xz - camera.xz;
        float camera_distance = length(camera_delta);
        width_axis = camera_distance > 1.0e-6
            ? vec2(
                  camera_delta.y / camera_distance,
                  -camera_delta.x / camera_distance)
            : vec2(1.0, 0.0);
    } else {
        width_axis = normalize(width_axis);
    }
    if (plane != 0u)
        width_axis = vec2(-width_axis.y, width_axis.x);
    float bend_factor =
        height_factor * (0.65 + 0.35 * height_factor);
    float width_factor =
        tuft ? tuft_coverage : height_factor >= 1.0 ? 0.0 :
        1.0 - 0.75 * height_factor;
    vec2 normalized_bend =
        evaluate_wind(
            environment_index, base_height.xyz,
            bend_half_width.w, wind_time) +
        dynamic_bend;
    float bend_length = length(normalized_bend);
    float maximum_bend =
        environment_state.records[
            environment_index].wind_detail.z;
    if (bend_length > maximum_bend &&
        bend_length > 1.0e-6)
        normalized_bend *=
            maximum_bend / bend_length;
    vec2 bend = normalized_bend * base_height.w;
    float twist;
    vec3 shape = grass_blade_shape(
        height_factor, bend_half_width.w, normalize(root_axis),
        environment_state.records[environment_index].blade_shape, twist);
    if (!tuft) width_axis = mat2(cos(twist), sin(twist), -sin(twist), cos(twist)) * width_axis;
    vec3 position = base_height.xyz;
    position += shape * base_height.w;
    position.xz += bend * bend_factor;
    position.xz += width_axis * bend_half_width.z *
                   width_factor * width_sign;
    return position;
}

vec4 grass_clip_position(mat4 position_to_clip, vec3 position, vec2 jitter_ndc) {
    vec4 clip = position_to_clip * vec4(position, 1.0);
    vec4 result;
    if (grass.flags.x != 0u) {
        // Match the generated PICA vertex shader exactly. Its projection is
        // already rotated for the physical CTR framebuffer.
        result = vec4(clip.x, clip.y, -clip.z, clip.w);
    } else {
        // Renderer-owned world projections are logical/unrotated and must be
        // transposed into the physical top framebuffer before scanout.
        result = vec4(clip.y, clip.x, clip.z, clip.w);
    }
    result.xy += jitter_ndc * result.w;
    return result;
}

void main() {
    bool grouped = (grass.flags.y & 0x80000000u) != 0u;
    uint segment_mode = grass.flags.y & 0x7fffffffu;
    bool tuft = segment_mode == 0u;
    uint segments = clamp(segment_mode, 1u, 12u);
    uint vertex = uint(gl_VertexIndex);
    uint root_index = uint(gl_InstanceIndex);
    vec4 in_base_height = attr_base_height;
    vec4 in_bend_half_width = attr_bend_half_width;
    vec2 in_width_axis = attr_width_axis;
    vec4 in_world_normal = attr_world_normal;
    uint in_surface_color = attr_surface_color;
    uvec2 in_surface_reference = attr_surface_reference;
    vec3 visibility_position = attr_base_height.xyz;
    float visibility_seed = attr_world_normal.w;
    if (grouped) {
        uint vertices_per_child = grass.flags.z * (2u * segments + 1u);
        uint child = vertex / vertices_per_child;
        vertex %= vertices_per_child;
        uvec2 group = prepared_groups.entries[gl_InstanceIndex];
        if (child >= group.y) { gl_Position = vec4(0.0,0.0,2.0,1.0); return; }
        root_index = group.x + child;
        uint representative = group.x * 17u;
        visibility_position = uintBitsToFloat(uvec3(prepared_roots.words[representative], prepared_roots.words[representative+1u], prepared_roots.words[representative+2u]));
        visibility_seed = uintBitsToFloat(prepared_roots.words[representative+13u]);
        uint base = (group.x + child) * 17u;
        in_base_height = uintBitsToFloat(uvec4(prepared_roots.words[base],prepared_roots.words[base+1u],prepared_roots.words[base+2u],prepared_roots.words[base+3u]));
        in_bend_half_width = uintBitsToFloat(uvec4(prepared_roots.words[base+4u],prepared_roots.words[base+5u],prepared_roots.words[base+6u],prepared_roots.words[base+7u]));
        in_width_axis = uintBitsToFloat(uvec2(prepared_roots.words[base+8u],prepared_roots.words[base+9u]));
        in_world_normal = uintBitsToFloat(uvec4(prepared_roots.words[base+10u],prepared_roots.words[base+11u],prepared_roots.words[base+12u],prepared_roots.words[base+13u]));
        in_surface_color = prepared_roots.words[base+14u];
        in_surface_reference = uvec2(prepared_roots.words[base+15u],prepared_roots.words[base+16u]);
    }
    uint plane;
    float height_factor;
    float width_sign;
    grass_indexed_vertex(vertex, segments, tuft, plane, height_factor, width_sign);
    vec4 lod = environment_state.records[grass.flags.w].tuft_lod;
    vec4 distance_lod = environment_state.records[grass.flags.w].distance_lod;
    vec4 tuft_style = environment_state.records[grass.flags.w].tuft_style;
    float distance = length(visibility_position - environment_state.records[grass.flags.w].camera_position.xyz);
    float normalized_distance = distance/max(lod.y,1.0e-6);
    float tuft_weight = grass_tuft_weight(normalized_distance,lod.x,lod.x+lod.w);
    float retention = grass_density_retention(normalized_distance,distance_lod.y,distance_lod.z);
    if (tuft_style.w > 0.5)
        retention *= grass_tuft_retention_scale(tuft_weight,lod.z,tuft_style.y);
    lod_visibility = grass_visibility_fade(clamp(retention,0.0,1.0),visibility_seed,distance_lod.w);
    lod_visibility *= 1.0-grass_tuft_weight(distance,distance_lod.x*(1.0-tuft_style.z),distance_lod.x);
    float tuft_coverage = tuft ? grass_tuft_coverage(tuft_weight, in_world_normal.w, lod, tuft_style) : 1.0;
    tuft_sample = vec4(width_sign, height_factor, tuft_coverage, in_bend_half_width.w);

    vec3 position = grass_blade_position(
        grass.flags.w, environment_state.records[grass.flags.w].camera_position.xyz,
        environment_state.records[grass.flags.w].wind_direction_time.z,
        in_bend_half_width.xy, tuft_coverage, in_base_height, in_bend_half_width,
        in_width_axis, tuft, plane, height_factor, width_sign);
    gl_Position = grass_clip_position(grass.position_to_clip, position, grass.jitter_ndc.xy);
    // Exact motion, like the native draws' rigid motion guide: the same blade
    // point with the previous frame's camera, jitter, wind time and bend.
    blade_current_clip = vec4(0.0);
    blade_previous_clip = vec4(0.0);
    vec4 motion_state = environment_state.records[grass.flags.w].motion_state;
    if (motion_state.z > 0.5) {
        vec4 previous_camera_time = environment_state.records[grass.flags.w].previous_camera_time;
        float previous_coverage = tuft ? grass_tuft_coverage(grass_tuft_weight(
            length(visibility_position - previous_camera_time.xyz)/max(lod.y,1.0e-6), lod.x, lod.x+lod.w),
            in_world_normal.w, lod, tuft_style) : 1.0;
        vec2 previous_bend = motion_state.w > 0.5 ? previous_bends.values[root_index] : in_bend_half_width.xy;
        vec3 previous_position = grass_blade_position(
            grass.flags.w, previous_camera_time.xyz, previous_camera_time.w,
            previous_bend, previous_coverage, in_base_height, in_bend_half_width,
            in_width_axis, tuft, plane, height_factor, width_sign);
        blade_current_clip = gl_Position;
        blade_previous_clip = grass_clip_position(
            environment_state.records[grass.flags.w].previous_position_to_clip,
            previous_position, motion_state.xy);
    }
    vec3 world_normal = normalize(in_world_normal.xyz);
    vec3 lighting;
    vec3 ambient_response;
    evaluate_shading(
        grass.flags.w, world_normal,
        lighting, ambient_response);
    uvec4 source_light = environment_state.records[grass.flags.w].native_lighting;
    if (source_light.y != 0u) {
        uvec3 vertices = uvec3(in_surface_reference.x & 65535u,
            in_surface_reference.x >> 16u, in_surface_reference.y & 65535u) + source_light.x;
        vec2 weights = vec2((in_surface_reference.y >> 16u) & 255u, in_surface_reference.y >> 24u) / 255.0;
        bool coefficients = (source_light.y & 2u) != 0u;
        float shadow_visibility = 1.0;
        if (coefficients) {
            GrassShadowReference reference = shadow_references.values[visible_roots.values[root_index]];
            GrassShadowVisibility first = shadow_visibility_grid.values[reference.first];
            shadow_visibility = mix(first.visibility,
                shadow_visibility_grid.values[reference.second].visibility, reference.weight);
            shadow_visibility = grass_local_shadow(
                shadow_visibility, in_base_height.xyz, world_normal, first.patches);
        }
        vec3 original_a, original_b, original_c;
        vec4 a = grass_surface_sample(vertices.x, shadow_visibility, coefficients, original_a);
        vec4 b = grass_surface_sample(vertices.y, shadow_visibility, coefficients, original_b);
        vec4 c = grass_surface_sample(vertices.z, shadow_visibility, coefficients, original_c);
        if (min(a.a, min(b.a, c.a)) > 0.5) {
            lighting = a.rgb * (1.0 - weights.x - weights.y) + b.rgb * weights.x + c.rgb * weights.y;
            if (coefficients) {
                vec3 original = original_a * (1.0 - weights.x - weights.y) + original_b * weights.x + original_c * weights.y;
                // Preserve the existing ambient response when direct light is
                // removed, instead of applying shadow a second time through AO.
                ambient_response = clamp(ambient_response * original / max(lighting, vec3(1.0e-6)), 0.0, 1.0);
            }
        }
    }
    blade_color = vec4(
        evaluate_blade_color(
            grass.flags.w, height_factor, lighting, in_surface_color),
        1.0);
    // Root/tip texture gains and the optional toon rim are appearance controls,
    // not emission. Their combined result may not exceed the native terrain's
    // lit white response (including its TEV scale). Unlit grass keeps its opt-out.
    blade_lit_ceiling = grass_native_material_rgb(vec3(1.0), lighting, source_light);
    // Lighting is constant over a blade. The unclamped toon transform is
    // linear in color, so it commutes with perspective interpolation.
    ToonSurfaceParameters toon = environment_state.records[grass.flags.w].toon;
    if (toon.flags.x > 0.5)
        blade_color.rgb = oot3d_toon_banded_color(blade_color.rgb, lighting, toon.flags.y, true, toon);
    vec3 view_side =
        environment_state.records[
            grass.flags.w].view_side.xyz;
    vec3 guide_normal =
        dot(view_side, view_side) > 1.0e-8
            ? vec3(
                  dot(view_side, world_normal),
                  dot(
                      environment_state.records[
                          grass.flags.w].view_up.xyz,
                      world_normal),
                  -dot(
                      environment_state.records[
                          grass.flags.w].view_forward.xyz,
                      world_normal))
            : vec3(0.0, 0.0, 1.0);
    blade_normal_guide = vec4(
        guide_normal * 0.5 + 0.5,
        0.25098039215686274);
    blade_ambient_guide =
        vec4(ambient_response, 1.0);
    vec3 eye = environment_state.records[grass.flags.w].camera_position.xyz;
    vec3 toward_eye = eye - position;
    blade_view_direction = vec3(dot(view_side, toward_eye),
        dot(environment_state.records[grass.flags.w].view_up.xyz, toward_eye),
        -dot(environment_state.records[grass.flags.w].view_forward.xyz, toward_eye));
    vec4 rim_distance = environment_state.records[grass.flags.w].rim_distance;
    // Root distance is identical for all vertices and LOD representations.
    float root_distance = length(eye - in_base_height.xyz);
    blade_rim_weight = rim_distance.z * (1.0 - smoothstep(rim_distance.x, rim_distance.y, root_distance));
}
)glsl";
}

std::string BuildGrassFragmentShader(bool rasterDepth) {
    return std::string("#version 450\n") +
            (rasterDepth ? "#define GRASS_RASTER_DEPTH 1\n" : "") +
            std::string(Renderer3ds::PicaGeometryGuideShaderLibrary()) +
            std::string(kToonSurfaceResponseShader) +
            std::string(kGrassDistantTuftShader) + R"glsl(
layout(location=0) in vec4 blade_color;
layout(location=1) in vec4 blade_normal_guide;
layout(location=2) in vec4 blade_ambient_guide;
layout(location=3) in vec4 tuft_sample;
layout(location=4) flat in float lod_visibility;
layout(location=6) in vec3 blade_view_direction;
layout(location=7) flat in float blade_rim_weight;
layout(location=10) in vec3 blade_lit_ceiling;
struct GrassEnvironmentRecord {
    vec4 color_and_mode;
    vec2 lut[128];
    vec4 wind_direction_time;
    vec4 wind_primary;
    vec4 wind_detail;
    vec4 camera_position;
    vec4 appearance_root;
    vec4 appearance_tip;
    vec4 texture_color;
    vec4 texture_brightness_flags;
    vec4 light_directions[3];
    vec4 light_diffuse[3];
    vec4 light_ambient[3];
    vec4 view_forward;
    vec4 view_side;
    vec4 view_up;
    vec4 blade_shape;
    vec4 tuft_lod;
    vec4 distance_lod;
    vec4 tuft_style;
    ToonSurfaceParameters toon;
    uvec4 native_lighting;
    vec4 rim_distance;
    mat4 previous_position_to_clip;
    vec4 previous_camera_time;
    vec4 motion_state;
};
layout(std430, set=0, binding=0) readonly buffer GrassEnvironmentState {
    GrassEnvironmentRecord records[];
} environment_state;
layout(push_constant) uniform GrassState {
    mat4 position_to_clip;
    vec4 jitter_ndc;
    vec4 depth_state;
    uvec4 flags;
} grass;
layout(location=0) out vec4 out_color;
#if GRASS_AUXILIARY_OUTPUTS
layout(location=8) in vec4 blade_current_clip;
layout(location=9) in vec4 blade_previous_clip;
layout(location=1) out vec4 out_normal_guide;
layout(location=2) out vec4 out_material_guide;
layout(location=3) out vec4 out_rigid_motion;
layout(location=4) out vec4 out_ambient_guide;
layout(location=6) out uvec4 out_geometry_guide;
#endif
void main() {
#if GRASS_AUXILIARY_OUTPUTS
    // Before any discard: derivatives need the whole quad.
    vec3 blade_face_dx = dFdx(blade_view_direction), blade_face_dy = dFdy(blade_view_direction);
#endif
    // Blade-local stipple, not frame/screen-space noise. Discard before every
    // guide/depth write; fading geometry cannot leave invisible occluders.
    if (lod_visibility < 1.0) {
        vec2 cell = floor(vec2(tuft_sample.x*tuft_sample.z,tuft_sample.y)*32.0);
        float threshold = fract(sin(dot(cell,vec2(12.9898,78.233))+tuft_sample.w*37.719)*43758.5453);
        if (lod_visibility <= threshold) discard;
    }
    if (grass.flags.y == 0u && !grass_tuft_covered(tuft_sample,
            uint(environment_state.records[grass.flags.w].tuft_lod.z),
            environment_state.records[grass.flags.w].tuft_style.x)) discard;
    vec4 resolved_color = blade_color;
    ToonSurfaceParameters toon = environment_state.records[grass.flags.w].toon;
    if (toon.flags.x > 0.5)
        resolved_color.rgb = clamp(resolved_color.rgb, 0.0, 1.0);
    if (toon.flags.x > 0.5 && blade_rim_weight > 0.0)
        resolved_color.rgb = clamp(resolved_color.rgb + blade_lit_ceiling * blade_rim_weight *
            oot3d_toon_rim(blade_normal_guide.xyz * 2.0 - 1.0, blade_view_direction, toon), 0.0, 1.0);
    resolved_color.rgb = min(resolved_color.rgb, blade_lit_ceiling);
#if GRASS_AUXILIARY_OUTPUTS
    out_normal_guide = blade_normal_guide;
    out_geometry_guide = uvec4(0u, pica_geometry_encode_normal(pica_geometry_face_normal(
        blade_view_direction, blade_face_dx, blade_face_dy)), 2u, 0u);
    // With the placement's previous frame the blade writes exact motion and
    // keeps its temporal history. Without it, the motion pass invalidates
    // motion and the reactive mask rejects the history,
    // since wind and interaction still move the blade.
    bool exact_motion = environment_state.records[grass.flags.w].motion_state.z > 0.5 &&
        blade_current_clip.w > 1.0e-7 && blade_previous_clip.w > 1.0e-7;
    out_material_guide = vec4(0.0, 1.0, 0.0, exact_motion ? 0.0 : 1.0);
    out_ambient_guide = blade_ambient_guide;
    out_rigid_motion = vec4(0.0);
    if (exact_motion) {
        vec2 current_uv = blade_current_clip.xy / blade_current_clip.w * 0.5 + 0.5;
        vec2 previous_uv = blade_previous_clip.xy / blade_previous_clip.w * 0.5 + 0.5;
        out_rigid_motion.xyz = vec3(previous_uv - current_uv, 3.0);
    }
#endif
    float resolved_depth = gl_FragCoord.z;
    if (grass.depth_state.w > 0.5) {
        float pica_z_over_w =
            grass.flags.x != 0u ? -gl_FragCoord.z : gl_FragCoord.z;
        resolved_depth =
            pica_z_over_w * grass.depth_state.x +
            grass.depth_state.y;
        if (grass.depth_state.z > 0.5)
            resolved_depth /= max(gl_FragCoord.w, 1.0e-7);
    }
    vec4 fog_color_and_mode =
        environment_state.records[
            grass.flags.w].color_and_mode;
    if (fog_color_and_mode.w > 0.5) {
        float fog_depth =
            fog_color_and_mode.w > 1.5
                ? 1.0 - resolved_depth
                : resolved_depth;
        float fog_index = fog_depth * 128.0;
        float floor_index = clamp(floor(fog_index), 0.0, 127.0);
        vec2 sample_pair =
            environment_state.records[
                grass.flags.w].lut[
                uint(floor_index)];
        float fog_factor = clamp(
            sample_pair.x +
                sample_pair.y * (fog_index - floor_index),
            0.0, 1.0);
        resolved_color.rgb = mix(
            fog_color_and_mode.rgb,
            resolved_color.rgb, fog_factor);
    }
    out_color = resolved_color;
#ifdef GRASS_RASTER_DEPTH
    float written_depth = gl_FragCoord.z;
#else
    gl_FragDepth = clamp(resolved_depth, 0.0, 1.0);
    float written_depth = gl_FragDepth;
#endif
#if GRASS_AUXILIARY_OUTPUTS
    // Only rasterized, depth-visible blades/tufts occlude native contours.
    // The separate native geometry guide remains untouched.
    out_rigid_motion.a = 1.0 - written_depth;
#endif
}
)glsl";
}

std::string BuildGrassCompactionComputeShader() {
    return std::string("#version 450\n#define GRASS_ANCHOR_WORDS ") +
        std::to_string(sizeof(GrassWorldAnchor) / sizeof(uint32_t)) +
        "u\n#define GRASS_INSTANCE_WORDS " +
        std::to_string(sizeof(GrassInstance) / sizeof(uint32_t)) + "u\n" + R"glsl(
layout(local_size_x=256, local_size_y=1, local_size_z=1) in;

layout(std430, set=0, binding=0) readonly buffer StaticAnchors {
    uint words[];
} static_anchors;
layout(std430, set=0, binding=1) readonly buffer VisibleIndices {
    uint values[];
} visible_indices;
layout(std430, set=0, binding=2) readonly buffer InteractionSamples {
    vec4 values[];
} interaction_samples;
layout(std430, set=0, binding=3) writeonly buffer OutputInstances {
    uint words[];
} output_instances;

struct ActorCollider {
    vec4 previous_radius;
    vec4 current_half_height;
    vec4 velocity_teleported;
};
layout(std430, set=0, binding=4) readonly buffer Actors {
    ActorCollider values[];
} actors;
// Per static anchor: the last bend (XY bits), the frame stamp that wrote it and
// the bend it replaced (half precision).
layout(std430, set=0, binding=5) buffer BendHistory {
    uvec4 values[];
} bend_history;
layout(std430, set=0, binding=6) writeonly buffer PreviousBends {
    vec2 values[];
} previous_bends;
// GrassActorCells: cell offsets, then the actor indices of every cell.
layout(std430, set=0, binding=7) readonly buffer ActorCells {
    uint words[];
} actor_cells;
// GPU selection control (word 3: admitted roots) when history.z is set.
layout(std430, set=0, binding=8) readonly buffer SelectionControl {
    uint values[];
} selection_control;

layout(push_constant) uniform CompactionState {
    uvec4 counts_flags;
    vec4 interaction;
    vec4 collision;
    vec4 bend;
    // current frame stamp, previous frame stamp (0 without continuity)
    uvec4 history;
    // actor cells: origin X/Z, inverse cell size; width, height, cell count
    vec4 actor_grid;
    uvec4 actor_grid_size;
} state;

vec2 sample_interaction(vec3 base) {
    uint resolution = state.counts_flags.y;
    bool initialized = (state.counts_flags.w & 1u) != 0u;
    if (!initialized || resolution == 0u)
        return vec2(0.0);
    float extent = state.interaction.z;
    float cell = (extent * 2.0) / float(resolution);
    float half_cell = extent / float(resolution);
    if (base.x < state.interaction.x - extent + half_cell ||
        base.x > state.interaction.x + extent - half_cell ||
        base.z < state.interaction.y - extent + half_cell ||
        base.z > state.interaction.y + extent - half_cell)
        return vec2(0.0);
    float origin_x = state.interaction.x - extent;
    float origin_z = state.interaction.y - extent;
    vec2 grid = vec2(
        (base.x - origin_x) / cell - 0.5,
        (base.z - origin_z) / cell - 0.5);
    if (grid.x < 0.0 || grid.y < 0.0 ||
        grid.x > float(resolution - 1u) ||
        grid.y > float(resolution - 1u))
        return vec2(0.0);
    uvec2 lower = uvec2(floor(grid));
    uvec2 upper = min(
        lower + uvec2(1u),
        uvec2(resolution - 1u));
    vec2 fraction = grid - vec2(lower);
    vec2 result = vec2(0.0);
    uvec2 coordinates[4] = uvec2[4](
        uvec2(lower.x, lower.y),
        uvec2(upper.x, lower.y),
        uvec2(lower.x, upper.y),
        uvec2(upper.x, upper.y));
    float weights[4] = float[4](
        (1.0 - fraction.x) * (1.0 - fraction.y),
        fraction.x * (1.0 - fraction.y),
        (1.0 - fraction.x) * fraction.y,
        fraction.x * fraction.y);
    for (uint index = 0u; index < 4u; ++index) {
        uint sample_index =
            coordinates[index].y * resolution +
            coordinates[index].x;
        vec4 sample_value =
            interaction_samples.values[sample_index];
        if (!isnan(sample_value.z) &&
            abs(base.y - sample_value.z) <=
                state.interaction.w) {
            result += sample_value.xy * weights[index];
        }
    }
    return result;
}

vec2 resolve_direct_collision(vec3 base, float blade_height, ActorCollider actor) {
    if (state.collision.z <= 0.0 ||
        blade_height <= 0.0)
        return vec2(0.0);
    vec3 previous = actor.previous_radius.xyz;
    vec3 current = actor.current_half_height.xyz;
    float radius = max(0.01, actor.previous_radius.w * state.collision.x);
    // Reject the swept bounding box before projection, roots and normalization.
    if (any(lessThan(base.xz, min(previous.xz, current.xz) - radius)) ||
        any(greaterThan(base.xz, max(previous.xz, current.xz) + radius))) return vec2(0.0);
    vec2 segment = current.xz - previous.xz;
    float segment_length_squared = dot(segment, segment);
    float t = 1.0;
    if (actor.velocity_teleported.w <= 0.5 &&
        segment_length_squared > 1.0e-6) {
        t = clamp(
            dot(base.xz - previous.xz, segment) /
                segment_length_squared,
            0.0, 1.0);
    }
    vec3 closest = mix(previous, current, t);
    vec2 delta = base.xz - closest.xz;
    float distance = length(delta);
    if (distance >= radius)
        return vec2(0.0);
    float full_height = max(
        radius,
        actor.current_half_height.w * 2.0 *
            state.collision.y);
    float collider_bottom =
        closest.y - state.bend.y;
    float collider_top =
        closest.y + full_height + state.bend.y;
    if (base.y + blade_height < collider_bottom ||
        base.y > collider_top)
        return vec2(0.0);
    if (distance > 1.0e-5) {
        delta /= distance;
    } else {
        float velocity_length =
            length(actor.velocity_teleported.xz);
        float motion_length = sqrt(segment_length_squared);
        if (velocity_length > 1.0e-5)
            delta =
                actor.velocity_teleported.xz /
                velocity_length;
        else if (motion_length > 1.0e-5)
            delta = segment / motion_length;
        else
            delta = vec2(1.0, 0.0);
    }
    // Contact alone, as ResolveGrassCollision: the velocity response is the
    // trail's, which recovers with its spring.
    float weight =
        (1.0 - distance / radius) *
        state.collision.z;
    return delta * min(weight, state.bend.x);
}

void main() {
    uint visible_index = gl_GlobalInvocationID.x;
    uint visible_count = state.history.z != 0u ? selection_control.values[3] : state.counts_flags.x;
    if (visible_index >= visible_count)
        return;
    uint static_index =
        visible_indices.values[visible_index];
    if (static_index >= state.counts_flags.z)
        return;
    uint source = static_index * GRASS_ANCHOR_WORDS;
    vec4 base_height = uintBitsToFloat(uvec4(
        static_anchors.words[source + 0u],
        static_anchors.words[source + 1u],
        static_anchors.words[source + 2u],
        static_anchors.words[source + 3u]));
    vec2 half_width_phase = uintBitsToFloat(uvec2(
        static_anchors.words[source + 4u],
        static_anchors.words[source + 5u]));
    vec2 width_axis = unpackSnorm2x16(static_anchors.words[source + 6u]);
    vec2 oct = unpackSnorm2x16(static_anchors.words[source + 7u]);
    vec3 normal = vec3(oct, 1.0 - abs(oct.x) - abs(oct.y));
    float fold = max(-normal.z, 0.0);
    normal.xy += mix(vec2(fold), vec2(-fold), greaterThanEqual(normal.xy, vec2(0.0)));
    // Identical to GrassStableVisibilityValue; W was unused in the instance ABI.
    uint stable = static_anchors.words[source + 8u];
    stable ^= stable >> 16u;
    stable *= 0x7feb352du;
    stable ^= stable >> 15u;
    stable *= 0x846ca68bu;
    stable ^= stable >> 16u;
    vec4 world_normal = vec4(normalize(normal), float(stable >> 8u) / 16777216.0);
    vec2 dynamic_bend = sample_interaction(base_height.xyz);
    uint actor_count = state.counts_flags.w >> 1u;
    if (state.actor_grid_size.x == 0u || any(isnan(base_height.xz)) || any(isinf(base_height.xz))) {
        for (uint i = 0u; i < actor_count; ++i)
            dynamic_bend += resolve_direct_collision(base_height.xyz, base_height.w, actors.values[i]);
    } else {
        // Only the actors whose swept box can contain this root, in order;
        // every other actor would add an exact zero.
        vec2 cell = floor((base_height.xz - state.actor_grid.xy) * state.actor_grid.z);
        uint first = 0u;
        uint last = 0u;
        if (cell.x >= 0.0 && cell.y >= 0.0 &&
            cell.x < float(state.actor_grid_size.x) && cell.y < float(state.actor_grid_size.y)) {
            uint index = uint(cell.y) * state.actor_grid_size.x + uint(cell.x);
            first = actor_cells.words[index];
            last = actor_cells.words[index + 1u];
        }
        uint list = state.actor_grid_size.z + 1u;
        for (uint i = first; i < last; ++i)
            dynamic_bend += resolve_direct_collision(
                base_height.xyz, base_height.w, actors.values[actor_cells.words[list + i]]);
        if (last - first < actor_count)
            dynamic_bend += vec2(0.0);
    }
    float bend_length = length(dynamic_bend);
    if (bend_length > state.bend.x && bend_length > 0.0)
        dynamic_bend *= state.bend.x / bend_length;
    // Motion vectors place the blade where the previous frame drew it. A
    // later invocation in the same frame reads the bend this frame replaced.
    uvec4 history = bend_history.values[static_index];
    vec2 previous_bend = dynamic_bend;
    if (history.z == state.history.x) {
        previous_bend = unpackHalf2x16(history.w);
    } else {
        if (state.history.y != 0u && history.z == state.history.y)
            previous_bend = uintBitsToFloat(history.xy);
        bend_history.values[static_index] = uvec4(
            floatBitsToUint(dynamic_bend), state.history.x, packHalf2x16(previous_bend));
    }
    previous_bends.values[visible_index] = previous_bend;

    uint destination = visible_index * GRASS_INSTANCE_WORDS;
    output_instances.words[destination + 0u] =
        floatBitsToUint(base_height.x);
    output_instances.words[destination + 1u] =
        floatBitsToUint(base_height.y);
    output_instances.words[destination + 2u] =
        floatBitsToUint(base_height.z);
    output_instances.words[destination + 3u] =
        floatBitsToUint(base_height.w);
    output_instances.words[destination + 4u] =
        floatBitsToUint(dynamic_bend.x);
    output_instances.words[destination + 5u] =
        floatBitsToUint(dynamic_bend.y);
    output_instances.words[destination + 6u] =
        floatBitsToUint(half_width_phase.x);
    output_instances.words[destination + 7u] =
        floatBitsToUint(half_width_phase.y);
    output_instances.words[destination + 8u] =
        floatBitsToUint(width_axis.x);
    output_instances.words[destination + 9u] =
        floatBitsToUint(width_axis.y);
    output_instances.words[destination + 10u] =
        floatBitsToUint(world_normal.x);
    output_instances.words[destination + 11u] =
        floatBitsToUint(world_normal.y);
    output_instances.words[destination + 12u] =
        floatBitsToUint(world_normal.z);
    output_instances.words[destination + 13u] =
        floatBitsToUint(world_normal.w);
    output_instances.words[destination + 14u] = static_anchors.words[source + 9u];
    output_instances.words[destination + 15u] = static_anchors.words[source + 10u];
    output_instances.words[destination + 16u] = static_anchors.words[source + 11u];
}
)glsl";
}

std::string BuildGrassShadowGridComputeShader() {
    // The coarse grid and the refinement bind different atlases (static
    // depth, or the composed map); layout 1 has its own.
    return std::string("#version 450\n#define PICA_VSM_LAYOUT_ATLASES\n") +
        std::string(Renderer3ds::VirtualShadowReceiverShader()) +
        std::string(kGrassShadowRefinementShader) + R"glsl(
layout(local_size_x=64) in;
struct GrassGridPoint { vec4 position; vec4 normal; };
layout(std430,set=0,binding=0) readonly buffer GridPoints { GrassGridPoint values[]; } points;
layout(std430,set=0,binding=1) writeonly buffer GridVisibility { GrassShadowVisibility values[]; } visibility;
layout(std430,set=0,binding=2) readonly buffer SurfacePatches { uvec2 values[]; } surface_patches;
layout(std430,set=0,binding=3) readonly buffer RefinementPatches { GrassShadowPatch values[]; } patches;
layout(push_constant) uniform GridDispatch { uint count; uint first; uint coarse_count; uint patch_count; } dispatch;

vec3 grass_shadow_surface_point(GrassShadowPatch tile, vec2 position) {
    uint axis = floatBitsToUint(tile.normal.w);
    vec2 uv[3] = vec2[3](grass_shadow_plane(tile.a.xyz, axis),
                         grass_shadow_plane(tile.b.xyz, axis), grass_shadow_plane(tile.c.xyz, axis));
    vec3 world[3] = vec3[3](tile.a.xyz, tile.b.xyz, tile.c.xyz);
    vec2 ab = uv[1] - uv[0], ac = uv[2] - uv[0], delta = position - uv[0];
    float determinant = ab.x * ac.y - ab.y * ac.x;
    if (abs(determinant) <= 1.0e-12) return (world[0] + world[1] + world[2]) / 3.0;
    float b = (delta.x * ac.y - delta.y * ac.x) / determinant;
    float c = (ab.x * delta.y - ab.y * delta.x) / determinant;
    if (b >= 0.0 && c >= 0.0 && b + c <= 1.0)
        return world[0] * (1.0 - b - c) + world[1] * b + world[2] * c;
    // Border samples stay on the real native triangle, never an extrapolated
    // plane across a cliff or the floor of another overlapping UV island.
    float nearest = 3.402823e38;
    vec3 result = world[0];
    for (uint edge = 0u; edge < 3u; ++edge) {
        uint next = (edge + 1u) % 3u;
        vec2 segment = uv[next] - uv[edge];
        float squared = dot(segment, segment);
        float t = squared > 0.0 ? clamp(dot(position - uv[edge], segment) / squared, 0.0, 1.0) : 0.0;
        vec2 error = mix(uv[edge], uv[next], t) - position;
        float distance = dot(error, error);
        if (distance < nearest) { nearest = distance; result = mix(world[edge], world[next], t); }
    }
    return result;
}
void main() {
    uint index = dispatch.first + gl_GlobalInvocationID.x;
    if (index >= dispatch.count) return;
    GrassGridPoint point;
    uint packed_patches = 0u;
    if (index < dispatch.coarse_count) {
        point = points.values[index];
        uvec2 coverage = surface_patches.values[floatBitsToUint(point.normal.w)];
        packed_patches = coverage.x | (coverage.y << 16u);
    } else {
        uint low = 0u, high = dispatch.patch_count;
        while (low + 1u < high) {
            uint middle = (low + high) / 2u;
            if (patches.values[middle].address.x <= index) low = middle;
            else high = middle;
        }
        GrassShadowPatch tile = patches.values[low];
        uint local = index - tile.address.x;
        vec2 position = tile.grid.xy + vec2(local % tile.address.y, local / tile.address.y) * tile.a.w;
        point.position = vec4(grass_shadow_surface_point(tile, position), 1.0);
        point.normal = tile.normal;
    }
    // A point without a normal has no depth slope.
    vec3 tangent = vec3(0.0), bitangent = vec3(0.0);
    if (dot(point.normal.xyz, point.normal.xyz) > 1.0e-12) {
        vec3 normal = normalize(point.normal.xyz);
        tangent = normalize(cross(normal, abs(normal.z) < 0.9 ? vec3(0,0,1) : vec3(0,1,0)));
        bitangent = cross(normal, tangent);
    }
    float value = pica_vsm_visibility(point.position.xyz, tangent, bitangent, vec3(0.0));
    visibility.values[index] = GrassShadowVisibility(value, packed_patches);
}
)glsl";
}

// GPU grass selection, three passes over one flat cluster index space (each
// source padded to whole workgroups). The decisions follow the CPU selection
// (GrassVisibilityQuery::Classify, the retained prefix, ResolveGrassLod...,
// the per-blade frustum test); float rounding may differ at exact ties.
// Output order is the CPU's: source, topology slot (bin order), cluster,
// anchor; the budget keeps the first visible roots in that order. No atomics
// decide an order, so the output is deterministic.
std::string BuildGrassSelectionComputeShader() {
    return std::string("#version 450\n") + R"glsl(
#ifndef GRASS_SELECTION_PASS
#define GRASS_SELECTION_PASS 0
#endif
layout(local_size_x=256, local_size_y=1, local_size_z=1) in;

const uint SLOT_COUNTERS = 8u;   // seven topology slots, then the visible total
const uint TOTAL = 7u;
const uint NO_SLOT = 0xFFu;
const uint RECORD_WORDS = 6u;    // first, end, eight 16-bit counts
const uint CLUSTER_WORDS = 8u;
const uint HEADER_WORDS = 64u;
const uint SOURCE_WORDS = 32u;
const uint DRAW_BASE = 4u;       // control: dispatch xyz, admitted total, draws
const uint MAX_SOURCES = 16u;
const uint RANK_BASE = DRAW_BASE + MAX_SOURCES * SLOT_COUNTERS * 5u;

layout(std430, set=0, binding=0) readonly buffer StaticAnchors { uint words[]; } static_anchors;
layout(std430, set=0, binding=1) readonly buffer Clusters { uint words[]; } clusters;
layout(std430, set=0, binding=2) readonly buffer Sources { uint words[]; } sources;
layout(std430, set=0, binding=3) buffer AnchorSlots { uint values[]; } anchor_slots;
layout(std430, set=0, binding=4) buffer ClusterRecords { uint words[]; } records;
layout(std430, set=0, binding=5) buffer GroupCounts { uint values[]; } groups;
layout(std430, set=0, binding=6) writeonly buffer Visible { uint values[]; } visible;
layout(std430, set=0, binding=7) buffer Control { uint values[]; } control;

layout(push_constant) uniform SelectionPolicy {
    // draw distance, its square, LOD reference, LOD start
    vec4 distance;
    // LOD end, far density, segment start, segment end
    vec4 lod;
    // tuft transition, far tuft density, segment softness, blade radius scale
    vec4 tuft;
    // near segments, far segments, far tufts enabled | frustum culling << 1, far tuft blades
    uvec4 counts;
} policy;

uint header(uint index) { return sources.words[index]; }
uint source_word(uint source, uint index) { return sources.words[HEADER_WORDS + source * SOURCE_WORDS + index]; }
float source_float(uint source, uint index) { return uintBitsToFloat(source_word(source, index)); }

uint find_source(uint slot) {
    uint count = header(0u);
    for (uint s = 0u; s < count; ++s) {
        uint first = source_word(s, 25u) * 256u;
        if (slot >= first && slot < first + source_word(s, 26u) * 256u)
            return s;
    }
    return 0xFFFFFFFFu;
}

float tuft_weight(float value, float start, float end) {
    float t = start < end ? clamp((value - start) / (end - start), 0.0, 1.0) : (value >= start ? 1.0 : 0.0);
    return t * t * (3.0 - 2.0 * t);
}
float tuft_mean_growth(float weight) {
    if (weight <= 0.0) return 0.0;
    if (weight >= 1.0) return 1.0;
    if (weight < 0.01)
        return weight * weight * (0.5 + weight * (1.0 / 6.0 + weight * (1.0 / 12.0 + weight * 0.05)));
    return weight + (1.0 - weight) * log(1.0 - weight);
}
float tuft_retention_scale(float weight) {
    float blades = float(max(policy.counts.w, 1u) - 1u);
    return (1.0 + (policy.tuft.y - 1.0) * weight) / (1.0 + blades * tuft_mean_growth(weight));
}
float density_retention(float normalized) {
    float lod_start = policy.distance.w;
    float far_density = policy.lod.y;
    if (normalized <= lod_start) return 1.0;
    if (normalized > 1.0) return far_density / (normalized * normalized);
    float remaining = clamp((1.0 - normalized) / max(1.0 - lod_start, 1.0e-6), 0.0, 1.0);
    float squared = remaining * remaining;
    return far_density + (1.0 - far_density) * squared * squared;
}
bool far_tufts() { return (policy.counts.z & 1u) != 0u; }
float retention_from_normalized(float normalized) {
    float weight = tuft_weight(normalized, policy.lod.x, policy.lod.x + policy.tuft.x);
    float scale = far_tufts() ? tuft_retention_scale(weight) : 1.0;
    return clamp(density_retention(normalized) * scale, 0.0, 1.0);
}
bool distance_in_range(float d) {
    return !(isnan(d) || isinf(d) || d < 0.0 || d > policy.distance.x || policy.distance.x <= 0.0);
}
float retention_upper_bound(float d) {
    if (!distance_in_range(d)) return 0.0;
    float normalized = d / policy.distance.z;
    if (!far_tufts() || policy.tuft.y <= 1.0) return retention_from_normalized(normalized);
    float weight = tuft_weight(normalized, policy.lod.x, policy.lod.x + policy.tuft.x);
    float coverage = 1.0 + float(policy.counts.w - 1u) * tuft_mean_growth(weight);
    return min(1.0, density_retention(normalized) * policy.tuft.y / coverage);
}
float stable_unit(uint value) {
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return float(value >> 8u) / 16777216.0;
}
// Returns the CPU bin (0..24) or NO_SLOT when the root is not retained.
uint resolve_bin(float d, float stable_visibility) {
    if (!distance_in_range(d)) return NO_SLOT;
    float normalized = d / policy.distance.z;
    float progress = 0.0;
    float segment_start = policy.lod.z, segment_end = policy.lod.w;
    if (d > segment_start)
        progress = segment_end > segment_start ? clamp((d - segment_start) / (segment_end - segment_start), 0.0, 1.0) : 1.0;
    uint near_segments = policy.counts.x, far_segments = policy.counts.y;
    uint middle = max(far_segments, (near_segments + 1u) / 2u);
    float choice = stable_unit(uint(stable_visibility * 16777216.0));
    float half_band = policy.tuft.z / 6.0;
    float first = tuft_weight(progress, 1.0 / 3.0 - half_band, 1.0 / 3.0 + half_band);
    float second = tuft_weight(progress, 2.0 / 3.0 - half_band, 2.0 / 3.0 + half_band);
    uint segments = choice < second ? far_segments : choice < first ? middle : near_segments;
    float tuft_w = tuft_weight(normalized, policy.lod.x, policy.lod.x + policy.tuft.x);
    bool distant = choice < tuft_w;
    if (!(stable_visibility <= retention_from_normalized(normalized))) return NO_SLOT;
    if (far_tufts() && distant) return 24u;
    uint planes = distant ? 1u : 2u;
    return (clamp(segments, 1u, 12u) - 1u) * 2u + (planes - 1u);
}

// 0 outside, 1 intersecting, 2 inside (ClassifyGrassSphereInFrustum).
uint classify_sphere(uint source, vec3 center, float radius) {
    radius = max(radius, 0.0);
    vec4 clip;
    for (uint row = 0u; row < 4u; ++row)
        clip[row] = source_float(source, row) * center.x + source_float(source, 4u + row) * center.y +
                    source_float(source, 8u + row) * center.z + source_float(source, 12u + row);
    if (any(isnan(clip)) || any(isinf(clip))) return 0u;
    float radius_x = source_float(source, 19u) * radius;
    float radius_y = source_float(source, 20u) * radius;
    float radius_w = source_float(source, 21u) * radius;
    if (clip.w + radius_w <= 1.0e-7) return 0u;
    float extent = abs(clip.w) + radius_w;
    if (clip.x + radius_x < -extent || clip.x - radius_x > extent ||
        clip.y + radius_y < -extent || clip.y - radius_y > extent)
        return 0u;
    float minimum_w = clip.w - radius_w;
    if (minimum_w > 1.0e-7 && clip.x - radius_x >= -minimum_w && clip.x + radius_x <= minimum_w &&
        clip.y - radius_y >= -minimum_w && clip.y + radius_y <= minimum_w)
        return 2u;
    return 1u;
}

vec4 anchor_base(uint index) {
    uint w = index * 12u;
    return uintBitsToFloat(uvec4(static_anchors.words[w], static_anchors.words[w + 1u],
                                 static_anchors.words[w + 2u], static_anchors.words[w + 3u]));
}
float anchor_stable_visibility(uint index) {
    uint value = static_anchors.words[index * 12u + 8u];
    return stable_unit(value);
}
uint record_count(uint slot, uint counter) {
    uint word = records.words[slot * RECORD_WORDS + 2u + counter / 2u];
    return (counter & 1u) == 0u ? (word & 0xFFFFu) : (word >> 16u);
}

#if GRASS_SELECTION_PASS == 0
shared uint group_totals[SLOT_COUNTERS];
void main() {
    uint slot = gl_GlobalInvocationID.x;
    if (gl_LocalInvocationID.x < SLOT_COUNTERS) group_totals[gl_LocalInvocationID.x] = 0u;
    barrier();
    uint counts[SLOT_COUNTERS];
    for (uint k = 0u; k < SLOT_COUNTERS; ++k) counts[k] = 0u;
    uint first = 0u, end = 0u;
    uint source = find_source(slot);
    uint cluster = slot * CLUSTER_WORDS;
    if (source != 0xFFFFFFFFu && slot - source_word(source, 25u) * 256u < source_word(source, 24u)) {
        uint anchor_count = clusters.words[cluster + 1u];
        first = source_word(source, 22u) + clusters.words[cluster];
        end = first;
        vec3 center = uintBitsToFloat(uvec3(clusters.words[cluster + 2u], clusters.words[cluster + 3u],
                                            clusters.words[cluster + 4u]));
        float cluster_radius = uintBitsToFloat(clusters.words[cluster + 5u]);
        float blade_height = uintBitsToFloat(clusters.words[cluster + 6u]);
        float minimum_visibility = uintBitsToFloat(clusters.words[cluster + 7u]);
        vec3 eye = vec3(source_float(source, 16u), source_float(source, 17u), source_float(source, 18u));
        float radius = cluster_radius + blade_height * max(0.0, policy.tuft.w - 1.0);
        vec3 delta = center - eye;
        float distance_squared = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
        float limit = policy.distance.x + radius;
        uint relation = 0u;
        float retention = 0.0;
        if (!(distance_squared > limit * limit)) {
            float nearest = max(0.0, sqrt(distance_squared) - radius);
            if (!(nearest > policy.distance.x)) {
                relation = (policy.counts.z & 2u) != 0u ? classify_sphere(source, center, radius) : 2u;
                if (relation != 0u) {
                    retention = retention_upper_bound(nearest);
                    if (minimum_visibility > retention) relation = 0u;
                }
            }
        }
        if (relation != 0u) {
            uint cluster_end = first + anchor_count;
            while (end < cluster_end && !(retention < anchor_stable_visibility(end))) ++end;
            for (uint a = first; a < end; ++a) {
                vec4 base = anchor_base(a);
                vec3 offset = base.xyz - eye;
                float squared = offset.x * offset.x + offset.y * offset.y + offset.z * offset.z;
                uint bin = NO_SLOT;
                if (!(squared > policy.distance.y)) {
                    bin = resolve_bin(sqrt(squared), anchor_stable_visibility(a));
                    if (bin != NO_SLOT && relation != 2u) {
                        float half_width = uintBitsToFloat(static_anchors.words[a * 12u + 4u]);
                        if (classify_sphere(source, base.xyz, base.w * policy.tuft.w + half_width * 2.0) == 0u)
                            bin = NO_SLOT;
                    }
                }
                uint mapped = bin == NO_SLOT ? NO_SLOT : header(4u + bin);
                anchor_slots.values[a] = mapped;
                if (mapped != NO_SLOT) {
                    ++counts[mapped];
                    ++counts[TOTAL];
                }
            }
        }
    }
    uint record = slot * RECORD_WORDS;
    records.words[record] = first;
    records.words[record + 1u] = end;
    for (uint k = 0u; k < SLOT_COUNTERS; k += 2u)
        records.words[record + 2u + k / 2u] = min(counts[k], 0xFFFFu) | (min(counts[k + 1u], 0xFFFFu) << 16u);
    for (uint k = 0u; k < SLOT_COUNTERS; ++k)
        if (counts[k] != 0u) atomicAdd(group_totals[k], counts[k]);
    barrier();
    if (gl_LocalInvocationID.x < SLOT_COUNTERS)
        groups.values[gl_WorkGroupID.x * SLOT_COUNTERS + gl_LocalInvocationID.x] = group_totals[gl_LocalInvocationID.x];
}
#elif GRASS_SELECTION_PASS == 1
shared uint totals[SLOT_COUNTERS];
shared uint lane_sums[SLOT_COUNTERS][256];
uint prefix(uint group, uint counter) {
    uint group_count = header(1u);
    return group < group_count ? groups.values[(group_count + group) * SLOT_COUNTERS + counter] : totals[counter];
}
void main() {
    uint group_count = header(1u);
    uint lane = gl_LocalInvocationID.x;
    // Each lane scans a contiguous chunk of workgroups; the lane sums are
    // scanned in shared memory.
    uint chunk = (group_count + 255u) / 256u;
    uint begin = min(lane * chunk, group_count), end = min(begin + chunk, group_count);
    for (uint k = 0u; k < SLOT_COUNTERS; ++k) {
        uint sum = 0u;
        for (uint g = begin; g < end; ++g) sum += groups.values[g * SLOT_COUNTERS + k];
        lane_sums[k][lane] = sum;
    }
    barrier();
    for (uint stride = 1u; stride < 256u; stride <<= 1u) {
        uint add[SLOT_COUNTERS];
        for (uint k = 0u; k < SLOT_COUNTERS; ++k) add[k] = lane >= stride ? lane_sums[k][lane - stride] : 0u;
        barrier();
        for (uint k = 0u; k < SLOT_COUNTERS; ++k) lane_sums[k][lane] += add[k];
        barrier();
    }
    for (uint k = 0u; k < SLOT_COUNTERS; ++k) {
        uint running = lane == 0u ? 0u : lane_sums[k][lane - 1u];
        for (uint g = begin; g < end; ++g) {
            groups.values[(group_count + g) * SLOT_COUNTERS + k] = running;
            running += groups.values[g * SLOT_COUNTERS + k];
        }
        if (lane == 255u) totals[k] = lane_sums[k][255];
    }
    memoryBarrierBuffer();
    memoryBarrierShared();
    barrier();
    if (lane != 0u) return;
    uint budget = header(3u);
    uint slot_count = header(2u);
    uint cut[SLOT_COUNTERS];
    for (uint k = 0u; k < SLOT_COUNTERS; ++k) cut[k] = 0xFFFFFFFFu;
    if (totals[TOTAL] > budget) {
        // The workgroup holding visible rank `budget` (the last whose prefix
        // does not exceed it), then its clusters and the roots of the
        // cluster that crosses it.
        uint low = 0u, high = group_count - 1u;
        while (low < high) {
            uint middle = (low + high + 1u) / 2u;
            if (prefix(middle, TOTAL) <= budget) low = middle;
            else high = middle - 1u;
        }
        uint cut_group = low;
        for (uint k = 0u; k < SLOT_COUNTERS; ++k) cut[k] = prefix(cut_group, k);
        for (uint c = 0u; c < 256u && cut[TOTAL] < budget; ++c) {
            uint slot = cut_group * 256u + c;
            uint total = record_count(slot, TOTAL);
            if (cut[TOTAL] + total <= budget) {
                for (uint k = 0u; k < SLOT_COUNTERS; ++k) cut[k] += record_count(slot, k);
                continue;
            }
            uint end = records.words[slot * RECORD_WORDS + 1u];
            for (uint a = records.words[slot * RECORD_WORDS]; a < end && cut[TOTAL] < budget; ++a) {
                uint mapped = anchor_slots.values[a];
                if (mapped == NO_SLOT) continue;
                ++cut[mapped];
                ++cut[TOTAL];
            }
        }
    }
    uint offset = 0u;
    uint source_count = header(0u);
    for (uint s = 0u; s < source_count; ++s) {
        uint begin_group = source_word(s, 25u);
        uint end_group = begin_group + source_word(s, 26u);
        for (uint k = 0u; k < slot_count; ++k) {
            uint start = prefix(begin_group, k);
            uint finish = prefix(end_group, k);
            uint admitted = min(finish, cut[k]) - min(start, cut[k]);
            uint draw = DRAW_BASE + (s * SLOT_COUNTERS + k) * 5u;
            control.values[draw] = header(32u + k * 2u);
            control.values[draw + 1u] = admitted;
            control.values[draw + 2u] = header(33u + k * 2u);
            control.values[draw + 3u] = 0u;
            control.values[draw + 4u] = offset;
            // Wraps like a signed offset: position = rank + (offset - start).
            control.values[RANK_BASE + s * SLOT_COUNTERS + k] = offset - start;
            offset += admitted;
        }
    }
    control.values[0] = (offset + 255u) / 256u;
    control.values[1] = 1u;
    control.values[2] = 1u;
    control.values[3] = offset;
}
#else
shared uint scan[SLOT_COUNTERS][256];
void main() {
    uint slot = gl_GlobalInvocationID.x;
    uint lane = gl_LocalInvocationID.x;
    uint own[SLOT_COUNTERS];
    for (uint k = 0u; k < SLOT_COUNTERS; ++k) {
        own[k] = record_count(slot, k);
        scan[k][lane] = own[k];
    }
    barrier();
    for (uint stride = 1u; stride < 256u; stride <<= 1u) {
        uint add[SLOT_COUNTERS];
        for (uint k = 0u; k < SLOT_COUNTERS; ++k) add[k] = lane >= stride ? scan[k][lane - stride] : 0u;
        barrier();
        for (uint k = 0u; k < SLOT_COUNTERS; ++k) scan[k][lane] += add[k];
        barrier();
    }
    if (own[TOTAL] == 0u) return;
    uint group_count = header(1u);
    uint rank[SLOT_COUNTERS];
    for (uint k = 0u; k < SLOT_COUNTERS; ++k)
        rank[k] = groups.values[(group_count + gl_WorkGroupID.x) * SLOT_COUNTERS + k] + scan[k][lane] - own[k];
    uint budget = header(3u);
    if (rank[TOTAL] >= budget) return;
    uint source = find_source(slot);
    uint end = records.words[slot * RECORD_WORDS + 1u];
    for (uint a = records.words[slot * RECORD_WORDS]; a < end && rank[TOTAL] < budget; ++a) {
        uint mapped = anchor_slots.values[a];
        if (mapped == NO_SLOT) continue;
        visible.values[control.values[RANK_BASE + source * SLOT_COUNTERS + mapped] + rank[mapped]] = a;
        ++rank[mapped];
        ++rank[TOTAL];
    }
}
#endif
)glsl";
}

}
