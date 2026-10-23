#include "fast/oot3d/hiz_reflection.h"
#include "fast/oot3d/pica_surface_coordinates.h"
#include "fast/renderer3ds/pica_material_guide.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Fast::Oot3d {

namespace {
constexpr const char* kPushBlock = R"glsl(
layout(push_constant) uniform ReflectionPush {
    uvec2 extent;
    vec2 inverse_size;
    float strength;
    float max_distance;
    float thickness;
    float edge_fade;
    uint max_steps;
    uint hiz_mip_count;
    float projection_scale_x;
    float projection_scale_y;
    float projection_offset_x;
    float projection_offset_y;
    float near_plane;
    float far_plane;
    float roughness_bias;
    float normal_sigma;
    uint surface_orientation;
} pc;
)glsl";

// The water surface the view ray meets (HiZWaterUniforms), its waves (the
// surface shader's: eight directional waves) and Fresnel.
constexpr const char* kWaterLibrary = R"glsl(
layout(set = 0, binding = 7, std140) uniform HiZWater {
    vec4 view_to_world[3];
    vec4 footprints[8];
    vec4 heights[2];
    vec4 parameters;
    vec4 medium;
    vec4 rings[16];
    vec4 map_info;
    vec4 map_levels[4];
    vec4 map_counts;
    vec4 map_extent;
} hiz_water;
// Per texel of the drawn water's map: its surface height at the centre
// (-1e30: none), landing foam and the surface's slope (d height / dx, dz).
layout(set = 0, binding = 9, std430) readonly buffer HiZWaterMap {
    vec4 texels[];
} hiz_water_map;
vec4 hiz_water_map_texel(ivec2 texel) {
    int size = int(hiz_water.map_info.w);
    if (any(lessThan(texel, ivec2(0))) || any(greaterThanEqual(texel, ivec2(size)))) return vec4(-1.0e30, 0.0, 0.0, 0.0);
    return hiz_water_map.texels[texel.y * size + texel.x];
}
// A texel's surface height at a world position (its plane continued).
float hiz_water_map_height(ivec2 index, vec4 texel, vec2 xz) {
    vec2 centre = hiz_water.map_info.xy + (vec2(index) + 0.5) * hiz_water.map_info.z;
    return texel.x + dot(texel.zw, xz - centre);
}
// The surface height under a world position (-1e30: no water).
float hiz_water_map_surface(vec2 xz) {
    if (hiz_water.map_info.w <= 0.0) return -1.0e30;
    ivec2 index = ivec2(floor((xz - hiz_water.map_info.xy) / hiz_water.map_info.z));
    vec4 texel = hiz_water_map_texel(index);
    return texel.x > -1.0e29 ? hiz_water_map_height(index, texel, xz) : -1.0e30;
}
// The map around a world position for the surface at this height,
// interpolated between the four nearest texel centres (no texel steps): how
// much of it is water there (x; water where at least half), each texel's
// plane continued to the position (a sloped stream stays one surface), and
// its landing foam (y).
vec2 hiz_water_map_sample(vec2 xz, float height) {
    if (hiz_water.map_info.w <= 0.0) return vec2(0.0);
    vec2 position = (xz - hiz_water.map_info.xy) / hiz_water.map_info.z - 0.5;
    ivec2 base = ivec2(floor(position));
    vec2 f = position - vec2(base);
    vec2 result = vec2(0.0);
    for (int i = 0; i < 4; ++i) {
        ivec2 offset = ivec2(i & 1, i >> 1);
        vec4 texel = hiz_water_map_texel(base + offset);
        float weight = (offset.x == 1 ? f.x : 1.0 - f.x) * (offset.y == 1 ? f.y : 1.0 - f.y);
        if (texel.x > -1.0e29 && abs(hiz_water_map_height(base + offset, texel, xz) - height) <= 4.0)
            result += weight * vec2(1.0, texel.y);
    }
    return result;
}
vec3 hiz_water_view_direction(vec2 uv) {
    vec2 ndc = oot3d_surface_view_ndc(uv, pc.surface_orientation);
    return vec3((ndc.x + pc.projection_offset_x) / pc.projection_scale_x,
                (ndc.y + pc.projection_offset_y) / pc.projection_scale_y, -1.0);
}
vec3 hiz_water_to_world(vec3 v) {
    return vec3(dot(hiz_water.view_to_world[0].xyz, v), dot(hiz_water.view_to_world[1].xyz, v),
                dot(hiz_water.view_to_world[2].xyz, v));
}
// World distances of the map's edge rules (any scene scale; at least a
// texel): drawn water near a gameplay box, a lip's lower water, the fade of
// refraction at a piece's edge.
const float kHiZWaterNear = 24.0;
const float kHiZWaterLipNear = 30.0;
const float kHiZWaterLipFar = 60.0;
const float kHiZWaterEdgeFade = 24.0;
float hiz_water_map_reach(float world) {
    return max(world, hiz_water.map_info.z);
}
// Drawn water within kHiZWaterNear of a world position (any height).
bool hiz_water_map_drawn_near(vec2 xz) {
    if (hiz_water.map_info.w <= 0.0) return false;
    float reach = hiz_water_map_reach(kHiZWaterNear);
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            vec2 at = (xz + reach * vec2(x, y) - hiz_water.map_info.xy) / hiz_water.map_info.z;
            if (hiz_water_map_texel(ivec2(floor(at))).x > -1.0e29) return true;
        }
    return false;
}
// A drawn water fragment marked the material guide here (B at 1.0). Later
// transparent draws (mist, particles) may clear it: only edges rely on it.
bool hiz_water_marked(vec2 uv) {
    return textureLod(sampler2D(material_guide, nearest_sampler), uv, 0.0).b > 0.996;
}
// The nearest listed surface in front of the scene: its view depth, world
// position and view direction (depth 1); depth 0 if none.
float hiz_water_surface(vec2 uv, float scene_depth, out vec3 world_position, out vec3 direction) {
    uint count = min(uint(hiz_water.parameters.x), 8u);
    bool mapped = hiz_water.map_info.w > 0.0 && hiz_water.map_counts.x > 0.0;
    direction = hiz_water_view_direction(uv);
    world_position = vec3(0.0);
    if (count == 0u && !mapped) return 0.0;
    vec3 ray = hiz_water_to_world(direction);
    vec3 camera = vec3(hiz_water.view_to_world[0].w, hiz_water.view_to_world[1].w,
                       hiz_water.view_to_world[2].w);
    if (ray.y >= -1.0e-6) return 0.0;
    float nearest = 0.0;
    // Gameplay water boxes.
    for (uint i = 0u; i < count; ++i) {
        float height = hiz_water.heights[i >> 2u][i & 3u];
        if (camera.y <= height) continue;
        float depth = (height - camera.y) / ray.y;
        vec3 position = camera + ray * depth;
        vec4 footprint = hiz_water.footprints[i];
        if (position.x < footprint.x || position.z < footprint.y || position.x > footprint.z ||
            position.z > footprint.w) continue;
        // Where water is drawn nearby, the drawn water decides: a box reaching
        // past a piece's edge (over a drop) is water only where marked.
        if (hiz_water_map_sample(position.xz, height).x < 0.5 && hiz_water_map_drawn_near(position.xz) &&
            !hiz_water_marked(uv)) continue;
        if (nearest == 0.0 || depth < nearest) {
            nearest = depth;
            world_position = position;
        }
    }
    // The drawn water: where the view ray first goes below the map's surface
    // (each piece of a river with steps at its own height, a stream running
    // downhill sloped as it is), marched between the surface's highest and
    // lowest heights, then refined.
    if (mapped && camera.y > hiz_water.map_counts.y) {
        float first = max((hiz_water.map_counts.z - camera.y) / ray.y, 0.0);
        float last = (hiz_water.map_counts.y - camera.y) / ray.y;
        if (nearest != 0.0) last = min(last, nearest);
        if (!(isnan(scene_depth) || isinf(scene_depth))) last = min(last, scene_depth);
        last = min(last, pc.far_plane);
        const int steps = 64;
        float above = first;
        bool found = false;
        // Samples outside the water cells' bounds (hiz_water.map_extent, a
        // texel wider than the cells: a rounded edge still lies outside) meet
        // no water cell: the search would only move `above` past them. They
        // are skipped without a fetch, `above` kept as the search leaves it,
        // and the search ends after the last sample inside, so the samples
        // and the refinement below are unchanged.
        float enter = first, leave = last;
        for (int axis = 0; axis < 2; ++axis) {
            float origin = axis == 0 ? camera.x : camera.z;
            float direction = axis == 0 ? ray.x : ray.z;
            float low = hiz_water.map_extent[axis], high = hiz_water.map_extent[axis + 2];
            if (abs(direction) > 1.0e-6) {
                float t0 = (low - origin) / direction, t1 = (high - origin) / direction;
                enter = max(enter, min(t0, t1));
                leave = min(leave, max(t0, t1));
            } else if (origin < low || origin > high) {
                leave = -1.0;
            }
        }
        for (int i = 1; first < last && i <= steps; ++i) {
            float depth = mix(first, last, float(i) / float(steps));
            if (depth < enter) {
                above = depth;
                continue;
            }
            if (depth > leave) break;
            vec3 position = camera + ray * depth;
            float surface = hiz_water_map_surface(position.xz);
            if (surface > -1.0e29 && position.y <= surface) {
                // Between the last point above (or off) the surface and this one.
                float below = depth;
                for (int k = 0; k < 6; ++k) {
                    float middle = 0.5 * (above + below);
                    vec3 at = camera + ray * middle;
                    float height = hiz_water_map_surface(at.xz);
                    if (height > -1.0e29 && at.y <= height) below = middle;
                    else above = middle;
                }
                found = true;
                above = below;
                break;
            }
            above = depth;
        }
        if (found) {
            vec3 position = camera + ray * above;
            float height = hiz_water_map_surface(position.xz);
            position.y = height > -1.0e29 ? height : position.y;
            // Inside a piece (all four texels on this surface): water. At its
            // edge (some): where a water fragment marked the guide, the drawn
            // edge. Over no bed (sky, the far plane) only the drawn water counts.
            float coverage = hiz_water_map_sample(position.xz, position.y).x;
            bool bed = scene_depth < pc.far_plane;
            if (coverage > 0.0 && (coverage >= 0.999 && bed || hiz_water_marked(uv)) &&
                (nearest == 0.0 || above < nearest)) {
                nearest = above;
                world_position = position;
            }
        }
    }
    // Something stands on the water (in front of it): not water there.
    return nearest > 0.0 && nearest < scene_depth - 1.0 ? nearest : 0.0;
}
// A lip: lower water near this piece (it pours over its edge there), where a
// shallow bed is the brink of a drop, not a shore. Lower than this surface
// continued (a stream running downhill is no lip).
bool hiz_water_lip(vec2 xz, float height) {
    if (hiz_water.map_info.w <= 0.0) return false;
    vec2 slope = hiz_water_map_texel(ivec2(floor((xz - hiz_water.map_info.xy) / hiz_water.map_info.z))).zw;
    for (int i = 0; i < 16; ++i) {
        float angle = 0.7853982 * float(i & 7);
        float reach = hiz_water_map_reach(i < 8 ? kHiZWaterLipNear : kHiZWaterLipFar);
        vec2 at = xz + reach * vec2(cos(angle), sin(angle));
        float there = hiz_water_map_surface(at);
        if (there > -1.0e29 && there < height + dot(slope, at - xz) - 4.0) return true;
    }
    return false;
}
vec2 hiz_water_slope(vec2 position, float footprint) {
    const vec3 waves[8] = vec3[8](vec3(157.0, 0.0, 0.050), vec3(113.0, 37.0, 0.045), vec3(83.0, -29.0, 0.040),
                                  vec3(61.0, 71.0, 0.035), vec3(43.0, -63.0, 0.030), vec3(31.0, 113.0, 0.025),
                                  vec3(23.0, -101.0, 0.020), vec3(17.0, 151.0, 0.020));
    float time = hiz_water.parameters.y;
    vec2 slope = vec2(0.0);
    for (int wave = 0; wave < 8; ++wave) {
        float wavelength = waves[wave].x;
        float fade = clamp(1.5 - 3.0 * footprint / wavelength, 0.0, 1.0);
        if (fade <= 0.0) continue;
        float angle = 0.38 + radians(waves[wave].y);
        vec2 direction = vec2(cos(angle), sin(angle));
        float number = 6.2831853 / wavelength;
        float speed = 0.33 * sqrt(981.0 / number);
        float phase = number * (dot(direction, position) - speed * time) + float(wave) * 1.618;
        slope += direction * (waves[wave].z * cos(phase) * fade);
    }
    // Movers' rings (the surface shader's).
    float ring_fade = clamp(1.5 - 3.0 * footprint / 12.0, 0.0, 1.0);
    for (int i = 0; ring_fade > 0.0 && i < 16; ++i) {
        vec4 ring = hiz_water.rings[i];
        float age = time - ring.z;
        if (ring.w <= 0.0 || age < 0.0 || age > 3.0) continue;
        vec2 offset = position - ring.xy;
        float distance_world = length(offset);
        float front = distance_world - (6.0 + 55.0 * age);
        float width = 6.0 + 5.0 * age;
        float envelope = ring.w * exp(-1.2 * age) * exp(-0.5 * front * front / (width * width));
        slope += offset / max(distance_world, 1.0e-3) * (0.9 * ring_fade * envelope * sin(0.5236 * front));
    }
    return slope;
}
// The rippled surface normal in view space.
vec3 hiz_water_normal(vec3 world_position, float depth, vec3 direction) {
    vec3 ray = normalize(hiz_water_to_world(direction));
    float footprint = depth * 2.0 * max(pc.inverse_size.x / abs(pc.projection_scale_x),
                                        pc.inverse_size.y / abs(pc.projection_scale_y)) /
                      max(-ray.y, 0.05);
    vec2 slope = hiz_water_slope(world_position.xz, footprint);
    vec3 world_normal = vec3(-slope.x, 1.0, -slope.y);
    return normalize(hiz_water.view_to_world[0].xyz * world_normal.x +
                     hiz_water.view_to_world[1].xyz * world_normal.y +
                     hiz_water.view_to_world[2].xyz * world_normal.z);
}
float hiz_water_fresnel(float cosine) {
    float f0 = hiz_water.parameters.z;
    float grazing = 1.0 - clamp(cosine, 0.0, 1.0);
    float grazing2 = grazing * grazing;
    return f0 + (1.0 - f0) * grazing2 * grazing2 * grazing;
}
)glsl";
}

std::string BuildHiZReflectionComputeShader() {
    std::string source = R"glsl(#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform texture2D immutable_color;
layout(set = 0, binding = 1) uniform texture2D hiz_depth;
layout(set = 0, binding = 2) uniform texture2D normal_guide;
layout(set = 0, binding = 3) uniform texture2D material_guide;
layout(set = 0, binding = 4, rgba16f) uniform writeonly image2D reflection_output;
layout(set = 0, binding = 5) uniform sampler linear_sampler;
layout(set = 0, binding = 6) uniform sampler nearest_sampler;
)glsl";
    source += kPushBlock;
    source += EffectSurfaceCoordinateShaderLibrary;
    source += kWaterLibrary;
    source += R"glsl(
vec3 view_position(vec2 uv, float view_depth) {
    vec2 ndc = oot3d_surface_view_ndc(uv, pc.surface_orientation);
    return vec3((ndc.x + pc.projection_offset_x) * view_depth /
                    pc.projection_scale_x,
                (ndc.y + pc.projection_offset_y) * view_depth /
                    pc.projection_scale_y,
                -view_depth);
}

vec2 project_view(vec3 position) {
    float inverse_w = 1.0 / max(-position.z, 1.0e-5);
    vec2 ndc = vec2(
        pc.projection_scale_x * position.x * inverse_w -
            pc.projection_offset_x,
        pc.projection_scale_y * position.y * inverse_w -
            pc.projection_offset_y);
    return oot3d_view_ndc_surface_uv(ndc, pc.surface_orientation);
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(pixel, ivec2(pc.extent)))) return;
    vec2 uv = (vec2(pixel) + 0.5) * pc.inverse_size;
    float surface_depth = textureLod(
        sampler2D(hiz_depth,nearest_sampler), uv, 0.0).r;
    bool depth_valid = !(isnan(surface_depth) || isinf(surface_depth) ||
        surface_depth <= pc.near_plane);
    vec3 water_world, water_direction;
    float water_depth = hiz_water_surface(
        uv, depth_valid ? surface_depth : 1.0e30, water_world, water_direction);
    vec4 material;
    vec3 normal;
    vec3 surface;
    float roughness;
    if (water_depth > 0.0) {
        // Water: its own surface, rippled normal and Fresnel weight.
        surface = water_direction * water_depth;
        normal = hiz_water_normal(water_world, water_depth, water_direction);
        roughness = 0.06;
        material = vec4(hiz_water_fresnel(dot(normal, -normalize(surface))), roughness, 0.0, 0.0);
    } else {
        if (hiz_water.parameters.w > 0.5) {
            imageStore(reflection_output, pixel, vec4(0.0));
            return;
        }
        material = texture(sampler2D(material_guide,linear_sampler), uv);
        vec4 normal_sample = texture(sampler2D(normal_guide,linear_sampler), uv);
        roughness = clamp(material.g + pc.roughness_bias, 0.0, 1.0);
        if (material.r <= 0.001 || normal_sample.a < 0.5 || roughness >= 1.0 ||
            !depth_valid || surface_depth >= pc.far_plane) {
            imageStore(reflection_output, pixel, vec4(0.0));
            return;
        }
        normal = normalize(normal_sample.xyz * 2.0 - 1.0);
        surface = view_position(uv, surface_depth);
    }
    vec3 incident = normalize(surface);
    vec3 ray_direction = normalize(reflect(incident, normal));
    if (ray_direction.z >= -0.001) {
        imageStore(reflection_output, pixel, vec4(0.0));
        return;
    }
    float step_length = max(pc.thickness, pc.max_distance /
                            max(float(pc.max_steps), 1.0));
    vec3 origin = surface + normal * pc.thickness;
    float travelled = step_length;
    vec2 hit_uv = vec2(-1.0);
    float confidence = 0.0;
    float previous_travelled = 0.0;
    // The last on-screen point the water's ray passed over open sky.
    vec2 sky_uv = vec2(-1.0);
    // Water traces its full step budget at full depth resolution and refines
    // where the ray crosses the scene (its long steps miss thin hits).
    uint steps = water_depth > 0.0 ? 64u : pc.max_steps;
    for (uint step_index = 0u; step_index < 64u; ++step_index) {
        if (step_index >= steps || travelled > pc.max_distance) break;
        vec3 ray_position = origin + ray_direction * travelled;
        float ray_depth = -ray_position.z;
        if (ray_depth <= pc.near_plane || ray_depth >= pc.far_plane) break;
        vec2 candidate_uv = project_view(ray_position);
        if (any(lessThanEqual(candidate_uv, vec2(0.0))) ||
            any(greaterThanEqual(candidate_uv, vec2(1.0)))) break;
        float footprint = max(travelled * max(pc.inverse_size.x,
                                              pc.inverse_size.y), 1.0);
        uint mip_index = water_depth > 0.0 ? 0u : min(uint(findMSB(uint(footprint))),
                             max(pc.hiz_mip_count, 1u) - 1u);
        float mip = float(mip_index);
        float scene_depth = textureLod(
            sampler2D(hiz_depth,nearest_sampler), candidate_uv, mip).r;
        if (water_depth > 0.0 && (isnan(scene_depth) || isinf(scene_depth) || scene_depth <= pc.near_plane ||
                                  scene_depth >= 0.98 * pc.far_plane))
            sky_uv = candidate_uv;
        float gap = ray_depth - scene_depth;
        float tolerance = pc.thickness * (1.0 + mip * 0.5);
        if (gap >= 0.0 && gap <= tolerance) {
            hit_uv = candidate_uv;
            confidence = 1.0 - travelled / pc.max_distance;
            break;
        }
        if (gap > tolerance && water_depth > 0.0) {
            // Crossed: bisect between the last point in front and this one.
            float near_travelled = previous_travelled, far_travelled = travelled;
            for (int refine = 0; refine < 6; ++refine) {
                float middle = 0.5 * (near_travelled + far_travelled);
                vec3 position = origin + ray_direction * middle;
                vec2 middle_uv = project_view(position);
                float middle_gap = -position.z - textureLod(
                    sampler2D(hiz_depth,nearest_sampler), middle_uv, 0.0).r;
                if (middle_gap >= 0.0) far_travelled = middle; else near_travelled = middle;
            }
            vec3 position = origin + ray_direction * far_travelled;
            vec2 refined_uv = project_view(position);
            float refined_gap = -position.z - textureLod(
                sampler2D(hiz_depth,nearest_sampler), refined_uv, 0.0).r;
            if (refined_gap <= max(tolerance, 25.0 + 0.05 * -position.z)) {
                hit_uv = refined_uv;
                confidence = 1.0 - 0.5 * far_travelled / pc.max_distance;
                break;
            }
        }
        previous_travelled = travelled;
        travelled += step_length * float(1u << max(int(mip_index) - 1, 0));
    }
    if (confidence <= 0.0 && water_depth > 0.0 && sky_uv.x >= 0.0) {
        // Water reflects the sky it was heading for (screen sky, not a hit).
        hit_uv = sky_uv;
        confidence = 1.0;
    }
    if (confidence <= 0.0) {
        imageStore(reflection_output, pixel, vec4(0.0));
        return;
    }
    vec2 edge_distance = min(hit_uv, vec2(1.0) - hit_uv);
    float edge = smoothstep(0.0, max(pc.edge_fade, 1.0e-4),
                            min(edge_distance.x, edge_distance.y));
    float grazing = 1.0 - abs(dot(normal, -incident));
    // Water's weight is its Fresnel reflectance; guide materials keep their
    // grazing response.
    confidence *= water_depth > 0.0 ? edge * material.r
                                    : edge * material.r * mix(0.35, 1.0, grazing) * (1.0 - roughness);
    imageStore(reflection_output, pixel,
               vec4(texture(sampler2D(immutable_color,linear_sampler),
                            hit_uv).rgb, confidence));
}
)glsl";
    return source;
}

std::string BuildHiZReflectionBilateralFilterShader() {
    std::string source = R"glsl(#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform texture2D raw_reflection;
layout(set = 0, binding = 1) uniform texture2D hiz_depth;
layout(set = 0, binding = 2) uniform texture2D normal_guide;
layout(set = 0, binding = 3) uniform texture2D material_guide;
layout(set = 0, binding = 4, rgba16f) uniform writeonly image2D filtered_output;
layout(set = 0, binding = 5) uniform sampler linear_sampler;
layout(set = 0, binding = 6) uniform sampler nearest_sampler;
layout(set = 0, binding = 8) uniform texture2D scene_color;
)glsl";
    source += kPushBlock;
    source += Renderer3ds::PicaMaterialGuideShaderLibrary;
    source += EffectSurfaceCoordinateShaderLibrary;
    source += kWaterLibrary;
    source += R"glsl(
vec2 water_project(vec3 position) {
    float inverse_w = 1.0 / max(-position.z, 1.0e-5);
    vec2 ndc = vec2(pc.projection_scale_x * position.x * inverse_w - pc.projection_offset_x,
                    pc.projection_scale_y * position.y * inverse_w - pc.projection_offset_y);
    return oot3d_view_ndc_surface_uv(ndc, pc.surface_orientation);
}
// A drifting pattern in [0, 1] (world units): warped directional sines, no
// lattice (cell noise shows square tiles).
float water_pattern(vec2 p, float time) {
    float a = sin(dot(p, vec2(0.071, 0.043)) + 1.9 * sin(dot(p, vec2(-0.033, 0.058)) + 0.6 * time));
    float b = sin(dot(p, vec2(-0.052, 0.087)) + 1.5 * sin(dot(p, vec2(0.061, 0.029)) - 0.45 * time) + 1.3);
    float c = sin(dot(p, vec2(0.113, -0.097)) + 1.2 * sin(dot(p, vec2(0.021, 0.104)) + 0.8 * time) + 2.1);
    return 0.5 + 0.2 * a + 0.17 * b + 0.13 * c;
}
// The water as a medium (hiz_water.medium): what is seen through the
// surface, displaced by its ripples (refraction) and absorbed along the
// view path below it, with foam where the water is shallow (shores, around
// what stands in it); the reflection then by its Fresnel weight.
vec3 water_compose(vec2 uv, vec3 world, vec3 direction, float water_depth, float scene_depth,
                   vec3 reflection, float reflection_weight) {
    vec3 normal = hiz_water_normal(world, water_depth, direction);
    vec3 flat_normal = normalize(hiz_water.view_to_world[1].xyz);
    vec3 ray_world = hiz_water_to_world(direction);
    bool bed = !(isnan(scene_depth) || isinf(scene_depth)) && scene_depth < pc.far_plane;
    // Depth below the surface (vertical) and the view path through it.
    float below = bed ? max(scene_depth - water_depth, 0.0) : 1.0e4;
    float column = below * abs(ray_world.y);
    float path = below * length(ray_world);
    // Refraction: the view of the bed shifts with the slope, more where the
    // water is deeper; a shifted sample in front of the water is not its bed.
    vec3 surface = direction * water_depth;
    // Near a piece's edge the scene behind holds the water's own edge: no
    // shift there (it would show a rippled ghost of the edge).
    float edge_scale = 1.0;
    if (hiz_water.map_info.w > 0.0) {
        float reach = hiz_water_map_reach(kHiZWaterEdgeFade);
        for (int i = 0; i < 4; ++i) {
            vec2 offset = reach * vec2(i == 0 ? 1.0 : i == 1 ? -1.0 : 0.0, i == 2 ? 1.0 : i == 3 ? -1.0 : 0.0);
            edge_scale = min(edge_scale, clamp(2.0 * hiz_water_map_sample(world.xz + offset, world.y).x - 1.0, 0.0, 1.0));
        }
        // Water the map does not hold (a gameplay box): unchanged.
        if (hiz_water_map_sample(world.xz, world.y).x < 0.5) edge_scale = 1.0;
    }
    vec2 refracted_uv = water_project(surface + (normal - flat_normal) * hiz_water.medium.y * edge_scale *
                                                    min(column, 80.0));
    vec2 sample_uv = uv;
    if (all(greaterThan(refracted_uv, vec2(0.0))) && all(lessThan(refracted_uv, vec2(1.0)))) {
        float refracted_depth = textureLod(sampler2D(hiz_depth, nearest_sampler), refracted_uv, 0.0).r;
        // The shifted view must still pass through this water: on the map,
        // where it has one (no ghost of the edge's surroundings).
        vec3 camera = vec3(hiz_water.view_to_world[0].w, hiz_water.view_to_world[1].w, hiz_water.view_to_world[2].w);
        vec3 shifted_ray = hiz_water_to_world(hiz_water_view_direction(refracted_uv));
        bool on_water = true;
        if (hiz_water.map_info.w > 0.0 && shifted_ray.y < -1.0e-6) {
            vec3 shifted = camera + shifted_ray * ((world.y - camera.y) / shifted_ray.y);
            on_water = hiz_water_map_sample(shifted.xz, world.y).x >= 0.5 ||
                       hiz_water_map_sample(world.xz, world.y).x < 0.5;
        }
        if (!(refracted_depth < water_depth + 1.0) && on_water) sample_uv = refracted_uv;
    }
    vec3 color = textureLod(sampler2D(scene_color, linear_sampler), sample_uv, 0.0).rgb;
    // Absorption (red first), never to black: the title's water colour stays.
    vec3 transmission = exp(-hiz_water.medium.x * vec3(2.6, 1.0, 0.75) * path);
    color *= mix(vec3(0.6), vec3(1.0), transmission);
    // Shore foam: broken by drifting noise, densest at the waterline.
    float width = max(hiz_water.medium.z, 1.0e-3);
    float shore = bed ? 1.0 - smoothstep(0.0, width, column) : 0.0;
    if (shore > 0.0 && hiz_water_lip(world.xz, world.y)) shore = 0.0;
    float time = hiz_water.parameters.y;
    float foam = 0.0;
    // Where a fall between pieces lands (the map's landing foam).
    float landing = hiz_water_map_sample(world.xz, world.y).y;
    // Soft churn, densest where the water falls in and fading fast; slow
    // drifting variation, no thresholds (no spots, no texel edges).
    if (landing > 0.0)
        foam = landing * landing * (0.55 + 0.45 * water_pattern(world.zx * 1.3, 2.0 * time));
    if (shore > 0.0) {
        float noise = water_pattern(world.xz, time);
        foam = max(foam, shore * shore * smoothstep(0.6, 0.9, noise + 0.45 * shore));
    }
    if (foam > 0.0) {
        // Foam scatters what light there is: white, a little dimmer in shade.
        float light = clamp(dot(color, vec3(0.299, 0.587, 0.114)) * 1.6 + 0.45, 0.6, 1.0);
        color = mix(color, vec3(light), 0.75 * foam);
    }
    return mix(color, reflection, clamp(reflection_weight, 0.0, 1.0));
}
// An 8x8 workgroup shares its two-texel halo. Keep the full 5x5 bilateral
// kernel; only redundant guide/reflection fetches and normalization disappear.
shared vec4 tile_reflection[144];
shared vec4 tile_normal[144];
shared float tile_depth[144];
shared uint tile_active;
void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (gl_LocalInvocationIndex == 0u) tile_active = 0u;
    barrier();
    bool inside = all(lessThan(pixel, ivec2(pc.extent)));
    vec2 uv = (vec2(pixel) + 0.5) * pc.inverse_size;
    vec4 material = textureLod(sampler2D(material_guide,linear_sampler), uv, 0.0);
    vec4 center_guide = textureLod(sampler2D(normal_guide,linear_sampler), uv, 0.0);
    float reflection_weight = pica_reflection_weight(material);
    float center_scene_depth = textureLod(sampler2D(hiz_depth,nearest_sampler), uv, 0.0).r;
    vec3 water_world, water_direction;
    float water_depth = inside ? hiz_water_surface(uv, isnan(center_scene_depth) || isinf(center_scene_depth)
        ? 1.0e30 : center_scene_depth, water_world, water_direction) : 0.0;
    bool water = water_depth > 0.0;
    bool participates = water || (inside && hiz_water.parameters.w <= 0.5 && material.r > 0.001 &&
        reflection_weight > 0.0 && center_guide.a >= 0.5);
    if (participates) atomicOr(tile_active, 1u);
    barrier();
    if (tile_active == 0u) {
        if (inside) imageStore(filtered_output, pixel, vec4(0.0));
        return;
    }
    ivec2 origin = ivec2(gl_WorkGroupID.xy * 8u) - 2;
    for (uint i = gl_LocalInvocationIndex; i < 144u; i += 64u) {
        ivec2 q = origin + ivec2(int(i) % 12, int(i) / 12);
        vec2 sample_uv = clamp((vec2(q) + 0.5) * pc.inverse_size,
                               vec2(0.0), vec2(1.0));
        tile_reflection[i] = textureLod(
            sampler2D(raw_reflection,linear_sampler), sample_uv, 0.0);
        vec4 normal = textureLod(
            sampler2D(normal_guide,linear_sampler), sample_uv, 0.0);
        tile_normal[i] = vec4(normalize(normal.xyz * 2.0 - 1.0), normal.a);
        tile_depth[i] = textureLod(
            sampler2D(hiz_depth,nearest_sampler), sample_uv, 0.0).r;
    }
    barrier();
    if (any(greaterThanEqual(pixel, ivec2(pc.extent)))) return;
    int center_index = (int(gl_LocalInvocationID.y) + 2) * 12 +
                       int(gl_LocalInvocationID.x) + 2;
    vec4 center_normal_sample = tile_normal[center_index];
    if (!participates) {
        imageStore(filtered_output, pixel, vec4(0.0));
        return;
    }
    if (water) {
        // Water: the raw hits' confidence weighs a plain spatial kernel (the
        // guides under transparent water describe its bed).
        vec3 water_color = vec3(0.0);
        float water_confidence = 0.0, water_total = 0.0;
        for (int y = -2; y <= 2; ++y) {
            for (int x = -2; x <= 2; ++x) {
                vec4 reflection = tile_reflection[center_index + y * 12 + x];
                float spatial_weight = exp(-0.5 * float(x * x + y * y));
                water_total += spatial_weight;
                water_color += reflection.rgb * reflection.a * spatial_weight;
                water_confidence += reflection.a * spatial_weight;
            }
        }
        vec3 reflection = water_confidence > 1.0e-5 ? water_color / water_confidence : vec3(0.0);
        float weight = water_confidence / water_total;
        if (hiz_water.medium.w > 0.0) {
            // The composed water over the scene at its opacity (medium.w; the
            // composite's strength scales the weight: undone).
            imageStore(filtered_output, pixel,
                       vec4(water_compose(uv, water_world, water_direction, water_depth, center_scene_depth,
                                          reflection, weight), hiz_water.medium.w / max(pc.strength, 1.0e-3)));
            return;
        }
        imageStore(filtered_output, pixel, vec4(reflection, weight));
        return;
    }
    float center_depth = tile_depth[center_index];
    vec3 center_normal = center_normal_sample.xyz;
    vec3 weighted_color = vec3(0.0);
    float weighted_confidence = 0.0;
    float total_weight = 0.0;
    float valid_weight = 0.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            int index = center_index + y * 12 + x;
            vec4 reflection = tile_reflection[index];
            float sample_depth = tile_depth[index];
            vec4 sample_normal_value = tile_normal[index];
            vec3 sample_normal = sample_normal_value.xyz;
            float spatial_weight = exp(-0.5 * float(x * x + y * y));
            float depth_weight = exp(-abs(sample_depth - center_depth) /
                                     max(pc.thickness, 1.0e-3));
            float normal_weight = pow(max(dot(center_normal, sample_normal), 0.0),
                                      max(pc.normal_sigma, 1.0));
            float weight = spatial_weight * depth_weight * normal_weight *
                           step(0.5, sample_normal_value.a);
            total_weight += weight;
            float confidence_weight = weight * reflection.a;
            weighted_color += reflection.rgb * confidence_weight;
            weighted_confidence += confidence_weight;
            valid_weight += confidence_weight;
        }
    }
    vec3 color = valid_weight > 1.0e-5
        ? weighted_color / valid_weight : vec3(0.0);
    float confidence = total_weight > 1.0e-5
        ? weighted_confidence / total_weight : 0.0;
    imageStore(filtered_output, pixel, vec4(color, confidence * reflection_weight));
}
)glsl";
    return source;
}

HiZWaterUniforms BuildHiZWaterUniforms(const std::array<float, 16>* worldToView,
                                       const Renderer3ds::PicaSceneWaterBodies* bodies, float seconds,
                                       bool waterOnly,
                                       const std::array<Renderer3ds::PicaWaterRing, Renderer3ds::kPicaWaterRings>* rings,
                                       const Renderer3ds::PicaWaterMap* map) {
    HiZWaterUniforms result;
    if (map != nullptr && map->Size != 0U) {
        result.MapInfo = {map->Origin[0], map->Origin[1], map->Texel, static_cast<float>(map->Size)};
        for (size_t i = 0; i < std::min<size_t>(map->Levels.size(), Renderer3ds::kPicaWaterMapLevels); ++i)
            result.MapLevels[i / 4U][i % 4U] = map->Levels[i];
        result.MapCounts[0] = static_cast<float>(std::min<size_t>(map->Levels.size(),
                                                                   Renderer3ds::kPicaWaterMapLevels));
        result.MapCounts[1] = map->HeightRange[0];
        result.MapCounts[2] = map->HeightRange[1];
        // A texel of margin: a sample the search would place in an edge
        // cell by rounding still lies inside.
        result.MapExtent = {map->ExtentMinimumXZ[0] - map->Texel, map->ExtentMinimumXZ[1] - map->Texel,
                            map->ExtentMaximumXZ[0] + map->Texel, map->ExtentMaximumXZ[1] + map->Texel};
    }
    if (rings != nullptr)
        for (uint32_t i = 0; i < Renderer3ds::kPicaWaterRings; ++i)
            result.Rings[i] = {(*rings)[i].CenterXZ[0], (*rings)[i].CenterXZ[1], (*rings)[i].Birth,
                               (*rings)[i].Strength};
    result.Parameters = {0.0F, std::fmod(std::max(seconds, 0.0F), 3600.0F), 0.02F, waterOnly ? 1.0F : 0.0F};
    result.Medium = {0.0022F, 3.0F, 4.0F, 0.6F};
    if (worldToView == nullptr || bodies == nullptr) return result;
    // Column-major world-to-view: rows of its linear part and translation.
    std::array<std::array<float, 3>, 3> linear{};
    std::array<float, 3> translation{};
    for (size_t row = 0; row < 3U; ++row) {
        for (size_t column = 0; column < 3U; ++column) linear[row][column] = (*worldToView)[column * 4U + row];
        translation[row] = (*worldToView)[12U + row];
    }
    const auto& m = linear;
    const float determinant = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
                              m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                              m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    if (!std::isfinite(determinant) || std::abs(determinant) < 1.0e-12F) return result;
    std::array<std::array<float, 3>, 3> inverse{};
    inverse[0] = {(m[1][1] * m[2][2] - m[1][2] * m[2][1]) / determinant,
                  (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / determinant,
                  (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / determinant};
    inverse[1] = {(m[1][2] * m[2][0] - m[1][0] * m[2][2]) / determinant,
                  (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / determinant,
                  (m[0][2] * m[1][0] - m[0][0] * m[1][2]) / determinant};
    inverse[2] = {(m[1][0] * m[2][1] - m[1][1] * m[2][0]) / determinant,
                  (m[0][1] * m[2][0] - m[0][0] * m[2][1]) / determinant,
                  (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / determinant};
    std::array<float, 3> camera{};
    for (size_t row = 0; row < 3U; ++row) {
        for (size_t column = 0; column < 3U; ++column) camera[row] -= inverse[row][column] * translation[column];
        result.ViewToWorld[row] = {inverse[row][0], inverse[row][1], inverse[row][2], camera[row]};
    }
    // Bodies seen from above, nearest footprint first.
    std::array<std::pair<float, uint32_t>, Renderer3ds::kMaximumPicaSceneWaterBodies> order{};
    uint32_t count = 0;
    for (uint32_t i = 0; i < std::min(bodies->Count, Renderer3ds::kMaximumPicaSceneWaterBodies); ++i) {
        const auto& body = bodies->Bodies[i];
        if (camera[1] <= body.SurfaceY) continue;
        const float dx = std::max({body.MinimumXZ[0] - camera[0], 0.0F, camera[0] - body.MaximumXZ[0]});
        const float dz = std::max({body.MinimumXZ[1] - camera[2], 0.0F, camera[2] - body.MaximumXZ[1]});
        order[count++] = {dx * dx + dz * dz + (camera[1] - body.SurfaceY) * (camera[1] - body.SurfaceY), i};
    }
    std::sort(order.begin(), order.begin() + count);
    count = std::min(count, kHiZWaterPlanes);
    for (uint32_t slot = 0; slot < count; ++slot) {
        const auto& body = bodies->Bodies[order[slot].second];
        result.Footprints[slot] = {body.MinimumXZ[0], body.MinimumXZ[1], body.MaximumXZ[0], body.MaximumXZ[1]};
        result.Heights[slot / 4U][slot % 4U] = body.SurfaceY;
    }
    result.Parameters[0] = static_cast<float>(count);
    return result;
}

bool HiZWaterPossiblyInView(const std::array<float, 16>* worldToView, const std::array<float, 16>& projection,
                            float farPlane, const Renderer3ds::PicaSceneWaterBodies* bodies,
                            const Renderer3ds::PicaWaterMap* map) {
    if (worldToView == nullptr) return true;
    // The trace's projection (project_view): ndc = scale * xy / w - offset,
    // w = -z. A view point is on screen when both ndc are within [-1, 1];
    // each bound is linear in view space once w is positive.
    const float sx = projection[0], sy = projection[5], ox = projection[8], oy = projection[9];
    const auto boxInView = [&](const std::array<float, 3>& low, const std::array<float, 3>& high,
                               bool farBounded) {
        std::array<uint32_t, 6> outside{};
        for (uint32_t corner = 0; corner < 8U; ++corner) {
            const std::array<float, 3> world{(corner & 1U) ? high[0] : low[0], (corner & 2U) ? high[1] : low[1],
                                             (corner & 4U) ? high[2] : low[2]};
            std::array<float, 3> view{};
            for (size_t row = 0; row < 3U; ++row)
                view[row] = (*worldToView)[row] * world[0] + (*worldToView)[4U + row] * world[1] +
                            (*worldToView)[8U + row] * world[2] + (*worldToView)[12U + row];
            const float w = -view[2];
            const std::array<float, 6> planes{w,
                                              sx * view[0] - (ox - 1.0F) * w,
                                              (ox + 1.0F) * w - sx * view[0],
                                              sy * view[1] - (oy - 1.0F) * w,
                                              (oy + 1.0F) * w - sy * view[1],
                                              farBounded ? farPlane - w : 1.0F};
            for (size_t plane = 0; plane < planes.size(); ++plane)
                if (planes[plane] < 0.0F) ++outside[plane];
        }
        // Every corner beyond one plane: the box is out of view. Anything
        // else counts as visible (a box straddling a corner of the pyramid
        // may still be outside; the search then finds nothing).
        return std::none_of(outside.begin(), outside.end(), [](uint32_t count) { return count == 8U; });
    };
    if (bodies != nullptr) {
        for (uint32_t i = 0; i < std::min(bodies->Count, Renderer3ds::kMaximumPicaSceneWaterBodies); ++i) {
            const auto& body = bodies->Bodies[i];
            if (boxInView({body.MinimumXZ[0], body.SurfaceY - Renderer3ds::kPicaWaterSurfaceHeightTolerance,
                           body.MinimumXZ[1]},
                          {body.MaximumXZ[0], body.SurfaceY + Renderer3ds::kPicaWaterSurfaceHeightTolerance,
                           body.MaximumXZ[1]},
                          false))
                return true;
        }
    }
    if (map != nullptr && map->Size != 0U && !map->Levels.empty() &&
        boxInView({map->ExtentMinimumXZ[0] - map->Texel, map->HeightRange[0], map->ExtentMinimumXZ[1] - map->Texel},
                  {map->ExtentMaximumXZ[0] + map->Texel, map->HeightRange[1], map->ExtentMaximumXZ[1] + map->Texel},
                  true))
        return true;
    return false;
}

} // namespace Fast::Oot3d
