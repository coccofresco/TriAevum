#include "fast/oot3d/grass_shader_sources.h"
#include "fast/oot3d/grass_blade_shape.h"
#include "fast/oot3d/grass_card_atlas.h"
#include "fast/oot3d/grass_card_lod.h"
#include "fast/oot3d/grass_distant_tuft.h"
#include "fast/oot3d/grass_indexed_topology.h"
#include "fast/oot3d/grass_world_placement_cache.h"
#include "fast/oot3d/toon_surface_response.h"
#include "fast/renderer3ds/pica_geometry_guide.h"
#include "fast/renderer3ds/pica_surface_lighting_shader.h"
#include "fast/renderer3ds/virtual_shadow_receiver_shader.h"

#include <string>

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

// One GrassWorldAnchor (three uvec4) as a blade reads it: the anchors stay in
// the resident store and each root addresses one, instead of a per-frame copy.
constexpr char kGrassRootAnchorShader[] = R"glsl(
struct GrassRoot {
    vec4 base_height;
    vec2 half_width_phase;
    vec2 width_axis;
    // W: GrassStableVisibilityValue of the anchor's stable id.
    vec4 world_normal;
    uint surface_color;
    uvec2 surface_reference;
};
vec3 grass_unpack_normal(uint packed) {
    vec2 oct = unpackSnorm2x16(packed);
    vec3 normal = vec3(oct, 1.0 - abs(oct.x) - abs(oct.y));
    float fold = max(-normal.z, 0.0);
    normal.xy += mix(vec2(fold), vec2(-fold), greaterThanEqual(normal.xy, vec2(0.0)));
    return normalize(normal);
}
GrassRoot grass_decode_anchor(uvec4 a, uvec4 b, uvec4 c) {
    GrassRoot root;
    root.base_height = uintBitsToFloat(a);
    root.half_width_phase = uintBitsToFloat(b.xy);
    root.width_axis = unpackSnorm2x16(b.z);
    uint stable = c.x;
    stable ^= stable >> 16u;
    stable *= 0x7feb352du;
    stable ^= stable >> 15u;
    stable *= 0x846ca68bu;
    stable ^= stable >> 16u;
    root.world_normal = vec4(grass_unpack_normal(b.w), float(stable >> 8u) / 16777216.0);
    root.surface_color = c.y;
    root.surface_reference = c.zw;
    return root;
}
)glsl";

// Every grass program: the placement's environment record
// (GrassEnvironmentRecord of grass_draw_state.h, one layout) and the draw's
// push constants. Needs ToonSurfaceParameters.
constexpr char kGrassEnvironmentShader[] = R"glsl(
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
    // Card tier ends (blades, clumps, groups) and the blend fraction.
    vec4 card_distances;
    // Card tile height over footprint radius per level, cards enabled.
    vec4 card_style;
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

// The clip transform of a world position, in both stages: the vertex
// programs place their vertices with it and the card fragment program its
// texel's blade.
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
)glsl";

// Vertex-side shading the blades and the cards share: the native surface
// lighting, the coarse shadow grid with the actor patches, the lights, the
// root/tip colours and the wind. Needs the environment,
// pica_surface_recompose and the shadow refinement types.
constexpr char kGrassShadingShader[] = R"glsl(
layout(set=0,binding=1) uniform sampler2DArray native_surface_lighting;
layout(std430,set=0,binding=4) readonly buffer GrassShadowVisibilityGrid {
    GrassShadowVisibility values[];
} shadow_visibility_grid;
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

bool grass_native_lighting_enabled(uint environment_index) {
    return environment_state.records[environment_index].native_lighting.y != 0u;
}
bool grass_native_lighting_coefficients(uint environment_index) {
    return (environment_state.records[environment_index].native_lighting.y & 2u) != 0u;
}

// The root's native surface lighting: its three surface vertices' texels,
// weighted, with the shadow visibility applied to the directional part.
void grass_apply_native_lighting(
    uint environment_index, uvec2 surface_reference, float shadow_visibility,
    inout vec3 lighting, inout vec3 ambient_response) {
    uvec4 source_light = environment_state.records[environment_index].native_lighting;
    if (source_light.y == 0u) return;
    uvec3 vertices = uvec3(surface_reference.x & 65535u,
        surface_reference.x >> 16u, surface_reference.y & 65535u) + source_light.x;
    vec2 weights = vec2((surface_reference.y >> 16u) & 255u, surface_reference.y >> 24u) / 255.0;
    bool coefficients = (source_light.y & 2u) != 0u;
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

// A world vector in the view basis the guides use (side, up, toward eye).
vec3 grass_view_space(uint environment_index, vec3 vector) {
    vec3 view_side = environment_state.records[environment_index].view_side.xyz;
    if (dot(view_side, view_side) <= 1.0e-8) return vec3(0.0, 0.0, 1.0);
    return vec3(dot(view_side, vector),
        dot(environment_state.records[environment_index].view_up.xyz, vector),
        -dot(environment_state.records[environment_index].view_forward.xyz, vector));
}
)glsl";

// Every grass fragment program ends in grass_write_fragment: the toon clamp
// and rim, the lit ceiling, the guides, the PICA depth mapping, the fog LUT,
// the colour and depth writes. Needs the environment, the toon response and
// the geometry guide library.
constexpr char kGrassFragmentOutputShader[] = R"glsl(
layout(location=0) out vec4 out_color;
#if GRASS_AUXILIARY_OUTPUTS
layout(location=1) out vec4 out_normal_guide;
layout(location=2) out vec4 out_material_guide;
layout(location=3) out vec4 out_rigid_motion;
layout(location=4) out vec4 out_ambient_guide;
layout(location=6) out uvec4 out_geometry_guide;
#endif
// raster_z and inverse_w: the fragment's window depth and 1/w (gl_FragCoord
// for a rasterized surface; a card passes its texel's blade).
void grass_write_fragment(
    vec4 resolved_color, vec4 normal_guide, vec4 ambient_guide, vec3 view_direction,
    vec3 face_dx, vec3 face_dy, float rim_weight, vec3 lit_ceiling,
    vec4 current_clip, vec4 previous_clip, float raster_z, float inverse_w) {
    ToonSurfaceParameters toon = environment_state.records[grass.flags.w].toon;
    if (toon.flags.x > 0.5)
        resolved_color.rgb = clamp(resolved_color.rgb, 0.0, 1.0);
    if (toon.flags.x > 0.5 && rim_weight > 0.0)
        resolved_color.rgb = clamp(resolved_color.rgb + lit_ceiling * rim_weight *
            oot3d_toon_rim(normal_guide.xyz * 2.0 - 1.0, view_direction, toon), 0.0, 1.0);
    resolved_color.rgb = min(resolved_color.rgb, lit_ceiling);
#if GRASS_AUXILIARY_OUTPUTS
    out_normal_guide = normal_guide;
    out_geometry_guide = uvec4(0u, pica_geometry_encode_normal(pica_geometry_face_normal(
        view_direction, face_dx, face_dy)), 2u, 0u);
    // With the placement's previous frame the grass writes exact motion and
    // keeps its temporal history. Without it, the motion pass invalidates
    // motion and the reactive mask rejects the history,
    // since wind and interaction still move the grass.
    bool exact_motion = environment_state.records[grass.flags.w].motion_state.z > 0.5 &&
        current_clip.w > 1.0e-7 && previous_clip.w > 1.0e-7;
    out_material_guide = vec4(0.0, 1.0, 0.0, exact_motion ? 0.0 : 1.0);
    out_ambient_guide = ambient_guide;
    out_rigid_motion = vec4(0.0);
    if (exact_motion) {
        vec2 current_uv = current_clip.xy / current_clip.w * 0.5 + 0.5;
        vec2 previous_uv = previous_clip.xy / previous_clip.w * 0.5 + 0.5;
        out_rigid_motion.xyz = vec3(previous_uv - current_uv, 3.0);
    }
#endif
    float resolved_depth = raster_z;
    if (grass.depth_state.w > 0.5) {
        float pica_z_over_w =
            grass.flags.x != 0u ? -raster_z : raster_z;
        resolved_depth =
            pica_z_over_w * grass.depth_state.x +
            grass.depth_state.y;
        if (grass.depth_state.z > 0.5)
            resolved_depth /= max(inverse_w, 1.0e-7);
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
    float written_depth = raster_z;
#else
    gl_FragDepth = clamp(resolved_depth, 0.0, 1.0);
    float written_depth = gl_FragDepth;
#endif
#if GRASS_AUXILIARY_OUTPUTS
    // Only rasterized, depth-visible grass occludes native contours.
    // The separate native geometry guide remains untouched.
    out_rigid_motion.a = 1.0 - written_depth;
#endif
}
)glsl";

// The card tiers' visibility weight (GrassCardLevelWeight of grass_card_lod.h).
constexpr char kGrassCardWeightShader[] = R"glsl(
float grass_card_weight(uint level, float distance, vec4 card_distances) {
    float blend = card_distances.w;
    float half_blend = blend * 0.5;
    float enter = level == 0u ? card_distances.x : level == 1u ? card_distances.y : card_distances.z;
    float weight = grass_tuft_weight(distance, enter * (1.0 - blend), enter * (1.0 - half_blend));
    if (level + 1u < 3u) {
        float leave = level == 0u ? card_distances.y : card_distances.z;
        weight *= 1.0 - grass_tuft_weight(distance, leave * (1.0 - half_blend), leave);
    }
    return weight;
}
)glsl";

std::string GrassCardConstants() {
    return "#define GRASS_CARD_ELEVATIONS " + std::to_string(kGrassCardAtlasElevations) + "u\n" +
           "#define GRASS_CARD_VARIANTS " + std::to_string(kGrassCardAtlasVariants) + "u\n" +
           "#define GRASS_CARD_LEVELS " + std::to_string(kGrassCardLevelCount) + "u\n";
}
} // namespace

std::string BuildGrassVertexShader() {
    return
            std::string("#version 450\n") + std::string(kToonSurfaceResponseShader) + std::string(kGrassBladeShapeShader) +
            std::string(kGrassDistantTuftShader) + std::string(kGrassIndexedVertexShader) +
            std::string(Renderer3ds::kPicaSurfaceLightingRecomposeShader) +
            std::string(kGrassShadowRefinementShader) + std::string(kGrassRootAnchorShader) +
            std::string(kGrassEnvironmentShader) + std::string(kGrassShadingShader) + R"glsl(
// Root i draws the anchor root_anchor_indices[i] of root_anchors.
layout(std430,set=0,binding=2) readonly buffer GrassRootAnchors { uvec4 values[]; } root_anchors;
layout(std430,set=0,binding=9) readonly buffer GrassRootAnchorIndices { uint values[]; } root_anchor_indices;
layout(std430,set=0,binding=3) readonly buffer GrassPreparedGroups { uvec2 entries[]; } prepared_groups;
struct GrassShadowReference { uint first; uint second; float weight; };
layout(std430,set=0,binding=5) readonly buffer GrassShadowReferences { GrassShadowReference values[]; } shadow_references;
layout(std430,set=0,binding=6) readonly buffer GrassVisibleRoots { uint values[]; } visible_roots;
// Per root, half precision: the interaction bend (X) and the bend the
// previous frame drew (Y).
layout(std430,set=0,binding=8) readonly buffer GrassRootBends { uvec2 values[]; } root_bends;

GrassRoot grass_load_root(uint root) {
    uint first = root_anchor_indices.values[root] * 3u;
    return grass_decode_anchor(root_anchors.values[first], root_anchors.values[first + 1u],
                               root_anchors.values[first + 2u]);
}
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

void main() {
    bool grouped = (grass.flags.y & 0x80000000u) != 0u;
    uint segment_mode = grass.flags.y & 0x1fffffffu;
    bool tuft = segment_mode == 0u;
    uint segments = clamp(segment_mode, 1u, 12u);
    uint vertex = uint(gl_VertexIndex);
    uint root_index = uint(gl_InstanceIndex);
    uint representative = root_index;
    if (grouped) {
        uint vertices_per_child = grass.flags.z * (2u * segments + 1u);
        uint child = vertex / vertices_per_child;
        vertex %= vertices_per_child;
        uvec2 group = prepared_groups.entries[gl_InstanceIndex];
        if (child >= group.y) { gl_Position = vec4(0.0,0.0,2.0,1.0); return; }
        root_index = group.x + child;
        representative = group.x;
    }
    GrassRoot root = grass_load_root(root_index);
    uvec2 packed_bends = root_bends.values[root_index];
    vec4 bends = vec4(unpackHalf2x16(packed_bends.x), unpackHalf2x16(packed_bends.y));
    vec4 in_base_height = root.base_height;
    vec4 in_bend_half_width = vec4(bends.xy, root.half_width_phase);
    vec2 in_width_axis = root.width_axis;
    vec4 in_world_normal = root.world_normal;
    uint in_surface_color = root.surface_color;
    uvec2 in_surface_reference = root.surface_reference;
    vec3 visibility_position = in_base_height.xyz;
    float visibility_seed = in_world_normal.w;
    if (representative != root_index) {
        GrassRoot group_root = grass_load_root(representative);
        visibility_position = group_root.base_height.xyz;
        visibility_seed = group_root.world_normal.w;
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
    // A batch drawn twice (kGrassWholePassFlag, kGrassFadingPassFlag): each
    // pass drops the blades of the other whole. The whole pass's fragment
    // program has no discard; its blades never needed one.
    uint split = grass.flags.y & 0x60000000u;
    if (split != 0u && ((!tuft && lod_visibility >= 1.0) != (split == 0x40000000u))) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        return;
    }
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
        vec2 previous_bend = motion_state.w > 0.5 ? bends.zw : in_bend_half_width.xy;
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
        float shadow_visibility = 1.0;
        if ((source_light.y & 2u) != 0u) {
            GrassShadowReference reference = shadow_references.values[visible_roots.values[root_index]];
            GrassShadowVisibility first = shadow_visibility_grid.values[reference.first];
            shadow_visibility = mix(first.visibility,
                shadow_visibility_grid.values[reference.second].visibility, reference.weight);
            shadow_visibility = grass_local_shadow(
                shadow_visibility, in_base_height.xyz, world_normal, first.patches);
        }
        grass_apply_native_lighting(grass.flags.w, in_surface_reference, shadow_visibility, lighting, ambient_response);
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
    blade_normal_guide = vec4(
        grass_view_space(grass.flags.w, world_normal) * 0.5 + 0.5,
        0.25098039215686274);
    blade_ambient_guide =
        vec4(ambient_response, 1.0);
    vec3 eye = environment_state.records[grass.flags.w].camera_position.xyz;
    blade_view_direction = grass_view_space(grass.flags.w, eye - position);
    vec4 rim_distance = environment_state.records[grass.flags.w].rim_distance;
    // Root distance is identical for all vertices and LOD representations.
    float root_distance = length(eye - in_base_height.xyz);
    blade_rim_weight = rim_distance.z * (1.0 - smoothstep(rim_distance.x, rim_distance.y, root_distance));
}
)glsl";
}

std::string BuildGrassFragmentShader(bool rasterDepth, bool wholeBlades) {
    return std::string("#version 450\n") +
            (rasterDepth ? "#define GRASS_RASTER_DEPTH 1\n" : "") +
            (wholeBlades ? "#define GRASS_WHOLE_BLADES 1\n" : "") +
            std::string(Renderer3ds::PicaGeometryGuideShaderLibrary()) +
            std::string(kToonSurfaceResponseShader) +
            std::string(kGrassDistantTuftShader) + std::string(kGrassEnvironmentShader) +
            std::string(kGrassFragmentOutputShader) + R"glsl(
layout(location=0) in vec4 blade_color;
layout(location=1) in vec4 blade_normal_guide;
layout(location=2) in vec4 blade_ambient_guide;
layout(location=3) in vec4 tuft_sample;
layout(location=4) flat in float lod_visibility;
layout(location=6) in vec3 blade_view_direction;
layout(location=7) flat in float blade_rim_weight;
layout(location=10) in vec3 blade_lit_ceiling;
#if GRASS_AUXILIARY_OUTPUTS
layout(location=8) in vec4 blade_current_clip;
layout(location=9) in vec4 blade_previous_clip;
#endif
void main() {
    vec3 blade_face_dx = vec3(0.0), blade_face_dy = vec3(0.0);
    vec4 current_clip = vec4(0.0), previous_clip = vec4(0.0);
#if GRASS_AUXILIARY_OUTPUTS
    // Before any discard: derivatives need the whole quad.
    blade_face_dx = dFdx(blade_view_direction); blade_face_dy = dFdy(blade_view_direction);
    current_clip = blade_current_clip; previous_clip = blade_previous_clip;
#endif
#ifndef GRASS_WHOLE_BLADES
    // Blade-local stipple, not frame/screen-space noise. Discard before every
    // guide/depth write; fading geometry cannot leave invisible occluders.
    if (lod_visibility < 1.0) {
        vec2 cell = floor(vec2(tuft_sample.x*tuft_sample.z,tuft_sample.y)*32.0);
        float threshold = fract(sin(dot(cell,vec2(12.9898,78.233))+tuft_sample.w*37.719)*43758.5453);
        if (lod_visibility <= threshold) discard;
    }
    // Ungrouped distant tufts (segment mode 0), in either pass.
    if ((grass.flags.y & 0x9fffffffu) == 0u && !grass_tuft_covered(tuft_sample,
            uint(environment_state.records[grass.flags.w].tuft_lod.z),
            environment_state.records[grass.flags.w].tuft_style.x)) discard;
#endif
    grass_write_fragment(blade_color, blade_normal_guide, blade_ambient_guide, blade_view_direction,
        blade_face_dx, blade_face_dy, blade_rim_weight, blade_lit_ceiling, current_clip, previous_clip,
        gl_FragCoord.z, gl_FragCoord.w);
}
)glsl";
}

std::string BuildGrassCardVertexShader() {
    return std::string("#version 450\n") + GrassCardConstants() + std::string(kToonSurfaceResponseShader) +
            std::string(kGrassDistantTuftShader) + std::string(Renderer3ds::kPicaSurfaceLightingRecomposeShader) +
            std::string(kGrassShadowRefinementShader) + std::string(kGrassRootAnchorShader) +
            std::string(kGrassEnvironmentShader) + std::string(kGrassShadingShader) +
            std::string(kGrassCardWeightShader) + R"glsl(
// GrassCardRecord of grass_card_lod.h.
struct GrassCardRecord {
    vec4 root_height;
    vec4 extent_phase;
    // normal (octahedral), seed, surface colour, first shadow point
    uvec4 packed;
    // second shadow point, shadow weight (float bits), surface reference
    uvec4 shadow;
};
layout(std430,set=0,binding=10) readonly buffer GrassCardRecords { GrassCardRecord values[]; } card_records;

layout(location=0) flat out vec4 card_root_color;
layout(location=1) flat out vec4 card_tip_color;
layout(location=2) flat out vec4 card_normal_guide;
layout(location=3) flat out vec4 card_ambient_guide;
// u across the card, the vertical position over the tile radius
layout(location=4) out vec2 card_uv;
// weight, dither seed, atlas layer of the lower and the upper elevation tile
layout(location=5) flat out vec4 card_lod;
layout(location=6) out vec3 card_view_direction;
layout(location=7) flat out float card_rim_weight;
layout(location=8) out vec4 card_current_clip;
layout(location=9) out vec4 card_previous_clip;
layout(location=10) flat out vec3 card_lit_ceiling;
// tile mix, the tile set's height over radius, the lower tile's elevation
// index, the blade depth scale (world units per encoded unit)
layout(location=11) flat out vec4 card_tiles;
// the point of the card plane (through the root) under the fragment
layout(location=12) out vec3 card_world_position;
// toward the eye (the atlas depth axis)
layout(location=13) flat out vec3 card_toward;
// how far toward the eye of the card plane the drawn fragment lies: the
// largest blade offset the fragment may place in front of the plane
layout(location=14) out float card_shift;

const float kCardHalfPi = 1.57079632679;

// The tile set whose height over radius is nearest the card's, in log space
// (GrassCardAtlasSetFor).
uint card_tile_set(float ratio, vec4 card_style, out float set_ratio) {
    uint best = 0u;
    set_ratio = card_style.x;
    float nearest = abs(log(ratio) - log(card_style.x));
    float d1 = abs(log(ratio) - log(card_style.y));
    if (d1 < nearest) { nearest = d1; best = 1u; set_ratio = card_style.y; }
    float d2 = abs(log(ratio) - log(card_style.z));
    if (d2 < nearest) { best = 2u; set_ratio = card_style.z; }
    return best;
}

// The quad corner of a card seen from an eye at a time: the plane through
// the representative root that faces the eye, the tile's extent
// (GrassCardTileExtentAt) and the wind shear of the top edge. The elevation
// the tiles are mixed at is returned for the fragment's per-tile mapping.
vec3 card_corner(vec3 eye, float wind_time, uint env, vec3 root, float height, float tile_radius,
                 float set_ratio, float phase, float sx, float sy, out float elevation, out vec3 toward,
                 out float vertical) {
    vec3 to_eye = eye - root;
    float distance = max(length(to_eye), 1.0e-3);
    toward = to_eye / distance;
    elevation = clamp(asin(clamp(toward.y, -1.0, 1.0)), 0.0, kCardHalfPi);
    vec3 horizontal = vec3(toward.x, 0.0, toward.z);
    float horizontal_length = length(horizontal);
    vec3 side = horizontal_length > 1.0e-4 ? vec3(horizontal.z, 0.0, -horizontal.x) / horizontal_length
                                           : environment_state.records[env].view_side.xyz;
    vec3 up_axis = cross(toward, side);
    float cos_e = cos(elevation), sin_e = sin(elevation);
    float above = tile_radius * (set_ratio * cos_e + sin_e);
    float below = tile_radius * sin_e;
    float t = mix(-below, above, sy);
    vertical = t / tile_radius;
    vec3 position = root + side * (sx * tile_radius) + up_axis * t;
    // The top shears with the wind like a blade's tip.
    float bend_fraction = clamp(t / max(height, 1.0e-3), 0.0, 1.0);
    vec2 wind = evaluate_wind(env, root, phase, wind_time);
    position.xz += wind * height * bend_fraction * (0.65 + 0.35 * bend_fraction);
    return position;
}

void main() {
    uint env = grass.flags.w;
    uint level = min(grass.flags.y, GRASS_CARD_LEVELS - 1u);
    GrassCardRecord card = card_records.values[gl_InstanceIndex];
    // Six vertices: two triangles of the quad (bottom-left, bottom-right,
    // top-left; top-left, bottom-right, top-right).
    uint corner = uint(gl_VertexIndex) % 6u;
    uint quad = corner < 3u ? corner : corner == 3u ? 2u : corner == 4u ? 1u : 3u;
    float sx = (quad & 1u) != 0u ? 1.0 : -1.0;
    float sy = (quad >> 1u) != 0u ? 1.0 : 0.0;
    vec3 root = card.root_height.xyz;
    float height = max(card.root_height.w, 1.0e-3);
    float radius = max(card.extent_phase.x, 1.0e-3);
    vec4 card_distances = environment_state.records[env].card_distances;
    vec4 card_style = environment_state.records[env].card_style;
    vec3 eye = environment_state.records[env].camera_position.xyz;
    float distance = length(eye - root);
    float weight = card_style.w > 0.5 ? grass_card_weight(level, distance, card_distances) : 0.0;
    card_lod = vec4(0.0);
    card_tiles = vec4(0.0);
    card_current_clip = vec4(0.0);
    card_previous_clip = vec4(0.0);
    if (weight <= 0.0 || distance <= 1.0e-3) { gl_Position = vec4(0.0, 0.0, 2.0, 1.0); return; }
    // The tile set nearest the card's blade height over footprint radius; the
    // card is drawn at the radius at which that set's blades have its height,
    // so the tile keeps its proportions (and its ground footprint).
    float set_ratio;
    uint tile_set = card_tile_set(height / radius, card_style, set_ratio);
    float tile_radius = height / set_ratio;
    float depth_scale = (1.0 + set_ratio) * tile_radius;
    // Frustum test before the shading: a card none of whose points (the
    // quad, its wind shear, its blade offsets, all within twice the depth
    // scale of the root) can reach the clip volume collapses here. The
    // projection's rows bound how far a world offset moves each clip term.
    {
        vec4 root_clip = grass.position_to_clip * vec4(root, 1.0);
        float reach = 2.0 * depth_scale;
        mat4 m = grass.position_to_clip;
        float margin_w = reach * length(vec3(m[0][3], m[1][3], m[2][3]));
        float margin_x = reach * length(vec3(m[0][0], m[1][0], m[2][0]));
        float margin_y = reach * length(vec3(m[0][1], m[1][1], m[2][1]));
        float w_max = root_clip.w + margin_w;
        if (w_max <= 0.0 || abs(root_clip.x) - margin_x > w_max || abs(root_clip.y) - margin_y > w_max) {
            gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
            return;
        }
    }
    float elevation;
    float vertical;
    vec3 position = card_corner(eye, environment_state.records[env].wind_direction_time.z, env, root, height,
                                tile_radius, set_ratio, card.extent_phase.y, sx, sy, elevation, card_toward, vertical);
    card_world_position = position;
    // Conservative depth: the corner is drawn on its own eye ray, pulled
    // toward the eye by the largest blade offset the tile encodes (in view
    // depth), so the blade the fragment places lies at or behind the drawn
    // fragment and the raster-depth programs keep the early depth test. The
    // projection is unchanged, the plane only moves along the rays.
    vec3 forward = environment_state.records[env].view_forward.xyz;
    if (dot(forward, forward) <= 1.0e-8) forward = -card_toward;
    float cos_theta = max(-dot(card_toward, forward), 1.0e-3);
    float view_depth = max(dot(position - eye, forward), 1.0e-3);
    float ray = clamp(1.0 - depth_scale * cos_theta / view_depth, 0.1, 1.0);
    card_shift = (1.0 - ray) * view_depth / cos_theta;
    gl_Position = grass_clip_position(grass.position_to_clip, eye + (position - eye) * ray, grass.jitter_ndc.xy);
    // Exact motion like the blades: the same corner with the previous
    // frame's eye, wind time and jitter, on the same fraction of its ray.
    vec4 motion_state = environment_state.records[env].motion_state;
    if (motion_state.z > 0.5) {
        vec4 previous_camera_time = environment_state.records[env].previous_camera_time;
        float previous_elevation, previous_vertical;
        vec3 previous_toward;
        vec3 previous_position = card_corner(previous_camera_time.xyz, previous_camera_time.w, env, root, height,
                                             tile_radius, set_ratio, card.extent_phase.y, sx, sy, previous_elevation,
                                             previous_toward, previous_vertical);
        card_current_clip = gl_Position;
        card_previous_clip = grass_clip_position(
            environment_state.records[env].previous_position_to_clip,
            previous_camera_time.xyz + (previous_position - previous_camera_time.xyz) * ray, motion_state.xy);
    }
    card_uv = vec2(sx * 0.5 + 0.5, vertical);
    // The two elevation tiles around the view's elevation, mixed per texel;
    // each tile maps the vertical position through its own extent.
    float tile_position = elevation / kCardHalfPi * float(GRASS_CARD_ELEVATIONS - 1u);
    uint tile0 = min(uint(floor(tile_position)), GRASS_CARD_ELEVATIONS - 2u);
    uint tile1 = tile0 + 1u;
    uint variant = card.packed.y % GRASS_CARD_VARIANTS;
    uint layer0 = (tile_set * GRASS_CARD_ELEVATIONS + tile0) * GRASS_CARD_VARIANTS + variant;
    uint layer1 = (tile_set * GRASS_CARD_ELEVATIONS + tile1) * GRASS_CARD_VARIANTS + variant;
    card_lod = vec4(weight, float(card.packed.y >> 8u) / 16777216.0, float(layer0), float(layer1));
    card_tiles = vec4(clamp(tile_position - float(tile0), 0.0, 1.0), set_ratio, float(tile0), depth_scale);

    vec3 world_normal = grass_unpack_normal(card.packed.x);
    vec3 lighting;
    vec3 ambient_response;
    evaluate_shading(env, world_normal, lighting, ambient_response);
    uvec4 source_light = environment_state.records[env].native_lighting;
    if (source_light.y != 0u) {
        float shadow_visibility = 1.0;
        if ((source_light.y & 2u) != 0u && card.packed.w != 0xffffffffu) {
            GrassShadowVisibility first = shadow_visibility_grid.values[card.packed.w];
            shadow_visibility = mix(first.visibility,
                shadow_visibility_grid.values[card.shadow.x].visibility, uintBitsToFloat(card.shadow.y));
            shadow_visibility = grass_local_shadow(shadow_visibility, root, world_normal, first.patches);
        }
        grass_apply_native_lighting(env, card.shadow.zw, shadow_visibility, lighting, ambient_response);
    }
    card_root_color = vec4(evaluate_blade_color(env, 0.0, lighting, card.packed.z), 1.0);
    card_tip_color = vec4(evaluate_blade_color(env, 1.0, lighting, card.packed.z), 1.0);
    card_lit_ceiling = grass_native_material_rgb(vec3(1.0), lighting, source_light);
    ToonSurfaceParameters toon = environment_state.records[env].toon;
    if (toon.flags.x > 0.5) {
        card_root_color.rgb = oot3d_toon_banded_color(card_root_color.rgb, lighting, toon.flags.y, true, toon);
        card_tip_color.rgb = oot3d_toon_banded_color(card_tip_color.rgb, lighting, toon.flags.y, true, toon);
    }
    card_normal_guide = vec4(grass_view_space(env, world_normal) * 0.5 + 0.5, 0.25098039215686274);
    card_ambient_guide = vec4(ambient_response, 1.0);
    card_view_direction = grass_view_space(env, eye - position);
    vec4 rim_distance = environment_state.records[env].rim_distance;
    card_rim_weight = rim_distance.z * (1.0 - smoothstep(rim_distance.x, rim_distance.y, distance));
}
)glsl";
}

std::string BuildGrassCardFragmentShader(bool conservativeDepth) {
    return std::string("#version 450\n") +
            // The written depth is never nearer than the rasterized one (the
            // vertex program pulls the quad toward the eye by the largest
            // blade offset), which with the raster depth mapping keeps the
            // early depth test.
            (conservativeDepth ? "layout(depth_greater) out float gl_FragDepth;\n" : "") +
            GrassCardConstants() +
            std::string(Renderer3ds::PicaGeometryGuideShaderLibrary()) +
            std::string(kToonSurfaceResponseShader) + std::string(kGrassEnvironmentShader) +
            std::string(kGrassFragmentOutputShader) + R"glsl(
layout(set=0,binding=11) uniform sampler2DArray grass_card_atlas;
layout(location=0) flat in vec4 card_root_color;
layout(location=1) flat in vec4 card_tip_color;
layout(location=2) flat in vec4 card_normal_guide;
layout(location=3) flat in vec4 card_ambient_guide;
layout(location=4) in vec2 card_uv;
layout(location=5) flat in vec4 card_lod;
layout(location=6) in vec3 card_view_direction;
layout(location=7) flat in float card_rim_weight;
layout(location=10) flat in vec3 card_lit_ceiling;
layout(location=11) flat in vec4 card_tiles;
layout(location=12) in vec3 card_world_position;
layout(location=13) flat in vec3 card_toward;
layout(location=14) in float card_shift;
#if GRASS_AUXILIARY_OUTPUTS
layout(location=8) in vec4 card_current_clip;
layout(location=9) in vec4 card_previous_clip;
#endif
const float kCardTileStep = 1.57079632679 / float(GRASS_CARD_ELEVATIONS - 1u);

// The tile's v for the card's vertical position (over the tile radius): the
// tile spans [-sin e, ratio cos e + sin e] at its own elevation.
float card_tile_v(float vertical, float tile_elevation, float ratio) {
    float sin_e = sin(tile_elevation), cos_e = cos(tile_elevation);
    return (vertical + sin_e) / max(ratio * cos_e + 2.0 * sin_e, 1.0e-5);
}

void main() {
    vec3 face_dx = vec3(0.0), face_dy = vec3(0.0);
    vec4 current_clip = vec4(0.0), previous_clip = vec4(0.0);
#if GRASS_AUXILIARY_OUTPUTS
    face_dx = dFdx(card_view_direction); face_dy = dFdy(card_view_direction);
    current_clip = card_current_clip; previous_clip = card_previous_clip;
#endif
    // Both elevation tiles before any discard (their derivatives select the mip).
    float elevation0 = card_tiles.z * kCardTileStep;
    vec2 uv0 = vec2(card_uv.x, card_tile_v(card_uv.y, elevation0, card_tiles.y));
    vec2 uv1 = vec2(card_uv.x, card_tile_v(card_uv.y, elevation0 + kCardTileStep, card_tiles.y));
    vec4 lower = texture(grass_card_atlas, vec3(uv0, card_lod.z));
    vec4 upper = texture(grass_card_atlas, vec3(uv1, card_lod.w));
    float coverage = mix(lower.x, upper.x, card_tiles.x);
    if (coverage < 0.5) discard;
    // Card-local stipple for the tier blend, like the blades' fade.
    if (card_lod.x < 1.0) {
        vec2 cell = floor(card_uv * 48.0);
        float threshold = fract(sin(dot(cell, vec2(12.9898, 78.233)) + card_lod.y * 37.719) * 43758.5453);
        if (card_lod.x <= threshold) discard;
    }
    float height_factor = mix(lower.y, upper.y, card_tiles.x);
    vec4 color = mix(card_root_color, card_tip_color, height_factor);
    // The texel's blade sits ahead of or behind the card plane: its depth is
    // written, so the terrain and the actors inside the footprint resolve
    // against the blade and not against the plane. Never ahead of the drawn
    // fragment (card_shift): the conservative depth declaration holds.
    float offset = min((mix(lower.z, upper.z, card_tiles.x) * 2.0 - 1.0) * card_tiles.w, card_shift);
    vec4 blade_clip = grass_clip_position(grass.position_to_clip, card_world_position + card_toward * offset,
                                          grass.jitter_ndc.xy);
    float blade_w = max(blade_clip.w, 1.0e-6);
    grass_write_fragment(color, card_normal_guide, card_ambient_guide, card_view_direction,
        face_dx, face_dy, card_rim_weight, card_lit_ceiling, current_clip, previous_clip,
        blade_clip.z / blade_w, 1.0 / blade_w);
}
)glsl";
}
std::string BuildGrassCompactionComputeShader() {
    return std::string("#version 450\n#define GRASS_ANCHOR_WORDS ") +
        std::to_string(sizeof(GrassWorldAnchor) / sizeof(uint32_t)) + "u\n" + R"glsl(
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
// Per visible root, half precision: the interaction bend (X) and the bend the
// previous frame drew (Y), or the current one where the blade has no previous
// frame. The blades read everything else from the static anchors.
layout(std430, set=0, binding=3) writeonly buffer RootBends {
    uvec2 values[];
} root_bends;

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
// GrassActorCells: cell offsets, then the actor indices of every cell.
layout(std430, set=0, binding=6) readonly buffer ActorCells {
    uint words[];
} actor_cells;
// GPU selection control (word 3: admitted roots) when history.z is set.
layout(std430, set=0, binding=7) readonly buffer SelectionControl {
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
    root_bends.values[visible_index] = uvec2(packHalf2x16(dynamic_bend), packHalf2x16(previous_bend));
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

// GPU grass selection over one flat cluster index space (each source padded
// to whole workgroups): count (pass 0), scan (pass 1), recount under the
// budget cut (pass 3), scan again, scatter (pass 2). The decisions follow the
// CPU selection (GrassVisibilityQuery::Classify, the retained prefix,
// ResolveGrassLod..., the per-blade frustum test); float rounding may differ
// at exact ties. Output order is the CPU's: source, topology slot (bin
// order), cluster, anchor. Over budget, the roots with the lowest thinning
// key stay (grass_selection_budget.h): every cluster keeps the same fraction
// and a root's admission depends on its own key, not on the camera. No
// atomics decide an order, so the output is deterministic.
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
// Budget thinning (grass_selection_budget.h): the histogram of the visible
// roots' keys, then the cut (BUCKETS admits every root).
const uint BUCKETS = 1024u;
const uint HISTOGRAM_BASE = RANK_BASE + MAX_SOURCES * SLOT_COUNTERS;
const uint CUT_WORD = HISTOGRAM_BASE + BUCKETS;

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
    // near segments, far segments,
    // far tufts enabled | frustum culling << 1 | crossed blades << 2, far tuft blades
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
// Returns the CPU bin (0..24) or NO_SLOT when the root is not retained, and
// the thinning bucket of a retained root (GrassBudgetBucket).
uint resolve_bin(float d, float stable_visibility, out uint bucket) {
    bucket = 0u;
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
    float retention = retention_from_normalized(normalized);
    if (!(stable_visibility <= retention)) return NO_SLOT;
    if (retention > 0.0) {
        float scaled = stable_visibility / retention * float(BUCKETS);
        if (scaled > 0.0) bucket = min(BUCKETS - 1u, uint(scaled));
    }
    if (far_tufts() && distant) return 24u;
    uint planes = distant || (policy.counts.z & 4u) == 0u ? 1u : 2u;
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
shared uint histogram[BUCKETS];
void main() {
    uint slot = gl_GlobalInvocationID.x;
    if (gl_LocalInvocationID.x < SLOT_COUNTERS) group_totals[gl_LocalInvocationID.x] = 0u;
    for (uint b = gl_LocalInvocationID.x; b < BUCKETS; b += 256u) histogram[b] = 0u;
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
                uint bucket = 0u;
                if (!(squared > policy.distance.y)) {
                    bin = resolve_bin(sqrt(squared), anchor_stable_visibility(a), bucket);
                    if (bin != NO_SLOT && relation != 2u) {
                        float half_width = uintBitsToFloat(static_anchors.words[a * 12u + 4u]);
                        if (classify_sphere(source, base.xyz, base.w * policy.tuft.w + half_width * 2.0) == 0u)
                            bin = NO_SLOT;
                    }
                }
                uint mapped = bin == NO_SLOT ? NO_SLOT : header(4u + bin);
                // The slot in the low byte, the thinning bucket above it.
                anchor_slots.values[a] = mapped | (bucket << 8u);
                if (mapped != NO_SLOT) {
                    ++counts[mapped];
                    ++counts[TOTAL];
                    atomicAdd(histogram[bucket], 1u);
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
    for (uint b = gl_LocalInvocationID.x; b < BUCKETS; b += 256u)
        if (histogram[b] != 0u) atomicAdd(control.values[HISTOGRAM_BASE + b], histogram[b]);
}
#elif GRASS_SELECTION_PASS == 3
// Recount under the cut: the records and group totals of the admitted roots
// only. Skipped as a whole when nothing is thinned (the counts stand).
shared uint group_totals[SLOT_COUNTERS];
void main() {
    uint cut = control.values[CUT_WORD];
    if (cut == BUCKETS) return;
    uint slot = gl_GlobalInvocationID.x;
    if (gl_LocalInvocationID.x < SLOT_COUNTERS) group_totals[gl_LocalInvocationID.x] = 0u;
    barrier();
    uint counts[SLOT_COUNTERS];
    for (uint k = 0u; k < SLOT_COUNTERS; ++k) counts[k] = 0u;
    uint record = slot * RECORD_WORDS;
    uint end = records.words[record + 1u];
    for (uint a = records.words[record]; a < end; ++a) {
        uint value = anchor_slots.values[a];
        uint mapped = value & 0xFFu;
        if (mapped == NO_SLOT || (value >> 8u) >= cut) continue;
        ++counts[mapped];
        ++counts[TOTAL];
    }
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
// The budget cut: per-lane sums of four histogram buckets and their scan.
shared uint bucket_sums[2][256];
shared uint cut_lane;
// Double-buffered: each scan step reads one half and writes the other, so no
// invocation rewrites a value another may still read.
shared uint lane_sums[2][SLOT_COUNTERS][256];
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
        lane_sums[0][k][lane] = sum;
    }
    barrier();
    uint half_index = 0u;
    for (uint stride = 1u; stride < 256u; stride <<= 1u) {
        for (uint k = 0u; k < SLOT_COUNTERS; ++k)
            lane_sums[half_index ^ 1u][k][lane] = lane_sums[half_index][k][lane] +
                (lane >= stride ? lane_sums[half_index][k][lane - stride] : 0u);
        barrier();
        half_index ^= 1u;
    }
    for (uint k = 0u; k < SLOT_COUNTERS; ++k) {
        uint running = lane == 0u ? 0u : lane_sums[half_index][k][lane - 1u];
        for (uint g = begin; g < end; ++g) {
            groups.values[(group_count + g) * SLOT_COUNTERS + k] = running;
            running += groups.values[g * SLOT_COUNTERS + k];
        }
        if (lane == 255u) totals[k] = lane_sums[half_index][k][255];
    }
    // The cut from the histogram of every visible root (GrassBudgetCut): the
    // first bucket whose cumulative count would exceed the budget. The
    // histogram is the count pass's, so the second scan finds the same cut.
    // Every lane owns four consecutive buckets; a lane scan finds the one lane
    // whose range crosses the budget (no serial walk over the histogram).
    uint budget = header(3u);
    if (lane == 0u) cut_lane = BUCKETS;
    uint bucket_counts[4];
    uint own_sum = 0u;
    for (uint j = 0u; j < 4u; ++j) {
        bucket_counts[j] = control.values[HISTOGRAM_BASE + lane * 4u + j];
        own_sum += bucket_counts[j];
    }
    bucket_sums[0][lane] = own_sum;
    barrier();
    uint bucket_half = 0u;
    for (uint stride = 1u; stride < 256u; stride <<= 1u) {
        bucket_sums[bucket_half ^ 1u][lane] = bucket_sums[bucket_half][lane] +
            (lane >= stride ? bucket_sums[bucket_half][lane - stride] : 0u);
        barrier();
        bucket_half ^= 1u;
    }
    uint inclusive = bucket_sums[bucket_half][lane];
    uint running = inclusive - own_sum;
    if (running <= budget && inclusive > budget) {
        for (uint j = 0u; j < 4u; ++j) {
            if (running + bucket_counts[j] > budget) {
                cut_lane = lane * 4u + j;
                break;
            }
            running += bucket_counts[j];
        }
    }
    memoryBarrierBuffer();
    memoryBarrierShared();
    barrier();
    if (lane != 0u) return;
    uint slot_count = header(2u);
    uint cut = cut_lane;
    control.values[CUT_WORD] = cut;
    uint offset = 0u;
    uint source_count = header(0u);
    for (uint s = 0u; s < source_count; ++s) {
        uint begin_group = source_word(s, 25u);
        uint end_group = begin_group + source_word(s, 26u);
        for (uint k = 0u; k < slot_count; ++k) {
            uint start = prefix(begin_group, k);
            uint finish = prefix(end_group, k);
            uint admitted = finish - start;
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
// Double-buffered as the group scan.
shared uint scan[2][SLOT_COUNTERS][256];
void main() {
    uint slot = gl_GlobalInvocationID.x;
    uint lane = gl_LocalInvocationID.x;
    uint own[SLOT_COUNTERS];
    for (uint k = 0u; k < SLOT_COUNTERS; ++k) {
        own[k] = record_count(slot, k);
        scan[0][k][lane] = own[k];
    }
    barrier();
    uint half_index = 0u;
    for (uint stride = 1u; stride < 256u; stride <<= 1u) {
        for (uint k = 0u; k < SLOT_COUNTERS; ++k)
            scan[half_index ^ 1u][k][lane] = scan[half_index][k][lane] +
                (lane >= stride ? scan[half_index][k][lane - stride] : 0u);
        barrier();
        half_index ^= 1u;
    }
    if (own[TOTAL] == 0u) return;
    uint group_count = header(1u);
    uint rank[SLOT_COUNTERS];
    for (uint k = 0u; k < SLOT_COUNTERS; ++k)
        rank[k] = groups.values[(group_count + gl_WorkGroupID.x) * SLOT_COUNTERS + k] + scan[half_index][k][lane] - own[k];
    // The recount keeps the admitted total within the budget; the bound only
    // guards the visible buffer.
    uint budget = header(3u);
    if (rank[TOTAL] >= budget) return;
    uint cut = control.values[CUT_WORD];
    uint source = find_source(slot);
    uint end = records.words[slot * RECORD_WORDS + 1u];
    for (uint a = records.words[slot * RECORD_WORDS]; a < end && rank[TOTAL] < budget; ++a) {
        uint value = anchor_slots.values[a];
        uint mapped = value & 0xFFu;
        if (mapped == NO_SLOT || (value >> 8u) >= cut) continue;
        visible.values[control.values[RANK_BASE + source * SLOT_COUNTERS + mapped] + rank[mapped]] = a;
        ++rank[mapped];
        ++rank[TOTAL];
    }
}
#endif
)glsl";
}

}
