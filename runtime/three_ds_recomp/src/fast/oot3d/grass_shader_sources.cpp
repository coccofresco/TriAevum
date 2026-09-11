#include "fast/oot3d/grass_shader_sources.h"
#include "fast/oot3d/grass_blade_shape.h"
#include "fast/oot3d/grass_distant_tuft.h"
#include "fast/oot3d/grass_indexed_topology.h"
#include "fast/oot3d/grass_instance_layout.h"
#include "fast/oot3d/grass_world_placement_cache.h"
#include "fast/oot3d/toon_surface_response.h"

namespace Fast::Oot3d {
std::string BuildGrassVertexShader() {
    return
            std::string("#version 450\n") + std::string(kToonSurfaceResponseShader) + std::string(kGrassBladeShapeShader) +
            std::string(kGrassDistantTuftShader) + std::string(kGrassIndexedVertexShader) + R"glsl(
layout(location=0) in vec4 in_base_height;
layout(location=1) in vec4 in_bend_half_width;
layout(location=2) in vec2 in_width_axis;
layout(location=3) in vec4 in_world_normal;
layout(location=4) in uint in_surface_color;
layout(location=5) in uvec2 in_surface_reference;
layout(set=0,binding=1) uniform sampler2D native_surface_lighting;
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
layout(location=0) out vec4 blade_color;
layout(location=1) out vec4 blade_normal_guide;
layout(location=2) out vec4 blade_ambient_guide;
layout(location=3) out vec4 tuft_sample;
layout(location=4) flat out float lod_visibility;
layout(location=6) out vec3 blade_view_direction;
layout(location=7) flat out float blade_rim_weight;

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

vec3 evaluate_blade_color(
    uint environment_index, float height_factor,
    vec3 lighting) {
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
    vec3 world_position, float anchor_phase) {
    vec4 wind_direction_time =
        environment_state.records[
            environment_index].wind_direction_time;
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

void main() {
    bool tuft = grass.flags.y == 0u;
    uint segments = clamp(grass.flags.y, 1u, 12u);
    uint plane;
    float height_factor;
    float width_sign;
    grass_indexed_vertex(uint(gl_VertexIndex), segments, tuft, plane, height_factor, width_sign);
    float tuft_coverage = 1.0;
    vec4 lod = environment_state.records[grass.flags.w].tuft_lod;
    vec4 distance_lod = environment_state.records[grass.flags.w].distance_lod;
    vec4 tuft_style = environment_state.records[grass.flags.w].tuft_style;
    float distance = length(in_base_height.xyz - environment_state.records[grass.flags.w].camera_position.xyz);
    float normalized_distance = distance/max(lod.y,1.0e-6);
    float tuft_weight = grass_tuft_weight(normalized_distance,lod.x,lod.x+lod.w);
    float retention = grass_density_retention(normalized_distance,distance_lod.y,distance_lod.z);
    if (tuft_style.w > 0.5)
        retention *= grass_tuft_retention_scale(tuft_weight,lod.z,tuft_style.y);
    lod_visibility = grass_visibility_fade(clamp(retention,0.0,1.0),in_world_normal.w,distance_lod.w);
    lod_visibility *= 1.0-grass_tuft_weight(distance,distance_lod.x*(1.0-tuft_style.z),distance_lod.x);
    if (tuft) {
        float choice = grass_lod_choice(in_world_normal.w);
        float growth = clamp((tuft_weight-choice)/max(1.0-choice,1.0e-6),0.0,1.0);
        tuft_coverage += (lod.z-1.0) * growth * tuft_style.x;
    }
    tuft_sample = vec4(width_sign, height_factor, tuft_coverage, in_bend_half_width.w);

    vec2 width_axis = in_width_axis;
    if (grass.flags.z == 1u) {
        vec2 camera_delta =
            in_base_height.xz -
            environment_state.records[
                grass.flags.w].camera_position.xz;
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
            grass.flags.w, in_base_height.xyz,
            in_bend_half_width.w) +
        in_bend_half_width.xy;
    float bend_length = length(normalized_bend);
    float maximum_bend =
        environment_state.records[
            grass.flags.w].wind_detail.z;
    if (bend_length > maximum_bend &&
        bend_length > 1.0e-6)
        normalized_bend *=
            maximum_bend / bend_length;
    vec2 bend = normalized_bend * in_base_height.w;
    float twist;
    vec3 shape = grass_blade_shape(
        height_factor, in_bend_half_width.w, normalize(in_width_axis),
        environment_state.records[grass.flags.w].blade_shape, twist);
    if (!tuft) width_axis = mat2(cos(twist), sin(twist), -sin(twist), cos(twist)) * width_axis;
    vec3 position = in_base_height.xyz;
    position += shape * in_base_height.w;
    position.xz += bend * bend_factor;
    position.xz += width_axis * in_bend_half_width.z *
                   width_factor * width_sign;

    vec4 clip = grass.position_to_clip * vec4(position, 1.0);
    if (grass.flags.x != 0u) {
        // Match the generated PICA vertex shader exactly. Its projection is
        // already rotated for the physical CTR framebuffer.
        gl_Position = vec4(clip.x, clip.y, -clip.z, clip.w);
    } else {
        // Renderer-owned world projections are logical/unrotated and must be
        // transposed into the physical top framebuffer before scanout.
        gl_Position = vec4(clip.y, clip.x, clip.z, clip.w);
    }
    gl_Position.xy += grass.jitter_ndc.xy * gl_Position.w;
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
        uint width = uint(textureSize(native_surface_lighting, 0).x);
        vec4 a = texelFetch(native_surface_lighting, ivec2(vertices.x % width, vertices.x / width), 0);
        vec4 b = texelFetch(native_surface_lighting, ivec2(vertices.y % width, vertices.y / width), 0);
        vec4 c = texelFetch(native_surface_lighting, ivec2(vertices.z % width, vertices.z / width), 0);
        if (min(a.a, min(b.a, c.a)) > 0.5)
            lighting = a.rgb * (1.0 - weights.x - weights.y) + b.rgb * weights.x + c.rgb * weights.y;
    }
    blade_color = vec4(
        evaluate_blade_color(
            grass.flags.w, height_factor, lighting),
        1.0);
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

std::string BuildGrassFragmentShader() {
    return std::string("#version 450\n") +
            std::string(kToonSurfaceResponseShader) +
            std::string(kGrassDistantTuftShader) + R"glsl(
layout(location=0) in vec4 blade_color;
layout(location=1) in vec4 blade_normal_guide;
layout(location=2) in vec4 blade_ambient_guide;
layout(location=3) in vec4 tuft_sample;
layout(location=4) flat in float lod_visibility;
layout(location=6) in vec3 blade_view_direction;
layout(location=7) flat in float blade_rim_weight;
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
layout(location=1) out vec4 out_normal_guide;
layout(location=2) out vec4 out_material_guide;
layout(location=3) out vec4 out_rigid_motion;
layout(location=4) out vec4 out_ambient_guide;
#endif
void main() {
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
        resolved_color.rgb = clamp(resolved_color.rgb + blade_rim_weight *
            oot3d_toon_rim(blade_normal_guide.xyz * 2.0 - 1.0, blade_view_direction, toon), 0.0, 1.0);
#if GRASS_AUXILIARY_OUTPUTS
    out_normal_guide = blade_normal_guide;
    // Grass topology, LOD and wind can change every presentation. Let the
    // motion pass reconstruct camera motion from depth and reject temporal
    // history for these pixels instead of emitting unstable rigid vectors.
    out_material_guide = vec4(0.0, 1.0, 0.0, 1.0);
    out_ambient_guide = blade_ambient_guide;
    out_rigid_motion = vec4(0.0);
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
    gl_FragDepth = clamp(resolved_depth, 0.0, 1.0);
#if GRASS_AUXILIARY_OUTPUTS
    // Only rasterized, depth-visible blades/tufts occlude native contours.
    // The separate native geometry guide remains untouched.
    out_rigid_motion.a = 1.0 - gl_FragDepth;
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

layout(push_constant) uniform CompactionState {
    uvec4 counts_flags;
    vec4 interaction;
    vec4 collision;
    vec4 bend;
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
    float speed =
        length(actor.velocity_teleported.xz);
    float velocity_gain =
        1.0 + clamp(speed / 300.0, 0.0, 2.0) *
            state.collision.w;
    float weight =
        (1.0 - distance / radius) *
        state.collision.z * velocity_gain;
    return delta * min(weight, state.bend.x);
}

void main() {
    uint visible_index = gl_GlobalInvocationID.x;
    if (visible_index >= state.counts_flags.x)
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
    for (uint i = 0u; i < (state.counts_flags.w >> 1u); ++i)
        dynamic_bend += resolve_direct_collision(base_height.xyz, base_height.w, actors.values[i]);
    float bend_length = length(dynamic_bend);
    if (bend_length > state.bend.x && bend_length > 0.0)
        dynamic_bend *= state.bend.x / bend_length;

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
}
